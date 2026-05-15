#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/ArrowMarketData.h"
#include "domain/factor/include/FactorBacktestCachedBarUtils.h"
#include "domain/factor/include/FactorBacktestGroupingUtils.h"
#include "domain/factor/include/FactorBacktestIcUtils.h"

#include <algorithm>
#include <deque>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <future>
#include <iterator>
#include <set>
#include <limits>
#include <map>
#include <thread>
#include <sstream>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <QDateTime>
#include <QElapsedTimer>

namespace factor {

namespace {

QString threadIdText()
{
    const auto threadId = std::this_thread::get_id();
    std::ostringstream stream;
    stream << threadId;
    return QString::fromStdString(stream.str());
}

std::string resolveMarketDataIdentityKey(const factor::BacktestConfig& config);

QString rangeText(size_t beginIndex, size_t endIndex)
{
    return QStringLiteral("[%1,%2)").arg(beginIndex).arg(endIndex);
}

std::string buildMarketContextCacheKey(const factor::BacktestConfig& config)
{
    std::vector<std::string> allowedStockCodes = config.allowedStockCodes;
    std::sort(allowedStockCodes.begin(), allowedStockCodes.end());

    std::ostringstream stream;
    stream << resolveMarketDataIdentityKey(config) << '|'
           << config.startDate << '|'
           << config.endDate << '|';
    if (allowedStockCodes.empty()) {
        stream << '*';
    } else {
        for (const auto& stockCode : allowedStockCodes) {
            stream << stockCode << ';';
        }
    }
    return stream.str();
}

bool hasPreparedHistoricalData(const factor::BacktestConfig& config)
{
    return static_cast<bool>(config.preparedArrowData) || !config.cachedBars.empty();
}

std::string resolveMarketDataIdentityKey(const factor::BacktestConfig& config)
{
    if (!config.marketDataCacheKey.empty()) {
        return config.marketDataCacheKey;
    }

    std::ostringstream stream;
    stream << config.datasetId << '|' << config.cachedBars.size() << '|'
           << config.startDate << '|' << config.endDate;
    return stream.str();
}

std::string buildCachedMarketIndexKey(const factor::BacktestConfig& config)
{
    return resolveMarketDataIdentityKey(config);
}

std::string buildFutureReturnCacheKey(const std::string& symbol,
                                     const std::string& startDate,
                                     int forwardDays,
                                     const factor::BacktestConfig& config)
{
    std::ostringstream stream;
    stream << resolveMarketDataIdentityKey(config) << '|'
           << forwardDays << '|' << symbol << '|' << startDate;
    return stream.str();
}

std::unordered_map<std::string, std::vector<double>> precomputeFutureReturns(
    const std::vector<factor::CachedMarketBar>& cachedBars,
    int forwardDays)
{
    std::unordered_map<std::string, std::vector<double>> futureReturns;
    if (cachedBars.empty()) {
        return futureReturns;
    }

    std::unordered_map<std::string, std::vector<std::pair<std::string, double>>> closeBySymbol;
    std::unordered_set<std::string> tradeDateSet;
    closeBySymbol.reserve(cachedBars.size());
    tradeDateSet.reserve(cachedBars.size());

    for (const auto& bar : cachedBars) {
        const std::string normalizedTradeDate = factor::cached_bars::normalizeTradeDate(bar.tradeDate);
        if (normalizedTradeDate.empty() || bar.symbol.empty() || !std::isfinite(bar.close) || bar.close <= 0.0) {
            continue;
        }

        tradeDateSet.insert(normalizedTradeDate);
        closeBySymbol[bar.symbol].emplace_back(normalizedTradeDate, bar.close);
    }

    std::vector<std::string> tradeDates(tradeDateSet.begin(), tradeDateSet.end());
    std::sort(tradeDates.begin(), tradeDates.end());

    std::unordered_map<std::string, size_t> dateIndexByDate;
    dateIndexByDate.reserve(tradeDates.size());
    for (size_t index = 0; index < tradeDates.size(); ++index) {
        dateIndexByDate.emplace(tradeDates[index], index);
    }

    const size_t dateCount = tradeDates.size();
    for (auto& [symbol, series] : closeBySymbol) {
        std::sort(series.begin(), series.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

        auto& returns = futureReturns[symbol];
        returns.assign(dateCount, std::numeric_limits<double>::quiet_NaN());
        if (forwardDays <= 0 || series.size() <= static_cast<size_t>(forwardDays)) {
            continue;
        }

        for (size_t index = 0; index + static_cast<size_t>(forwardDays) < series.size(); ++index) {
            const auto startDateIt = dateIndexByDate.find(series[index].first);
            if (startDateIt == dateIndexByDate.end()) {
                continue;
            }

            const double startClose = series[index].second;
            const double endClose = series[index + static_cast<size_t>(forwardDays)].second;
            if (!std::isfinite(startClose) || !std::isfinite(endClose) || startClose <= 0.0 || endClose <= 0.0) {
                continue;
            }

            returns[startDateIt->second] = (endClose - startClose) / startClose;
        }
    }

    return futureReturns;
}

double lookupPrecomputedFutureReturn(
    const std::unordered_map<std::string, std::vector<double>>& futureReturnsBySymbol,
    const std::shared_ptr<ArrowMarketData>& arrowData,
    const std::string& symbol,
    const std::string& date)
{
    if (!arrowData) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int dateIdx = arrowData->dateIndex(date);
    const int symIdx = arrowData->symbolIndex(symbol);
    if (dateIdx < 0 || symIdx < 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const auto futureIt = futureReturnsBySymbol.find(symbol);
    if (futureIt == futureReturnsBySymbol.end()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const auto& returns = futureIt->second;
    if (dateIdx >= static_cast<int>(returns.size())) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return returns[static_cast<size_t>(dateIdx)];
}

std::unordered_map<std::string, int> buildDateIndexMap(const ArrowMarketData& arrowData)
{
    std::unordered_map<std::string, int> dateToIndex;
    const auto& dates = arrowData.dates();
    dateToIndex.reserve(dates.size());
    for (size_t index = 0; index < dates.size(); ++index) {
        dateToIndex.emplace(dates[index], static_cast<int>(index));
    }
    return dateToIndex;
}

struct CachedReturnAggregationState {
    std::vector<double> icSeries;
    std::vector<double> longShortSeries;
    std::vector<double> turnoverSeries;
    std::vector<std::string> longShortDates;
    std::vector<double> aggregatedReturns;
    std::vector<int> aggregatedCounts;
    std::vector<int> aggregatedStockCounts;
    std::vector<double> aggregatedMinFactorValues;
    std::vector<double> aggregatedMaxFactorValues;
    std::vector<std::string> previousLongSymbols;
    std::vector<std::string> previousShortSymbols;
    std::vector<std::vector<std::string>> activeGroupSymbols;
    size_t overlapDateCount = 0;
    int groupedDateCount = 0;
    int maxEffectiveGroupCount = 0;
    size_t maxMatchedStocks = 0;
    bool hasValidGroup = false;
    bool hasAnyFactorResult = false;
    int holdingDaysSinceRebalance = 0;
};

void initializeCachedReturnAggregationState(const BacktestConfig& config,
                                            CachedReturnAggregationState& state)
{
    const int groupCount = (std::max)(1, config.numGroups);
    const int rebalanceInterval = (std::max)(1, config.rebalanceDays);
    state.icSeries.clear();
    state.longShortSeries.clear();
    state.turnoverSeries.clear();
    state.longShortDates.clear();
    state.aggregatedReturns.assign(static_cast<size_t>(groupCount), 0.0);
    state.aggregatedCounts.assign(static_cast<size_t>(groupCount), 0);
    state.aggregatedStockCounts.assign(static_cast<size_t>(groupCount), 0);
    state.aggregatedMinFactorValues.assign(static_cast<size_t>(groupCount), std::numeric_limits<double>::max());
    state.aggregatedMaxFactorValues.assign(static_cast<size_t>(groupCount), std::numeric_limits<double>::lowest());
    state.previousLongSymbols.clear();
    state.previousShortSymbols.clear();
    state.activeGroupSymbols.clear();
    state.overlapDateCount = 0;
    state.groupedDateCount = 0;
    state.maxEffectiveGroupCount = 0;
    state.maxMatchedStocks = 0;
    state.hasValidGroup = false;
    state.hasAnyFactorResult = false;
    state.holdingDaysSinceRebalance = rebalanceInterval;
}

bool aggregateSingleResultWithPrecomputedReturns(
    const CalculationResult& factorResult,
    const std::unordered_map<std::string, std::vector<double>>& futureReturnsBySymbol,
    const std::unordered_map<std::string, int>& dateToIndex,
    const BacktestConfig& config,
    CachedReturnAggregationState& state,
    const FactorBacktestExecutor::CachedMarketIndex* cachedMarketIndex,
    std::string* failureReason)
{
    const int groupCount = (std::max)(1, config.numGroups);
    const int rebalanceInterval = (std::max)(1, config.rebalanceDays);
    state.hasAnyFactorResult = true;

    const auto dateIt = dateToIndex.find(factorResult.date);
    if (dateIt == dateToIndex.end()) {
        if (failureReason) {
            failureReason->clear();
        }
        return true;
    }

    const size_t dateIdx = static_cast<size_t>(dateIt->second);
    std::vector<double> factorValues;
    std::vector<double> returnValues;
    std::vector<std::pair<std::string, double>> rankedValues;
    factorValues.reserve(factorResult.values.size());
    returnValues.reserve(factorResult.values.size());
    rankedValues.reserve(factorResult.values.size());

    const auto resolveFutureReturn = [&](const std::string& symbol) {
        double futureReturn = std::numeric_limits<double>::quiet_NaN();
        const auto futureIt = futureReturnsBySymbol.find(symbol);
        if (futureIt != futureReturnsBySymbol.end()) {
            const auto& returns = futureIt->second;
            if (dateIdx < returns.size()) {
                futureReturn = returns[dateIdx];
            }
        } else if (cachedMarketIndex) {
            const auto symbolIt = cachedMarketIndex->closeSeriesBySymbol.find(symbol);
            if (symbolIt != cachedMarketIndex->closeSeriesBySymbol.end()) {
                const auto& series = symbolIt->second;
                const auto startIndexIt = std::lower_bound(series.begin(), series.end(), factorResult.date,
                    [](const FactorBacktestExecutor::CachedMarketIndex::CachedSymbolBar& bar, const std::string& value) {
                        return bar.tradeDate < value;
                    });
                if (startIndexIt != series.end() && startIndexIt->tradeDate == factorResult.date) {
                    futureReturn = startIndexIt->futureReturn;
                }
            }
        }
        return futureReturn;
    };

    for (const auto& [symbol, factorValue] : factorResult.values) {
        const double futureReturn = resolveFutureReturn(symbol);
        if (!std::isfinite(futureReturn)) {
            continue;
        }

        factorValues.push_back(factorValue);
        returnValues.push_back(futureReturn);
        rankedValues.emplace_back(symbol, factorValue);
    }

    if (factorValues.size() >= 2) {
        state.icSeries.push_back(factor::icir::calculateCorrelation(factorValues, returnValues));
    }

    if (rankedValues.empty()) {
        if (failureReason) {
            failureReason->clear();
        }
        return true;
    }

    ++state.overlapDateCount;
    state.maxMatchedStocks = (std::max)(state.maxMatchedStocks, rankedValues.size());

    const bool shouldRebalance = state.activeGroupSymbols.empty() || state.holdingDaysSinceRebalance >= rebalanceInterval;
    if (shouldRebalance) {
        if (rankedValues.size() < 2) {
            if (!state.activeGroupSymbols.empty()) {
                ++state.holdingDaysSinceRebalance;
            }
            if (failureReason) {
                failureReason->clear();
            }
            return true;
        }

        const int effectiveGroupCount = (std::max)(1, (std::min)(groupCount, static_cast<int>(rankedValues.size())));
        const std::size_t groupSize = (std::max)(static_cast<std::size_t>(1), rankedValues.size() / static_cast<std::size_t>(effectiveGroupCount));
        std::vector<std::vector<std::string>> proposedGroupSymbols(
            static_cast<size_t>(effectiveGroupCount),
            std::vector<std::string>());

        auto rankCompare = [](const auto& lhs, const auto& rhs) {
            return lhs.second > rhs.second;
        };
        using RankedValueIterator = std::vector<std::pair<std::string, double>>::iterator;

        for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
            const size_t begin = static_cast<size_t>(groupIndex) * groupSize;
            const size_t end = groupIndex == effectiveGroupCount - 1
                ? rankedValues.size()
                : (std::min)(rankedValues.size(), begin + groupSize);

            if (begin >= end) {
                continue;
            }

            if (end < rankedValues.size()) {
                std::nth_element(rankedValues.begin() + static_cast<RankedValueIterator::difference_type>(begin),
                                 rankedValues.begin() + static_cast<RankedValueIterator::difference_type>(end),
                                 rankedValues.end(),
                                 rankCompare);
            }

            auto& groupSymbols = proposedGroupSymbols[static_cast<size_t>(groupIndex)];
            groupSymbols.reserve(end - begin);
            for (size_t valueIndex = begin; valueIndex < end; ++valueIndex) {
                groupSymbols.push_back(rankedValues[valueIndex].first);
            }
        }

        const std::unordered_map<std::string, double> factorValuesBySymbol(factorResult.values.begin(), factorResult.values.end());
        const bool passesSignalThreshold = factor::group_backtest::passesSignalChangeThreshold(
            factorValuesBySymbol,
            state.previousLongSymbols,
            state.previousShortSymbols,
            proposedGroupSymbols.front(),
            proposedGroupSymbols.back(),
            config.signalChangeThresholdStdMultiplier);
        const bool passesMaxTurnover = factor::group_backtest::passesTurnoverLimit(
            state.previousLongSymbols,
            state.previousShortSymbols,
            proposedGroupSymbols.front(),
            proposedGroupSymbols.back(),
            config.enableTurnoverLimit,
            config.maxRebalanceTurnover);

        if (passesSignalThreshold && passesMaxTurnover) {
            state.activeGroupSymbols = std::move(proposedGroupSymbols);
            state.holdingDaysSinceRebalance = 1;
        } else {
            ++state.holdingDaysSinceRebalance;
        }
    } else {
        ++state.holdingDaysSinceRebalance;
    }

    const int effectiveGroupCount = static_cast<int>(state.activeGroupSymbols.size());
    if (effectiveGroupCount <= 0) {
        if (failureReason) {
            failureReason->clear();
        }
        return true;
    }

    state.maxEffectiveGroupCount = (std::max)(state.maxEffectiveGroupCount, effectiveGroupCount);
    bool dateGrouped = false;
    double topGroupReturnForDate = 0.0;
    double bottomGroupReturnForDate = 0.0;
    bool hasTopGroup = false;
    bool hasBottomGroup = false;
    for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
        const auto& groupSymbols = state.activeGroupSymbols[static_cast<size_t>(groupIndex)];
        if (groupSymbols.empty()) {
            continue;
        }

        double groupReturn = 0.0;
        int sampleCount = 0;
        double minFactorValue = std::numeric_limits<double>::max();
        double maxFactorValue = std::numeric_limits<double>::lowest();
        for (const auto& symbol : groupSymbols) {
            const double futureReturn = resolveFutureReturn(symbol);
            if (!std::isfinite(futureReturn)) {
                continue;
            }

            groupReturn += futureReturn;
            ++sampleCount;

            auto factorValueIt = factorResult.values.find(symbol);
            if (factorValueIt != factorResult.values.end()) {
                minFactorValue = (std::min)(minFactorValue, factorValueIt->second);
                maxFactorValue = (std::max)(maxFactorValue, factorValueIt->second);
            }
        }

        if (sampleCount == 0) {
            continue;
        }

        const double averageGroupReturn = groupReturn / static_cast<double>(sampleCount);
        state.aggregatedReturns[static_cast<size_t>(groupIndex)] += averageGroupReturn;
        state.aggregatedCounts[static_cast<size_t>(groupIndex)] += 1;
        state.aggregatedStockCounts[static_cast<size_t>(groupIndex)] += sampleCount;
        if (minFactorValue <= maxFactorValue) {
            state.aggregatedMinFactorValues[static_cast<size_t>(groupIndex)] = (std::min)(state.aggregatedMinFactorValues[static_cast<size_t>(groupIndex)], minFactorValue);
            state.aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)] = (std::max)(state.aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)], maxFactorValue);
        }
        if (groupIndex == 0) {
            topGroupReturnForDate = averageGroupReturn;
            hasTopGroup = true;
        }
        if (groupIndex == effectiveGroupCount - 1) {
            bottomGroupReturnForDate = averageGroupReturn;
            hasBottomGroup = true;
        }
        dateGrouped = true;
    }

    if (dateGrouped) {
        ++state.groupedDateCount;
        state.hasValidGroup = true;
        if (hasTopGroup && hasBottomGroup) {
            state.longShortSeries.push_back(topGroupReturnForDate - bottomGroupReturnForDate - (2.0 * config.transactionCost));
            state.longShortDates.push_back(factorResult.date);
            const double longTurnover = factor::group_backtest::calculatePortfolioTurnover(state.previousLongSymbols, state.activeGroupSymbols.front());
            const double shortTurnover = factor::group_backtest::calculatePortfolioTurnover(state.previousShortSymbols, state.activeGroupSymbols.back());
            state.turnoverSeries.push_back((longTurnover + shortTurnover) / 2.0);
            state.previousLongSymbols = state.activeGroupSymbols.front();
            state.previousShortSymbols = state.activeGroupSymbols.back();
        }
    }

    if (failureReason) {
        failureReason->clear();
    }
    return true;
}

bool aggregateResultsWithPrecomputedReturns(
    const std::vector<CalculationResult>& factorResults,
    const std::unordered_map<std::string, std::vector<double>>& futureReturnsBySymbol,
    const std::unordered_map<std::string, int>& dateToIndex,
    const BacktestConfig& config,
    CachedReturnAggregationState& state,
    const FactorBacktestExecutor::CachedMarketIndex* cachedMarketIndex,
    std::string* failureReason)
{
    initializeCachedReturnAggregationState(config, state);

    for (const auto& factorResult : factorResults) {
        if (!aggregateSingleResultWithPrecomputedReturns(factorResult,
                                                         futureReturnsBySymbol,
                                                         dateToIndex,
                                                         config,
                                                         state,
                                                         cachedMarketIndex,
                                                         failureReason)) {
            return false;
        }
    }

    if (state.overlapDateCount == 0) {
        qWarning() << "FactorBacktestExecutor: 缓存回测未生成有效因子/收益重叠样本";
    }

    if (failureReason) {
        failureReason->clear();
    }
    return true;
}

constexpr size_t kFutureReturnCacheLimit = 200000;

std::vector<std::string> deduplicateSymbolsPreservingOrder(const std::vector<std::string>& symbols)
{
    std::vector<std::string> uniqueSymbols;
    uniqueSymbols.reserve(symbols.size());

    std::unordered_set<std::string> seen;
    seen.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        if (seen.insert(symbol).second) {
            uniqueSymbols.push_back(symbol);
        }
    }

    return uniqueSymbols;
}

uint64_t fnv1a64Append(uint64_t hash, const void* data, size_t length)
{
    constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;

    if (hash == 0) {
        hash = kFnvOffset;
    }

    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t index = 0; index < length; ++index) {
        hash ^= static_cast<uint64_t>(bytes[index]);
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t mixHash64(uint64_t value)
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

void hashString(uint64_t& hash, const std::string& value)
{
    hash = fnv1a64Append(hash, value.data(), value.size());
    static constexpr char separator = '\x1f';
    hash = fnv1a64Append(hash, &separator, 1);
}

std::string toHexString(uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string formatDoubleForKey(double value)
{
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            return "nan";
        }
        return value > 0.0 ? "inf" : "-inf";
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

std::string buildAllowedStockCodesFingerprint(std::vector<std::string> allowedStockCodes)
{
    std::sort(allowedStockCodes.begin(), allowedStockCodes.end());

    std::ostringstream stream;
    if (allowedStockCodes.empty()) {
        stream << '*';
    } else {
        for (const auto& stockCode : allowedStockCodes) {
            stream << stockCode << ';';
        }
    }
    return stream.str();
}

std::string buildCachedBarsFingerprint(const std::vector<factor::CachedMarketBar>& cachedBars)
{
    uint64_t xorHash = 0;
    uint64_t sumHash = 0;
    size_t validRowCount = 0;

    for (const auto& bar : cachedBars) {
        uint64_t rowHash = 0;
        hashString(rowHash, bar.tradeDate);
        hashString(rowHash, bar.symbol);
        hashString(rowHash, formatDoubleForKey(bar.close));

        std::vector<std::pair<std::string, double>> fields(bar.numericFields.begin(), bar.numericFields.end());
        std::sort(fields.begin(), fields.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

        for (const auto& [field, value] : fields) {
            hashString(rowHash, field);
            hashString(rowHash, formatDoubleForKey(value));
        }

        const uint64_t mixedRowHash = mixHash64(rowHash);
        xorHash ^= mixedRowHash;
        sumHash += mixedRowHash;
        ++validRowCount;
    }

    uint64_t hash = 0;
    hashString(hash, std::to_string(validRowCount));
    hash = fnv1a64Append(hash, &xorHash, sizeof(xorHash));
    hash = fnv1a64Append(hash, &sumHash, sizeof(sumHash));
    return toHexString(hash);
}

struct BenchmarkComparisonSummary {
    bool hasValidAlignment = false;
    double benchmarkAnnualReturn = 0.0;
    double excessAnnualReturn = 0.0;
    double trackingError = 0.0;
    double informationRatio = 0.0;
    double beta = 0.0;
    double alpha = 0.0;
};

BenchmarkComparisonSummary calculateBenchmarkComparison(const std::vector<double>& strategyReturns,
                                                        const std::vector<std::string>& strategyDates,
                                                        int forwardDays,
                                                        double riskFreeRate,
                                                        const std::function<double(const std::string&)>& benchmarkLookup)
{
    BenchmarkComparisonSummary summary;
    const double annualizationFactor = 252.0 / static_cast<double>((std::max)(1, forwardDays));

    size_t alignedSampleCount = 0;
    double alignedStrategySum = 0.0;
    double alignedBenchmarkSum = 0.0;
    double alignedStrategySquareSum = 0.0;
    double alignedBenchmarkSquareSum = 0.0;
    double alignedProductSum = 0.0;
    double alignedExcessSum = 0.0;
    double alignedExcessSquareSum = 0.0;

    const size_t alignedCount = (std::min)(strategyReturns.size(), strategyDates.size());
    for (size_t index = 0; index < alignedCount; ++index) {
        const double benchmarkReturn = benchmarkLookup(strategyDates[index]);
        if (!std::isfinite(benchmarkReturn)) {
            continue;
        }

        const double strategyReturn = strategyReturns[index];
        const double excessReturn = strategyReturn - benchmarkReturn;

        ++alignedSampleCount;
        alignedStrategySum += strategyReturn;
        alignedBenchmarkSum += benchmarkReturn;
        alignedStrategySquareSum += strategyReturn * strategyReturn;
        alignedBenchmarkSquareSum += benchmarkReturn * benchmarkReturn;
        alignedProductSum += strategyReturn * benchmarkReturn;
        alignedExcessSum += excessReturn;
        alignedExcessSquareSum += excessReturn * excessReturn;
    }

    if (alignedSampleCount == 0) {
        return summary;
    }

    const double benchmarkMean = alignedBenchmarkSum / static_cast<double>(alignedSampleCount);
    const double alignedStrategyMean = alignedStrategySum / static_cast<double>(alignedSampleCount);
    summary.benchmarkAnnualReturn = benchmarkMean * annualizationFactor;
    summary.excessAnnualReturn = (alignedStrategyMean - benchmarkMean) * annualizationFactor;

    const double excessMean = alignedExcessSum / static_cast<double>(alignedSampleCount);
    double excessVariance = 0.0;
    if (alignedSampleCount > 1) {
        excessVariance = (alignedExcessSquareSum - static_cast<double>(alignedSampleCount) * excessMean * excessMean)
            / static_cast<double>(alignedSampleCount - 1);
    }
    const double excessStd = excessVariance > 0.0 ? std::sqrt(excessVariance) : 0.0;
    summary.trackingError = excessStd * std::sqrt(annualizationFactor);
    summary.informationRatio = summary.trackingError > 0.0
        ? (summary.excessAnnualReturn / summary.trackingError)
        : 0.0;

    double benchmarkVariance = 0.0;
    if (alignedSampleCount > 1) {
        benchmarkVariance = (alignedBenchmarkSquareSum - static_cast<double>(alignedSampleCount) * benchmarkMean * benchmarkMean)
            / static_cast<double>(alignedSampleCount - 1);
    }
    if (benchmarkVariance > 0.0) {
        const double covariance = (alignedProductSum - static_cast<double>(alignedSampleCount) * alignedStrategyMean * benchmarkMean)
            / static_cast<double>(alignedSampleCount - 1);
        summary.beta = covariance / benchmarkVariance;
        summary.alpha = summary.benchmarkAnnualReturn
            + summary.excessAnnualReturn
            - (riskFreeRate + summary.beta * (summary.benchmarkAnnualReturn - riskFreeRate));
    }

    summary.hasValidAlignment = true;
    return summary;
}

struct CombinedNonCachedAggregationSummary {
    ICIRResult icirResult;
    GroupBacktestResult groupResult;
    std::vector<double> longShortSeries;
    std::vector<double> turnoverSeries;
    std::vector<std::string> longShortDates;
    bool hasValidGroup = false;
    size_t overlapDateCount = 0;
    size_t maxMatchedStocks = 0;
    int groupedDateCount = 0;
    int maxEffectiveGroupCount = 0;
    std::string failureReason;
};

CombinedNonCachedAggregationSummary aggregateNonCachedBacktestResults(const std::vector<CalculationResult>& factorResults,
                                                                      const std::vector<CalculationResult>& returnResults,
                                                                      const factor::BacktestConfig& config)
{
    CombinedNonCachedAggregationSummary summary;
    const int groupCount = (std::max)(1, config.numGroups);
    const int rebalanceInterval = (std::max)(1, config.rebalanceDays);

    std::unordered_map<std::string, const CalculationResult*> returnsByDate;
    returnsByDate.reserve(returnResults.size());
    for (const auto& result : returnResults) {
        returnsByDate[result.date] = &result;
    }

    std::vector<double> icSeries;
    icSeries.reserve((std::min)(factorResults.size(), returnResults.size()));

    std::vector<double> aggregatedReturns(static_cast<size_t>(groupCount), 0.0);
    std::vector<int> aggregatedCounts(static_cast<size_t>(groupCount), 0);
    std::vector<int> aggregatedStockCounts(static_cast<size_t>(groupCount), 0);
    std::vector<double> aggregatedMinFactorValues(static_cast<size_t>(groupCount), std::numeric_limits<double>::max());
    std::vector<double> aggregatedMaxFactorValues(static_cast<size_t>(groupCount), std::numeric_limits<double>::lowest());
    std::vector<std::string> previousLongSymbols;
    std::vector<std::string> previousShortSymbols;
    std::vector<std::vector<std::string>> activeGroupSymbols;
    int holdingDaysSinceRebalance = rebalanceInterval;

    struct MatchedSymbolValue {
        std::string symbol;
        double factorValue = 0.0;
        double futureReturn = 0.0;
    };

    for (const auto& factorResult : factorResults) {
        const auto returnIt = returnsByDate.find(factorResult.date);
        if (returnIt == returnsByDate.end()) {
            continue;
        }

        const auto& returnValuesBySymbol = returnIt->second->values;
        std::vector<MatchedSymbolValue> matchedValues;
        matchedValues.reserve(factorResult.values.size());
        std::unordered_map<std::string, size_t> matchedIndexBySymbol;
        matchedIndexBySymbol.reserve(factorResult.values.size());

        std::vector<double> factorValues;
        std::vector<double> returnValues;
        factorValues.reserve(factorResult.values.size());
        returnValues.reserve(factorResult.values.size());

        for (const auto& [symbol, factorValue] : factorResult.values) {
            const auto returnValueIt = returnValuesBySymbol.find(symbol);
            if (returnValueIt == returnValuesBySymbol.end()) {
                continue;
            }

            matchedIndexBySymbol.emplace(symbol, matchedValues.size());
            matchedValues.push_back(MatchedSymbolValue{symbol, factorValue, returnValueIt->second});
            factorValues.push_back(factorValue);
            returnValues.push_back(returnValueIt->second);
        }

        if (matchedValues.empty()) {
            continue;
        }

        if (factorValues.size() >= 2) {
            icSeries.push_back(factor::icir::calculateCorrelation(factorValues, returnValues));
        }

        ++summary.overlapDateCount;
        summary.maxMatchedStocks = (std::max)(summary.maxMatchedStocks, matchedValues.size());

        std::sort(matchedValues.begin(), matchedValues.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.factorValue > rhs.factorValue;
        });

        const bool shouldRebalance = activeGroupSymbols.empty() || holdingDaysSinceRebalance >= rebalanceInterval;
        if (shouldRebalance) {
            if (matchedValues.size() < 2) {
                if (!activeGroupSymbols.empty()) {
                    ++holdingDaysSinceRebalance;
                }
                continue;
            }

            const int effectiveGroupCount = (std::max)(1, (std::min)(groupCount, static_cast<int>(matchedValues.size())));
            const std::size_t groupSize = (std::max)(static_cast<std::size_t>(1), matchedValues.size() / static_cast<std::size_t>(effectiveGroupCount));
            std::vector<std::vector<std::string>> proposedGroupSymbols(
                static_cast<size_t>(effectiveGroupCount),
                std::vector<std::string>());
            for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
                const size_t begin = static_cast<size_t>(groupIndex) * groupSize;
                const size_t end = groupIndex == effectiveGroupCount - 1
                    ? matchedValues.size()
                    : (std::min)(matchedValues.size(), begin + groupSize);

                if (begin >= end) {
                    continue;
                }

                auto& groupSymbols = proposedGroupSymbols[static_cast<size_t>(groupIndex)];
                groupSymbols.reserve(end - begin);
                for (size_t index = begin; index < end; ++index) {
                    groupSymbols.push_back(matchedValues[index].symbol);
                }
            }

            const std::unordered_map<std::string, double> factorValuesBySymbol(factorResult.values.begin(), factorResult.values.end());
            const bool passesSignalThreshold = factor::group_backtest::passesSignalChangeThreshold(
                factorValuesBySymbol,
                previousLongSymbols,
                previousShortSymbols,
                proposedGroupSymbols.front(),
                proposedGroupSymbols.back(),
                config.signalChangeThresholdStdMultiplier);
            const bool passesMaxTurnover = factor::group_backtest::passesTurnoverLimit(
                previousLongSymbols,
                previousShortSymbols,
                proposedGroupSymbols.front(),
                proposedGroupSymbols.back(),
                config.enableTurnoverLimit,
                config.maxRebalanceTurnover);

            if (passesSignalThreshold && passesMaxTurnover) {
                activeGroupSymbols = std::move(proposedGroupSymbols);
                holdingDaysSinceRebalance = 1;
            } else {
                ++holdingDaysSinceRebalance;
            }
        } else {
            ++holdingDaysSinceRebalance;
        }

        const int effectiveGroupCount = static_cast<int>(activeGroupSymbols.size());
        if (effectiveGroupCount <= 0) {
            continue;
        }

        summary.maxEffectiveGroupCount = (std::max)(summary.maxEffectiveGroupCount, effectiveGroupCount);
        bool dateGrouped = false;
        double topGroupReturnForDate = 0.0;
        double bottomGroupReturnForDate = 0.0;
        bool hasTopGroup = false;
        bool hasBottomGroup = false;
        for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
            const auto& groupSymbols = activeGroupSymbols[static_cast<size_t>(groupIndex)];
            if (groupSymbols.empty()) {
                continue;
            }

            double groupReturn = 0.0;
            int sampleCount = 0;
            double minFactorValue = std::numeric_limits<double>::max();
            double maxFactorValue = std::numeric_limits<double>::lowest();
            for (const auto& symbol : groupSymbols) {
                const auto matchedIndexIt = matchedIndexBySymbol.find(symbol);
                if (matchedIndexIt == matchedIndexBySymbol.end()) {
                    continue;
                }

                const auto& matchedValue = matchedValues[matchedIndexIt->second];
                groupReturn += matchedValue.futureReturn;
                ++sampleCount;

                minFactorValue = (std::min)(minFactorValue, matchedValue.factorValue);
                maxFactorValue = (std::max)(maxFactorValue, matchedValue.factorValue);
            }

            if (sampleCount == 0) {
                continue;
            }

            const double averageGroupReturn = groupReturn / static_cast<double>(sampleCount);
            aggregatedReturns[static_cast<size_t>(groupIndex)] += averageGroupReturn;
            aggregatedCounts[static_cast<size_t>(groupIndex)] += 1;
            aggregatedStockCounts[static_cast<size_t>(groupIndex)] += sampleCount;
            if (minFactorValue <= maxFactorValue) {
                aggregatedMinFactorValues[static_cast<size_t>(groupIndex)] = (std::min)(aggregatedMinFactorValues[static_cast<size_t>(groupIndex)], minFactorValue);
                aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)] = (std::max)(aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)], maxFactorValue);
            }
            if (groupIndex == 0) {
                topGroupReturnForDate = averageGroupReturn;
                hasTopGroup = true;
            }
            if (groupIndex == effectiveGroupCount - 1) {
                bottomGroupReturnForDate = averageGroupReturn;
                hasBottomGroup = true;
            }
            dateGrouped = true;
        }

        if (dateGrouped) {
            ++summary.groupedDateCount;
            if (hasTopGroup && hasBottomGroup) {
                summary.longShortSeries.push_back(topGroupReturnForDate - bottomGroupReturnForDate - (2.0 * config.transactionCost));
                summary.longShortDates.push_back(factorResult.date);
                const double longTurnover = factor::group_backtest::calculatePortfolioTurnover(previousLongSymbols, activeGroupSymbols.front());
                const double shortTurnover = factor::group_backtest::calculatePortfolioTurnover(previousShortSymbols, activeGroupSymbols.back());
                summary.turnoverSeries.push_back((longTurnover + shortTurnover) / 2.0);
                previousLongSymbols = activeGroupSymbols.front();
                previousShortSymbols = activeGroupSymbols.back();
            }
        }
    }

    summary.icirResult.icSeries = std::move(icSeries);
    summary.icirResult.icMean = factor::icir::calculateMean(summary.icirResult.icSeries);
    summary.icirResult.icStd = factor::icir::calculateStdDev(summary.icirResult.icSeries, summary.icirResult.icMean);
    summary.icirResult.ir = summary.icirResult.icStd > 0.0 ? summary.icirResult.icMean / summary.icirResult.icStd : 0.0;
    if (!summary.icirResult.icSeries.empty()) {
        const auto positiveCount = std::count_if(summary.icirResult.icSeries.begin(), summary.icirResult.icSeries.end(), [](double value) {
            return value > 0.0;
        });
        summary.icirResult.icPositiveRatio = static_cast<double>(positiveCount) / static_cast<double>(summary.icirResult.icSeries.size());
    }

    const int outputGroupCount = (std::max)(0, summary.maxEffectiveGroupCount);
    summary.groupResult.groupReturns.resize(static_cast<size_t>(outputGroupCount), 0.0);
    summary.groupResult.groupStockCounts.resize(static_cast<size_t>(outputGroupCount), 0);
    summary.groupResult.minFactorValues.resize(static_cast<size_t>(outputGroupCount), 0.0);
    summary.groupResult.maxFactorValues.resize(static_cast<size_t>(outputGroupCount), 0.0);

    for (int index = 0; index < outputGroupCount; ++index) {
        if (aggregatedCounts[static_cast<size_t>(index)] <= 0) {
            continue;
        }

        summary.hasValidGroup = true;
        summary.groupResult.groupReturns[static_cast<size_t>(index)] =
            aggregatedReturns[static_cast<size_t>(index)] / static_cast<double>(aggregatedCounts[static_cast<size_t>(index)]);
        summary.groupResult.groupStockCounts[static_cast<size_t>(index)] =
            aggregatedStockCounts[static_cast<size_t>(index)] / aggregatedCounts[static_cast<size_t>(index)];
        summary.groupResult.minFactorValues[static_cast<size_t>(index)] = aggregatedMinFactorValues[static_cast<size_t>(index)];
        summary.groupResult.maxFactorValues[static_cast<size_t>(index)] = aggregatedMaxFactorValues[static_cast<size_t>(index)];
    }

    const bool hasUsableGroupResult = summary.hasValidGroup && summary.groupResult.groupReturns.size() >= 2;
    if (hasUsableGroupResult) {
        summary.groupResult.topGroupReturn = summary.groupResult.groupReturns.front();
        summary.groupResult.bottomGroupReturn = summary.groupResult.groupReturns.back();
        summary.groupResult.longShortReturn = summary.groupResult.topGroupReturn
            - summary.groupResult.bottomGroupReturn
            - (2.0 * config.transactionCost);
    } else if (returnResults.empty()) {
        summary.failureReason = "未生成未来收益序列，请检查所选数据区间是否至少覆盖到下一个交易日";
    } else if (summary.overlapDateCount == 0) {
        summary.failureReason = "因子值与未来收益没有重叠样本，无法执行分组回测";
    } else if (summary.maxMatchedStocks <= 1) {
        summary.failureReason = "有效股票数不足，至少需要 2 只股票才能执行分组回测";
    } else {
        summary.failureReason = "未生成有效分组回测结果：最大有效股票数为 " + std::to_string(summary.maxMatchedStocks)
            + "，请求分组数为 " + std::to_string(config.numGroups)
            + "，成功分组日期数为 " + std::to_string(summary.groupedDateCount);
    }

    summary.hasValidGroup = hasUsableGroupResult;
    return summary;
}

class CachedRowHistoricalView final : public HistoricalView {
public:
    explicit CachedRowHistoricalView(const std::vector<factor::CachedMarketBar>& rows,
                                     std::shared_ptr<void> = {})
        : data_(factor::ArrowMarketData::fromCachedBars(rows))
    {
        if (!data_) {
            throw std::runtime_error("FactorBacktestExecutor: failed to initialize ArrowMarketData");
        }
    }

    explicit CachedRowHistoricalView(std::shared_ptr<factor::ArrowMarketData> data)
        : data_(std::move(data))
    {
        if (!data_) {
            throw std::runtime_error("FactorBacktestExecutor: failed to initialize ArrowMarketData");
        }
    }

    bool hasField(const std::string& field) const override {
        return data_->getColumn(field) != nullptr;
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override {
        const double value = data_->getValue(symbol, date, field);
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    }

    std::vector<factor::HistoricalDataPoint> getSeries(const std::string& symbol,
                                                       const std::string& startDate,
                                                       const std::string& endDate,
                                                       const std::string& field) const override {
        return data_->getSeries(symbol, startDate, endDate, field);
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override {
        return data_->getAvailableSymbols(date);
    }

    std::unordered_map<std::string, double> getCrossSection(const std::string& date,
                                                            const std::string& field,
                                                            const std::vector<std::string>& symbols = {}) const override {
        return data_->getCrossSection(date, field, symbols);
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const override
    {
        return data_->getBatchCrossSections(date, symbols, fields);
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& fields) const override
    {
        return data_->getBatchTimeSeries(symbols, startDate, endDate, fields);
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& anchorDate,
        int window,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        if (!data_ || window <= 0) {
            return batchSeries;
        }

        const auto uniqueSymbols = deduplicateSymbolsPreservingOrder(symbols);
        for (const auto& field : fields) {
            const auto seriesBySymbol = data_->getBatchTimeSeries(uniqueSymbols, field, window, anchorDate);
            auto& fieldSeries = batchSeries[field];
            for (size_t index = 0; index < uniqueSymbols.size() && index < seriesBySymbol.size(); ++index) {
                fieldSeries.emplace(uniqueSymbols[index], seriesBySymbol[index]);
            }
        }

        return batchSeries;
    }

    std::vector<std::vector<double>> getBatchTimeSeries(const std::vector<std::string>& symbols,
                                                        const std::string& field,
                                                        int window,
                                                        const std::string& anchorDate) const {
        return data_->getBatchTimeSeries(symbols, field, window, anchorDate);
    }

private:
    std::shared_ptr<factor::ArrowMarketData> data_;
};

double annualizationFactorForPeriods(int forwardDays)
{
    return 252.0 / static_cast<double>((std::max)(1, forwardDays));
}

std::string buildBacktestCacheSignature(const BacktestConfig& config)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6)
           << "ds" << config.datasetId
           << "_bm" << config.benchmarkSymbol
           << "_tc" << config.transactionCost
           << "_slp" << config.slippageRate
           << "_rf" << config.riskFreeRate
           << "_rb" << config.rebalanceDays
           << "_sl" << config.stopLossRate
           << "_tp" << config.takeProfitRate
           << "_dd" << config.maxDrawdownLimit
           << "_dl" << config.maxDailyLoss
           << "_mp" << config.maxPositionPercent
           << "_te" << config.maxTotalExposure
           << "_stocks" << buildAllowedStockCodesFingerprint(config.allowedStockCodes)
           << "_bars" << buildCachedBarsFingerprint(config.cachedBars);
    return stream.str();
}

bool tryLoadBacktestResultFromCache(const std::shared_ptr<FactorCacheManager>& cacheManager,
                                   const BacktestConfig& config,
                                   const std::string& cacheSignature,
                                   foundation::json::JsonFacade& cachedResult)
{
    if (!cacheManager || !cacheManager->isCacheAvailable()) {
        return false;
    }

    if (cacheManager->getBacktestResult(
            config.instanceId,
            config.startDate,
            config.endDate,
            config.forwardDays,
            config.numGroups,
            cacheSignature,
            cachedResult)) {
        return true;
    }

    return false;
}

double calculateMaxDrawdown(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    double cumulativeNetValue = 1.0;
    double peakNetValue = 1.0;
    double maxDrawdown = 0.0;
    for (double periodicReturn : periodicReturns) {
        cumulativeNetValue *= (1.0 + periodicReturn);
        peakNetValue = (std::max)(peakNetValue, cumulativeNetValue);
        if (peakNetValue <= 0.0) {
            continue;
        }

        const double drawdown = (peakNetValue - cumulativeNetValue) / peakNetValue;
        maxDrawdown = (std::max)(maxDrawdown, drawdown);
    }

    return maxDrawdown;
}

struct RiskControlResult {
    std::vector<double> adjustedReturns;
    int triggeredCount = 0;
    std::string summary;
};

RiskControlResult applyRiskControls(const std::vector<double>& periodicReturns,
                                    const BacktestConfig& config)
{
    RiskControlResult result;
    result.adjustedReturns.reserve(periodicReturns.size());

    double cumulativeNetValue = 1.0;
    double peakNetValue = 1.0;
    int stopLossCount = 0;
    int takeProfitCount = 0;
    int dailyLossCount = 0;
    int drawdownBreakCount = 0;
    int positionLimitCount = 0;

    for (double periodicReturn : periodicReturns) {
        double adjustedReturn = periodicReturn;

        // 单只股票仓位限制：将收益按仓位比例缩放
        if (config.maxPositionPercent > 0.0 && config.maxPositionPercent < 1.0) {
            adjustedReturn *= config.maxPositionPercent;
            if (std::abs(adjustedReturn - periodicReturn) > 1e-12) {
                ++positionLimitCount;
            }
        }

        // 总仓位暴露限制
        if (config.maxTotalExposure > 0.0 && config.maxTotalExposure < 1.0) {
            adjustedReturn *= config.maxTotalExposure;
        }

        // 止损
        if (config.stopLossRate > 0.0 && adjustedReturn < -config.stopLossRate) {
            adjustedReturn = -config.stopLossRate;
            ++stopLossCount;
        }

        // 止盈
        if (config.takeProfitRate > 0.0 && adjustedReturn > config.takeProfitRate) {
            adjustedReturn = config.takeProfitRate;
            ++takeProfitCount;
        }

        // 单日最大亏损限制
        if (config.maxDailyLoss > 0.0 && adjustedReturn < -config.maxDailyLoss) {
            adjustedReturn = -config.maxDailyLoss;
            ++dailyLossCount;
        }

        result.adjustedReturns.push_back(adjustedReturn);

        cumulativeNetValue *= (1.0 + adjustedReturn);
        peakNetValue = (std::max)(peakNetValue, cumulativeNetValue);
        if (config.maxDrawdownLimit <= 0.0 || peakNetValue <= 0.0) {
            continue;
        }

        const double currentDrawdown = (peakNetValue - cumulativeNetValue) / peakNetValue;
        if (currentDrawdown >= config.maxDrawdownLimit) {
            ++drawdownBreakCount;
            break;
        }
    }

    result.triggeredCount = stopLossCount + takeProfitCount + dailyLossCount + drawdownBreakCount + positionLimitCount;

    std::ostringstream oss;
    bool hasEntry = false;
    auto appendEntry = [&](const char* label, int count) {
        if (count > 0) {
            if (hasEntry) oss << ", ";
            oss << label << ": " << count << "次";
            hasEntry = true;
        }
    };
    appendEntry("止损触发", stopLossCount);
    appendEntry("止盈触发", takeProfitCount);
    appendEntry("单日亏损限制", dailyLossCount);
    appendEntry("回撤熔断", drawdownBreakCount);
    appendEntry("仓位限制", positionLimitCount);
    if (!hasEntry) {
        oss << "未触发风控";
    }
    result.summary = oss.str();

    return result;
}

// ---- 风控指标计算 ----

double calculateDownsideDeviation(const std::vector<double>& returns, double threshold = 0.0)
{
    if (returns.empty()) return 0.0;
    double sumSqNeg = 0.0;
    for (double r : returns) {
        if (r < threshold) {
            const double diff = r - threshold;
            sumSqNeg += diff * diff;
        }
    }
    return std::sqrt(sumSqNeg / static_cast<double>(returns.size()));
}

double calculateValueAtRisk(const std::vector<double>& returns, double confidenceLevel = 0.95)
{
    if (returns.empty()) return 0.0;
    std::vector<double> sorted = returns;
    const size_t index = static_cast<size_t>(std::floor((1.0 - confidenceLevel) * static_cast<double>(sorted.size())));
    const size_t clampedIndex = (std::min)(index, sorted.size() - 1);
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(clampedIndex), sorted.end());
    return -sorted[clampedIndex];
}

double calculateConditionalVaR(const std::vector<double>& returns, double confidenceLevel = 0.95)
{
    if (returns.empty()) return 0.0;
    std::vector<double> sorted = returns;
    const size_t cutoff = static_cast<size_t>(std::floor((1.0 - confidenceLevel) * static_cast<double>(sorted.size())));
    const size_t n = (std::max)(cutoff, static_cast<size_t>(1));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n - 1), sorted.end());
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += sorted[i];
    }
    return -(sum / static_cast<double>(n));
}

double calculateWinRate(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    const auto wins = std::count_if(periodicReturns.begin(), periodicReturns.end(), [](double value) {
        return value > 0.0;
    });
    return static_cast<double>(wins) / static_cast<double>(periodicReturns.size());
}

double calculateProfitFactor(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    double grossProfit = 0.0;
    double grossLossAbs = 0.0;
    for (double periodicReturn : periodicReturns) {
        if (periodicReturn > 0.0) {
            grossProfit += periodicReturn;
        } else if (periodicReturn < 0.0) {
            grossLossAbs += -periodicReturn;
        }
    }

    if (grossLossAbs <= 1e-12) {
        return grossProfit > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
    }

    return grossProfit / grossLossAbs;
}

double calculateVariance(const std::vector<double>& values, double mean)
{
    if (values.size() < 2) {
        return 0.0;
    }

    double sum = 0.0;
    for (double value : values) {
        const double diff = value - mean;
        sum += diff * diff;
    }

    return sum / static_cast<double>(values.size() - 1);
}

double calculateCovariance(const std::vector<double>& lhs,
                           double lhsMean,
                           const std::vector<double>& rhs,
                           double rhsMean)
{
    if (lhs.size() < 2 || lhs.size() != rhs.size()) {
        return 0.0;
    }

    double sum = 0.0;
    for (size_t index = 0; index < lhs.size(); ++index) {
        sum += (lhs[index] - lhsMean) * (rhs[index] - rhsMean);
    }

    return sum / static_cast<double>(lhs.size() - 1);
}

bool isBarWithinRange(const CachedMarketBar& bar,
                      const std::string& startDate,
                      const std::string& endDate)
{
    return factor::cached_bars::isBarWithinRange(bar, startDate, endDate);
}

int progressPercentFromWork(size_t completedWorkUnits, size_t totalWorkUnits)
{
    if (totalWorkUnits == 0) {
        return 0;
    }

    const double ratio = static_cast<double>(completedWorkUnits) / static_cast<double>(totalWorkUnits);
    const int percent = static_cast<int>(ratio * 100.0);
    return (std::max)(0, percent);
}

}

FactorBacktestExecutor::FactorBacktestExecutor(
    std::shared_ptr<FactorInstanceManager> instanceManager,
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> threadPool,
    std::shared_ptr<FactorCacheManager> cacheManager)
    : instanceManager_(std::move(instanceManager)),
      threadPool_(std::move(threadPool)),
      cacheManager_(std::move(cacheManager)) {
}

FactorBacktestExecutor::ProgressInfo FactorBacktestExecutor::createProgressInfo(const std::string& instanceId) const
{
    ProgressInfo progress;
    progress.taskId = foundation::utils::Uuid::generate_v4();
    progress.instanceId = instanceId;
    progress.status = "RUNNING";
    progress.currentStep = "初始化回测任务：准备执行上下文";
    return progress;
}

void FactorBacktestExecutor::registerTask(const ProgressInfo& progress)
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    activeTasks_[progress.taskId] = progress;
}

void FactorBacktestExecutor::finalizeTask(const foundation::utils::Uuid& taskId)
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    activeTasks_.erase(taskId);
    cancelledTasks_.erase(taskId.to_string());
}

BacktestResult FactorBacktestExecutor::executeTracked(const BacktestConfig& config,
                                                      ProgressInfo progress)
{
    const bool useBacktestCache = cacheManager_ && cacheManager_->isCacheAvailable();
    const std::string cacheSignature = buildBacktestCacheSignature(config);

    if (useBacktestCache) {
        foundation::json::JsonFacade cachedResult;
        if (tryLoadBacktestResultFromCache(cacheManager_, config, cacheSignature, cachedResult)) {
            updateProgress(progress,
                           progressPercentFromWork(1, 1),
                           "命中回测缓存：直接返回已缓存结果");
            return BacktestResult::fromJson(cachedResult);
        }
    }

    BacktestResult result = executeInternal(config, progress);

    if (useBacktestCache && result.status == "SUCCESS") {
        cacheManager_->setBacktestResult(
            config.instanceId,
            config.startDate,
            config.endDate,
            config.forwardDays,
            config.numGroups,
            cacheSignature,
            result.toJson()
        );
    }

    return result;
}

BacktestResult FactorBacktestExecutor::execute(const BacktestConfig& config)
{
    ProgressInfo progress = createProgressInfo(config.instanceId);
    registerTask(progress);
    BacktestResult result = executeTracked(config, progress);
    finalizeTask(progress.taskId);
    return result;
}

std::future<BacktestResult> FactorBacktestExecutor::executeAsync(const BacktestConfig& config)
{
    return executeTrackedAsync(config).future;
}

FactorBacktestExecutor::ExecutionHandle FactorBacktestExecutor::executeTrackedAsync(const BacktestConfig& config)
{
    ProgressInfo progress = createProgressInfo(config.instanceId);
    registerTask(progress);

    const std::string cacheSignature = buildBacktestCacheSignature(config);
    if (cacheManager_ && cacheManager_->isCacheAvailable()) {
        foundation::json::JsonFacade cachedResult;
        if (tryLoadBacktestResultFromCache(cacheManager_, config, cacheSignature, cachedResult)) {
            std::promise<BacktestResult> promise;
            promise.set_value(BacktestResult::fromJson(cachedResult));
            auto future = promise.get_future();
            finalizeTask(progress.taskId);
            return ExecutionHandle{progress.taskId, std::move(future)};
        }
    }

    if (!threadPool_) {
        finalizeTask(progress.taskId);
        throw std::runtime_error("FactorBacktestExecutor: threadPool 未初始化，无法执行异步回测");
    }

    auto future = threadPool_->submit([this, config, progress]() mutable {
        BacktestResult result = executeTracked(config, progress);
        finalizeTask(progress.taskId);
        return result;
    });
    return ExecutionHandle{progress.taskId, std::move(future)};
}

std::vector<BacktestResult> FactorBacktestExecutor::executeBatch(const std::vector<BacktestConfig>& configs)
{
    if (configs.empty()) {
        return {};
    }

    if (configs.size() == 1) {
        return {execute(configs.front())};
    }

    const size_t workerCount = threadPool_ ? (std::max)(size_t{1}, threadPool_->getWorkerCount()) : size_t{1};
    const bool hasHeavyPayload = std::any_of(configs.begin(), configs.end(), [](const BacktestConfig& config) {
        return config.enableDateParallelism || hasPreparedHistoricalData(config);
    });
    const size_t maxInflight = hasHeavyPayload
        ? size_t{1}
        : (std::min)(configs.size(), (std::max)(size_t{1}, workerCount / 2));

    std::vector<BacktestResult> results;
    results.reserve(configs.size());
    std::deque<ExecutionHandle> inflight;
    size_t nextConfigIndex = 0;

    while (nextConfigIndex < configs.size() || !inflight.empty()) {
        while (nextConfigIndex < configs.size() && inflight.size() < maxInflight) {
            inflight.push_back(executeTrackedAsync(configs[nextConfigIndex++]));
        }

        auto handle = std::move(inflight.front());
        inflight.pop_front();
        results.push_back(handle.future.get());
    }
    return results;
}

FactorBacktestExecutor::ProgressInfo FactorBacktestExecutor::getProgress(const foundation::utils::Uuid& taskId) const
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    auto it = activeTasks_.find(taskId);
    if (it != activeTasks_.end()) {
        return it->second;
    }

    ProgressInfo progress;
    progress.taskId = taskId;
    progress.status = "NOT_FOUND";
    progress.currentStep = "任务不存在";
    return progress;
}

bool FactorBacktestExecutor::cancel(const foundation::utils::Uuid& taskId)
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    auto it = activeTasks_.find(taskId);
    if (it == activeTasks_.end()) {
        return false;
    }

    it->second.status = "CANCELLED";
    it->second.currentStep = "任务已取消";
    cancelledTasks_.insert(taskId.to_string());
    return true;
}

BacktestResult FactorBacktestExecutor::executeInternal(const BacktestConfig& config,
                                                       ProgressInfo& progress)
{
    QElapsedTimer timer;
    timer.start();

    BacktestResult result;
    result.resultId = foundation::utils::Uuid::generate_v4();
    result.instanceId = config.instanceId;
    result.config = config;
    result.status = "FAILED";

    try {
        if (!instanceManager_) {
            return BacktestResult::createError(config.instanceId, "因子实例管理器未初始化");
        }

        auto info = instanceManager_->getInstanceInfo(config.instanceId);
        if (info.instanceId.empty()) {
            return BacktestResult::createError(config.instanceId, "因子实例不存在");
        }

        result.instanceName = info.instanceName;
        result.dataStatus = info.dataStatus;
        result.dataCoverage = info.dataStatus.coverage;

        ExecutionMarketContext marketContext;
        CachedMarketIndex cachedMarketIndex;
        std::string prepareFailureReason;
        if (!prepareExecutionMarketContext(config, marketContext, &cachedMarketIndex, &prepareFailureReason, &progress)) {
            result.errorMessage = prepareFailureReason.empty() ? "准备市场上下文失败" : prepareFailureReason;
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        const size_t tradeDateCount = marketContext.tradeDates.size();
        const size_t totalWorkUnits = (std::max)(size_t{9}, tradeDateCount * 2 + size_t{9});
        size_t completedWorkUnits = 0;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                   "准备回测上下文：构建交易日与股票池索引");
        ++completedWorkUnits;

        std::shared_ptr<BaseFactor> factor;
        if (!prepareData(config, progress, factor, &prepareFailureReason)) {
            result.errorMessage = prepareFailureReason.empty() ? "准备回测数据失败" : prepareFailureReason;
            if (isCancelled(progress.taskId)) {
                result.status = "CANCELLED";
                result.errorMessage = "任务已取消";
            }
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        ++completedWorkUnits;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "加载因子实例：解析配置并构建执行对象");

        const std::unordered_map<std::string, std::vector<double>> futureReturnsBySymbol = config.cachedBars.empty()
            ? std::unordered_map<std::string, std::vector<double>>{}
            : precomputeFutureReturns(config.cachedBars, config.forwardDays);

        const auto benchmarkLookup = [this, &config, &marketContext, &cachedMarketIndex, &futureReturnsBySymbol](const std::string& date) {
            if (!futureReturnsBySymbol.empty()) {
                return lookupPrecomputedFutureReturn(futureReturnsBySymbol,
                                                     marketContext.arrowData,
                                                     config.benchmarkSymbol,
                                                     date);
            }

            return calculateFutureReturn(config.benchmarkSymbol,
                                         date,
                                         config.forwardDays,
                                         config,
                                         &cachedMarketIndex);
        };

        if (hasPreparedHistoricalData(config)) {
            if (!marketContext.arrowData) {
                result.errorMessage = "缓存回测未能构建 Arrow 行情上下文";
                result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
                result.executionTimeMs = static_cast<int>(timer.elapsed());
                return result;
            }

            const auto dateToIndex = buildDateIndexMap(*marketContext.arrowData);

            CachedReturnAggregationState aggregationState;
            initializeCachedReturnAggregationState(config, aggregationState);
            std::vector<CalculationResult> unusedFactorResults;
            std::string factorFailureReason;
            size_t streamedFactorWorkUnits = 0;
            std::function<bool(CalculationResult&&)> consumeCachedFactorResult = [&futureReturnsBySymbol,
                                                                                   &dateToIndex,
                                                                                   &config,
                                                                                   &aggregationState,
                                                                                   &cachedMarketIndex,
                                                                                   &factorFailureReason,
                                                                                   &progress,
                                                                                   this](CalculationResult&& factorResult) {
                if (isCancelled(progress.taskId)) {
                    factorFailureReason = "任务已取消";
                    return false;
                }
                if (!aggregateSingleResultWithPrecomputedReturns(factorResult,
                                                                 futureReturnsBySymbol,
                                                                 dateToIndex,
                                                                 config,
                                                                 aggregationState,
                                                                 &cachedMarketIndex,
                                                                 &factorFailureReason)) {
                    if (factorFailureReason.empty()) {
                        factorFailureReason = "缓存回测聚合失败";
                    }
                    return false;
                }
                return true;
            };
            if (!calculateFactorSeries(config,
                                       marketContext,
                                       factor,
                                       progress,
                                       unusedFactorResults,
                                       &consumeCachedFactorResult,
                                       &factorFailureReason,
                                       completedWorkUnits,
                                       totalWorkUnits,
                                       &streamedFactorWorkUnits)) {
                result.errorMessage = isCancelled(progress.taskId)
                    ? "任务已取消"
                    : (factorFailureReason.empty() ? "因子序列计算失败" : factorFailureReason);
                result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
                result.executionTimeMs = static_cast<int>(timer.elapsed());
                return result;
            }

            completedWorkUnits += streamedFactorWorkUnits;

            auto& icSeries = aggregationState.icSeries;
            auto& longShortSeries = aggregationState.longShortSeries;
            auto& turnoverSeries = aggregationState.turnoverSeries;
            auto& longShortDates = aggregationState.longShortDates;
            auto& aggregatedReturns = aggregationState.aggregatedReturns;
            auto& aggregatedCounts = aggregationState.aggregatedCounts;
            auto& aggregatedStockCounts = aggregationState.aggregatedStockCounts;
            auto& aggregatedMinFactorValues = aggregationState.aggregatedMinFactorValues;
            auto& aggregatedMaxFactorValues = aggregationState.aggregatedMaxFactorValues;
            auto& previousLongSymbols = aggregationState.previousLongSymbols;
            auto& previousShortSymbols = aggregationState.previousShortSymbols;
            auto& activeGroupSymbols = aggregationState.activeGroupSymbols;
            size_t& overlapDateCount = aggregationState.overlapDateCount;
            int& groupedDateCount = aggregationState.groupedDateCount;
            int& maxEffectiveGroupCount = aggregationState.maxEffectiveGroupCount;
            size_t& maxMatchedStocks = aggregationState.maxMatchedStocks;
            bool& hasValidGroup = aggregationState.hasValidGroup;
            bool& hasAnyFactorResult = aggregationState.hasAnyFactorResult;

            result.icirResult.icSeries = std::move(icSeries);
            result.icirResult.icMean = factor::icir::calculateMean(result.icirResult.icSeries);
            result.icirResult.icStd = factor::icir::calculateStdDev(result.icirResult.icSeries, result.icirResult.icMean);
            result.icirResult.ir = result.icirResult.icStd > 0.0 ? result.icirResult.icMean / result.icirResult.icStd : 0.0;
            if (!result.icirResult.icSeries.empty()) {
                const auto positiveCount = std::count_if(result.icirResult.icSeries.begin(), result.icirResult.icSeries.end(), [](double value) {
                    return value > 0.0;
                });
                result.icirResult.icPositiveRatio = static_cast<double>(positiveCount) / static_cast<double>(result.icirResult.icSeries.size());
            }

            result.groupResult.groupReturns.resize(static_cast<size_t>(maxEffectiveGroupCount), 0.0);
            result.groupResult.groupStockCounts.resize(static_cast<size_t>(maxEffectiveGroupCount), 0);
            result.groupResult.minFactorValues.resize(static_cast<size_t>(maxEffectiveGroupCount), 0.0);
            result.groupResult.maxFactorValues.resize(static_cast<size_t>(maxEffectiveGroupCount), 0.0);
            for (int groupIndex = 0; groupIndex < maxEffectiveGroupCount; ++groupIndex) {
                if (aggregatedCounts[static_cast<size_t>(groupIndex)] <= 0) {
                    continue;
                }

                hasValidGroup = true;
                result.groupResult.groupReturns[static_cast<size_t>(groupIndex)] =
                    aggregatedReturns[static_cast<size_t>(groupIndex)] / static_cast<double>(aggregatedCounts[static_cast<size_t>(groupIndex)]);
                result.groupResult.groupStockCounts[static_cast<size_t>(groupIndex)] =
                    aggregatedStockCounts[static_cast<size_t>(groupIndex)] / aggregatedCounts[static_cast<size_t>(groupIndex)];
                result.groupResult.minFactorValues[static_cast<size_t>(groupIndex)] = aggregatedMinFactorValues[static_cast<size_t>(groupIndex)];
                result.groupResult.maxFactorValues[static_cast<size_t>(groupIndex)] = aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)];
            }
            if (hasValidGroup && result.groupResult.groupReturns.size() >= 2) {
                result.groupResult.topGroupReturn = result.groupResult.groupReturns.front();
                result.groupResult.bottomGroupReturn = result.groupResult.groupReturns.back();
                result.groupResult.longShortReturn = result.groupResult.topGroupReturn
                    - result.groupResult.bottomGroupReturn
                    - (2.0 * config.transactionCost);
            } else {
                result.status = "PARTIAL";
            }

            if (overlapDateCount == 0) {
                qWarning() << "FactorBacktestExecutor: 缓存回测未生成有效因子/收益重叠样本"
                           << "instanceId=" << QString::fromStdString(config.instanceId);
            }

            ++completedWorkUnits;
            updateProgress(progress,
                           progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                           "执行分组回测：生成多空曲线和分组收益序列");

            updateProgress(progress,
                           progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                           "应用风控：止损、止盈、仓位和回撤约束");
            const auto riskResult = applyRiskControls(longShortSeries, config);
            longShortSeries = riskResult.adjustedReturns;
            const double annualizationFactor = annualizationFactorForPeriods(config.forwardDays);
            const double periodRiskFreeRate = config.riskFreeRate / annualizationFactor;
            const double averageLongShort = factor::icir::calculateMean(longShortSeries);
            const double longShortStd = factor::icir::calculateStdDev(longShortSeries, averageLongShort);
            const double averageTurnover = factor::icir::calculateMean(turnoverSeries);
            const double averageExcessReturn = averageLongShort - periodRiskFreeRate;

            result.annualReturn = averageLongShort * annualizationFactor;
            result.sharpeRatio = longShortStd > 0.0 ? (averageExcessReturn / longShortStd) * std::sqrt(annualizationFactor) : 0.0;
            result.maxDrawdown = calculateMaxDrawdown(longShortSeries);
            result.winRate = calculateWinRate(longShortSeries);
            result.profitFactor = calculateProfitFactor(longShortSeries);
            result.turnoverRate = averageTurnover * annualizationFactor * 100.0;

            ++completedWorkUnits;
            updateProgress(progress,
                           progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                           "汇总结果：计算年化收益、夏普、回撤、胜率和换手率");

            const auto benchmarkSummary = calculateBenchmarkComparison(longShortSeries,
                                                                        longShortDates,
                                                                        config.forwardDays,
                                                                        config.riskFreeRate,
                                                                        benchmarkLookup);
            if (benchmarkSummary.hasValidAlignment) {
                result.benchmarkAnnualReturn = benchmarkSummary.benchmarkAnnualReturn;
                result.excessAnnualReturn = benchmarkSummary.excessAnnualReturn;
                result.trackingError = benchmarkSummary.trackingError;
                result.informationRatio = benchmarkSummary.informationRatio;
                result.beta = benchmarkSummary.beta;
                result.alpha = benchmarkSummary.alpha;
            }

            ++completedWorkUnits;
            updateProgress(progress,
                           progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                           "生成最终结果：整理摘要并准备返回");

            result.volatility = longShortStd * std::sqrt(annualizationFactor);
            result.downsideDeviation = calculateDownsideDeviation(longShortSeries) * std::sqrt(annualizationFactor);
            result.sortinoRatio = result.downsideDeviation > 0.0
                ? (averageExcessReturn / result.downsideDeviation) * std::sqrt(annualizationFactor)
                : 0.0;
            result.calmarRatio = result.maxDrawdown > 0.0
                ? result.annualReturn / result.maxDrawdown
                : 0.0;
            result.valueAtRisk = calculateValueAtRisk(longShortSeries, 0.95);
            result.conditionalVaR = calculateConditionalVaR(longShortSeries, 0.95);
            result.riskTriggeredCount = riskResult.triggeredCount;
            result.riskControlSummary = riskResult.summary;
            result.dataStatus.availability = hasAnyFactorResult ? DataAvailability::AVAILABLE : DataAvailability::UNAVAILABLE;
            result.dataStatus.coverage = hasAnyFactorResult ? 1.0 : 0.0;
            result.dataStatus.message = hasAnyFactorResult ? "回测执行完成" : "未生成有效因子序列";
            result.dataCoverage = result.dataStatus.coverage;
            result.status = isCancelled(progress.taskId)
                ? "CANCELLED"
                : (result.status == "PARTIAL" ? "PARTIAL" : "SUCCESS");
            result.executionTimeMs = static_cast<int>(timer.elapsed());

            updateProgress(progress, 100, result.status == "CANCELLED" ? "已取消" : "回测完成");
            return result;
        }

        std::vector<double> icSeries;
        std::vector<double> longShortSeries;
        std::vector<double> turnoverSeries;
        std::vector<std::string> longShortDates;

        const int groupCount = (std::max)(1, config.numGroups);
        const int rebalanceInterval = (std::max)(1, config.rebalanceDays);
        std::vector<double> aggregatedReturns(static_cast<size_t>(groupCount), 0.0);
        std::vector<int> aggregatedCounts(static_cast<size_t>(groupCount), 0);
        std::vector<int> aggregatedStockCounts(static_cast<size_t>(groupCount), 0);
        std::vector<double> aggregatedMinFactorValues(static_cast<size_t>(groupCount), std::numeric_limits<double>::max());
        std::vector<double> aggregatedMaxFactorValues(static_cast<size_t>(groupCount), std::numeric_limits<double>::lowest());
        std::vector<std::string> previousLongSymbols;
        std::vector<std::string> previousShortSymbols;
        std::vector<std::vector<std::string>> activeGroupSymbols;
        int holdingDaysSinceRebalance = rebalanceInterval;
        size_t overlapDateCount = 0;
        int groupedDateCount = 0;
        int maxEffectiveGroupCount = 0;
        size_t maxMatchedStocks = 0;
        bool hasValidGroup = false;
        bool hasAnyFactorResult = false;

        std::function<bool(CalculationResult&&)> consumeFactorResult = [&](CalculationResult&& factorResult) -> bool {
            hasAnyFactorResult = true;

            const auto symbolsIt = marketContext.symbolsByDate.find(factorResult.date);
            if (symbolsIt == marketContext.symbolsByDate.end()) {
                return true;
            }

            CalculationResult returnResult;
            returnResult.calculationId = foundation::utils::Uuid::generate_v4();
            returnResult.date = factorResult.date;
            returnResult.metadata = foundation::json::JsonFacade::createObject();

            for (const auto& symbol : symbolsIt->second) {
                const double futureReturn = lookupPrecomputedFutureReturn(futureReturnsBySymbol,
                                                                         marketContext.arrowData,
                                                                         symbol,
                                                                         factorResult.date);
                if (std::isfinite(futureReturn)) {
                    returnResult.values[symbol] = futureReturn;
                }
            }

            if (returnResult.isEmpty()) {
                return true;
            }

            std::vector<double> factorValues;
            std::vector<double> returnValues;
            factorValues.reserve(factorResult.values.size());
            returnValues.reserve(factorResult.values.size());
            for (const auto& [symbol, factorValue] : factorResult.values) {
                const auto valueIt = returnResult.values.find(symbol);
                if (valueIt == returnResult.values.end()) {
                    continue;
                }
                factorValues.push_back(factorValue);
                returnValues.push_back(valueIt->second);
            }

            if (factorValues.size() >= 2) {
                icSeries.push_back(factor::icir::calculateCorrelation(factorValues, returnValues));
            }

            std::vector<std::pair<std::string, double>> rankedValues(factorResult.values.begin(), factorResult.values.end());
            rankedValues.erase(
                std::remove_if(rankedValues.begin(), rankedValues.end(), [&](const auto& item) {
                    return returnResult.values.find(item.first) == returnResult.values.end();
                }),
                rankedValues.end());

            if (rankedValues.empty()) {
                return true;
            }

            ++overlapDateCount;
            maxMatchedStocks = (std::max)(maxMatchedStocks, rankedValues.size());

            std::sort(rankedValues.begin(), rankedValues.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.second > rhs.second;
            });

            const bool shouldRebalance = activeGroupSymbols.empty() || holdingDaysSinceRebalance >= rebalanceInterval;
            if (shouldRebalance) {
                if (rankedValues.size() < 2) {
                    if (!activeGroupSymbols.empty()) {
                        ++holdingDaysSinceRebalance;
                    }
                    return true;
                }

                const int effectiveGroupCount = (std::max)(1, (std::min)(groupCount, static_cast<int>(rankedValues.size())));
                const std::size_t groupSize = (std::max)(static_cast<std::size_t>(1), rankedValues.size() / static_cast<std::size_t>(effectiveGroupCount));
                std::vector<std::vector<std::string>> proposedGroupSymbols(
                    static_cast<size_t>(effectiveGroupCount),
                    std::vector<std::string>());
                for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
                    const size_t begin = static_cast<size_t>(groupIndex) * groupSize;
                    const size_t end = groupIndex == effectiveGroupCount - 1
                        ? rankedValues.size()
                        : (std::min)(rankedValues.size(), begin + groupSize);

                    if (begin >= end) {
                        continue;
                    }

                    auto& groupSymbols = proposedGroupSymbols[static_cast<size_t>(groupIndex)];
                    groupSymbols.reserve(end - begin);
                    for (size_t valueIndex = begin; valueIndex < end; ++valueIndex) {
                        groupSymbols.push_back(rankedValues[valueIndex].first);
                    }
                }

                const std::unordered_map<std::string, double> factorValuesBySymbol(factorResult.values.begin(), factorResult.values.end());
                const bool passesSignalThreshold = factor::group_backtest::passesSignalChangeThreshold(
                    factorValuesBySymbol,
                    previousLongSymbols,
                    previousShortSymbols,
                    proposedGroupSymbols.front(),
                    proposedGroupSymbols.back(),
                    config.signalChangeThresholdStdMultiplier);
                const bool passesMaxTurnover = factor::group_backtest::passesTurnoverLimit(
                    previousLongSymbols,
                    previousShortSymbols,
                    proposedGroupSymbols.front(),
                    proposedGroupSymbols.back(),
                    config.enableTurnoverLimit,
                    config.maxRebalanceTurnover);

                if (passesSignalThreshold && passesMaxTurnover) {
                    activeGroupSymbols = std::move(proposedGroupSymbols);
                    holdingDaysSinceRebalance = 1;
                } else {
                    ++holdingDaysSinceRebalance;
                }
            } else {
                ++holdingDaysSinceRebalance;
            }

            const int effectiveGroupCount = static_cast<int>(activeGroupSymbols.size());
            if (effectiveGroupCount <= 0) {
                return true;
            }

            maxEffectiveGroupCount = (std::max)(maxEffectiveGroupCount, effectiveGroupCount);
            bool dateGrouped = false;
            double topGroupReturnForDate = 0.0;
            double bottomGroupReturnForDate = 0.0;
            bool hasTopGroup = false;
            bool hasBottomGroup = false;
            for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
                const auto& groupSymbols = activeGroupSymbols[static_cast<size_t>(groupIndex)];
                if (groupSymbols.empty()) {
                    continue;
                }

                double groupReturn = 0.0;
                int sampleCount = 0;
                double minFactorValue = std::numeric_limits<double>::max();
                double maxFactorValue = std::numeric_limits<double>::lowest();
                for (const auto& symbol : groupSymbols) {
                    auto returnValueIt = returnResult.values.find(symbol);
                    if (returnValueIt == returnResult.values.end()) {
                        continue;
                    }

                    groupReturn += returnValueIt->second;
                    ++sampleCount;

                    auto factorValueIt = factorResult.values.find(symbol);
                    if (factorValueIt != factorResult.values.end()) {
                        minFactorValue = (std::min)(minFactorValue, factorValueIt->second);
                        maxFactorValue = (std::max)(maxFactorValue, factorValueIt->second);
                    }
                }

                if (sampleCount == 0) {
                    continue;
                }

                const double averageGroupReturn = groupReturn / static_cast<double>(sampleCount);
                aggregatedReturns[static_cast<size_t>(groupIndex)] += averageGroupReturn;
                aggregatedCounts[static_cast<size_t>(groupIndex)] += 1;
                aggregatedStockCounts[static_cast<size_t>(groupIndex)] += sampleCount;
                if (minFactorValue <= maxFactorValue) {
                    aggregatedMinFactorValues[static_cast<size_t>(groupIndex)] = (std::min)(aggregatedMinFactorValues[static_cast<size_t>(groupIndex)], minFactorValue);
                    aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)] = (std::max)(aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)], maxFactorValue);
                }
                if (groupIndex == 0) {
                    topGroupReturnForDate = averageGroupReturn;
                    hasTopGroup = true;
                }
                if (groupIndex == effectiveGroupCount - 1) {
                    bottomGroupReturnForDate = averageGroupReturn;
                    hasBottomGroup = true;
                }
                dateGrouped = true;
            }

            if (dateGrouped) {
                ++groupedDateCount;
                if (hasTopGroup && hasBottomGroup) {
                    longShortSeries.push_back(topGroupReturnForDate - bottomGroupReturnForDate - (2.0 * config.transactionCost));
                    longShortDates.push_back(factorResult.date);
                    const double longTurnover = factor::group_backtest::calculatePortfolioTurnover(previousLongSymbols, activeGroupSymbols.front());
                    const double shortTurnover = factor::group_backtest::calculatePortfolioTurnover(previousShortSymbols, activeGroupSymbols.back());
                    turnoverSeries.push_back((longTurnover + shortTurnover) / 2.0);
                    previousLongSymbols = activeGroupSymbols.front();
                    previousShortSymbols = activeGroupSymbols.back();
                }
            }

            return true;
        };

        std::vector<CalculationResult> unusedFactorResults;
        std::string factorFailureReason;
        size_t streamedFactorWorkUnits = 0;
        if (!calculateFactorSeries(config,
                                   marketContext,
                                   factor,
                                   progress,
                                   unusedFactorResults,
                                   &consumeFactorResult,
                                   &factorFailureReason,
                                   completedWorkUnits,
                                   totalWorkUnits,
                                   &streamedFactorWorkUnits)) {
            result.errorMessage = isCancelled(progress.taskId)
                ? "任务已取消"
                : (factorFailureReason.empty() ? "因子序列计算失败" : factorFailureReason);
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }
        completedWorkUnits += streamedFactorWorkUnits;

        result.icirResult.icSeries = std::move(icSeries);
        result.icirResult.icMean = factor::icir::calculateMean(result.icirResult.icSeries);
        result.icirResult.icStd = factor::icir::calculateStdDev(result.icirResult.icSeries, result.icirResult.icMean);
        result.icirResult.ir = result.icirResult.icStd > 0.0 ? result.icirResult.icMean / result.icirResult.icStd : 0.0;
        if (!result.icirResult.icSeries.empty()) {
            const auto positiveCount = std::count_if(result.icirResult.icSeries.begin(), result.icirResult.icSeries.end(), [](double value) {
                return value > 0.0;
            });
            result.icirResult.icPositiveRatio = static_cast<double>(positiveCount) / static_cast<double>(result.icirResult.icSeries.size());
        }

        result.groupResult.groupReturns.resize(static_cast<size_t>(maxEffectiveGroupCount), 0.0);
        result.groupResult.groupStockCounts.resize(static_cast<size_t>(maxEffectiveGroupCount), 0);
        result.groupResult.minFactorValues.resize(static_cast<size_t>(maxEffectiveGroupCount), 0.0);
        result.groupResult.maxFactorValues.resize(static_cast<size_t>(maxEffectiveGroupCount), 0.0);
        for (int groupIndex = 0; groupIndex < maxEffectiveGroupCount; ++groupIndex) {
            if (aggregatedCounts[static_cast<size_t>(groupIndex)] <= 0) {
                continue;
            }

            hasValidGroup = true;
            result.groupResult.groupReturns[static_cast<size_t>(groupIndex)] =
                aggregatedReturns[static_cast<size_t>(groupIndex)] / static_cast<double>(aggregatedCounts[static_cast<size_t>(groupIndex)]);
            result.groupResult.groupStockCounts[static_cast<size_t>(groupIndex)] =
                aggregatedStockCounts[static_cast<size_t>(groupIndex)] / aggregatedCounts[static_cast<size_t>(groupIndex)];
            result.groupResult.minFactorValues[static_cast<size_t>(groupIndex)] = aggregatedMinFactorValues[static_cast<size_t>(groupIndex)];
            result.groupResult.maxFactorValues[static_cast<size_t>(groupIndex)] = aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)];
        }
        if (hasValidGroup && result.groupResult.groupReturns.size() >= 2) {
            result.groupResult.topGroupReturn = result.groupResult.groupReturns.front();
            result.groupResult.bottomGroupReturn = result.groupResult.groupReturns.back();
            result.groupResult.longShortReturn = result.groupResult.topGroupReturn
                - result.groupResult.bottomGroupReturn
                - (2.0 * config.transactionCost);
        } else {
            result.status = "PARTIAL";
        }

        if (overlapDateCount == 0) {
            qWarning() << "FactorBacktestExecutor: 流式回测未生成有效因子/收益重叠样本"
                       << "instanceId=" << QString::fromStdString(config.instanceId);
        }

        ++completedWorkUnits;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "执行分组回测：生成多空曲线和分组收益序列");

        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "应用风控：止损、止盈、仓位和回撤约束");
        const auto riskResult = applyRiskControls(longShortSeries, config);
        longShortSeries = riskResult.adjustedReturns;
        const double annualizationFactor = annualizationFactorForPeriods(config.forwardDays);
        const double periodRiskFreeRate = config.riskFreeRate / annualizationFactor;
        const double averageLongShort = factor::icir::calculateMean(longShortSeries);
        const double longShortStd = factor::icir::calculateStdDev(longShortSeries, averageLongShort);
        const double averageTurnover = factor::icir::calculateMean(turnoverSeries);
        const double averageExcessReturn = averageLongShort - periodRiskFreeRate;

        result.annualReturn = averageLongShort * annualizationFactor;
        result.sharpeRatio = longShortStd > 0.0 ? (averageExcessReturn / longShortStd) * std::sqrt(annualizationFactor) : 0.0;
        result.maxDrawdown = calculateMaxDrawdown(longShortSeries);
        result.winRate = calculateWinRate(longShortSeries);
        result.profitFactor = calculateProfitFactor(longShortSeries);
        result.turnoverRate = averageTurnover * annualizationFactor * 100.0;

        ++completedWorkUnits;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "汇总结果：计算年化收益、夏普、回撤、胜率和换手率");

        const auto benchmarkSummary = calculateBenchmarkComparison(longShortSeries,
                                                                    longShortDates,
                                                                    config.forwardDays,
                                                                    config.riskFreeRate,
                                                                    benchmarkLookup);
        if (benchmarkSummary.hasValidAlignment) {
            result.benchmarkAnnualReturn = benchmarkSummary.benchmarkAnnualReturn;
            result.excessAnnualReturn = benchmarkSummary.excessAnnualReturn;
            result.trackingError = benchmarkSummary.trackingError;
            result.informationRatio = benchmarkSummary.informationRatio;
            result.beta = benchmarkSummary.beta;
            result.alpha = benchmarkSummary.alpha;
        }

        ++completedWorkUnits;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "生成最终结果：整理摘要并准备返回");

        result.volatility = longShortStd * std::sqrt(annualizationFactor);
        result.downsideDeviation = calculateDownsideDeviation(longShortSeries) * std::sqrt(annualizationFactor);
        result.sortinoRatio = result.downsideDeviation > 0.0
            ? (averageExcessReturn / result.downsideDeviation) * std::sqrt(annualizationFactor)
            : 0.0;
        result.calmarRatio = result.maxDrawdown > 0.0
            ? result.annualReturn / result.maxDrawdown
            : 0.0;
        result.valueAtRisk = calculateValueAtRisk(longShortSeries, 0.95);
        result.conditionalVaR = calculateConditionalVaR(longShortSeries, 0.95);
        result.riskTriggeredCount = riskResult.triggeredCount;
        result.riskControlSummary = riskResult.summary;
        result.dataStatus.availability = hasAnyFactorResult ? DataAvailability::AVAILABLE : DataAvailability::UNAVAILABLE;
        result.dataStatus.coverage = hasAnyFactorResult ? 1.0 : 0.0;
        result.dataStatus.message = hasAnyFactorResult ? "回测执行完成" : "未生成有效因子序列";
        result.dataCoverage = result.dataStatus.coverage;
        result.status = isCancelled(progress.taskId)
            ? "CANCELLED"
            : (result.status == "PARTIAL" ? "PARTIAL" : "SUCCESS");
        result.executionTimeMs = static_cast<int>(timer.elapsed());

        updateProgress(progress, 100, result.status == "CANCELLED" ? "已取消" : "回测完成");
        return result;
    } catch (const std::exception& e) {
        result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
        result.errorMessage = e.what();
        result.executionTimeMs = static_cast<int>(timer.elapsed());
        updateProgress(progress, progress.progress, result.status == "CANCELLED" ? "已取消" : "执行失败");
        return result;
    }
}

bool FactorBacktestExecutor::prepareData(const BacktestConfig& config,
                                        ProgressInfo& progress,
                                        std::shared_ptr<BaseFactor>& factor,
                                        std::string* failureReason)
{
    if (isCancelled(progress.taskId)) {
        if (failureReason) {
            *failureReason = "任务已取消";
        }
        return false;
    }

    factor = instanceManager_->createInstance(config.instanceId);
    if (!factor && failureReason) {
        *failureReason = "未能创建因子实例，请检查实例是否已激活且定义与实例表保持同步";
    }
    return static_cast<bool>(factor);
}

bool FactorBacktestExecutor::calculateFactorSeries(const BacktestConfig& config,
                                                   const ExecutionMarketContext& marketContext,
                                                   std::shared_ptr<BaseFactor> factor,
                                                   ProgressInfo& progress,
                                                   std::vector<CalculationResult>& factorResults,
                                                   std::function<bool(CalculationResult&&)>* resultConsumer,
                                                   std::string* failureReason,
                                                   size_t progressBaseUnits,
                                                   size_t totalWorkUnits,
                                                   size_t* completedWorkUnits)
{
    if (!factor) {
        if (failureReason) {
            *failureReason = "因子实例无效";
        }
        return false;
    }

    const auto& tradeDates = marketContext.tradeDates;
    if (tradeDates.empty()) {
        if (failureReason) {
            *failureReason = "未找到可用于回测的交易日";
        }
        return false;
    }

    updateProgress(progress,
                   progressPercentFromWork(progressBaseUnits, totalWorkUnits),
                   "计算因子序列");

    factorResults.clear();
    const bool collectResultsLocally = (resultConsumer == nullptr);
    if (collectResultsLocally) {
        factorResults.reserve(tradeDates.size());
    }
    size_t emptyCalculationCount = 0;
    bool producedAnyResult = false;
    std::string lastEmptyReason;
    std::shared_ptr<HistoricalView> historicalView;
    if (hasPreparedHistoricalData(config)) {
        historicalView = std::make_shared<CachedRowHistoricalView>(marketContext.arrowData);
    }

    qDebug() << "FactorBacktestExecutor: 计算因子序列"
             << "instanceId=" << QString::fromStdString(config.instanceId)
             << "tradeDateCount=" << static_cast<int>(tradeDates.size())
                  << "cachedBarCount=" << static_cast<qulonglong>(config.cachedBars.empty() && marketContext.arrowData
                      ? marketContext.arrowData->rowCount()
                      : config.cachedBars.size())
             << "allowedSymbolCount=" << static_cast<int>(config.allowedStockCodes.size())
             << "usingHistoricalView=" << static_cast<bool>(historicalView);

    const bool useDateParallelism = config.enableDateParallelism
        && threadPool_
        && tradeDates.size() > 1;
    if (useDateParallelism) {
        struct ChunkFactorCalculation {
            std::vector<CalculationResult> results;
            size_t emptyCalculationCount = 0;
            std::string lastEmptyReason;
            bool producedAnyResult = false;
            bool success = true;
            std::string failureReason;
        };

        std::atomic<size_t> processedDates{0};
        std::mutex progressMutex;
        auto publishProgress = [&](size_t processedDateTotal) {
            if (totalWorkUnits == 0) {
                return;
            }
            std::lock_guard<std::mutex> lock(progressMutex);
            updateProgress(progress,
                           progressPercentFromWork(progressBaseUnits + processedDateTotal, totalWorkUnits),
                           "计算因子序列");
        };

        auto calculateRange = [&](BaseFactor& activeFactor,
                                  size_t beginIndex,
                                  size_t endIndex) -> ChunkFactorCalculation {
            ChunkFactorCalculation chunk;
            try {
                std::vector<CalculationContext> contexts;
                contexts.reserve(endIndex - beginIndex);
                for (size_t i = beginIndex; i < endIndex; ++i) {
                    if (isCancelled(progress.taskId)) {
                        chunk.success = false;
                        chunk.failureReason = "任务已取消";
                        return chunk;
                    }

                    CalculationContext context;
                    context.date = tradeDates[i];
                    auto symbolsIt = marketContext.symbolsByDate.find(tradeDates[i]);
                    if (symbolsIt != marketContext.symbolsByDate.end()) {
                        context.symbols = symbolsIt->second;
                    }
                    context.historicalView = historicalView;
                    if (i < 3 || i + 1 == tradeDates.size()) {
                        qDebug() << "FactorBacktestExecutor: 单日样本"
                                 << "date=" << QString::fromStdString(context.date)
                                 << "symbolCount=" << static_cast<int>(context.symbols.size());
                    }
                    contexts.push_back(std::move(context));
                }

                QElapsedTimer calculateTimer;
                calculateTimer.start();
                std::vector<CalculationResult> calculations = activeFactor.calculateBatch(contexts);
                const qint64 calculationElapsedMs = calculateTimer.elapsed();
                if (calculationElapsedMs >= 300) {
                    qDebug() << "FactorBacktestExecutor: 因子批量计算耗时较长"
                             << "beginIndex=" << static_cast<qulonglong>(beginIndex)
                             << "endIndex=" << static_cast<qulonglong>(endIndex)
                             << "resultCount=" << static_cast<int>(calculations.size())
                             << "elapsedMs=" << calculationElapsedMs
                             << "usingHistoricalView=" << static_cast<bool>(historicalView);
                }

                chunk.results.reserve(calculations.size());
                for (size_t calculationIndex = 0; calculationIndex < calculations.size(); ++calculationIndex) {
                    auto& calculation = calculations[calculationIndex];
                    if (!calculation.dataStatus.isValid()) {
                        chunk.success = false;
                        chunk.failureReason = calculation.dataStatus.message.empty()
                            ? "因子序列计算失败"
                            : calculation.dataStatus.message;
                        return chunk;
                    }
                    if (!calculation.isEmpty()) {
                        chunk.results.push_back(std::move(calculation));
                        chunk.producedAnyResult = true;
                    } else {
                        if (calculation.metadata.has("empty_reason")) {
                            chunk.lastEmptyReason = calculation.metadata.get("empty_reason").asString();
                        }
                        ++chunk.emptyCalculationCount;
                    }

                    const size_t processedDateTotal = processedDates.fetch_add(1) + 1;
                    publishProgress(processedDateTotal);
                }
            } catch (const std::exception& e) {
                chunk.success = false;
                chunk.failureReason = e.what();
            }
            return chunk;
        };

        const size_t executorWorkers = (std::max)(size_t{1}, threadPool_->getWorkerCount());
        const size_t parallelWorkerLimit = hasPreparedHistoricalData(config)
            ? (std::min)(executorWorkers, size_t{4})
            : executorWorkers;
        const size_t chunkCount = (std::min)(tradeDates.size(), parallelWorkerLimit);

        if (chunkCount > 1) {
            std::vector<std::pair<size_t, size_t>> ranges;
            const size_t defaultChunkSize = (tradeDates.size() + chunkCount - 1) / chunkCount;
            const size_t chunkSize = resultConsumer != nullptr
                ? (std::min)(size_t{32}, defaultChunkSize)
                : defaultChunkSize;
            ranges.reserve((tradeDates.size() + chunkSize - 1) / chunkSize);
            for (size_t beginIndex = 0; beginIndex < tradeDates.size(); beginIndex += chunkSize) {
                ranges.emplace_back(beginIndex, std::min(tradeDates.size(), beginIndex + chunkSize));
            }

            const auto firstRange = ranges.front();
            qDebug() << "FactorBacktestExecutor: 日期并行分片启动"
                     << "instanceId=" << QString::fromStdString(config.instanceId)
                     << "chunkCount=" << static_cast<int>(ranges.size())
                     << "threadId=" << threadIdText();
            ChunkFactorCalculation firstChunk = calculateRange(*factor, firstRange.first, firstRange.second);
            if (!firstChunk.success) {
                if (failureReason) {
                    *failureReason = firstChunk.failureReason.empty() ? "因子序列计算失败" : firstChunk.failureReason;
                }
                return false;
            }

            size_t emptyCalculationCount = firstChunk.emptyCalculationCount;
            std::string lastEmptyReason = firstChunk.lastEmptyReason;
            bool producedAnyResult = firstChunk.producedAnyResult;

            if (resultConsumer != nullptr) {
                for (auto& calculation : firstChunk.results) {
                    if (!(*resultConsumer)(std::move(calculation))) {
                        if (failureReason) {
                            *failureReason = "任务已取消";
                        }
                        return false;
                    }
                }
            } else {
                factorResults.insert(factorResults.end(),
                                     std::make_move_iterator(firstChunk.results.begin()),
                                     std::make_move_iterator(firstChunk.results.end()));
            }

            auto submitRange = [this,
                                &config,
                                historicalView,
                                calculateRange,
                                &progress](size_t beginIndex, size_t endIndex) {
                return threadPool_->submit(
                    [this,
                     config,
                     historicalView,
                     beginIndex,
                     endIndex,
                     calculateRange,
                     &progress]() mutable {
                        ChunkFactorCalculation chunk;
                        QElapsedTimer chunkTimer;
                        chunkTimer.start();
                        qDebug() << "FactorBacktestExecutor: 日期分片开始"
                                 << "instanceId=" << QString::fromStdString(config.instanceId)
                                 << "range=" << rangeText(beginIndex, endIndex)
                                 << "threadId=" << threadIdText();
                        try {
                            auto chunkFactor = instanceManager_ ? instanceManager_->createInstance(config.instanceId) : nullptr;
                            if (!chunkFactor) {
                                chunk.success = false;
                                chunk.failureReason = "未能创建因子实例，请检查实例是否已激活且定义与实例表保持同步";
                            } else {
                                chunk = calculateRange(*chunkFactor, beginIndex, endIndex);
                            }
                        } catch (const std::exception& e) {
                            chunk.success = false;
                            chunk.failureReason = e.what();
                        }

                        qDebug() << "FactorBacktestExecutor: 日期分片结束"
                                 << "instanceId=" << QString::fromStdString(config.instanceId)
                                 << "range=" << rangeText(beginIndex, endIndex)
                                 << "threadId=" << threadIdText()
                                 << "elapsedMs=" << chunkTimer.elapsed()
                                 << "success=" << chunk.success;
                        return chunk;
                    });
            };

            const size_t maxInflightFutures = resultConsumer != nullptr
                ? (std::max)(size_t{1}, parallelWorkerLimit)
                : (ranges.size() > 1 ? ranges.size() - 1 : 0);
            std::deque<std::future<ChunkFactorCalculation>> futures;
            size_t nextRangeIndex = 1;

            while (nextRangeIndex < ranges.size() || !futures.empty()) {
                while (nextRangeIndex < ranges.size() && futures.size() < maxInflightFutures) {
                    const auto [beginIndex, endIndex] = ranges[nextRangeIndex++];
                    futures.emplace_back(submitRange(beginIndex, endIndex));
                }

                if (futures.empty()) {
                    break;
                }

                ChunkFactorCalculation chunk = futures.front().get();
                futures.pop_front();
                if (!chunk.success) {
                    if (failureReason) {
                        *failureReason = chunk.failureReason.empty() ? "因子序列计算失败" : chunk.failureReason;
                    }
                    return false;
                }

                if (resultConsumer != nullptr) {
                    for (auto& calculation : chunk.results) {
                        if (!(*resultConsumer)(std::move(calculation))) {
                            if (failureReason) {
                                *failureReason = "任务已取消";
                            }
                            return false;
                        }
                    }
                } else {
                    factorResults.insert(factorResults.end(),
                                         std::make_move_iterator(chunk.results.begin()),
                                         std::make_move_iterator(chunk.results.end()));
                }
                emptyCalculationCount += chunk.emptyCalculationCount;
                producedAnyResult = producedAnyResult || chunk.producedAnyResult;
                if (!chunk.lastEmptyReason.empty()) {
                    lastEmptyReason = chunk.lastEmptyReason;
                }
            }

            if (!producedAnyResult && factorResults.empty() && failureReason) {
                if (emptyCalculationCount == tradeDates.size() && !lastEmptyReason.empty()) {
                    *failureReason = lastEmptyReason + "，因此因子在全部交易日都未产出有效值";
                } else {
                    *failureReason = emptyCalculationCount == tradeDates.size()
                        ? "因子在全部交易日都未产出有效值，常见原因包括样本不足、字段缺失、参数窗口过长或筛选后全部被剔除"
                        : "未生成有效因子序列";
                }
            }
            if (completedWorkUnits) {
                *completedWorkUnits = tradeDates.size();
            }
            return producedAnyResult || !factorResults.empty();
        }
    }

    const size_t sequentialChunkSize = resultConsumer != nullptr ? size_t{32} : tradeDates.size();
    size_t processedCount = 0;
    for (size_t beginIndex = 0; beginIndex < tradeDates.size(); beginIndex += sequentialChunkSize) {
        const size_t endIndex = std::min(tradeDates.size(), beginIndex + sequentialChunkSize);
        std::vector<CalculationContext> contexts;
        contexts.reserve(endIndex - beginIndex);
        for (size_t i = beginIndex; i < endIndex; ++i) {
            if (isCancelled(progress.taskId)) {
                return false;
            }

            CalculationContext context;
            context.date = tradeDates[i];
            auto symbolsIt = marketContext.symbolsByDate.find(tradeDates[i]);
            if (symbolsIt != marketContext.symbolsByDate.end()) {
                context.symbols = symbolsIt->second;
            }
            context.historicalView = historicalView;
            if (i < 3 || i + 1 == tradeDates.size()) {
                qDebug() << "FactorBacktestExecutor: 单日样本"
                         << "date=" << QString::fromStdString(context.date)
                         << "symbolCount=" << static_cast<int>(context.symbols.size());
            }

            contexts.push_back(std::move(context));
        }

        QElapsedTimer calculateTimer;
        calculateTimer.start();
        std::vector<CalculationResult> calculations = factor->calculateBatch(contexts);
        const qint64 calculationElapsedMs = calculateTimer.elapsed();
        if (calculationElapsedMs >= 300) {
            qDebug() << "FactorBacktestExecutor: 因子批量计算耗时较长"
                     << "beginIndex=" << static_cast<qulonglong>(beginIndex)
                     << "endIndex=" << static_cast<qulonglong>(endIndex)
                     << "resultCount=" << static_cast<int>(calculations.size())
                     << "elapsedMs=" << calculationElapsedMs
                     << "usingHistoricalView=" << static_cast<bool>(historicalView);
        }

        if (collectResultsLocally) {
            factorResults.reserve(factorResults.size() + calculations.size());
        }
        for (auto& calculation : calculations) {
            if (isCancelled(progress.taskId)) {
                return false;
            }

            if (!calculation.dataStatus.isValid()) {
                if (failureReason) {
                    *failureReason = calculation.dataStatus.message.empty()
                        ? "因子序列计算失败"
                        : calculation.dataStatus.message;
                }
                return false;
            }
            if (!calculation.isEmpty()) {
                if (resultConsumer != nullptr) {
                    if (!(*resultConsumer)(std::move(calculation))) {
                        return false;
                    }
                } else {
                    factorResults.push_back(std::move(calculation));
                }
                producedAnyResult = true;
            } else {
                if (calculation.metadata.has("empty_reason")) {
                    lastEmptyReason = calculation.metadata.get("empty_reason").asString();
                }
                ++emptyCalculationCount;
            }

            ++processedCount;

            if (totalWorkUnits > 0) {
                updateProgress(progress,
                               progressPercentFromWork(progressBaseUnits + processedCount, totalWorkUnits),
                               "计算因子序列");
            }
        }
    }
    if (!producedAnyResult && factorResults.empty() && failureReason) {
        if (emptyCalculationCount == tradeDates.size() && !lastEmptyReason.empty()) {
            *failureReason = lastEmptyReason + "，因此因子在全部交易日都未产出有效值";
        } else {
            *failureReason = emptyCalculationCount == tradeDates.size()
                ? "因子在全部交易日都未产出有效值，常见原因包括样本不足、字段缺失、参数窗口过长或筛选后全部被剔除"
                : "未生成有效因子序列";
        }
    }
    if (completedWorkUnits) {
        *completedWorkUnits = tradeDates.size();
    }
    return producedAnyResult || !factorResults.empty();
}

bool FactorBacktestExecutor::calculateReturnSeries(const BacktestConfig& config,
                                                   const ExecutionMarketContext& marketContext,
                                                   const CachedMarketIndex* cachedMarketIndex,
                                                   ProgressInfo& progress,
                                                   std::vector<CalculationResult>& returnResults,
                                                   size_t progressBaseUnits,
                                                   size_t totalWorkUnits,
                                                   size_t* completedWorkUnits)
{
    const auto& tradeDates = marketContext.tradeDates;
    if (tradeDates.empty()) {
        return false;
    }

    updateProgress(progress,
                   progressPercentFromWork(progressBaseUnits, totalWorkUnits),
                   "计算未来收益");

    returnResults.clear();
    returnResults.reserve(tradeDates.size());

    const bool useDateParallelism = config.enableDateParallelism
        && threadPool_
        && tradeDates.size() > 1;
    if (useDateParallelism) {
        struct ChunkReturnCalculation {
            std::vector<CalculationResult> results;
            size_t processedDateCount = 0;
            bool success = true;
            std::string failureReason;
        };

        std::atomic<size_t> processedDates{0};
        std::mutex progressMutex;
        auto publishProgress = [&](size_t processedDateTotal) {
            if (totalWorkUnits == 0) {
                return;
            }
            std::lock_guard<std::mutex> lock(progressMutex);
            updateProgress(progress,
                           progressPercentFromWork(progressBaseUnits + processedDateTotal, totalWorkUnits),
                           "计算未来收益");
        };

        auto calculateRange = [&](size_t beginIndex, size_t endIndex) -> ChunkReturnCalculation {
            ChunkReturnCalculation chunk;
            try {
                chunk.results.reserve(endIndex - beginIndex);
                for (size_t i = beginIndex; i < endIndex; ++i) {
                    if (isCancelled(progress.taskId)) {
                        chunk.success = false;
                        chunk.failureReason = "任务已取消";
                        return chunk;
                    }

                    CalculationResult result;
                    result.calculationId = foundation::utils::Uuid::generate_v4();
                    result.date = tradeDates[i];
                    result.metadata = foundation::json::JsonFacade::createObject();

                    const auto symbolsIt = marketContext.symbolsByDate.find(tradeDates[i]);
                    if (symbolsIt == marketContext.symbolsByDate.end()) {
                        const size_t processedDateTotal = processedDates.fetch_add(1) + 1;
                        chunk.processedDateCount = processedDateTotal;
                        publishProgress(processedDateTotal);
                        continue;
                    }

                    const auto& symbols = symbolsIt->second;
                    for (const auto& symbol : symbols) {
                        const double futureReturn = calculateFutureReturn(symbol,
                                                                          tradeDates[i],
                                                                          config.forwardDays,
                                                                          config,
                                                                          cachedMarketIndex);
                        if (std::isfinite(futureReturn)) {
                            result.values[symbol] = futureReturn;
                        }
                    }

                    if (!result.isEmpty()) {
                        chunk.results.push_back(std::move(result));
                    }

                    const size_t processedDateTotal = processedDates.fetch_add(1) + 1;
                    chunk.processedDateCount = processedDateTotal;
                    publishProgress(processedDateTotal);
                }
            } catch (const std::exception& e) {
                chunk.success = false;
                chunk.failureReason = e.what();
            }
            return chunk;
        };

        const size_t executorWorkers = (std::max)(size_t{1}, threadPool_->getWorkerCount());
        const size_t parallelWorkerLimit = hasPreparedHistoricalData(config)
            ? (std::min)(executorWorkers, size_t{4})
            : executorWorkers;
        const size_t chunkCount = (std::min)(tradeDates.size(), parallelWorkerLimit);

        if (chunkCount > 1) {
            std::vector<std::pair<size_t, size_t>> ranges;
            ranges.reserve(chunkCount);
            const size_t chunkSize = (tradeDates.size() + chunkCount - 1) / chunkCount;
            for (size_t beginIndex = 0; beginIndex < tradeDates.size(); beginIndex += chunkSize) {
                ranges.emplace_back(beginIndex, std::min(tradeDates.size(), beginIndex + chunkSize));
            }

            const auto firstRange = ranges.front();
            qDebug() << "FactorBacktestExecutor: 收益日期并行分片启动"
                     << "instanceId=" << QString::fromStdString(config.instanceId)
                     << "chunkCount=" << static_cast<int>(ranges.size())
                     << "threadId=" << threadIdText();
            ChunkReturnCalculation firstChunk = calculateRange(firstRange.first, firstRange.second);
            if (!firstChunk.success) {
                return false;
            }

            returnResults.insert(returnResults.end(),
                                 std::make_move_iterator(firstChunk.results.begin()),
                                 std::make_move_iterator(firstChunk.results.end()));

            std::vector<std::future<ChunkReturnCalculation>> futures;
            futures.reserve(ranges.size() > 1 ? ranges.size() - 1 : 0);
            for (size_t rangeIndex = 1; rangeIndex < ranges.size(); ++rangeIndex) {
                const auto [beginIndex, endIndex] = ranges[rangeIndex];
                futures.emplace_back(threadPool_->submit(
                    [this,
                     config,
                     &marketContext,
                     beginIndex,
                     endIndex,
                     calculateRange,
                     &progress]() mutable {
                        ChunkReturnCalculation chunk;
                        QElapsedTimer chunkTimer;
                        chunkTimer.start();
                        qDebug() << "FactorBacktestExecutor: 收益日期分片开始"
                                 << "instanceId=" << QString::fromStdString(config.instanceId)
                                 << "range=" << rangeText(beginIndex, endIndex)
                                 << "threadId=" << threadIdText();
                        try {
                            chunk = calculateRange(beginIndex, endIndex);
                        } catch (const std::exception& e) {
                            chunk.success = false;
                            chunk.failureReason = e.what();
                        }

                        qDebug() << "FactorBacktestExecutor: 收益日期分片结束"
                                 << "instanceId=" << QString::fromStdString(config.instanceId)
                                 << "range=" << rangeText(beginIndex, endIndex)
                                 << "threadId=" << threadIdText()
                                 << "elapsedMs=" << chunkTimer.elapsed()
                                 << "success=" << chunk.success;
                        return chunk;
                    }));
            }

            for (auto& future : futures) {
                ChunkReturnCalculation chunk = future.get();
                if (!chunk.success) {
                    return false;
                }
                returnResults.insert(returnResults.end(),
                                     std::make_move_iterator(chunk.results.begin()),
                                     std::make_move_iterator(chunk.results.end()));
            }

            if (completedWorkUnits) {
                *completedWorkUnits = tradeDates.size();
            }

            return !returnResults.empty();
        }
    }

    for (size_t i = 0; i < tradeDates.size(); ++i) {
        if (isCancelled(progress.taskId)) {
            return false;
        }

        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = tradeDates[i];
        result.metadata = foundation::json::JsonFacade::createObject();

        const auto symbolsIt = marketContext.symbolsByDate.find(tradeDates[i]);
        if (symbolsIt == marketContext.symbolsByDate.end()) {
            continue;
        }

        const auto& symbols = symbolsIt->second;
        for (const auto& symbol : symbols) {
            const double futureReturn = calculateFutureReturn(symbol,
                                                              tradeDates[i],
                                                              config.forwardDays,
                                                              config,
                                                              cachedMarketIndex);
            if (std::isfinite(futureReturn)) {
                result.values[symbol] = futureReturn;
            }
        }

        if (!result.isEmpty()) {
            returnResults.push_back(std::move(result));
        }

        if (totalWorkUnits > 0) {
            updateProgress(progress,
                           progressPercentFromWork(progressBaseUnits + i + 1, totalWorkUnits),
                           "计算未来收益");
        }
    }

    if (completedWorkUnits) {
        *completedWorkUnits = tradeDates.size();
    }
    return !returnResults.empty();
}

bool FactorBacktestExecutor::calculateICIR(const std::vector<CalculationResult>& factorResults,
                                           const std::vector<CalculationResult>& returnResults,
                                           ProgressInfo& progress,
                                           ICIRResult& icirResult)
{
    auto summary = factor::icir::aggregate(factorResults, returnResults);
    icirResult = std::move(summary.result);
    return summary.hasValidSeries;
}

bool FactorBacktestExecutor::executeGroupBacktest(const std::vector<CalculationResult>& factorResults,
                                                  const std::vector<CalculationResult>& returnResults,
                                                  const BacktestConfig& config,
                                                  ProgressInfo& progress,
                                                  GroupBacktestResult& groupResult,
                                                  std::vector<double>* longShortSeries,
                                                  std::vector<double>* turnoverSeries,
                                                  std::vector<std::string>* longShortDates,
                                                  std::string* failureReason)
{
    auto summary = factor::group_backtest::aggregate(
        factorResults,
        returnResults,
        config);
    groupResult = std::move(summary.groupResult);
    if (longShortSeries) {
        *longShortSeries = std::move(summary.longShortReturnsByDate);
    }
    if (turnoverSeries) {
        *turnoverSeries = std::move(summary.longShortTurnoversByDate);
    }
    if (longShortDates) {
        *longShortDates = std::move(summary.longShortDatesByDate);
    }

    if (!summary.hasValidGroup && failureReason) {
        if (returnResults.empty()) {
            *failureReason = "未生成未来收益序列，请检查所选数据区间是否至少覆盖到下一个交易日";
        } else if (summary.overlapDateCount == 0) {
            *failureReason = "因子值与未来收益没有重叠样本，无法执行分组回测";
        } else if (summary.maxMatchedStocks <= 1) {
            *failureReason = "有效股票数不足，至少需要 2 只股票才能执行分组回测";
        } else {
            *failureReason = "未生成有效分组回测结果：最大有效股票数为 " + std::to_string(summary.maxMatchedStocks)
                + "，请求分组数为 " + std::to_string(config.numGroups)
                + "，成功分组日期数为 " + std::to_string(summary.groupedDateCount);
        }
    }

    return summary.hasValidGroup && !summary.longShortReturnsByDate.empty();
}

double FactorBacktestExecutor::calculateFutureReturn(const std::string& symbol,
                                                     const std::string& startDate,
                                                     int forwardDays,
                                                     const BacktestConfig& config,
                                                     const CachedMarketIndex* cachedMarketIndex)
{
    const std::string cacheKey = buildFutureReturnCacheKey(symbol, startDate, forwardDays, config);
    {
        std::lock_guard<std::mutex> lock(futureReturnCacheMutex_);
        const auto cacheIt = futureReturnCache_.find(cacheKey);
        if (cacheIt != futureReturnCache_.end()) {
            return cacheIt->second;
        }
    }

    double calculatedFutureReturn = std::numeric_limits<double>::quiet_NaN();
    if (hasPreparedHistoricalData(config)) {
        if (!cachedMarketIndex) {
            throw std::logic_error("FactorBacktestExecutor: cached market index is required for cached-bar future return calculation");
        }

        const auto symbolIt = cachedMarketIndex->closeSeriesBySymbol.find(symbol);
        if (symbolIt != cachedMarketIndex->closeSeriesBySymbol.end()) {
            const auto& series = symbolIt->second;
            const auto startIndexIt = std::lower_bound(series.begin(), series.end(), startDate,
                [](const CachedMarketIndex::CachedSymbolBar& bar, const std::string& value) {
                    return bar.tradeDate < value;
                });
            if (startIndexIt != series.end() && startIndexIt->tradeDate == startDate) {
                const size_t startIndex = static_cast<size_t>(std::distance(series.begin(), startIndexIt));
                if (forwardDays > 0 && startIndex < series.size()) {
                    calculatedFutureReturn = series[startIndex].futureReturn;
                }
            }
        }
    } else {
        Q_UNUSED(symbol);
        Q_UNUSED(startDate);
        Q_UNUSED(forwardDays);
        return std::numeric_limits<double>::quiet_NaN();
    }

    if (std::isfinite(calculatedFutureReturn)) {
        std::lock_guard<std::mutex> lock(futureReturnCacheMutex_);
        if (futureReturnCache_.size() >= kFutureReturnCacheLimit) {
            futureReturnCache_.clear();
        }
        futureReturnCache_[cacheKey] = calculatedFutureReturn;
    }

    return calculatedFutureReturn;
}

bool FactorBacktestExecutor::prepareExecutionMarketContext(const BacktestConfig& config,
                                                           ExecutionMarketContext& marketContext,
                                                           CachedMarketIndex* cachedMarketIndex,
                                                           std::string* failureReason,
                                                           ProgressInfo* progress)
{
    if (hasPreparedHistoricalData(config)) {
        if (!cachedMarketIndex) {
            if (failureReason) {
                *failureReason = "缓存行情索引未初始化";
            }
            return false;
        }
        return prepareCachedExecutionMarketContext(config, marketContext, *cachedMarketIndex, failureReason, progress);
    }

    if (failureReason) {
        *failureReason = "已移除非 HistoricalView 回测路径，请先由引擎构建缓存行情视图";
    }
    return false;
}

bool FactorBacktestExecutor::prepareCachedExecutionMarketContext(const BacktestConfig& config,
                                                                 ExecutionMarketContext& marketContext,
                                                                 CachedMarketIndex& cachedIndex,
                                                                 std::string* failureReason,
                                                                 ProgressInfo* progress)
{
    const std::string cacheKey = buildCachedMarketIndexKey(config);

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(0, 4),
                       "准备回测上下文：扫描缓存集并构建交易日索引");
    }

    {
        std::lock_guard<std::mutex> lock(cachedMarketIndexMutex_);
        const auto cacheIt = cachedMarketIndexCache_.find(cacheKey);
        if (cacheIt != cachedMarketIndexCache_.end()) {
            cachedIndex = cacheIt->second;
        }
    }

    if (cachedIndex.tradeDates.empty() && cachedIndex.symbolsByDate.empty()) {
        if (config.preparedArrowData) {
            cachedIndex = buildCachedMarketIndex(*config.preparedArrowData, config.forwardDays);
        } else {
            cachedIndex = buildCachedMarketIndex(config.cachedBars, config.forwardDays);
        }
        if (cachedIndex.tradeDates.empty()) {
            if (failureReason) {
                *failureReason = "缓存集没有可用于回测的交易日";
            }
            return false;
        }

        std::lock_guard<std::mutex> lock(cachedMarketIndexMutex_);
        cachedMarketIndexCache_[cacheKey] = cachedIndex;
    }

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(1, 4),
                       "准备回测上下文：交易日索引已就绪，整理日期范围");
    }

    marketContext.tradeDates.clear();
    marketContext.tradeDates.reserve(cachedIndex.tradeDates.size());
    for (const auto& tradeDate : cachedIndex.tradeDates) {
        if ((!config.startDate.empty() && tradeDate < config.startDate)
            || (!config.endDate.empty() && tradeDate > config.endDate)) {
            continue;
        }
        marketContext.tradeDates.push_back(tradeDate);
    }

    if (marketContext.tradeDates.empty()) {
        if (failureReason) {
            *failureReason = "缓存集中未找到所选日期范围内的交易日";
        }
        return false;
    }

    // cachedBars 分支直接构建 Arrow 列式数据，后续查询可以复用同一份上下文。
    marketContext.arrowData = config.preparedArrowData
        ? config.preparedArrowData
        : factor::ArrowMarketData::fromCachedBars(config.cachedBars);
    if (!marketContext.arrowData) {
        if (failureReason) {
            *failureReason = "缓存行情 Arrow 数据构建失败";
        }
        return false;
    }

    marketContext.allowedSymbols = std::unordered_set<std::string>(
        config.allowedStockCodes.begin(),
        config.allowedStockCodes.end());

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(2, 4),
                       "准备回测上下文：整理股票池与交易日交集");
    }

    marketContext.symbolsByDate.clear();
    marketContext.symbolsByDate.reserve(marketContext.tradeDates.size());

    for (const auto& tradeDate : marketContext.tradeDates) {
        const auto symbolsIt = cachedIndex.symbolsByDate.find(tradeDate);
        if (symbolsIt == cachedIndex.symbolsByDate.end()) {
            marketContext.symbolsByDate.emplace(tradeDate, std::vector<std::string>{});
            continue;
        }

        const auto& allSymbols = symbolsIt->second;
        if (marketContext.allowedSymbols.empty()) {
            marketContext.symbolsByDate.emplace(tradeDate, allSymbols);
            continue;
        }

        std::vector<std::string> filteredSymbols;
        filteredSymbols.reserve(allSymbols.size());
        for (const auto& symbol : allSymbols) {
            if (marketContext.allowedSymbols.find(symbol) != marketContext.allowedSymbols.end()) {
                filteredSymbols.push_back(symbol);
            }
        }
        marketContext.symbolsByDate.emplace(tradeDate, std::move(filteredSymbols));
    }

    {
        std::lock_guard<std::mutex> lock(marketContextMutex_);
        marketContextCache_[buildMarketContextCacheKey(config)] = marketContext;
    }

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(4, 4),
                       "准备回测上下文：缓存集索引可复用，准备进入因子计算");
    }

    qDebug() << "FactorBacktestExecutor: 复用缓存集索引"
             << "datasetId=" << config.datasetId
             << "tradeDateCount=" << static_cast<int>(marketContext.tradeDates.size())
             << "allowedStockCount=" << static_cast<int>(config.allowedStockCodes.size())
              << "cachedBarCount=" << static_cast<qulonglong>(config.cachedBars.empty() && marketContext.arrowData
                  ? marketContext.arrowData->rowCount()
                  : config.cachedBars.size());

    return true;
}

FactorBacktestExecutor::CachedMarketIndex FactorBacktestExecutor::buildCachedMarketIndex(const std::vector<CachedMarketBar>& cachedBars,
                                                                                       int forwardDays) const
{
    CachedMarketIndex index;
    std::unordered_map<std::string, std::unordered_set<std::string>> symbolSetsByDate;
    std::unordered_set<std::string> tradeDateSet;

    symbolSetsByDate.reserve(cachedBars.size());
    tradeDateSet.reserve(cachedBars.size());

    for (const auto& bar : cachedBars) {
        const std::string normalizedTradeDate = factor::cached_bars::normalizeTradeDate(bar.tradeDate);
        if (normalizedTradeDate.empty() || bar.symbol.empty()) {
            continue;
        }

        tradeDateSet.insert(normalizedTradeDate);
        symbolSetsByDate[normalizedTradeDate].insert(bar.symbol);
        index.closeSeriesBySymbol[bar.symbol].push_back(CachedMarketIndex::CachedSymbolBar{normalizedTradeDate, bar.close});
    }

    index.tradeDates.assign(tradeDateSet.begin(), tradeDateSet.end());
    std::sort(index.tradeDates.begin(), index.tradeDates.end());

    index.symbolsByDate.reserve(symbolSetsByDate.size());
    for (auto& [tradeDate, symbols] : symbolSetsByDate) {
        std::vector<std::string> symbolList(symbols.begin(), symbols.end());
        std::sort(symbolList.begin(), symbolList.end());
        index.symbolsByDate.emplace(tradeDate, std::move(symbolList));
    }

    for (auto& [symbol, closeSeries] : index.closeSeriesBySymbol) {
        std::sort(closeSeries.begin(), closeSeries.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.tradeDate < rhs.tradeDate;
        });
        if (forwardDays > 0) {
            for (size_t i = 0; i < closeSeries.size(); ++i) {
                const size_t futureIndex = i + static_cast<size_t>(forwardDays);
                if (futureIndex >= closeSeries.size()) {
                    continue;
                }

                const double startClose = closeSeries[i].close;
                const double endClose = closeSeries[futureIndex].close;
                if (startClose > 0.0 && endClose > 0.0 && std::isfinite(startClose) && std::isfinite(endClose)) {
                    closeSeries[i].futureReturn = (endClose - startClose) / startClose;
                }
            }
        }
        (void)symbol;
    }

    return index;
}

FactorBacktestExecutor::CachedMarketIndex FactorBacktestExecutor::buildCachedMarketIndex(const ArrowMarketData& arrowData,
                                                                                         int forwardDays) const
{
    CachedMarketIndex index;
    index.tradeDates = arrowData.dates();

    for (const auto& tradeDate : index.tradeDates) {
        auto symbols = arrowData.getAvailableSymbols(tradeDate);
        std::sort(symbols.begin(), symbols.end());
        index.symbolsByDate.emplace(tradeDate, std::move(symbols));
    }

    for (const auto& symbol : arrowData.symbols()) {
        auto& closeSeries = index.closeSeriesBySymbol[symbol];
        closeSeries.reserve(index.tradeDates.size());
        for (const auto& tradeDate : index.tradeDates) {
            const double close = arrowData.getValue(symbol, tradeDate, "close");
            if (!std::isfinite(close) || close <= 0.0) {
                continue;
            }

            closeSeries.push_back(CachedMarketIndex::CachedSymbolBar{tradeDate, close});
        }

        if (forwardDays > 0) {
            for (size_t i = 0; i < closeSeries.size(); ++i) {
                const size_t futureIndex = i + static_cast<size_t>(forwardDays);
                if (futureIndex >= closeSeries.size()) {
                    continue;
                }

                const double startClose = closeSeries[i].close;
                const double endClose = closeSeries[futureIndex].close;
                if (startClose > 0.0 && endClose > 0.0 && std::isfinite(startClose) && std::isfinite(endClose)) {
                    closeSeries[i].futureReturn = (endClose - startClose) / startClose;
                }
            }
        }
    }

    return index;
}

void FactorBacktestExecutor::updateProgress(ProgressInfo& progress,
                                            int newProgress,
                                            const std::string& step)
{
    progress.progress = newProgress;
    progress.currentStep = step;
    if (progress.status.empty()) {
        progress.status = "RUNNING";
    }

    std::lock_guard<std::mutex> lock(taskMutex_);
    auto it = activeTasks_.find(progress.taskId);
    if (it != activeTasks_.end()) {
        it->second = progress;
    } else {
        activeTasks_[progress.taskId] = progress;
    }
}

bool FactorBacktestExecutor::isCancelled(const foundation::utils::Uuid& taskId) const
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    return cancelledTasks_.find(taskId.to_string()) != cancelledTasks_.end();
}

} // namespace factor
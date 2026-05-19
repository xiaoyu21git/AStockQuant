#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/ArrowMarketData.h"
#include "domain/factor/include/FactorBacktestCachedBarUtils.h"
#include "domain/factor/include/FactorBacktestGroupingUtils.h"
#include "domain/factor/include/FactorBacktestIcUtils.h"
#include "domain/factor/include/FactorBacktestMetricsCalculator.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"
#include "ui/bridge/include/DataServiceCache.h"

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

bool hasPreparedHistoricalData(const factor::BacktestConfig& config)
{
    return static_cast<bool>(config.preparedArrowData) || !config.cachedBars.empty();
}

size_t streamingCalculationChunkSize(const factor::BacktestConfig& config,
                                    size_t preferredChunkSize)
{
    if (preferredChunkSize == 0) {
        return size_t{1};
    }

    const size_t maxChunkSize = hasPreparedHistoricalData(config)
        ? size_t{96}
        : size_t{64};
    return (std::min)(preferredChunkSize, maxChunkSize);
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

int findTradeDateIndex(const std::vector<std::string>& tradeDates,
                       const std::string& tradeDate)
{
    const auto tradeDateIt = std::lower_bound(tradeDates.begin(), tradeDates.end(), tradeDate);
    if (tradeDateIt == tradeDates.end() || *tradeDateIt != tradeDate) {
        return -1;
    }

    return static_cast<int>(std::distance(tradeDates.begin(), tradeDateIt));
}

void finalizeCachedSymbolSeries(FactorBacktestExecutor::CachedMarketIndex::CachedSymbolSeries& series,
                                int forwardDays,
                                bool isSortedByTradeDateIndex)
{
    if (!isSortedByTradeDateIndex) {
        std::vector<size_t> order(series.tradeDateIndices.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::sort(order.begin(), order.end(), [&series](size_t lhs, size_t rhs) {
            return series.tradeDateIndices[lhs] < series.tradeDateIndices[rhs];
        });

        std::vector<int> sortedTradeDateIndices;
        std::vector<double> sortedCloses;
        sortedTradeDateIndices.reserve(order.size());
        sortedCloses.reserve(order.size());
        for (size_t index : order) {
            sortedTradeDateIndices.push_back(series.tradeDateIndices[index]);
            sortedCloses.push_back(series.closes[index]);
        }
        series.tradeDateIndices = std::move(sortedTradeDateIndices);
        series.closes = std::move(sortedCloses);
    }

    series.futureReturns.assign(series.tradeDateIndices.size(), std::numeric_limits<double>::quiet_NaN());
    if (forwardDays <= 0) {
        return;
    }

    for (size_t i = 0; i < series.closes.size(); ++i) {
        const size_t futureIndex = i + static_cast<size_t>(forwardDays);
        if (futureIndex >= series.closes.size()) {
            continue;
        }

        const double startClose = series.closes[i];
        const double endClose = series.closes[futureIndex];
        if (startClose > 0.0 && endClose > 0.0 && std::isfinite(startClose) && std::isfinite(endClose)) {
            series.futureReturns[i] = (endClose - startClose) / startClose;
        }
    }
}

std::vector<factor::CachedMarketBar> buildCachedBarsFromRows(const QVariantList& rows)
{
    std::vector<factor::CachedMarketBar> cachedBars;
    cachedBars.reserve(static_cast<size_t>(rows.size()));
    std::unordered_map<std::string, double> industryCodeBuckets;
    double nextIndustryCodeBucket = 1.0;
    std::unordered_map<std::string, size_t> rowIndexBySymbolDate;
    rowIndexBySymbolDate.reserve(static_cast<size_t>(rows.size()));

    for (const QVariant& rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        const QString symbol = row.value(factor::bridge::CommonFieldKeys::SYMBOL).toString().trimmed();
        QString effectiveDate = row.value(
            factor::bridge::CommonFieldKeys::TRADE_DATE,
            row.value(factor::bridge::LegacyCleaningFieldKeys::DATE)).toString().trimmed();
        if (effectiveDate.isEmpty()) {
            effectiveDate = row.value(QStringLiteral("disclosure_date")).toString().trimmed();
        }
        const double close = row.value(factor::bridge::MarketBarFieldKeys::CLOSE).toDouble();
        if (symbol.isEmpty() || effectiveDate.isEmpty()) {
            continue;
        }

        const std::string symbolKey = symbol.toStdString();
        const std::string dateKey = effectiveDate.toStdString();
        const std::string rowKey = symbolKey + "|" + dateKey;

        factor::CachedMarketBar* bar = nullptr;
        auto rowIndexIt = rowIndexBySymbolDate.find(rowKey);
        if (rowIndexIt == rowIndexBySymbolDate.end()) {
            factor::CachedMarketBar newBar;
            newBar.symbol = symbolKey;
            newBar.tradeDate = dateKey;
            newBar.close = std::numeric_limits<double>::quiet_NaN();
            cachedBars.push_back(std::move(newBar));
            rowIndexIt = rowIndexBySymbolDate.emplace(rowKey, cachedBars.size() - 1).first;
        }
        bar = &cachedBars[rowIndexIt->second];

        if (std::isfinite(close)) {
            bar->close = close;
            bar->numericFields[factor::bridge::MarketBarFieldKeys::CLOSE.c_str()] = close;
        }

        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString normalizedField = factor::bridge::runtimeContractFieldName(it.key());
            if (normalizedField.isEmpty()
                || normalizedField == factor::bridge::CommonFieldKeys::SYMBOL
                || normalizedField == factor::bridge::CommonFieldKeys::TRADE_DATE
                || normalizedField == factor::bridge::LegacyCleaningFieldKeys::DATE) {
                continue;
            }

            if (normalizedField == factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE) {
                bool ok = false;
                const double numericValue = it.value().toDouble(&ok);
                if (ok && std::isfinite(numericValue)) {
                    bar->numericFields[normalizedField.toStdString()] = numericValue;
                    continue;
                }

                const std::string industryText = it.value().toString().trimmed().toStdString();
                if (industryText.empty()) {
                    continue;
                }

                auto bucketIt = industryCodeBuckets.find(industryText);
                if (bucketIt == industryCodeBuckets.end()) {
                    bucketIt = industryCodeBuckets.emplace(industryText, nextIndustryCodeBucket).first;
                    nextIndustryCodeBucket += 1.0;
                }
                bar->numericFields[normalizedField.toStdString()] = bucketIt->second;
                continue;
            }

            bool ok = false;
            const double numericValue = it.value().toDouble(&ok);
            if (!ok || !std::isfinite(numericValue)) {
                continue;
            }

            bar->numericFields[normalizedField.toStdString()] = numericValue;
        }

        const QVariant adjFactorValue = row.contains(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)
            ? row.value(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)
            : row.value(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR, 1.0);
        const double adjFactor = adjFactorValue.toDouble();
        if (std::isfinite(adjFactor)) {
            bar->numericFields[factor::bridge::LegacyCleaningFieldKeys::ADJ_FACTOR.c_str()] = adjFactor;
        }
    }

    std::sort(cachedBars.begin(), cachedBars.end(), [](const factor::CachedMarketBar& left,
                                                       const factor::CachedMarketBar& right) {
        if (left.symbol != right.symbol) {
            return left.symbol < right.symbol;
        }
        return left.tradeDate < right.tradeDate;
    });

    return cachedBars;
}

bool ensureCachedHistoricalDataLoaded(factor::BacktestConfig& config,
                                      std::string* failureReason)
{
    if (hasPreparedHistoricalData(config)) {
        return true;
    }

    if (config.datasetId <= 0) {
        if (failureReason) {
            *failureReason = "未提供可用于回测的缓存行情数据，无法构建 HistoricalView";
        }
        return false;
    }

    qDebug() << "FactorBacktestExecutor: 工作线程装载缓存集"
             << "datasetId=" << config.datasetId
             << "threadId=" << threadIdText();

    auto& cache = DataServiceCache::getInstance();
    cache.initializeCache();
    const QVariantList rows = cache.getDataSetById(config.datasetId);
    config.cachedBars = buildCachedBarsFromRows(rows);
    if (config.cachedBars.empty()) {
        if (failureReason) {
            *failureReason = "缓存集没有可用于回测的有效行情数据";
        }
        return false;
    }

    return true;
}

bool isNeutralizationSampleInsufficientReason(const std::string& reason)
{
    const QString normalizedReason = QString::fromUtf8(reason.c_str()).trimmed();
    return normalizedReason.contains(QStringLiteral("中性化样本不足"))
        || normalizedReason.contains(QStringLiteral("中性化后没有有效样本"))
        || normalizedReason.contains(QStringLiteral("行业和市值残差化"));
}

std::string extractCalculationEmptyReason(const CalculationResult& calculation)
{
    if (calculation.metadata.has("empty_reason")) {
        return calculation.metadata.get("empty_reason").asString();
    }
    if (calculation.metadata.has("emptyReason")) {
        return calculation.metadata.get("emptyReason").asString();
    }
    if (calculation.metadata.has("error")) {
        return calculation.metadata.get("error").asString();
    }
    return calculation.dataStatus.message;
}

bool shouldTreatInvalidCalculationAsEmpty(const CalculationResult& calculation)
{
    return !calculation.dataStatus.isValid()
        && isNeutralizationSampleInsufficientReason(extractCalculationEmptyReason(calculation));
}

struct CachedReturnAggregationState {
    std::vector<double> icSeries;
    std::vector<double> rawLongShortSeries;
    std::vector<double> longShortSeries;
    std::vector<double> turnoverSeries;
    std::vector<std::string> longShortDates;
    std::vector<double> aggregatedReturns;
    std::vector<int> aggregatedCounts;
    std::vector<int> aggregatedStockCounts;
    std::vector<double> aggregatedMinFactorValues;
    std::vector<double> aggregatedMaxFactorValues;
    std::vector<std::vector<double>> groupReturnSeriesByGroup;
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
    state.rawLongShortSeries.clear();
    state.longShortSeries.clear();
    state.turnoverSeries.clear();
    state.longShortDates.clear();
    state.aggregatedReturns.assign(static_cast<size_t>(groupCount), 0.0);
    state.aggregatedCounts.assign(static_cast<size_t>(groupCount), 0);
    state.aggregatedStockCounts.assign(static_cast<size_t>(groupCount), 0);
    state.aggregatedMinFactorValues.assign(static_cast<size_t>(groupCount), std::numeric_limits<double>::max());
    state.aggregatedMaxFactorValues.assign(static_cast<size_t>(groupCount), std::numeric_limits<double>::lowest());
    state.groupReturnSeriesByGroup.assign(static_cast<size_t>(groupCount), std::vector<double>());
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

bool aggregateSingleResultWithCachedFutureReturns(
    const CalculationResult& factorResult,
    const BacktestConfig& config,
    CachedReturnAggregationState& state,
    const FactorBacktestExecutor::CachedMarketIndex* cachedMarketIndex,
    std::string* failureReason)
{
    const int groupCount = (std::max)(1, config.numGroups);
    const int rebalanceInterval = (std::max)(1, config.rebalanceDays);
    state.hasAnyFactorResult = true;
    if (!cachedMarketIndex) {
        if (failureReason) {
            failureReason->clear();
        }
        return true;
    }

    const int tradeDateIndex = findTradeDateIndex(cachedMarketIndex->tradeDates, factorResult.date);
    if (tradeDateIndex < 0) {
        if (failureReason) {
            failureReason->clear();
        }
        return true;
    }

    std::vector<double> factorValues;
    std::vector<double> returnValues;
    std::vector<std::pair<std::string, double>> rankedValues;
    factorValues.reserve(factorResult.values.size());
    returnValues.reserve(factorResult.values.size());
    rankedValues.reserve(factorResult.values.size());

    const auto resolveFutureReturn = [&](const std::string& symbol) {
        const auto symbolIt = cachedMarketIndex->closeSeriesBySymbol.find(symbol);
        if (symbolIt == cachedMarketIndex->closeSeriesBySymbol.end()) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const auto& series = symbolIt->second;
        const auto startIndexIt = std::lower_bound(series.tradeDateIndices.begin(),
                                                   series.tradeDateIndices.end(),
                                                   tradeDateIndex);
        if (startIndexIt == series.tradeDateIndices.end() || *startIndexIt != tradeDateIndex) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const size_t startIndex = static_cast<size_t>(std::distance(series.tradeDateIndices.begin(), startIndexIt));
        return startIndex < series.futureReturns.size()
            ? series.futureReturns[startIndex]
            : std::numeric_limits<double>::quiet_NaN();
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
        state.groupReturnSeriesByGroup[static_cast<size_t>(groupIndex)].push_back(averageGroupReturn);
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
            state.rawLongShortSeries.push_back(topGroupReturnForDate - bottomGroupReturnForDate);
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

bool populateBacktestResultFromAggregationState(const factor::BacktestConfig& config,
                                                CachedReturnAggregationState& aggregationState,
                                                factor::BacktestResult& result)
{
    result.icirResult.icSeries = std::move(aggregationState.icSeries);
    result.icirResult.icMean = factor::icir::calculateMean(result.icirResult.icSeries);
    result.icirResult.icStd = factor::icir::calculateStdDev(result.icirResult.icSeries, result.icirResult.icMean);
    result.icirResult.ir = result.icirResult.icStd > 0.0 ? result.icirResult.icMean / result.icirResult.icStd : 0.0;
    if (!result.icirResult.icSeries.empty()) {
        const auto positiveCount = std::count_if(result.icirResult.icSeries.begin(), result.icirResult.icSeries.end(), [](double value) {
            return value > 0.0;
        });
        result.icirResult.icPositiveRatio = static_cast<double>(positiveCount) / static_cast<double>(result.icirResult.icSeries.size());
    }

    const int groupCount = aggregationState.maxEffectiveGroupCount;
    result.groupResult.groupReturns.resize(static_cast<size_t>(groupCount), 0.0);
    result.groupResult.groupStockCounts.resize(static_cast<size_t>(groupCount), 0);
    result.groupResult.minFactorValues.resize(static_cast<size_t>(groupCount), 0.0);
    result.groupResult.maxFactorValues.resize(static_cast<size_t>(groupCount), 0.0);

    bool hasValidGroup = false;
    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        if (aggregationState.aggregatedCounts[static_cast<size_t>(groupIndex)] <= 0) {
            continue;
        }

        hasValidGroup = true;
        result.groupResult.groupReturns[static_cast<size_t>(groupIndex)] =
            aggregationState.aggregatedReturns[static_cast<size_t>(groupIndex)]
            / static_cast<double>(aggregationState.aggregatedCounts[static_cast<size_t>(groupIndex)]);
        result.groupResult.groupStockCounts[static_cast<size_t>(groupIndex)] =
            aggregationState.aggregatedStockCounts[static_cast<size_t>(groupIndex)]
            / aggregationState.aggregatedCounts[static_cast<size_t>(groupIndex)];
        result.groupResult.minFactorValues[static_cast<size_t>(groupIndex)] =
            aggregationState.aggregatedMinFactorValues[static_cast<size_t>(groupIndex)];
        result.groupResult.maxFactorValues[static_cast<size_t>(groupIndex)] =
            aggregationState.aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)];
    }

    const bool hasUsableGroupResult = hasValidGroup && result.groupResult.groupReturns.size() >= 2;
    if (hasUsableGroupResult) {
        result.groupResult.topGroupReturn = result.groupResult.groupReturns.front();
        result.groupResult.bottomGroupReturn = result.groupResult.groupReturns.back();
        result.groupResult.longShortReturn = result.groupResult.topGroupReturn
            - result.groupResult.bottomGroupReturn;
    }

    return hasUsableGroupResult;
}

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

std::string buildAllowedStockCodesByDateFingerprint(
    std::unordered_map<std::string, std::vector<std::string>> allowedStockCodesByDate)
{
    if (allowedStockCodesByDate.empty()) {
        return "*";
    }

    std::vector<std::string> tradeDates;
    tradeDates.reserve(allowedStockCodesByDate.size());
    for (const auto& [tradeDate, stockCodes] : allowedStockCodesByDate) {
        (void)stockCodes;
        tradeDates.push_back(tradeDate);
    }
    std::sort(tradeDates.begin(), tradeDates.end());

    std::ostringstream stream;
    for (const std::string& tradeDate : tradeDates) {
        auto stockCodes = allowedStockCodesByDate[tradeDate];
        std::sort(stockCodes.begin(), stockCodes.end());
        stream << tradeDate << ':';
        for (const auto& stockCode : stockCodes) {
            stream << stockCode << ',';
        }
        stream << ';';
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

class DirectCachedHistoricalView final : public HistoricalView {
public:
    explicit DirectCachedHistoricalView(const std::vector<factor::CachedMarketBar>& rows)
    {
        fieldNames_.insert("close");
        for (const auto& rawBar : rows) {
            const std::string normalizedTradeDate = factor::cached_bars::normalizeTradeDate(rawBar.tradeDate);
            if (normalizedTradeDate.empty() || rawBar.symbol.empty()) {
                continue;
            }

            CachedFieldRow bar;
            bar.close = rawBar.close;
            bar.numericFields = rawBar.numericFields;

            rowByDateSymbol_[normalizedTradeDate][rawBar.symbol] = bar;
            symbolRows_[rawBar.symbol].push_back(SymbolRow{normalizedTradeDate, std::move(bar)});
            dateToSymbols_[normalizedTradeDate].push_back(rawBar.symbol);

            for (const auto& [fieldName, value] : rawBar.numericFields) {
                if (std::isfinite(value) && !fieldName.empty()) {
                    fieldNames_.insert(fieldName);
                }
            }
        }

        for (auto& [symbol, rowsBySymbol] : symbolRows_) {
            std::sort(rowsBySymbol.begin(), rowsBySymbol.end(), [](const SymbolRow& left, const SymbolRow& right) {
                return left.date < right.date;
            });
            (void)symbol;
        }

        for (auto& [date, symbols] : dateToSymbols_) {
            std::sort(symbols.begin(), symbols.end());
            symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
            (void)date;
        }
    }

    bool hasField(const std::string& field) const override
    {
        return fieldNames_.find(field) != fieldNames_.end();
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override
    {
        const auto dateIt = rowByDateSymbol_.find(date);
        if (dateIt == rowByDateSymbol_.end()) {
            return std::nullopt;
        }

        const auto symbolIt = dateIt->second.find(symbol);
        if (symbolIt == dateIt->second.end()) {
            return std::nullopt;
        }

        if (field == "close") {
            return std::isfinite(symbolIt->second.close) ? std::optional<double>(symbolIt->second.close) : std::nullopt;
        }

        const auto fieldIt = symbolIt->second.numericFields.find(field);
        if (fieldIt == symbolIt->second.numericFields.end() || !std::isfinite(fieldIt->second)) {
            return std::nullopt;
        }
        return fieldIt->second;
    }

    std::vector<factor::HistoricalDataPoint> getSeries(const std::string& symbol,
                                                       const std::string& startDate,
                                                       const std::string& endDate,
                                                       const std::string& field) const override
    {
        std::vector<factor::HistoricalDataPoint> series;
        const auto symbolIt = symbolRows_.find(symbol);
        if (symbolIt == symbolRows_.end()) {
            return series;
        }

        for (const auto& row : symbolIt->second) {
            if (row.date < startDate || row.date > endDate) {
                continue;
            }

            const std::optional<double> value = valueForField(row.row, field);
            if (!value.has_value()) {
                continue;
            }
            series.push_back({row.date, *value});
        }
        return series;
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override
    {
        const auto it = dateToSymbols_.find(date);
        return it == dateToSymbols_.end() ? std::vector<std::string>{} : it->second;
    }

    std::unordered_map<std::string, double> getCrossSection(const std::string& date,
                                                            const std::string& field,
                                                            const std::vector<std::string>& symbols = {}) const override
    {
        std::unordered_map<std::string, double> values;
        const auto dateIt = rowByDateSymbol_.find(date);
        if (dateIt == rowByDateSymbol_.end()) {
            return values;
        }

        if (symbols.empty()) {
            for (const auto& [symbol, row] : dateIt->second) {
                const std::optional<double> value = valueForField(row, field);
                if (value.has_value()) {
                    values.emplace(symbol, *value);
                }
            }
            return values;
        }

        for (const auto& symbol : symbols) {
            const auto symbolIt = dateIt->second.find(symbol);
            if (symbolIt == dateIt->second.end()) {
                continue;
            }

            const std::optional<double> value = valueForField(symbolIt->second, field);
            if (value.has_value()) {
                values.emplace(symbol, *value);
            }
        }
        return values;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, double>> batchValues;
        for (const auto& field : fields) {
            batchValues[field] = getCrossSection(date, field, symbols);
        }
        return batchValues;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        const std::vector<std::string> effectiveSymbols = symbols.empty() ? allSymbols() : deduplicateSymbolsPreservingOrder(symbols);
        for (const auto& field : fields) {
            auto& fieldSeries = batchSeries[field];
            for (const auto& symbol : effectiveSymbols) {
                const auto series = getSeries(symbol, startDate, endDate, field);
                auto& values = fieldSeries[symbol];
                values.reserve(series.size());
                for (const auto& point : series) {
                    values.push_back(point.value);
                }
            }
        }
        return batchSeries;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& anchorDate,
        int window,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        if (window <= 0) {
            return batchSeries;
        }

        const std::vector<std::string> effectiveSymbols = symbols.empty() ? allSymbols() : deduplicateSymbolsPreservingOrder(symbols);
        for (const auto& field : fields) {
            auto& fieldSeries = batchSeries[field];
            for (const auto& symbol : effectiveSymbols) {
                const auto symbolIt = symbolRows_.find(symbol);
                if (symbolIt == symbolRows_.end()) {
                    continue;
                }

                std::vector<double> values;
                values.reserve(static_cast<size_t>(window));
                for (auto rowIt = symbolIt->second.rbegin(); rowIt != symbolIt->second.rend() && static_cast<int>(values.size()) < window; ++rowIt) {
                    if (rowIt->date > anchorDate) {
                        continue;
                    }
                    const std::optional<double> value = valueForField(rowIt->row, field);
                    if (value.has_value()) {
                        values.push_back(*value);
                    }
                }
                fieldSeries.emplace(symbol, std::move(values));
            }
        }
        return batchSeries;
    }

private:
    struct CachedFieldRow {
        double close{std::numeric_limits<double>::quiet_NaN()};
        std::unordered_map<std::string, double> numericFields;
    };

    struct SymbolRow {
        std::string date;
        CachedFieldRow row;
    };

    static std::optional<double> valueForField(const CachedFieldRow& row, const std::string& field)
    {
        if (field == "close") {
            return std::isfinite(row.close) ? std::optional<double>(row.close) : std::nullopt;
        }

        const auto it = row.numericFields.find(field);
        if (it == row.numericFields.end() || !std::isfinite(it->second)) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<std::string> allSymbols() const
    {
        std::vector<std::string> symbols;
        symbols.reserve(symbolRows_.size());
        for (const auto& [symbol, rows] : symbolRows_) {
            Q_UNUSED(rows);
            symbols.push_back(symbol);
        }
        std::sort(symbols.begin(), symbols.end());
        return symbols;
    }

    std::unordered_set<std::string> fieldNames_;
    std::unordered_map<std::string, std::unordered_map<std::string, CachedFieldRow>> rowByDateSymbol_;
    std::unordered_map<std::string, std::vector<SymbolRow>> symbolRows_;
    std::unordered_map<std::string, std::vector<std::string>> dateToSymbols_;
};

std::string buildBacktestCacheSignature(const BacktestConfig& config)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6)
           << "fq1_"
           << "ds" << config.datasetId
           << "_bm" << config.benchmarkSymbol
           << "_tc" << config.transactionCost
           << "_slp" << config.slippageRate
           << "_rf" << config.riskFreeRate
           << "_rb" << config.rebalanceDays
           << "_rc" << (config.enableRiskControls ? 1 : 0)
           << "_sl" << config.stopLossRate
           << "_tp" << config.takeProfitRate
           << "_dd" << config.maxDrawdownLimit
           << "_dl" << config.maxDailyLoss
           << "_mp" << config.maxPositionPercent
           << "_te" << config.maxTotalExposure
           << "_stocks" << buildAllowedStockCodesFingerprint(config.allowedStockCodes)
           << "_stocksByDate" << buildAllowedStockCodesByDateFingerprint(config.allowedStockCodesByDate)
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

    if (!config.enableRiskControls) {
        result.adjustedReturns = periodicReturns;
        result.summary = "未启用风控";
        return result;
    }

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

    updateProgress(progress,
                   1,
                   "初始化回测任务：准备执行上下文");

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

FactorBacktestExecutor::BatchExecutionHandle FactorBacktestExecutor::executeBatchTrackedAsync(
    const std::vector<BacktestConfig>& configs)
{
    ProgressInfo progress = createProgressInfo(configs.empty() ? std::string("batch") : configs.front().instanceId);
    progress.currentStep = "初始化批量回测任务";
    registerTask(progress);

    if (configs.empty()) {
        std::promise<std::vector<BacktestResult>> promise;
        promise.set_value({});
        auto future = promise.get_future();
        finalizeTask(progress.taskId);
        return BatchExecutionHandle{progress.taskId, std::move(future)};
    }

    auto future = std::async(std::launch::async, [this, configs, progress]() mutable {
        if (configs.size() == 1) {
            updateProgress(progress, 0, "批量回测 0/1 - 正在执行单配置回测");
            std::vector<BacktestResult> singleResult;
            singleResult.reserve(1);
            singleResult.push_back(executeTracked(configs.front(), progress));
            updateProgress(progress,
                           100,
                           singleResult.front().status == "SUCCESS" ? "回测完成" : "执行失败");
            finalizeTask(progress.taskId);
            return singleResult;
        }

        struct InflightBatchHandle {
            size_t index{0};
            ExecutionHandle handle;
        };

        const size_t totalConfigs = configs.size();
        std::vector<BacktestResult> results(totalConfigs);
        const size_t workerCount = threadPool_ ? (std::max)(size_t{1}, threadPool_->getWorkerCount()) : size_t{1};
        const bool hasHeavyPayload = std::any_of(configs.begin(), configs.end(), [](const BacktestConfig& config) {
            return config.enableDateParallelism || hasPreparedHistoricalData(config);
        });
        const size_t maxInflight = hasHeavyPayload
            ? size_t{1}
            : (std::min)(configs.size(), (std::max)(size_t{1}, workerCount / 2));

        std::deque<InflightBatchHandle> inflight;
        size_t nextConfigIndex = 0;
        size_t completedCount = 0;

        while (nextConfigIndex < totalConfigs || !inflight.empty()) {
            if (isCancelled(progress.taskId)) {
                for (const InflightBatchHandle& inflightHandle : inflight) {
                    cancel(inflightHandle.handle.taskId);
                }
                break;
            }

            while (nextConfigIndex < totalConfigs && inflight.size() < maxInflight) {
                inflight.push_back(InflightBatchHandle{nextConfigIndex, executeTrackedAsync(configs[nextConfigIndex])});
                ++nextConfigIndex;
            }

            bool completedAny = false;
            double inflightProgress = 0.0;
            std::string currentStep = "正在批量回测";

            for (auto it = inflight.begin(); it != inflight.end();) {
                if (it->handle.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    results[it->index] = it->handle.future.get();
                    ++completedCount;
                    it = inflight.erase(it);
                    completedAny = true;
                    continue;
                }

                const ProgressInfo childProgress = getProgress(it->handle.taskId);
                if (childProgress.status != "NOT_FOUND") {
                    inflightProgress += static_cast<double>((std::max)(0, (std::min)(100, childProgress.progress))) / 100.0;
                    if (currentStep == "正在批量回测" && !childProgress.currentStep.empty()) {
                        currentStep = childProgress.currentStep;
                    }
                }
                ++it;
            }

            const double aggregateProgress = (static_cast<double>(completedCount) + inflightProgress)
                / static_cast<double>((std::max)(size_t{1}, totalConfigs));
            const int progressPercent = (std::max)(0,
                                                   (std::min)(99,
                                                              static_cast<int>(std::floor(aggregateProgress * 100.0))));
            std::ostringstream batchStep;
            batchStep << "批量回测 " << completedCount << '/' << totalConfigs;
            if (!currentStep.empty()) {
                batchStep << " - " << currentStep;
            }
            updateProgress(progress, progressPercent, batchStep.str());

            if (!completedAny && !inflight.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        if (isCancelled(progress.taskId)) {
            updateProgress(progress, 100, "已取消");
        } else {
            updateProgress(progress, 100, "回测完成");
        }

        finalizeTask(progress.taskId);
        return results;
    });

    return BatchExecutionHandle{progress.taskId, std::move(future)};
}

std::vector<BacktestResult> FactorBacktestExecutor::executeBatch(const std::vector<BacktestConfig>& configs)
{
    return executeBatchTrackedAsync(configs).future.get();
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
    result.status = "FAILED";
    BacktestConfig effectiveConfig = config;

    try {
        if (!instanceManager_) {
            return BacktestResult::createError(config.instanceId, "因子实例管理器未初始化");
        }

        std::string loadFailureReason;
        if (!ensureCachedHistoricalDataLoaded(effectiveConfig, &loadFailureReason)) {
            result.errorMessage = loadFailureReason.empty() ? "缓存集装载失败" : loadFailureReason;
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }
        result.config = effectiveConfig;

        auto info = instanceManager_->getInstanceInfo(effectiveConfig.instanceId);
        if (info.instanceId.empty()) {
            return BacktestResult::createError(effectiveConfig.instanceId, "因子实例不存在");
        }

        result.instanceName = info.instanceName;
        result.dataStatus = info.dataStatus;
        result.dataCoverage = info.dataStatus.coverage;

        ExecutionMarketContext marketContext;
        CachedMarketIndex cachedMarketIndex;
        std::string prepareFailureReason;
        if (!prepareExecutionMarketContext(effectiveConfig, marketContext, &cachedMarketIndex, &prepareFailureReason, &progress)) {
            result.errorMessage = prepareFailureReason.empty() ? "准备市场上下文失败" : prepareFailureReason;
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        std::shared_ptr<BaseFactor> factor;
        if (!prepareData(effectiveConfig, progress, factor, &prepareFailureReason)) {
            result.errorMessage = prepareFailureReason.empty() ? "准备回测数据失败" : prepareFailureReason;
            if (isCancelled(progress.taskId)) {
                result.status = "CANCELLED";
                result.errorMessage = "任务已取消";
            }
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        const size_t tradeDateCountBeforeWarmup = marketContext.tradeDates.size();
        if (!applyFactorWarmupToMarketContext(*factor, marketContext, &prepareFailureReason)) {
            result.errorMessage = prepareFailureReason.empty() ? "因子 warmup 裁剪失败" : prepareFailureReason;
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        result.warmupTrimmedTradingDays = static_cast<int>(tradeDateCountBeforeWarmup > marketContext.tradeDates.size()
            ? tradeDateCountBeforeWarmup - marketContext.tradeDates.size()
            : 0);
        if (!marketContext.tradeDates.empty()) {
            result.actualStartDate = marketContext.tradeDates.front();
        }

        const size_t tradeDateCount = marketContext.tradeDates.size();
        const size_t totalWorkUnits = (std::max)(size_t{9}, tradeDateCount * 2 + size_t{9});
        size_t completedWorkUnits = 0;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "准备回测上下文：构建交易日与股票池索引");
        ++completedWorkUnits;

        ++completedWorkUnits;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "加载因子实例：解析配置并构建执行对象");

        const auto benchmarkLookup = [this, &effectiveConfig, &cachedMarketIndex](const std::string& date) {
            return calculateFutureReturn(effectiveConfig.benchmarkSymbol,
                                         date,
                                         effectiveConfig.forwardDays,
                                         effectiveConfig,
                                         &cachedMarketIndex);
        };

        if (!marketContext.historicalView) {
            result.errorMessage = "缓存回测未能构建历史行情上下文";
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        CachedReturnAggregationState aggregationState;
        initializeCachedReturnAggregationState(effectiveConfig, aggregationState);
        std::vector<CalculationResult> unusedFactorResults;
        std::string factorFailureReason;
        size_t streamedFactorWorkUnits = 0;
        std::function<bool(CalculationResult&&)> consumeCachedFactorResult = [&effectiveConfig,
                                                                               &aggregationState,
                                                                               &cachedMarketIndex,
                                                                               &factorFailureReason,
                                                                               &progress,
                                                                               this](CalculationResult&& factorResult) {
            if (isCancelled(progress.taskId)) {
                factorFailureReason = "任务已取消";
                return false;
            }
            if (!aggregateSingleResultWithCachedFutureReturns(factorResult,
                                                              effectiveConfig,
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
        if (!calculateFactorSeries(effectiveConfig,
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
        if (!populateBacktestResultFromAggregationState(effectiveConfig, aggregationState, result)) {
            result.status = "PARTIAL";
        }

        if (aggregationState.overlapDateCount == 0) {
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
        auto& longShortSeries = aggregationState.longShortSeries;
        const auto rawLongShortSeries = aggregationState.rawLongShortSeries;
        const auto costAdjustedLongShortSeries = longShortSeries;
        const auto& turnoverSeries = aggregationState.turnoverSeries;
        const auto& longShortDates = aggregationState.longShortDates;
        const auto riskResult = applyRiskControls(longShortSeries, config);
        longShortSeries = riskResult.adjustedReturns;

        result.longShortDates = longShortDates;
        result.rawLongShortSeries = rawLongShortSeries;
        result.costAdjustedLongShortSeries = costAdjustedLongShortSeries;
        result.riskAdjustedLongShortSeries = longShortSeries;

        ++completedWorkUnits;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "汇总结果：计算年化收益、夏普、回撤、胜率和换手率");

        const auto benchmarkSummary = FactorBacktestMetricsCalculator::calculateBenchmarkComparison(longShortSeries,
                                                                                                    longShortDates,
                                                                                                    config.forwardDays,
                                                                                                    config.riskFreeRate,
                                                                                                    benchmarkLookup);

        ++completedWorkUnits;
        updateProgress(progress,
                       progressPercentFromWork(completedWorkUnits, totalWorkUnits),
                       "生成最终结果：整理摘要并准备返回");

        FactorBacktestMetricsCalculator::populateResultMetrics(
            result,
            FactorBacktestMetricsCalculator::Inputs{config,
                                                    result.icirResult,
                                                    result.groupResult,
                                                    rawLongShortSeries,
                                                    costAdjustedLongShortSeries,
                                                    longShortSeries,
                                                    turnoverSeries,
                                                    longShortDates,
                                                    aggregationState.groupReturnSeriesByGroup,
                                                    result.alpha,
                                                    &benchmarkSummary,
                                                    riskResult.triggeredCount});
        result.riskControlSummary = riskResult.summary;
        result.dataStatus.availability = aggregationState.hasAnyFactorResult ? DataAvailability::AVAILABLE : DataAvailability::UNAVAILABLE;
        result.dataStatus.coverage = aggregationState.hasAnyFactorResult ? 1.0 : 0.0;
        result.dataStatus.message = aggregationState.hasAnyFactorResult ? "回测执行完成" : "未生成有效因子序列";
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

    factor = instanceManager_->createIsolatedInstance(config.instanceId);
    if (!factor && failureReason) {
        *failureReason = "未能创建因子实例，请检查实例是否已激活且定义与实例表保持同步";
    }
    return static_cast<bool>(factor);
}

bool FactorBacktestExecutor::applyFactorWarmupToMarketContext(const BaseFactor& factor,
                                                              ExecutionMarketContext& marketContext,
                                                              std::string* failureReason) const
{
    const BoundaryRules boundaryRules = factor.getBoundaryRules();
    const size_t leadingWarmupTradeDates = boundaryRules.minDataPoints > 1
        ? static_cast<size_t>(boundaryRules.minDataPoints - 1)
        : size_t{0};
    if (leadingWarmupTradeDates == 0) {
        return true;
    }

    if (marketContext.tradeDates.size() <= leadingWarmupTradeDates) {
        if (failureReason) {
            *failureReason = "所选日期范围不足以满足因子最小历史深度要求";
        }
        return false;
    }

    marketContext.tradeDates.erase(marketContext.tradeDates.begin(),
                                   marketContext.tradeDates.begin() + static_cast<std::ptrdiff_t>(leadingWarmupTradeDates));

    qDebug() << "FactorBacktestExecutor: 应用因子 warmup 裁剪"
             << "instanceName=" << QString::fromStdString(factor.getName())
             << "minDataPoints=" << boundaryRules.minDataPoints
             << "trimmedTradeDateCount=" << static_cast<int>(leadingWarmupTradeDates)
             << "remainingTradeDateCount=" << static_cast<int>(marketContext.tradeDates.size());
    return true;
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
    const auto makeHistoricalView = [&]() -> std::shared_ptr<HistoricalView> {
        if (marketContext.historicalView) {
            return marketContext.historicalView;
        }
        if (!hasPreparedHistoricalData(config)) {
            return {};
        }
        return std::make_shared<CachedRowHistoricalView>(marketContext.arrowData);
    };
    std::shared_ptr<HistoricalView> historicalView = makeHistoricalView();

    qDebug() << "FactorBacktestExecutor: 计算因子序列"
             << "instanceId=" << QString::fromStdString(config.instanceId)
             << "tradeDateCount=" << static_cast<int>(tradeDates.size())
                  << "cachedBarCount=" << static_cast<qulonglong>(config.cachedBars.empty() && marketContext.arrowData
                      ? marketContext.arrowData->rowCount()
                      : config.cachedBars.size())
             << "explicitAllowedSymbolCount=" << static_cast<int>(config.allowedStockCodes.size())
             << "usingHistoricalView=" << static_cast<bool>(historicalView);

    const size_t executorWorkers = threadPool_ ? (std::max)(size_t{1}, threadPool_->getWorkerCount()) : size_t{0};
    qDebug() << "FactorBacktestExecutor: 日期并行门控"
             << "instanceId=" << QString::fromStdString(config.instanceId)
             << "enableDateParallelism=" << config.enableDateParallelism
             << "hasThreadPool=" << static_cast<bool>(threadPool_)
             << "workerCount=" << static_cast<int>(executorWorkers)
             << "tradeDateCount=" << static_cast<int>(tradeDates.size())
             << "threadId=" << threadIdText();

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
                              const std::shared_ptr<HistoricalView>& activeHistoricalView,
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
                context.symbols = resolveSymbolsForTradeDate(marketContext, tradeDates[i]);
                context.historicalView = activeHistoricalView;
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
                         << "usingHistoricalView=" << static_cast<bool>(activeHistoricalView);
            }

            chunk.results.reserve(calculations.size());
            for (size_t calculationIndex = 0; calculationIndex < calculations.size(); ++calculationIndex) {
                auto& calculation = calculations[calculationIndex];
                if (!calculation.dataStatus.isValid()) {
                    if (shouldTreatInvalidCalculationAsEmpty(calculation)) {
                        chunk.lastEmptyReason = extractCalculationEmptyReason(calculation);
                        ++chunk.emptyCalculationCount;

                        const size_t processedDateTotal = processedDates.fetch_add(1) + 1;
                        publishProgress(processedDateTotal);
                        continue;
                    }
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
                    chunk.lastEmptyReason = extractCalculationEmptyReason(calculation);
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

    auto consumeChunk = [&](ChunkFactorCalculation&& chunk) -> bool {
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
        return true;
    };

    const bool useDateParallelism = config.enableDateParallelism
        && resultConsumer == nullptr
        && threadPool_
        && tradeDates.size() > 1;
    if (useDateParallelism) {
        const size_t parallelWorkerLimit = hasPreparedHistoricalData(config)
            ? (std::min)(executorWorkers, size_t{4})
            : executorWorkers;
        const size_t chunkCount = (std::min)(tradeDates.size(), parallelWorkerLimit);

        if (chunkCount > 1) {
            std::vector<std::pair<size_t, size_t>> ranges;
            const size_t defaultChunkSize = (tradeDates.size() + chunkCount - 1) / chunkCount;
            const size_t chunkSize = resultConsumer != nullptr
                ? streamingCalculationChunkSize(config, defaultChunkSize)
                : defaultChunkSize;
            ranges.reserve((tradeDates.size() + chunkSize - 1) / chunkSize);
            for (size_t beginIndex = 0; beginIndex < tradeDates.size(); beginIndex += chunkSize) {
                ranges.emplace_back(beginIndex, std::min(tradeDates.size(), beginIndex + chunkSize));
            }

            const auto firstRange = ranges.front();
            qDebug() << "FactorBacktestExecutor: 日期并行分片启动"
                     << "instanceId=" << QString::fromStdString(config.instanceId)
                     << "chunkCount=" << static_cast<int>(ranges.size())
                     << "workerCount=" << static_cast<int>(parallelWorkerLimit)
                     << "threadId=" << threadIdText();
            auto firstChunkFactor = instanceManager_ ? instanceManager_->createIsolatedInstance(config.instanceId) : nullptr;
            if (!firstChunkFactor) {
                if (failureReason) {
                    *failureReason = "未能创建日期并行分片的独立因子实例";
                }
                return false;
            }
            ChunkFactorCalculation firstChunk = calculateRange(*firstChunkFactor,
                                                              makeHistoricalView(),
                                                              firstRange.first,
                                                              firstRange.second);
            if (!consumeChunk(std::move(firstChunk))) {
                return false;
            }

            auto submitRange = [this,
                                &config,
                                makeHistoricalView,
                                calculateRange,
                                &progress](size_t beginIndex, size_t endIndex) {
                return threadPool_->submit(
                    [this,
                     config,
                     makeHistoricalView,
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
                            auto chunkFactor = instanceManager_ ? instanceManager_->createIsolatedInstance(config.instanceId) : nullptr;
                            if (!chunkFactor) {
                                chunk.success = false;
                                chunk.failureReason = "未能创建因子实例，请检查实例是否已激活且定义与实例表保持同步";
                            } else {
                                chunk = calculateRange(*chunkFactor,
                                                       makeHistoricalView(),
                                                       beginIndex,
                                                       endIndex);
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
                                 << "success=" << chunk.success
                                 << "failureReason=" << QString::fromStdString(chunk.failureReason);
                        return chunk;
                    });
            };

            const size_t maxInflightFutures = resultConsumer != nullptr
                ? (std::max)(size_t{1}, parallelWorkerLimit)
                : (ranges.size() > 1 ? ranges.size() - 1 : 0);
            std::deque<std::future<ChunkFactorCalculation>> futures;
            size_t nextRangeIndex = 1;
            auto drainOutstandingFutures = [&futures]() {
                while (!futures.empty()) {
                    try {
                        futures.front().wait();
                        (void)futures.front().get();
                    } catch (...) {
                    }
                    futures.pop_front();
                }
            };

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
                    qWarning() << "FactorBacktestExecutor: 日期分片失败"
                               << "instanceId=" << QString::fromStdString(config.instanceId)
                               << "failureReason=" << QString::fromStdString(chunk.failureReason);
                    if (failureReason) {
                        *failureReason = chunk.failureReason.empty() ? "因子序列计算失败" : chunk.failureReason;
                    }
                    drainOutstandingFutures();
                    return false;
                }

                if (!consumeChunk(std::move(chunk))) {
                    drainOutstandingFutures();
                    return false;
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

    qDebug() << "FactorBacktestExecutor: 日期并行未启用，退回顺序执行"
             << "instanceId=" << QString::fromStdString(config.instanceId)
             << "enableDateParallelism=" << config.enableDateParallelism
             << "hasThreadPool=" << static_cast<bool>(threadPool_)
             << "workerCount=" << static_cast<int>(executorWorkers)
             << "tradeDateCount=" << static_cast<int>(tradeDates.size())
             << "threadId=" << threadIdText();

    const size_t sequentialChunkSize = resultConsumer != nullptr
        ? streamingCalculationChunkSize(config, tradeDates.size())
        : tradeDates.size();
    for (size_t beginIndex = 0; beginIndex < tradeDates.size(); beginIndex += sequentialChunkSize) {
        const size_t endIndex = std::min(tradeDates.size(), beginIndex + sequentialChunkSize);
        ChunkFactorCalculation chunk = calculateRange(*factor, historicalView, beginIndex, endIndex);
        if (!consumeChunk(std::move(chunk))) {
            return false;
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

double FactorBacktestExecutor::calculateFutureReturn(const std::string& symbol,
                                                     const std::string& startDate,
                                                     int forwardDays,
                                                     const BacktestConfig& config,
                                                     const CachedMarketIndex* cachedMarketIndex)
{
    double calculatedFutureReturn = std::numeric_limits<double>::quiet_NaN();
    if (hasPreparedHistoricalData(config)) {
        if (!cachedMarketIndex) {
            throw std::logic_error("FactorBacktestExecutor: cached market index is required for cached-bar future return calculation");
        }

        const int tradeDateIndex = findTradeDateIndex(cachedMarketIndex->tradeDates, startDate);
        if (tradeDateIndex < 0) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const auto symbolIt = cachedMarketIndex->closeSeriesBySymbol.find(symbol);
        if (symbolIt != cachedMarketIndex->closeSeriesBySymbol.end()) {
            const auto& series = symbolIt->second;
            const auto startIndexIt = std::lower_bound(series.tradeDateIndices.begin(),
                                                       series.tradeDateIndices.end(),
                                                       tradeDateIndex);
            if (startIndexIt != series.tradeDateIndices.end() && *startIndexIt == tradeDateIndex) {
                const size_t startIndex = static_cast<size_t>(std::distance(series.tradeDateIndices.begin(), startIndexIt));
                if (forwardDays > 0 && startIndex < series.futureReturns.size()) {
                    calculatedFutureReturn = series.futureReturns[startIndex];
                }
            }
        }
    } else {
        Q_UNUSED(symbol);
        Q_UNUSED(startDate);
        Q_UNUSED(forwardDays);
        return std::numeric_limits<double>::quiet_NaN();
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
        *failureReason = "已移除非 HistoricalView 回测路径，请先由引擎构建 HistoricalView 缓存行情视图";
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

    if (cachedIndex.tradeDates.empty()) {
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

    marketContext.symbolsByDate.clear();
    if (config.preparedArrowData) {
        marketContext.arrowData = config.preparedArrowData;
        marketContext.historicalView = std::make_shared<CachedRowHistoricalView>(marketContext.arrowData);
    } else {
        marketContext.arrowData.reset();
        marketContext.historicalView = std::make_shared<DirectCachedHistoricalView>(config.cachedBars);
        for (const auto& rawBar : config.cachedBars) {
            const std::string normalizedTradeDate = factor::cached_bars::normalizeTradeDate(rawBar.tradeDate);
            if (normalizedTradeDate.empty() || rawBar.symbol.empty()) {
                continue;
            }
            marketContext.symbolsByDate[normalizedTradeDate].push_back(rawBar.symbol);
        }
        for (auto& [date, symbols] : marketContext.symbolsByDate) {
            std::sort(symbols.begin(), symbols.end());
            symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
            (void)date;
        }
    }
    if (!marketContext.historicalView) {
        if (failureReason) {
            *failureReason = "缓存行情历史视图构建失败";
        }
        return false;
    }

    marketContext.allowedSymbols = std::unordered_set<std::string>(
        config.allowedStockCodes.begin(),
        config.allowedStockCodes.end());
    marketContext.allowedSymbolsByDate.clear();
    for (const auto& [tradeDate, stockCodes] : config.allowedStockCodesByDate) {
        auto& allowedSymbols = marketContext.allowedSymbolsByDate[tradeDate];
        allowedSymbols.insert(stockCodes.begin(), stockCodes.end());
    }

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(2, 4),
                       "准备回测上下文：整理股票池与交易日交集");
    }

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(4, 4),
                       "准备回测上下文：缓存集索引可复用，准备进入因子计算");
    }

    int effectiveFirstDateSymbolCount = 0;
    if (!marketContext.tradeDates.empty()) {
        effectiveFirstDateSymbolCount = static_cast<int>(
            resolveSymbolsForTradeDate(marketContext, marketContext.tradeDates.front()).size());
    }

    qDebug() << "FactorBacktestExecutor: 复用缓存集索引"
             << "datasetId=" << config.datasetId
             << "tradeDateCount=" << static_cast<int>(marketContext.tradeDates.size())
             << "explicitAllowedStockCount=" << static_cast<int>(config.allowedStockCodes.size())
             << "cachedBarCount=" << static_cast<qulonglong>(config.cachedBars.empty() && marketContext.arrowData
                 ? marketContext.arrowData->rowCount()
                 : config.cachedBars.size())
             << "effectiveFirstDateSymbolCount=" << effectiveFirstDateSymbolCount;

    return true;
}

std::vector<std::string> FactorBacktestExecutor::resolveSymbolsForTradeDate(
    const ExecutionMarketContext& marketContext,
    const std::string& tradeDate) const
{
    std::vector<std::string> symbols;
    if (!marketContext.arrowData) {
        const auto it = marketContext.symbolsByDate.find(tradeDate);
        symbols = it == marketContext.symbolsByDate.end() ? std::vector<std::string>{} : it->second;
    } else {
        symbols = marketContext.arrowData->getAvailableSymbols(tradeDate);
    }

    const auto perDateIt = marketContext.allowedSymbolsByDate.find(tradeDate);
    if (perDateIt != marketContext.allowedSymbolsByDate.end()) {
        std::vector<std::string> filteredSymbols;
        filteredSymbols.reserve(symbols.size());
        for (const auto& symbol : symbols) {
            if (perDateIt->second.find(symbol) != perDateIt->second.end()) {
                filteredSymbols.push_back(symbol);
            }
        }
        return filteredSymbols;
    }

    if (marketContext.allowedSymbols.empty()) {
        return symbols;
    }

    std::vector<std::string> filteredSymbols;
    filteredSymbols.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        if (marketContext.allowedSymbols.find(symbol) != marketContext.allowedSymbols.end()) {
            filteredSymbols.push_back(symbol);
        }
    }
    return filteredSymbols;
}

FactorBacktestExecutor::CachedMarketIndex FactorBacktestExecutor::buildCachedMarketIndex(const std::vector<CachedMarketBar>& cachedBars,
                                                                                       int forwardDays) const
{
    CachedMarketIndex index;
    std::unordered_set<std::string> tradeDateSet;

    tradeDateSet.reserve(cachedBars.size());

    for (const auto& bar : cachedBars) {
        const std::string normalizedTradeDate = factor::cached_bars::normalizeTradeDate(bar.tradeDate);
        if (normalizedTradeDate.empty() || bar.symbol.empty()) {
            continue;
        }

        tradeDateSet.insert(normalizedTradeDate);
    }

    index.tradeDates.assign(tradeDateSet.begin(), tradeDateSet.end());
    std::sort(index.tradeDates.begin(), index.tradeDates.end());

    std::unordered_map<std::string, int> tradeDateIndexByDate;
    tradeDateIndexByDate.reserve(index.tradeDates.size());
    for (size_t indexValue = 0; indexValue < index.tradeDates.size(); ++indexValue) {
        tradeDateIndexByDate.emplace(index.tradeDates[indexValue], static_cast<int>(indexValue));
    }

    for (const auto& bar : cachedBars) {
        const std::string normalizedTradeDate = factor::cached_bars::normalizeTradeDate(bar.tradeDate);
        if (normalizedTradeDate.empty() || bar.symbol.empty()) {
            continue;
        }

        const auto tradeDateIndexIt = tradeDateIndexByDate.find(normalizedTradeDate);
        if (tradeDateIndexIt == tradeDateIndexByDate.end()) {
            continue;
        }

        auto& series = index.closeSeriesBySymbol[bar.symbol];
        series.tradeDateIndices.push_back(tradeDateIndexIt->second);
        series.closes.push_back(bar.close);
    }

    for (auto& [symbol, closeSeries] : index.closeSeriesBySymbol) {
        finalizeCachedSymbolSeries(closeSeries, forwardDays, false);
        (void)symbol;
    }

    return index;
}

FactorBacktestExecutor::CachedMarketIndex FactorBacktestExecutor::buildCachedMarketIndex(const ArrowMarketData& arrowData,
                                                                                         int forwardDays) const
{
    CachedMarketIndex index;
    index.tradeDates = arrowData.dates();

    for (const auto& symbol : arrowData.symbols()) {
        auto& closeSeries = index.closeSeriesBySymbol[symbol];
        closeSeries.tradeDateIndices.reserve(index.tradeDates.size());
        closeSeries.closes.reserve(index.tradeDates.size());
        for (size_t tradeDateIndex = 0; tradeDateIndex < index.tradeDates.size(); ++tradeDateIndex) {
            const auto& tradeDate = index.tradeDates[tradeDateIndex];
            const double close = arrowData.getValue(symbol, tradeDate, "close");
            if (!std::isfinite(close) || close <= 0.0) {
                continue;
            }

            closeSeries.tradeDateIndices.push_back(static_cast<int>(tradeDateIndex));
            closeSeries.closes.push_back(close);
        }

        finalizeCachedSymbolSeries(closeSeries, forwardDays, true);
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
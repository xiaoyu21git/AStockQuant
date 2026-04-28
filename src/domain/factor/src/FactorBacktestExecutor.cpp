#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/FactorBacktestCachedBarUtils.h"
#include "domain/factor/include/FactorBacktestGroupingUtils.h"
#include "domain/factor/include/FactorBacktestIcUtils.h"

#include "infrastructure/include/database/QtMySQLDatabase.h"

#if 0

        if (!config.cachedBars.empty()) {
            std::vector<CalculationResult> unusedFactorResults;
            std::string factorFailureReason;
            size_t streamedFactorWorkUnits = 0;

            std::vector<double> icSeries;
            std::vector<double> longShortSeries;
            std::vector<double> turnoverSeries;
            std::vector<double> alignedLongShort;
            std::vector<double> alignedBenchmark;

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

            struct MatchedSymbolValue {
                std::string symbol;
                double factorValue = 0.0;
                double futureReturn = 0.0;
            };
            BenchmarkComparisonSummary calculateBenchmarkComparison(const std::vector<double>& strategyReturns,
                                                                    const std::vector<std::string>& strategyDates) {
                BenchmarkComparisonSummary summary;
                const double annualizationFactor = 252.0 / static_cast<double>((std::max)(1, config.forwardDays));
                // ... (rest of the function remains unchanged)
                return summary;
                            groupSymbols.push_back(matchedValues[index].symbol);
                        }
                    }
                    holdingDaysSinceRebalance = 1;
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

            if (!alignedBenchmark.empty()) {
                const double benchmarkMean = factor::icir::calculateMean(alignedBenchmark);
                const double alignedStrategyMean = factor::icir::calculateMean(alignedLongShort);
                result.benchmarkAnnualReturn = benchmarkMean * annualizationFactor;
                result.excessAnnualReturn = (alignedStrategyMean - benchmarkMean) * annualizationFactor;

                std::vector<double> excessSeries;
                excessSeries.reserve(alignedBenchmark.size());
                for (size_t alignedIndex = 0; alignedIndex < alignedBenchmark.size(); ++alignedIndex) {
                    excessSeries.push_back(alignedLongShort[alignedIndex] - alignedBenchmark[alignedIndex]);
                }

                const double excessMean = factor::icir::calculateMean(excessSeries);
                const double excessStd = factor::icir::calculateStdDev(excessSeries, excessMean);
                result.trackingError = excessStd * std::sqrt(annualizationFactor);
                result.informationRatio = result.trackingError > 0.0
                    ? (result.excessAnnualReturn / result.trackingError)
                    : 0.0;

                const double benchmarkVariance = calculateVariance(alignedBenchmark, benchmarkMean);
                if (benchmarkVariance > 0.0) {
                    const double covariance = calculateCovariance(alignedLongShort,
                                                                  alignedStrategyMean,
                                                                  alignedBenchmark,
                                                                  benchmarkMean);
                    result.beta = covariance / benchmarkVariance;
                    result.alpha = result.annualReturn
                        - (config.riskFreeRate + result.beta * (result.benchmarkAnnualReturn - config.riskFreeRate));
                }
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

            result.groupResult.longShortReturn = averageLongShort;
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

#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <QVariant>

#endif

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
#include <stdexcept>
#include <QDateTime>

namespace factor {

namespace {

using Row = astock::database::QueryResultRow;

QString threadIdText()
{
    const auto threadId = std::this_thread::get_id();
    std::ostringstream stream;
    stream << threadId;
    return QString::fromStdString(stream.str());
}

QString rangeText(size_t beginIndex, size_t endIndex)
{
    return QStringLiteral("[%1,%2)").arg(beginIndex).arg(endIndex);
}

std::string buildMarketContextCacheKey(const factor::BacktestConfig& config)
{
    std::vector<std::string> allowedStockCodes = config.allowedStockCodes;
    std::sort(allowedStockCodes.begin(), allowedStockCodes.end());

    std::ostringstream stream;
    stream << config.datasetId << '|'
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

std::string buildCachedMarketIndexKey(const factor::BacktestConfig& config)
{
    std::ostringstream stream;
    stream << config.datasetId << '|' << config.cachedBars.size() << '|'
           << config.startDate << '|' << config.endDate;
    return stream.str();
}

std::string buildFutureReturnCacheKey(const std::string& symbol,
                                     const std::string& startDate,
                                     int forwardDays,
                                     const factor::BacktestConfig& config)
{
    std::ostringstream stream;
    stream << config.datasetId << '|' << config.cachedBars.size() << '|'
           << forwardDays << '|' << symbol << '|' << startDate;
    return stream.str();
}

constexpr size_t kFutureReturnCacheLimit = 200000;

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
            activeGroupSymbols.assign(static_cast<size_t>(effectiveGroupCount), {});
            for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
                const size_t begin = static_cast<size_t>(groupIndex) * groupSize;
                const size_t end = groupIndex == effectiveGroupCount - 1
                    ? matchedValues.size()
                    : (std::min)(matchedValues.size(), begin + groupSize);

                if (begin >= end) {
                    continue;
                }

                auto& groupSymbols = activeGroupSymbols[static_cast<size_t>(groupIndex)];
                groupSymbols.reserve(end - begin);
                for (size_t index = begin; index < end; ++index) {
                    groupSymbols.push_back(matchedValues[index].symbol);
                }
            }
            holdingDaysSinceRebalance = 1;
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

class CachedRowFactorDataProvider final : public FactorDataProvider {
public:
    explicit CachedRowFactorDataProvider(const std::vector<factor::CachedMarketBar>& rows)
        : rows_(&rows)
    {
        rowsByDate_.reserve(rows.size());
        rowsBySymbol_.reserve(rows.size());

        for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const auto& row = rows[rowIndex];
            const std::string normalizedTradeDate = factor::cached_bars::normalizeTradeDate(row.tradeDate);
            if (normalizedTradeDate.empty() || row.symbol.empty()) {
                continue;
            }

            rowsByDate_[normalizedTradeDate][row.symbol] = rowIndex;
            auto& symbolRows = rowsBySymbol_[row.symbol];
            symbolRows.push_back(CachedRowReference{normalizedTradeDate, rowIndex});
            availableFields_.insert("close");
            for (const auto& [field, value] : row.numericFields) {
                (void)value;
                availableFields_.insert(field);
            }
        }

        for (auto& [symbol, symbolRows] : rowsBySymbol_) {
            std::sort(symbolRows.begin(), symbolRows.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.tradeDate < rhs.tradeDate;
            });
        }
    }

    bool hasField(const std::string& field) const override {
        return availableFields_.find(field) != availableFields_.end();
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override {
        auto dateIt = rowsByDate_.find(date);
        if (dateIt == rowsByDate_.end()) {
            return std::nullopt;
        }

        auto symbolIt = dateIt->second.find(symbol);
        if (symbolIt == dateIt->second.end()) {
            return std::nullopt;
        }

        return extractFieldValue((*rows_)[symbolIt->second], field);
    }

    std::vector<FactorDataPoint> getSeries(const std::string& symbol,
                                           const std::string& startDate,
                                           const std::string& endDate,
                                           const std::string& field) const override {
        std::vector<FactorDataPoint> series;
        auto symbolIt = rowsBySymbol_.find(symbol);
        if (symbolIt == rowsBySymbol_.end()) {
            return series;
        }

        for (const auto& row : symbolIt->second) {
            if (row.tradeDate < startDate || row.tradeDate > endDate) {
                continue;
            }

            const auto value = extractFieldValue((*rows_)[row.rowIndex], field);
            if (!value.has_value()) {
                continue;
            }

            series.push_back(FactorDataPoint{row.tradeDate, *value});
        }
        return series;
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override {
        std::vector<std::string> symbols;
        auto dateIt = rowsByDate_.find(date);
        if (dateIt == rowsByDate_.end()) {
            return symbols;
        }

        symbols.reserve(dateIt->second.size());
        for (const auto& [symbol, rowIndex] : dateIt->second) {
            (void)rowIndex;
            symbols.push_back(symbol);
        }
        std::sort(symbols.begin(), symbols.end());
        return symbols;
    }

    std::unordered_map<std::string, double> getCrossSection(const std::string& date,
                                                            const std::string& field,
                                                            const std::vector<std::string>& symbols = {}) const override {
        std::unordered_map<std::string, double> values;
        auto dateIt = rowsByDate_.find(date);
        if (dateIt == rowsByDate_.end()) {
            return values;
        }

        if (symbols.empty()) {
            values.reserve(dateIt->second.size());
            for (const auto& [symbol, rowIndex] : dateIt->second) {
                const auto value = extractFieldValue((*rows_)[rowIndex], field);
                if (value.has_value()) {
                    values.emplace(symbol, *value);
                }
            }
            return values;
        }

        values.reserve(symbols.size());
        for (const auto& symbol : symbols) {
            auto symbolIt = dateIt->second.find(symbol);
            if (symbolIt == dateIt->second.end()) {
                continue;
            }

            const auto value = extractFieldValue((*rows_)[symbolIt->second], field);
            if (value.has_value()) {
                values.emplace(symbol, *value);
            }
        }

        return values;
    }

private:
    struct CachedRowReference {
        std::string tradeDate;
        size_t rowIndex = 0;
    };

    static std::optional<double> extractFieldValue(const factor::CachedMarketBar& row, const std::string& field) {
        if (field == "close") {
            return std::isfinite(row.close) && row.close > 0.0 ? std::optional<double>(row.close) : std::nullopt;
        }

        auto fieldIt = row.numericFields.find(field);
        if (fieldIt == row.numericFields.end() || !std::isfinite(fieldIt->second)) {
            return std::nullopt;
        }
        return fieldIt->second;
    }

    const std::vector<factor::CachedMarketBar>* rows_ = nullptr;
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> rowsByDate_;
    std::unordered_map<std::string, std::vector<CachedRowReference>> rowsBySymbol_;
    std::unordered_set<std::string> availableFields_;
};

std::map<QString, QVariant> makeNamedParams(std::initializer_list<std::pair<QString, QVariant>> values)
{
    std::map<QString, QVariant> params;
    for (const auto& [key, value] : values) {
        params.emplace(key, value);
    }
    return params;
}

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
    std::sort(sorted.begin(), sorted.end());
    const size_t index = static_cast<size_t>(std::floor((1.0 - confidenceLevel) * static_cast<double>(sorted.size())));
    return -sorted[(std::min)(index, sorted.size() - 1)];
}

double calculateConditionalVaR(const std::vector<double>& returns, double confidenceLevel = 0.95)
{
    if (returns.empty()) return 0.0;
    std::vector<double> sorted = returns;
    std::sort(sorted.begin(), sorted.end());
    const size_t cutoff = static_cast<size_t>(std::floor((1.0 - confidenceLevel) * static_cast<double>(sorted.size())));
    const size_t n = (std::max)(cutoff, static_cast<size_t>(1));
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
        if (cacheManager_->getBacktestResult(
                config.instanceId,
                config.startDate,
                config.endDate,
                config.forwardDays,
                config.numGroups,
                cacheSignature,
                cachedResult)) {
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
        return config.enableDateParallelism || !config.cachedBars.empty();
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

        const auto benchmarkLookup = [this, &config, &cachedMarketIndex](const std::string& date) {
            return calculateFutureReturn(config.benchmarkSymbol,
                                         date,
                                         config.forwardDays,
                                         config,
                                         config.cachedBars.empty() ? nullptr : &cachedMarketIndex);
        };

        if (!config.cachedBars.empty()) {
            std::vector<CalculationResult> unusedFactorResults;
            std::string factorFailureReason;
            size_t streamedFactorWorkUnits = 0;

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
                    const double futureReturn = calculateFutureReturn(symbol,
                                                                      factorResult.date,
                                                                      config.forwardDays,
                                                                      config,
                                                                      &cachedMarketIndex);
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
                    activeGroupSymbols.assign(static_cast<size_t>(effectiveGroupCount), {});
                    for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
                        const size_t begin = static_cast<size_t>(groupIndex) * groupSize;
                        const size_t end = groupIndex == effectiveGroupCount - 1
                            ? rankedValues.size()
                            : (std::min)(rankedValues.size(), begin + groupSize);

                        if (begin >= end) {
                            continue;
                        }

                        auto& groupSymbols = activeGroupSymbols[static_cast<size_t>(groupIndex)];
                        groupSymbols.reserve(end - begin);
                        for (size_t index = begin; index < end; ++index) {
                            groupSymbols.push_back(rankedValues[index].first);
                        }
                    }
                    holdingDaysSinceRebalance = 1;
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

            result.groupResult.longShortReturn = averageLongShort;
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
                const double futureReturn = calculateFutureReturn(symbol,
                                                                  factorResult.date,
                                                                  config.forwardDays,
                                                                  config,
                                                                  config.cachedBars.empty() ? nullptr : &cachedMarketIndex);
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
                activeGroupSymbols.assign(static_cast<size_t>(effectiveGroupCount), {});
                for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
                    const size_t begin = static_cast<size_t>(groupIndex) * groupSize;
                    const size_t end = groupIndex == effectiveGroupCount - 1
                        ? rankedValues.size()
                        : (std::min)(rankedValues.size(), begin + groupSize);

                    if (begin >= end) {
                        continue;
                    }

                    auto& groupSymbols = activeGroupSymbols[static_cast<size_t>(groupIndex)];
                    groupSymbols.reserve(end - begin);
                    for (size_t valueIndex = begin; valueIndex < end; ++valueIndex) {
                        groupSymbols.push_back(rankedValues[valueIndex].first);
                    }
                }
                holdingDaysSinceRebalance = 1;
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

        result.groupResult.longShortReturn = averageLongShort;
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
    factorResults.reserve(tradeDates.size());
    size_t emptyCalculationCount = 0;
    bool producedAnyResult = false;
    std::string lastEmptyReason;
    std::shared_ptr<FactorDataProvider> dataProvider;
    if (!config.cachedBars.empty()) {
        dataProvider = std::make_shared<CachedRowFactorDataProvider>(config.cachedBars);
    }

    qDebug() << "FactorBacktestExecutor: 计算因子序列"
             << "instanceId=" << QString::fromStdString(config.instanceId)
             << "tradeDateCount=" << static_cast<int>(tradeDates.size())
             << "cachedBarCount=" << static_cast<qulonglong>(config.cachedBars.size())
             << "allowedSymbolCount=" << static_cast<int>(config.allowedStockCodes.size())
             << "usingCacheProvider=" << static_cast<bool>(dataProvider);

    const QString runtimeFactorType = factor ? QString::fromStdString(factor->getFactorType()).trimmed() : QString();
    const bool profileLiquidity = runtimeFactorType == QString::fromUtf8("流动性因子")
        || runtimeFactorType.compare(QStringLiteral("liquidity"), Qt::CaseInsensitive) == 0;

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
                chunk.results.reserve(endIndex - beginIndex);
                for (size_t i = beginIndex; i < endIndex; ++i) {
                    if (isCancelled(progress.taskId)) {
                        chunk.success = false;
                        chunk.failureReason = "任务已取消";
                        return chunk;
                    }

                    CalculationContext context;
                    context.date = tradeDates[i];
                    context.parameters = foundation::json::JsonFacade::createObject();
                    context.parameters.set("benchmarkSymbol", foundation::json::JsonFacade::createString(config.benchmarkSymbol));
                    auto symbolsIt = marketContext.symbolsByDate.find(tradeDates[i]);
                    if (symbolsIt != marketContext.symbolsByDate.end()) {
                        context.symbols = symbolsIt->second;
                    }
                    context.dataProvider = dataProvider;
                    if (i < 3 || i + 1 == tradeDates.size()) {
                        qDebug() << "FactorBacktestExecutor: 单日样本"
                                 << "date=" << QString::fromStdString(context.date)
                                 << "symbolCount=" << static_cast<int>(context.symbols.size());
                    }

                    QElapsedTimer calculateTimer;
                    calculateTimer.start();
                    CalculationResult calculation = activeFactor.calculate(context);
                    const qint64 calculationElapsedMs = calculateTimer.elapsed();
                    if (profileLiquidity && calculationElapsedMs >= 300) {
                        qDebug() << "FactorBacktestExecutor(liquidity): 单日因子计算耗时较长"
                                 << "date=" << QString::fromStdString(context.date)
                                 << "symbolCount=" << static_cast<int>(context.symbols.size())
                                 << "resultCount=" << static_cast<int>(calculation.values.size())
                                 << "elapsedMs=" << calculationElapsedMs
                                 << "usingCacheProvider=" << static_cast<bool>(dataProvider);
                    }
                    if (!calculation.dataStatus.isValid()) {
                        chunk.success = false;
                        chunk.failureReason = calculation.dataStatus.message.empty()
                            ? "因子序列计算失败"
                            : calculation.dataStatus.message;
                        return chunk;
                    }
                    if (!calculation.isEmpty()) {
                        if (resultConsumer != nullptr) {
                            if (!(*resultConsumer)(std::move(calculation))) {
                                chunk.success = false;
                                chunk.failureReason = "任务已取消";
                                return chunk;
                            }
                        } else {
                            chunk.results.push_back(std::move(calculation));
                        }
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
        const size_t parallelWorkerLimit = config.cachedBars.empty()
            ? executorWorkers
            : (std::min)(executorWorkers, size_t{4});
        const size_t chunkCount = (std::min)(tradeDates.size(), parallelWorkerLimit);

        if (chunkCount > 1) {
            std::vector<std::pair<size_t, size_t>> ranges;
            ranges.reserve(chunkCount);
            const size_t chunkSize = (tradeDates.size() + chunkCount - 1) / chunkCount;
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

            factorResults.insert(factorResults.end(),
                                 std::make_move_iterator(firstChunk.results.begin()),
                                 std::make_move_iterator(firstChunk.results.end()));
            size_t emptyCalculationCount = firstChunk.emptyCalculationCount;
            std::string lastEmptyReason = firstChunk.lastEmptyReason;
            bool producedAnyResult = firstChunk.producedAnyResult;

            std::vector<std::future<ChunkFactorCalculation>> futures;
            futures.reserve(ranges.size() > 1 ? ranges.size() - 1 : 0);
            for (size_t rangeIndex = 1; rangeIndex < ranges.size(); ++rangeIndex) {
                const auto [beginIndex, endIndex] = ranges[rangeIndex];
                futures.emplace_back(threadPool_->submit(
                    [this,
                     config,
                     &marketContext,
                     dataProvider,
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
                    }));
            }

            for (auto& future : futures) {
                ChunkFactorCalculation chunk = future.get();
                if (!chunk.success) {
                    if (failureReason) {
                        *failureReason = chunk.failureReason.empty() ? "因子序列计算失败" : chunk.failureReason;
                    }
                    return false;
                }

                factorResults.insert(factorResults.end(),
                                     std::make_move_iterator(chunk.results.begin()),
                                     std::make_move_iterator(chunk.results.end()));
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
            return !factorResults.empty();
        }
    }

    for (size_t i = 0; i < tradeDates.size(); ++i) {
        if (isCancelled(progress.taskId)) {
            return false;
        }

        CalculationContext context;
        context.date = tradeDates[i];
        context.parameters = foundation::json::JsonFacade::createObject();
        context.parameters.set("benchmarkSymbol", foundation::json::JsonFacade::createString(config.benchmarkSymbol));
        auto symbolsIt = marketContext.symbolsByDate.find(tradeDates[i]);
        if (symbolsIt != marketContext.symbolsByDate.end()) {
            context.symbols = symbolsIt->second;
        }
        context.dataProvider = dataProvider;
        if (i < 3 || i + 1 == tradeDates.size()) {
            qDebug() << "FactorBacktestExecutor: 单日样本"
                     << "date=" << QString::fromStdString(context.date)
                     << "symbolCount=" << static_cast<int>(context.symbols.size());
        }

        QElapsedTimer calculateTimer;
        calculateTimer.start();
        CalculationResult calculation = factor->calculate(context);
        const qint64 calculationElapsedMs = calculateTimer.elapsed();
        if (profileLiquidity && calculationElapsedMs >= 300) {
            qDebug() << "FactorBacktestExecutor(liquidity): 单日因子计算耗时较长"
                     << "date=" << QString::fromStdString(context.date)
                     << "symbolCount=" << static_cast<int>(context.symbols.size())
                     << "resultCount=" << static_cast<int>(calculation.values.size())
                     << "elapsedMs=" << calculationElapsedMs
                     << "usingCacheProvider=" << static_cast<bool>(dataProvider);
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

        if (totalWorkUnits > 0) {
            updateProgress(progress,
                           progressPercentFromWork(progressBaseUnits + i + 1, totalWorkUnits),
                           "计算因子序列");
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
        const size_t parallelWorkerLimit = config.cachedBars.empty()
            ? executorWorkers
            : (std::min)(executorWorkers, size_t{4});
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
        config.numGroups,
        config.transactionCost,
        config.rebalanceDays);
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

std::vector<std::string> FactorBacktestExecutor::getTradeDates(const std::string& startDate,
                                                               const std::string& endDate,
                                                               const BacktestConfig& config)
{
    if (!config.cachedBars.empty()) {
        return factor::cached_bars::extractTradeDates(config.cachedBars, startDate, endDate);
    }

    auto db = instanceManager_ ? instanceManager_->getDatabase() : nullptr;
    if (!db) {
        return {};
    }

    auto result = db->executeQuery(
        "SELECT DISTINCT trade_date FROM daily_bar WHERE trade_date BETWEEN :start_date AND :end_date ORDER BY trade_date",
        makeNamedParams({
            {":start_date", QString::fromStdString(startDate)},
            {":end_date", QString::fromStdString(endDate)}
        })
    );

    std::vector<std::string> tradeDates;
    tradeDates.reserve(result.rowCount());
    for (size_t i = 0; i < result.rowCount(); ++i) {
        tradeDates.push_back(result.getRow(i).getString("trade_date").toStdString());
    }
    return tradeDates;
}

std::vector<std::string> FactorBacktestExecutor::getSymbols(const std::string& date,
                                                            const std::unordered_set<std::string>& allowedSymbols,
                                                            const BacktestConfig& config)
{
    if (!config.cachedBars.empty()) {
        return factor::cached_bars::extractSymbols(config.cachedBars, date, allowedSymbols);
    }

    auto db = instanceManager_ ? instanceManager_->getDatabase() : nullptr;
    if (!db) {
        return {};
    }

    auto result = db->executeQuery(
        "SELECT DISTINCT symbol FROM daily_bar WHERE trade_date = :date ORDER BY symbol",
        makeNamedParams({
            {":date", QString::fromStdString(date)}
        })
    );

    std::vector<std::string> symbols;
    symbols.reserve(result.rowCount());
    for (size_t i = 0; i < result.rowCount(); ++i) {
        const std::string symbol = result.getRow(i).getString("symbol").toStdString();
        if (!allowedSymbols.empty() && allowedSymbols.find(symbol) == allowedSymbols.end()) {
            continue;
        }
        symbols.push_back(symbol);
    }
    return symbols;
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
    if (!config.cachedBars.empty()) {
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
                const size_t futureIndex = startIndex + static_cast<size_t>(forwardDays);
                if (futureIndex < series.size()) {
                    const double startClose = series[startIndex].close;
                    const double endClose = series[futureIndex].close;
                    if (startClose > 0.0 && endClose > 0.0 && std::isfinite(startClose) && std::isfinite(endClose)) {
                        calculatedFutureReturn = (endClose - startClose) / startClose;
                    }
                }
            }
        }
    } else {
        auto db = instanceManager_ ? instanceManager_->getDatabase() : nullptr;
        if (!db || forwardDays <= 0) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        auto result = db->executeQuery(
            QString("SELECT trade_date, close FROM daily_bar WHERE symbol = :symbol AND trade_date >= :date ORDER BY trade_date ASC LIMIT %1")
                .arg(forwardDays + 1),
            makeNamedParams({
                {":symbol", QString::fromStdString(symbol)},
                {":date", QString::fromStdString(startDate)}
            })
        );

        if (result.rowCount() <= static_cast<size_t>(forwardDays)) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const double startClose = result.getRow(0).getDouble("close");
        const double endClose = result.getRow(static_cast<size_t>(forwardDays)).getDouble("close");
        if (startClose <= 0.0 || endClose <= 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        calculatedFutureReturn = (endClose - startClose) / startClose;
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
    if (!config.cachedBars.empty()) {
        if (!cachedMarketIndex) {
            if (failureReason) {
                *failureReason = "缓存行情索引未初始化";
            }
            return false;
        }
        return prepareCachedExecutionMarketContext(config, marketContext, *cachedMarketIndex, failureReason, progress);
    }

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(0, 4),
                       "准备回测上下文：查询交易日列表");
    }

    const std::string cacheKey = buildMarketContextCacheKey(config);
    {
        std::lock_guard<std::mutex> lock(marketContextMutex_);
        const auto cacheIt = marketContextCache_.find(cacheKey);
        if (cacheIt != marketContextCache_.end()) {
            marketContext = cacheIt->second;
            if (progress) {
                updateProgress(*progress,
                               progressPercentFromWork(4, 4),
                               "准备回测上下文：复用已缓存的市场上下文");
            }
            qDebug() << "FactorBacktestExecutor: 复用执行市场上下文"
                     << "datasetId=" << config.datasetId
                     << "startDate=" << QString::fromStdString(config.startDate)
                     << "endDate=" << QString::fromStdString(config.endDate)
                     << "allowedStockCount=" << static_cast<int>(config.allowedStockCodes.size());
            return true;
        }
    }

    marketContext.tradeDates = getTradeDates(config.startDate, config.endDate, config);
    if (marketContext.tradeDates.empty()) {
        if (failureReason) {
            *failureReason = "未找到可用于回测的交易日";
        }
        return false;
    }

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(1, 4),
                       "准备回测上下文：交易日列表已就绪，整理股票池映射");
    }

    marketContext.allowedSymbols = std::unordered_set<std::string>(
        config.allowedStockCodes.begin(),
        config.allowedStockCodes.end());
    marketContext.symbolsByDate.clear();
    marketContext.symbolsByDate.reserve(marketContext.tradeDates.size());

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(2, 4),
                       "准备回测上下文：整理股票池与交易日交集");
    }

    for (const auto& tradeDate : marketContext.tradeDates) {
        marketContext.symbolsByDate.emplace(
            tradeDate,
            getSymbols(tradeDate, marketContext.allowedSymbols, config));
    }

    {
        std::lock_guard<std::mutex> lock(marketContextMutex_);
        marketContextCache_[cacheKey] = marketContext;
    }

    if (progress) {
        updateProgress(*progress,
                       progressPercentFromWork(4, 4),
                       "准备回测上下文：市场上下文已就绪，准备进入因子计算");
    }

    return true;
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
        cachedIndex = buildCachedMarketIndex(config.cachedBars);
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
             << "cachedBarCount=" << static_cast<qulonglong>(config.cachedBars.size());

    return true;
}

FactorBacktestExecutor::CachedMarketIndex FactorBacktestExecutor::buildCachedMarketIndex(const std::vector<CachedMarketBar>& cachedBars) const
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
        (void)symbol;
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
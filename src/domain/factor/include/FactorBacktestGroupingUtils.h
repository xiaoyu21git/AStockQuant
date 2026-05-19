#pragma once

#include "FactorBacktestExecutor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace factor::group_backtest {

struct AggregationSummary {
    GroupBacktestResult groupResult;
    bool hasValidGroup{false};
    size_t maxMatchedStocks{0};
    int overlapDateCount{0};
    int groupedDateCount{0};
    int maxEffectiveGroupCount{0};
    std::vector<double> longShortReturnsByDate;
    std::vector<double> longShortTurnoversByDate;
    std::vector<std::string> longShortDatesByDate;
};

inline double calculatePortfolioTurnover(const std::vector<std::string>& previousSymbols,
                                         const std::vector<std::string>& currentSymbols)
{
    if (previousSymbols.empty() || currentSymbols.empty()) {
        return 0.0;
    }

    std::unordered_set<std::string> previousSet(previousSymbols.begin(), previousSymbols.end());
    size_t overlapCount = 0;
    for (const auto& symbol : currentSymbols) {
        if (previousSet.find(symbol) != previousSet.end()) {
            ++overlapCount;
        }
    }

    const size_t denominator = (std::max)(previousSymbols.size(), currentSymbols.size());
    if (denominator == 0) {
        return 0.0;
    }

    return 1.0 - (static_cast<double>(overlapCount) / static_cast<double>(denominator));
}

inline double calculateCrossSectionStdDev(const std::unordered_map<std::string, double>& factorValuesBySymbol)
{
    if (factorValuesBySymbol.size() < 2) {
        return 0.0;
    }

    double sum = 0.0;
    double sumSquares = 0.0;
    size_t count = 0;
    for (const auto& [symbol, value] : factorValuesBySymbol) {
        (void)symbol;
        if (!std::isfinite(value)) {
            continue;
        }
        sum += value;
        sumSquares += value * value;
        ++count;
    }

    if (count < 2) {
        return 0.0;
    }

    const double mean = sum / static_cast<double>(count);
    const double variance = (sumSquares / static_cast<double>(count)) - (mean * mean);
    return std::sqrt((std::max)(0.0, variance));
}

inline double calculateMeanFactorValue(const std::unordered_map<std::string, double>& factorValuesBySymbol,
                                       const std::vector<std::string>& symbols)
{
    if (symbols.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double sum = 0.0;
    size_t count = 0;
    for (const auto& symbol : symbols) {
        const auto valueIt = factorValuesBySymbol.find(symbol);
        if (valueIt == factorValuesBySymbol.end() || !std::isfinite(valueIt->second)) {
            continue;
        }
        sum += valueIt->second;
        ++count;
    }

    if (count == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sum / static_cast<double>(count);
}

inline bool passesSignalChangeThreshold(const std::unordered_map<std::string, double>& factorValuesBySymbol,
                                        const std::vector<std::string>& previousLongSymbols,
                                        const std::vector<std::string>& previousShortSymbols,
                                        const std::vector<std::string>& proposedLongSymbols,
                                        const std::vector<std::string>& proposedShortSymbols,
                                        double stdMultiplier)
{
    if (previousLongSymbols.empty() || previousShortSymbols.empty()) {
        return true;
    }
    if (!(stdMultiplier > 0.0)) {
        return true;
    }

    const double crossSectionStdDev = calculateCrossSectionStdDev(factorValuesBySymbol);
    const double signalThreshold = crossSectionStdDev * stdMultiplier;
    const double previousLongMean = calculateMeanFactorValue(factorValuesBySymbol, previousLongSymbols);
    const double previousShortMean = calculateMeanFactorValue(factorValuesBySymbol, previousShortSymbols);
    const double proposedLongMean = calculateMeanFactorValue(factorValuesBySymbol, proposedLongSymbols);
    const double proposedShortMean = calculateMeanFactorValue(factorValuesBySymbol, proposedShortSymbols);

    if (!std::isfinite(previousLongMean) || !std::isfinite(previousShortMean)
            || !std::isfinite(proposedLongMean) || !std::isfinite(proposedShortMean)) {
        return true;
    }

    const double longSignalChange = std::abs(proposedLongMean - previousLongMean);
    const double shortSignalChange = std::abs(proposedShortMean - previousShortMean);
    return (std::max)(longSignalChange, shortSignalChange) >= signalThreshold;
}

inline bool passesTurnoverLimit(const std::vector<std::string>& previousLongSymbols,
                                const std::vector<std::string>& previousShortSymbols,
                                const std::vector<std::string>& proposedLongSymbols,
                                const std::vector<std::string>& proposedShortSymbols,
                                bool enabled,
                                double maxTurnover)
{
    if (!enabled) {
        return true;
    }
    if (previousLongSymbols.empty() || previousShortSymbols.empty()) {
        return true;
    }

    const double longTurnover = calculatePortfolioTurnover(previousLongSymbols, proposedLongSymbols);
    const double shortTurnover = calculatePortfolioTurnover(previousShortSymbols, proposedShortSymbols);
    const double averageTurnover = (longTurnover + shortTurnover) / 2.0;
    return averageTurnover <= maxTurnover;
}

inline AggregationSummary aggregate(const std::vector<CalculationResult>& factorResults,
                                    const std::vector<CalculationResult>& returnResults,
                                    const BacktestConfig& config)
{
    AggregationSummary summary;
    const int groupCount = (std::max)(1, config.numGroups);
    const int rebalanceInterval = (std::max)(1, config.rebalanceDays);

    std::unordered_map<std::string, const CalculationResult*> returnsByDate;
    returnsByDate.reserve(returnResults.size());
    for (const auto& result : returnResults) {
        returnsByDate[result.date] = &result;
    }

    std::vector<double> aggregatedReturns(static_cast<size_t>(groupCount), 0.0);
    std::vector<int> aggregatedCounts(static_cast<size_t>(groupCount), 0);
    std::vector<int> aggregatedStockCounts(static_cast<size_t>(groupCount), 0);
    std::vector<double> aggregatedMinFactorValues(static_cast<size_t>(groupCount), std::numeric_limits<double>::max());
    std::vector<double> aggregatedMaxFactorValues(static_cast<size_t>(groupCount), std::numeric_limits<double>::lowest());
    std::vector<std::string> previousLongSymbols;
    std::vector<std::string> previousShortSymbols;
    std::vector<std::vector<std::string>> activeGroupSymbols;
    int holdingDaysSinceRebalance = rebalanceInterval;

    for (const auto& factorResult : factorResults) {
        auto returnIt = returnsByDate.find(factorResult.date);
        if (returnIt == returnsByDate.end()) {
            continue;
        }

        std::vector<std::pair<std::string, double>> rankedValues(factorResult.values.begin(), factorResult.values.end());
        rankedValues.erase(
            std::remove_if(rankedValues.begin(), rankedValues.end(), [&](const auto& item) {
                return returnIt->second->values.find(item.first) == returnIt->second->values.end();
            }),
            rankedValues.end()
        );

        if (rankedValues.empty()) {
            continue;
        }

        ++summary.overlapDateCount;
        summary.maxMatchedStocks = (std::max)(summary.maxMatchedStocks, rankedValues.size());

        std::sort(rankedValues.begin(), rankedValues.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second > rhs.second;
        });

        const bool shouldRebalance = activeGroupSymbols.empty() || holdingDaysSinceRebalance >= rebalanceInterval;
        if (shouldRebalance) {
            if (rankedValues.size() < 2) {
                ++summary.groupedDateCount;
                if (!activeGroupSymbols.empty()) {
                    ++holdingDaysSinceRebalance;
                }
                continue;
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
                for (size_t index = begin; index < end; ++index) {
                    groupSymbols.push_back(rankedValues[index].first);
                }
            }

            const std::unordered_map<std::string, double> factorValuesBySymbol(factorResult.values.begin(), factorResult.values.end());
            const bool passesSignalThreshold = passesSignalChangeThreshold(
                factorValuesBySymbol,
                previousLongSymbols,
                previousShortSymbols,
                proposedGroupSymbols.front(),
                proposedGroupSymbols.back(),
                config.signalChangeThresholdStdMultiplier);
            const bool passesMaxTurnover = passesTurnoverLimit(
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
                auto returnValueIt = returnIt->second->values.find(symbol);
                if (returnValueIt == returnIt->second->values.end()) {
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
            ++summary.groupedDateCount;
            if (hasTopGroup && hasBottomGroup) {
                summary.longShortReturnsByDate.push_back(topGroupReturnForDate - bottomGroupReturnForDate - (2.0 * config.transactionCost));
                summary.longShortDatesByDate.push_back(factorResult.date);
                const double longTurnover = calculatePortfolioTurnover(previousLongSymbols, activeGroupSymbols.front());
                const double shortTurnover = calculatePortfolioTurnover(previousShortSymbols, activeGroupSymbols.back());
                summary.longShortTurnoversByDate.push_back((longTurnover + shortTurnover) / 2.0);
                previousLongSymbols = activeGroupSymbols.front();
                previousShortSymbols = activeGroupSymbols.back();
            }
        }
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
            static_cast<double>(aggregatedStockCounts[static_cast<size_t>(index)]) / static_cast<double>(aggregatedCounts[static_cast<size_t>(index)]);
        summary.groupResult.minFactorValues[static_cast<size_t>(index)] = aggregatedMinFactorValues[static_cast<size_t>(index)];
        summary.groupResult.maxFactorValues[static_cast<size_t>(index)] = aggregatedMaxFactorValues[static_cast<size_t>(index)];
    }

    const bool hasUsableGroupResult = summary.hasValidGroup && summary.groupResult.groupReturns.size() >= 2;
    if (hasUsableGroupResult) {
        summary.groupResult.topGroupReturn = summary.groupResult.groupReturns.front();
        summary.groupResult.bottomGroupReturn = summary.groupResult.groupReturns.back();
        summary.groupResult.longShortReturn = summary.groupResult.topGroupReturn
            - summary.groupResult.bottomGroupReturn;
    }

    summary.hasValidGroup = hasUsableGroupResult;

    return summary;
}

inline AggregationSummary aggregate(const std::vector<CalculationResult>& factorResults,
                                    const std::vector<CalculationResult>& returnResults,
                                    int requestedNumGroups,
                                    double transactionCost,
                                    int rebalanceDays = 1)
{
    BacktestConfig config;
    config.numGroups = requestedNumGroups;
    config.transactionCost = transactionCost;
    config.rebalanceDays = rebalanceDays;
    return aggregate(factorResults, returnResults, config);
}

} // namespace factor::group_backtest
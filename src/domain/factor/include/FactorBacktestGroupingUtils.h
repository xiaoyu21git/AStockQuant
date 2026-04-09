#pragma once

#include "FactorBacktestExecutor.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <unordered_set>
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

inline AggregationSummary aggregate(const std::vector<CalculationResult>& factorResults,
                                    const std::vector<CalculationResult>& returnResults,
                                    int requestedNumGroups,
                                    double transactionCost)
{
    AggregationSummary summary;
    const int groupCount = (std::max)(1, requestedNumGroups);

    std::map<std::string, const CalculationResult*> returnsByDate;
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

        const int effectiveGroupCount = (std::max)(1, (std::min)(groupCount, static_cast<int>(rankedValues.size())));
        summary.maxEffectiveGroupCount = (std::max)(summary.maxEffectiveGroupCount, effectiveGroupCount);
        const std::size_t groupSize = (std::max)(static_cast<std::size_t>(1), rankedValues.size() / static_cast<std::size_t>(effectiveGroupCount));
        bool dateGrouped = false;
        double topGroupReturnForDate = 0.0;
        double bottomGroupReturnForDate = 0.0;
        bool hasTopGroup = false;
        bool hasBottomGroup = false;
        std::vector<std::vector<std::string>> dateGroupSymbols(static_cast<size_t>(effectiveGroupCount));
        for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
            const size_t begin = static_cast<size_t>(groupIndex) * groupSize;
            const size_t end = groupIndex == effectiveGroupCount - 1
                ? rankedValues.size()
                : (std::min)(rankedValues.size(), begin + groupSize);

            if (begin >= end) {
                continue;
            }

            double groupReturn = 0.0;
            int sampleCount = 0;
            double minFactorValue = std::numeric_limits<double>::max();
            double maxFactorValue = std::numeric_limits<double>::lowest();
            auto& groupSymbols = dateGroupSymbols[static_cast<size_t>(groupIndex)];
            groupSymbols.reserve(end - begin);
            for (size_t index = begin; index < end; ++index) {
                const auto returnValue = returnIt->second->values.at(rankedValues[index].first);
                groupReturn += returnValue;
                ++sampleCount;
                minFactorValue = (std::min)(minFactorValue, rankedValues[index].second);
                maxFactorValue = (std::max)(maxFactorValue, rankedValues[index].second);
                groupSymbols.push_back(rankedValues[index].first);
            }

            if (sampleCount == 0) {
                continue;
            }

            const double averageGroupReturn = groupReturn / static_cast<double>(sampleCount);
            aggregatedReturns[static_cast<size_t>(groupIndex)] += averageGroupReturn;
            aggregatedCounts[static_cast<size_t>(groupIndex)] += 1;
            aggregatedStockCounts[static_cast<size_t>(groupIndex)] += sampleCount;
            aggregatedMinFactorValues[static_cast<size_t>(groupIndex)] = (std::min)(aggregatedMinFactorValues[static_cast<size_t>(groupIndex)], minFactorValue);
            aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)] = (std::max)(aggregatedMaxFactorValues[static_cast<size_t>(groupIndex)], maxFactorValue);
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
                summary.longShortReturnsByDate.push_back(topGroupReturnForDate - bottomGroupReturnForDate - (2.0 * transactionCost));
                const double longTurnover = calculatePortfolioTurnover(previousLongSymbols, dateGroupSymbols.front());
                const double shortTurnover = calculatePortfolioTurnover(previousShortSymbols, dateGroupSymbols.back());
                summary.longShortTurnoversByDate.push_back((longTurnover + shortTurnover) / 2.0);
                previousLongSymbols = dateGroupSymbols.front();
                previousShortSymbols = dateGroupSymbols.back();
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
            - (2.0 * transactionCost);
    }

    summary.hasValidGroup = hasUsableGroupResult;

    return summary;
}

} // namespace factor::group_backtest
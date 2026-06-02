#pragma once

#include "FactorBacktestExecutor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace factor::icir {

struct Summary {
    ICIRResult result;
    bool hasValidSeries{false};
};

inline double calculateMean(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

inline double calculateStdDev(const std::vector<double>& values, double mean)
{
    if (values.size() < 2) {
        return 0.0;
    }

    double variance = 0.0;
    for (double value : values) {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    return std::sqrt(variance);
}

inline double calculateCorrelation(const std::vector<double>& lhs, const std::vector<double>& rhs)
{
    if (lhs.size() != rhs.size() || lhs.size() < 2) {
        return 0.0;
    }

    const double lhsMean = calculateMean(lhs);
    const double rhsMean = calculateMean(rhs);
    double covariance = 0.0;
    double lhsVariance = 0.0;
    double rhsVariance = 0.0;

    for (size_t index = 0; index < lhs.size(); ++index) {
        const double lhsDelta = lhs[index] - lhsMean;
        const double rhsDelta = rhs[index] - rhsMean;
        covariance += lhsDelta * rhsDelta;
        lhsVariance += lhsDelta * lhsDelta;
        rhsVariance += rhsDelta * rhsDelta;
    }

    if (lhsVariance <= 0.0 || rhsVariance <= 0.0) {
        return 0.0;
    }

    return covariance / std::sqrt(lhsVariance * rhsVariance);
}

inline Summary aggregate(const std::vector<CalculationResult>& factorResults,
                         const std::vector<CalculationResult>& returnResults)
{
    Summary summary;

    std::unordered_map<std::string, const CalculationResult*> returnsByDate;
    returnsByDate.reserve(returnResults.size());
    for (const auto& result : returnResults) {
        returnsByDate[result.date] = &result;
    }

    std::vector<double> icSeries;
    for (const auto& factorResult : factorResults) {
        auto returnIt = returnsByDate.find(factorResult.date);
        if (returnIt == returnsByDate.end()) {
            continue;
        }

        std::vector<double> factorValues;
        std::vector<double> returnValues;
        for (const auto& [symbol, factorValue] : factorResult.values) {
            auto valueIt = returnIt->second->values.find(symbol);
            if (valueIt == returnIt->second->values.end()) {
                continue;
            }
            factorValues.push_back(factorValue);
            returnValues.push_back(valueIt->second);
        }

        if (factorValues.size() < 2) {
            continue;
        }

        icSeries.push_back(calculateCorrelation(factorValues, returnValues));
    }

    summary.result.icSeries = icSeries;
    summary.result.icMean = calculateMean(icSeries);
    summary.result.icStd = calculateStdDev(icSeries, summary.result.icMean);
    summary.result.ir = summary.result.icStd > 0.0 ? summary.result.icMean / summary.result.icStd : 0.0;
    if (!icSeries.empty()) {
        const auto positiveCount = std::count_if(icSeries.begin(), icSeries.end(), [](double value) {
            return value > 0.0;
        });
        summary.result.icPositiveRatio = static_cast<double>(positiveCount) / static_cast<double>(icSeries.size());
    }

    summary.hasValidSeries = !icSeries.empty();
    return summary;
}

} // namespace factor::icir
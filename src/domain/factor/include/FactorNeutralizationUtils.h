#pragma once

#include "BaseFactor.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <cmath>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor::neutralization {

inline bool applyIndustrySizeNeutralization(const CalculationContext& context,
                                            std::unordered_map<std::string, double>& values,
                                            std::string* errorMessage,
                                            const factor::bridge::FieldKey& industryField = factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE,
                                            const factor::bridge::FieldKey& marketCapField = factor::bridge::MarketBarFieldKeys::MARKET_CAP)
{
    const std::string industryFieldName = industryField.c_str();
    const std::string marketCapFieldName = marketCapField.c_str();

    if (!context.historicalView) {
        if (errorMessage) {
            *errorMessage = "中性化要求 HistoricalView 已就绪";
        }
        return false;
    }
    if (!context.historicalView->hasField(industryFieldName)) {
        if (errorMessage) {
            *errorMessage = "中性化缺少 " + industryFieldName + " 字段";
        }
        return false;
    }
    if (!context.historicalView->hasField(marketCapFieldName)) {
        if (errorMessage) {
            *errorMessage = "中性化缺少 " + marketCapFieldName + " 字段";
        }
        return false;
    }

    const auto industryBySymbol = context.historicalView->getCrossSection(context.date, industryFieldName, context.symbols);
    const auto marketCapBySymbol = context.historicalView->getCrossSection(context.date, marketCapFieldName, context.symbols);

    struct Sample {
        std::string symbol;
        double value{0.0};
        double logMarketCap{0.0};
        long long industryCode{0};
    };

    std::vector<Sample> samples;
    samples.reserve(values.size());
    std::unordered_map<long long, std::vector<double>> industryBuckets;
    for (const auto& [symbol, value] : values) {
        if (!std::isfinite(value)) {
            continue;
        }

        const auto industryIt = industryBySymbol.find(symbol);
        const auto marketCapIt = marketCapBySymbol.find(symbol);
        if (industryIt == industryBySymbol.end() || marketCapIt == marketCapBySymbol.end()) {
            continue;
        }
        if (!std::isfinite(industryIt->second) || !std::isfinite(marketCapIt->second) || marketCapIt->second <= 0.0) {
            continue;
        }

        const long long industryCode = static_cast<long long>(std::llround(industryIt->second));
        const double logMarketCap = std::log((std::max)(marketCapIt->second, 1.0));
        samples.push_back({symbol, value, logMarketCap, industryCode});
        industryBuckets[industryCode].push_back(value);
    }

    if (samples.size() < 3) {
        if (errorMessage) {
            *errorMessage = "中性化样本不足，无法完成行业和市值残差化";
        }
        return false;
    }

    std::vector<double> residuals;
    std::vector<double> logCaps;
    residuals.reserve(samples.size());
    logCaps.reserve(samples.size());
    for (const Sample& sample : samples) {
        const auto bucketIt = industryBuckets.find(sample.industryCode);
        if (bucketIt == industryBuckets.end() || bucketIt->second.empty()) {
            continue;
        }

        const std::vector<double>& bucket = bucketIt->second;
        const double industryMean = std::accumulate(bucket.begin(), bucket.end(), 0.0)
            / static_cast<double>(bucket.size());
        residuals.push_back(sample.value - industryMean);
        logCaps.push_back(sample.logMarketCap);
    }

    if (residuals.size() < 3 || residuals.size() != samples.size() || logCaps.size() != samples.size()) {
        if (errorMessage) {
            *errorMessage = "中性化样本不足，无法完成行业和市值残差化";
        }
        return false;
    }

    const double xMean = std::accumulate(logCaps.begin(), logCaps.end(), 0.0)
        / static_cast<double>(logCaps.size());
    const double yMean = std::accumulate(residuals.begin(), residuals.end(), 0.0)
        / static_cast<double>(residuals.size());
    double numerator = 0.0;
    double denominator = 0.0;
    for (size_t index = 0; index < residuals.size(); ++index) {
        numerator += (logCaps[index] - xMean) * (residuals[index] - yMean);
        denominator += (logCaps[index] - xMean) * (logCaps[index] - xMean);
    }
    const double slope = denominator > 0.0 ? numerator / denominator : 0.0;

    std::unordered_map<std::string, double> neutralizedValues;
    neutralizedValues.reserve(samples.size());
    for (size_t index = 0; index < samples.size(); ++index) {
        const double neutralized = residuals[index] - slope * (logCaps[index] - xMean);
        if (std::isfinite(neutralized)) {
            neutralizedValues[samples[index].symbol] = neutralized;
        }
    }

    if (neutralizedValues.empty()) {
        if (errorMessage) {
            *errorMessage = "中性化后没有有效样本";
        }
        return false;
    }

    values = std::move(neutralizedValues);
    return true;
}

} // namespace factor::neutralization
#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace factor {

using namespace configurable_factor_detail;

namespace {

std::string dividendMetricFieldName(const DividendIndicatorSpec& indicator)
{
    return indicator.common.fieldKey ? std::string(indicator.common.fieldKey->c_str()) : std::string();
}

std::string joinStrings(const std::vector<std::string>& values, const char* delimiter)
{
    if (values.empty()) {
        return "";
    }

    std::ostringstream stream;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << delimiter;
        }
        stream << values[index];
    }
    return stream.str();
}

std::vector<DividendMetric> dividendMetricList(const std::vector<DividendIndicatorSpec>& indicators)
{
    std::vector<DividendMetric> metrics;
    metrics.reserve(indicators.size());
    for (const DividendIndicatorSpec& indicator : indicators) {
        metrics.push_back(indicator.metric);
    }
    return metrics;
}

} // namespace

CalculationResult ConfigurableFactorBase::calculateDividend(const CalculationContext& context) const
{
    const CommonParams& common = commonParams_;
    const DividendParams& dividend = dividendParams();
    const DividendConfigMode dividendConfigMode = !dividend.dividendMetrics.empty()
        ? DividendConfigMode::DividendMetrics
        : DividendConfigMode::DividendMetric;

    std::vector<DividendIndicatorSpec> dividendIndicators;
    std::unordered_set<int> seenDividendMetrics;
    for (const DividendMetric metric : dividend.dividendMetrics) {
        const DividendIndicatorSpec indicator = dividendIndicatorSpec(metric);
        const int indicatorKey = static_cast<int>(indicator.metric);
        if (indicator.common.hasResolvedSource() && seenDividendMetrics.insert(indicatorKey).second) {
            dividendIndicators.push_back(indicator);
        }
    }
    if (dividendIndicators.empty()) {
        const DividendIndicatorSpec indicator = dividendIndicatorSpec(dividend.dividendMetric);
        if (!indicator.common.hasResolvedSource()) {
            return createHistoricalViewRuntimeError(context, "红利因子缺少有效 metric 枚举");
        }
        dividendIndicators.push_back(indicator);
    }

    std::vector<std::string> dateResolutionFields;
    dateResolutionFields.reserve(dividendIndicators.size());
    std::vector<std::string> batchFields;
    batchFields.reserve(dividendIndicators.size());
    std::unordered_set<std::string> seenBatchFields;
    for (const DividendIndicatorSpec& indicator : dividendIndicators) {
        const std::string fieldName = dividendMetricFieldName(indicator);
        dateResolutionFields.push_back(fieldName);
        if (seenBatchFields.insert(fieldName).second) {
            batchFields.push_back(fieldName);
        }
    }

    return executeWithCommonParams(
        context,
        common,
        [this, &context, &common, &dateResolutionFields]() {
            return resolveCommonEffectiveDateForFields(
                context,
                common,
                dateResolutionFields,
                CommonFieldRequirementMode::AnyField);
        },
        [this, &context, &dividend, &dividendIndicators, &batchFields](const CommonRuntimeState& runtime, CalculationResult& result) {
            CalculationContext effectiveContext = context;
            effectiveContext.date = runtime.effectiveDate;

            const std::vector<std::string> symbols = effectiveSymbols(effectiveContext);
            for (const std::string& fieldName : batchFields) {
                if (!context.historicalView->hasField(fieldName)) {
                    const std::string errorMessage = "红利因子 HistoricalView 回测缺少字段 " + fieldName;
                    result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                    return;
                }
            }

            std::unordered_map<std::string, std::unordered_map<std::string, double>> batchCrossSections;
            if (context.historicalView && !batchFields.empty()) {
                batchCrossSections = context.historicalView->getBatchCrossSections(runtime.effectiveDate, symbols, batchFields);
                if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
                    for (const auto& [fieldName, symbolValues] : batchCrossSections) {
                        std::string batchKey;
                        buildBatchCrossSectionKey(batchKey, runtime.effectiveDate, fieldName);
                        activeBatchComputationCache->crossSectionsByKey[batchKey] = symbolValues;
                    }
                }
            }

            bool hasAnyMetricData = false;
            std::vector<std::string> fieldSampleSummaries;
            fieldSampleSummaries.reserve(dividendIndicators.size());
            for (const DividendIndicatorSpec& indicator : dividendIndicators) {
                const std::string fieldName = dividendMetricFieldName(indicator);
                const auto fieldIt = batchCrossSections.find(fieldName);
                const std::unordered_map<std::string, double> metricMap = fieldIt != batchCrossSections.end() ? fieldIt->second : std::unordered_map<std::string, double>{};
                if (!metricMap.empty()) {
                    hasAnyMetricData = true;
                }
                fieldSampleSummaries.push_back(fieldName + ":" + std::to_string(metricMap.size()));
                batchCrossSections[fieldName] = metricMap;
            }

            if (!hasAnyMetricData) {
                result.metadata.set("emptyReason", json_helper::toJsonValue("红利因子字段存在但当天没有可用分红数据"));
                return;
            }

            for (const auto& symbol : symbols) {
                std::vector<double> scores;
                bool rejectedByYieldFloor = false;

                for (const DividendIndicatorSpec& indicator : dividendIndicators) {
                    const std::string fieldName = dividendMetricFieldName(indicator);
                    const auto fieldIt = batchCrossSections.find(fieldName);
                    const auto& directMetricMap = fieldIt != batchCrossSections.end()
                        ? fieldIt->second
                        : std::unordered_map<std::string, double>{};
                    const auto directIt = directMetricMap.find(symbol);
                    if (directIt == directMetricMap.end() || !std::isfinite(directIt->second)) {
                        continue;
                    }

                    if (indicator.metric == DividendMetric::DIVIDEND_YIELD && normalizeDividendYieldFloor(dividend.minDividendYield) > 0.0
                        && directIt->second < normalizeDividendYieldFloor(dividend.minDividendYield)) {
                        rejectedByYieldFloor = true;
                        break;
                    }

                    scores.push_back(directIt->second);
                }

                if (rejectedByYieldFloor || scores.empty()) {
                    continue;
                }

                result.values[symbol] = safeMean(scores);
            }

            if (result.values.empty()) {
                result.metadata.set("fieldSampleCounts", json_helper::toJsonValue(joinStrings(fieldSampleSummaries, ", ")));
                result.metadata.set("emptyReason", json_helper::toJsonValue("红利因子没有可用分红数据"));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&dividendIndicators, dividendConfigMode](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("metric", json_helper::toJsonValue(static_cast<int>(dividendIndicators.front().metric)));
            result.metadata.set("metrics", dividendMetricArrayJson(dividendMetricList(dividendIndicators)));
            result.metadata.set("metricSourceTable", json_helper::toJsonValue(static_cast<int>(SourceTable::FINANCIAL_INDICATOR)));
            result.metadata.set("dividendConfigMode", json_helper::toJsonValue(static_cast<int>(dividendConfigMode)));
            result.metadata.set("dataMode", json_helper::toJsonValue(static_cast<int>(ConfigurableDataMode::BatchCrossSection)));
        },
        "使用红利字段");
}

} // namespace factor
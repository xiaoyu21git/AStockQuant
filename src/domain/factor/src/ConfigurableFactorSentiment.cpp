#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace factor {

using namespace configurable_factor_detail;

CalculationResult ConfigurableFactorBase::calculateSentiment(const CalculationContext& context) const
{
    const CommonParams& common = commonParams_;
    const SentimentParams& sentiment = sentimentParams();
    const int window = (std::max)(5, static_cast<int>(common.window));
    const auto symbols = effectiveSymbols(context);
    const SentimentIndicatorSpec metricSpec = sentimentIndicatorSpec(sentiment.sentimentMetric);
    const std::string metric = metricSpec.common.fieldKey ? std::string(metricSpec.common.fieldKey->c_str()) : std::string();
    if (metric.empty() || metricSpec.common.sourceTable != SourceTable::NEWS_SENTIMENT) {
        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = context.date;
        result.dataStatus = CalculationResult::createError("情绪因子缺少有效 metric 枚举").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("情绪因子缺少有效 metric 枚举"));
        return result;
    }

    const DataFrequency frequency = common.frequency;

    CalculationContext effectiveContext = context;
    effectiveContext.symbols = symbols;

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateSentimentBody = [&]() -> CalculationResult {
        return executeWithCommonParams(
            context,
            common,
            [this, &context, &common, &effectiveContext, &metric]() {
                return resolveCommonEffectiveDateForFields(
                    effectiveContext,
                    common,
                    std::vector<std::string>{metric},
                    CommonFieldRequirementMode::AnyField);
            },
            [this, &effectiveContext, &metric, window, &symbols](const CommonRuntimeState& runtime, CalculationResult& result) {
                effectiveContext.date = runtime.effectiveDate;
                const auto directMetricMap = currentFieldCrossSection(effectiveContext, metric);
                const auto metricSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, metric, window);
                if (directMetricMap.empty() && metricSeriesBySymbol.empty()) {
                    const std::string errorMessage = "情绪因子缺少真实情绪字段，已禁止使用市场宽度代理回测";
                    result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                    return;
                }

                for (const auto& symbol : symbols) {
                    double resolvedValue = std::numeric_limits<double>::quiet_NaN();
                    const auto seriesIt = metricSeriesBySymbol.find(symbol);
                    if (seriesIt != metricSeriesBySymbol.end()) {
                        resolvedValue = safeFiniteMean(seriesIt->second);
                    }
                    if (!std::isfinite(resolvedValue)) {
                        const auto directIt = directMetricMap.find(symbol);
                        if (directIt != directMetricMap.end()) {
                            resolvedValue = directIt->second;
                        }
                    }
                    if (std::isfinite(resolvedValue)) {
                        result.values[symbol] = resolvedValue;
                    }
                }

                if (result.values.empty()) {
                    result.metadata.set("emptyReason", json_helper::toJsonValue("情绪因子字段存在但没有可用数值"));
                }
            },
            [](const CommonRuntimeState&, CalculationResult&) {},
            [&sentiment, &metricSpec, &symbols, window](const CommonRuntimeState&, CalculationResult& result) {
                const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
                result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
                result.dataStatus.coverage = coverage;
                result.metadata.set("metric", json_helper::toJsonValue(static_cast<int>(sentiment.sentimentMetric)));
                result.metadata.set("metricSourceTable", json_helper::toJsonValue(static_cast<int>(metricSpec.common.sourceTable)));
                result.metadata.set("sentimentSource", json_helper::toJsonValue(static_cast<int>(sentiment.sentimentSource)));
                result.metadata.set("dataMode", json_helper::toJsonValue(static_cast<int>(ConfigurableDataMode::Direct)));
                result.metadata.set("window", json_helper::toJsonValue(window));
            },
            "使用情绪字段/代理模型");
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return calculateSentimentBody();
    }

    return calculateSentimentBody();
}

} // namespace factor
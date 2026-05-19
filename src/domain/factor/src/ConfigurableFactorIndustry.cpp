#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cmath>

namespace factor {

using namespace configurable_factor_detail;

CalculationResult ConfigurableFactorBase::calculateIndustry(const CalculationContext& context) const
{
    const CommonParams& common = commonParams_;
    const IndustryParams& industry = industryParams();
    const IndustryIndicatorSpec industryIndicator = industryIndicatorSpec(industry.industryMetricKind);
    const QString industryMetric = industryIndicator.common.fieldKey ? industryIndicator.common.fieldKey->toQString() : QString();
    if (industryMetric.isEmpty()) {
        return createHistoricalViewRuntimeError(context, "行业因子缺少有效 metric 枚举");
    }
    const ConfigurableSectorType sectorType = industry.sectorType;
    const int window = (std::max)(1, static_cast<int>(common.window));
    const double sectorWeight = sectorIndustryWeight(sectorType);

    return executeWithCommonParams(
        context,
        common,
        [this, &context, &common, &industryMetric]() {
            return resolveCommonEffectiveDateForFields(
                context,
                common,
                QStringList{industryMetric},
                CommonFieldRequirementMode::AnyField);
        },
        [this, &context, &industryMetric, window, sectorWeight](const CommonRuntimeState& runtime, CalculationResult& result) {
            CalculationContext effectiveContext = context;
            effectiveContext.date = runtime.effectiveDate.toStdString();
            effectiveContext.symbols = effectiveSymbols(effectiveContext);

            const auto metricValues = currentFieldCrossSection(effectiveContext, industryMetric);
            if (metricValues.empty()) {
                const QString errorMessage = QStringLiteral("行业因子缺少真实行业字段，已禁止使用代理模型回测");
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                return;
            }

            const auto metricSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, industryMetric, window);
            for (const auto& [symbol, value] : metricValues) {
                double resolvedValue = value;
                const auto seriesIt = metricSeriesBySymbol.find(symbol);
                if (seriesIt != metricSeriesBySymbol.end()) {
                    const double aggregatedValue = safeFiniteMean(seriesIt->second);
                    if (std::isfinite(aggregatedValue)) {
                        resolvedValue = aggregatedValue;
                    }
                }
                resolvedValue *= sectorWeight;
                if (std::isfinite(resolvedValue)) {
                    result.values[symbol] = resolvedValue;
                }
            }

            if (result.values.empty()) {
                result.metadata.set("emptyReason", json_helper::toJsonValue("行业因子字段存在但没有可用数值"));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&industry, &industryIndicator, sectorType, window](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("industryMetric", json_helper::toJsonValue(static_cast<int>(industry.industryMetricKind)));
            result.metadata.set("industryMetricSourceTable", json_helper::toJsonValue(static_cast<int>(industryIndicator.common.sourceTable)));
            result.metadata.set("sectorType", json_helper::toJsonValue(static_cast<int>(sectorType)));
            result.metadata.set("window", json_helper::toJsonValue(window));
            result.metadata.set("dataMode", json_helper::toJsonValue(static_cast<int>(ConfigurableDataMode::DirectCrossSection)));
        },
        QStringLiteral("使用行业字段"));
}

} // namespace factor
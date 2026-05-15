#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cmath>

namespace factor {

using namespace configurable_factor_detail;

CalculationResult ConfigurableFactorBase::calculateIndustry(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用行业字段";
    const CommonParams& common = commonParams_;
    const IndustryParams& industry = industryParams();
    const IndustryIndicatorSpec industryIndicator = industryIndicatorSpec(industry.industryMetricKind);
    const QString industryMetric = industryIndicator.common.fieldKey ? industryIndicator.common.fieldKey->toQString() : QString();
    if (industryMetric.isEmpty()) {
        result.dataStatus = CalculationResult::createError("行业因子缺少有效 metric 枚举").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("行业因子缺少有效 metric 枚举"));
        return result;
    }
    const ConfigurableSectorType sectorType = industry.sectorType;
    const DataFrequency frequency = common.frequency;
    const StandardizationMethod standardization = common.standardization;
    const int window = (std::max)(1, static_cast<int>(common.window));
    const double sectorWeight = sectorIndustryWeight(sectorType);

    auto appendCommonMetadata = [&](const QString& effectiveDate, CommonNeutralizationMode neutralizationMode) {
        result.metadata.set("industryMetric", json_helper::toJsonValue(industryMetricText(industry.industryMetricKind).toStdString()));
        result.metadata.set("industryMetricSourceTable", json_helper::toJsonValue(static_cast<int>(industryIndicator.common.sourceTable)));
        result.metadata.set("sectorType", json_helper::toJsonValue(sectorTypeText(sectorType).toStdString()));
        result.metadata.set("window", json_helper::toJsonValue(window));
        result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
        result.metadata.set("frequency", json_helper::toJsonValue(configurableFrequencyText(frequency).toStdString()));
        result.metadata.set("lookbackPeriod", json_helper::toJsonValue(common.lookbackWindow));
        result.metadata.set("laggedEnabled", json_helper::toJsonValue(common.lagEnabled));
        result.metadata.set("standardization", json_helper::toJsonValue(configurableStandardizationText(standardization).toStdString()));
        result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(common.neutralizationEnabled));
        result.metadata.set("neutralizationMode", json_helper::toJsonValue(configurableNeutralizationModeText(neutralizationMode).toStdString()));
        result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    };

    auto resolveIndustryEffectiveDate = [&]() {
        QString effectiveDate = QString::fromStdString(context.date);
        QDate anchorDate = QDate::fromString(effectiveDate, Qt::ISODate);
        if (anchorDate.isValid()) {
            if (frequency == DataFrequency::Weekly) {
                const int shiftToPreviousFriday = anchorDate.dayOfWeek() >= 5 ? anchorDate.dayOfWeek() - 5 : anchorDate.dayOfWeek() + 2;
                anchorDate = anchorDate.addDays(-shiftToPreviousFriday);
            } else if (frequency == DataFrequency::Monthly) {
                anchorDate = QDate(anchorDate.year(), anchorDate.month(), 1).addDays(-1);
            }
            effectiveDate = anchorDate.toString(Qt::ISODate);
        }

        const int maxOffset = (std::max)(0, static_cast<int>(common.lookbackWindow));
        const int startOffset = common.lagEnabled ? (std::max)(1, static_cast<int>(common.lagPeriods)) : 0;
        const std::vector<std::string> symbols = context.symbols.empty()
            ? context.historicalView->getAvailableSymbols(context.date)
            : context.symbols;
        for (int offset = startOffset; offset <= maxOffset; ++offset) {
            const QString candidate = anchorDate.isValid()
                ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                : effectiveDate;
            CalculationContext candidateContext = context;
            candidateContext.date = candidate.toStdString();
            candidateContext.symbols = symbols;
            if (!currentFieldCrossSection(candidateContext, industryMetric).empty()) {
                return candidate;
            }
        }

        return effectiveDate;
    };

    const QString effectiveDate = resolveIndustryEffectiveDate();
    CalculationContext effectiveContext = context;
    effectiveContext.date = effectiveDate.toStdString();
    effectiveContext.symbols = effectiveSymbols(effectiveContext);
    CommonNeutralizationMode industryNeutralizationMode = common.neutralizationEnabled
        ? CommonNeutralizationMode::REQUESTED
        : CommonNeutralizationMode::DISABLED;

    const auto metricValues = currentFieldCrossSection(effectiveContext, industryMetric);
    if (metricValues.empty()) {
        result.dataStatus = CalculationResult::createError("行业因子缺少真实行业字段，已禁止使用代理模型回测").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("行业因子缺少真实行业字段，已禁止使用代理模型回测"));
        appendCommonMetadata(effectiveDate, industryNeutralizationMode);
        return result;
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
        result.dataStatus = CalculationResult::createError("行业因子字段存在但没有可用数值").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("行业因子字段存在但没有可用数值"));
        appendCommonMetadata(effectiveDate, industryNeutralizationMode);
        return result;
    }

    if (common.neutralizationEnabled) {
        QString errorMessage;
        if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            result.values.clear();
            industryNeutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_NEUTRALIZATION_FAILED;
            appendCommonMetadata(effectiveDate, industryNeutralizationMode);
            return result;
        }
        industryNeutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_CROSS_SECTION_INDUSTRY_SIZE;
    }

    applyConfigurableStandardization(standardization, result.values);

    appendCommonMetadata(effectiveDate, industryNeutralizationMode);
    result.metadata.set("dataMode", json_helper::toJsonValue(static_cast<int>(ConfigurableDataMode::DirectCrossSection)));
    return result;
}

} // namespace factor
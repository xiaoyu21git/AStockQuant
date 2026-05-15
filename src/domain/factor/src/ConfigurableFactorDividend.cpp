#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace factor {

using namespace configurable_factor_detail;

namespace {

QString dividendMetricFieldName(const DividendIndicatorSpec& indicator)
{
    return indicator.common.fieldKey ? indicator.common.fieldKey->toQString() : QString();
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
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用红利字段";
    const CommonParams& common = commonParams_;
    const DividendParams& dividend = dividendParams();
    const DataFrequency frequency = common.frequency;
    const StandardizationMethod standardization = common.standardization;
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
            result.dataStatus = CalculationResult::createError("红利因子缺少有效 metric 枚举").dataStatus;
            result.metadata.set("emptyReason", json_helper::toJsonValue("红利因子缺少有效 metric 枚举"));
            return result;
        }
        dividendIndicators.push_back(indicator);
    }

    auto appendCommonMetadata = [&](const QString& effectiveDate, CommonNeutralizationMode neutralizationMode) {
        result.metadata.set("metric", json_helper::toJsonValue(dividendMetricText(dividendIndicators.front().metric).toStdString()));
        result.metadata.set("metrics", dividendMetricArrayJson(dividendMetricList(dividendIndicators)));
        result.metadata.set("metricSourceTable", json_helper::toJsonValue(static_cast<int>(SourceTable::FINANCIAL_INDICATOR)));
        result.metadata.set("dividendConfigMode", json_helper::toJsonValue(static_cast<int>(dividendConfigMode)));
        result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
        result.metadata.set("frequency", json_helper::toJsonValue(configurableFrequencyText(frequency).toStdString()));
        result.metadata.set("lookbackPeriod", json_helper::toJsonValue(common.lookbackWindow));
        result.metadata.set("laggedEnabled", json_helper::toJsonValue(common.lagEnabled));
        result.metadata.set("standardization", json_helper::toJsonValue(configurableStandardizationText(standardization).toStdString()));
        result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(common.neutralizationEnabled));
        result.metadata.set("neutralizationMode", json_helper::toJsonValue(configurableNeutralizationModeText(neutralizationMode).toStdString()));
        result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    };

    auto resolveDividendEffectiveDate = [&]() {
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

            bool hasAnyField = false;
            for (const DividendIndicatorSpec& indicator : dividendIndicators) {
                if (!currentFieldCrossSection(candidateContext, dividendMetricFieldName(indicator)).empty()) {
                    hasAnyField = true;
                    break;
                }
            }
            if (hasAnyField) {
                return candidate;
            }
        }

        return effectiveDate;
    };

    CommonNeutralizationMode dividendNeutralizationMode = common.neutralizationEnabled
        ? CommonNeutralizationMode::REQUESTED
        : CommonNeutralizationMode::DISABLED;
    const QString effectiveDate = resolveDividendEffectiveDate();
    CalculationContext effectiveContext = context;
    effectiveContext.date = effectiveDate.toStdString();

    const std::vector<std::string> symbols = effectiveSymbols(effectiveContext);
    std::vector<std::string> batchFields;
    batchFields.reserve(static_cast<size_t>(dividendIndicators.size()));
    std::unordered_set<std::string> seenBatchFields;
    for (const DividendIndicatorSpec& indicator : dividendIndicators) {
        const std::string fieldName = dividendMetricFieldName(indicator).toStdString();
        if (seenBatchFields.insert(fieldName).second) {
            batchFields.push_back(fieldName);
        }
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> batchCrossSections;
    if (context.historicalView && !batchFields.empty()) {
        batchCrossSections = context.historicalView->getBatchCrossSections(effectiveDate.toStdString(), symbols, batchFields);
        if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
            for (const auto& [fieldName, symbolValues] : batchCrossSections) {
                std::string batchKey;
                buildBatchCrossSectionKey(batchKey, effectiveDate.toStdString(), QString::fromStdString(fieldName));
                activeBatchComputationCache->crossSectionsByKey[batchKey] = symbolValues;
            }
        }
    }

    bool hasAnyMetricData = false;
    for (const DividendIndicatorSpec& indicator : dividendIndicators) {
        const std::string fieldName = dividendMetricFieldName(indicator).toStdString();
        const auto fieldIt = batchCrossSections.find(fieldName);
        const std::unordered_map<std::string, double> metricMap = fieldIt != batchCrossSections.end() ? fieldIt->second : std::unordered_map<std::string, double>{};
        if (!metricMap.empty()) {
            hasAnyMetricData = true;
        }
        batchCrossSections[fieldName] = metricMap;
    }

    if (!hasAnyMetricData) {
        result.dataStatus = CalculationResult::createError("红利因子缺少真实底层字段，已禁止使用代理模型回测").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("红利因子缺少真实底层字段，已禁止使用代理模型回测"));
        appendCommonMetadata(effectiveDate, dividendNeutralizationMode);
        return result;
    }

    for (const auto& symbol : symbols) {
        std::vector<double> scores;
        bool rejectedByYieldFloor = false;

        for (const DividendIndicatorSpec& indicator : dividendIndicators) {
            const std::string fieldName = dividendMetricFieldName(indicator).toStdString();
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
        result.dataStatus = CalculationResult::createError("红利因子没有可用分红数据").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("红利因子没有可用分红数据"));
        appendCommonMetadata(effectiveDate, dividendNeutralizationMode);
        return result;
    }

    if (common.neutralizationEnabled) {
        QString errorMessage;
        if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            result.values.clear();
            dividendNeutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_NEUTRALIZATION_FAILED;
            appendCommonMetadata(effectiveDate, dividendNeutralizationMode);
            return result;
        }
        dividendNeutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_CROSS_SECTION_INDUSTRY_SIZE;
    }

    applyConfigurableStandardization(standardization, result.values);

    appendCommonMetadata(effectiveDate, dividendNeutralizationMode);
    result.metadata.set("dataMode", json_helper::toJsonValue(static_cast<int>(ConfigurableDataMode::BatchCrossSection)));
    return result;
}

} // namespace factor
#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace factor {

using namespace configurable_factor_detail;

CalculationResult ConfigurableFactorBase::calculateSentiment(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用情绪字段/代理模型";

    const CommonParams& common = commonParams_;
    const SentimentParams& sentiment = sentimentParams();
    const int window = (std::max)(5, static_cast<int>(common.window));
    const auto symbols = effectiveSymbols(context);
    const SentimentIndicatorSpec metricSpec = sentimentIndicatorSpec(sentiment.sentimentMetric);
    const QString metric = metricSpec.common.fieldKey ? metricSpec.common.fieldKey->toQString() : QString();
    if (metric.isEmpty() || metricSpec.common.sourceTable != SourceTable::NEWS_SENTIMENT) {
        result.dataStatus = CalculationResult::createError("情绪因子缺少有效 metric 枚举").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("情绪因子缺少有效 metric 枚举"));
        return result;
    }

    const DataFrequency frequency = common.frequency;
    const StandardizationMethod standardization = common.standardization;

    CalculationContext effectiveContext = context;
    effectiveContext.symbols = symbols;

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateSentimentBody = [&]() -> CalculationResult {
        auto resolveSentimentEffectiveDate = [&]() {
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
            for (int offset = startOffset; offset <= maxOffset; ++offset) {
                const QString candidate = anchorDate.isValid()
                    ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                    : effectiveDate;
                CalculationContext candidateContext = effectiveContext;
                candidateContext.date = candidate.toStdString();
                if (!currentFieldCrossSection(candidateContext, metric).empty()) {
                    return candidate;
                }
            }

            return effectiveDate;
        };

        const QString effectiveDate = resolveSentimentEffectiveDate();
        effectiveContext.date = effectiveDate.toStdString();
        CommonNeutralizationMode neutralizationMode = common.neutralizationEnabled
            ? CommonNeutralizationMode::REQUESTED
            : CommonNeutralizationMode::DISABLED;

        const auto appendCommonMetadata = [&](CommonNeutralizationMode resolvedNeutralizationMode) {
            result.metadata.set("metric", json_helper::toJsonValue(static_cast<int>(sentiment.sentimentMetric)));
            result.metadata.set("metricSourceTable", json_helper::toJsonValue(static_cast<int>(metricSpec.common.sourceTable)));
            result.metadata.set("sentimentSource", json_helper::toJsonValue(static_cast<int>(sentiment.sentimentSource)));
            result.metadata.set("dataMode", json_helper::toJsonValue(static_cast<int>(ConfigurableDataMode::Direct)));
            result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
            result.metadata.set("frequency", json_helper::toJsonValue(static_cast<int>(frequency)));
            result.metadata.set("lookbackPeriod", json_helper::toJsonValue(common.lookbackWindow));
            result.metadata.set("laggedEnabled", json_helper::toJsonValue(common.lagEnabled));
            result.metadata.set("standardization", json_helper::toJsonValue(static_cast<int>(standardization)));
            result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(common.neutralizationEnabled));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(resolvedNeutralizationMode)));
            result.metadata.set("window", json_helper::toJsonValue(window));
        };

        const auto directMetricMap = currentFieldCrossSection(effectiveContext, metric);
        const auto metricSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, metric, window);
        if (!directMetricMap.empty() || !metricSeriesBySymbol.empty()) {
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
                result.dataStatus = CalculationResult::createError("情绪因子字段存在但没有可用数值").dataStatus;
                result.metadata.set("emptyReason", json_helper::toJsonValue("情绪因子字段存在但没有可用数值"));
                appendCommonMetadata(neutralizationMode);
                return result;
            }

            if (common.neutralizationEnabled) {
                QString errorMessage;
                if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    result.values.clear();
                    neutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_NEUTRALIZATION_FAILED;
                    appendCommonMetadata(neutralizationMode);
                    return result;
                }
                neutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_CROSS_SECTION_INDUSTRY_SIZE;
            }

            applyConfigurableStandardization(standardization, result.values);
            const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
            result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
            result.dataStatus.coverage = coverage;
            appendCommonMetadata(neutralizationMode);
            return result;
        }

        result.dataStatus = CalculationResult::createError("情绪因子缺少真实情绪字段，已禁止使用市场宽度代理回测").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("情绪因子缺少真实情绪字段，已禁止使用市场宽度代理回测"));
        appendCommonMetadata(neutralizationMode);
        return result;
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
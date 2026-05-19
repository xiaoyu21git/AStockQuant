#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <QDebug>

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

    QStringList dateResolutionFields;
    dateResolutionFields.reserve(static_cast<int>(dividendIndicators.size()));
    std::vector<std::string> batchFields;
    batchFields.reserve(static_cast<size_t>(dividendIndicators.size()));
    std::unordered_set<std::string> seenBatchFields;
    for (const DividendIndicatorSpec& indicator : dividendIndicators) {
        dateResolutionFields.append(dividendMetricFieldName(indicator));
        const std::string fieldName = dividendMetricFieldName(indicator).toStdString();
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
            effectiveContext.date = runtime.effectiveDate.toStdString();

            const std::vector<std::string> symbols = effectiveSymbols(effectiveContext);
            for (const std::string& fieldName : batchFields) {
                if (!context.historicalView->hasField(fieldName)) {
                    qWarning() << "ConfigurableFactorBase(dividend): HistoricalView 缺少红利字段"
                               << "effectiveDate=" << runtime.effectiveDate
                               << "field=" << QString::fromStdString(fieldName)
                               << "symbolCount=" << static_cast<qsizetype>(symbols.size());
                    const QString errorMessage = QStringLiteral("红利因子 HistoricalView 回测缺少字段 %1")
                        .arg(QString::fromStdString(fieldName));
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    return;
                }
            }

            std::unordered_map<std::string, std::unordered_map<std::string, double>> batchCrossSections;
            if (context.historicalView && !batchFields.empty()) {
                batchCrossSections = context.historicalView->getBatchCrossSections(runtime.effectiveDate.toStdString(), symbols, batchFields);
                if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
                    for (const auto& [fieldName, symbolValues] : batchCrossSections) {
                        std::string batchKey;
                        buildBatchCrossSectionKey(batchKey, runtime.effectiveDate.toStdString(), QString::fromStdString(fieldName));
                        activeBatchComputationCache->crossSectionsByKey[batchKey] = symbolValues;
                    }
                }
            }

            bool hasAnyMetricData = false;
            QStringList fieldSampleSummaries;
            fieldSampleSummaries.reserve(static_cast<qsizetype>(dividendIndicators.size()));
            for (const DividendIndicatorSpec& indicator : dividendIndicators) {
                const QString fieldNameText = dividendMetricFieldName(indicator);
                const std::string fieldName = fieldNameText.toStdString();
                const auto fieldIt = batchCrossSections.find(fieldName);
                const std::unordered_map<std::string, double> metricMap = fieldIt != batchCrossSections.end() ? fieldIt->second : std::unordered_map<std::string, double>{};
                if (!metricMap.empty()) {
                    hasAnyMetricData = true;
                }
                fieldSampleSummaries.append(QStringLiteral("%1:%2")
                                                .arg(fieldNameText)
                                                .arg(static_cast<qsizetype>(metricMap.size())));
                batchCrossSections[fieldName] = metricMap;
            }

            if (!hasAnyMetricData) {
                qDebug() << "ConfigurableFactorBase(dividend): 红利字段存在但当日无截面样本"
                         << "effectiveDate=" << runtime.effectiveDate
                         << "symbolCount=" << static_cast<qsizetype>(symbols.size())
                         << "fieldSampleCounts=" << fieldSampleSummaries.join(QStringLiteral(", "));
                result.metadata.set("emptyReason", json_helper::toJsonValue("红利因子字段存在但当天没有可用分红数据"));
                return;
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
                qDebug() << "ConfigurableFactorBase(dividend): 红利字段有样本但未产出最终结果"
                         << "effectiveDate=" << runtime.effectiveDate
                         << "symbolCount=" << static_cast<qsizetype>(symbols.size())
                         << "minDividendYield=" << normalizeDividendYieldFloor(dividend.minDividendYield)
                         << "fieldSampleCounts=" << fieldSampleSummaries.join(QStringLiteral(", "));
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
        QStringLiteral("使用红利字段"));
}

} // namespace factor
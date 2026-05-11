#include "domain/factor/include/ValueFactor.h"

#include "domain/factor/include/FactorInstanceManager.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <algorithm>
#include <cmath>

namespace factor {

namespace {

QString normalizedMetric(const std::string& metric)
{
    const QString normalized = QString::fromStdString(metric).trimmed().toLower();
    if (normalized == QStringLiteral("bp")) {
        return QStringLiteral("bp");
    }
    if (normalized == QStringLiteral("ep")) {
        return QStringLiteral("ep");
    }
    if (normalized == QStringLiteral("cf_p")) {
        return QStringLiteral("cf_p");
    }
    if (normalized == QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD)) {
        return QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD);
    }
    return {};
}

QString metricField(const QString& metric)
{
    if (metric == QStringLiteral("bp")) {
        return QString(factor::bridge::MarketBarFieldKeys::PB_RATIO);
    }
    if (metric == QStringLiteral("ep")) {
        return QString(factor::bridge::MarketBarFieldKeys::PE_RATIO);
    }
    if (metric == QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD)) {
        return QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD);
    }
    return {};
}

QStringList selectedMetricsFromParams(const ValueFactor::Params& params)
{
    QStringList metrics;
    for (const auto& rawMetric : params.valuationMetrics) {
        const QString metric = normalizedMetric(rawMetric);
        if (!metric.isEmpty() && !metrics.contains(metric)) {
            metrics.append(metric);
        }
    }
    return metrics;
}

double metricWeight(const ValueFactor::Params& params, const QString& metric)
{
    if (metric == QStringLiteral("bp")) {
        return params.bpWeight;
    }
    if (metric == QStringLiteral("ep")) {
        return params.epWeight;
    }
    if (metric == QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD)) {
        return params.dividendYieldWeight;
    }
    if (metric == QStringLiteral("cf_p")) {
        return params.cfPWeight;
    }
    return 0.0;
}

double scoreFromMetricRawValue(const QString& metric, double rawValue)
{
    if (metric == QStringLiteral("bp") || metric == QStringLiteral("ep")) {
        return 1.0 / rawValue;
    }
    if (metric == QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD) || metric == QStringLiteral("cf_p")) {
        return rawValue;
    }
    return 0.0;
}

} // namespace

ValueFactor::ValueFactor()
{
    factorType_ = "价值因子";
}

CalculationResult ValueFactor::calculate(const CalculationContext& context)
{
    const CommonFactorParams commonParams{
        params_.lookbackPeriod,
        params_.laggedEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled};

    QStringList dateResolutionFields;
    for (const QString& metric : selectedMetricsFromParams(params_)) {
        if (metric == QStringLiteral("cf_p")) {
            if (!dateResolutionFields.contains(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP))) {
                dateResolutionFields.append(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP));
            }
            if (!dateResolutionFields.contains(QString(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW))) {
                dateResolutionFields.append(QString(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW));
            }
            continue;
        }

        const QString field = metricField(metric);
        if (!field.isEmpty() && !dateResolutionFields.contains(field)) {
            dateResolutionFields.append(field);
        }
    }

    return executeWithCommonParams(
        context,
        commonParams,
        dateResolutionFields,
        [this, &context](const CommonFactorRuntimeState& runtime, CalculationResult& result) {
            const QStringList selectedMetrics = selectedMetricsFromParams(params_);
            if (selectedMetrics.isEmpty()) {
                const QString errorMessage = QString::fromUtf8("价值因子未配置有效的 valuationMetrics");
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                return;
            }

            struct MetricContribution {
                double weight{0.0};
                std::unordered_map<std::string, double> scores;
                int rawSampleCount{0};
                int invalidSampleCount{0};
            };

            std::vector<MetricContribution> metricContributions;
            metricContributions.reserve(static_cast<size_t>(selectedMetrics.size()));

            for (const QString& metric : selectedMetrics) {
                const double weight = metricWeight(params_, metric);
                if (weight <= 0.0) {
                    continue;
                }

                MetricContribution contribution;
                contribution.weight = weight;

                if (metric == QStringLiteral("cf_p")) {
                    if (!context.historicalView->hasField("market_cap") || !context.historicalView->hasField("operating_cash_flow")) {
                        const QString errorMessage = QString::fromUtf8("缓存数据集缺少 market_cap 或 operating_cash_flow 字段，无法计算价值因子CF/P");
                        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                        return;
                    }

                    const auto marketCaps = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), "market_cap", context.symbols);
                    const auto cashFlows = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), "operating_cash_flow", context.symbols);
                    for (const auto& [symbol, marketCap] : marketCaps) {
                        const auto cashFlowIt = cashFlows.find(symbol);
                        if (cashFlowIt == cashFlows.end()) {
                            continue;
                        }
                        ++contribution.rawSampleCount;
                        const double operatingCashFlow = cashFlowIt->second;
                        if (marketCap <= 0.0 || operatingCashFlow <= 0.0) {
                            ++contribution.invalidSampleCount;
                            continue;
                        }
                        contribution.scores[symbol] = scoreFromMetricRawValue(metric, operatingCashFlow / marketCap);
                    }
                } else {
                    const QString field = metricField(metric);
                    if (field.isEmpty() || !context.historicalView->hasField(field.toStdString())) {
                        const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算价值因子").arg(field.isEmpty() ? metric : field);
                        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                        return;
                    }

                    const auto crossSection = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), field.toStdString(), context.symbols);
                    for (const auto& [symbol, rawValue] : crossSection) {
                        ++contribution.rawSampleCount;
                        if (rawValue <= 0.0) {
                            ++contribution.invalidSampleCount;
                            continue;
                        }
                        contribution.scores[symbol] = scoreFromMetricRawValue(metric, rawValue);
                    }
                }

                metricContributions.push_back(std::move(contribution));
            }

            if (metricContributions.empty()) {
                result.dataStatus = CalculationResult::createError("价值因子没有可用的指标权重配置").dataStatus;
                result.metadata.set("error", json_helper::toJsonValue("价值因子没有可用的指标权重配置"));
                return;
            }

            std::unordered_map<std::string, double> weightedScores;
            std::unordered_map<std::string, double> usedWeights;
            int totalRawSampleCount = 0;
            int totalInvalidSampleCount = 0;
            for (const auto& contribution : metricContributions) {
                totalRawSampleCount += contribution.rawSampleCount;
                totalInvalidSampleCount += contribution.invalidSampleCount;
                for (const auto& [symbol, score] : contribution.scores) {
                    weightedScores[symbol] += score * contribution.weight;
                    usedWeights[symbol] += contribution.weight;
                }
            }

            for (const auto& [symbol, weightedScore] : weightedScores) {
                const auto weightIt = usedWeights.find(symbol);
                if (weightIt == usedWeights.end() || weightIt->second <= 0.0) {
                    continue;
                }
                result.values[symbol] = weightedScore / weightIt->second;
            }

            if (result.values.empty()) {
                const QString emptyReason = totalRawSampleCount == 0
                    ? QString::fromUtf8("当前价值因子没有可用的指标样本")
                    : QString::fromUtf8("当前价值因子的多指标样本全部无效或非正数");
                result.metadata.set("emptyReason", json_helper::toJsonValue(emptyReason.toStdString()));
                result.metadata.set("rawSampleCount", json_helper::toJsonValue(totalRawSampleCount));
                result.metadata.set("nonPositiveSampleCount", json_helper::toJsonValue(totalInvalidSampleCount));
                return;
            }
        },
        [](const CommonFactorRuntimeState&, CalculationResult& result) {
            std::vector<double> finiteValues;
            finiteValues.reserve(result.values.size());
            for (const auto& entry : result.values) {
                if (std::isfinite(entry.second)) {
                    finiteValues.push_back(entry.second);
                }
            }
            if (finiteValues.size() >= 16) {
                const double lower = calculatePercentileValue(finiteValues, 0.05);
                const double upper = calculatePercentileValue(finiteValues, 0.95);
                if (upper > lower) {
                    for (auto& entry : result.values) {
                        entry.second = (std::max)(lower, (std::min)(upper, entry.second));
                    }
                }
            }
        },
        [this](const CommonFactorRuntimeState&, CalculationResult& result) {
            const QStringList selectedMetrics = selectedMetricsFromParams(params_);
            auto valuationMetricsJson = foundation::json::JsonFacade::createArray();
            auto valuationWeightsJson = foundation::json::JsonFacade::createArray();
            for (const QString& metric : selectedMetrics) {
                valuationMetricsJson.push_back(json_helper::toJsonValue(metric.toStdString()));
                valuationWeightsJson.push_back(json_helper::toJsonValue(metricWeight(params_, metric)));
            }
            result.metadata.set("valuationMetrics", valuationMetricsJson);
            result.metadata.set("valuationWeights", valuationWeightsJson);
            if (!selectedMetrics.isEmpty()) {
                result.metadata.set("valuationMetric", json_helper::toJsonValue(selectedMetrics.front().toStdString()));
            }
        });
}

DataRequirements ValueFactor::getDataRequirements() const
{
    DataRequirements req;
    const auto appendUnique = [&req](const std::string& field) {
        if (std::find(req.requiredFields.begin(), req.requiredFields.end(), field) == req.requiredFields.end()) {
            req.requiredFields.push_back(field);
        }
    };

    for (const auto& rawMetric : params_.valuationMetrics) {
        const QString metric = normalizedMetric(rawMetric);
        if (metric == QStringLiteral("bp")) {
            appendUnique(QString(factor::bridge::MarketBarFieldKeys::PB_RATIO).toStdString());
        } else if (metric == QStringLiteral("ep")) {
            appendUnique(QString(factor::bridge::MarketBarFieldKeys::PE_RATIO).toStdString());
        } else if (metric == QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD)) {
            appendUnique(QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD).toStdString());
        } else if (metric == QStringLiteral("cf_p")) {
            appendUnique(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP).toStdString());
            appendUnique(QString(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW).toStdString());
        }
    }

    if (params_.neutralizationEnabled) {
        appendUnique("industry_code");
        appendUnique("market_cap");
    }
    return req;
}

BoundaryRules ValueFactor::getBoundaryRules() const
{
    BoundaryRules rules;
    rules.minDataPoints = 1;
    rules.handleOutliers = "winsorize_3sigma";
    return rules;
}

std::shared_ptr<ValueFactor> ValueFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<ValueFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

void ValueFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"));
    }
    dataRequirements_.requiredFields = getDataRequirements().requiredFields;
}

} // namespace factor
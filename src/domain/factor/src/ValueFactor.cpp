#include "domain/factor/include/ValueFactor.h"

#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/factor_enums.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <algorithm>
#include <cmath>

namespace factor {

namespace {

struct ValuationMetricInfo {
    ValuationMetric metric;
    const char* name;
};

constexpr ValuationMetricInfo kValuationMetricInfos[] = {
    {ValuationMetric::BP, "bp"},
    {ValuationMetric::EP, "ep"},
    {ValuationMetric::DIVIDEND_YIELD, "dividend_yield"},
    {ValuationMetric::CFP, "cf_p"}
};

} // namespace

void ValueFactor::appendUniqueField(QStringList& fields, const QString& field)
{
    if (!field.isEmpty() && !fields.contains(field)) {
        fields.append(field);
    }
}

double ValueFactor::calculatePercentileValueLocal(std::vector<double> values, double quantile)
{
    if (values.empty()) {
        return 0.0;
    }

    quantile = (std::max)(0.0, (std::min)(1.0, quantile));
    const double position = quantile * static_cast<double>(values.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));

    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(lower), values.end());
    const double lowValue = values[lower];
    if (upper == lower) {
        return lowValue;
    }

    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(upper), values.end());
    const double highValue = values[upper];
    return lowValue + (highValue - lowValue) * (position - static_cast<double>(lower));
}

ValuationMetric ValueFactor::valuationMetricFromString(const std::string& rawMetric)
{
    const QString normalized = QString::fromStdString(rawMetric).trimmed().toLower();
    if (normalized.isEmpty()) {
        return ValuationMetric::UNKNOWN;
    }

    for (const ValuationMetricInfo& info : kValuationMetricInfos) {
        if (normalized == QLatin1String(info.name)) {
            return info.metric;
        }
    }

    return ValuationMetric::UNKNOWN;
}

QString ValueFactor::valuationMetricToString(ValuationMetric metric)
{
    switch (metric) {
    case ValuationMetric::BP:
        return QStringLiteral("bp");
    case ValuationMetric::EP:
        return QStringLiteral("ep");
    case ValuationMetric::DIVIDEND_YIELD:
        return QStringLiteral("dividend_yield");
    case ValuationMetric::CFP:
        return QStringLiteral("cf_p");
    default:
        return {};
    }
}

QString ValueFactor::valuationMetricField(ValuationMetric metric)
{
    switch (metric) {
    case ValuationMetric::BP:
        return QString(factor::bridge::MarketBarFieldKeys::PB_RATIO);
    case ValuationMetric::EP:
        return QString(factor::bridge::MarketBarFieldKeys::PE_RATIO);
    case ValuationMetric::DIVIDEND_YIELD:
        return QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD);
    default:
        return {};
    }
}

std::vector<ValuationMetric> ValueFactor::selectedMetricsFromParams(const ValueFactor::Params& params)
{
    std::vector<ValuationMetric> metrics;
    metrics.reserve(params.valuationMetrics.size());

    for (const std::string& rawMetric : params.valuationMetrics) {
        const ValuationMetric metric = valuationMetricFromString(rawMetric);
        if (metric == ValuationMetric::UNKNOWN) {
            continue;
        }
        if (std::find(metrics.begin(), metrics.end(), metric) == metrics.end()) {
            metrics.push_back(metric);
        }
    }

    return metrics;
}

double ValueFactor::valuationMetricWeight(const ValueFactor::Params& params, ValuationMetric metric)
{
    switch (metric) {
    case ValuationMetric::BP:
        return params.bpWeight;
    case ValuationMetric::EP:
        return params.epWeight;
    case ValuationMetric::DIVIDEND_YIELD:
        return params.dividendYieldWeight;
    case ValuationMetric::CFP:
        return params.cfPWeight;
    default:
        return 0.0;
    }
}

double ValueFactor::scoreFromMetricRawValue(ValuationMetric metric, double rawValue)
{
    switch (metric) {
    case ValuationMetric::BP:
    case ValuationMetric::EP:
        return 1.0 / rawValue;
    case ValuationMetric::DIVIDEND_YIELD:
    case ValuationMetric::CFP:
        return rawValue;
    default:
        return 0.0;
    }
}

QStringList ValueFactor::collectDateResolutionFields(const std::vector<ValuationMetric>& metrics)
{
    QStringList fields;
    for (const ValuationMetric metric : metrics) {
        if (metric == ValuationMetric::CFP) {
            appendUniqueField(fields, QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP));
            appendUniqueField(fields, QString(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW));
            continue;
        }

        appendUniqueField(fields, valuationMetricField(metric));
    }
    return fields;
}

ValueFactor::MetricContribution ValueFactor::computeCFPContribution(const CalculationContext& context,
                                                              const CommonFactorRuntimeState& runtime,
                                                              double weight)
{
    MetricContribution contribution;
    contribution.weight = weight;

    const auto marketCaps = context.historicalView->getCrossSection(
        runtime.effectiveDate.toStdString(),
        QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP).toStdString(),
        context.symbols);
    const auto cashFlows = context.historicalView->getCrossSection(
        runtime.effectiveDate.toStdString(),
        QString(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW).toStdString(),
        context.symbols);

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

        contribution.scores[symbol] = scoreFromMetricRawValue(
            ValuationMetric::CFP,
            operatingCashFlow / marketCap);
    }

    return contribution;
}

ValueFactor::MetricContribution ValueFactor::computeStandardContribution(const CalculationContext& context,
                                                                     const CommonFactorRuntimeState& runtime,
                                                                     ValuationMetric metric,
                                                                     double weight)
{
    MetricContribution contribution;
    contribution.weight = weight;

    const QString field = valuationMetricField(metric);
    const auto crossSection = context.historicalView->getCrossSection(
        runtime.effectiveDate.toStdString(),
        field.toStdString(),
        context.symbols);

    for (const auto& [symbol, rawValue] : crossSection) {
        ++contribution.rawSampleCount;
        if (rawValue <= 0.0) {
            ++contribution.invalidSampleCount;
            continue;
        }

        contribution.scores[symbol] = scoreFromMetricRawValue(metric, rawValue);
    }

    return contribution;
}

void ValueFactor::winsorizeTopBottom5Percent(std::unordered_map<std::string, double>& values)
{
    std::vector<double> finiteValues;
    finiteValues.reserve(values.size());
    for (const auto& entry : values) {
        if (std::isfinite(entry.second)) {
            finiteValues.push_back(entry.second);
        }
    }

    if (finiteValues.size() < 16) {
        return;
    }

    const double lower = calculatePercentileValueLocal(finiteValues, 0.05);
    const double upper = calculatePercentileValueLocal(finiteValues, 0.95);
    if (upper <= lower) {
        return;
    }

    for (auto& entry : values) {
        entry.second = (std::max)(lower, (std::min)(upper, entry.second));
    }
}

ValueFactor::ValueFactor()
{
    factorType_ = FactorType::VALUE;
}

std::shared_ptr<ValueFactor> ValueFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
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

DataRequirements ValueFactor::getDataRequirements() const
{
    DataRequirements req;
    const auto appendUnique = [&req](const std::string& field) {
        if (std::find(req.requiredFields.begin(), req.requiredFields.end(), field) == req.requiredFields.end()) {
            req.requiredFields.push_back(field);
        }
    };

    for (const std::string& rawMetric : params_.valuationMetrics) {
        switch (valuationMetricFromString(rawMetric)) {
        case ValuationMetric::BP:
            appendUnique(QString(factor::bridge::MarketBarFieldKeys::PB_RATIO).toStdString());
            break;
        case ValuationMetric::EP:
            appendUnique(QString(factor::bridge::MarketBarFieldKeys::PE_RATIO).toStdString());
            break;
        case ValuationMetric::DIVIDEND_YIELD:
            appendUnique(QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD).toStdString());
            break;
        case ValuationMetric::CFP:
            appendUnique(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP).toStdString());
            appendUnique(QString(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW).toStdString());
            break;
        default:
            break;
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

CalculationResult ValueFactor::calculate(const CalculationContext& context)
{
    const CommonFactorParams commonParams{
        params_.lookbackPeriod,
        params_.laggedEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled};

    const std::vector<ValuationMetric> metrics = selectedMetricsFromParams(params_);
    const QStringList dateResolutionFields = collectDateResolutionFields(metrics);

    return executeWithCommonParams(
        context,
        commonParams,
        dateResolutionFields,
        [this, &context, metrics](const CommonFactorRuntimeState& runtime, CalculationResult& result) {
            if (metrics.empty()) {
                const std::string errorMessage = "价值因子未配置有效的 valuationMetrics";
                result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                return;
            }

            std::vector<MetricContribution> contributions;
            contributions.reserve(metrics.size());

            for (const ValuationMetric metric : metrics) {
                const double weight = valuationMetricWeight(params_, metric);
                if (weight <= 0.0) {
                    continue;
                }

                if (metric == ValuationMetric::CFP) {
                    if (!context.historicalView->hasField("market_cap") || !context.historicalView->hasField("operating_cash_flow")) {
                        const std::string errorMessage = "缓存数据集缺少 market_cap 或 operating_cash_flow 字段，无法计算价值因子CF/P";
                        result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                        result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                        return;
                    }
                    contributions.push_back(computeCFPContribution(context, runtime, weight));
                } else {
                    const QString field = valuationMetricField(metric);
                    if (field.isEmpty() || !context.historicalView->hasField(field.toStdString())) {
                        const std::string errorMessage = "缓存数据集缺少字段 "
                            + (field.isEmpty() ? valuationMetricToString(metric).toStdString() : field.toStdString())
                            + "，无法计算价值因子";
                        result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                        result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                        return;
                    }
                    contributions.push_back(computeStandardContribution(context, runtime, metric, weight));
                }
            }

            if (contributions.empty()) {
                const std::string errorMessage = "价值因子没有可用的指标权重配置";
                result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                return;
            }

            std::unordered_map<std::string, double> weightedScores;
            std::unordered_map<std::string, double> usedWeights;
            int totalRawSampleCount = 0;
            int totalInvalidSampleCount = 0;

            for (const MetricContribution& contribution : contributions) {
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
                const std::string emptyReason = totalRawSampleCount == 0
                    ? "当前价值因子没有可用的指标样本"
                    : "当前价值因子的多指标样本全部无效或非正数";
                result.metadata.set("emptyReason", json_helper::toJsonValue(emptyReason));
                result.metadata.set("rawSampleCount", json_helper::toJsonValue(totalRawSampleCount));
                result.metadata.set("nonPositiveSampleCount", json_helper::toJsonValue(totalInvalidSampleCount));
                return;
            }
        },
        [](const CommonFactorRuntimeState&, CalculationResult& result) {
            winsorizeTopBottom5Percent(result.values);
        },
        [this, metrics](const CommonFactorRuntimeState&, CalculationResult& result) {
            auto valuationMetricsJson = foundation::json::JsonFacade::createArray();
            auto valuationWeightsJson = foundation::json::JsonFacade::createArray();

            for (const ValuationMetric metric : metrics) {
                valuationMetricsJson.push_back(json_helper::toJsonValue(valuationMetricToString(metric).toStdString()));
                valuationWeightsJson.push_back(json_helper::toJsonValue(valuationMetricWeight(params_, metric)));
            }

            result.metadata.set("valuationMetrics", valuationMetricsJson);
            result.metadata.set("valuationWeights", valuationWeightsJson);
            if (!metrics.empty()) {
                result.metadata.set("valuationMetric", json_helper::toJsonValue(valuationMetricToString(metrics.front()).toStdString()));
            }
        });
}

} // namespace factor
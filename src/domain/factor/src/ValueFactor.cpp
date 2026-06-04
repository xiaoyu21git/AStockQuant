#include "domain/factor/include/ValueFactor.h"

#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/factor_enums.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <algorithm>
#include <cmath>

namespace factor {

namespace {

namespace value_json {

constexpr const char* kValuationMetricsKey = "valuationMetrics";
constexpr const char* kBpWeightKey = "bpWeight";
constexpr const char* kEpWeightKey = "epWeight";
constexpr const char* kDividendYieldWeightKey = "dividendYieldWeight";
constexpr const char* kCfPWeightKey = "cfPWeight";

bool hasValuationMetrics(const foundation::json::JsonFacade& json)
{
    return json.has(kValuationMetricsKey);
}

foundation::json::JsonFacade valuationMetrics(const foundation::json::JsonFacade& json)
{
    return json.get(kValuationMetricsKey);
}

bool hasBpWeight(const foundation::json::JsonFacade& json)
{
    return json.has(kBpWeightKey);
}

double bpWeight(const foundation::json::JsonFacade& json)
{
    return json.get(kBpWeightKey).asDouble();
}

bool hasEpWeight(const foundation::json::JsonFacade& json)
{
    return json.has(kEpWeightKey);
}

double epWeight(const foundation::json::JsonFacade& json)
{
    return json.get(kEpWeightKey).asDouble();
}

bool hasDividendYieldWeight(const foundation::json::JsonFacade& json)
{
    return json.has(kDividendYieldWeightKey);
}

double dividendYieldWeight(const foundation::json::JsonFacade& json)
{
    return json.get(kDividendYieldWeightKey).asDouble();
}

bool hasCfPWeight(const foundation::json::JsonFacade& json)
{
    return json.has(kCfPWeightKey);
}

double cfPWeight(const foundation::json::JsonFacade& json)
{
    return json.get(kCfPWeightKey).asDouble();
}

} // namespace value_json

struct ValuationMetricDescriptor {
    ValuationMetric metric;
    const char* jsonName;
};

constexpr ValuationMetricDescriptor kValuationMetricDescriptors[] = {
    {ValuationMetric::BP, "bp"},
    {ValuationMetric::EP, "ep"},
    {ValuationMetric::DIVIDEND_YIELD, "dividend_yield"},
    {ValuationMetric::CFP, "cf_p"}
};

std::string valuationMetricToJsonString(ValuationMetric metric)
{
    for (const auto& descriptor : kValuationMetricDescriptors) {
        if (descriptor.metric == metric) {
            return descriptor.jsonName;
        }
    }
    return "";
}

ValuationMetric valuationMetricFromJsonValue(const foundation::json::JsonFacade& value)
{
    return requireNumericEnumValue<ValuationMetric>(
        value,
        value_json::kValuationMetricsKey,
        static_cast<int>(ValuationMetric::BP),
        static_cast<int>(ValuationMetric::CFP));
}

DataFrequency dataFrequencyFromJsonValue(const foundation::json::JsonFacade& value,
                                         const char* fieldName)
{
    return requireNumericEnumValue<DataFrequency>(
        value,
        fieldName,
        static_cast<int>(DataFrequency::Daily),
        static_cast<int>(DataFrequency::Yearly));
}

StandardizationMethod standardizationFromJsonValue(const foundation::json::JsonFacade& value,
                                                   const char* fieldName)
{
    return requireNumericEnumValue<StandardizationMethod>(
        value,
        fieldName,
        static_cast<int>(StandardizationMethod::None),
        static_cast<int>(StandardizationMethod::Percentile));
}

ValueFactor::Params valueParamsFromJson(const foundation::json::JsonFacade& json)
{
    ValueFactor::Params params;
    params.fromJson(json);

    params.valuationMetrics.clear();
    if (value_json::hasValuationMetrics(json)) {
        const auto metrics = value_json::valuationMetrics(json);
        if (metrics.isArray() && metrics.size() > 0) {
            for (size_t index = 0; index < metrics.size(); ++index) {
                const ValuationMetric metric = valuationMetricFromJsonValue(metrics.at(index));
                if (std::find(params.valuationMetrics.begin(), params.valuationMetrics.end(), metric) == params.valuationMetrics.end()) {
                    params.valuationMetrics.push_back(metric);
                }
            }
        } else {
            throw std::runtime_error("valuationMetrics 不是枚举数组字段");
        }
    }
    if (params.valuationMetrics.empty()) {
        params.valuationMetrics = {ValuationMetric::BP, ValuationMetric::EP};
    }
    if (value_json::hasBpWeight(json)) params.bpWeight = value_json::bpWeight(json);
    if (value_json::hasEpWeight(json)) params.epWeight = value_json::epWeight(json);
    if (value_json::hasDividendYieldWeight(json)) params.dividendYieldWeight = value_json::dividendYieldWeight(json);
    if (value_json::hasCfPWeight(json)) params.cfPWeight = value_json::cfPWeight(json);
    return params;
}

}

void ValueFactor::appendUniqueField(std::vector<std::string>& fields, const std::string& field)
{
    if (field.empty()) {
        return;
    }
    if (std::find(fields.begin(), fields.end(), field) == fields.end()) {
        fields.push_back(field);
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

std::string ValueFactor::valuationMetricField(ValuationMetric metric)
{
    switch (metric) {
    case ValuationMetric::BP:
        return std::string(factor::bridge::MarketBarFieldKeys::PB_RATIO.c_str());
    case ValuationMetric::EP:
        return std::string(factor::bridge::MarketBarFieldKeys::PE_RATIO.c_str());
    case ValuationMetric::DIVIDEND_YIELD:
        return std::string(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD.c_str());
    default:
        return "";
    }
}

std::vector<ValuationMetric> ValueFactor::selectedMetricsFromParams(const ValueFactor::Params& params)
{
    std::vector<ValuationMetric> metrics;
    metrics.reserve(params.valuationMetrics.size());

    for (const ValuationMetric metric : params.valuationMetrics) {
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

std::vector<std::string> ValueFactor::collectDateResolutionFields(const std::vector<ValuationMetric>& metrics)
{
    std::vector<std::string> fields;
    for (const ValuationMetric metric : metrics) {
        if (metric == ValuationMetric::CFP) {
            appendUniqueField(fields, std::string(factor::bridge::MarketBarFieldKeys::MARKET_CAP.c_str()));
            appendUniqueField(fields, std::string(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW.c_str()));
            continue;
        }

        appendUniqueField(fields, valuationMetricField(metric));
    }
    return fields;
}

ValueFactor::MetricContribution ValueFactor::computeCFPContribution(const CalculationContext& context,
                                                              const CommonRuntimeState& runtime,
                                                              double weight)
{
    MetricContribution contribution;
    contribution.weight = weight;

    const auto marketCaps = context.historicalView->getCrossSection(
        runtime.effectiveDate,
        std::string(factor::bridge::MarketBarFieldKeys::MARKET_CAP.c_str()),
        context.symbols);
    const auto cashFlows = context.historicalView->getCrossSection(
        runtime.effectiveDate,
        std::string(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW.c_str()),
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
                                                                     const CommonRuntimeState& runtime,
                                                                     ValuationMetric metric,
                                                                     double weight)
{
    MetricContribution contribution;
    contribution.weight = weight;

    const std::string field = valuationMetricField(metric);
    const auto crossSection = context.historicalView->getCrossSection(
        runtime.effectiveDate,
        field,
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
    if (config::hasCalculationConfig(config)) {
        params_ = valueParamsFromJson(config::calculationConfig(config));
    }
    dataRequirements_ = getDataRequirements();
}

DataRequirements ValueFactor::getDataRequirements() const
{
    DataRequirements req;
    for (const ValuationMetric metric : params_.valuationMetrics) {
        switch (metric) {
        case ValuationMetric::BP:
            appendRequiredField(req, std::string(factor::bridge::MarketBarFieldKeys::PB_RATIO.c_str()));
            break;
        case ValuationMetric::EP:
            appendRequiredField(req, std::string(factor::bridge::MarketBarFieldKeys::PE_RATIO.c_str()));
            break;
        case ValuationMetric::DIVIDEND_YIELD:
            appendRequiredField(req, std::string(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD.c_str()));
            break;
        case ValuationMetric::CFP:
            appendRequiredField(req, std::string(factor::bridge::MarketBarFieldKeys::MARKET_CAP.c_str()));
            appendRequiredField(req, std::string(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW.c_str()));
            break;
        default:
            break;
        }
    }

    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules ValueFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

CalculationResult ValueFactor::calculate(const CalculationContext& context)
{
    const CommonMetricParams commonParams = buildCommonMetricParams(
        params_.lookbackWindow,
        params_.lagEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled);

    const std::vector<ValuationMetric> metrics = selectedMetricsFromParams(params_);
    const std::vector<std::string> dateResolutionFields = collectDateResolutionFields(metrics);

    return executeWithCommonParams(
        context,
        commonParams,
        [this, &context, &commonParams, &dateResolutionFields]() {
            return resolveCommonEffectiveDateForFields(
                context,
                commonParams,
                dateResolutionFields,
                CommonFieldRequirementMode::AllFields);
        },
        [this, &context, metrics](const CommonRuntimeState& runtime, CalculationResult& result) {
            if (metrics.empty()) {
                const std::string errorMessage = "价值因子未配置有效的 valuationMetrics";
                result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                return;
            }

            std::vector<MetricContribution> contributions;
            contributions.reserve(metrics.size());

            qDebug() << "[ValueFactor] 指标数=" << metrics.size()
                     << " effectiveDate=" << QString::fromStdString(runtime.effectiveDate);
            for (const ValuationMetric metric : metrics) {
                const double weight = valuationMetricWeight(params_, metric);
                qDebug() << "[ValueFactor] 指标=" << (int)metric << " weight=" << weight;
                if (weight <= 0.0) {
                    qDebug() << "[ValueFactor] 跳过: weight<=0";
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
                    const std::string field = valuationMetricField(metric);
                    qDebug() << "[ValueFactor]   field=" << QString::fromStdString(field)
                             << " hasField=" << context.historicalView->hasField(field);
                    if (field.empty() || !context.historicalView->hasField(field)) {
                        const std::string errorMessage = "缓存数据集缺少字段 "
                            + (field.empty() ? valuationMetricToJsonString(metric) : field)
                            + "，无法计算价值因子";
                        result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                        result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                        return;
                    }
                    auto contrib = computeStandardContribution(context, runtime, metric, weight);
                    qDebug() << "[ValueFactor]   贡献: scores=" << contrib.scores.size()
                             << " rawSample=" << contrib.rawSampleCount
                             << " invalid=" << contrib.invalidSampleCount;
                    contributions.push_back(std::move(contrib));
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
        [](const CommonRuntimeState&, CalculationResult& result) {
            winsorizeTopBottom5Percent(result.values);
        },
        [this, metrics](const CommonRuntimeState&, CalculationResult& result) {
            auto valuationMetricsJson = foundation::json::JsonFacade::createArray();
            auto valuationWeightsJson = foundation::json::JsonFacade::createArray();

            for (const ValuationMetric metric : metrics) {
                valuationMetricsJson.push_back(json_helper::toJsonValue(valuationMetricToJsonString(metric)));
                valuationWeightsJson.push_back(json_helper::toJsonValue(valuationMetricWeight(params_, metric)));
            }

            result.metadata.set("valuationMetrics", valuationMetricsJson);
            result.metadata.set("valuationWeights", valuationWeightsJson);
            if (!metrics.empty()) {
                result.metadata.set("valuationMetric",
                                    json_helper::toJsonValue(valuationMetricToJsonString(metrics.front())));
            }
        });
}

} // namespace factor
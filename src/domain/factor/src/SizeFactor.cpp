#include "domain/factor/include/SizeFactor.h"
//#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace factor {

namespace {

namespace size_json {

constexpr const char* kSizeMetricKey = "sizeMetric";
constexpr const char* kLogTransformKey = "logTransform";

bool hasSizeMetric(const foundation::json::JsonFacade& json)
{
    return json.has(kSizeMetricKey);
}

bool hasLogTransform(const foundation::json::JsonFacade& json)
{
    return json.has(kLogTransformKey);
}

bool logTransform(const foundation::json::JsonFacade& json)
{
    return json.get(kLogTransformKey).asBool();
}

} // namespace size_json

struct SizeMetricDescriptor {
    SizeMetric metric;
    const char* jsonName;
};

constexpr SizeMetricDescriptor kSizeMetricDescriptors[] = {
    {SizeMetric::MARKET_CAP, "market_cap"},
    {SizeMetric::CIRCULATING_MARKET_CAP, "circulating_market_cap"},
    {SizeMetric::TOTAL_ASSETS, "total_assets"}
};

std::string sizeMetricToJsonString(SizeMetric metric)
{
    for (const auto& descriptor : kSizeMetricDescriptors) {
        if (descriptor.metric == metric) {
            return descriptor.jsonName;
        }
    }
    return "";
}

SizeFactor::Params sizeParamsFromJson(const foundation::json::JsonFacade& json)
{
    SizeFactor::Params params;
    params.fromJson(json);

    if (size_json::hasSizeMetric(json)) {
        params.sizeMetric = requireNumericEnumField<SizeMetric>(json, size_json::kSizeMetricKey, static_cast<int>(SizeMetric::MARKET_CAP), static_cast<int>(SizeMetric::TOTAL_ASSETS));
    }
    if (size_json::hasLogTransform(json)) {
        params.logTransform = size_json::logTransform(json);
    }
    return params;
}

}

SizeFactor::SizeFactor() {
    factorType_ = FactorType::SIZE;
}

CalculationResult SizeFactor::calculate(const CalculationContext& context) {
    const std::string column = selectedColumn();
    if (column.empty()) {
        CalculationResult result = CalculationResult::createError(
            std::string("当前运行时暂不支持计算规模因子指标 ") + sizeMetricToJsonString(params_.sizeMetric));
        result.date = context.date;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const CommonMetricParams commonParams = buildCommonMetricParams(
        params_.lookbackWindow,
        params_.lagEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled);

    return executeWithCommonParams(
        context,
        commonParams,
        [this, &context, &commonParams, column]() {
            return resolveCommonEffectiveDateForFields(
                context,
                commonParams,
                std::vector<std::string>{column},
                CommonFieldRequirementMode::AllFields);
        },
        [this, &context, column](const CommonRuntimeState& runtime, CalculationResult& result) {
            if (!context.historicalView->hasField(column)) {
                const std::string errorMessage = std::string("缓存数据集缺少字段 ") + column + "，无法计算规模因子";
                result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                return;
            }

            const auto crossSection = context.historicalView->getCrossSection(runtime.effectiveDate, column, context.symbols);
            for (const auto& [symbol, rawValue] : crossSection) {
                if (rawValue <= 0.0) {
                    continue;
                }
                result.values[symbol] = scoreFromRawValue(rawValue);
            }

            if (result.values.empty()) {
                result.metadata.set("emptyReason", json_helper::toJsonValue("规模因子字段存在但没有可用正数值"));
            }
        },
        [](const CommonRuntimeState&, CalculationResult& result) {
            std::vector<double> finiteValues;
            finiteValues.reserve(result.values.size());
            for (const auto& [symbol, value] : result.values) {
                (void)symbol;
                if (std::isfinite(value)) {
                    finiteValues.push_back(value);
                }
            }

            if (finiteValues.size() >= 16) {
                const double lower = BaseFactor::calculatePercentileValue(finiteValues, 0.05);
                const double upper = BaseFactor::calculatePercentileValue(finiteValues, 0.95);
                if (upper > lower) {
                    for (auto& [symbol, value] : result.values) {
                        (void)symbol;
                        value = (std::max)(lower, (std::min)(upper, value));
                    }
                }
            }
        },
        [this](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("sizeMetric", json_helper::toJsonValue(static_cast<int>(params_.sizeMetric)));
            result.metadata.set("logTransform", json_helper::toJsonValue(params_.logTransform));
            result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        });
}

DataRequirements SizeFactor::getDataRequirements() const {
    DataRequirements req;
    appendRequiredField(req, selectedColumn());
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules SizeFactor::getBoundaryRules() const {
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

std::shared_ptr<SizeFactor> SizeFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<SizeFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

std::string SizeFactor::selectedColumn() const {
    switch (params_.sizeMetric) {
    case SizeMetric::MARKET_CAP:
        return "market_cap";
    case SizeMetric::CIRCULATING_MARKET_CAP:
        return "circulating_market_cap";
    case SizeMetric::TOTAL_ASSETS:
        return "total_assets";
    default:
        return "";
    }
}

double SizeFactor::scoreFromRawValue(double rawValue) const {
    if (params_.logTransform) {
        return -std::log(rawValue);
    }
    return -rawValue;
}

void SizeFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config)) {
        params_ = sizeParamsFromJson(config::calculationConfig(config));
    }
    dataRequirements_ = getDataRequirements();
    boundaryRules_ = getBoundaryRules();
}

} // namespace factor
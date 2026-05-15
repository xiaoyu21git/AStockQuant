#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"

#include <algorithm>
#include <cmath>

namespace factor {

namespace {

namespace size_json {

constexpr const char* kSizeMetricKey = "sizeMetric";
constexpr const char* kLogTransformKey = "logTransform";
constexpr const char* kLookbackPeriodKey = "lookbackPeriod";
constexpr const char* kLaggedEnabledKey = "laggedEnabled";
constexpr const char* kFrequencyKey = "frequency";
constexpr const char* kStandardizationKey = "standardization";
constexpr const char* kNeutralizationEnabledKey = "neutralizationEnabled";

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

bool hasLookbackPeriod(const foundation::json::JsonFacade& json)
{
    return json.has(kLookbackPeriodKey);
}

int lookbackPeriod(const foundation::json::JsonFacade& json)
{
    return json.get(kLookbackPeriodKey).asInt();
}

bool hasLaggedEnabled(const foundation::json::JsonFacade& json)
{
    return json.has(kLaggedEnabledKey);
}

bool laggedEnabled(const foundation::json::JsonFacade& json)
{
    return json.get(kLaggedEnabledKey).asBool();
}

bool hasFrequency(const foundation::json::JsonFacade& json)
{
    return json.has(kFrequencyKey);
}

bool hasStandardization(const foundation::json::JsonFacade& json)
{
    return json.has(kStandardizationKey);
}

bool hasNeutralizationEnabled(const foundation::json::JsonFacade& json)
{
    return json.has(kNeutralizationEnabledKey);
}

bool neutralizationEnabled(const foundation::json::JsonFacade& json)
{
    return json.get(kNeutralizationEnabledKey).asBool();
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

QString sizeMetricToJsonString(SizeMetric metric)
{
    for (const auto& descriptor : kSizeMetricDescriptors) {
        if (descriptor.metric == metric) {
            return QLatin1String(descriptor.jsonName);
        }
    }
    return {};
}

SizeFactor::Params sizeParamsFromJson(const foundation::json::JsonFacade& json)
{
    SizeFactor::Params params;
    if (size_json::hasSizeMetric(json)) {
        params.sizeMetric = requireNumericEnumField<SizeMetric>(json, size_json::kSizeMetricKey, static_cast<int>(SizeMetric::MARKET_CAP), static_cast<int>(SizeMetric::TOTAL_ASSETS));
    }
    if (size_json::hasLogTransform(json)) {
        params.logTransform = size_json::logTransform(json);
    }
    if (size_json::hasLookbackPeriod(json)) {
        params.lookbackPeriod = size_json::lookbackPeriod(json);
    }
    if (size_json::hasLaggedEnabled(json)) {
        params.laggedEnabled = size_json::laggedEnabled(json);
    }
    if (size_json::hasFrequency(json)) {
        params.frequency = requireNumericEnumField<CommonFrequency>(json, size_json::kFrequencyKey, static_cast<int>(CommonFrequency::DAILY), static_cast<int>(CommonFrequency::ANNUAL));
    }
    if (size_json::hasStandardization(json)) {
        params.standardization = requireNumericEnumField<CommonStandardization>(json, size_json::kStandardizationKey, static_cast<int>(CommonStandardization::NONE), static_cast<int>(CommonStandardization::PERCENTILE));
    }
    if (size_json::hasNeutralizationEnabled(json)) {
        params.neutralizationEnabled = size_json::neutralizationEnabled(json);
    }
    return params;
}

}

SizeFactor::SizeFactor() {
    factorType_ = FactorType::SIZE;
}

CalculationResult SizeFactor::calculate(const CalculationContext& context) {
    const QString column = selectedColumn();
    if (column.isEmpty()) {
        CalculationResult result = CalculationResult::createError(
            QString("当前运行时暂不支持计算规模因子指标 %1").arg(sizeMetricToJsonString(params_.sizeMetric)).toStdString());
        result.date = context.date;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const CommonFactorParams commonParams{
        params_.lookbackPeriod,
        params_.laggedEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled};

    return executeWithCommonParams(
        context,
        commonParams,
        QStringList{column},
        [this, &context, column](const CommonFactorRuntimeState& runtime, CalculationResult& result) {
            if (!context.historicalView->hasField(column.toStdString())) {
                const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算规模因子").arg(column);
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                return;
            }

            const auto crossSection = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), column.toStdString(), context.symbols);
            for (const auto& [symbol, rawValue] : crossSection) {
                if (rawValue <= 0.0) {
                    continue;
                }
                result.values[symbol] = scoreFromRawValue(rawValue);
            }
        },
        [](const CommonFactorRuntimeState&, CalculationResult& result) {
            std::vector<double> finiteValues;
            finiteValues.reserve(result.values.size());
            for (const auto& [symbol, value] : result.values) {
                Q_UNUSED(symbol);
                if (std::isfinite(value)) {
                    finiteValues.push_back(value);
                }
            }

            if (finiteValues.size() >= 16) {
                const double lower = BaseFactor::calculatePercentileValue(finiteValues, 0.05);
                const double upper = BaseFactor::calculatePercentileValue(finiteValues, 0.95);
                if (upper > lower) {
                    for (auto& [symbol, value] : result.values) {
                        Q_UNUSED(symbol);
                        value = (std::max)(lower, (std::min)(upper, value));
                    }
                }
            }
        },
        [this](const CommonFactorRuntimeState&, CalculationResult& result) {
            result.metadata.set("sizeMetric", json_helper::toJsonValue(static_cast<int>(params_.sizeMetric)));
            result.metadata.set("logTransform", json_helper::toJsonValue(params_.logTransform));
            result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        });
}

DataRequirements SizeFactor::getDataRequirements() const {
    DataRequirements req;
    const auto appendUnique = [&req](const std::string& field) {
        if (field.empty()) {
            return;
        }
        if (std::find(req.requiredFields.begin(), req.requiredFields.end(), field) == req.requiredFields.end()) {
            req.requiredFields.push_back(field);
        }
    };

    appendUnique(selectedColumn().toStdString());
    if (params_.neutralizationEnabled) {
        appendUnique("industry_code");
        appendUnique("market_cap");
    }
    return req;
}

BoundaryRules SizeFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = 1;
    rules.handleOutliers = OutlierHandling::WINSORIZE_3SIGMA;
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

QString SizeFactor::selectedColumn() const {
    switch (params_.sizeMetric) {
    case SizeMetric::MARKET_CAP:
        return "market_cap";
    case SizeMetric::CIRCULATING_MARKET_CAP:
        return "circulating_market_cap";
    case SizeMetric::TOTAL_ASSETS:
        return "total_assets";
    default:
        return {};
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
}

} // namespace factor
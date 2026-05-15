#include "domain/factor/include/QualityFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <QString>

#include <unordered_set>

namespace factor {

namespace {

namespace quality_json {

constexpr const char* kMetricKey = "metric";
constexpr const char* kFrequencyKey = "frequency";
constexpr const char* kLookbackPeriodKey = "lookbackPeriod";
constexpr const char* kStandardizationKey = "standardization";
constexpr const char* kLaggedEnabledKey = "laggedEnabled";
constexpr const char* kNeutralizationEnabledKey = "neutralizationEnabled";
constexpr const char* kQualityThresholdKey = "qualityThreshold";

bool hasMetric(const foundation::json::JsonFacade& json)
{
    return json.has(kMetricKey);
}

bool hasFrequency(const foundation::json::JsonFacade& json)
{
    return json.has(kFrequencyKey);
}

bool hasLookbackPeriod(const foundation::json::JsonFacade& json)
{
    return json.has(kLookbackPeriodKey);
}

int lookbackPeriod(const foundation::json::JsonFacade& json)
{
    return json.get(kLookbackPeriodKey).asInt();
}

bool hasStandardization(const foundation::json::JsonFacade& json)
{
    return json.has(kStandardizationKey);
}

bool hasLaggedEnabled(const foundation::json::JsonFacade& json)
{
    return json.has(kLaggedEnabledKey);
}

bool laggedEnabled(const foundation::json::JsonFacade& json)
{
    return json.get(kLaggedEnabledKey).asBool();
}

bool hasNeutralizationEnabled(const foundation::json::JsonFacade& json)
{
    return json.has(kNeutralizationEnabledKey);
}

bool neutralizationEnabled(const foundation::json::JsonFacade& json)
{
    return json.get(kNeutralizationEnabledKey).asBool();
}

bool hasQualityThreshold(const foundation::json::JsonFacade& json)
{
    return json.has(kQualityThresholdKey);
}

double qualityThreshold(const foundation::json::JsonFacade& json)
{
    return json.get(kQualityThresholdKey).asDouble();
}

} // namespace quality_json

struct QualityMetricDescriptor {
    QualityMetric metric;
    const char* jsonName;
};

constexpr QualityMetricDescriptor kQualityMetricDescriptors[] = {
    {QualityMetric::ROE, "roe"},
    {QualityMetric::ROA, "roa"},
    {QualityMetric::GROSS_MARGIN, "gross_margin"},
    {QualityMetric::OPERATING_MARGIN, "operating_margin"},
    {QualityMetric::EARNINGS_QUALITY, "earnings_quality"}
};

QString qualityMetricToJsonString(QualityMetric metric)
{
    for (const auto& descriptor : kQualityMetricDescriptors) {
        if (descriptor.metric == metric) {
            return QLatin1String(descriptor.jsonName);
        }
    }
    return {};
}

QualityFactor::Params qualityParamsFromJson(const foundation::json::JsonFacade& json)
{
    QualityFactor::Params params;
    if (quality_json::hasMetric(json)) {
        params.metric = requireNumericEnumField<QualityMetric>(json, quality_json::kMetricKey, static_cast<int>(QualityMetric::ROE), static_cast<int>(QualityMetric::EARNINGS_QUALITY));
    }
    if (quality_json::hasFrequency(json)) params.frequency = requireNumericEnumField<CommonFrequency>(json, quality_json::kFrequencyKey, static_cast<int>(CommonFrequency::DAILY), static_cast<int>(CommonFrequency::ANNUAL));
    if (quality_json::hasLookbackPeriod(json)) params.lookbackPeriod = quality_json::lookbackPeriod(json);
    if (quality_json::hasStandardization(json)) params.standardization = requireNumericEnumField<CommonStandardization>(json, quality_json::kStandardizationKey, static_cast<int>(CommonStandardization::NONE), static_cast<int>(CommonStandardization::PERCENTILE));
    if (quality_json::hasLaggedEnabled(json)) params.laggedEnabled = quality_json::laggedEnabled(json);
    if (quality_json::hasNeutralizationEnabled(json)) params.neutralizationEnabled = quality_json::neutralizationEnabled(json);
    if (quality_json::hasQualityThreshold(json)) params.qualityThreshold = quality_json::qualityThreshold(json);
    return params;
}

QString resolveMetricColumn(QualityMetric metric)
{
    if (metric == QualityMetric::ROE) {
        return QString(factor::bridge::FinancialFieldKeys::ROE);
    }
    if (metric == QualityMetric::ROA) {
        return QString(factor::bridge::FinancialFieldKeys::ROA);
    }
    if (metric == QualityMetric::GROSS_MARGIN || metric == QualityMetric::OPERATING_MARGIN) {
        return QString(factor::bridge::FinancialFieldKeys::PROFIT_MARGIN);
    }
    return QString();
}

double normalizeThreshold(double threshold)
{
    return threshold > 1.0 ? threshold / 100.0 : threshold;
}

}

QualityFactor::QualityFactor() {
    factorType_ = FactorType::QUALITY;
}

CalculationResult QualityFactor::calculate(const CalculationContext& context) {
    const QualityMetric metric = params_.metric;
    const double qualityThreshold = normalizeThreshold(params_.qualityThreshold);

    QStringList requiredFields;
    if (metric == QualityMetric::EARNINGS_QUALITY) {
        requiredFields = {QString(factor::bridge::FinancialFieldKeys::NET_PROFIT), QString(factor::bridge::FinancialFieldKeys::EQUITY)};
    } else {
        const QString fieldName = resolveMetricColumn(metric);
        if (fieldName.isEmpty()) {
            auto result = CalculationResult::createError(
                QStringLiteral("质量因子 HistoricalView 回测不支持指标 %1").arg(qualityMetricToJsonString(metric)).toStdString());
            result.date = context.date;
            result.calculationId = foundation::utils::Uuid::generate_v4();
            result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
            return result;
        }
        requiredFields = {fieldName};
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
        requiredFields,
        [this, &context, &requiredFields, metric, qualityThreshold](const CommonFactorRuntimeState& runtime,
                                                                    CalculationResult& result) {
            auto requireField = [&](const char* fieldName) {
                if (!context.historicalView->hasField(fieldName)) {
                    const std::string error = QStringLiteral("质量因子 HistoricalView 回测缺少字段 %1")
                        .arg(QString::fromUtf8(fieldName))
                        .toStdString();
                    result.dataStatus = CalculationResult::createError(error).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(error));
                    return false;
                }
                return true;
            };

            for (const QString& fieldName : requiredFields) {
                if (!requireField(fieldName.toUtf8().constData())) {
                    return;
                }
            }

            if (metric == QualityMetric::EARNINGS_QUALITY) {
                const auto netProfitMap = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), "net_profit", context.symbols);
                const auto equityMap = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), "equity", context.symbols);
                for (const auto& [symbol, netProfit] : netProfitMap) {
                    const auto equityIt = equityMap.find(symbol);
                    if (equityIt == equityMap.end()) {
                        continue;
                    }
                    if (netProfit <= 0.0 || equityIt->second <= 0.0) {
                        continue;
                    }
                    const double factorValue = netProfit / equityIt->second;
                    if (factorValue >= qualityThreshold) {
                        result.values[symbol] = factorValue;
                    }
                }
            } else {
                const QString fieldName = requiredFields.front();
                const auto crossSection = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), fieldName.toStdString(), context.symbols);
                for (const auto& [symbol, factorValue] : crossSection) {
                    if (factorValue > 0.0 && factorValue >= qualityThreshold) {
                        result.values[symbol] = factorValue;
                    }
                }
            }

            if (result.values.empty()) {
                result.dataStatus.availability = DataAvailability::UNAVAILABLE;
                result.dataStatus.coverage = 0.0;
                result.dataStatus.message = "未查询到满足条件的质量因子数据";
                result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
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
        [this, &context, metric, qualityThreshold](const CommonFactorRuntimeState&, CalculationResult& result) {
            if (!result.values.empty()) {
                result.values = handleOutliers(applyBoundaryRules(result.values, context));
            }
            result.metadata.set("metric", json_helper::toJsonValue(static_cast<int>(metric)));
            result.metadata.set("qualityThreshold", json_helper::toJsonValue(qualityThreshold));
            result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        });
}

DataRequirements QualityFactor::getDataRequirements() const {
    DataRequirements req;
    const auto appendUnique = [&req](const std::string& field) {
        if (std::find(req.requiredFields.begin(), req.requiredFields.end(), field) == req.requiredFields.end()) {
            req.requiredFields.push_back(field);
        }
    };
    switch (params_.metric) {
    case QualityMetric::ROE:
        appendUnique("roe");
        break;
    case QualityMetric::ROA:
        appendUnique("roa");
        break;
    case QualityMetric::GROSS_MARGIN:
    case QualityMetric::OPERATING_MARGIN:
        appendUnique("profit_margin");
        break;
    case QualityMetric::EARNINGS_QUALITY:
    default:
        appendUnique("net_profit");
        appendUnique("equity");
        break;
    }
    if (params_.neutralizationEnabled) {
        appendUnique("industry_code");
        appendUnique("market_cap");
    }
    return req;
}

BoundaryRules QualityFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = 1;
    return rules;
}

std::shared_ptr<QualityFactor> QualityFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<QualityFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

void QualityFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config)) {
        const auto calculation = config::calculationConfig(config);
        params_ = qualityParamsFromJson(calculation);
    }
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
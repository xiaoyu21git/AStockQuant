#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/ConfigurableFactorDetail.h"
#include "domain/factor/include/factor_enums.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace factor {
namespace {

void requireStringField(std::string& valueOut,
                        const foundation::json::JsonFacade& json,
                        const char* fieldName)
{
    if (!json.has(fieldName)) {
        throw std::runtime_error(std::string(fieldName) + " 字段缺失");
    }
    const auto value = json.get(fieldName);
    if (!value.isString()) {
        throw std::runtime_error(std::string(fieldName) + " 不是字符串字段");
    }
    valueOut = value.asString();
}

namespace custom_config {

constexpr const char* kExpressionKey = "expression";
constexpr const char* kVariablesKey = "variables";
constexpr const char* kVariableNameKey = "name";
constexpr const char* kVariableFieldKey = "field";
constexpr const char* kDefaultValueKey = "defaultValue";

} // namespace custom_config

namespace common_config {

constexpr const char* kFrequencyKey = "frequency";
constexpr const char* kLagEnabledKey = "laggedEnabled";
constexpr const char* kLagModeKey = "lagMode";
constexpr const char* kLagPeriodsKey = "lagPeriods";
constexpr const char* kStandardizationKey = "standardization";
constexpr const char* kNeutralizationEnabledKey = "neutralizationEnabled";
constexpr const char* kNeutralizationMethodKey = "neutralizationMethod";
constexpr const char* kWindowKey = "window";
constexpr const char* kLookbackWindowKey = "lookbackWindow";

} // namespace common_config

namespace growth_config {

constexpr const char* kGrowthMetricsKey = "growthMetrics";
constexpr const char* kGrowthWeightsKey = "growthWeights";

} // namespace growth_config

namespace macro_config {

constexpr const char* kMacroDimensionsKey = "macroDimensions";
constexpr const char* kMacroIndicatorsKey = "macroIndicators";
constexpr const char* kMacroFrequencyKey = "macroFrequency";
constexpr const char* kMacroWindowKey = "macroWindow";
constexpr const char* kBenchmarkSymbolKey = "benchmarkSymbol";
constexpr const char* kPriceTypeKey = "priceType";

} // namespace macro_config

namespace metric_config {

constexpr const char* kMetricKey = "metric";
constexpr const char* kDividendMetricsKey = "dividendMetrics";
constexpr const char* kMinDividendYieldKey = "minDividendYield";
constexpr const char* kIndustryMetricKey = "industryMetric";
constexpr const char* kSectorTypeKey = "sectorType";
constexpr const char* kSentimentSourceKey = "sentimentSource";

} // namespace metric_config

namespace technical_config {

constexpr const char* kTechnicalIndicatorsKey = "technicalIndicators";
constexpr const char* kTechnicalCombinationModeKey = "technicalCombinationMode";
constexpr const char* kMaWindowKey = "maWindow";
constexpr const char* kEmaWindowKey = "emaWindow";
constexpr const char* kBollWindowKey = "bollWindow";
constexpr const char* kBollStdDevKey = "bollStdDev";
constexpr const char* kKdjWindowKey = "kdjWindow";
constexpr const char* kKdjKPeriodKey = "kdjKPeriod";
constexpr const char* kKdjDPeriodKey = "kdjDPeriod";
constexpr const char* kAtrWindowKey = "atrWindow";
constexpr const char* kVwapWindowKey = "vwapWindow";
constexpr const char* kVolumeRatioWindowKey = "volumeRatioWindow";
constexpr const char* kRsiWindowKey = "rsiWindow";
constexpr const char* kMacdFastPeriodKey = "macdFastPeriod";
constexpr const char* kMacdSlowPeriodKey = "macdSlowPeriod";
constexpr const char* kMacdSignalPeriodKey = "macdSignalPeriod";
constexpr const char* kObvWindowKey = "obvWindow";
constexpr const char* kTurnoverStabilityWindowKey = "turnoverStabilityWindow";
constexpr const char* kTurnoverStabilityMetricKey = "turnoverStabilityMetric";
constexpr const char* kTechnicalPriceTypeKey = "technicalPriceType";
constexpr const char* kUseVolumeKey = "useVolume";

} // namespace technical_config

void requireStringItem(std::string& valueOut,
                       const foundation::json::JsonFacade& json,
                       size_t index,
                       const char* fieldName)
{
    const auto value = json.at(index);
    if (!value.isString()) {
        throw std::runtime_error(std::string(fieldName) + " 数组项不是字符串");
    }
    valueOut = value.asString();
}

template <typename EnumType>
EnumType requireEnumValue(const foundation::json::JsonFacade& value,
                          const char* fieldName,
                          int minValue,
                          int maxValue)
{
    if (!value.isNumber()) {
        throw std::runtime_error(std::string(fieldName) + " 不是枚举数值字段");
    }
    const int enumValue = value.asInt();
    if (enumValue < minValue || enumValue > maxValue) {
        throw std::runtime_error(std::string(fieldName) + " 不是有效的枚举值");
    }
    return static_cast<EnumType>(enumValue);
}

template <typename EnumType>
EnumType requireEnumField(const foundation::json::JsonFacade& json,
                          const char* fieldName,
                          int minValue,
                          int maxValue)
{
    if (!json.has(fieldName)) {
        throw std::runtime_error(std::string(fieldName) + " 字段缺失");
    }
    return requireEnumValue<EnumType>(json.get(fieldName), fieldName, minValue, maxValue);
}



void appendUniqueField(std::vector<std::string>& fields, const std::string& field)
{
    if (field.empty()) {
        return;
    }
    if (std::find(fields.begin(), fields.end(), field) == fields.end()) {
        fields.push_back(field);
    }
}

bool configurableFactorNeedsHistoricalNeutralization(
    FactorType factorType,
    const ConfigurableFactorBase::CommonParams& commonParams)
{
    if (!commonParams.neutralizationEnabled) {
        return false;
    }

    switch (factorType) {
    case FactorType::GROWTH:
    case FactorType::DIVIDEND:
    case FactorType::TECHNICAL:
    case FactorType::LIQUIDITY:
    case FactorType::MACRO:
    case FactorType::INDUSTRY:
    case FactorType::SENTIMENT:
    case FactorType::CUSTOM:
        return true;
    default:
        return false;
    }
}

std::vector<TechnicalIndicator> normalizeTechnicalIndicatorList(const foundation::json::JsonFacade& value)
{
    if (!value.isArray()) {
        throw std::runtime_error(std::string(technical_config::kTechnicalIndicatorsKey) + " 不是枚举数组字段");
    }
    std::vector<TechnicalIndicator> indicators;
    for (size_t index = 0; index < value.size(); ++index) {
        const TechnicalIndicator indicator = requireEnumValue<TechnicalIndicator>(
            value.at(index),
            technical_config::kTechnicalIndicatorsKey,
            static_cast<int>(TechnicalIndicator::RSI),
            static_cast<int>(TechnicalIndicator::TURNOVER_STABILITY));
        if (indicator != TechnicalIndicator::UNKNOWN) {
            if (std::find(indicators.begin(), indicators.end(), indicator) == indicators.end()) {
                indicators.push_back(indicator);
            }
        }
    }
    return indicators;
}

std::vector<TechnicalIndicator> resolvedTechnicalIndicators(const ConfigurableFactorBase::TechnicalParams& params)
{
    std::vector<TechnicalIndicator> indicatorTypes;
    for (const TechnicalIndicator indicatorType : params.technicalIndicators) {
        if (indicatorType != TechnicalIndicator::UNKNOWN
                && std::find(indicatorTypes.begin(), indicatorTypes.end(), indicatorType) == indicatorTypes.end()) {
            indicatorTypes.push_back(indicatorType);
        }
    }
    return indicatorTypes;
}

TechnicalCombinationMode technicalCombinationModeFromValue(const foundation::json::JsonFacade& value)
{
    return requireEnumValue<TechnicalCombinationMode>(value,
                                                      technical_config::kTechnicalCombinationModeKey,
                                                      static_cast<int>(TechnicalCombinationMode::EqualWeight),
                                                      static_cast<int>(TechnicalCombinationMode::NormalizedAverage));
}

configurable_factor_detail::LiquidityIndicatorSpec resolvedTechnicalTurnoverMetricSpec(const ConfigurableFactorBase::TechnicalParams& params)
{
    return configurable_factor_detail::liquidityIndicatorSpec(params.turnoverStabilityMetric);
}

void appendUniqueRequirementField(std::vector<std::string>& fields, const QString& field)
{
    appendUniqueField(fields, field.trimmed().toStdString());
}

void appendUniqueRequirementField(std::vector<std::string>& fields, const factor::bridge::FieldKey* field)
{
    if (!field) {
        return;
    }
    appendUniqueField(fields, field->c_str());
}

DataRequirements derivedTechnicalDataRequirements(
    const ConfigurableFactorBase::TechnicalParams& params,
    const ConfigurableFactorBase::CommonParams& commonParams)
{
    DataRequirements requirements;
    const std::vector<TechnicalIndicator> indicatorTypes = resolvedTechnicalIndicators(params);
    const bool needHighLowSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesHighLow);
    const bool needVolumeSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesVolume);
    const bool needPriceSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesPriceField);
    const bool needTurnoverSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesTurnoverMetric);

    if (needPriceSeries) {
        const auto priceIndicator = configurable_factor_detail::technicalPriceIndicatorSpec(params.technicalPriceType);
        if (!priceIndicator.common.hasResolvedSource() || priceIndicator.common.sourceTable != SourceTable::DAILY_BAR) {
            return requirements;
        }
        appendUniqueRequirementField(requirements.requiredFields, priceIndicator.common.fieldKey);
    }
    if (needHighLowSeries) {
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::HIGH));
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::LOW));
    }
    if (needVolumeSeries) {
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::VOLUME));
    }
    if (needTurnoverSeries) {
        const auto turnoverIndicator = resolvedTechnicalTurnoverMetricSpec(params);
        appendUniqueRequirementField(requirements.requiredFields, turnoverIndicator.common.fieldKey);
    }
    if (commonParams.neutralizationEnabled) {
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE));
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP));
    }

    if (!requirements.requiredFields.empty()) {
        requirements.sourceTable = SourceTable::DAILY_BAR;
    }
    if (commonParams.neutralizationEnabled) {
        requirements.sourceTable = SourceTable::UNKNOWN;
    }

    return requirements;
}

DataRequirements derivedIndustryDataRequirements(
    const ConfigurableFactorBase::IndustryParams& params,
    const ConfigurableFactorBase::CommonParams& commonParams)
{
    DataRequirements requirements;
    const auto industryIndicator = configurable_factor_detail::industryIndicatorSpec(params.industryMetricKind);
    if (industryIndicator.common.fieldKey) {
        appendUniqueRequirementField(requirements.requiredFields, industryIndicator.common.fieldKey);
        requirements.sourceTable = industryIndicator.common.sourceTable;
    }
    if (commonParams.neutralizationEnabled) {
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE));
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP));
        requirements.sourceTable = SourceTable::UNKNOWN;
    }
    return requirements;
}

BoundaryRules derivedTechnicalBoundaryRules(
    const ConfigurableFactorBase::TechnicalParams& params,
    const BoundaryRules& baseRules)
{
    BoundaryRules rules = baseRules;

    const std::vector<TechnicalIndicator> indicatorTypes = resolvedTechnicalIndicators(params);
    const int rsiWindow = (std::max)(2, params.rsiWindow);
    const int maWindow = (std::max)(2, params.maWindow);
    const int emaWindow = (std::max)(2, params.emaWindow);
    const int bollWindow = (std::max)(2, params.bollWindow);
    const int kdjWindow = (std::max)(2, params.kdjWindow);
    const int atrWindow = (std::max)(2, params.atrWindow);
    const int macdFastPeriod = (std::max)(2, params.macdFastPeriod);
    const int macdSlowPeriod = (std::max)(macdFastPeriod + 1, params.macdSlowPeriod);
    const int macdSignalPeriod = (std::max)(2, params.macdSignalPeriod);
    const int obvWindow = (std::max)(2, params.obvWindow);
    const int vwapWindow = (std::max)(2, params.vwapWindow);
    const int volumeRatioWindow = (std::max)(2, params.volumeRatioWindow);
    const int turnoverStabilityWindow = (std::max)(2, params.turnoverStabilityWindow);

    int minDataPoints = 1;
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::RSI) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, rsiWindow + 1);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::MACD) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, macdSlowPeriod + macdSignalPeriod + 5);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::MA) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, maWindow);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::EMA) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, emaWindow);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::BOLL) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, bollWindow);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::KDJ) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, kdjWindow + 1);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::ATR) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, atrWindow + 1);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::OBV) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, obvWindow + 1);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::VWAP) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, vwapWindow + 1);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::VOLUME_RATIO) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, volumeRatioWindow + 1);
    }
    if (std::find(indicatorTypes.begin(), indicatorTypes.end(), TechnicalIndicator::TURNOVER_STABILITY) != indicatorTypes.end()) {
        minDataPoints = (std::max)(minDataPoints, turnoverStabilityWindow);
    }

    rules.minDataPoints = minDataPoints;
    return rules;
}

BoundaryRules derivedGrowthBoundaryRules(
    const ConfigurableFactorBase::GrowthParams& params,
    const ConfigurableFactorBase::CommonParams& common,
    const BoundaryRules& baseRules)
{
    BoundaryRules rules = baseRules;

    int minDataPoints = 1;
    for (const GrowthMetric metric : params.growthMetrics) {
        switch (metric) {
        case GrowthMetric::REVENUE_GROWTH:
        case GrowthMetric::NET_PROFIT_GROWTH:
        case GrowthMetric::DELTA_ROE:
            minDataPoints = (std::max)(minDataPoints, 2);
            break;
        case GrowthMetric::SUE:
            minDataPoints = (std::max)(minDataPoints, 5);
            break;
        default:
            break;
        }
    }

    const int lagPeriods = common.lagEnabled
        ? (std::max)(1, static_cast<int>(common.lagPeriods))
        : 0;
    rules.minDataPoints = minDataPoints + lagPeriods;
    return rules;
}

} // namespace

void ConfigurableFactorBase::CommonParams::fromJson(const foundation::json::JsonFacade& json)
{
    if (json.has(common_config::kFrequencyKey)) {
        frequency = requireEnumField<DataFrequency>(json, common_config::kFrequencyKey,
                                                    static_cast<int>(DataFrequency::Daily),
                                                    static_cast<int>(DataFrequency::Yearly));
    }
    if (json.has(common_config::kLagEnabledKey)) lagEnabled = json.get(common_config::kLagEnabledKey).asBool();
    if (json.has(common_config::kLagModeKey)) {
        lagMode = requireEnumField<LagMode>(json, common_config::kLagModeKey,
                                            static_cast<int>(LagMode::None),
                                            static_cast<int>(LagMode::Adaptive));
    }
    if (json.has(common_config::kLagPeriodsKey)) lagPeriods = static_cast<uint8_t>(json.get(common_config::kLagPeriodsKey).asInt());
    if (json.has(common_config::kStandardizationKey)) {
        standardization = requireEnumField<StandardizationMethod>(json, common_config::kStandardizationKey,
                                                                  static_cast<int>(StandardizationMethod::None),
                                                                  static_cast<int>(StandardizationMethod::Percentile));
    }
    if (json.has(common_config::kNeutralizationEnabledKey)) neutralizationEnabled = json.get(common_config::kNeutralizationEnabledKey).asBool();
    if (json.has(common_config::kNeutralizationMethodKey)) {
        neutralizationMethod = requireEnumField<NeutralizationMethod>(json, common_config::kNeutralizationMethodKey,
                                                                      static_cast<int>(NeutralizationMethod::None),
                                                                      static_cast<int>(NeutralizationMethod::IndustryMarketCap));
    }
    if (json.has(common_config::kWindowKey)) window = static_cast<uint16_t>(json.get(common_config::kWindowKey).asInt());
    if (json.has(common_config::kLookbackWindowKey)) {
        lookbackWindow = static_cast<uint16_t>(json.get(common_config::kLookbackWindowKey).asInt());
    }
}

void ConfigurableFactorBase::GrowthParams::fromJson(const foundation::json::JsonFacade& json)
{
    growthMetrics.clear();
    growthWeights.clear();
    if (!json.has(growth_config::kGrowthMetricsKey) || !json.has(growth_config::kGrowthWeightsKey)) {
        throw std::runtime_error("成长因子必须同时提供 growthMetrics 和 growthWeights");
    }
    const auto metrics = json.get(growth_config::kGrowthMetricsKey);
    const auto weights = json.get(growth_config::kGrowthWeightsKey);
    if (!metrics.isArray() || !weights.isArray() || metrics.size() != weights.size()) {
        throw std::runtime_error("growthMetrics 和 growthWeights 必须为等长数组");
    }
    for (size_t index = 0; index < metrics.size(); ++index) {
        const GrowthMetric growthMetric = requireEnumValue<GrowthMetric>(
            metrics.at(index),
            growth_config::kGrowthMetricsKey,
            static_cast<int>(GrowthMetric::REVENUE_GROWTH),
            static_cast<int>(GrowthMetric::SUE));
        const double weight = weights.at(index).asDouble();
        if (!std::isfinite(weight)) {
            throw std::runtime_error("growthMetrics 或 growthWeights 包含非法值");
        }
        growthMetrics.push_back(growthMetric);
        growthWeights.push_back(weight);
    }
}

void ConfigurableFactorBase::LiquidityParams::fromJson(const foundation::json::JsonFacade& json)
{
    if (!json.has(metric_config::kMetricKey)) {
        throw std::runtime_error("metric 字段缺失");
    }
    liquidityMetric = requireEnumField<LiquidityMetric>(json, metric_config::kMetricKey,
                                                        static_cast<int>(LiquidityMetric::TURNOVER_RATE),
                                                        static_cast<int>(LiquidityMetric::AMPLITUDE));
}

void ConfigurableFactorBase::DividendParams::fromJson(const foundation::json::JsonFacade& json)
{
    dividendMetrics.clear();
    if (json.has(metric_config::kMetricKey)) {
        throw std::runtime_error("红利因子已禁止 legacy metric 字段，必须显式提供 dividendMetrics");
    }
    if (!json.has(metric_config::kDividendMetricsKey)) {
        throw std::runtime_error("红利因子缺少 dividendMetrics 配置");
    }

    const auto metrics = json.get(metric_config::kDividendMetricsKey);
    if (!metrics.isArray()) {
        throw std::runtime_error("dividendMetrics 不是数组字段");
    }

    if (dividendMetrics.empty()) {
        for (size_t index = 0; index < metrics.size(); ++index) {
            const DividendMetric parsedMetric = requireEnumValue<DividendMetric>(
                metrics.at(index),
                metric_config::kDividendMetricsKey,
                static_cast<int>(DividendMetric::DIVIDEND_YIELD),
                static_cast<int>(DividendMetric::DIVIDEND_STABILITY));
            const auto indicator = configurable_factor_detail::dividendIndicatorSpec(parsedMetric);
            if (!indicator.common.hasField()) {
                throw std::runtime_error("dividendMetrics 包含不支持的枚举");
            }
            dividendMetrics.push_back(parsedMetric);
        }
    }
    if (dividendMetrics.empty()) {
        throw std::runtime_error("dividendMetrics 不能为空");
    }
    dividendMetric = DividendMetric::UNKNOWN;
    if (json.has(metric_config::kMinDividendYieldKey)) minDividendYield = json.get(metric_config::kMinDividendYieldKey).asDouble();
}

void ConfigurableFactorBase::TechnicalParams::fromJson(const foundation::json::JsonFacade& json)
{
    if (json.has(technical_config::kTechnicalIndicatorsKey)) {
        technicalIndicators = normalizeTechnicalIndicatorList(json.get(technical_config::kTechnicalIndicatorsKey));
    }
    if (json.has(technical_config::kTechnicalCombinationModeKey)) {
        technicalCombinationMode = technicalCombinationModeFromValue(json.get(technical_config::kTechnicalCombinationModeKey));
    }
    if (json.has(technical_config::kMaWindowKey)) maWindow = json.get(technical_config::kMaWindowKey).asInt();
    if (json.has(technical_config::kEmaWindowKey)) emaWindow = json.get(technical_config::kEmaWindowKey).asInt();
    if (json.has(technical_config::kBollWindowKey)) bollWindow = json.get(technical_config::kBollWindowKey).asInt();
    if (json.has(technical_config::kBollStdDevKey)) bollStdDev = json.get(technical_config::kBollStdDevKey).asDouble();
    if (json.has(technical_config::kKdjWindowKey)) kdjWindow = json.get(technical_config::kKdjWindowKey).asInt();
    if (json.has(technical_config::kKdjKPeriodKey)) kdjKPeriod = json.get(technical_config::kKdjKPeriodKey).asInt();
    if (json.has(technical_config::kKdjDPeriodKey)) kdjDPeriod = json.get(technical_config::kKdjDPeriodKey).asInt();
    if (json.has(technical_config::kAtrWindowKey)) atrWindow = json.get(technical_config::kAtrWindowKey).asInt();
    if (json.has(technical_config::kVwapWindowKey)) vwapWindow = json.get(technical_config::kVwapWindowKey).asInt();
    if (json.has(technical_config::kVolumeRatioWindowKey)) volumeRatioWindow = json.get(technical_config::kVolumeRatioWindowKey).asInt();
    if (json.has(technical_config::kRsiWindowKey)) rsiWindow = json.get(technical_config::kRsiWindowKey).asInt();
    if (json.has(technical_config::kMacdFastPeriodKey)) macdFastPeriod = json.get(technical_config::kMacdFastPeriodKey).asInt();
    if (json.has(technical_config::kMacdSlowPeriodKey)) macdSlowPeriod = json.get(technical_config::kMacdSlowPeriodKey).asInt();
    if (json.has(technical_config::kMacdSignalPeriodKey)) macdSignalPeriod = json.get(technical_config::kMacdSignalPeriodKey).asInt();
    if (json.has(technical_config::kObvWindowKey)) obvWindow = json.get(technical_config::kObvWindowKey).asInt();
    if (json.has(technical_config::kTurnoverStabilityWindowKey)) {
        turnoverStabilityWindow = json.get(technical_config::kTurnoverStabilityWindowKey).asInt();
    }
    if (json.has(technical_config::kTurnoverStabilityMetricKey)) {
        turnoverStabilityMetric = requireEnumField<LiquidityMetric>(json, technical_config::kTurnoverStabilityMetricKey,
                                                                    static_cast<int>(LiquidityMetric::TURNOVER_RATE),
                                                                    static_cast<int>(LiquidityMetric::AMPLITUDE));
        if (!resolvedTechnicalTurnoverMetricSpec(*this).common.hasResolvedSource()) {
            throw std::runtime_error("turnoverStabilityMetric 包含不支持的字段");
        }
    }
    if (json.has(technical_config::kTechnicalPriceTypeKey)) {
        technicalPriceType = requireEnumField<TechnicalPriceType>(json, technical_config::kTechnicalPriceTypeKey,
                                                                  static_cast<int>(TechnicalPriceType::CLOSE),
                                                                  static_cast<int>(TechnicalPriceType::LOW));
    }
    if (json.has(technical_config::kUseVolumeKey)) useVolume = json.get(technical_config::kUseVolumeKey).asBool();
}

void ConfigurableFactorBase::MacroParams::fromJson(const foundation::json::JsonFacade& json)
{
    if (json.has(macro_config::kMacroDimensionsKey)) {
        macroDimensions.clear();
        const auto dimensions = json.get(macro_config::kMacroDimensionsKey);
        if (!dimensions.isArray()) {
            throw std::runtime_error("macroDimensions 不是数组字段");
        }
        for (size_t index = 0; index < dimensions.size(); ++index) {
            const MacroDimension dimension = requireEnumValue<MacroDimension>(
                dimensions.at(index),
                macro_config::kMacroDimensionsKey,
                static_cast<int>(MacroDimension::GROWTH),
                static_cast<int>(MacroDimension::RISK_APPETITE));
            if (std::find(macroDimensions.begin(), macroDimensions.end(), dimension) == macroDimensions.end()) {
                macroDimensions.push_back(dimension);
            }
        }
    }
    if (json.has(macro_config::kMacroIndicatorsKey)) {
        macroIndicators.clear();
        const auto indicators = json.get(macro_config::kMacroIndicatorsKey);
        if (!indicators.isArray()) {
            throw std::runtime_error("macroIndicators 不是数组字段");
        }
        for (size_t index = 0; index < indicators.size(); ++index) {
            const MacroIndicator indicator = requireEnumValue<MacroIndicator>(
                indicators.at(index),
                macro_config::kMacroIndicatorsKey,
                static_cast<int>(MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY),
                static_cast<int>(MacroIndicator::VIX_PROXY));
            if (std::find(macroIndicators.begin(), macroIndicators.end(), indicator) == macroIndicators.end()) {
                macroIndicators.push_back(indicator);
            }
        }
    }
    if (json.has(macro_config::kMacroFrequencyKey)) {
        macroFrequency = requireEnumField<DataFrequency>(json, macro_config::kMacroFrequencyKey,
                                                         static_cast<int>(DataFrequency::Daily),
                                                         static_cast<int>(DataFrequency::Yearly));
    }
    if (json.has(macro_config::kMacroWindowKey)) macroWindow = json.get(macro_config::kMacroWindowKey).asInt();
    if (macroWindow <= 0 && json.has(common_config::kWindowKey)) macroWindow = json.get(common_config::kWindowKey).asInt();
    if (json.has(macro_config::kBenchmarkSymbolKey)) {
        requireStringField(benchmarkSymbol, json, macro_config::kBenchmarkSymbolKey);
    }
    if (json.has(macro_config::kPriceTypeKey)) {
        priceType = requireEnumField<TechnicalPriceType>(json, macro_config::kPriceTypeKey,
                                                         static_cast<int>(TechnicalPriceType::CLOSE),
                                                         static_cast<int>(TechnicalPriceType::LOW));
    }
}

void ConfigurableFactorBase::IndustryParams::fromJson(const foundation::json::JsonFacade& json)
{
    if (!json.has(metric_config::kIndustryMetricKey)) {
        throw std::runtime_error("industryMetric 字段缺失");
    }
    industryMetricKind = requireEnumField<IndustryMetric>(json, metric_config::kIndustryMetricKey,
                                                          static_cast<int>(IndustryMetric::INDUSTRY_PROSPERITY),
                                                          static_cast<int>(IndustryMetric::INDUSTRY_CONCENTRATION));
    if (json.has(metric_config::kSectorTypeKey)) {
        sectorType = requireEnumField<ConfigurableSectorType>(json, metric_config::kSectorTypeKey,
                                                              static_cast<int>(ConfigurableSectorType::SW_L1),
                                                              static_cast<int>(ConfigurableSectorType::CITIC_L2));
    }
}

void ConfigurableFactorBase::SentimentParams::fromJson(const foundation::json::JsonFacade& json)
{
    if (json.has(metric_config::kMetricKey)) {
        sentimentMetric = requireEnumField<SentimentMetric>(json, metric_config::kMetricKey,
                                                            static_cast<int>(SentimentMetric::SENTIMENT_SCORE),
                                                            static_cast<int>(SentimentMetric::MARKET_SENTIMENT));
    }
    if (json.has(metric_config::kSentimentSourceKey)) {
        sentimentSource = requireEnumField<SentimentSource>(json, metric_config::kSentimentSourceKey,
                                                            static_cast<int>(SentimentSource::NEWS),
                                                            static_cast<int>(SentimentSource::DERIVATIVES));
    }
}

void ConfigurableFactorBase::CustomParams::fromJson(const foundation::json::JsonFacade& json)
{
    if (json.has(custom_config::kExpressionKey)) {
        expression = json.get(custom_config::kExpressionKey).asString();
    }
    if (json.has(custom_config::kVariablesKey)) {
        variables.clear();
        const auto variableArray = json.get(custom_config::kVariablesKey);
        if (!variableArray.isArray()) {
            throw std::runtime_error("variables 不是数组字段");
        }
        for (size_t index = 0; index < variableArray.size(); ++index) {
            const auto variable = variableArray.at(index);
            if (!variable.isObject() || !variable.has(custom_config::kVariableNameKey)) {
                continue;
            }
            CustomVariableBinding binding;
            requireStringField(binding.name, variable, custom_config::kVariableNameKey);
            if (variable.has(custom_config::kVariableFieldKey)) {
                requireStringField(binding.field, variable, custom_config::kVariableFieldKey);
            }
            if (variable.has(custom_config::kDefaultValueKey)) {
                throw std::runtime_error("variables 不支持 defaultValue 兜底字段");
            }
            variables.push_back(std::move(binding));
        }
    }
}

DataRequirements ConfigurableFactorBase::getDataRequirements() const
{
    const FactorType factorType = configuredFactorType();
    if (factorType == FactorType::TECHNICAL) {
        return derivedTechnicalDataRequirements(technicalParams(), commonParams_);
    }
    if (factorType == FactorType::INDUSTRY) {
        DataRequirements requirements = dataRequirements_;
        const DataRequirements derivedRequirements = derivedIndustryDataRequirements(industryParams(), commonParams_);
        if (requirements.sourceTable == SourceTable::UNKNOWN) {
            requirements.sourceTable = derivedRequirements.sourceTable;
        } else if (derivedRequirements.sourceTable != SourceTable::UNKNOWN
                   && requirements.sourceTable != derivedRequirements.sourceTable) {
            requirements.sourceTable = SourceTable::UNKNOWN;
        }
        for (const std::string& field : derivedRequirements.requiredFields) {
            appendUniqueField(requirements.requiredFields, field);
        }
        for (const std::string& field : derivedRequirements.optionalFields) {
            appendUniqueField(requirements.optionalFields, field);
        }
        for (const std::string& field : derivedRequirements.alternativeFields) {
            appendUniqueField(requirements.alternativeFields, field);
        }
        return requirements;
    }

    DataRequirements requirements = dataRequirements_;
    if (configurableFactorNeedsHistoricalNeutralization(factorType, commonParams_)) {
        appendUniqueField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE).toStdString());
        appendUniqueField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP).toStdString());
    }
    return requirements;
}

BoundaryRules ConfigurableFactorBase::getBoundaryRules() const
{
    if (configuredFactorType() == FactorType::GROWTH) {
        return derivedGrowthBoundaryRules(growthParams(), commonParams_, boundaryRules_);
    }
    if (configuredFactorType() == FactorType::TECHNICAL) {
        return derivedTechnicalBoundaryRules(technicalParams(), boundaryRules_);
    }
    return boundaryRules_;
}

void ConfigurableFactorBase::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    commonParams_ = CommonParams{};
    switch (factorType_) {
    case FactorType::GROWTH:
        specificParams_ = GrowthParams{};
        break;
    case FactorType::LIQUIDITY:
        specificParams_ = LiquidityParams{};
        break;
    case FactorType::TECHNICAL:
        specificParams_ = TechnicalParams{};
        break;
    case FactorType::DIVIDEND:
        specificParams_ = DividendParams{};
        break;
    case FactorType::MACRO:
        specificParams_ = MacroParams{};
        break;
    case FactorType::INDUSTRY:
        specificParams_ = IndustryParams{};
        break;
    case FactorType::SENTIMENT:
        specificParams_ = SentimentParams{};
        break;
    case FactorType::CUSTOM:
        specificParams_ = CustomParams{};
        break;
    default:
        break;
    }
    if (config::hasCalculationConfig(config)) {
        const auto calculation = config::calculationConfig(config);
        commonParams_.fromJson(calculation);
        switch (factorType_) {
        case FactorType::GROWTH:
            std::get<GrowthParams>(specificParams_).fromJson(calculation);
            break;
        case FactorType::LIQUIDITY:
            std::get<LiquidityParams>(specificParams_).fromJson(calculation);
            break;
        case FactorType::TECHNICAL:
            std::get<TechnicalParams>(specificParams_).fromJson(calculation);
            break;
        case FactorType::DIVIDEND:
            std::get<DividendParams>(specificParams_).fromJson(calculation);
            break;
        case FactorType::MACRO:
            std::get<MacroParams>(specificParams_).fromJson(calculation);
            break;
        case FactorType::INDUSTRY:
            std::get<IndustryParams>(specificParams_).fromJson(calculation);
            break;
        case FactorType::SENTIMENT:
            std::get<SentimentParams>(specificParams_).fromJson(calculation);
            break;
        case FactorType::CUSTOM:
            std::get<CustomParams>(specificParams_).fromJson(calculation);
            break;
        default:
            break;
        }
    }
    if (factorType_ == FactorType::TECHNICAL) {
        dataRequirements_ = derivedTechnicalDataRequirements(technicalParams(), commonParams_);
        boundaryRules_ = derivedTechnicalBoundaryRules(technicalParams(), boundaryRules_);
    }
}

} // namespace factor
#include "domain/factor/include/BaseFactor.h"

#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorNeutralizationUtils.h"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace factor {

namespace {

namespace nested_config {

constexpr const char* kSourceTableKey = "sourceTable";
constexpr const char* kRequiredKey = "required";
constexpr const char* kOptionalKey = "optional";
constexpr const char* kAlternativeKey = "alternative";
constexpr const char* kMinDataPointsKey = "minDataPoints";
constexpr const char* kHandleNewStockKey = "handleNewStock";
constexpr const char* kHandleSuspendedKey = "handleSuspended";
constexpr const char* kHandleDelistedKey = "handleDelisted";
constexpr const char* kHandleOutliersKey = "handleOutliers";
constexpr const char* kDataRequirementsSourceTableField = "dataRequirements.sourceTable";
constexpr const char* kDataRequirementsRequiredField = "dataRequirements.required";
constexpr const char* kDataRequirementsOptionalField = "dataRequirements.optional";
constexpr const char* kDataRequirementsAlternativeField = "dataRequirements.alternative";

} // namespace nested_config

void appendStringArrayField(std::vector<std::string>& target,
                            const foundation::json::JsonFacade& json,
                            const char* key,
                            const char* errorField)
{
    if (!json.has(key)) {
        return;
    }

    const auto values = json.get(key);
    for (size_t index = 0; index < values.size(); ++index) {
        const auto item = values.at(index);
        if (!item.isString()) {
            throw std::runtime_error(std::string(errorField) + " 不是字符串字段");
        }
        target.push_back(item.asString());
    }
}

void loadDataRequirementsFromJson(DataRequirements& requirements,
                                  const foundation::json::JsonFacade& dataReq)
{
    if (dataReq.has(nested_config::kSourceTableKey)) {
        requirements.sourceTable = requireNumericEnumValue<SourceTable>(
            dataReq.get(nested_config::kSourceTableKey),
            nested_config::kDataRequirementsSourceTableField,
            static_cast<int>(SourceTable::DAILY_BAR),
            static_cast<int>(SourceTable::UNKNOWN));
    }

    appendStringArrayField(requirements.requiredFields,
                           dataReq,
                           nested_config::kRequiredKey,
                           nested_config::kDataRequirementsRequiredField);
    appendStringArrayField(requirements.optionalFields,
                           dataReq,
                           nested_config::kOptionalKey,
                           nested_config::kDataRequirementsOptionalField);
    appendStringArrayField(requirements.alternativeFields,
                           dataReq,
                           nested_config::kAlternativeKey,
                           nested_config::kDataRequirementsAlternativeField);
}

void loadBoundaryRulesFromJson(BoundaryRules& boundaryRules,
                               const foundation::json::JsonFacade& rules)
{
    if (rules.has(nested_config::kMinDataPointsKey)) {
        boundaryRules.minDataPoints = rules.get(nested_config::kMinDataPointsKey).asInt();
    }

    if (rules.has(nested_config::kHandleNewStockKey)) {
        boundaryRules.handleNewStock = requireNumericEnumField<NewStockHandling>(
            rules,
            nested_config::kHandleNewStockKey,
            static_cast<int>(NewStockHandling::EXCLUDE_IF_LT_60D),
            static_cast<int>(NewStockHandling::INCLUDE));
    }

    if (rules.has(nested_config::kHandleSuspendedKey)) {
        boundaryRules.handleSuspended = requireNumericEnumField<SuspendedHandling>(
            rules,
            nested_config::kHandleSuspendedKey,
            static_cast<int>(SuspendedHandling::FORWARD_FILL),
            static_cast<int>(SuspendedHandling::SET_NULL));
    }

    if (rules.has(nested_config::kHandleDelistedKey)) {
        boundaryRules.handleDelisted = requireNumericEnumField<DelistedHandling>(
            rules,
            nested_config::kHandleDelistedKey,
            static_cast<int>(DelistedHandling::KEEP_UNTIL_DELIST),
            static_cast<int>(DelistedHandling::EXCLUDE));
    }

    if (rules.has(nested_config::kHandleOutliersKey)) {
        boundaryRules.handleOutliers = requireNumericEnumField<OutlierHandling>(
            rules,
            nested_config::kHandleOutliersKey,
            static_cast<int>(OutlierHandling::WINSORIZE_3SIGMA),
            static_cast<int>(OutlierHandling::KEEP));
    }
}

bool isNeutralizationSampleInsufficientMessage(const QString& message)
{
    const QString normalized = message.trimmed();
    return normalized.contains(QStringLiteral("中性化样本不足"))
        || normalized.contains(QStringLiteral("中性化后没有有效样本"))
        || normalized.contains(QStringLiteral("行业和市值残差化"));
}

} // namespace

BaseFactor::BaseFactor() 
    : instanceId_(foundation::utils::Uuid::generate_v4().to_string()) {
}

std::vector<CalculationResult> BaseFactor::calculateBatch(
    const std::vector<CalculationContext>& contexts) {
    
    std::vector<CalculationResult> results;
    results.reserve(contexts.size());
    
    for (const auto& context : contexts) {
        results.push_back(calculate(context));
    }
    
    return results;
}

DataStatus BaseFactor::checkDataAvailability(const std::string& date) const {
    if (!dataChecker_) {
        DataStatus status;
        status.availability = DataAvailability::UNAVAILABLE;
        status.message = "数据检查器未初始化";
        return status;
    }

    return dataChecker_->checkFactorData(instanceId_.to_string(), date, date);
}

double BaseFactor::calculatePercentileValue(std::vector<double> values, double quantile)
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

void BaseFactor::applyCommonStandardization(std::unordered_map<std::string, double>& values,
                                            StandardizationMethod standardization)
{
    if (values.empty() || standardization == StandardizationMethod::None) {
        return;
    }

    if (standardization == StandardizationMethod::Percentile || standardization == StandardizationMethod::Rank) {
        std::vector<std::pair<std::string, double>> ranked(values.begin(), values.end());
        std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
            return left.second < right.second;
        });
        const double denominator = static_cast<double>(ranked.size());
        for (size_t index = 0; index < ranked.size();) {
            size_t groupEnd = index + 1;
            while (groupEnd < ranked.size() && ranked[groupEnd].second == ranked[index].second) {
                ++groupEnd;
            }

            const double precedingFraction = static_cast<double>(index) / denominator;
            for (size_t groupIndex = index; groupIndex < groupEnd; ++groupIndex) {
                values[ranked[groupIndex].first] = precedingFraction;
            }
            index = groupEnd;
        }
        return;
    }

    std::vector<double> finiteValues;
    finiteValues.reserve(values.size());
    for (const auto& entry : values) {
        if (std::isfinite(entry.second)) {
            finiteValues.push_back(entry.second);
        }
    }
    if (finiteValues.empty()) {
        return;
    }

    if (standardization == StandardizationMethod::ZScore) {
        const double mean = std::accumulate(finiteValues.begin(), finiteValues.end(), 0.0)
            / static_cast<double>(finiteValues.size());
        double variance = 0.0;
        for (const double value : finiteValues) {
            const double delta = value - mean;
            variance += delta * delta;
        }
        const double stdev = std::sqrt(variance / static_cast<double>(finiteValues.size()));
        if (stdev > 1e-12) {
            for (auto& entry : values) {
                entry.second = (entry.second - mean) / stdev;
            }
        }
        return;
    }

    if (standardization == StandardizationMethod::MinMax) {
        const auto [minIt, maxIt] = std::minmax_element(finiteValues.begin(), finiteValues.end());
        const double range = *maxIt - *minIt;
        if (range > 1e-12) {
            for (auto& entry : values) {
                entry.second = (entry.second - *minIt) / range;
            }
        }
    }
}

void BaseFactor::appendCommonMetadata(CalculationResult& result,
                                      const CommonMetricParams& params,
                                      const CommonRuntimeState& runtime)
{
    result.metadata.set("effectiveDate", json_helper::toJsonValue(runtime.effectiveDate.toStdString()));
    result.metadata.set("frequency", json_helper::toJsonValue(static_cast<int>(runtime.frequency)));
    result.metadata.set("laggedEnabled", json_helper::toJsonValue(params.lagEnabled));
    result.metadata.set("lookbackWindow", json_helper::toJsonValue(params.lookbackWindow));
    result.metadata.set("standardization", json_helper::toJsonValue(static_cast<int>(runtime.standardization)));
    result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params.neutralizationEnabled));
    result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(runtime.neutralizationMode)));
    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
}

foundation::json::JsonFacade BaseFactor::toJson() const {
    auto json = foundation::json::JsonFacade::createObject();
    
    config::setSerializedInstanceId(json, instanceId_.to_string());
    config::setSerializedFactorName(json, name_);
    config::setSerializedDescription(json, description_);
    config::setFactorType(json, factorType_);
    config::setDataRequirementsConfig(json, dataRequirements_.toJson());
    config::setBoundaryRulesConfig(json, boundaryRules_.toJson());
    
    return json;
}

void BaseFactor::fromJson(const foundation::json::JsonFacade& json) {
    if (config::hasSerializedInstanceId(json)) {
        instanceId_ = foundation::utils::Uuid::from_string(config::requiredSerializedInstanceId(json));
    }
    
    if (config::hasSerializedFactorName(json)) {
        name_ = config::requiredSerializedFactorName(json);
    }
    
    if (config::hasSerializedDescription(json)) {
        description_ = config::requiredSerializedDescription(json);
    }
    
    if (config::hasFactorType(json)) {
        factorType_ = config::requiredFactorTypeFromConfig(json);
    }
    
    if (config::hasDataRequirementsConfig(json)) {
        loadDataRequirementsFromJson(dataRequirements_, config::dataRequirementsConfig(json));
    }
    
    if (config::hasBoundaryRulesConfig(json)) {
        loadBoundaryRulesFromJson(boundaryRules_, config::boundaryRulesConfig(json));
    }
}

bool BaseFactor::isHistoricalViewRuntime(const CalculationContext& context) const {
    return static_cast<bool>(context.historicalView);
}

CalculationResult BaseFactor::createHistoricalViewRuntimeError(const CalculationContext& context,
                                                               const std::string& errorMsg) const {
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus = CalculationResult::createError(errorMsg).dataStatus;
    result.metadata.set("error", json_helper::toJsonValue(errorMsg));
    return result;
}

CalculationResult BaseFactor::executeWithCommonParams(
    const CalculationContext& context,
    const CommonMetricParams& params,
    const std::function<QString()>& effectiveDateResolver,
    const std::function<void(const CommonRuntimeState&, CalculationResult&)>& rawCalculator,
    const std::function<void(const CommonRuntimeState&, CalculationResult&)>& preStandardizationProcessor,
    const std::function<void(const CommonRuntimeState&, CalculationResult&)>& metadataAppender,
    const QString& dataStatusMessage) const {

    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("已移除因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
    }

    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = dataStatusMessage.toStdString();

    CommonRuntimeState runtime;
    runtime.frequency = params.frequency;
    runtime.standardization = params.standardization;
    runtime.effectiveDate = effectiveDateResolver ? effectiveDateResolver() : QString::fromStdString(context.date);
    runtime.neutralizationMode = params.neutralizationEnabled
        ? NeutralizationStatus::Requested
        : NeutralizationStatus::Disabled;

    rawCalculator(runtime, result);

    if (result.dataStatus.isValid() && !result.values.empty()) {
        applyCommonNeutralization(context, params, runtime, result, runtime.neutralizationMode);
    }

    if (result.dataStatus.isValid() && !result.values.empty()) {
        preStandardizationProcessor(runtime, result);
    }

    if (result.dataStatus.isValid() && !result.values.empty()) {
        applyCommonStandardization(result.values, runtime.standardization);
    }

    appendCommonMetadata(result, params, runtime);
    metadataAppender(runtime, result);
    return result;
}

QString BaseFactor::resolveCommonEffectiveDateForFields(const CalculationContext& context,
                                                        const CommonMetricParams& params,
                                                        const QStringList& requiredFieldsForDateResolution,
                                                        CommonFieldRequirementMode requirementMode) const {
    QDate anchorDate = QDate::fromString(QString::fromStdString(context.date), Qt::ISODate);
    if (!anchorDate.isValid()) {
        return QString::fromStdString(context.date);
    }

    if (params.frequency == DataFrequency::Weekly) {
        const int shiftToPreviousFriday = anchorDate.dayOfWeek() >= 5 ? anchorDate.dayOfWeek() - 5 : anchorDate.dayOfWeek() + 2;
        anchorDate = anchorDate.addDays(-shiftToPreviousFriday);
    } else if (params.frequency == DataFrequency::Monthly) {
        anchorDate = QDate(anchorDate.year(), anchorDate.month(), 1).addDays(-1);
    } else if (params.frequency == DataFrequency::Quarterly) {
        const int quarter = (anchorDate.month() - 1) / 3;
        const int quarterStartMonth = quarter * 3 + 1;
        anchorDate = QDate(anchorDate.year(), quarterStartMonth, 1).addDays(-1);
    } else if (params.frequency == DataFrequency::Yearly) {
        anchorDate = QDate(anchorDate.year(), 1, 1).addDays(-1);
    }

    if (requiredFieldsForDateResolution.isEmpty()) {
        return anchorDate.toString(Qt::ISODate);
    }

    const std::vector<std::string> symbols = context.symbols.empty()
        ? context.historicalView->getAvailableSymbols(context.date)
        : context.symbols;
    const int maxOffset = (std::max)(0, static_cast<int>(params.lookbackWindow));
    const int startOffset = params.lagEnabled ? (std::max)(1, static_cast<int>(params.lagPeriods)) : 0;
    for (int offset = startOffset; offset <= maxOffset; ++offset) {
        const QString candidate = anchorDate.addDays(-offset).toString(Qt::ISODate);
        bool matchedField = false;
        bool missingField = false;
        for (const QString& field : requiredFieldsForDateResolution) {
            if (field.isEmpty()) {
                missingField = true;
                continue;
            }
            const bool hasFieldData = !context.historicalView->getCrossSection(candidate.toStdString(), field.toStdString(), symbols).empty();
            if (hasFieldData) {
                matchedField = true;
            } else if (requirementMode == CommonFieldRequirementMode::AllFields) {
                missingField = true;
                break;
            }
        }
        if (requirementMode == CommonFieldRequirementMode::AllFields && !missingField && matchedField) {
            return candidate;
        }
        if (requirementMode == CommonFieldRequirementMode::AnyField && matchedField) {
            return candidate;
        }
    }

    return anchorDate.toString(Qt::ISODate);
}

CommonMetricParams BaseFactor::buildCommonMetricParams(int lookbackWindow,
                                                       bool laggedEnabled,
                                                       DataFrequency frequency,
                                                       StandardizationMethod standardization,
                                                       bool neutralizationEnabled,
                                                       uint8_t lagPeriods)
{
    CommonMetricParams params;
    params.lookbackWindow = static_cast<uint16_t>((std::max)(1, lookbackWindow));
    params.lagEnabled = laggedEnabled;
    params.lagPeriods = lagPeriods;
    params.frequency = frequency;
    params.standardization = standardization;
    params.neutralizationEnabled = neutralizationEnabled;
    return params;
}

void BaseFactor::appendRequiredField(DataRequirements& requirements,
                                     const std::string& field)
{
    if (field.empty()) {
        return;
    }
    if (std::find(requirements.requiredFields.begin(), requirements.requiredFields.end(), field)
        == requirements.requiredFields.end()) {
        requirements.requiredFields.push_back(field);
    }
}

void BaseFactor::appendHistoricalNeutralizationRequirements(
    DataRequirements& requirements,
    bool neutralizationEnabled,
    SourceTable nonNeutralizedSourceTable)
{
    requirements.sourceTable = nonNeutralizedSourceTable;
    if (!neutralizationEnabled) {
        return;
    }

    appendRequiredField(requirements, QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE).toStdString());
    appendRequiredField(requirements, QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP).toStdString());
    requirements.sourceTable = SourceTable::UNKNOWN;
}

BoundaryRules BaseFactor::buildBoundaryRules(int minDataPoints,
                                             OutlierHandling handleOutliers)
{
    BoundaryRules rules;
    rules.minDataPoints = (std::max)(1, minDataPoints);
    rules.handleOutliers = handleOutliers;
    return rules;
}

bool BaseFactor::applyCommonNeutralization(const CalculationContext& context,
                                           const CommonMetricParams& params,
                                           const CommonRuntimeState& runtime,
                                           CalculationResult& result,
                                           NeutralizationStatus& neutralizationMode) const {
    if (!params.neutralizationEnabled || result.values.empty()) {
        return true;
    }

    CalculationContext neutralizationContext = context;
    neutralizationContext.date = runtime.effectiveDate.toStdString();

    QString errorMessage;
    if (!factor::neutralization::applyIndustrySizeNeutralization(neutralizationContext, result.values, &errorMessage)) {
        neutralizationMode = NeutralizationStatus::HistoricalViewFailed;
        result.values.clear();
        if (isNeutralizationSampleInsufficientMessage(errorMessage)) {
            result.dataStatus.availability = DataAvailability::PARTIAL;
            result.dataStatus.coverage = 0.0;
            result.dataStatus.message = errorMessage.toStdString();
            result.metadata.set("emptyReason", json_helper::toJsonValue(errorMessage.toStdString()));
            return true;
        }

        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return false;
    }

    neutralizationMode = NeutralizationStatus::HistoricalViewCrossSectionIndustryMarketCap;
    return true;
}

std::unordered_map<std::string, double> BaseFactor::applyBoundaryRules(
    const std::unordered_map<std::string, double>& rawValues,
    const CalculationContext& context) {
    
    // 简化实现：直接返回原始值
    // 实际实现需要根据boundaryRules_处理新股、停牌等
    return rawValues;
}

std::unordered_map<std::string, double> BaseFactor::handleOutliers(
    const std::unordered_map<std::string, double>& values) {
    if (values.empty()) {
        return values;
    }

    switch (boundaryRules_.handleOutliers) {
    case OutlierHandling::KEEP:
        return values;
    case OutlierHandling::EXCLUDE:
        // 排除异常值：这里简化处理，实际需要计算统计量
        return values;
    case OutlierHandling::WINSORIZE_3SIGMA: {
        // 计算均值和标准差
        std::vector<double> valueList;
        for (const auto& [symbol, value] : values) {
            valueList.push_back(value);
        }
        
        double sum = std::accumulate(valueList.begin(), valueList.end(), 0.0);
        double mean = sum / valueList.size();
        
        double variance = 0.0;
                for (double v : valueList) {
                    double delta = v - mean;
                    variance += delta * delta;
                }
                variance /= static_cast<double>(valueList.size());
                double stdev = std::sqrt(std::max(0.0, variance));
        
        double lower = mean - 3 * stdev;
        double upper = mean + 3 * stdev;
        
        std::unordered_map<std::string, double> winsorized;
        for (const auto& [symbol, value] : values) {
            double newValue = value;
            if (value < lower) newValue = lower;
            if (value > upper) newValue = upper;
            winsorized[symbol] = newValue;
        }
        
        return winsorized;
    }
    }

    return values;
}

void BaseFactor::loadConfig(const foundation::json::JsonFacade& config) {
    dataRequirements_.requiredFields.clear();
    dataRequirements_.optionalFields.clear();
    dataRequirements_.alternativeFields.clear();
    dataRequirements_.sourceTable = SourceTable::UNKNOWN;
    boundaryRules_ = BoundaryRules{};
    boundaryRules_.minDataPoints = 0;

    // 解析配置
    if (config::hasDataRequirementsConfig(config)) {
        loadDataRequirementsFromJson(dataRequirements_, config::dataRequirementsConfig(config));
    }
    
    if (config::hasBoundaryRulesConfig(config)) {
        loadBoundaryRulesFromJson(boundaryRules_, config::boundaryRulesConfig(config));
    }
}

} // namespace factor
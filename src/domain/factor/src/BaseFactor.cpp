#include "domain/factor/include/BaseFactor.h"
#include "foundation/log/logging.hpp"

#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorNeutralizationUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
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

constexpr int kMonthsPerYear = 12;
constexpr int kFridayIndex = 5;
constexpr int kIsoWeekLength = 7;

bool parseIsoDate(const std::string& text, std::tm& out)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }

    try {
        const int year = std::stoi(text.substr(0, 4));
        const int month = std::stoi(text.substr(5, 2));
        const int day = std::stoi(text.substr(8, 2));
        if (month < 1 || month > kMonthsPerYear || day < 1 || day > 31) {
            return false;
        }

        std::tm candidate = {};
        candidate.tm_year = year - 1900;
        candidate.tm_mon = month - 1;
        candidate.tm_mday = day;
        candidate.tm_isdst = -1;
        if (std::mktime(&candidate) == -1) {
            return false;
        }
        out = candidate;
        return true;
    } catch (...) {
        return false;
    }
}

std::string formatIsoDate(const std::tm& value)
{
    char buffer[11] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &value);
    return std::string(buffer);
}

std::tm addDays(const std::tm& base, int dayOffset)
{
    std::tm shifted = base;
    shifted.tm_mday += dayOffset;
    shifted.tm_isdst = -1;
    std::mktime(&shifted);
    return shifted;
}

int isoDayOfWeek(const std::tm& value)
{
    const int wday = value.tm_wday;
    return ((wday + 6) % kIsoWeekLength) + 1;
}

std::string trimAsciiWhitespace(std::string text)
{
    const auto isSpace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };
    const auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    const auto end = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

bool isNeutralizationSampleInsufficientMessage(const std::string& message)
{
    const std::string normalized = trimAsciiWhitespace(message);
    return normalized.find("中性化样本不足") != std::string::npos
        || normalized.find("中性化后没有有效样本") != std::string::npos
        || normalized.find("行业和市值残差化") != std::string::npos;
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
        // Percentile: rank / N   (范围 0 ~ (N-1)/N)
        // Rank:       rank / (N-1) (范围 0 ~ 1)，与 CompositeFactor::applyRankLike 一致
        const bool isPercentile = (standardization == StandardizationMethod::Percentile);
        const double denominator = isPercentile
            ? static_cast<double>(ranked.size())
            : (ranked.size() > 1 ? static_cast<double>(ranked.size() - 1) : 1.0);
        for (size_t index = 0; index < ranked.size();) {
            size_t groupEnd = index + 1;
            while (groupEnd < ranked.size() && ranked[groupEnd].second == ranked[index].second) {
                ++groupEnd;
            }

            const double rankValue = static_cast<double>(index) / denominator;
            for (size_t groupIndex = index; groupIndex < groupEnd; ++groupIndex) {
                values[ranked[groupIndex].first] = rankValue;
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
    result.metadata.set("effectiveDate", json_helper::toJsonValue(runtime.effectiveDate));
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
    const std::function<std::string()>& effectiveDateResolver,
    const std::function<void(const CommonRuntimeState&, CalculationResult&)>& rawCalculator,
    const std::function<void(const CommonRuntimeState&, CalculationResult&)>& preStandardizationProcessor,
    const std::function<void(const CommonRuntimeState&, CalculationResult&)>& metadataAppender,
    const std::string& dataStatusMessage) const {

    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            "已移除因子运行期数据库取数路径，请由引擎提供 HistoricalView");
    }

    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = dataStatusMessage;

    CommonRuntimeState runtime;
    runtime.frequency = params.frequency;
    runtime.standardization = params.standardization;
    runtime.effectiveDate = effectiveDateResolver ? effectiveDateResolver() : context.date;
    runtime.neutralizationMode = params.neutralizationEnabled
        ? NeutralizationStatus::Requested
        : NeutralizationStatus::Disabled;

    rawCalculator(runtime, result);

    // 方向翻转：ascending=false 时对所有原始值取反（与中性化/标准化可交换，放在最前面语义最清晰）
    if (!params.ascending && !result.values.empty()) {
        for (auto& [symbol, value] : result.values) {
            value = -value;
        }
    }

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

std::string BaseFactor::resolveCommonEffectiveDateForFields(const CalculationContext& context,
                                                            const CommonMetricParams& params,
                                                            const std::vector<std::string>& requiredFieldsForDateResolution,
                                                            CommonFieldRequirementMode requirementMode) const {
    std::tm anchorDate = {};
    if (!parseIsoDate(context.date, anchorDate)) {
        return context.date;
    }

    if (params.frequency == DataFrequency::Weekly) {
        const int dayOfWeek = isoDayOfWeek(anchorDate);
        const int shiftToPreviousFriday = dayOfWeek >= kFridayIndex ? dayOfWeek - kFridayIndex : dayOfWeek + 2;
        anchorDate = addDays(anchorDate, -shiftToPreviousFriday);
    } else if (params.frequency == DataFrequency::Monthly) {
        std::tm monthStart = anchorDate;
        monthStart.tm_mday = 1;
        std::mktime(&monthStart);
        anchorDate = addDays(monthStart, -1);
    } else if (params.frequency == DataFrequency::Quarterly) {
        const int currentMonth = anchorDate.tm_mon + 1;
        const int quarter = (currentMonth - 1) / 3;
        const int quarterStartMonth = quarter * 3 + 1;
        std::tm quarterStart = anchorDate;
        quarterStart.tm_mon = quarterStartMonth - 1;
        quarterStart.tm_mday = 1;
        std::mktime(&quarterStart);
        anchorDate = addDays(quarterStart, -1);
    } else if (params.frequency == DataFrequency::Yearly) {
        std::tm yearStart = anchorDate;
        yearStart.tm_mon = 0;
        yearStart.tm_mday = 1;
        std::mktime(&yearStart);
        anchorDate = addDays(yearStart, -1);
    }

    if (requiredFieldsForDateResolution.empty()) {
        return formatIsoDate(anchorDate);
    }

    const std::vector<std::string> symbols = context.symbols.empty()
        ? context.historicalView->getAvailableSymbols(context.date)
        : context.symbols;
    const int maxOffset = (std::max)(0, static_cast<int>(params.lookbackWindow));
    const int startOffset = params.lagEnabled ? (std::max)(1, static_cast<int>(params.lagPeriods)) : 0;
    for (int offset = startOffset; offset <= maxOffset; ++offset) {
        const std::string candidate = formatIsoDate(addDays(anchorDate, -offset));
        bool matchedField = false;
        bool missingField = false;
        for (const std::string& field : requiredFieldsForDateResolution) {
            if (field.empty()) {
                missingField = true;
                continue;
            }
            const bool hasFieldData = !context.historicalView->getCrossSection(candidate, field, symbols).empty();
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

    return formatIsoDate(anchorDate);
}

CommonMetricParams BaseFactor::buildCommonMetricParams(int lookbackWindow,
                                                       bool laggedEnabled,
                                                       DataFrequency frequency,
                                                       StandardizationMethod standardization,
                                                       bool neutralizationEnabled,
                                                       uint8_t lagPeriods,
                                                       bool ascending)
{
    CommonMetricParams params;
    params.lookbackWindow = static_cast<uint16_t>((std::max)(1, lookbackWindow));
    params.lagEnabled = laggedEnabled;
    params.lagPeriods = lagPeriods;
    params.frequency = frequency;
    params.standardization = standardization;
    params.neutralizationEnabled = neutralizationEnabled;
    params.ascending = ascending;
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

    appendRequiredField(requirements, field_names::INDUSTRY_CODE);
    appendRequiredField(requirements, field_names::MARKET_CAP);
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
    neutralizationContext.date = runtime.effectiveDate;

    std::string errorMessage;
    if (!factor::neutralization::applyIndustrySizeNeutralization(neutralizationContext, result.values, &errorMessage)) {
        neutralizationMode = NeutralizationStatus::HistoricalViewFailed;
        // 中性化失败不丢弃原始因子值
        INTERNAL_WARN_STREAM << "[neutralization] failed: " << errorMessage << " — keeping raw values";
        return true;
    }

    neutralizationMode = NeutralizationStatus::HistoricalViewCrossSectionIndustryMarketCap;
    return true;
}

std::unordered_map<std::string, double> BaseFactor::applyBoundaryRules(///处新股停盘的逻辑为什么放这里
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

// ── 运行时辅助方法 ──
std::vector<std::string> BaseFactor::effectiveSymbols(const CalculationContext& context) const {
    if (!context.symbols.empty()) {
        return context.symbols;
    }
    if (context.historicalView) {
        return context.historicalView->getAvailableSymbols(context.date);
    }
    return {};
}

std::unordered_map<std::string, double> BaseFactor::currentFieldCrossSection(
    const CalculationContext& context, const std::string& field) const {
    std::unordered_map<std::string, double> result;
    if (!context.historicalView || field.empty()) {
        return result;
    }
    const auto symbols = effectiveSymbols(context);
    const auto crossSection = context.historicalView->getCrossSection(context.date, field, symbols);
    for (const auto& entry : crossSection) {
        result[entry.first] = entry.second;
    }
    return result;
}

std::vector<double> BaseFactor::seriesForField(
    const CalculationContext& context, const std::string& symbol,
    const std::string& field, int window) const {
    if (!context.historicalView || symbol.empty() || field.empty()) {
        return {};
    }
    const auto series = context.historicalView->getSeries(symbol, context.date, context.date, field);
    std::vector<double> result;
    result.reserve(series.size());
    for (const auto& dp : series) {
        result.push_back(dp.value);
    }
    if (window > 0 && static_cast<int>(result.size()) > window) {
        result.erase(result.begin(), result.end() - window);
    }
    return result;
}

std::unordered_map<std::string, double> BaseFactor::latestFinancialMetric(
    const CalculationContext& context, const std::string& field,
    const std::string& date) const {
    std::unordered_map<std::string, double> result;
    if (!context.historicalView || field.empty()) {
        return result;
    }
    const std::string effectiveDate = date.empty() ? context.date : date;
    const auto symbols = effectiveSymbols(context);
    const auto crossSection = context.historicalView->getCrossSection(effectiveDate, field, symbols);
    for (const auto& entry : crossSection) {
        result[entry.first] = entry.second;
    }
    return result;
}

std::unordered_map<std::string, std::vector<double>> BaseFactor::latestFinancialSeries(
    const CalculationContext& context, const std::string& field,
    const std::string& date, int limit) const {
    std::unordered_map<std::string, std::vector<double>> result;
    if (!context.historicalView || field.empty()) return result;

    const std::string effectiveDate = date.empty() ? context.date : date;
    const auto symbols = effectiveSymbols(context);
    const int effectiveLimit = (limit > 0) ? limit : 1;

    // 记忆化：财务按季度更新，回测每交易日都调本函数；同一 (field,effectiveDate,limit)
    // 的结果不变，缓存后跨交易日直接复用，避免每天重复做 4~16 次全市场横切(dbFallback)。
    const std::string cacheKey =
        field + "|" + effectiveDate + "|" + std::to_string(effectiveLimit);
    {
        auto cit = m_finSeriesCache.find(cacheKey);
        if (cit != m_finSeriesCache.end()) return cit->second;
    }

    // 用 getCrossSection 横切多个历史锚点日期（从 effectiveDate 往回推 quarterly），
    // 自动走 dbFallback 且每次横切只查一次静态缓存
    int y = 0, m = 0, d = 1;
    if (effectiveDate.size() == 10)
        sscanf(effectiveDate.c_str(), "%d-%d-%d", &y, &m, &d);

    for (int i = 0; i < effectiveLimit * 2 && i < 16; ++i) {
        int qy = y, qm = m - i * 3;
        while (qm <= 0) { qm += 12; --qy; }
        char qBuf[16];
        snprintf(qBuf, sizeof(qBuf), "%04d-%02d-%02d", qy, qm, (d > 28 ? 28 : d));
        auto cs = context.historicalView->getCrossSection(std::string(qBuf), field, symbols);
        bool anyNew = false;
        for (const auto& [sym, val] : cs) {
            if (!std::isfinite(val)) continue;
            auto& vec = result[sym];
            // 只收集不同于前值的（不同报告期）
            if (vec.empty() || std::abs(vec.back() - val) > 1e-12) {
                vec.push_back(val);
                anyNew = true;
            }
        }
        if (!anyNew && i >= effectiveLimit) break;
    }
    // 上限保护：月频工作集很小(每月同一 effectiveDate 复用)，几乎不触发；
    // 日频 effectiveDate 每日变、不复用，满则清空以免内存无限增长。
    if (m_finSeriesCache.size() >= 256) m_finSeriesCache.clear();
    m_finSeriesCache.emplace(cacheKey, result);
    return result;
}

std::unordered_map<std::string, std::string> BaseFactor::industryBySymbol(
    const CalculationContext& context) const {
    std::unordered_map<std::string, std::string> result;
    if (!context.historicalView) {
        return result;
    }
    const auto symbols = effectiveSymbols(context);
    const auto crossSection = context.historicalView->getCrossSection(context.date, field_names::INDUSTRY_CODE, symbols);
    for (const auto& entry : crossSection) {
        if (entry.second != 0.0) {
            result[entry.first] = std::to_string(static_cast<int>(entry.second));
        }
    }
    return result;
}

std::string BaseFactor::subtractCalendarDays(const std::string& isoDate, int days)
{
    if (days < 0) days = 0;
    std::tm tm = {};
    int y = 0, m = 0, d = 0;
    if (sscanf(isoDate.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return isoDate;
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d - days;
    if (std::mktime(&tm) == static_cast<std::time_t>(-1)) return isoDate;
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

} // namespace factor

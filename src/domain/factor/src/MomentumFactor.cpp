#include "domain/factor/include/MomentumFactor.h"
//#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/HistoricalView.h"
#include <ta_libc.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace factor {

namespace {

namespace momentum_json {

constexpr const char* kWindowKey = "window";
constexpr const char* kLookbackWindowKey = "lookbackWindow";
constexpr const char* kLaggedEnabledKey = "laggedEnabled";
constexpr const char* kFrequencyKey = "frequency";
constexpr const char* kStandardizationKey = "standardization";
constexpr const char* kNeutralizationEnabledKey = "neutralizationEnabled";
constexpr const char* kTypeKey = "type";
constexpr const char* kAdjustPriceTypeKey = "adjustPriceType";
constexpr const char* kUseVolumeKey = "useVolume";
constexpr const char* kSkipRecentKey = "skipRecent";

constexpr const char* kParamsMetadataKey = "params";
constexpr const char* kCalculationTypeMetadataKey = "calculationType";
constexpr const char* kEmptyReasonMetadataKey = "emptyReason";

void setParamMetadata(foundation::json::JsonFacade& json, const MomentumFactor::Params& params)
{
    json.set(kWindowKey, json_helper::toJsonValue(params.window));
    json.set(kLookbackWindowKey, json_helper::toJsonValue(static_cast<int>(params.lookbackWindow)));
    json.set(kLaggedEnabledKey, json_helper::toJsonValue(params.lagEnabled));
    json.set(kFrequencyKey, json_helper::toJsonValue(static_cast<int>(params.frequency)));
    json.set(kStandardizationKey, json_helper::toJsonValue(static_cast<int>(params.standardization)));
    json.set(kNeutralizationEnabledKey, json_helper::toJsonValue(params.neutralizationEnabled));
    json.set(kTypeKey, json_helper::toJsonValue(static_cast<int>(params.type)));
    json.set(kAdjustPriceTypeKey, json_helper::toJsonValue(static_cast<int>(params.adjustPriceType)));
    json.set(kUseVolumeKey, json_helper::toJsonValue(params.useVolume));
    json.set(kSkipRecentKey, json_helper::toJsonValue(params.skipRecent));
}

} // namespace momentum_json

MomentumFactor::Params momentumParamsFromJson(const foundation::json::JsonFacade& json)
{
    MomentumFactor::Params params;
    params.fromJson(json);

    if (json.has(momentum_json::kWindowKey)) params.window = json.get(momentum_json::kWindowKey).asInt();
    if (json.has(momentum_json::kTypeKey)) {
        params.type = requireNumericEnumField<MomentumCalculationType>(json, momentum_json::kTypeKey, static_cast<int>(MomentumCalculationType::SIMPLE), static_cast<int>(MomentumCalculationType::EXPONENTIAL));
    }
    if (json.has(momentum_json::kAdjustPriceTypeKey)) {
        params.adjustPriceType = requireNumericEnumField<AdjustPriceType>(json, momentum_json::kAdjustPriceTypeKey, static_cast<int>(AdjustPriceType::PRE_ADJUST_FACTOR), static_cast<int>(AdjustPriceType::POST_ADJUST_FACTOR));
    }
    if (json.has(momentum_json::kUseVolumeKey)) params.useVolume = json.get(momentum_json::kUseVolumeKey).asBool();
    if (json.has(momentum_json::kSkipRecentKey)) params.skipRecent = json.get(momentum_json::kSkipRecentKey).asInt();
    return params;
}

foundation::json::JsonFacade momentumParamsToJson(const MomentumFactor::Params& params)
{
    auto json = foundation::json::JsonFacade::createObject();
    momentum_json::setParamMetadata(json, params);
    return json;
}

void ensureTaLibInitialized()
{
    static std::once_flag initFlag;
    static TA_RetCode initResult = TA_BAD_PARAM;
    std::call_once(initFlag, []() {
        initResult = TA_Initialize();
    });
    if (initResult != TA_SUCCESS) {
        throw std::runtime_error("TA-Lib initialization failed for momentum factor");
    }
}

double taLastOutput(const std::vector<double>& output, int outBegIdx, int outNBElement)
{
    if (outBegIdx < 0 || outNBElement <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const size_t lastIndex = static_cast<size_t>(outNBElement - 1);
    if (lastIndex >= output.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return output[lastIndex];
}

bool parseDateYyyyMmDd(const std::string& dateText, std::tm& dateValue)
{
    std::istringstream input(dateText);
    input >> std::get_time(&dateValue, "%Y-%m-%d");
    return !input.fail();
}

std::string formatDateYyyyMmDd(const std::tm& dateValue)
{
    std::ostringstream output;
    output << std::put_time(&dateValue, "%Y-%m-%d");
    return output.str();
}

std::string shiftDateByDays(const std::string& anchorDate, int dayOffset)
{
    std::tm parsedDate{};
    if (!parseDateYyyyMmDd(anchorDate, parsedDate)) {
        throw std::runtime_error("非法计算日期");
    }

    parsedDate.tm_isdst = -1;
    std::time_t timeValue = std::mktime(&parsedDate);
    if (timeValue == static_cast<std::time_t>(-1)) {
        throw std::runtime_error("非法计算日期");
    }

    constexpr std::time_t kSecondsPerDay = 24 * 60 * 60;
    timeValue += static_cast<std::time_t>(dayOffset) * kSecondsPerDay;

    const std::tm* shiftedDate = std::localtime(&timeValue);
    if (shiftedDate == nullptr) {
        throw std::runtime_error("非法计算日期");
    }
    return formatDateYyyyMmDd(*shiftedDate);
}

}

std::string MomentumFactor::earliestMomentumSeriesDate(const std::string& anchorDate, int window, int skipRecent)
{
    const int lookbackDays = std::max(365, (window + skipRecent + 10) * 2);
    return shiftDateByDays(anchorDate, -lookbackDays);
}

/// 将回测配置中的 adjustPriceType 解析为实际使用的复权因子字段名
std::string MomentumFactor::resolveAdjustFieldName(factor::AdjustPriceType priceType)
{
    switch (priceType) {
    case factor::AdjustPriceType::PRE_ADJUST_FACTOR:
        return "pre_adjust_factor";
    case factor::AdjustPriceType::POST_ADJUST_FACTOR:
        return "post_adjust_factor";
    default:
        return "";
    }
}

double MomentumFactor::volumeConfirmationMultiplier(const std::vector<double>& volumes)
{
    if (volumes.size() < 2) {
        return 1.0;
    }

    const double latestVolume = volumes.back();
    double historyMean = 0.0;
    if (volumes.size() == 2) {
        historyMean = volumes.front();
    } else {
        ensureTaLibInitialized();

        std::vector<double> historyVolumes(volumes.begin(), volumes.end() - 1);
        std::vector<double> output(historyVolumes.size(), std::numeric_limits<double>::quiet_NaN());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_SMA(0,
                                      static_cast<int>(historyVolumes.size() - 1),
                                      historyVolumes.data(),
                                      static_cast<int>(historyVolumes.size()),
                                      &outBegIdx,
                                      &outNBElement,
                                      output.data());
        if (ret != TA_SUCCESS) {
            throw std::runtime_error("TA_SMA 计算成交量确认均值失败");
        }

        historyMean = taLastOutput(output, outBegIdx, outNBElement);
    }
    if (latestVolume <= 0.0 || historyMean <= 1e-12) {
        return 1.0;
    }

    return std::clamp(latestVolume / historyMean, 0.5, 1.5);
}

MomentumFactor::MomentumFactor() {
    factorType_ = FactorType::MOMENTUM;
}

CalculationResult MomentumFactor::calculate(const CalculationContext& context) {
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            "已移除动量因子运行期数据库取数路径，请由引擎提供 HistoricalView");
    }

    const std::string adjustField = resolveAdjustFieldName(params_.adjustPriceType);
    if (adjustField.empty()) {
        return createHistoricalViewRuntimeError(
            context,
            "动量因子 adjustPriceType 配置为空，无法确定复权因子字段");
    }

    const CommonMetricParams commonParams = buildCommonMetricParams(
        params_.lookbackWindow,
        params_.lagEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled);

    try {
        std::vector<std::string> dateResolutionFields{F_CLOSE};
        dateResolutionFields.push_back(adjustField);

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
            [this, &context](const CommonRuntimeState& runtime, CalculationResult& result) {
                if (params_.useVolume && !context.historicalView->hasField("volume")) {
                    const std::string errorMessage = "动量因子 HistoricalView 回测缺少 volume 字段，已禁止成交量确认数据库回退";
                    result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                    return;
                }

                CalculationContext resolvedContext = context;
                resolvedContext.date = runtime.effectiveDate;

                std::unordered_map<std::string, double> momentumValues;
                switch (params_.type) {
                case MomentumCalculationType::SIMPLE:
                    momentumValues = calculateSimpleMomentum(resolvedContext);
                    break;
                case MomentumCalculationType::RANK:
                    momentumValues = calculateRankMomentum(resolvedContext);
                    break;
                case MomentumCalculationType::NORMALIZED:
                case MomentumCalculationType::EXPONENTIAL:
                    momentumValues = calculateNormalizedMomentum(resolvedContext);
                    break;
                case MomentumCalculationType::UNKNOWN:
                default:
                    momentumValues = calculateSimpleMomentum(resolvedContext);
                    break;
                }

                result.values = applyBoundaryRules(momentumValues, resolvedContext);
                result.metadata.set(momentum_json::kCalculationTypeMetadataKey, json_helper::toJsonValue(static_cast<int>(params_.type)));
            },
            [this](const CommonRuntimeState&, CalculationResult& result) {
                result.values = handleOutliers(result.values);
            },
            [this](const CommonRuntimeState&, CalculationResult& result) {
                result.metadata.set(momentum_json::kParamsMetadataKey, momentumParamsToJson(params_));
                if (!result.metadata.has(momentum_json::kCalculationTypeMetadataKey)) {
                    result.metadata.set(momentum_json::kCalculationTypeMetadataKey, json_helper::toJsonValue(static_cast<int>(params_.type)));
                }
                result.metadata.set(momentum_json::kWindowKey, json_helper::toJsonValue(params_.window));
                result.metadata.set(momentum_json::kSkipRecentKey, json_helper::toJsonValue(params_.skipRecent));
                result.metadata.set(momentum_json::kAdjustPriceTypeKey, json_helper::toJsonValue(static_cast<int>(params_.adjustPriceType)));
                result.metadata.set(momentum_json::kUseVolumeKey, json_helper::toJsonValue(params_.useVolume));

                if (result.values.empty()) {
                    std::ostringstream emptyReason;
                    emptyReason << "动量因子需要至少 "
                                << (params_.window + params_.skipRecent + 1)
                                << " 个交易日样本（窗口 "
                                << params_.window
                                << "，跳过最近 "
                                << params_.skipRecent
                                << " 个交易日），当前区间内未找到满足条件的股票";
                    result.metadata.set(momentum_json::kEmptyReasonMetadataKey, json_helper::toJsonValue(emptyReason.str()));
                }
            });
    } catch (const std::exception& e) {
        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = context.date;
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.message = "计算失败: " + std::string(e.what());
        result.metadata.set("error", json_helper::toJsonValue(e.what()));
        return result;
    }
}

DataRequirements MomentumFactor::getDataRequirements() const {
    DataRequirements req;
    const std::string adjustField = resolveAdjustFieldName(params_.adjustPriceType);
    if (adjustField.empty()) {
        throw std::runtime_error("动量因子 adjustPriceType 配置非法");
    }
    appendRequiredField(req, F_CLOSE);
    appendRequiredField(req, adjustField);
    appendHistoricalNeutralizationRequirements(req,
                                               params_.neutralizationEnabled,
                                               SourceTable::DAILY_BAR);
    
    if (params_.useVolume) {
        req.optionalFields.push_back(F_VOLUME);
    }

    return req;
}

BoundaryRules MomentumFactor::getBoundaryRules() const {
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, params_.window + params_.skipRecent + 1);
    return rules;
}

std::shared_ptr<MomentumFactor> MomentumFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {
    
    auto factor = std::make_shared<MomentumFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

// ============ 私有方法实现 ============

double MomentumFactor::calculateSymbolMomentum(const std::string& symbol,
                                               const CalculationContext& context,
                                               MomentumCalculationType calculationType) {
    const auto adjustedSeries = getAdjustedPriceSeries(symbol, context);

    double momentum = 0.0;
    switch (calculationType) {
    case MomentumCalculationType::EXPONENTIAL:
        momentum = calculateTaLibExponentialMomentum(adjustedSeries);
        break;
    case MomentumCalculationType::SIMPLE:
    case MomentumCalculationType::RANK:
    case MomentumCalculationType::NORMALIZED:
    case MomentumCalculationType::UNKNOWN:
    default:
        momentum = calculateTaLibRocMomentum(adjustedSeries);
        break;
    }

    if (params_.useVolume) {
        std::vector<double> volumes;
        if (context.historicalView && context.historicalView->hasField("volume")) {
            const auto series = context.historicalView->getSeries(
                symbol,
                earliestMomentumSeriesDate(context.date, params_.window, params_.skipRecent),
                context.date,
                "volume"
            );
            for (const auto& point : series) {
                if (std::isfinite(point.value) && point.value > 0.0) {
                    volumes.push_back(point.value);
                }
            }
        }
        momentum *= volumeConfirmationMultiplier(volumes);
    }

    return momentum;
}

std::unordered_map<std::string, double> MomentumFactor::calculateSimpleMomentum(
    const CalculationContext& context) {
    
    std::unordered_map<std::string, double> momentumValues;

    std::vector<std::string> symbols = context.symbols;
    if (symbols.empty() && context.historicalView) {
        symbols = context.historicalView->getAvailableSymbols(context.date);
    }

    for (const auto& symbol : symbols) {
        try {
            momentumValues[symbol] = calculateSymbolMomentum(symbol, context, MomentumCalculationType::SIMPLE);
        } catch (const std::exception&) {
            continue;
        }
    }
    
    return momentumValues;
}

std::unordered_map<std::string, double> MomentumFactor::calculateRankMomentum(
    const CalculationContext& context) {
    
    auto simpleMomentum = calculateSimpleMomentum(context);
    
    // 转换为排名（0-1）
    std::vector<double> values;
    for (const auto& [symbol, value] : simpleMomentum) {
        values.push_back(value);
    }
    
    // 排序
    std::sort(values.begin(), values.end());
    
    std::unordered_map<std::string, double> rankValues;
    if (values.empty()) {
        return rankValues;
    }

    for (const auto& [symbol, value] : simpleMomentum) {
        // 计算百分位排名
        auto it = std::lower_bound(values.begin(), values.end(), value);
        double rank = static_cast<double>(std::distance(values.begin(), it)) / values.size();
        rankValues[symbol] = rank;
    }
    
    return rankValues;
}

std::unordered_map<std::string, double> MomentumFactor::calculateNormalizedMomentum(
    const CalculationContext& context) {
    std::unordered_map<std::string, double> rawMomentum;

    std::vector<std::string> symbols = context.symbols;
    if (symbols.empty() && context.historicalView) {
        symbols = context.historicalView->getAvailableSymbols(context.date);
    }

    for (const auto& symbol : symbols) {
        try {
            rawMomentum[symbol] = calculateSymbolMomentum(
                symbol,
                context,
                params_.type == MomentumCalculationType::EXPONENTIAL
                    ? MomentumCalculationType::EXPONENTIAL
                    : MomentumCalculationType::SIMPLE);
        } catch (const std::exception&) {
            continue;
        }
    }

    if (rawMomentum.empty()) {
        return {};
    }
    
    // 计算均值和标准差
    std::vector<double> values;
    for (const auto& [symbol, value] : rawMomentum) {
        values.push_back(value);
    }
    
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double mean = sum / values.size();
    
    double sq_sum = std::inner_product(values.begin(), values.end(), 
                                      values.begin(), 0.0);
    double stdev = std::sqrt(sq_sum / values.size() - mean * mean);
    
    // 标准化
    std::unordered_map<std::string, double> normalizedValues;
    for (const auto& [symbol, value] : rawMomentum) {
        if (stdev > 0) {
            normalizedValues[symbol] = (value - mean) / stdev;
        } else {
            normalizedValues[symbol] = 0.0;
        }
    }
    
    return normalizedValues;
}

std::vector<double> MomentumFactor::getAdjustedPriceSeries(const std::string& symbol,
                                                           const CalculationContext& context) {
    std::tm parsedDate{};
    if (!parseDateYyyyMmDd(context.date, parsedDate)) {
        throw std::runtime_error("非法计算日期");
    }

    const int requiredPoints = params_.window + params_.skipRecent + 1;
    const std::string adjustField = resolveAdjustFieldName(params_.adjustPriceType);
    if (adjustField.empty()) {
        throw std::runtime_error("动量因子 adjustPriceType 配置为空");
    }

    if (context.historicalView) {
        std::vector<double> adjustedSeries;
        if (!context.historicalView->hasField("close") || !context.historicalView->hasField(adjustField)) {
            throw std::runtime_error("动量因子在 " + adjustField + " 价格模式下要求 HistoricalView 同时提供 close 和 " + adjustField + " 字段");
        }

        const std::string startDate = earliestMomentumSeriesDate(context.date, params_.window, params_.skipRecent);

        const auto closeSeries = context.historicalView->getSeries(
            symbol,
            startDate,
            context.date,
            "close"
        );
        const auto factorSeries = context.historicalView->getSeries(
            symbol,
            startDate,
            context.date,
            adjustField
        );
        const size_t pairCount = std::min(closeSeries.size(), factorSeries.size());
        adjustedSeries.reserve(pairCount);
        for (size_t index = 0; index < pairCount; ++index) {
            if (closeSeries[index].date != factorSeries[index].date) {
                continue;
            }
            const double adjustedClose = closeSeries[index].value * factorSeries[index].value;
            if (std::isfinite(adjustedClose) && adjustedClose > 0.0) {
                adjustedSeries.push_back(adjustedClose);
            }
        }

        if (static_cast<int>(adjustedSeries.size()) < requiredPoints) {
            throw std::runtime_error("缓存集中缺少足够的历史交易日数据");
        }

        return adjustedSeries;
    }

    throw std::runtime_error("已移除动量因子运行期数据库取数路径");
}

double MomentumFactor::calculateTaLibRocMomentum(const std::vector<double>& adjustedSeries) const
{
    ensureTaLibInitialized();

    const int resolvedWindow = std::max(1, params_.window);
    const size_t anchorIndex = adjustedSeries.size() - static_cast<size_t>(params_.skipRecent) - 1;
    const std::vector<double> effectiveSeries(adjustedSeries.begin(), adjustedSeries.begin() + static_cast<std::ptrdiff_t>(anchorIndex + 1));
    if (effectiveSeries.size() < static_cast<size_t>(resolvedWindow + 1)) {
        throw std::runtime_error("动量因子缺少足够样本以计算 TA_ROCP");
    }

    std::vector<double> output(effectiveSeries.size(), std::numeric_limits<double>::quiet_NaN());
    int outBegIdx = 0;
    int outNBElement = 0;
    const TA_RetCode ret = TA_ROCP(0,
                                   static_cast<int>(effectiveSeries.size() - 1),
                                   effectiveSeries.data(),
                                   resolvedWindow,
                                   &outBegIdx,
                                   &outNBElement,
                                   output.data());
    if (ret != TA_SUCCESS) {
        throw std::runtime_error("TA_ROCP 计算失败");
    }

    const double momentum = taLastOutput(output, outBegIdx, outNBElement);
    if (!std::isfinite(momentum)) {
        throw std::runtime_error("TA_ROCP 未返回有效结果");
    }
    return momentum;
}

double MomentumFactor::calculateTaLibExponentialMomentum(const std::vector<double>& adjustedSeries) const
{
    ensureTaLibInitialized();

    const int resolvedWindow = std::max(2, params_.window);
    const size_t anchorIndex = adjustedSeries.size() - static_cast<size_t>(params_.skipRecent) - 1;
    const std::vector<double> effectiveSeries(adjustedSeries.begin(), adjustedSeries.begin() + static_cast<std::ptrdiff_t>(anchorIndex + 1));
    if (effectiveSeries.size() < static_cast<size_t>(resolvedWindow)) {
        throw std::runtime_error("动量因子缺少足够样本以计算 TA_EMA");
    }

    std::vector<double> output(effectiveSeries.size(), std::numeric_limits<double>::quiet_NaN());
    int outBegIdx = 0;
    int outNBElement = 0;
    const TA_RetCode ret = TA_EMA(0,
                                  static_cast<int>(effectiveSeries.size() - 1),
                                  effectiveSeries.data(),
                                  resolvedWindow,
                                  &outBegIdx,
                                  &outNBElement,
                                  output.data());
    if (ret != TA_SUCCESS) {
        throw std::runtime_error("TA_EMA 计算失败");
    }

    const double emaValue = taLastOutput(output, outBegIdx, outNBElement);
    const double anchorPrice = effectiveSeries.back();
    if (!std::isfinite(emaValue) || !std::isfinite(anchorPrice) || std::abs(emaValue) <= 1e-12) {
        throw std::runtime_error("TA_EMA 未返回有效结果");
    }
    return (anchorPrice - emaValue) / emaValue;
}

void MomentumFactor::loadConfig(const foundation::json::JsonFacade& config) {
    // 调用基类加载
    BaseFactor::loadConfig(config);
    
    // 加载动量因子特定配置
    if (config::hasCalculationConfig(config)) {
        auto calcConfig = config::calculationConfig(config);
        params_ = momentumParamsFromJson(calcConfig);
    }

    dataRequirements_ = getDataRequirements();
    boundaryRules_ = getBoundaryRules();
}

} // namespace factor
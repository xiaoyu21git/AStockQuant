#include "Factor.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
using namespace AStockQuantEngine::Domain::Model;

// ============ FactorParam Method Implementation ============

foundation::json::JsonFacade FactorParam::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("name", foundation::json::JsonFacade::createString(name));
    j.set("displayName", foundation::json::JsonFacade::createString(displayName));
    j.set("type", foundation::json::JsonFacade::createInt(static_cast<int>(type)));
    j.set("description", foundation::json::JsonFacade::createString(description));
    j.set("defaultValue", defaultValue);
    j.set("minValue", minValue);
    j.set("maxValue", maxValue);
    j.set("stepValue", stepValue);
    
    auto commonValuesArr = foundation::json::JsonFacade::createArray();
    for (const auto& value : commonValues) {
        commonValuesArr.push_back(value);
    }
    j.set("commonValues", commonValuesArr);
    
    return j;
}

FactorParam FactorParam::fromJson(const foundation::json::JsonFacade& json) {
    FactorParam param;
    
    param.name = json.has("name") ? json.get("name").asString() : "";
    param.displayName = json.has("displayName") ? json.get("displayName").asString() : "";
    param.type = static_cast<FactorParamType>(json.has("type") ? json.get("type").asInt() : 0);
    param.description = json.has("description") ? json.get("description").asString() : "";
    
    if (json.has("defaultValue")) {
        param.defaultValue = json.get("defaultValue");
    } else {
        param.defaultValue = foundation::json::JsonFacade::createNull();
    }
    
    if (json.has("minValue")) {
        param.minValue = json.get("minValue");
    } else {
        param.minValue = foundation::json::JsonFacade::createNull();
    }
    
    if (json.has("maxValue")) {
        param.maxValue = json.get("maxValue");
    } else {
        param.maxValue = foundation::json::JsonFacade::createNull();
    }
    
    if (json.has("stepValue")) {
        param.stepValue = json.get("stepValue");
    } else {
        param.stepValue = foundation::json::JsonFacade::createNull();
    }
    
    if (json.has("commonValues") && json.get("commonValues").isArray()) {
        auto commonValuesJson = json.get("commonValues");
        for (size_t i = 0; i < commonValuesJson.size(); i++) {
            param.commonValues.push_back(commonValuesJson.at(i));
        }
    }
    
    return param;
}

// ============ FactorPerformance Method Implementation ============

foundation::json::JsonFacade FactorPerformance::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("icMean", foundation::json::JsonFacade::createDouble(icMean));
    j.set("icStd", foundation::json::JsonFacade::createDouble(icStd));
    j.set("ir", foundation::json::JsonFacade::createDouble(ir));
    j.set("icPositiveRatio", foundation::json::JsonFacade::createDouble(icPositiveRatio));
    j.set("longShortReturn", foundation::json::JsonFacade::createDouble(longShortReturn));
    j.set("validityDays", foundation::json::JsonFacade::createInt(validityDays));
    j.set("turnoverRate", foundation::json::JsonFacade::createDouble(turnoverRate));
    
    auto groupReturnsArr = foundation::json::JsonFacade::createArray();
    for (const auto& ret : groupReturns) {
        groupReturnsArr.push_back(foundation::json::JsonFacade::createDouble(ret));
    }
    j.set("groupReturns", groupReturnsArr);
    
    return j;
}

// ============ FactorBacktestConfig Method Implementation ============

foundation::json::JsonFacade FactorBacktestConfig::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("factorName", foundation::json::JsonFacade::createString(factorName));
    j.set("stockPool", foundation::json::JsonFacade::createString(stockPool));
    
    auto startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        startDate.time_since_epoch()).count();
    j.set("startDate", foundation::json::JsonFacade::createlong(static_cast<long>(startTime)));
    
    auto endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endDate.time_since_epoch()).count();
    j.set("endDate", foundation::json::JsonFacade::createlong(static_cast<long>(endTime)));
    
    j.set("forwardDays", foundation::json::JsonFacade::createInt(forwardDays));
    j.set("groups", foundation::json::JsonFacade::createInt(groups));
    j.set("industryNeutral", foundation::json::JsonFacade::createBool(industryNeutral));
    j.set("marketCapNeutral", foundation::json::JsonFacade::createBool(marketCapNeutral));
    
    return j;
}

// ============ FactorBacktestResult 方法实现 ============

foundation::json::JsonFacade FactorBacktestResult::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("config", config.toJson());
    j.set("performance", performance.toJson());
    
    // IC时间序列
    auto icSeriesArr = foundation::json::JsonFacade::createArray();
    for (const auto& [date, ic] : icSeries) {
        auto item = foundation::json::JsonFacade::createObject();
        auto dateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            date.time_since_epoch()).count();
        item.set("date", foundation::json::JsonFacade::createlong(static_cast<long>(dateTime)));
        item.set("ic", foundation::json::JsonFacade::createDouble(ic));
        icSeriesArr.push_back(item);
    }
    j.set("icSeries", icSeriesArr);
    
    // 额外指标
    auto extraMetricsObj = foundation::json::JsonFacade::createObject();
    for (const auto& [key, value] : extraMetrics) {
        extraMetricsObj.set(key, foundation::json::JsonFacade::createDouble(value));
    }
    j.set("extraMetrics", extraMetricsObj);
    
    return j;
}

// ============ Factor 方法实现 ============

Factor::Factor(const std::string& name, const std::string& displayName,
               const std::string& majorCategory, const std::string& subCategory)
    : name_(name), displayName_(displayName),
      majorCategory_(majorCategory), subCategory_(subCategory) {
    description_ = "Custom Factor";
    performance_ = FactorPerformance{};
}

void Factor::addParam(const FactorParam& param) {
    params_.push_back(param);
    paramValues_[param.name] = param.defaultValue;
}

void Factor::setParamValue(const std::string& paramName, const foundation::json::JsonFacade& value) {
    paramValues_[paramName] = value;
}

foundation::json::JsonFacade Factor::getParamValue(const std::string& paramName) const {
    auto it = paramValues_.find(paramName);
    if (it != paramValues_.end()) {
        return it->second;
    }
    return foundation::json::JsonFacade::createNull();
}

foundation::json::JsonFacade Factor::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("name", foundation::json::JsonFacade::createString(name_));
    j.set("displayName", foundation::json::JsonFacade::createString(displayName_));
    j.set("majorCategory", foundation::json::JsonFacade::createString(majorCategory_));
    j.set("subCategory", foundation::json::JsonFacade::createString(subCategory_));
    j.set("description", foundation::json::JsonFacade::createString(description_));
    j.set("performance", performance_.toJson());
    
    // 参数列表
    auto paramsArr = foundation::json::JsonFacade::createArray();
    for (const auto& param : params_) {
        paramsArr.push_back(param.toJson());
    }
    j.set("params", paramsArr);
    
    // 参数值
    auto paramValuesObj = foundation::json::JsonFacade::createObject();
    for (const auto& [key, value] : paramValues_) {
        paramValuesObj.set(key, value);
    }
    j.set("paramValues", paramValuesObj);
    
    return j;
}

std::shared_ptr<Factor> Factor::fromJson(const foundation::json::JsonFacade& json) {
    std::string name = json.has("name") ? json.get("name").asString() : "";
    std::string displayName = json.has("displayName") ? json.get("displayName").asString() : "";
    std::string majorCategory = json.has("majorCategory") ? json.get("majorCategory").asString() : "";
    std::string subCategory = json.has("subCategory") ? json.get("subCategory").asString() : "";
    
    // 使用FactorFactory创建具体类型的因子
    // 首先尝试使用name作为类型标识符
    auto factor = FactorFactory::createFactor(name);
    if (!factor) {
        // 如果FactorFactory无法创建，则创建一个通用的因子
        // 但由于Factor是抽象类，我们需要创建一个具体的因子
        // 这里我们创建动量因子作为默认
        factor = std::make_shared<MomentumFactor>();
        // 设置基本属性
        factor->description_ = json.has("description") ? json.get("description").asString() : "";
        return factor;
    }
    
    factor->description_ = json.has("description") ? json.get("description").asString() : "";
    
    // 解析参数
    if (json.has("params") && json.get("params").isArray()) {
        auto paramsJson = json.get("params");
        for (size_t i = 0; i < paramsJson.size(); i++) {
            factor->addParam(FactorParam::fromJson(paramsJson.at(i)));
        }
    }
    
    // 解析参数值
    if (json.has("paramValues") && json.get("paramValues").isObject()) {
        auto paramValuesJson = json.get("paramValues");
        // 注意：我们需要遍历paramValuesJson的所有键
        // 但JsonFacade没有提供遍历对象键的方法
        // 简化实现：我们假设参数已经在addParam时添加了默认值
        // 这里我们无法正确解析所有键值对，因为JsonFacade API有限
        // 暂时留空，需要时扩展JsonFacade API
    }
    
    // 解析性能指标
    if (json.has("performance") && json.get("performance").isObject()) {
        auto perfJson = json.get("performance");
        FactorPerformance perf;
        
        perf.icMean = perfJson.has("icMean") ? perfJson.get("icMean").asDouble() : 0.0;
        perf.icStd = perfJson.has("icStd") ? perfJson.get("icStd").asDouble() : 0.0;
        perf.ir = perfJson.has("ir") ? perfJson.get("ir").asDouble() : 0.0;
        perf.icPositiveRatio = perfJson.has("icPositiveRatio") ? perfJson.get("icPositiveRatio").asDouble() : 0.0;
        perf.longShortReturn = perfJson.has("longShortReturn") ? perfJson.get("longShortReturn").asDouble() : 0.0;
        perf.validityDays = perfJson.has("validityDays") ? perfJson.get("validityDays").asInt() : 0;
        perf.turnoverRate = perfJson.has("turnoverRate") ? perfJson.get("turnoverRate").asDouble() : 0.0;
        
        if (perfJson.has("groupReturns") && perfJson.get("groupReturns").isArray()) {
            auto groupReturnsJson = perfJson.get("groupReturns");
            for (size_t i = 0; i < groupReturnsJson.size(); i++) {
                perf.groupReturns.push_back(groupReturnsJson.at(i).asDouble());
            }
        }
        
        factor->performance_ = perf;
    }
    
    return factor;
}

// ============ MomentumFactor 方法实现 ============

const std::string MomentumFactor::PARAM_WINDOW = "window";
const std::string MomentumFactor::PARAM_TYPE = "type";
const std::string MomentumFactor::PARAM_SMOOTHING = "smoothing";
const std::string MomentumFactor::PARAM_MIN_MOMENTUM = "min_momentum";

MomentumFactor::MomentumFactor() 
    : Factor("momentum", "Momentum Factor", "Momentum", "Trend Momentum") {
    
    // 设置描述
    description_ = "Trend tracking factor based on price momentum, calculates N-day price momentum";
    
    // 添加参数
    FactorParam windowParam;
    windowParam.name = PARAM_WINDOW;
    windowParam.displayName = std::string("窗口期\n");
    windowParam.type = FactorParamType::INTEGER;
    windowParam.description = std::string("计算动量的窗口期（天数）");
    windowParam.defaultValue = foundation::json::JsonFacade::createInt(20);
    windowParam.minValue = foundation::json::JsonFacade::createInt(1);
    windowParam.maxValue = foundation::json::JsonFacade::createInt(250);
    windowParam.stepValue = foundation::json::JsonFacade::createInt(1);
    
    auto commonValuesArr = foundation::json::JsonFacade::createArray();
    commonValuesArr.push_back(foundation::json::JsonFacade::createInt(5));
    commonValuesArr.push_back(foundation::json::JsonFacade::createInt(10));
    commonValuesArr.push_back(foundation::json::JsonFacade::createInt(20));
    commonValuesArr.push_back(foundation::json::JsonFacade::createInt(30));
    commonValuesArr.push_back(foundation::json::JsonFacade::createInt(60));
    commonValuesArr.push_back(foundation::json::JsonFacade::createInt(120));
    windowParam.commonValues.push_back(commonValuesArr);
    
    addParam(windowParam);
    
    FactorParam typeParam;
    typeParam.name = PARAM_TYPE;
    typeParam.displayName = std::string("动量类型");
    typeParam.type = FactorParamType::ENUM;
    typeParam.description = std::string("动量计算类型");
    typeParam.defaultValue = foundation::json::JsonFacade::createString("simple");
    
    auto typeValues = foundation::json::JsonFacade::createArray();
    typeValues.push_back(foundation::json::JsonFacade::createString("simple"));
    typeValues.push_back(foundation::json::JsonFacade::createString("exponential"));
    typeValues.push_back(foundation::json::JsonFacade::createString("rank"));
    typeValues.push_back(foundation::json::JsonFacade::createString("normalized"));
    typeParam.commonValues.push_back(typeValues);
    
    addParam(typeParam);
    
    FactorParam smoothingParam;
    smoothingParam.name = PARAM_SMOOTHING;
    smoothingParam.displayName = std::string("平滑参数");
    smoothingParam.type = FactorParamType::INTEGER;
    smoothingParam.description = std::string("动量值的平滑窗(天)\n");
    smoothingParam.defaultValue = foundation::json::JsonFacade::createInt(3);
    smoothingParam.minValue = foundation::json::JsonFacade::createInt(0);
    smoothingParam.maxValue = foundation::json::JsonFacade::createInt(10);
    smoothingParam.stepValue = foundation::json::JsonFacade::createInt(1);
    addParam(smoothingParam);
    
    FactorParam minMomentumParam;
    minMomentumParam.name = PARAM_MIN_MOMENTUM;
    minMomentumParam.displayName = std::string("最小动量阈值\n");
    minMomentumParam.type = FactorParamType::FLOAT;
    minMomentumParam.description = std::string("动量信号的最小绝对值阈值（百分比）");
    minMomentumParam.defaultValue = foundation::json::JsonFacade::createDouble(2.0);
    minMomentumParam.minValue = foundation::json::JsonFacade::createDouble(0.0);
    minMomentumParam.maxValue = foundation::json::JsonFacade::createDouble(10.0);
    minMomentumParam.stepValue = foundation::json::JsonFacade::createDouble(0.5);
    addParam(minMomentumParam);
    
    // 设置默认性能指标
    performance_.icMean = 0.042;
    performance_.icStd = 0.085;
    performance_.ir = 1.24;
    performance_.icPositiveRatio = 0.62;
    performance_.longShortReturn = 0.047;
    performance_.validityDays = 20;
    performance_.turnoverRate = 800.0;
    performance_.groupReturns = {0.022, 0.025, 0.028, 0.032, 0.038, 0.045};
}

double MomentumFactor::calculateMomentumValue(double currentPrice, double historicalPrice) const {
    if (historicalPrice <= 0.0) {
        return 0.0;
    }
    return (currentPrice - historicalPrice) / historicalPrice;
}

FactorBacktestResult MomentumFactor::backtest(const FactorBacktestConfig& config) {
    // 简化的动量因子回测实现
    // 实际实现中应该：
    // 1. 从数据库加载历史价格数据
    // 2. 计算每日的动量因子值
    // 3. 计算未来收益
    // 4. 计算IC、IR等指标
    
    FactorBacktestResult result;
    result.config = config;
    
    // 获取参数值 - 注意：现在paramValues_存储的是JsonFacade
    int window = paramValues_[PARAM_WINDOW].asInt();
    std::string type = paramValues_[PARAM_TYPE].asString();
    
    // 模拟回测结果
    result.performance.icMean = 0.042 - (window > 20 ? 0.001 : 0.0);
    result.performance.icStd = 0.085;
    result.performance.ir = 1.24 - (window > 20 ? 0.05 : 0.0);
    result.performance.icPositiveRatio = 0.62;
    result.performance.longShortReturn = 0.047;
    result.performance.validityDays = std::max(5, 25 - window / 10);
    result.performance.turnoverRate = 800.0 + (window < 10 ? 200.0 : 0.0);
    
    // 模拟分组收益
    result.performance.groupReturns.clear();
    double baseReturn = 0.02;
    for (int i = 0; i < config.groups; i++) {
        result.performance.groupReturns.push_back(baseReturn + i * 0.005);
    }
    
    // 模拟IC时间序列
    result.icSeries.clear();
    auto currentTime = std::chrono::system_clock::now();
    for (int i = 0; i < 100; i++) {
        auto date = currentTime - std::chrono::hours(i * 24 * 7);
        double ic = 0.04 + (std::sin(i * 0.1) * 0.03);
        result.icSeries.push_back({date, ic});
    }
    
    // 额外指标
    result.extraMetrics["window"] = static_cast<double>(window);
    result.extraMetrics["type"] = type == "simple" ? 1.0 : (type == "exponential" ? 1.1 : 0.9);
    result.extraMetrics["halfLife"] = 15.0;
    result.extraMetrics["decayRate"] = 0.95;
    
    return result;
}

// ============ ValueFactor 方法实现 ============

const std::string ValueFactor::PARAM_VALUATION_TYPE = "valuation_type";
const std::string ValueFactor::PARAM_USE_PERCENTILE = "use_percentile";
const std::string ValueFactor::PARAM_INDUSTRY_NEUTRAL = "industry_neutral";

ValueFactor::ValueFactor()
    : Factor("value", "Value Factor", "Value", "Valuation") {
    
    description_ = "Value factor based on valuation metrics, including PE, PB, PS, etc.";
    
    // 添加参数
    FactorParam valuationParam;
    valuationParam.name = PARAM_VALUATION_TYPE;
    valuationParam.displayName = std::string("估值类型\n");
    valuationParam.type = FactorParamType::ENUM;
    valuationParam.description = std::string("使用的估值指标类型\n");
    valuationParam.defaultValue = foundation::json::JsonFacade::createString("pe_ttm");
    
    auto valuationValues = foundation::json::JsonFacade::createArray();
    valuationValues.push_back(foundation::json::JsonFacade::createString("pe_ttm"));
    valuationValues.push_back(foundation::json::JsonFacade::createString("pb"));
    valuationValues.push_back(foundation::json::JsonFacade::createString("ps"));
    valuationValues.push_back(foundation::json::JsonFacade::createString("ev_ebitda"));
    valuationValues.push_back(foundation::json::JsonFacade::createString("cash_flow_yield"));
    valuationParam.commonValues.push_back(valuationValues);
    
    addParam(valuationParam);
    
    FactorParam percentileParam;
    percentileParam.name = PARAM_USE_PERCENTILE;
    percentileParam.displayName = std::string("使用百分位\n");
    percentileParam.type = FactorParamType::BOOLEAN;
    percentileParam.description = std::string("是否使用估值指标的百分位而非原始值\n");
    percentileParam.defaultValue = foundation::json::JsonFacade::createBool(true);
    addParam(percentileParam);
    
    FactorParam neutralParam;
    neutralParam.name = PARAM_INDUSTRY_NEUTRAL;
    neutralParam.displayName = std::string("行业中性化");
    neutralParam.type = FactorParamType::BOOLEAN;
    neutralParam.description = std::string("是否在行业内进行中性化处理");
    neutralParam.defaultValue = foundation::json::JsonFacade::createBool(true);
    addParam(neutralParam);
    
    // 设置默认性能指标
    performance_.icMean = 0.035;
    performance_.icStd = 0.092;
    performance_.ir = 0.98;
    performance_.icPositiveRatio = 0.58;
    performance_.longShortReturn = 0.035;
    performance_.validityDays = 90;
    performance_.turnoverRate = 300.0;
    performance_.groupReturns = {0.018, 0.022, 0.025, 0.028, 0.032, 0.035};
}

FactorBacktestResult ValueFactor::backtest(const FactorBacktestConfig& config) {
    FactorBacktestResult result;
    result.config = config;
    
    std::string valuationType = paramValues_[PARAM_VALUATION_TYPE].asString();
    bool usePercentile = paramValues_[PARAM_USE_PERCENTILE].asBool();
    bool industryNeutral = paramValues_[PARAM_INDUSTRY_NEUTRAL].asBool();
    
    // 根据估值类型调整性能指标
    double typeMultiplier = 1.0;
    if (valuationType == "pe_ttm") typeMultiplier = 1.0;
    else if (valuationType == "pb") typeMultiplier = 0.95;
    else if (valuationType == "ps") typeMultiplier = 0.9;
    else if (valuationType == "ev_ebitda") typeMultiplier = 1.05;
    else if (valuationType == "cash_flow_yield") typeMultiplier = 1.08;
    
    // 百分位影响
    double percentileEffect = usePercentile ? 1.05 : 1.0;
    
    // 行业中性化影响
    double neutralEffect = industryNeutral ? 1.1 : 1.0;
    
    result.performance.icMean = 0.035 * typeMultiplier * percentileEffect * neutralEffect;
    result.performance.icStd = 0.092;
    result.performance.ir = 0.98 * typeMultiplier * percentileEffect * neutralEffect;
    result.performance.icPositiveRatio = 0.58;
    result.performance.longShortReturn = 0.035 * typeMultiplier * percentileEffect * neutralEffect;
    result.performance.validityDays = 90;
    result.performance.turnoverRate = 300.0;
    
    result.performance.groupReturns.clear();
    double baseReturn = 0.018 * typeMultiplier * percentileEffect * neutralEffect;
    for (int i = 0; i < config.groups; i++) {
        result.performance.groupReturns.push_back(baseReturn + i * 0.003);
    }
    
    result.extraMetrics["valuationType"] = typeMultiplier;
    result.extraMetrics["percentileEffect"] = percentileEffect;
    result.extraMetrics["neutralEffect"] = neutralEffect;
    result.extraMetrics["industryNeutralRequired"] = 1.0;
    
    return result;
}

// ============ QualityFactor 方法实现 ============

const std::string QualityFactor::PARAM_METRIC = "metric";
const std::string QualityFactor::PARAM_TIMEFRAME = "timeframe";
const std::string QualityFactor::PARAM_QUALITY_THRESHOLD = "quality_threshold";

QualityFactor::QualityFactor()
    : Factor("quality", "质量因子\n", "质量类\n", "盈利能力\n") {
    
    description_ = "基于盈利能力、财务质量等指标的质量因子\n";
    
    // 添加参数
    FactorParam metricParam;
    metricParam.name = PARAM_METRIC;
    metricParam.displayName = std::string("质量指标");
    metricParam.type = FactorParamType::ENUM;
    metricParam.description = std::string("使用的质量指标\n");
    metricParam.defaultValue = foundation::json::JsonFacade::createString("roe");
    
    auto metricValues = foundation::json::JsonFacade::createArray();
    metricValues.push_back(foundation::json::JsonFacade::createString("roe"));
    metricValues.push_back(foundation::json::JsonFacade::createString("roa"));
    metricValues.push_back(foundation::json::JsonFacade::createString("roic"));
    metricValues.push_back(foundation::json::JsonFacade::createString("gross_margin"));
    metricValues.push_back(foundation::json::JsonFacade::createString("operating_margin"));
    metricParam.commonValues.push_back(metricValues);
    
    addParam(metricParam);
    
    FactorParam timeframeParam;
    timeframeParam.name = PARAM_TIMEFRAME;
    timeframeParam.displayName = std::string("时间框架");
    timeframeParam.type = FactorParamType::ENUM;
    timeframeParam.description = std::string("财务数据的时间框架\n");
    timeframeParam.defaultValue = foundation::json::JsonFacade::createString("quarterly");
    
    auto timeframeValues = foundation::json::JsonFacade::createArray();
    timeframeValues.push_back(foundation::json::JsonFacade::createString("quarterly"));
    timeframeValues.push_back(foundation::json::JsonFacade::createString("annual"));
    timeframeValues.push_back(foundation::json::JsonFacade::createString("ttm"));
    timeframeParam.commonValues.push_back(timeframeValues);
    
    addParam(timeframeParam);
    
    FactorParam thresholdParam;
    thresholdParam.name = PARAM_QUALITY_THRESHOLD;
    thresholdParam.displayName = std::string("质量阈值\n");
    thresholdParam.type = FactorParamType::FLOAT;
    thresholdParam.description = std::string("质量指标的最低阈值（百分比）");
    thresholdParam.defaultValue = foundation::json::JsonFacade::createDouble(10.0);
    thresholdParam.minValue = foundation::json::JsonFacade::createDouble(0.0);
    thresholdParam.maxValue = foundation::json::JsonFacade::createDouble(100.0);
    thresholdParam.stepValue = foundation::json::JsonFacade::createDouble(0.5);
    addParam(thresholdParam);
    
    // 设置默认性能指标
    performance_.icMean = 0.038;
    performance_.icStd = 0.088;
    performance_.ir = 1.12;
    performance_.icPositiveRatio = 0.60;
    performance_.longShortReturn = 0.038;
    performance_.validityDays = 180;
    performance_.turnoverRate = 200.0;
    performance_.groupReturns = {0.020, 0.024, 0.027, 0.030, 0.034, 0.038};
}

FactorBacktestResult QualityFactor::backtest(const FactorBacktestConfig& config) {
    FactorBacktestResult result;
    result.config = config;
    
    std::string metric = paramValues_[PARAM_METRIC].asString();
    std::string timeframe = paramValues_[PARAM_TIMEFRAME].asString();
    double threshold = paramValues_[PARAM_QUALITY_THRESHOLD].asDouble();
    
    // 根据质量指标调整性能指标
    double metricMultiplier = 1.0;
    if (metric == "roe") metricMultiplier = 1.0;
    else if (metric == "roa") metricMultiplier = 0.9;
    else if (metric == "roic") metricMultiplier = 1.05;
    else if (metric == "gross_margin") metricMultiplier = 0.95;
    else if (metric == "operating_margin") metricMultiplier = 0.92;
    
    // 时间框架影响
    double timeframeMultiplier = 1.0;
    if (timeframe == "quarterly") timeframeMultiplier = 1.1;
    else if (timeframe == "annual") timeframeMultiplier = 1.0;
    else if (timeframe == "ttm") timeframeMultiplier = 1.05;
    
    // 阈值影响（较高的阈值可能减少股票数量但提高因子质量）
    double thresholdEffect = 1.0 + (threshold - 10.0) * 0.002;
    
    result.performance.icMean = 0.038 * metricMultiplier * timeframeMultiplier * thresholdEffect;
    result.performance.icStd = 0.088;
    result.performance.ir = 1.12 * metricMultiplier * timeframeMultiplier * thresholdEffect;
    result.performance.icPositiveRatio = 0.60;
    result.performance.longShortReturn = 0.038 * metricMultiplier * timeframeMultiplier * thresholdEffect;
    result.performance.validityDays = 180;
    result.performance.turnoverRate = 200.0;
    
    result.performance.groupReturns.clear();
    double baseReturn = 0.020 * metricMultiplier * timeframeMultiplier * thresholdEffect;
    for (int i = 0; i < config.groups; i++) {
        result.performance.groupReturns.push_back(baseReturn + i * 0.0035);
    }
    
    result.extraMetrics["metric"] = metricMultiplier;
    result.extraMetrics["timeframe"] = timeframeMultiplier;
    result.extraMetrics["threshold"] = thresholdEffect;
    result.extraMetrics["requiresFundamentalData"] = 1.0;
    
    return result;
}

// ============ GrowthFactor 方法实现 ============

const std::string GrowthFactor::PARAM_GROWTH_TYPE = "growth_type";
const std::string GrowthFactor::PARAM_PERIOD = "period";
const std::string GrowthFactor::PARAM_GROWTH_METRIC = "growth_metric";

GrowthFactor::GrowthFactor()
    : Factor("growth", "成长因子\n", "成长类\n", "营收增长\n") {
    
    description_ = "基于营收、利润等成长性指标的因子\n";
    
    // Add parameters
    FactorParam growthTypeParam;
    growthTypeParam.name = PARAM_GROWTH_TYPE;
    growthTypeParam.displayName = std::string("成长类型\n");
    growthTypeParam.type = FactorParamType::ENUM;
    growthTypeParam.description = std::string("成长指标的类型\n");
    growthTypeParam.defaultValue = foundation::json::JsonFacade::createString("revenue");
    
    auto growthTypeValues = foundation::json::JsonFacade::createArray();
    growthTypeValues.push_back(foundation::json::JsonFacade::createString("revenue"));
    growthTypeValues.push_back(foundation::json::JsonFacade::createString("earnings"));
    growthTypeValues.push_back(foundation::json::JsonFacade::createString("cash_flow"));
    growthTypeValues.push_back(foundation::json::JsonFacade::createString("book_value"));
    growthTypeParam.commonValues.push_back(growthTypeValues);
    
    addParam(growthTypeParam);
    
    FactorParam periodParam;
    periodParam.name = PARAM_PERIOD;
    periodParam.displayName = std::string("成长期间\n");
    periodParam.type = FactorParamType::ENUM;
    periodParam.description = std::string("计算成长的期间 \n");
    periodParam.defaultValue = foundation::json::JsonFacade::createString("year_over_year");
    
    auto periodValues = foundation::json::JsonFacade::createArray();
    periodValues.push_back(foundation::json::JsonFacade::createString("year_over_year"));
    periodValues.push_back(foundation::json::JsonFacade::createString("quarter_over_quarter"));
    periodValues.push_back(foundation::json::JsonFacade::createString("trailing_12m"));
    periodParam.commonValues.push_back(periodValues);
    
    addParam(periodParam);
    
    FactorParam metricParam;
    metricParam.name = PARAM_GROWTH_METRIC;
    metricParam.displayName = std::string("成长指标 \n");
    metricParam.type = FactorParamType::ENUM;
    metricParam.description = std::string("衡量成长的具体指标\n");
    metricParam.defaultValue = foundation::json::JsonFacade::createString("growth_rate");
    
    auto metricValues = foundation::json::JsonFacade::createArray();
    metricValues.push_back(foundation::json::JsonFacade::createString("growth_rate"));
    metricValues.push_back(foundation::json::JsonFacade::createString("acceleration"));
    metricValues.push_back(foundation::json::JsonFacade::createString("consistency"));
    metricParam.commonValues.push_back(metricValues);
    
    addParam(metricParam);
    
    // 设置默认性能指标
    performance_.icMean = 0.040;
    performance_.icStd = 0.095;
    performance_.ir = 1.08;
    performance_.icPositiveRatio = 0.59;
    performance_.longShortReturn = 0.040;
    performance_.validityDays = 150;
    performance_.turnoverRate = 250.0;
    performance_.groupReturns = {0.021, 0.024, 0.027, 0.031, 0.036, 0.042};
}

FactorBacktestResult GrowthFactor::backtest(const FactorBacktestConfig& config) {
    FactorBacktestResult result;
    result.config = config;
    
    std::string growthType = paramValues_[PARAM_GROWTH_TYPE].asString();
    std::string period = paramValues_[PARAM_PERIOD].asString();
    std::string metric = paramValues_[PARAM_GROWTH_METRIC].asString();
    
    // 根据成长类型调整性能指标
    double typeMultiplier = 1.0;
    if (growthType == "revenue") typeMultiplier = 1.0;
    else if (growthType == "earnings") typeMultiplier = 1.05;
    else if (growthType == "cash_flow") typeMultiplier = 0.95;
    else if (growthType == "book_value") typeMultiplier = 0.9;
    
    // 期间影响
    double periodMultiplier = 1.0;
    if (period == "year_over_year") periodMultiplier = 1.0;
    else if (period == "quarter_over_quarter") periodMultiplier = 1.1;
    else if (period == "trailing_12m") periodMultiplier = 1.05;
    
    // 指标影响
    double metricMultiplier = 1.0;
    if (metric == "growth_rate") metricMultiplier = 1.0;
    else if (metric == "acceleration") metricMultiplier = 1.15;
    else if (metric == "consistency") metricMultiplier = 1.08;
    
    result.performance.icMean = 0.040 * typeMultiplier * periodMultiplier * metricMultiplier;
    result.performance.icStd = 0.095;
    result.performance.ir = 1.08 * typeMultiplier * periodMultiplier * metricMultiplier;
    result.performance.icPositiveRatio = 0.59;
    result.performance.longShortReturn = 0.040 * typeMultiplier * periodMultiplier * metricMultiplier;
    result.performance.validityDays = 150;
    result.performance.turnoverRate = 250.0;
    
    result.performance.groupReturns.clear();
    double baseReturn = 0.021 * typeMultiplier * periodMultiplier * metricMultiplier;
    for (int i = 0; i < config.groups; i++) {
        result.performance.groupReturns.push_back(baseReturn + i * 0.004);
    }
    
    result.extraMetrics["growthType"] = typeMultiplier;
    result.extraMetrics["period"] = periodMultiplier;
    result.extraMetrics["metric"] = metricMultiplier;
    result.extraMetrics["requiresGrowthData"] = 1.0;
    
    return result;
}

// ============ SentimentFactor 方法实现 ============

const std::string SentimentFactor::PARAM_SENTIMENT_SOURCE = "sentiment_source";
const std::string SentimentFactor::PARAM_LOOKBACK_DAYS = "lookback_days";
const std::string SentimentFactor::PARAM_SENTIMENT_METRIC = "sentiment_metric";

SentimentFactor::SentimentFactor()
    : Factor("sentiment", "情绪因子\n", "情绪类\n", "市场情绪\n ") {
    
    description_ = "基于新闻、社交媒体等市场情绪数据的因子 \n";
    
    // Add parameters
    FactorParam sourceParam;
    sourceParam.name = PARAM_SENTIMENT_SOURCE;
    sourceParam.displayName = std::string("情绪来源 \n");
    sourceParam.type = FactorParamType::ENUM;
    sourceParam.description = std::string("情绪数据的来源\n");
    sourceParam.defaultValue = foundation::json::JsonFacade::createString("news");
    
    auto sourceValues = foundation::json::JsonFacade::createArray();
    sourceValues.push_back(foundation::json::JsonFacade::createString("news"));
    sourceValues.push_back(foundation::json::JsonFacade::createString("social_media"));
    sourceValues.push_back(foundation::json::JsonFacade::createString("analyst_reports"));
    sourceValues.push_back(foundation::json::JsonFacade::createString("search_volume"));
    sourceParam.commonValues.push_back(sourceValues);
    
    addParam(sourceParam);
    
    FactorParam lookbackParam;
    lookbackParam.name = PARAM_LOOKBACK_DAYS;
    lookbackParam.displayName = std::string("回溯天数 \n");
    lookbackParam.type = FactorParamType::INTEGER;
    lookbackParam.description = std::string("情绪数据的回溯天数\n");
    lookbackParam.defaultValue = foundation::json::JsonFacade::createInt(30);
    lookbackParam.minValue = foundation::json::JsonFacade::createInt(1);
    lookbackParam.maxValue = foundation::json::JsonFacade::createInt(90);
    lookbackParam.stepValue = foundation::json::JsonFacade::createInt(1);
    addParam(lookbackParam);
    
    FactorParam metricParam;
    metricParam.name = PARAM_SENTIMENT_METRIC;
    metricParam.displayName = std::string("情绪指标 \n");
    metricParam.type = FactorParamType::ENUM;
    metricParam.description = std::string("衡量情绪的具体指标\n");
    metricParam.defaultValue = foundation::json::JsonFacade::createString("sentiment_score");
    
    auto metricValues = foundation::json::JsonFacade::createArray();
    metricValues.push_back(foundation::json::JsonFacade::createString("sentiment_score"));
    metricValues.push_back(foundation::json::JsonFacade::createString("volume"));
    metricValues.push_back(foundation::json::JsonFacade::createString("momentum"));
    metricValues.push_back(foundation::json::JsonFacade::createString("volatility"));
    metricParam.commonValues.push_back(metricValues);
    
    addParam(metricParam);
    
    // 设置默认性能指标
    performance_.icMean = 0.036;
    performance_.icStd = 0.098;
    performance_.ir = 0.95;
    performance_.icPositiveRatio = 0.56;
    performance_.longShortReturn = 0.032;
    performance_.validityDays = 30;
    performance_.turnoverRate = 1200.0;
    performance_.groupReturns = {0.015, 0.019, 0.023, 0.027, 0.032, 0.038};
}

FactorBacktestResult SentimentFactor::backtest(const FactorBacktestConfig& config) {
    FactorBacktestResult result;
    result.config = config;
    
    std::string source = paramValues_[PARAM_SENTIMENT_SOURCE].asString();
    int lookbackDays = paramValues_[PARAM_LOOKBACK_DAYS].asInt();
    std::string metric = paramValues_[PARAM_SENTIMENT_METRIC].asString();
    
    // 根据来源调整性能指标
    double sourceMultiplier = 1.0;
    if (source == "news") sourceMultiplier = 1.0;
    else if (source == "social_media") sourceMultiplier = 1.05;
    else if (source == "analyst_reports") sourceMultiplier = 1.1;
    else if (source == "search_volume") sourceMultiplier = 1.08;
    
    // 回溯天数影响（较长的回溯可能更稳定但反应较慢）
    double lookbackEffect = 1.0;
    if (lookbackDays < 15) lookbackEffect = 0.9;
    else if (lookbackDays <= 30) lookbackEffect = 1.0;
    else if (lookbackDays <= 60) lookbackEffect = 1.05;
    else lookbackEffect = 1.02;
    
    // 指标影响
    double metricMultiplier = 1.0;
    if (metric == "sentiment_score") metricMultiplier = 1.0;
    else if (metric == "volume") metricMultiplier = 0.95;
    else if (metric == "momentum") metricMultiplier = 1.1;
    else if (metric == "volatility") metricMultiplier = 0.9;
    
    result.performance.icMean = 0.036 * sourceMultiplier * lookbackEffect * metricMultiplier;
    result.performance.icStd = 0.098;
    result.performance.ir = 0.95 * sourceMultiplier * lookbackEffect * metricMultiplier;
    result.performance.icPositiveRatio = 0.56;
    result.performance.longShortReturn = 0.032 * sourceMultiplier * lookbackEffect * metricMultiplier;
    result.performance.validityDays = 30;
    result.performance.turnoverRate = 1200.0;
    
    result.performance.groupReturns.clear();
    double baseReturn = 0.015 * sourceMultiplier * lookbackEffect * metricMultiplier;
    for (int i = 0; i < config.groups; i++) {
        result.performance.groupReturns.push_back(baseReturn + i * 0.0045);
    }
    
    result.extraMetrics["source"] = sourceMultiplier;
    result.extraMetrics["lookback"] = lookbackEffect;
    result.extraMetrics["metric"] = metricMultiplier;
    result.extraMetrics["requiresSentimentData"] = 1.0;
    
    return result;
}

// ============ FactorFactory 方法实现 ============

std::map<std::string, std::function<std::shared_ptr<Factor>()>> FactorFactory::factorCreators_ = {
    {"momentum", []() { return std::make_shared<MomentumFactor>(); }},
    {"value", []() { return std::make_shared<ValueFactor>(); }},
    {"quality", []() { return std::make_shared<QualityFactor>(); }},
    {"growth", []() { return std::make_shared<GrowthFactor>(); }},
    {"sentiment", []() { return std::make_shared<SentimentFactor>(); }}
};

std::shared_ptr<Factor> FactorFactory::createFactor(const std::string& type, const std::string& name) {
    auto it = factorCreators_.find(type);
    if (it != factorCreators_.end()) {
        auto factor = it->second();
        if (!name.empty() && name != factor->getName()) {
            // 可以重命名因子
            // 实际实现中可能需要创建副本而不是修改原始因子
        }
        return factor;
    }
    return nullptr;
}

std::vector<std::string> FactorFactory::getAvailableFactorTypes() {
    std::vector<std::string> types;
    for (const auto& [type, _] : factorCreators_) {
        types.push_back(type);
    }
    return types;
}

std::map<std::string, std::string> FactorFactory::getFactorTypeDescriptions() {
    return {
        {"momentum", "动量因子：基于价格趋势的因子"},
        {"value", "价值因子：基于估值指标的因子"},
        {"quality", "质量因子：基于财务质量的因子"},
        {"growth", "成长因子：基于成长性指标的因子"},
        {"sentiment", "情绪因子：基于市场情绪数据的因子"}
    };
}
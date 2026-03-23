#include "Strategy.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <random>
#include <ctime>
using namespace AStockQuantEngine::Domain::Model;

// ============ StrategyParam 方法实现 ============

foundation::json::JsonFacade StrategyParam::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("name", foundation::json::JsonFacade::createString(name));
    j.set("displayName", foundation::json::JsonFacade::createString(displayName));
    j.set("type", foundation::json::JsonFacade::createInt(static_cast<int>(type)));
    j.set("description", foundation::json::JsonFacade::createString(description));
    j.set("defaultValue", defaultValue);
    j.set("minValue", minValue);
    j.set("maxValue", maxValue);
    j.set("stepValue", stepValue);
    j.set("required", foundation::json::JsonFacade::createBool(required));
    
    // 选项列表
    auto optionsArr = foundation::json::JsonFacade::createArray();
    for (const auto& option : options) {
        optionsArr.push_back(option);
    }
    j.set("options", optionsArr);
    
    // 常用值
    auto commonValuesArr = foundation::json::JsonFacade::createArray();
    for (const auto& value : commonValues) {
        commonValuesArr.push_back(value);
    }
    j.set("commonValues", commonValuesArr);
    
    return j;
}

StrategyParam StrategyParam::fromJson(const foundation::json::JsonFacade& json) {
    StrategyParam param;
    
    param.name = json.has("name") ? json.get("name").asString() : "";
    param.displayName = json.has("displayName") ? json.get("displayName").asString() : "";
    param.type = static_cast<StrategyParamType>(json.has("type") ? json.get("type").asInt() : 0);
    param.description = json.has("description") ? json.get("description").asString() : "";
    param.required = json.has("required") ? json.get("required").asBool() : false;
    
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
    
    // 解析选项
    if (json.has("options") && json.get("options").isArray()) {
        auto optionsJson = json.get("options");
        for (size_t i = 0; i < optionsJson.size(); i++) {
            param.options.push_back(optionsJson.at(i));
        }
    }
    
    // 解析常用值
    if (json.has("commonValues") && json.get("commonValues").isArray()) {
        auto commonValuesJson = json.get("commonValues");
        for (size_t i = 0; i < commonValuesJson.size(); i++) {
            param.commonValues.push_back(commonValuesJson.at(i));
        }
    }
    
    return param;
}

// ============ StrategyPerformance 方法实现 ============

foundation::json::JsonFacade StrategyPerformance::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("totalReturn", foundation::json::JsonFacade::createDouble(totalReturn));
    j.set("annualReturn", foundation::json::JsonFacade::createDouble(annualReturn));
    j.set("sharpeRatio", foundation::json::JsonFacade::createDouble(sharpeRatio));
    j.set("maxDrawdown", foundation::json::JsonFacade::createDouble(maxDrawdown));
    j.set("volatility", foundation::json::JsonFacade::createDouble(volatility));
    j.set("winRate", foundation::json::JsonFacade::createDouble(winRate));
    j.set("profitLossRatio", foundation::json::JsonFacade::createDouble(profitLossRatio));
    j.set("totalTrades", foundation::json::JsonFacade::createInt(totalTrades));
    j.set("avgHoldingPeriod", foundation::json::JsonFacade::createDouble(avgHoldingPeriod));
    j.set("alpha", foundation::json::JsonFacade::createDouble(alpha));
    j.set("beta", foundation::json::JsonFacade::createDouble(beta));
    j.set("informationRatio", foundation::json::JsonFacade::createDouble(informationRatio));
    j.set("benchmarkReturn", foundation::json::JsonFacade::createDouble(benchmarkReturn));
    
    return j;
}

StrategyPerformance StrategyPerformance::fromJson(const foundation::json::JsonFacade& json) {
    StrategyPerformance perf;
    
    perf.totalReturn = json.has("totalReturn") ? json.get("totalReturn").asDouble() : 0.0;
    perf.annualReturn = json.has("annualReturn") ? json.get("annualReturn").asDouble() : 0.0;
    perf.sharpeRatio = json.has("sharpeRatio") ? json.get("sharpeRatio").asDouble() : 0.0;
    perf.maxDrawdown = json.has("maxDrawdown") ? json.get("maxDrawdown").asDouble() : 0.0;
    perf.volatility = json.has("volatility") ? json.get("volatility").asDouble() : 0.0;
    perf.winRate = json.has("winRate") ? json.get("winRate").asDouble() : 0.0;
    perf.profitLossRatio = json.has("profitLossRatio") ? json.get("profitLossRatio").asDouble() : 0.0;
    perf.totalTrades = json.has("totalTrades") ? json.get("totalTrades").asInt() : 0;
    perf.avgHoldingPeriod = json.has("avgHoldingPeriod") ? json.get("avgHoldingPeriod").asDouble() : 0.0;
    perf.alpha = json.has("alpha") ? json.get("alpha").asDouble() : 0.0;
    perf.beta = json.has("beta") ? json.get("beta").asDouble() : 0.0;
    perf.informationRatio = json.has("informationRatio") ? json.get("informationRatio").asDouble() : 0.0;
    perf.benchmarkReturn = json.has("benchmarkReturn") ? json.get("benchmarkReturn").asDouble() : 0.0;
    
    return perf;
}

// ============ StrategyBacktestConfig 方法实现 ============

foundation::json::JsonFacade StrategyBacktestConfig::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("strategyId", foundation::json::JsonFacade::createString(strategyId));
    j.set("strategyCode", foundation::json::JsonFacade::createString(strategyCode));
    
    auto startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        startDate.time_since_epoch()).count();
    j.set("startDate", foundation::json::JsonFacade::createlong(static_cast<long>(startTime)));
    
    auto endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endDate.time_since_epoch()).count();
    j.set("endDate", foundation::json::JsonFacade::createlong(static_cast<long>(endTime)));
    
    j.set("initialCapital", foundation::json::JsonFacade::createDouble(initialCapital));
    j.set("benchmark", foundation::json::JsonFacade::createString(benchmark));
    j.set("commissionRate", foundation::json::JsonFacade::createDouble(commissionRate));
    j.set("slippageRate", foundation::json::JsonFacade::createDouble(slippageRate));
    j.set("parameters", parameters);
    
    return j;
}

StrategyBacktestConfig StrategyBacktestConfig::fromJson(const foundation::json::JsonFacade& json) {
    StrategyBacktestConfig config;
    
    config.strategyId = json.has("strategyId") ? json.get("strategyId").asString() : "";
    config.strategyCode = json.has("strategyCode") ? json.get("strategyCode").asString() : "";
    
    if (json.has("startDate")) {
        auto startTime = json.get("startDate").asLong();
        config.startDate = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(startTime));
    } else {
        config.startDate = std::chrono::system_clock::now();
    }
    
    if (json.has("endDate")) {
        auto endTime = json.get("endDate").asLong();
        config.endDate = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(endTime));
    } else {
        config.endDate = std::chrono::system_clock::now();
    }
    
    config.initialCapital = json.has("initialCapital") ? json.get("initialCapital").asDouble() : 1000000.0;
    config.benchmark = json.has("benchmark") ? json.get("benchmark").asString() : "000300.SH";
    config.commissionRate = json.has("commissionRate") ? json.get("commissionRate").asDouble() : 0.0003;
    config.slippageRate = json.has("slippageRate") ? json.get("slippageRate").asDouble() : 0.0001;
    config.parameters = json.has("parameters") ? json.get("parameters") : foundation::json::JsonFacade::createObject();
    
    return config;
}

// ============ StrategyBacktestResult 方法实现 ============

foundation::json::JsonFacade StrategyBacktestResult::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    j.set("config", config.toJson());
    j.set("performance", performance.toJson());
    
    // 权益曲线
    auto equityCurveArr = foundation::json::JsonFacade::createArray();
    for (const auto& [date, equity] : equityCurve) {
        auto item = foundation::json::JsonFacade::createObject();
        auto dateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            date.time_since_epoch()).count();
        item.set("date", foundation::json::JsonFacade::createlong(static_cast<long>(dateTime)));
        item.set("equity", foundation::json::JsonFacade::createDouble(equity));
        equityCurveArr.push_back(item);
    }
    j.set("equityCurve", equityCurveArr);
    
    // 回撤曲线
    auto drawdownCurveArr = foundation::json::JsonFacade::createArray();
    for (const auto& [date, drawdown] : drawdownCurve) {
        auto item = foundation::json::JsonFacade::createObject();
        auto dateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            date.time_since_epoch()).count();
        item.set("date", foundation::json::JsonFacade::createlong(static_cast<long>(dateTime)));
        item.set("drawdown", foundation::json::JsonFacade::createDouble(drawdown));
        drawdownCurveArr.push_back(item);
    }
    j.set("drawdownCurve", drawdownCurveArr);
    
    // 额外指标
    auto extraMetricsObj = foundation::json::JsonFacade::createObject();
    for (const auto& [key, value] : extraMetrics) {
        extraMetricsObj.set(key, foundation::json::JsonFacade::createDouble(value));
    }
    j.set("extraMetrics", extraMetricsObj);
    
    return j;
}

StrategyBacktestResult StrategyBacktestResult::fromJson(const foundation::json::JsonFacade& json) {
    StrategyBacktestResult result;
    
    if (json.has("config")) {
        result.config = StrategyBacktestConfig::fromJson(json.get("config"));
    }
    
    if (json.has("performance")) {
        result.performance = StrategyPerformance::fromJson(json.get("performance"));
    }
    
    // 解析权益曲线
    if (json.has("equityCurve") && json.get("equityCurve").isArray()) {
        auto equityCurveJson = json.get("equityCurve");
        for (size_t i = 0; i < equityCurveJson.size(); i++) {
            auto item = equityCurveJson.at(i);
            if (item.has("date") && item.has("equity")) {
                auto dateTime = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(item.get("date").asLong()));
                double equity = item.get("equity").asDouble();
                result.equityCurve.push_back({dateTime, equity});
            }
        }
    }
    
    // 解析回撤曲线
    if (json.has("drawdownCurve") && json.get("drawdownCurve").isArray()) {
        auto drawdownCurveJson = json.get("drawdownCurve");
        for (size_t i = 0; i < drawdownCurveJson.size(); i++) {
            auto item = drawdownCurveJson.at(i);
            if (item.has("date") && item.has("drawdown")) {
                auto dateTime = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(item.get("date").asLong()));
                double drawdown = item.get("drawdown").asDouble();
                result.drawdownCurve.push_back({dateTime, drawdown});
            }
        }
    }
    
    // 解析额外指标
    if (json.has("extraMetrics") && json.get("extraMetrics").isObject()) {
        auto extraMetricsJson = json.get("extraMetrics");
        // JsonFacade API 限制，无法遍历对象键
        // 暂时留空，需要时扩展 JsonFacade API
    }
    
    return result;
}

// ============ Strategy 方法实现 ============

Strategy::Strategy(const std::string& code, const std::string& name, StrategyType type)
    : code_(code), name_(name), type_(type) {
    // 生成随机ID
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::stringstream ss;
    ss << std::hex << dis(gen) << dis(gen);
    id_ = ss.str();
    
    description_ = "量化交易策略";
    author_ = "系统";
    version_ = "1.0.0";
    language_ = "Python";
    status_ = StrategyStatus::DRAFT;
    
    createdAt_ = std::chrono::system_clock::now();
    updatedAt_ = createdAt_;
    
    metadata_ = foundation::json::JsonFacade::createObject();
    performance_ = StrategyPerformance{};
    backtestConfig_ = StrategyBacktestConfig{};
}

void Strategy::addParam(const StrategyParam& param) {
    params_.push_back(param);
    paramValues_[param.name] = param.defaultValue;
}

void Strategy::setParamValue(const std::string& paramName, const foundation::json::JsonFacade& value) {
    paramValues_[paramName] = value;
}

foundation::json::JsonFacade Strategy::getParamValue(const std::string& paramName) const {
    auto it = paramValues_.find(paramName);
    if (it != paramValues_.end()) {
        return it->second;
    }
    return foundation::json::JsonFacade::createNull();
}

void Strategy::setMetadata(const std::string& key, const foundation::json::JsonFacade& value) {
    metadata_.set(key, value);
}

foundation::json::JsonFacade Strategy::getMetadata(const std::string& key) const {
    if (metadata_.has(key)) {
        return metadata_.get(key);
    }
    return foundation::json::JsonFacade::createNull();
}

foundation::json::JsonFacade Strategy::toJson() const {
    auto j = foundation::json::JsonFacade::createObject();
    
    // 基本信息
    j.set("id", foundation::json::JsonFacade::createString(id_));
    j.set("code", foundation::json::JsonFacade::createString(code_));
    j.set("name", foundation::json::JsonFacade::createString(name_));
    j.set("type", foundation::json::JsonFacade::createInt(static_cast<int>(type_)));
    j.set("description", foundation::json::JsonFacade::createString(description_));
    j.set("author", foundation::json::JsonFacade::createString(author_));
    j.set("version", foundation::json::JsonFacade::createString(version_));
    j.set("language", foundation::json::JsonFacade::createString(language_));
    j.set("status", foundation::json::JsonFacade::createInt(static_cast<int>(status_)));
    
    // 时间戳
    auto createdAtTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        createdAt_.time_since_epoch()).count();
    j.set("createdAt", foundation::json::JsonFacade::createlong(static_cast<long>(createdAtTime)));
    
    auto updatedAtTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        updatedAt_.time_since_epoch()).count();
    j.set("updatedAt", foundation::json::JsonFacade::createlong(static_cast<long>(updatedAtTime)));
    
    // 参数定义
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
    
    // 元数据
    j.set("metadata", metadata_);
    
    // 性能指标
    j.set("performance", performance_.toJson());
    
    // 回测配置
    j.set("backtestConfig", backtestConfig_.toJson());
    
    return j;
}

std::shared_ptr<Strategy> Strategy::fromJson(const foundation::json::JsonFacade& json) {
    std::string code = json.has("code") ? json.get("code").asString() : "";
    std::string name = json.has("name") ? json.get("name").asString() : "";
    StrategyType type = static_cast<StrategyType>(json.has("type") ? json.get("type").asInt() : 0);
    
    // 使用工厂创建策略
    auto strategy = StrategyFactory::createStrategy(type, code, name);
    if (!strategy) {
        // 如果工厂无法创建，创建一个通用的策略
        strategy = std::make_shared<Strategy>(code, name, type);
    }
    
    strategy->id_ = json.has("id") ? json.get("id").asString() : "";
    strategy->description_ = json.has("description") ? json.get("description").asString() : "";
    strategy->author_ = json.has("author") ? json.get("author").asString() : "";
    strategy->version_ = json.has("version") ? json.get("version").asString() : "";
    strategy->language_ = json.has("language") ? json.get("language").asString() : "";
    strategy->status_ = static_cast<StrategyStatus>(json.has("status") ? json.get("status").asInt() : 0);
    
    // 时间戳
    if (json.has("createdAt")) {
        auto createdAtTime = json.get("createdAt").asLong();
        strategy->createdAt_ = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(createdAtTime));
    }
    
    if (json.has("updatedAt")) {
        auto updatedAtTime = json.get("updatedAt").asLong();
        strategy->updatedAt_ = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(updatedAtTime));
    }
    
    // 参数定义
    if (json.has("params") && json.get("params").isArray()) {
        auto paramsJson = json.get("params");
        for (size_t i = 0; i < paramsJson.size(); i++) {
            strategy->addParam(StrategyParam::fromJson(paramsJson.at(i)));
        }
    }
    
    // 参数值
    if (json.has("paramValues") && json.get("paramValues").isObject()) {
        auto paramValuesJson = json.get("paramValues");
        // JsonFacade API 限制，无法遍历对象键
        // 暂时留空，需要时扩展 JsonFacade API
    }
    
    // 元数据
    if (json.has("metadata") && json.get("metadata").isObject()) {
        strategy->metadata_ = json.get("metadata");
    }
    
    // 性能指标
    if (json.has("performance") && json.get("performance").isObject()) {
        strategy->performance_ = StrategyPerformance::fromJson(json.get("performance"));
    }
    
    // 回测配置
    if (json.has("backtestConfig") && json.get("backtestConfig").isObject()) {
        strategy->backtestConfig_ = StrategyBacktestConfig::fromJson(json.get("backtestConfig"));
    }
    
    return strategy;
}

std::shared_ptr<Strategy> Strategy::createStrategy(StrategyType type, const std::string& code, const std::string& name) {
    return StrategyFactory::createStrategy(type, code, name);
}

std::string Strategy::strategyTypeToString(StrategyType type) {
    switch (type) {
        case StrategyType::ALPHA: return "ALPHA";
        case StrategyType::ARBITRAGE: return "ARBITRAGE";
        case StrategyType::TREND: return "TREND";
        case StrategyType::MEAN_REVERSION: return "MEAN_REVERSION";
        case StrategyType::HFT: return "HFT";
        case StrategyType::PORTFOLIO: return "PORTFOLIO";
        case StrategyType::CUSTOM: return "CUSTOM";
        default: return "UNKNOWN";
    }
}

StrategyType Strategy::stringToStrategyType(const std::string& str) {
    if (str == "ALPHA") return StrategyType::ALPHA;
    if (str == "ARBITRAGE") return StrategyType::ARBITRAGE;
    if (str == "TREND") return StrategyType::TREND;
    if (str == "MEAN_REVERSION") return StrategyType::MEAN_REVERSION;
    if (str == "HFT") return StrategyType::HFT;
    if (str == "PORTFOLIO") return StrategyType::PORTFOLIO;
    if (str == "CUSTOM") return StrategyType::CUSTOM;
    return StrategyType::CUSTOM;
}

std::string Strategy::strategyStatusToString(StrategyStatus status) {
    switch (status) {
        case StrategyStatus::DRAFT: return "DRAFT";
        case StrategyStatus::ACTIVE: return "ACTIVE";
        case StrategyStatus::INACTIVE: return "INACTIVE";
        case StrategyStatus::TESTING: return "TESTING";
        case StrategyStatus::ARCHIVED: return "ARCHIVED";
        case StrategyStatus::DELETED: return "DELETED";
        default: return "UNKNOWN";
    }
}

StrategyStatus Strategy::stringToStrategyStatus(const std::string& str) {
    if (str == "DRAFT") return StrategyStatus::DRAFT;
    if (str == "ACTIVE") return StrategyStatus::ACTIVE;
    if (str == "INACTIVE") return StrategyStatus::INACTIVE;
    if (str == "TESTING") return StrategyStatus::TESTING;
    if (str == "ARCHIVED") return StrategyStatus::ARCHIVED;
    if (str == "DELETED") return StrategyStatus::DELETED;
    return StrategyStatus::DRAFT;
}

// ============ TrendFollowingStrategy 常量定义和方法实现 ============

const std::string TrendFollowingStrategy::PARAM_FAST_PERIOD = "fast_period";
const std::string TrendFollowingStrategy::PARAM_SLOW_PERIOD = "slow_period";
const std::string TrendFollowingStrategy::PARAM_TAKE_PROFIT = "take_profit";
const std::string TrendFollowingStrategy::PARAM_STOP_LOSS = "stop_loss";
const std::string TrendFollowingStrategy::PARAM_POSITION_SIZE = "position_size";

TrendFollowingStrategy::TrendFollowingStrategy(const std::string& code, const std::string& name)
    : Strategy(code, name, StrategyType::TREND) {
    
    description_ = "趋势跟踪策略，基于快慢均线交叉信号进行交易";
    author_ = "系统";
    version_ = "1.0.0";
    language_ = "Python";
    
    // 添加标准参数
    StrategyParam fastPeriodParam;
    fastPeriodParam.name = PARAM_FAST_PERIOD;
    fastPeriodParam.displayName = "快线周期";
    fastPeriodParam.type = StrategyParamType::INTEGER;
    fastPeriodParam.description = "快线移动平均的周期(天数)";
    fastPeriodParam.defaultValue = foundation::json::JsonFacade::createInt(DEFAULT_FAST_PERIOD);
    fastPeriodParam.minValue = foundation::json::JsonFacade::createInt(1);
    fastPeriodParam.maxValue = foundation::json::JsonFacade::createInt(100);
    fastPeriodParam.stepValue = foundation::json::JsonFacade::createInt(1);
    fastPeriodParam.required = true;
    
    auto fastCommonValues = foundation::json::JsonFacade::createArray();
    fastCommonValues.push_back(foundation::json::JsonFacade::createInt(5));
    fastCommonValues.push_back(foundation::json::JsonFacade::createInt(10));
    fastCommonValues.push_back(foundation::json::JsonFacade::createInt(20));
    fastCommonValues.push_back(foundation::json::JsonFacade::createInt(30));
    fastPeriodParam.commonValues.push_back(fastCommonValues);
    
    addParam(fastPeriodParam);
    
    StrategyParam slowPeriodParam;
    slowPeriodParam.name = PARAM_SLOW_PERIOD;
    slowPeriodParam.displayName = "慢线周期";
    slowPeriodParam.type = StrategyParamType::INTEGER;
    slowPeriodParam.description = "慢线移动平均的周期(天数)";
    slowPeriodParam.defaultValue = foundation::json::JsonFacade::createInt(DEFAULT_SLOW_PERIOD);
    slowPeriodParam.minValue = foundation::json::JsonFacade::createInt(5);
    slowPeriodParam.maxValue = foundation::json::JsonFacade::createInt(250);
    slowPeriodParam.stepValue = foundation::json::JsonFacade::createInt(5);
    slowPeriodParam.required = true;
    
    auto slowCommonValues = foundation::json::JsonFacade::createArray();
    slowCommonValues.push_back(foundation::json::JsonFacade::createInt(20));
    slowCommonValues.push_back(foundation::json::JsonFacade::createInt(30));
    slowCommonValues.push_back(foundation::json::JsonFacade::createInt(50));
    slowCommonValues.push_back(foundation::json::JsonFacade::createInt(60));
    slowPeriodParam.commonValues.push_back(slowCommonValues);
    
    addParam(slowPeriodParam);
    
    StrategyParam takeProfitParam;
    takeProfitParam.name = PARAM_TAKE_PROFIT;
    takeProfitParam.displayName = "止盈比例";
    takeProfitParam.type = StrategyParamType::FLOAT;
    takeProfitParam.description = "止盈比例(百分比)";
    takeProfitParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_TAKE_PROFIT);
    takeProfitParam.minValue = foundation::json::JsonFacade::createDouble(0.01);
    takeProfitParam.maxValue = foundation::json::JsonFacade::createDouble(0.5);
    takeProfitParam.stepValue = foundation::json::JsonFacade::createDouble(0.01);
    takeProfitParam.required = false;
    
    auto takeProfitCommonValues = foundation::json::JsonFacade::createArray();
    takeProfitCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.05));
    takeProfitCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.10));
    takeProfitCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.15));
    takeProfitCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.20));
    takeProfitParam.commonValues.push_back(takeProfitCommonValues);
    
    addParam(takeProfitParam);
    
    StrategyParam stopLossParam;
    stopLossParam.name = PARAM_STOP_LOSS;
    stopLossParam.displayName = "止损比例";
    stopLossParam.type = StrategyParamType::FLOAT;
    stopLossParam.description = "止损比例(百分比)";
    stopLossParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_STOP_LOSS);
    stopLossParam.minValue = foundation::json::JsonFacade::createDouble(0.01);
    stopLossParam.maxValue = foundation::json::JsonFacade::createDouble(0.2);
    stopLossParam.stepValue = foundation::json::JsonFacade::createDouble(0.01);
    stopLossParam.required = false;
    
    auto stopLossCommonValues = foundation::json::JsonFacade::createArray();
    stopLossCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.02));
    stopLossCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.05));
    stopLossCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.08));
    stopLossCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.10));
    stopLossParam.commonValues.push_back(stopLossCommonValues);
    
    addParam(stopLossParam);
    
    StrategyParam positionSizeParam;
    positionSizeParam.name = PARAM_POSITION_SIZE;
    positionSizeParam.displayName = "仓位大小";
    positionSizeParam.type = StrategyParamType::FLOAT;
    positionSizeParam.description = "每次开仓的仓位比例(0-1)";
    positionSizeParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_POSITION_SIZE);
    positionSizeParam.minValue = foundation::json::JsonFacade::createDouble(0.01);
    positionSizeParam.maxValue = foundation::json::JsonFacade::createDouble(1.0);
    positionSizeParam.stepValue = foundation::json::JsonFacade::createDouble(0.05);
    positionSizeParam.required = true;
    
    auto positionSizeCommonValues = foundation::json::JsonFacade::createArray();
    positionSizeCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.05));
    positionSizeCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.10));
    positionSizeCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.20));
    positionSizeCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.30));
    positionSizeParam.commonValues.push_back(positionSizeCommonValues);
    
    addParam(positionSizeParam);
}

// ============ MeanReversionStrategy 常量定义和方法实现 ============

const std::string MeanReversionStrategy::PARAM_BOLL_PERIOD = "boll_period";
const std::string MeanReversionStrategy::PARAM_BOLL_STD = "boll_std";
const std::string MeanReversionStrategy::PARAM_POSITION_SIZE = "position_size";
const std::string MeanReversionStrategy::PARAM_REVERSION_THRESHOLD = "reversion_threshold";

MeanReversionStrategy::MeanReversionStrategy(const std::string& code, const std::string& name)
    : Strategy(code, name, StrategyType::MEAN_REVERSION) {
    
    description_ = "均值回归策略，基于布林带指标进行交易";
    author_ = "系统";
    version_ = "1.0.0";
    language_ = "Python";
    
    // 添加标准参数
    StrategyParam bollPeriodParam;
    bollPeriodParam.name = PARAM_BOLL_PERIOD;
    bollPeriodParam.displayName = "布林带周期";
    bollPeriodParam.type = StrategyParamType::INTEGER;
    bollPeriodParam.description = "布林带计算周期(天数)";
    bollPeriodParam.defaultValue = foundation::json::JsonFacade::createInt(DEFAULT_BOLL_PERIOD);
    bollPeriodParam.minValue = foundation::json::JsonFacade::createInt(5);
    bollPeriodParam.maxValue = foundation::json::JsonFacade::createInt(100);
    bollPeriodParam.stepValue = foundation::json::JsonFacade::createInt(1);
    bollPeriodParam.required = true;
    
    auto bollPeriodCommonValues = foundation::json::JsonFacade::createArray();
    bollPeriodCommonValues.push_back(foundation::json::JsonFacade::createInt(10));
    bollPeriodCommonValues.push_back(foundation::json::JsonFacade::createInt(20));
    bollPeriodCommonValues.push_back(foundation::json::JsonFacade::createInt(30));
    bollPeriodCommonValues.push_back(foundation::json::JsonFacade::createInt(50));
    bollPeriodParam.commonValues.push_back(bollPeriodCommonValues);
    
    addParam(bollPeriodParam);
    
    StrategyParam bollStdParam;
    bollStdParam.name = PARAM_BOLL_STD;
    bollStdParam.displayName = "布林带标准差";
    bollStdParam.type = StrategyParamType::FLOAT;
    bollStdParam.description = "布林带标准差倍数";
    bollStdParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_BOLL_STD);
    bollStdParam.minValue = foundation::json::JsonFacade::createDouble(1.0);
    bollStdParam.maxValue = foundation::json::JsonFacade::createDouble(3.0);
    bollStdParam.stepValue = foundation::json::JsonFacade::createDouble(0.1);
    bollStdParam.required = true;
    
    auto bollStdCommonValues = foundation::json::JsonFacade::createArray();
    bollStdCommonValues.push_back(foundation::json::JsonFacade::createDouble(1.5));
    bollStdCommonValues.push_back(foundation::json::JsonFacade::createDouble(2.0));
    bollStdCommonValues.push_back(foundation::json::JsonFacade::createDouble(2.5));
    bollStdCommonValues.push_back(foundation::json::JsonFacade::createDouble(3.0));
    bollStdParam.commonValues.push_back(bollStdCommonValues);
    
    addParam(bollStdParam);
    
    StrategyParam positionSizeParam;
    positionSizeParam.name = PARAM_POSITION_SIZE;
    positionSizeParam.displayName = "仓位大小";
    positionSizeParam.type = StrategyParamType::FLOAT;
    positionSizeParam.description = "每次开仓的仓位比例(0-1)";
    positionSizeParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_POSITION_SIZE);
    positionSizeParam.minValue = foundation::json::JsonFacade::createDouble(0.01);
    positionSizeParam.maxValue = foundation::json::JsonFacade::createDouble(1.0);
    positionSizeParam.stepValue = foundation::json::JsonFacade::createDouble(0.05);
    positionSizeParam.required = true;
    
    auto positionSizeCommonValues = foundation::json::JsonFacade::createArray();
    positionSizeCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.05));
    positionSizeCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.10));
    positionSizeCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.15));
    positionSizeCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.20));
    positionSizeParam.commonValues.push_back(positionSizeCommonValues);
    
    addParam(positionSizeParam);
    
    StrategyParam reversionThresholdParam;
    reversionThresholdParam.name = PARAM_REVERSION_THRESHOLD;
    reversionThresholdParam.displayName = "回归阈值";
    reversionThresholdParam.type = StrategyParamType::FLOAT;
    reversionThresholdParam.description = "触发回归交易的阈值(标准差倍数)";
    reversionThresholdParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_REVERSION_THRESHOLD);
    reversionThresholdParam.minValue = foundation::json::JsonFacade::createDouble(0.1);
    reversionThresholdParam.maxValue = foundation::json::JsonFacade::createDouble(2.0);
    reversionThresholdParam.stepValue = foundation::json::JsonFacade::createDouble(0.1);
    reversionThresholdParam.required = false;
    
    auto reversionThresholdCommonValues = foundation::json::JsonFacade::createArray();
    reversionThresholdCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.5));
    reversionThresholdCommonValues.push_back(foundation::json::JsonFacade::createDouble(1.0));
    reversionThresholdCommonValues.push_back(foundation::json::JsonFacade::createDouble(1.5));
    reversionThresholdCommonValues.push_back(foundation::json::JsonFacade::createDouble(2.0));
    reversionThresholdParam.commonValues.push_back(reversionThresholdCommonValues);
    
    addParam(reversionThresholdParam);
}

// ============ AlphaStrategy 常量定义和方法实现 ============

const std::string AlphaStrategy::PARAM_TOP_N = "top_n";
const std::string AlphaStrategy::PARAM_REBALANCE_DAYS = "rebalance_days";
const std::string AlphaStrategy::PARAM_MOMENTUM_PERIOD = "momentum_period";

AlphaStrategy::AlphaStrategy(const std::string& code, const std::string& name)
    : Strategy(code, name, StrategyType::ALPHA) {
    
    description_ = "Alpha策略，基于动量因子进行选股和组合优化";
    author_ = "系统";
    version_ = "1.0.0";
    language_ = "Python";
    
    // 添加标准参数
    StrategyParam topNParam;
    topNParam.name = PARAM_TOP_N;
    topNParam.displayName = "Top N股票";
    topNParam.type = StrategyParamType::INTEGER;
    topNParam.description = "选择排名前N的股票";
    topNParam.defaultValue = foundation::json::JsonFacade::createInt(DEFAULT_TOP_N);
    topNParam.minValue = foundation::json::JsonFacade::createInt(1);
    topNParam.maxValue = foundation::json::JsonFacade::createInt(50);
    topNParam.stepValue = foundation::json::JsonFacade::createInt(1);
    topNParam.required = true;
    
    auto topNCommonValues = foundation::json::JsonFacade::createArray();
    topNCommonValues.push_back(foundation::json::JsonFacade::createInt(5));
    topNCommonValues.push_back(foundation::json::JsonFacade::createInt(10));
    topNCommonValues.push_back(foundation::json::JsonFacade::createInt(20));
    topNCommonValues.push_back(foundation::json::JsonFacade::createInt(30));
    topNParam.commonValues.push_back(topNCommonValues);
    
    addParam(topNParam);
    
    StrategyParam rebalanceDaysParam;
    rebalanceDaysParam.name = PARAM_REBALANCE_DAYS;
    rebalanceDaysParam.displayName = "调仓周期";
    rebalanceDaysParam.type = StrategyParamType::INTEGER;
    rebalanceDaysParam.description = "调仓周期(天数)";
    rebalanceDaysParam.defaultValue = foundation::json::JsonFacade::createInt(DEFAULT_REBALANCE_DAYS);
    rebalanceDaysParam.minValue = foundation::json::JsonFacade::createInt(1);
    rebalanceDaysParam.maxValue = foundation::json::JsonFacade::createInt(60);
    rebalanceDaysParam.stepValue = foundation::json::JsonFacade::createInt(1);
    rebalanceDaysParam.required = true;
    
    auto rebalanceDaysCommonValues = foundation::json::JsonFacade::createArray();
    rebalanceDaysCommonValues.push_back(foundation::json::JsonFacade::createInt(5));
    rebalanceDaysCommonValues.push_back(foundation::json::JsonFacade::createInt(10));
    rebalanceDaysCommonValues.push_back(foundation::json::JsonFacade::createInt(20));
    rebalanceDaysCommonValues.push_back(foundation::json::JsonFacade::createInt(30));
    rebalanceDaysParam.commonValues.push_back(rebalanceDaysCommonValues);
    
    addParam(rebalanceDaysParam);
    
    StrategyParam momentumPeriodParam;
    momentumPeriodParam.name = PARAM_MOMENTUM_PERIOD;
    momentumPeriodParam.displayName = "动量周期";
    momentumPeriodParam.type = StrategyParamType::INTEGER;
    momentumPeriodParam.description = "计算动量的周期(天数)";
    momentumPeriodParam.defaultValue = foundation::json::JsonFacade::createInt(DEFAULT_MOMENTUM_PERIOD);
    momentumPeriodParam.minValue = foundation::json::JsonFacade::createInt(5);
    momentumPeriodParam.maxValue = foundation::json::JsonFacade::createInt(250);
    momentumPeriodParam.stepValue = foundation::json::JsonFacade::createInt(5);
    momentumPeriodParam.required = true;
    
    auto momentumPeriodCommonValues = foundation::json::JsonFacade::createArray();
    momentumPeriodCommonValues.push_back(foundation::json::JsonFacade::createInt(20));
    momentumPeriodCommonValues.push_back(foundation::json::JsonFacade::createInt(30));
    momentumPeriodCommonValues.push_back(foundation::json::JsonFacade::createInt(60));
    momentumPeriodCommonValues.push_back(foundation::json::JsonFacade::createInt(90));
    momentumPeriodParam.commonValues.push_back(momentumPeriodCommonValues);
    
    addParam(momentumPeriodParam);
}

// ============ ArbitrageStrategy 常量定义和方法实现 ============

const std::string ArbitrageStrategy::PARAM_SPREAD_THRESHOLD = "spread_threshold";
const std::string ArbitrageStrategy::PARAM_ENTRY_Z_SCORE = "entry_z_score";
const std::string ArbitrageStrategy::PARAM_EXIT_Z_SCORE = "exit_z_score";

ArbitrageStrategy::ArbitrageStrategy(const std::string& code, const std::string& name)
    : Strategy(code, name, StrategyType::ARBITRAGE) {
    
    description_ = "套利策略，基于价差Z-score进行统计套利";
    author_ = "系统";
    version_ = "1.0.0";
    language_ = "Python";
    
    // 添加标准参数
    StrategyParam spreadThresholdParam;
    spreadThresholdParam.name = PARAM_SPREAD_THRESHOLD;
    spreadThresholdParam.displayName = "价差阈值";
    spreadThresholdParam.type = StrategyParamType::FLOAT;
    spreadThresholdParam.description = "最小价差阈值(百分比)";
    spreadThresholdParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_SPREAD_THRESHOLD);
    spreadThresholdParam.minValue = foundation::json::JsonFacade::createDouble(0.001);
    spreadThresholdParam.maxValue = foundation::json::JsonFacade::createDouble(0.1);
    spreadThresholdParam.stepValue = foundation::json::JsonFacade::createDouble(0.001);
    spreadThresholdParam.required = true;
    
    auto spreadThresholdCommonValues = foundation::json::JsonFacade::createArray();
    spreadThresholdCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.01));
    spreadThresholdCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.02));
    spreadThresholdCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.03));
    spreadThresholdCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.05));
    spreadThresholdParam.commonValues.push_back(spreadThresholdCommonValues);
    
    addParam(spreadThresholdParam);
    
    StrategyParam entryZScoreParam;
    entryZScoreParam.name = PARAM_ENTRY_Z_SCORE;
    entryZScoreParam.displayName = "入场Z-score";
    entryZScoreParam.type = StrategyParamType::FLOAT;
    entryZScoreParam.description = "入场信号Z-score阈值";
    entryZScoreParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_ENTRY_Z_SCORE);
    entryZScoreParam.minValue = foundation::json::JsonFacade::createDouble(0.5);
    entryZScoreParam.maxValue = foundation::json::JsonFacade::createDouble(5.0);
    entryZScoreParam.stepValue = foundation::json::JsonFacade::createDouble(0.1);
    entryZScoreParam.required = true;
    
    auto entryZScoreCommonValues = foundation::json::JsonFacade::createArray();
    entryZScoreCommonValues.push_back(foundation::json::JsonFacade::createDouble(1.0));
    entryZScoreCommonValues.push_back(foundation::json::JsonFacade::createDouble(1.5));
    entryZScoreCommonValues.push_back(foundation::json::JsonFacade::createDouble(2.0));
    entryZScoreCommonValues.push_back(foundation::json::JsonFacade::createDouble(2.5));
    entryZScoreParam.commonValues.push_back(entryZScoreCommonValues);
    
    addParam(entryZScoreParam);
    
    StrategyParam exitZScoreParam;
    exitZScoreParam.name = PARAM_EXIT_Z_SCORE;
    exitZScoreParam.displayName = "出场Z-score";
    exitZScoreParam.type = StrategyParamType::FLOAT;
    exitZScoreParam.description = "出场信号Z-score阈值";
    exitZScoreParam.defaultValue = foundation::json::JsonFacade::createDouble(DEFAULT_EXIT_Z_SCORE);
    exitZScoreParam.minValue = foundation::json::JsonFacade::createDouble(0.1);
    exitZScoreParam.maxValue = foundation::json::JsonFacade::createDouble(3.0);
    exitZScoreParam.stepValue = foundation::json::JsonFacade::createDouble(0.1);
    exitZScoreParam.required = true;
    
    auto exitZScoreCommonValues = foundation::json::JsonFacade::createArray();
    exitZScoreCommonValues.push_back(foundation::json::JsonFacade::createDouble(0.5));
    exitZScoreCommonValues.push_back(foundation::json::JsonFacade::createDouble(1.0));
    exitZScoreCommonValues.push_back(foundation::json::JsonFacade::createDouble(1.5));
    exitZScoreCommonValues.push_back(foundation::json::JsonFacade::createDouble(2.0));
    exitZScoreParam.commonValues.push_back(exitZScoreCommonValues);
    
    addParam(exitZScoreParam);
}

// ============ StrategyFactory 方法实现 ============

std::map<StrategyType, std::function<std::shared_ptr<Strategy>()>> StrategyFactory::strategyCreators_ = {
    {StrategyType::TREND, []() { return std::make_shared<TrendFollowingStrategy>("", ""); }},
    {StrategyType::MEAN_REVERSION, []() { return std::make_shared<MeanReversionStrategy>("", ""); }},
    {StrategyType::ALPHA, []() { return std::make_shared<AlphaStrategy>("", ""); }},
    {StrategyType::ARBITRAGE, []() { return std::make_shared<ArbitrageStrategy>("", ""); }}
};

std::shared_ptr<Strategy> StrategyFactory::createStrategy(StrategyType type, const std::string& code, const std::string& name) {
    auto it = strategyCreators_.find(type);
    if (it != strategyCreators_.end()) {
        auto strategy = it->second();
        if (!code.empty()) {
            // 这里需要调用基类的setCode方法，但Strategy类没有提供
            // 暂时通过直接访问成员变量或创建新实例的方式解决
            // 简化处理：创建新实例
            switch (type) {
                case StrategyType::TREND:
                    strategy = std::make_shared<TrendFollowingStrategy>(code, name);
                    break;
                case StrategyType::MEAN_REVERSION:
                    strategy = std::make_shared<MeanReversionStrategy>(code, name);
                    break;
                case StrategyType::ALPHA:
                    strategy = std::make_shared<AlphaStrategy>(code, name);
                    break;
                case StrategyType::ARBITRAGE:
                    strategy = std::make_shared<ArbitrageStrategy>(code, name);
                    break;
                default:
                    strategy = std::make_shared<Strategy>(code, name, type);
                    break;
            }
        } else if (!name.empty()) {
            // 可以设置策略名称
            strategy->setName(name);
        }
        return strategy;
    }
    // 如果找不到对应的策略类型，创建一个通用的策略
    return std::make_shared<Strategy>(code.empty() ? "GENERIC" : code, 
                                      name.empty() ? "通用策略" : name, 
                                      type);
}

std::vector<std::string> StrategyFactory::getAvailableStrategyTypes() {
    std::vector<std::string> types;
    for (const auto& [type, _] : strategyCreators_) {
        types.push_back(Strategy::strategyTypeToString(type));
    }
    return types;
}

std::map<std::string, std::string> StrategyFactory::getStrategyTypeDescriptions() {
    return {
        {"TREND", "趋势跟踪策略：基于价格趋势进行交易"},
        {"MEAN_REVERSION", "均值回归策略：基于价格回归均值进行交易"},
        {"ALPHA", "Alpha策略：基于选股因子构建投资组合"},
        {"ARBITRAGE", "套利策略：基于价差套利机会进行交易"},
        {"CUSTOM", "自定义策略：用户自定义的交易策略"}
    };
}
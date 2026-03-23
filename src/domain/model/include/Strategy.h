#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <functional>
#include "foundation/json/json_facade.h"

namespace AStockQuantEngine::Domain::Model {

// 策略参数类型枚举
enum class StrategyParamType {
    INTEGER,
    FLOAT,
    BOOLEAN,
    ENUM,
    STRING,
    ARRAY,
    OBJECT
};

// 策略参数定义
struct StrategyParam {
    std::string name;
    std::string displayName;
    StrategyParamType type;
    std::string description;
    foundation::json::JsonFacade defaultValue;
    foundation::json::JsonFacade minValue;
    foundation::json::JsonFacade maxValue;
    foundation::json::JsonFacade stepValue;
    std::vector<foundation::json::JsonFacade> options;
    std::vector<foundation::json::JsonFacade> commonValues;
    bool required;
    
    foundation::json::JsonFacade toJson() const;
    static StrategyParam fromJson(const foundation::json::JsonFacade& json);
};

// 策略性能指标
struct StrategyPerformance {
    double totalReturn;          // 总收益率(%)
    double annualReturn;         // 年化收益率(%)
    double sharpeRatio;          // 夏普比率
    double maxDrawdown;          // 最大回撤(%)
    double volatility;           // 波动率
    double winRate;              // 胜率(%)
    double profitLossRatio;      // 盈亏比
    int totalTrades;             // 总交易次数
    double avgHoldingPeriod;     // 平均持仓周期(天)
    double alpha;                // Alpha
    double beta;                 // Beta
    double informationRatio;     // 信息比率
    double benchmarkReturn;      // 基准收益率(%)
    
    foundation::json::JsonFacade toJson() const;
    static StrategyPerformance fromJson(const foundation::json::JsonFacade& json);
};

// 策略回测配置
struct StrategyBacktestConfig {
    std::string strategyId;
    std::string strategyCode;
    std::chrono::system_clock::time_point startDate;
    std::chrono::system_clock::time_point endDate;
    double initialCapital;           // 初始资金
    std::string benchmark;           // 基准指数
    double commissionRate;           // 佣金费率
    double slippageRate;             // 滑点费率
    foundation::json::JsonFacade parameters;  // 策略参数
    
    foundation::json::JsonFacade toJson() const;
    static StrategyBacktestConfig fromJson(const foundation::json::JsonFacade& json);
};

// 策略回测结果
struct StrategyBacktestResult {
    StrategyBacktestConfig config;
    StrategyPerformance performance;
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> equityCurve;  // 权益曲线
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> drawdownCurve; // 回撤曲线
    std::map<std::string, double> extraMetrics;
    
    foundation::json::JsonFacade toJson() const;
    static StrategyBacktestResult fromJson(const foundation::json::JsonFacade& json);
};

// 策略状态枚举
enum class StrategyStatus {
    DRAFT,          // 草稿
    ACTIVE,         // 激活
    INACTIVE,       // 未激活
    TESTING,        // 测试中
    ARCHIVED,       // 已归档
    DELETED         // 已删除
};

// 策略类型枚举 (与数据库表strategy.strategy_type对应)
enum class StrategyType {
    ALPHA,              // Alpha策略
    ARBITRAGE,          // 套利策略
    TREND,              // 趋势策略
    MEAN_REVERSION,     // 均值回归
    HFT,                // 高频交易
    PORTFOLIO,          // 组合策略
    CUSTOM              // 自定义策略
};

// 策略类
class Strategy {
public:
    Strategy(const std::string& code, const std::string& name, StrategyType type);
    virtual ~Strategy() = default;
    
    // 基本信息
    std::string getId() const { return id_; }
    std::string getCode() const { return code_; }
    std::string getName() const { return name_; }
    StrategyType getType() const { return type_; }
    std::string getDescription() const { return description_; }
    StrategyStatus getStatus() const { return status_; }
    
    // 设置方法
    void setId(const std::string& id) { id_ = id; }
    void setName(const std::string& name) { name_ = name; }
    void setDescription(const std::string& desc) { description_ = desc; }
    void setStatus(StrategyStatus status) { status_ = status; }
    void setAuthor(const std::string& author) { author_ = author; }
    void setVersion(const std::string& version) { version_ = version; }
    void setLanguage(const std::string& language) { language_ = language; }
    
    // 参数管理
    void addParam(const StrategyParam& param);
    const std::vector<StrategyParam>& getParams() const { return params_; }
    void setParamValue(const std::string& paramName, const foundation::json::JsonFacade& value);
    foundation::json::JsonFacade getParamValue(const std::string& paramName) const;
    const std::map<std::string, foundation::json::JsonFacade>& getParamValues() const { return paramValues_; }
    
    // 元数据管理
    void setMetadata(const std::string& key, const foundation::json::JsonFacade& value);
    foundation::json::JsonFacade getMetadata(const std::string& key) const;
    const foundation::json::JsonFacade& getAllMetadata() const { return metadata_; }
    
    // 性能指标
    void setPerformance(const StrategyPerformance& perf) { performance_ = perf; }
    const StrategyPerformance& getPerformance() const { return performance_; }
    
    // 回测配置
    void setBacktestConfig(const StrategyBacktestConfig& config) { backtestConfig_ = config; }
    const StrategyBacktestConfig& getBacktestConfig() const { return backtestConfig_; }
    
    // 时间戳
    std::chrono::system_clock::time_point getCreatedAt() const { return createdAt_; }
    std::chrono::system_clock::time_point getUpdatedAt() const { return updatedAt_; }
    void setUpdatedAt() { updatedAt_ = std::chrono::system_clock::now(); }
    
    // 序列化
    foundation::json::JsonFacade toJson() const;
    static std::shared_ptr<Strategy> fromJson(const foundation::json::JsonFacade& json);
    
    // 工厂方法：根据类型创建策略
    static std::shared_ptr<Strategy> createStrategy(StrategyType type, const std::string& code, const std::string& name);
    
    // 类型转换
    static std::string strategyTypeToString(StrategyType type);
    static StrategyType stringToStrategyType(const std::string& str);
    static std::string strategyStatusToString(StrategyStatus status);
    static StrategyStatus stringToStrategyStatus(const std::string& str);

protected:
    // 内部构造函数，供工厂方法使用
    Strategy() = default;
    
    std::string id_;                              // UUID或数据库ID
    std::string code_;                            // 策略代码(唯一标识)
    std::string name_;                            // 策略名称
    StrategyType type_;                           // 策略类型
    std::string description_;                     // 策略描述
    std::string author_;                          // 作者
    std::string version_;                         // 版本
    std::string language_;                        // 实现语言
    StrategyStatus status_;                       // 策略状态
    
    std::vector<StrategyParam> params_;           // 参数定义
    std::map<std::string, foundation::json::JsonFacade> paramValues_;  // 参数值
    
    foundation::json::JsonFacade metadata_;       // 元数据(JSON对象)
    StrategyPerformance performance_;             // 性能指标
    StrategyBacktestConfig backtestConfig_;       // 回测配置
    
    std::chrono::system_clock::time_point createdAt_;  // 创建时间
    std::chrono::system_clock::time_point updatedAt_;  // 更新时间
    
private:
    Strategy(const Strategy&) = delete;
    Strategy& operator=(const Strategy&) = delete;
};

// 趋势跟踪策略
class TrendFollowingStrategy : public Strategy {
public:
    TrendFollowingStrategy(const std::string& code, const std::string& name);
    
    // 标准参数定义
    static const std::string PARAM_FAST_PERIOD;
    static const std::string PARAM_SLOW_PERIOD;
    static const std::string PARAM_TAKE_PROFIT;
    static const std::string PARAM_STOP_LOSS;
    static const std::string PARAM_POSITION_SIZE;
    
    // 默认参数值
    static constexpr int DEFAULT_FAST_PERIOD = 10;
    static constexpr int DEFAULT_SLOW_PERIOD = 30;
    static constexpr double DEFAULT_TAKE_PROFIT = 0.15;
    static constexpr double DEFAULT_STOP_LOSS = 0.05;
    static constexpr double DEFAULT_POSITION_SIZE = 0.1;
};

// 均值回归策略
class MeanReversionStrategy : public Strategy {
public:
    MeanReversionStrategy(const std::string& code, const std::string& name);
    
    // 标准参数定义
    static const std::string PARAM_BOLL_PERIOD;
    static const std::string PARAM_BOLL_STD;
    static const std::string PARAM_POSITION_SIZE;
    static const std::string PARAM_REVERSION_THRESHOLD;
    
    // 默认参数值
    static constexpr int DEFAULT_BOLL_PERIOD = 20;
    static constexpr double DEFAULT_BOLL_STD = 2.0;
    static constexpr double DEFAULT_POSITION_SIZE = 0.1;
    static constexpr double DEFAULT_REVERSION_THRESHOLD = 0.5;
};

// Alpha策略
class AlphaStrategy : public Strategy {
public:
    AlphaStrategy(const std::string& code, const std::string& name);
    
    // 标准参数定义
    static const std::string PARAM_TOP_N;
    static const std::string PARAM_REBALANCE_DAYS;
    static const std::string PARAM_MOMENTUM_PERIOD;
    
    // 默认参数值
    static constexpr int DEFAULT_TOP_N = 10;
    static constexpr int DEFAULT_REBALANCE_DAYS = 20;
    static constexpr int DEFAULT_MOMENTUM_PERIOD = 60;
};

// 套利策略
class ArbitrageStrategy : public Strategy {
public:
    ArbitrageStrategy(const std::string& code, const std::string& name);
    
    // 标准参数定义
    static const std::string PARAM_SPREAD_THRESHOLD;
    static const std::string PARAM_ENTRY_Z_SCORE;
    static const std::string PARAM_EXIT_Z_SCORE;
    
    // 默认参数值
    static constexpr double DEFAULT_SPREAD_THRESHOLD = 0.02;
    static constexpr double DEFAULT_ENTRY_Z_SCORE = 2.0;
    static constexpr double DEFAULT_EXIT_Z_SCORE = 0.5;
};

// 策略工厂
class StrategyFactory {
public:
    static std::shared_ptr<Strategy> createStrategy(StrategyType type, 
                                                   const std::string& code = "", 
                                                   const std::string& name = "");
    
    static std::vector<std::string> getAvailableStrategyTypes();
    static std::map<std::string, std::string> getStrategyTypeDescriptions();
    
private:
    static std::map<StrategyType, std::function<std::shared_ptr<Strategy>()>> strategyCreators_;
};

} // namespace AStockQuantEngine::Domain::Model
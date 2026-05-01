#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <functional>
#include "foundation/json/json_facade.h"

namespace AStockQuantEngine::Domain::Model {

// 因子参数类型枚举
enum class FactorParamType {
    INTEGER,
    FLOAT,
    BOOLEAN,
    ENUM,
    ARRAY,
    STRING
};

// 因子参数定义
struct FactorParam {
    std::string name;
    std::string displayName;
    FactorParamType type;
    std::string description;
    foundation::json::JsonFacade defaultValue;
    foundation::json::JsonFacade minValue;
    foundation::json::JsonFacade maxValue;
    foundation::json::JsonFacade stepValue;
    std::vector<foundation::json::JsonFacade> commonValues;
    
    foundation::json::JsonFacade toJson() const;
    static FactorParam fromJson(const foundation::json::JsonFacade& json);
};

// 因子性能指标
struct FactorPerformance {
    double icMean;          // IC均值
    double icStd;           // IC标准差
    double ir;              // 信息比率
    double icPositiveRatio; // IC>0比例
    double longShortReturn; // 多空收益
    int validityDays;       // 有效期
    double turnoverRate;    // 年化换手率
    std::vector<double> groupReturns; // 分组收益
    
    foundation::json::JsonFacade toJson() const;
};

// 因子回测配置
struct FactorBacktestConfig {
    std::string factorName;
    std::string stockPool;          // 股票池
    std::chrono::system_clock::time_point startDate;
    std::chrono::system_clock::time_point endDate;
    int forwardDays;                // 预测天数
    int groups;                     // 分组数量
    bool industryNeutral;           // 是否行业中性化
    bool marketCapNeutral;          // 是否市值中性化
    
    foundation::json::JsonFacade toJson() const;
};

// 因子回测结果
struct FactorBacktestResult {
    FactorBacktestConfig config;
    FactorPerformance performance;
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> icSeries; // IC时间序列
    std::map<std::string, double> extraMetrics;
    
    foundation::json::JsonFacade toJson() const;
};

// 因子类
class Factor {
public:
    Factor(const std::string& name, const std::string& displayName, 
           const std::string& majorCategory, const std::string& subCategory);
    virtual ~Factor() = default;
    
    // 基本信息
    std::string getName() const { return name_; }
    std::string getDisplayName() const { return displayName_; }
    std::string getMajorCategory() const { return majorCategory_; }
    std::string getSubCategory() const { return subCategory_; }
    std::string getDescription() const { return description_; }
    
    // 参数管理
    void setDescription(const std::string& desc) { description_ = desc; }
    void addParam(const FactorParam& param);
    const std::vector<FactorParam>& getParams() const { return params_; }
    void setParamValue(const std::string& paramName, const foundation::json::JsonFacade& value);
    foundation::json::JsonFacade getParamValue(const std::string& paramName) const;
    
    // 性能指标
    void setPerformance(const FactorPerformance& perf) { performance_ = perf; }
    const FactorPerformance& getPerformance() const { return performance_; }
    
    // 回测
    virtual FactorBacktestResult backtest(const FactorBacktestConfig& config) = 0;
    
    // 序列化
    foundation::json::JsonFacade toJson() const;
    static std::shared_ptr<Factor> fromJson(const foundation::json::JsonFacade& json);
    
protected:
    std::string name_;
    std::string displayName_;
    std::string majorCategory_;
    std::string subCategory_;
    std::string description_;
    std::vector<FactorParam> params_;
    std::map<std::string, foundation::json::JsonFacade> paramValues_;
    FactorPerformance performance_;
    
private:
    Factor(const Factor&) = delete;
    Factor& operator=(const Factor&) = delete;
};

// 动量因子
class MomentumFactor : public Factor {
public:
    MomentumFactor();
    
    // 参数定义
    static const std::string PARAM_WINDOW;
    static const std::string PARAM_TYPE;
    static const std::string PARAM_SMOOTHING;
    static const std::string PARAM_MIN_MOMENTUM;
    
    FactorBacktestResult backtest(const FactorBacktestConfig& config) override;
    
private:
    double calculateMomentumValue(double currentPrice, double historicalPrice) const;
};

// 价值因子
class ValueFactor : public Factor {
public:
    ValueFactor();
    
    // 参数定义
    static const std::string PARAM_VALUATION_METRICS;
    static const std::string PARAM_USE_PERCENTILE;
    static const std::string PARAM_INDUSTRY_NEUTRAL;
    
    FactorBacktestResult backtest(const FactorBacktestConfig& config) override;
};

// 质量因子
class QualityFactor : public Factor {
public:
    QualityFactor();
    
    // 参数定义
    static const std::string PARAM_METRIC;
    static const std::string PARAM_TIMEFRAME;
    static const std::string PARAM_QUALITY_THRESHOLD;
    
    FactorBacktestResult backtest(const FactorBacktestConfig& config) override;
};

// 成长因子
class GrowthFactor : public Factor {
public:
    GrowthFactor();
    
    // 参数定义
    static const std::string PARAM_GROWTH_METRICS;
    static const std::string PARAM_REVENUE_GROWTH_WEIGHT;
    static const std::string PARAM_NET_PROFIT_GROWTH_WEIGHT;
    static const std::string PARAM_DELTA_ROE_WEIGHT;
    static const std::string PARAM_SUE_WEIGHT;
    
    FactorBacktestResult backtest(const FactorBacktestConfig& config) override;
};

// 情绪因子
class SentimentFactor : public Factor {
public:
    SentimentFactor();
    
    // 参数定义
    static const std::string PARAM_SENTIMENT_SOURCE;
    static const std::string PARAM_LOOKBACK_DAYS;
    static const std::string PARAM_SENTIMENT_METRIC;
    
    FactorBacktestResult backtest(const FactorBacktestConfig& config) override;
};

// 因子工厂
class FactorFactory {
public:
    static std::shared_ptr<Factor> createFactor(const std::string& type, 
                                                const std::string& name = "");
    
    static std::vector<std::string> getAvailableFactorTypes();
    static std::map<std::string, std::string> getFactorTypeDescriptions();
    
private:
    static std::map<std::string, std::function<std::shared_ptr<Factor>()>> factorCreators_;
};

} // namespace AStockQuantEngine::Domain::Model

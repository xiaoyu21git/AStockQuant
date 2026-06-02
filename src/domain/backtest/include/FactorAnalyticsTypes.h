#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>

// BacktestResult定义在engine模块
#include "../../../engine/include/BacktestResult.h"
// Bar定义在domain::model模块
#include "Bar.h"

namespace domain::backtest {

// 分组方法枚举
enum class GroupingMethod {
    QUANTILE,      // 分位数分组
    EQUAL_VALUE,   // 等值分组
    CUSTOM         // 自定义分组
};

// 回测策略枚举
enum class BacktestStrategy {
    EQUAL_WEIGHT,  // 等权重
    FACTOR_WEIGHT, // 因子权重
    RISK_PARITY,   // 风险平价
    CUSTOM         // 自定义策略
};

// 回测状态枚举
enum class BacktestStatus {
    PENDING,       // 等待中
    RUNNING,       // 运行中
    COMPLETED,     // 已完成
    FAILED,        // 失败
    CANCELLED      // 已取消
};

// 因子分组结构
struct FactorGroup {
    int groupId;
    std::string groupName;
    double minFactorValue;
    double maxFactorValue;
    std::vector<std::string> stockCodes;
    size_t stockCount;
    
    FactorGroup() : groupId(0), minFactorValue(0.0), maxFactorValue(0.0), stockCount(0) {}
    
    bool isEmpty() const { return stockCodes.empty(); }
    double getAverageFactorValue() const { return (minFactorValue + maxFactorValue) / 2.0; }
};

// IC/IR计算结果结构
struct ICIRResult {
    double icValue;          // 信息系数
    double irValue;          // 信息比率
    double icTStat;          // IC的t统计量
    double icPValue;         // IC的p值
    double icPositiveRate;   // IC正比例
    bool isSignificant;      // 是否显著
    std::vector<double> icSeries; // 时间序列IC
    std::vector<double> irSeries; // 时间序列IR
    
    ICIRResult() : icValue(0.0), irValue(0.0), icTStat(0.0), icPValue(0.0), 
                   icPositiveRate(0.0), isSignificant(false) {}
    
    bool isValid() const { return !icSeries.empty(); }
};

// 因子回测配置结构
struct FactorBacktestConfig {
    // 基本配置
    std::string factorId;
    std::string factorName;
    std::string startDate;
    std::string endDate;
    
    // 数据集配置
    int dataSetId;                    // 数据集ID（-1表示使用默认数据源）
    std::string dataSetName;          // 数据集名称（可选）
    std::vector<std::string> allowedStockCodes;  // 允许的股票代码列表（由Controller从DataServiceCache获取）
    
    // 分组配置
    GroupingMethod groupingMethod;
    int numGroups;                    // 分组数量，如10表示十分位
    std::vector<double> customThresholds; // 自定义分组阈值
    
    // 回测配置
    BacktestStrategy strategy;
    double initialCapital;
    double transactionCost;           // 交易成本（百分比）
    double slippage;                  // 滑点（百分比）
    
    // 策略参数
    std::map<std::string, double> strategyParams;
    
    // 性能配置
    int maxThreads;                   // 最大线程数
    bool enableCache;                 // 是否启用缓存
    int cacheTTL;                     // 缓存过期时间（秒）
    
    FactorBacktestConfig() : 
        dataSetId(-1),
        groupingMethod(GroupingMethod::QUANTILE),
        numGroups(10),
        strategy(BacktestStrategy::EQUAL_WEIGHT),
        initialCapital(1000000.0),
        transactionCost(0.001),
        slippage(0.001),
        maxThreads(4),
        enableCache(true),
        cacheTTL(3600) {}
    
    // 验证方法
    bool validate() const;
    std::string getValidationErrors() const;
    
    // 检查是否使用数据集
    bool useDataSet() const { return dataSetId >= 0; }
};

// 因子回测结果结构
struct FactorBacktestResult {
    // 元数据
    std::string taskId;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    double executionTime; // 执行时间（秒）
    
    // 配置信息
    FactorBacktestConfig config;
    
    // 分组结果
    std::vector<FactorGroup> groups;
    
    // 回测结果
    std::vector<engine::BacktestResult> groupBacktestResults;
    
    // 绩效指标
    ICIRResult icirResult;
    
    // 汇总统计
    struct SummaryStats {
        double topGroupReturn;        // 最高分组收益
        double bottomGroupReturn;     // 最低分组收益
        double spreadReturn;          // 多空收益差
        double monotonicity;          // 单调性
        double discrimination;        // 区分度
        double winRate;               // 胜率
        double sharpeRatio;           // 夏普比率
        double maxDrawdown;           // 最大回撤
        
        SummaryStats() : 
            topGroupReturn(0.0),
            bottomGroupReturn(0.0),
            spreadReturn(0.0),
            monotonicity(0.0),
            discrimination(0.0),
            winRate(0.0),
            sharpeRatio(0.0),
            maxDrawdown(0.0) {}
    } summary;
    
    // 时间序列数据
    std::map<std::string, std::vector<double>> timeSeriesData;
    
    FactorBacktestResult() : executionTime(0.0) {}
    
    // 序列化方法
    std::string toJson() const;
    bool saveToFile(const std::string& filepath) const;
    static FactorBacktestResult loadFromFile(const std::string& filepath);
    
    // 计算汇总统计
    void calculateSummaryStats();
    
    // 获取总执行时间（毫秒）
    long long getTotalExecutionTimeMs() const {
        auto duration = endTime - startTime;
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
};

// 缓存键生成器
class CacheKeyGenerator {
public:
    static std::string generateFactorBacktestKey(const FactorBacktestConfig& config) {
        std::string key = "factor_backtest:";
        key += config.factorId + ":";
        key += config.startDate + ":";
        key += config.endDate + ":";
        key += std::to_string(static_cast<int>(config.groupingMethod)) + ":";
        key += std::to_string(config.numGroups) + ":";
        key += std::to_string(static_cast<int>(config.strategy));
        
        // 添加策略参数哈希
        for (const auto& param : config.strategyParams) {
            key += ":" + param.first + "=" + std::to_string(param.second);
        }
        
        return key;
    }
    
    static std::string generateFactorDataKey(const std::string& factorId, const std::string& date) {
        return "factor_data:" + factorId + ":" + date;
    }
    
    static std::string generateStockDataKey(const std::string& symbol, 
                                           const std::string& startDate, 
                                           const std::string& endDate) {
        return "stock_data:" + symbol + ":" + startDate + ":" + endDate;
    }
};

} // namespace domain::backtest
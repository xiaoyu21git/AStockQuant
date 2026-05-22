#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <functional>
#include <map>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "FactorBacktestTypes.h"
#include "../../factor/include/factor_enums.h"

// 前向声明
namespace engine {
    class BacktestEngine;
    class BacktestResult;
}

namespace domain::backtest {

// 策略回测配置
struct StrategyBacktestConfig {
    // 基础信息
    std::string strategyId;
    std::string strategyName;
    std::string startDate;
    std::string endDate;
    factor::MarketEnvironmentProfile marketEnvironmentProfile;
    
    // 资金配置
    double initialCapital;
    double commissionRate;    // 佣金率
    double slippageRate;      // 滑点率
    double taxRate;           // 税率
    
    // 标的配置
    std::vector<std::string> symbols;         // 交易标的
    std::string universeId;                   // 股票池ID
    std::vector<std::string> sectorFilters;   // 行业过滤
    std::vector<std::string> marketFilters;   // 市场过滤
    std::string dataSourceMode;               // 数据源模式: raw/cleaned/cache
    int datasetId;                            // 缓存集ID（cache模式可选）
    
    // 策略参数
    std::map<std::string, double> strategyParams;
    std::map<std::string, std::string> strategyOptions;
    
    // 风险控制
    double maxPositionRatio;          // 最大仓位比例
    double maxSinglePositionRatio;    // 单股最大仓位比例
    double maxDrawdownLimit;          // 最大回撤限制
    double stopLossRate;              // 止损比例
    
    // 交易设置
    bool enableShortSelling;          // 是否允许卖空
    int rebalanceFrequency;           // 调仓频率（天）
    bool useMarketOnClose;            // 是否使用收盘价交易
    
    // 性能配置
    int maxThreads;                   // 最大线程数
    bool enableCache;                 // 是否启用缓存
    int cacheTTL;                     // 缓存过期时间（秒）
    
    StrategyBacktestConfig() : 
        initialCapital(1000000.0),
        commissionRate(0.0003),      // 0.03%
        slippageRate(0.0002),        // 0.02%
        taxRate(0.001),              // 0.1%
        maxPositionRatio(1.0),       // 100%
        maxSinglePositionRatio(0.1), // 10%
        maxDrawdownLimit(0.2),       // 20%
        stopLossRate(0.05),          // 5%
        dataSourceMode("raw"),
        datasetId(-1),
        marketEnvironmentProfile(factor::MarketEnvironmentProfile::GENERIC_EQUITY),
        enableShortSelling(false),
        rebalanceFrequency(1),
        useMarketOnClose(true),
        maxThreads(4),
        enableCache(true),
        cacheTTL(3600) {}
    
    // 验证方法
    bool validate() const;
    std::string getValidationErrors() const;
    
    // 序列化方法
    std::string toJson() const;
    static StrategyBacktestConfig fromJson(const std::string& json);
};

// 策略回测结果
struct StrategyBacktestResult {
    // 元数据
    std::string taskId;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    double executionTime; // 执行时间（秒）
    
    // 配置信息
    StrategyBacktestConfig config;
    
    // 回测结果
    std::shared_ptr<engine::BacktestResult> backtestResult;
    
    // 绩效指标
    struct PerformanceMetrics {
        double totalReturn;           // 总收益率
        double annualizedReturn;      // 年化收益率
        double volatility;            // 波动率
        double sharpeRatio;           // 夏普比率
        double sortinoRatio;          // 索提诺比率
        double calmarRatio;           // 卡尔玛比率
        double maxDrawdown;           // 最大回撤
        double winRate;               // 胜率
        double profitFactor;          // 盈亏比
        double averageWin;            // 平均盈利
        double averageLoss;           // 平均亏损
        double alpha;                 // 阿尔法
        double beta;                  // 贝塔
        double informationRatio;      // 信息比率
        double trackingError;         // 跟踪误差
        
        PerformanceMetrics() : 
            totalReturn(0.0),
            annualizedReturn(0.0),
            volatility(0.0),
            sharpeRatio(0.0),
            sortinoRatio(0.0),
            calmarRatio(0.0),
            maxDrawdown(0.0),
            winRate(0.0),
            profitFactor(0.0),
            averageWin(0.0),
            averageLoss(0.0),
            alpha(0.0),
            beta(0.0),
            informationRatio(0.0),
            trackingError(0.0) {}
    } performance;
    
    // 交易统计
    struct TradeStatistics {
        int totalTrades;              // 总交易次数
        int winningTrades;            // 盈利交易次数
        int losingTrades;             // 亏损交易次数
        double totalProfit;           // 总盈利
        double totalLoss;             // 总亏损
        double largestWin;            // 最大盈利
        double largestLoss;           // 最大亏损
        double averageHoldingPeriod;  // 平均持仓天数
        
        TradeStatistics() : 
            totalTrades(0),
            winningTrades(0),
            losingTrades(0),
            totalProfit(0.0),
            totalLoss(0.0),
            largestWin(0.0),
            largestLoss(0.0),
            averageHoldingPeriod(0.0) {}
    } trades;
    
    // 风险指标
    struct RiskMetrics {
        double var95;                 // 95%置信度VaR
        double cvar95;                // 95%置信度CVaR
        double downsideDeviation;     // 下行偏差
        double upsideDeviation;       // 上行偏差
        double skewness;              // 偏度
        double kurtosis;              // 峰度
        std::map<std::string, double> sectorExposure; // 行业暴露
        std::map<std::string, double> factorExposure; // 因子暴露
        
        RiskMetrics() : 
            var95(0.0),
            cvar95(0.0),
            downsideDeviation(0.0),
            upsideDeviation(0.0),
            skewness(0.0),
            kurtosis(0.0) {}
    } risk;
    
    // 时间序列数据
    struct TimeSeriesData {
        std::vector<std::string> dates;
        std::vector<double> portfolioValues;
        std::vector<double> returns;
        std::vector<double> drawdowns;
        std::vector<double> positions;
        std::vector<double> cash;
        
        TimeSeriesData() {}
    } timeSeries;
    
    StrategyBacktestResult() : executionTime(0.0) {}
    
    // 序列化方法
    std::string toJson() const;
    bool saveToFile(const std::string& filepath) const;
    static StrategyBacktestResult loadFromFile(const std::string& filepath);
    
    // 计算绩效指标
    void calculatePerformanceMetrics();
    
    // 获取总执行时间（毫秒）
    long long getTotalExecutionTimeMs() const {
        auto duration = endTime - startTime;
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
};

// 策略回测服务
class StrategyBacktestService {
public:
    StrategyBacktestService();
    ~StrategyBacktestService();
    
    // 禁用拷贝和赋值
    StrategyBacktestService(const StrategyBacktestService&) = delete;
    StrategyBacktestService& operator=(const StrategyBacktestService&) = delete;
    
    // 主接口：运行策略回测
    std::future<StrategyBacktestResult> runStrategyBacktestAsync(
        const StrategyBacktestConfig& config);
    
    StrategyBacktestResult runStrategyBacktestSync(
        const StrategyBacktestConfig& config);

    StrategyBacktestResult runStrategyBacktestSyncWithProgress(
        const StrategyBacktestConfig& config,
        std::function<void(int, const std::string&)> progressCallback);
    
    // 批量回测
    std::future<std::vector<StrategyBacktestResult>> runBatchStrategyBacktestAsync(
        const std::vector<StrategyBacktestConfig>& configs);
    
    // 参数优化
    struct OptimizationResult {
        StrategyBacktestConfig optimalConfig;
        StrategyBacktestResult optimalResult;
        std::vector<std::pair<StrategyBacktestConfig, StrategyBacktestResult>> history;
        double bestScore;
        std::string optimizationMethod;
        
        OptimizationResult() : bestScore(0.0) {}
    };
    
    OptimizationResult optimizeStrategyParameters(
        const StrategyBacktestConfig& baseConfig,
        const std::map<std::string, std::pair<double, double>>& paramRanges,
        const std::string& objectiveFunction = "sharpe_ratio",
        int maxIterations = 100);
    
    // 多策略对比
    struct StrategyComparisonResult {
        std::vector<StrategyBacktestResult> results;
        std::map<std::string, double> comparisonMetrics;
        std::string bestStrategyId;
        
        StrategyComparisonResult() {}
    };
    
    StrategyComparisonResult compareStrategies(
        const std::vector<StrategyBacktestConfig>& configs);
    
    // 取消正在进行的回测
    void cancelBacktest(const std::string& taskId);
    
    // 获取回测状态
    BacktestStatus getBacktestStatus(const std::string& taskId);
    
    // 获取任务进度
    struct TaskProgress {
        std::string taskId;
        BacktestStatus status;
        int progress; // 0-100
        std::string message;
        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point estimatedCompletionTime;
        
        TaskProgress() : progress(0) {}
    };
    
    TaskProgress getTaskProgress(const std::string& taskId);
    
    // 获取所有任务状态
    std::vector<TaskProgress> getAllTaskProgress();
    
    // 清理已完成的任务
    void cleanupCompletedTasks(int maxAgeHours = 24);
    
    // 设置数据提供器
    void setDataProvider(std::shared_ptr<class StockDataProvider> provider);
    void setFactorProvider(std::shared_ptr<class FactorDataProvider> provider);
    
    // 设置缓存管理器
    void setCacheManager(std::shared_ptr<class CacheManager> cacheManager);
    
    // 设置回测引擎
    void setBacktestEngine(std::shared_ptr<engine::BacktestEngine> engine);
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
    
    // 内部任务结构
    struct BacktestTask {
        std::string taskId;
        StrategyBacktestConfig config;
        std::promise<StrategyBacktestResult> promise;
        std::future<StrategyBacktestResult> future;
        BacktestStatus status;
        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point endTime;
        std::string errorMessage;
        std::string statusMessage;
        int progress;
        std::atomic<bool> cancelled;
        
        BacktestTask(const StrategyBacktestConfig& cfg)
            : config(cfg)
            , status(BacktestStatus::PENDING)
            , progress(0)
            , cancelled(false) {
            future = promise.get_future();
            startTime = std::chrono::system_clock::now();
        }
    };
};

} // namespace domain::backtest
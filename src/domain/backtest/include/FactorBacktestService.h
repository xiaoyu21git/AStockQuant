#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <map>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "FactorBacktestTypes.h"
#include "FactorGrouper.h"
#include "GroupBacktester.h"
#include "ICIRCalculator.h"

namespace domain::backtest {

class FactorBacktestService {
public:
    FactorBacktestService();
    ~FactorBacktestService();
    
    // 禁用拷贝和赋值
    FactorBacktestService(const FactorBacktestService&) = delete;
    FactorBacktestService& operator=(const FactorBacktestService&) = delete;
    
    // 主接口：运行因子回测
    std::future<FactorBacktestResult> runFactorBacktestAsync(
        const FactorBacktestConfig& config);
    
    FactorBacktestResult runFactorBacktestSync(
        const FactorBacktestConfig& config);
    
    // 批量回测
    std::future<std::vector<FactorBacktestResult>> runBatchFactorBacktestAsync(
        const std::vector<FactorBacktestConfig>& configs);
    
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
    };
    
    TaskProgress getTaskProgress(const std::string& taskId);
    
    // 获取所有任务状态
    std::vector<TaskProgress> getAllTaskProgress();
    
    // 清理已完成的任务
    void cleanupCompletedTasks(int maxAgeHours = 24);
    
    // 设置缓存管理器
    void setCacheManager(std::shared_ptr<class CacheManager> cacheManager);
    
    // 设置因子数据提供器
    void setFactorDataProvider(std::shared_ptr<class FactorDataProvider> provider);
    
    // 设置股票数据提供器
    void setStockDataProvider(std::shared_ptr<class StockDataProvider> provider);
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
    
    // 内部任务结构
    struct BacktestTask {
        std::string taskId;
        FactorBacktestConfig config;
        std::promise<FactorBacktestResult> promise;
        std::future<FactorBacktestResult> future;
        BacktestStatus status;
        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point endTime;
        std::string errorMessage;
        int progress;
        std::atomic<bool> cancelled;
        
        BacktestTask(const FactorBacktestConfig& cfg)
            : config(cfg)
            , status(BacktestStatus::PENDING)
            , progress(0)
            , cancelled(false) {
            future = promise.get_future();
            startTime = std::chrono::system_clock::now();
        }
    };
};

// 缓存管理器接口
class CacheManager {
public:
    virtual ~CacheManager() = default;
    
    virtual std::optional<FactorBacktestResult> getFromCache(
        const std::string& cacheKey) = 0;
    
    virtual void putToCache(
        const std::string& cacheKey,
        const FactorBacktestResult& result,
        int ttl = 3600) = 0;
    
    virtual void invalidateCache(const std::string& pattern) = 0;
    
    virtual void clearAllCache() = 0;
};

// 因子数据提供器接口
class FactorDataProvider {
public:
    virtual ~FactorDataProvider() = default;
    
    virtual std::map<std::string, double> getFactorValues(
        const std::string& factorId,
        const std::string& date) = 0;
    
    virtual std::map<std::string, std::map<std::string, double>> getFactorValuesRange(
        const std::string& factorId,
        const std::string& startDate,
        const std::string& endDate) = 0;
    
    virtual std::vector<std::string> getAvailableDates(
        const std::string& factorId) = 0;
};

// 股票数据提供器接口
class StockDataProvider {
public:
    virtual ~StockDataProvider() = default;
    
    virtual std::vector<domain::model::Bar> getStockBars(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate) = 0;
    
    virtual std::map<std::string, std::vector<domain::model::Bar>> getMultipleStockBars(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate) = 0;
    
    virtual std::vector<std::string> getAvailableSymbols() = 0;
};

} // namespace domain::backtest
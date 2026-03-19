#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include "BacktestEngine.h"
#include "FactorBacktestTypes.h"
#include "StockDataProvider.h"

namespace domain::backtest {

class GroupBacktester {
public:
    GroupBacktester(
        std::shared_ptr<engine::BacktestEngine> backtestEngine,
        std::shared_ptr<StockDataProvider> stockDataProvider = nullptr);
    ~GroupBacktester();
    
    // Single group backtest
    engine::BacktestResult runGroupBacktest(
        const FactorGroup& group,
        const FactorBacktestConfig& config);
    
    // Multi-group parallel backtest
    std::vector<engine::BacktestResult> runGroupsBacktestParallel(
        const std::vector<FactorGroup>& groups,
        const FactorBacktestConfig& config,
        int maxThreads = 4);
    
    // Time series backtest (rolling window)
    std::vector<engine::BacktestResult> runTimeSeriesBacktest(
        const FactorGroup& group,
        const FactorBacktestConfig& config,
        int windowSize = 20);
    
    // Cancel ongoing backtest
    void cancelBacktest();
    
    // Get backtest status
    bool isRunning() const { return isRunning_; }
    int getProgress() const { return progress_; }
    
private:
    std::shared_ptr<engine::BacktestEngine> backtestEngine_;
    std::shared_ptr<StockDataProvider> stockDataProvider_;
    
    // Concurrency control
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool isRunning_;
    bool shouldCancel_;
    int progress_;
    
    // Get historical data for group stocks
    std::vector<domain::model::Bar> getGroupBars(
        const FactorGroup& group,
        const std::string& startDate,
        const std::string& endDate);
    
    // Build backtest configuration
    struct BacktestTaskConfig {
        std::string strategyName;
        double maxPositionRatio;
        double commissionRate;
        double slippageRate;
        double minVolume;
    };
    
    BacktestTaskConfig buildBacktestConfig(
        const FactorBacktestConfig& factorConfig);
    
    // Thread pool helper
    class ThreadPool {
    public:
        ThreadPool(size_t numThreads);
        ~ThreadPool();
        
        template<typename F, typename... Args>
        auto enqueue(F&& f, Args&&... args) 
            -> std::future<typename std::result_of<F(Args...)>::type>;
        
        void waitAll();
        
    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        std::mutex queueMutex_;
        std::condition_variable condition_;
        bool stop_;
    };
    
    // Execute tasks in parallel
    template<typename Task, typename Result>
    std::vector<Result> executeParallel(
        const std::vector<Task>& tasks,
        std::function<Result(const Task&)> worker,
        int maxThreads) {
        
        std::vector<Result> results;
        results.reserve(tasks.size());
        
        if (tasks.empty()) {
            return results;
        }
        
        // Create thread pool
        ThreadPool pool((std::min)(static_cast<size_t>(maxThreads), tasks.size()));
        std::vector<std::future<Result>> futures;
        
        // Submit tasks
        for (const auto& task : tasks) {
            futures.push_back(pool.enqueue(worker, task));
        }
        
        // Collect results
        for (auto& future : futures) {
            results.push_back(future.get());
        }
        
        return results;
    }
};

} // namespace domain::backtest
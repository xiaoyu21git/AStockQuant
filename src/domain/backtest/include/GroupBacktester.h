#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "BacktestEngine.h"
#include "FactorBacktestTypes.h"

namespace domain::backtest {

class GroupBacktester {
public:
    GroupBacktester(std::shared_ptr<engine::BacktestEngine> backtestEngine);
    ~GroupBacktester();
    
    // 单分组回测
    engine::BacktestResult runGroupBacktest(
        const FactorGroup& group,
        const FactorBacktestConfig& config);
    
    // 多分组并行回测
    std::vector<engine::BacktestResult> runGroupsBacktestParallel(
        const std::vector<FactorGroup>& groups,
        const FactorBacktestConfig& config,
        int maxThreads = 4);
    
    // 时间序列回测（滚动窗口）
    std::vector<engine::BacktestResult> runTimeSeriesBacktest(
        const FactorGroup& group,
        const FactorBacktestConfig& config,
        int windowSize = 20);
    
    // 取消正在进行的回测
    void cancelBacktest();
    
    // 获取回测状态
    bool isRunning() const { return isRunning_; }
    int getProgress() const { return progress_; }
    
private:
    std::shared_ptr<engine::BacktestEngine> backtestEngine_;
    
    // 并发控制
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool isRunning_;
    bool shouldCancel_;
    int progress_;
    
    // 获取分组股票的历史数据
    std::vector<domain::model::Bar> getGroupBars(
        const FactorGroup& group,
        const std::string& startDate,
        const std::string& endDate);
    
    // 构建回测配置
    struct BacktestTaskConfig {
        std::string strategyName;
        double maxPositionRatio;
        double commissionRate;
        double slippageRate;
        double minVolume;
    };
    
    BacktestTaskConfig buildBacktestConfig(
        const FactorBacktestConfig& factorConfig);
    
    // 线程池辅助方法
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
    
    // 并行执行任务
    template<typename Task, typename Result>
    std::vector<Result> executeParallel(
        const std::vector<Task>& tasks,
        std::function<Result(const Task&)> worker,
        int maxThreads);
};

} // namespace domain::backtest
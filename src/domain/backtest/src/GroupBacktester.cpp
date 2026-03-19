#include "GroupBacktester.h"
#include <algorithm>
#include <future>
#include <queue>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <foundation.h>

namespace domain::backtest {

GroupBacktester::GroupBacktester(
    std::shared_ptr<engine::BacktestEngine> backtestEngine,
    std::shared_ptr<StockDataProvider> stockDataProvider)
    : backtestEngine_(backtestEngine)
    , stockDataProvider_(stockDataProvider)
    , isRunning_(false)
    , shouldCancel_(false)
    , progress_(0) {
}

GroupBacktester::~GroupBacktester() {
    cancelBacktest();
}

engine::BacktestResult GroupBacktester::runGroupBacktest(
    const FactorGroup& group,
    const FactorBacktestConfig& config) {
    
    if (group.isEmpty()) {
        return engine::BacktestResult();
    }
    
    // 构建回测配置
    auto taskConfig = buildBacktestConfig(config);
    
    // 获取分组股票的历史数�?
    std::vector<domain::model::Bar> bars = getGroupBars(group, config.startDate, config.endDate);
    
    if (bars.empty()) {
        // 如果没有数据，返回空结果
        return engine::BacktestResult();
    }
    
    // 运行回测
    return backtestEngine_->run(
        bars,
        config.initialCapital,
        taskConfig.strategyName,
        taskConfig.maxPositionRatio,
        taskConfig.commissionRate,
        taskConfig.slippageRate,
        taskConfig.minVolume
    );
}

std::vector<engine::BacktestResult> GroupBacktester::runGroupsBacktestParallel(
    const std::vector<FactorGroup>& groups,
    const FactorBacktestConfig& config,
    int maxThreads) {
    
    std::vector<engine::BacktestResult> results;
    
    if (groups.empty()) {
        return results;
    }
    
    // 设置运行状�?
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isRunning_ = true;
        shouldCancel_ = false;
        progress_ = 0;
    }
    
    try {
        // 使用foundation全局线程池进行并行回�?
        auto& threadPool = FOUNDATION_THREADS;
        
        std::vector<std::future<engine::BacktestResult>> futures;
        futures.reserve(groups.size());
        
        // 提交所有任�?
        for (size_t i = 0; i < groups.size(); ++i) {
            if (shouldCancel_) {
                break;
            }
            
            // 使用线程池的submit方法提交任务
            futures.push_back(threadPool.submit(
                [this, &groups, i, &config]() {
                    return runGroupBacktest(groups[i], config);
                }
            ));
            
            // 更新进度
            {
                std::lock_guard<std::mutex> lock(mutex_);
                progress_ = static_cast<int>((i + 1) * 100 / groups.size());
            }
        }
        
        // 收集结果
        for (auto& future : futures) {
            if (shouldCancel_) {
                future.wait();
            } else {
                results.push_back(future.get());
            }
        }
        
    } catch (const std::exception& e) {
        // 处理异常
        std::lock_guard<std::mutex> lock(mutex_);
        isRunning_ = false;
        throw;
    }
    
    // 重置状�?
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isRunning_ = false;
        progress_ = 100;
    }
    
    return results;
}

std::vector<engine::BacktestResult> GroupBacktester::runTimeSeriesBacktest(
    const FactorGroup& group,
    const FactorBacktestConfig& config,
    int windowSize) {
    
    std::vector<engine::BacktestResult> results;
    
    if (group.isEmpty() || windowSize <= 0) {
        return results;
    }
    
    // 这里实现滚动窗口回测逻辑
    // 由于需要时间序列数据，这里先返回空结果
    
    return results;
}

void GroupBacktester::cancelBacktest() {
    std::lock_guard<std::mutex> lock(mutex_);
    shouldCancel_ = true;
    cv_.notify_all();
}

std::vector<domain::model::Bar> GroupBacktester::getGroupBars(
    const FactorGroup& group,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::vector<domain::model::Bar> allBars;
    
    if (group.isEmpty()) {
        return allBars;
    }
    
    try {
        // 获取分组中的股票代码
        const std::vector<std::string>& symbols = group.stockCodes;
        
        if (symbols.empty()) {
            return allBars;
        }
        
        // 检查是否有数据提供�?
        if (!stockDataProvider_) {
            INTERNAL_WARN_STREAM << "No stock data provider available - returning empty data";
            return allBars;
        }
        
        // 获取所有股票的数据
        auto multipleBars = stockDataProvider_->getMultipleStockBars(symbols, startDate, endDate);
        
        // 合并所有股票的数据
        for (const auto& symbol : symbols) {
            auto it = multipleBars.find(symbol);
            if (it != multipleBars.end()) {
                const auto& bars = it->second;
                allBars.insert(allBars.end(), bars.begin(), bars.end());
            }
        }
        
        INTERNAL_INFO_STREAM << "Retrieved " << allBars.size() << " bars for group " 
                            << group.groupName << " from " << startDate << " to " << endDate;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get group bars: " << e.what();
    }
    
    return allBars;
}

GroupBacktester::BacktestTaskConfig GroupBacktester::buildBacktestConfig(
    const FactorBacktestConfig& factorConfig) {
    
    BacktestTaskConfig taskConfig;
    
    // 根据策略类型选择策略名称
    switch (factorConfig.strategy) {
        case BacktestStrategy::EQUAL_WEIGHT:
            taskConfig.strategyName = "EqualWeight";
            break;
        case BacktestStrategy::FACTOR_WEIGHT:
            taskConfig.strategyName = "FactorWeight";
            break;
        case BacktestStrategy::RISK_PARITY:
            taskConfig.strategyName = "RiskParity";
            break;
        case BacktestStrategy::CUSTOM:
            taskConfig.strategyName = "Custom";
            break;
        default:
            taskConfig.strategyName = "EqualWeight";
    }
    
    // 设置回测参数
    taskConfig.maxPositionRatio = 1.0; // 默认满仓
    taskConfig.commissionRate = factorConfig.transactionCost;
    taskConfig.slippageRate = factorConfig.slippage;
    taskConfig.minVolume = 0.0; // 默认不限制最小成交量
    
    // 从策略参数中获取特定参数
    auto it = factorConfig.strategyParams.find("max_position_ratio");
    if (it != factorConfig.strategyParams.end()) {
        taskConfig.maxPositionRatio = it->second;
    }
    
    it = factorConfig.strategyParams.find("min_volume");
    if (it != factorConfig.strategyParams.end()) {
        taskConfig.minVolume = it->second;
    }
    
    return taskConfig;
}

// ThreadPool实现
GroupBacktester::ThreadPool::ThreadPool(size_t numThreads) : stop_(false) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    condition_.wait(lock, [this] {
                        return stop_ || !tasks_.empty();
                    });
                    
                    if (stop_ && tasks_.empty()) {
                        return;
                    }
                    
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

GroupBacktester::ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        stop_ = true;
    }
    condition_.notify_all();
    
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

template<typename F, typename... Args>
auto GroupBacktester::ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using return_type = typename std::result_of<F(Args...)>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        if (stop_) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        tasks_.emplace([task]() { (*task)(); });
    }
    condition_.notify_one();
    return res;
}

void GroupBacktester::ThreadPool::waitAll() {
    // 等待所有任务完�?
    while (true) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        if (tasks_.empty()) {
            break;
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}


} // namespace domain::backtest

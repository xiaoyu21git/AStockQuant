#include "FactorBacktestService.h"
#include "DatabaseStockDataProvider.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>
#include <QDebug>
#include <foundation.h>

namespace domain::backtest {

// PIMPL实现类
class FactorBacktestService::Impl {
public:
    Impl() {
        // 初始化成员变量
        nextTaskId_ = 1;
        cacheManager_ = nullptr;
        factorDataProvider_ = nullptr;
        stockDataProvider_ = nullptr;
        
        // 创建回测引擎
        backtestEngine_ = std::make_shared<engine::BacktestEngine>();
        
        // 创建组件
        factorGrouper_ = std::make_unique<FactorGrouper>();
        // groupBacktester_ 将在 setStockDataProvider 中创建
        icirCalculator_ = std::make_unique<ICIRCalculator>();
    }
    
    ~Impl() {
        // 停止所有任务
        stopAllTasks();
    }
    
    std::future<FactorBacktestResult> runFactorBacktestAsync(
        const FactorBacktestConfig& config) {
        
        // 生成任务ID
        std::string taskId = generateTaskId();
        
        // 创建任务
        auto task = std::make_shared<BacktestTask>(config);
        task->taskId = taskId;
        
        // 存储任务
        {
            std::lock_guard<std::mutex> lock(tasksMutex_);
            tasks_[taskId] = task;
        }
        
        // 启动异步执行
        std::future<FactorBacktestResult> future = std::async(
            std::launch::async,
            [this, task]() {
                return executeBacktestTask(task);
            }
        );
        
        return future;
    }
    
    FactorBacktestResult runFactorBacktestSync(
        const FactorBacktestConfig& config) {
        
        // 检查缓存
        if (config.enableCache && cacheManager_) {
            std::string cacheKey = CacheKeyGenerator::generateFactorBacktestKey(config);
            auto cachedResult = cacheManager_->getFromCache<FactorBacktestResult>(cacheKey);
            if (cachedResult) {
                return *cachedResult;
            }
        }
        
        // 执行回测
        auto result = executeBacktest(config);
        
        // 保存到缓存
        if (config.enableCache && cacheManager_) {
            std::string cacheKey = CacheKeyGenerator::generateFactorBacktestKey(config);
            cacheManager_->putToCache<FactorBacktestResult>(cacheKey, result, config.cacheTTL);
        }
        
        return result;
    }
    
    std::future<std::vector<FactorBacktestResult>> runBatchFactorBacktestAsync(
        const std::vector<FactorBacktestConfig>& configs) {
        
        return std::async(
            std::launch::async,
            [this, configs]() {
                std::vector<FactorBacktestResult> results;
                results.reserve(configs.size());
                
                for (const auto& config : configs) {
                    results.push_back(runFactorBacktestSync(config));
                }
                
                return results;
            }
        );
    }
    
    void cancelBacktest(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto it = tasks_.find(taskId);
        if (it != tasks_.end()) {
            it->second->cancelled = true;
            it->second->status = BacktestStatus::CANCELLED;
        }
    }
    
    BacktestStatus getBacktestStatus(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto it = tasks_.find(taskId);
        if (it != tasks_.end()) {
            return it->second->status;
        }
        return BacktestStatus::FAILED;
    }
    
    FactorBacktestService::TaskProgress getTaskProgress(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto it = tasks_.find(taskId);
        if (it != tasks_.end()) {
            auto& task = it->second;
            TaskProgress progress;
            progress.taskId = taskId;
            progress.status = task->status;
            progress.progress = task->progress;
            progress.message = task->errorMessage;
            progress.startTime = task->startTime;
            
            // 估算完成时间（简单线性估算）
            if (task->progress > 0) {
                auto elapsed = std::chrono::system_clock::now() - task->startTime;
                auto estimatedTotal = elapsed * 100 / task->progress;
                progress.estimatedCompletionTime = task->startTime + estimatedTotal;
            } else {
                progress.estimatedCompletionTime = std::chrono::system_clock::now();
            }
            
            return progress;
        }
        
        // 返回默认值
        TaskProgress progress;
        progress.taskId = taskId;
        progress.status = BacktestStatus::FAILED;
        progress.progress = 0;
        progress.message = "Task not found";
        return progress;
    }
    
    std::vector<FactorBacktestService::TaskProgress> getAllTaskProgress() {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        std::vector<TaskProgress> allProgress;
        allProgress.reserve(tasks_.size());
        
        for (const auto& kv : tasks_) {
            allProgress.push_back(getTaskProgress(kv.first));
        }
        
        return allProgress;
    }
    
    void cleanupCompletedTasks(int maxAgeHours) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto now = std::chrono::system_clock::now();
        auto maxAge = std::chrono::hours(maxAgeHours);
        
        for (auto it = tasks_.begin(); it != tasks_.end(); ) {
            auto& task = it->second;
            bool isCompleted = (task->status == BacktestStatus::COMPLETED || 
                               task->status == BacktestStatus::FAILED || 
                               task->status == BacktestStatus::CANCELLED);
            
            if (isCompleted && (now - task->endTime) > maxAge) {
                it = tasks_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void setCacheManager(std::shared_ptr<CacheManager> cacheManager) {
        cacheManager_ = cacheManager;
    }
    
    void setFactorDataProvider(std::shared_ptr<FactorDataProvider> provider) {
        factorDataProvider_ = provider;
    }
    
    void setStockDataProvider(std::shared_ptr<StockDataProvider> provider) {
        stockDataProvider_ = provider;
        
        // 创建GroupBacktester，传递DatabaseStockDataProvider
        // 注意：这里需要将StockDataProvider转换为DatabaseStockDataProvider
        // 由于接口不同，这里需要类型转换
        std::shared_ptr<DatabaseStockDataProvider> dbProvider = nullptr;
        
        // 尝试动态转换
        if (provider) {
            // 这里假设provider实际上是DatabaseStockDataProvider
            // 在实际应用中，应该使用dynamic_pointer_cast
            dbProvider = std::dynamic_pointer_cast<DatabaseStockDataProvider>(provider);
            
            if (!dbProvider) {
                // 如果转换失败，尝试使用适配器
                INTERNAL_WARN_STREAM << "StockDataProvider is not a DatabaseStockDataProvider, "
                                    << "GroupBacktester may not work correctly";
            }
        }
        
        // 创建GroupBacktester
        groupBacktester_ = std::make_unique<GroupBacktester>(backtestEngine_, dbProvider);
    }
    
private:
    // 生成任务ID
    std::string generateTaskId() {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        std::stringstream ss;
        ss << "factor_backtest_" << nextTaskId_++ << "_" 
           << foundation::Uuid{}.to_string();
        return ss.str();
    }
    
    // 停止所有任务
    void stopAllTasks() {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        for (auto& kv : tasks_) {
            kv.second->cancelled = true;
        }
    }
    
    // 执行回测任务
    FactorBacktestResult executeBacktestTask(std::shared_ptr<BacktestTask> task) {
        try {
            task->status = BacktestStatus::RUNNING;
            task->progress = 10;
            
            // 执行回测，传递进度回调
            auto result = executeBacktestWithProgress(task->config, [task](int progress, const std::string& message) {
                task->progress = progress;
                task->statusMessage = message;
            });
            
            task->status = BacktestStatus::COMPLETED;
            task->progress = 100;
            task->endTime = std::chrono::system_clock::now();
            
            return result;
            
        } catch (const std::exception& e) {
            task->status = BacktestStatus::FAILED;
            task->errorMessage = e.what();
            task->endTime = std::chrono::system_clock::now();
            
            // 返回空结果
            return FactorBacktestResult();
        }
    }
    
    // 执行回测逻辑（带进度回调）
    FactorBacktestResult executeBacktestWithProgress(const FactorBacktestConfig& config, 
                                                    std::function<void(int, const std::string&)> progressCallback) {
        FactorBacktestResult result;
        result.config = config;
        result.startTime = std::chrono::system_clock::now();
        

        // 步骤1：获取因子数据
        if (!factorDataProvider_) {
            throw std::runtime_error("Factor data provider not set");
        }

        if (progressCallback) progressCallback(10, "Loading factor data...");

        // 默认获取全量因子数据
        std::map<std::string, std::map<std::string, double>> factorValues =
            factorDataProvider_->getFactorValuesRange(
                config.factorId, config.startDate, config.endDate);

        if (factorValues.empty()) {
            throw std::runtime_error(
                "No factor data available for factor " + config.factorId +
                " between " + config.startDate + " and " + config.endDate);
        }

        // 步骤2：如指定数据集ID，则通过配置中的allowedStocks过滤股票池
        // 注意：allowedStocks应该在Controller层从DataServiceCache获取后传入config
        std::set<std::string> allowedStocks;
        if (!config.allowedStockCodes.empty()) {
            for (const auto& code : config.allowedStockCodes) {
                allowedStocks.insert(code);
            }
            INTERNAL_INFO_STREAM << "Using filtered stock pool, count: " << allowedStocks.size();
        }

        // 这里简化处理：使用第一天的因子值进行分组
        auto firstDate = factorValues.begin()->first;
        auto& firstDayValues = factorValues.begin()->second;

        // 按数据集过滤股票池
        std::map<std::string, double> filteredFirstDayValues;
        if (!allowedStocks.empty()) {
            for (const auto& kv : firstDayValues) {
                if (allowedStocks.count(kv.first)) {
                    filteredFirstDayValues[kv.first] = kv.second;
                }
            }
        } else {
            filteredFirstDayValues = firstDayValues;
        }

        // Log
        INTERNAL_INFO_STREAM << "Grouped stock count: " << filteredFirstDayValues.size();
        
        result.groups = factorGrouper_->group(
            filteredFirstDayValues,
            config.groupingMethod,
            config.numGroups,
            config.customThresholds
        );
        
        // 步骤3：分组回测
        if (!stockDataProvider_) {
            throw std::runtime_error("Stock data provider not set");
        }
        
        if (progressCallback) progressCallback(30, "Running group backtest...");
        
        // 如果配置了数据集ID，传递给分组回测器
        // 注意：GroupBacktester的runGroupsBacktestParallel函数只有3个参数
        // 数据集ID过滤已经在因子分组阶段完成
        result.groupBacktestResults = groupBacktester_->runGroupsBacktestParallel(
            result.groups,
            config,
            config.maxThreads
        );
        
        // Step 4: Calculate IC/IR
        if (progressCallback) progressCallback(70, "Calculating IC/IR metrics...");
        
        if (!result.groupBacktestResults.empty()) {
            result.icirResult = icirCalculator_->calculateICIR(
                result.groups,
                result.groupBacktestResults
            );
        }
        
        // Step 5: Calculate summary statistics
        if (progressCallback) progressCallback(85, "Generating summary statistics...");
        
        result.calculateSummaryStats();
        
        result.endTime = std::chrono::system_clock::now();
        auto duration = result.endTime - result.startTime;
        result.executionTime = std::chrono::duration<double>(duration).count();
        
        if (progressCallback) progressCallback(100, "Backtest completed");
        
        return result;
    }
    
    // 执行回测逻辑（无进度回调）
    FactorBacktestResult executeBacktest(const FactorBacktestConfig& config) {
        return executeBacktestWithProgress(config, nullptr);
    }
    
private:
    // 组件
    std::shared_ptr<engine::BacktestEngine> backtestEngine_;
    std::unique_ptr<FactorGrouper> factorGrouper_;
    std::unique_ptr<GroupBacktester> groupBacktester_;
    std::unique_ptr<ICIRCalculator> icirCalculator_;
    
    // 数据提供器
    std::shared_ptr<CacheManager> cacheManager_;
    std::shared_ptr<FactorDataProvider> factorDataProvider_;
    std::shared_ptr<StockDataProvider> stockDataProvider_;
    
    // 任务管理
    std::mutex tasksMutex_;
    std::map<std::string, std::shared_ptr<BacktestTask>> tasks_;
    std::atomic<int> nextTaskId_;
};

// FactorBacktestService公共接口实现
FactorBacktestService::FactorBacktestService() 
    : pImpl(std::make_unique<Impl>()) {
}

FactorBacktestService::~FactorBacktestService() = default;

std::future<FactorBacktestResult> FactorBacktestService::runFactorBacktestAsync(
    const FactorBacktestConfig& config) {
    return pImpl->runFactorBacktestAsync(config);
}

FactorBacktestResult FactorBacktestService::runFactorBacktestSync(
    const FactorBacktestConfig& config) {
    return pImpl->runFactorBacktestSync(config);
}

std::future<std::vector<FactorBacktestResult>> FactorBacktestService::runBatchFactorBacktestAsync(
    const std::vector<FactorBacktestConfig>& configs) {
    return pImpl->runBatchFactorBacktestAsync(configs);
}

void FactorBacktestService::cancelBacktest(const std::string& taskId) {
    pImpl->cancelBacktest(taskId);
}

BacktestStatus FactorBacktestService::getBacktestStatus(const std::string& taskId) {
    return pImpl->getBacktestStatus(taskId);
}

FactorBacktestService::TaskProgress FactorBacktestService::getTaskProgress(const std::string& taskId) {
    return pImpl->getTaskProgress(taskId);
}

std::vector<FactorBacktestService::TaskProgress> FactorBacktestService::getAllTaskProgress() {
    return pImpl->getAllTaskProgress();
}

void FactorBacktestService::cleanupCompletedTasks(int maxAgeHours) {
    pImpl->cleanupCompletedTasks(maxAgeHours);
}

void FactorBacktestService::setCacheManager(std::shared_ptr<CacheManager> cacheManager) {
    pImpl->setCacheManager(cacheManager);
}

void FactorBacktestService::setFactorDataProvider(std::shared_ptr<FactorDataProvider> provider) {
    pImpl->setFactorDataProvider(provider);
}

void FactorBacktestService::setStockDataProvider(std::shared_ptr<StockDataProvider> provider) {
    pImpl->setStockDataProvider(provider);
}

} // namespace domain::backtest
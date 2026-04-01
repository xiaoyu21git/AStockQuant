#include "StrategyBacktestService.h"
#include "DatabaseStockDataProvider.h"
#include "DatabaseFactorDataProvider.h"
#include "../include/BacktestEngine.h"
#include "../../../foundation/include/foundation.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>
#include <QDebug>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace domain::backtest {

// PIMPL
class StrategyBacktestService::Impl {
public:
    Impl() {
        // 
        nextTaskId_ = 1;
        cacheManager_ = nullptr;
        stockDataProvider_ = nullptr;
        factorDataProvider_ = nullptr;
        
        // 
        backtestEngine_ = std::make_shared<engine::BacktestEngine>();
        
        qDebug() << "StrategyBacktestService::Impl initialized";
    }
    
    ~Impl() {
        // 
        stopAllTasks();
    }
    
    std::future<StrategyBacktestResult> runStrategyBacktestAsync(
        const StrategyBacktestConfig& config) {
        
        // ID
        std::string taskId = generateTaskId();
        
        // 
        auto task = std::make_shared<BacktestTask>(config);
        task->taskId = taskId;
        
        // 
        {
            std::lock_guard<std::mutex> lock(tasksMutex_);
            tasks_[taskId] = task;
        }
        
        // 
        std::future<StrategyBacktestResult> future = std::async(
            std::launch::async,
            [this, task]() {
                return executeStrategyBacktestTask(task);
            }
        );
        
        return future;
    }
    
    StrategyBacktestResult runStrategyBacktestSync(
        const StrategyBacktestConfig& config) {
        
        // 
        if (config.enableCache && cacheManager_) {
            std::string cacheKey = generateStrategyCacheKey(config);
            auto cachedResult = cacheManager_->getFromCache<StrategyBacktestResult>(cacheKey);
            if (cachedResult) {
                return *cachedResult;
            }
        }
        
        // 
        auto result = executeStrategyBacktest(config);
        
        // 
        if (config.enableCache && cacheManager_) {
            std::string cacheKey = generateStrategyCacheKey(config);
            cacheManager_->putToCache(cacheKey, result, config.cacheTTL);
        }
        
        return result;
    }
    
    std::future<std::vector<StrategyBacktestResult>> runBatchStrategyBacktestAsync(
        const std::vector<StrategyBacktestConfig>& configs) {
        
        return std::async(
            std::launch::async,
            [this, configs]() {
                std::vector<StrategyBacktestResult> results;
                results.reserve(configs.size());
                
                for (const auto& config : configs) {
                    results.push_back(runStrategyBacktestSync(config));
                }
                
                return results;
            }
        );
    }
    
    StrategyBacktestService::OptimizationResult optimizeStrategyParameters(
        const StrategyBacktestConfig& baseConfig,
        const std::map<std::string, std::pair<double, double>>& paramRanges,
        const std::string& objectiveFunction,
        int maxIterations) {
        
        OptimizationResult result;
        result.optimizationMethod = "grid_search"; // 
        
        // 
        int totalCombinations = 1;
        for (const auto& range : paramRanges) {
            // 
            totalCombinations *= 10; // 10
        }
        
        // 
        totalCombinations = std::min(totalCombinations, maxIterations);
        
        qDebug() << ":" << QString::fromStdString(objectiveFunction)
                 << ":" << maxIterations << ":" << totalCombinations;
        
        // 
        std::vector<std::map<std::string, double>> paramCombinations;
        generateParameterCombinations(baseConfig, paramRanges, paramCombinations, maxIterations);
        
        // 
        for (const auto& params : paramCombinations) {
            StrategyBacktestConfig testConfig = baseConfig;
            testConfig.strategyParams = params;
            
            // 
            auto backtestResult = runStrategyBacktestSync(testConfig);
            
            // 
            double score = calculateObjectiveScore(backtestResult, objectiveFunction);
            
            // 
            result.history.push_back({testConfig, backtestResult});
            
            // 
            if (score > result.bestScore) {
                result.bestScore = score;
                result.optimalConfig = testConfig;
                result.optimalResult = backtestResult;
            }
        }
        
        qDebug() << ":" << result.bestScore
                 << "ID:" << QString::fromStdString(result.optimalConfig.strategyId);
        
        return result;
    }
    
    StrategyBacktestService::StrategyComparisonResult compareStrategies(
        const std::vector<StrategyBacktestConfig>& configs) {
        
        StrategyComparisonResult comparisonResult;
        
        // 
        for (const auto& config : configs) {
            auto result = runStrategyBacktestSync(config);
            comparisonResult.results.push_back(result);
        }
        
        // 
        if (!comparisonResult.results.empty()) {
            // 
            double bestSharpe = -std::numeric_limits<double>::max();
            size_t bestIndex = 0;
            
            for (size_t i = 0; i < comparisonResult.results.size(); ++i) {
                double sharpe = comparisonResult.results[i].performance.sharpeRatio;
                if (sharpe > bestSharpe) {
                    bestSharpe = sharpe;
                    bestIndex = i;
                }
            }
            
            comparisonResult.bestStrategyId = comparisonResult.results[bestIndex].config.strategyId;
            
            // 
            comparisonResult.comparisonMetrics["best_sharpe_ratio"] = bestSharpe;
            comparisonResult.comparisonMetrics["average_sharpe_ratio"] = calculateAverageSharpe(comparisonResult.results);
            comparisonResult.comparisonMetrics["average_max_drawdown"] = calculateAverageMaxDrawdown(comparisonResult.results);
            comparisonResult.comparisonMetrics["average_annual_return"] = calculateAverageAnnualReturn(comparisonResult.results);
        }
        
        return comparisonResult;
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
    
    StrategyBacktestService::TaskProgress getTaskProgress(const std::string& taskId) {
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
            
            // 
            if (task->progress > 0) {
                auto elapsed = std::chrono::system_clock::now() - task->startTime;
                auto estimatedTotal = elapsed * 100 / task->progress;
                progress.estimatedCompletionTime = task->startTime + estimatedTotal;
            } else {
                progress.estimatedCompletionTime = std::chrono::system_clock::now();
            }
            
            return progress;
        }
        
        // 
        TaskProgress progress;
        progress.taskId = taskId;
        progress.status = BacktestStatus::FAILED;
        progress.progress = 0;
        progress.message = "Task not found";
        return progress;
    }
    
    std::vector<StrategyBacktestService::TaskProgress> getAllTaskProgress() {
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
    
    void setDataProvider(std::shared_ptr<StockDataProvider> provider) {
        stockDataProvider_ = provider;
        qDebug() << "StrategyBacktestService: set stock data provider";
    }
    
    void setFactorProvider(std::shared_ptr<FactorDataProvider> provider) {
        factorDataProvider_ = provider;
        qDebug() << "StrategyBacktestService: set factor data provider";
    }
    
    void setCacheManager(std::shared_ptr<CacheManager> cacheManager) {
        cacheManager_ = cacheManager;
        qDebug() << "StrategyBacktestService: set cache manager";
    }
    
    void setBacktestEngine(std::shared_ptr<engine::BacktestEngine> engine) {
        backtestEngine_ = engine;
        qDebug() << "StrategyBacktestService: set backtest engine";
    }
    
private:
    // ID
    std::string generateTaskId() {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        std::stringstream ss;
        ss << "strategy_backtest_" << nextTaskId_++ << "_" 
           << foundation::Uuid{}.to_string();
        return ss.str();
    }
    
    // 
    std::string generateStrategyCacheKey(const StrategyBacktestConfig& config) {
        std::string key = "strategy_backtest:";
        key += config.strategyId + ":";
        key += config.startDate + ":";
        key += config.endDate + ":";
        key += std::to_string(config.initialCapital) + ":";
        
        // 
        for (const auto& param : config.strategyParams) {
            key += ":" + param.first + "=" + std::to_string(param.second);
        }
        
        return key;
    }
    
    // 
    void stopAllTasks() {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        for (auto& kv : tasks_) {
            kv.second->cancelled = true;
        }
    }
    
    // 
    StrategyBacktestResult executeStrategyBacktestTask(std::shared_ptr<BacktestTask> task) {
        try {
            task->status = BacktestStatus::RUNNING;
            task->progress = 10;
            
            // 
            auto result = executeStrategyBacktestWithProgress(task->config, 
                [task](int progress, const std::string& message) {
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
            
            // 
            return StrategyBacktestResult();
        }
    }
    
    // 
    StrategyBacktestResult executeStrategyBacktestWithProgress(
        const StrategyBacktestConfig& config,
        std::function<void(int, const std::string&)> progressCallback) {
        
        StrategyBacktestResult result;
        result.config = config;
        result.startTime = std::chrono::system_clock::now();
        
        if (progressCallback) progressCallback(10, "...");
        
        // 
        if (!config.validate()) {
            throw std::runtime_error(": " + config.getValidationErrors());
        }
        
        if (!stockDataProvider_) {
            throw std::runtime_error("");
        }
        
        if (progressCallback) progressCallback(20, "...");
        
        // 
        std::vector<std::string> symbols = config.symbols;
        if (symbols.empty() && !config.universeId.empty()) {
            // 
            symbols = getSymbolsFromUniverse(config.universeId);
        }
        
        if (symbols.empty()) {
            throw std::runtime_error("");
        }
        
        // 
        if (!config.sectorFilters.empty() || !config.marketFilters.empty()) {
            symbols = filterSymbols(symbols, config.sectorFilters, config.marketFilters);
        }
        
        if (progressCallback) progressCallback(30, "...");
        
    if (progressCallback) progressCallback(40, "加载行情数据...");

    std::vector<domain::model::Bar> bars;
    for (const auto& symbol : symbols) {
        auto symbolBars = stockDataProvider_->getStockBars(symbol, config.startDate, config.endDate);
        bars.insert(bars.end(), symbolBars.begin(), symbolBars.end());
    }

    if (bars.empty()) {
        throw std::runtime_error("指定数据源下没有可用于回测的行情数据");
    }
    
    // 
    std::string strategyName = "MovingAverageStrategy";
    if (!config.strategyId.empty()) {
        strategyName = config.strategyId;
    }
    
    // BacktestEnginerun
    auto backtestResult = backtestEngine_->run(
        bars,
        config.initialCapital,
        strategyName,
        config.maxPositionRatio,
        config.commissionRate,
        config.slippageRate,
        0.0); // min_volume0
    
    result.backtestResult = std::make_shared<engine::BacktestResult>(backtestResult);
        
        if (progressCallback) progressCallback(80, "...");
        
        // 
        result.calculatePerformanceMetrics();
        
        // 
        extractTimeSeriesData(result);
        
        result.endTime = std::chrono::system_clock::now();
        auto duration = result.endTime - result.startTime;
        result.executionTime = std::chrono::duration<double>(duration).count();
        
        if (progressCallback) progressCallback(100, "");
        
        return result;
    }
    
    // 
    StrategyBacktestResult executeStrategyBacktest(const StrategyBacktestConfig& config) {
        return executeStrategyBacktestWithProgress(config, nullptr);
    }
    
    // 
    std::vector<std::string> getSymbolsFromUniverse(const std::string& universeId) {
        // 
        // TODO: 
        
        qDebug() << ":" << QString::fromStdString(universeId);
        
        // 
        return {};
    }
    
    // 
    std::vector<std::string> filterSymbols(const std::vector<std::string>& symbols,
                                          const std::vector<std::string>& sectorFilters,
                                          const std::vector<std::string>& marketFilters) {
        // 
        // TODO: 
        
        qDebug() << ": " << sectorFilters.size() 
                 << "" << marketFilters.size() << "";
        
        return symbols; // 
    }
    
    // 
    void generateParameterCombinations(const StrategyBacktestConfig& baseConfig,
                                      const std::map<std::string, std::pair<double, double>>& paramRanges,
                                      std::vector<std::map<std::string, double>>& combinations,
                                      int maxCombinations) {
        // 
        // TODO: 
        
        if (paramRanges.empty()) {
            return;
        }
        
        // 
        std::vector<std::vector<double>> paramValues;
        for (const auto& range : paramRanges) {
            double minVal = range.second.first;
            double maxVal = range.second.second;
            int steps = 5; // 5
            
            std::vector<double> values;
            for (int i = 0; i < steps; ++i) {
                double value = minVal + (maxVal - minVal) * i / (steps - 1);
                values.push_back(value);
            }
            paramValues.push_back(values);
        }
        
        // 
        // 
        generateCombinationsRecursive(paramRanges, paramValues, combinations, 
                                     {}, 0, maxCombinations);
    }
    
    void generateCombinationsRecursive(
        const std::map<std::string, std::pair<double, double>>& paramRanges,
        const std::vector<std::vector<double>>& paramValues,
        std::vector<std::map<std::string, double>>& combinations,
        std::map<std::string, double> current,
        size_t paramIndex,
        int maxCombinations) {
        
        if (paramIndex >= paramRanges.size()) {
            combinations.push_back(current);
            return;
        }
        
        if (combinations.size() >= maxCombinations) {
            return;
        }
        
        auto it = paramRanges.begin();
        std::advance(it, paramIndex);
        std::string paramName = it->first;
        
        for (double value : paramValues[paramIndex]) {
            current[paramName] = value;
            generateCombinationsRecursive(paramRanges, paramValues, combinations,
                                         current, paramIndex + 1, maxCombinations);
            
            if (combinations.size() >= maxCombinations) {
                break;
            }
        }
    }
    
    // 
    double calculateObjectiveScore(const StrategyBacktestResult& result,
                                  const std::string& objectiveFunction) {
        if (objectiveFunction == "sharpe_ratio") {
            return result.performance.sharpeRatio;
        } else if (objectiveFunction == "total_return") {
            return result.performance.totalReturn;
        } else if (objectiveFunction == "calmar_ratio") {
            return result.performance.calmarRatio;
        } else if (objectiveFunction == "information_ratio") {
            return result.performance.informationRatio;
        } else if (objectiveFunction == "win_rate") {
            return result.performance.winRate;
        } else if (objectiveFunction == "profit_factor") {
            return result.performance.profitFactor;
        }
        
        // 
        return result.performance.sharpeRatio;
    }
    
    // 
    void extractTimeSeriesData(StrategyBacktestResult& result) {
        if (!result.backtestResult) {
            return;
        }
        
        // BacktestResult
        // TODO: 
    }
    
    // 
    double calculateAverageSharpe(const std::vector<StrategyBacktestResult>& results) {
        if (results.empty()) return 0.0;
        
        double sum = 0.0;
        for (const auto& result : results) {
            sum += result.performance.sharpeRatio;
        }
        return sum / results.size();
    }
    
    // 
    double calculateAverageMaxDrawdown(const std::vector<StrategyBacktestResult>& results) {
        if (results.empty()) return 0.0;
        
        double sum = 0.0;
        for (const auto& result : results) {
            sum += result.performance.maxDrawdown;
        }
        return sum / results.size();
    }
    
    // 
    double calculateAverageAnnualReturn(const std::vector<StrategyBacktestResult>& results) {
        if (results.empty()) return 0.0;
        
        double sum = 0.0;
        for (const auto& result : results) {
            sum += result.performance.annualizedReturn;
        }
        return sum / results.size();
    }
    
private:
    // 
    std::shared_ptr<engine::BacktestEngine> backtestEngine_;
    
    // 
    std::shared_ptr<CacheManager> cacheManager_;
    std::shared_ptr<StockDataProvider> stockDataProvider_;
    std::shared_ptr<FactorDataProvider> factorDataProvider_;
    
    // 
    std::mutex tasksMutex_;
    std::map<std::string, std::shared_ptr<BacktestTask>> tasks_;
    std::atomic<int> nextTaskId_;
};

// StrategyBacktestService
StrategyBacktestService::StrategyBacktestService() 
    : pImpl(std::make_unique<Impl>()) {
    qDebug() << "StrategyBacktestService ";
}

StrategyBacktestService::~StrategyBacktestService() {
    qDebug() << "StrategyBacktestService ";
}

std::future<StrategyBacktestResult> StrategyBacktestService::runStrategyBacktestAsync(
    const StrategyBacktestConfig& config) {
    return pImpl->runStrategyBacktestAsync(config);
}

StrategyBacktestResult StrategyBacktestService::runStrategyBacktestSync(
    const StrategyBacktestConfig& config) {
    return pImpl->runStrategyBacktestSync(config);
}

std::future<std::vector<StrategyBacktestResult>> StrategyBacktestService::runBatchStrategyBacktestAsync(
    const std::vector<StrategyBacktestConfig>& configs) {
    return pImpl->runBatchStrategyBacktestAsync(configs);
}

StrategyBacktestService::OptimizationResult StrategyBacktestService::optimizeStrategyParameters(
    const StrategyBacktestConfig& baseConfig,
    const std::map<std::string, std::pair<double, double>>& paramRanges,
    const std::string& objectiveFunction,
    int maxIterations) {
    return pImpl->optimizeStrategyParameters(baseConfig, paramRanges, objectiveFunction, maxIterations);
}

StrategyBacktestService::StrategyComparisonResult StrategyBacktestService::compareStrategies(
    const std::vector<StrategyBacktestConfig>& configs) {
    return pImpl->compareStrategies(configs);
}

void StrategyBacktestService::cancelBacktest(const std::string& taskId) {
    pImpl->cancelBacktest(taskId);
}

BacktestStatus StrategyBacktestService::getBacktestStatus(const std::string& taskId) {
    return pImpl->getBacktestStatus(taskId);
}

StrategyBacktestService::TaskProgress StrategyBacktestService::getTaskProgress(const std::string& taskId) {
    return pImpl->getTaskProgress(taskId);
}

std::vector<StrategyBacktestService::TaskProgress> StrategyBacktestService::getAllTaskProgress() {
    return pImpl->getAllTaskProgress();
}

void StrategyBacktestService::cleanupCompletedTasks(int maxAgeHours) {
    pImpl->cleanupCompletedTasks(maxAgeHours);
}

void StrategyBacktestService::setDataProvider(std::shared_ptr<StockDataProvider> provider) {
    pImpl->setDataProvider(provider);
}

void StrategyBacktestService::setFactorProvider(std::shared_ptr<FactorDataProvider> provider) {
    pImpl->setFactorProvider(provider);
}

void StrategyBacktestService::setCacheManager(std::shared_ptr<CacheManager> cacheManager) {
    pImpl->setCacheManager(cacheManager);
}

void StrategyBacktestService::setBacktestEngine(std::shared_ptr<engine::BacktestEngine> engine) {
    pImpl->setBacktestEngine(engine);
}

// StrategyBacktestConfig
bool StrategyBacktestConfig::validate() const {
    // 
    if (strategyId.empty()) return false;
    if (startDate.empty() || endDate.empty()) return false;
    if (initialCapital <= 0) return false;
    if (commissionRate < 0 || commissionRate > 0.1) return false; // 10%
    if (slippageRate < 0 || slippageRate > 0.1) return false; // 10%
    if (taxRate < 0 || taxRate > 0.3) return false; // 30%
    if (maxPositionRatio <= 0 || maxPositionRatio > 1.0) return false;
    if (maxSinglePositionRatio <= 0 || maxSinglePositionRatio > 1.0) return false;
    if (maxDrawdownLimit < 0 || maxDrawdownLimit > 1.0) return false;
    if (stopLossRate < 0 || stopLossRate > 1.0) return false;
    if (rebalanceFrequency <= 0) return false;
    if (maxThreads <= 0) return false;
    
    return true;
}

std::string StrategyBacktestConfig::getValidationErrors() const {
    std::string errors;
    
    if (strategyId.empty()) errors += "ID; ";
    if (startDate.empty()) errors += "; ";
    if (endDate.empty()) errors += "; ";
    if (initialCapital <= 0) errors += "0; ";
    if (commissionRate < 0 || commissionRate > 0.1) errors += "0-10%; ";
    if (slippageRate < 0 || slippageRate > 0.1) errors += "0-10%; ";
    if (taxRate < 0 || taxRate > 0.3) errors += "0-30%; ";
    if (maxPositionRatio <= 0 || maxPositionRatio > 1.0) errors += "0-1; ";
    if (maxSinglePositionRatio <= 0 || maxSinglePositionRatio > 1.0) errors += "0-1; ";
    if (maxDrawdownLimit < 0 || maxDrawdownLimit > 1.0) errors += "0-1; ";
    if (stopLossRate < 0 || stopLossRate > 1.0) errors += "0-1; ";
    if (rebalanceFrequency <= 0) errors += "0; ";
    if (maxThreads <= 0) errors += "0; ";
    
    return errors;
}

std::string StrategyBacktestConfig::toJson() const {
    json j;
    
    j["strategyId"] = strategyId;
    j["strategyName"] = strategyName;
    j["startDate"] = startDate;
    j["endDate"] = endDate;
    j["initialCapital"] = initialCapital;
    j["commissionRate"] = commissionRate;
    j["slippageRate"] = slippageRate;
    j["taxRate"] = taxRate;
    j["symbols"] = symbols;
    j["universeId"] = universeId;
    j["sectorFilters"] = sectorFilters;
    j["marketFilters"] = marketFilters;
    j["strategyParams"] = strategyParams;
    j["strategyOptions"] = strategyOptions;
    j["maxPositionRatio"] = maxPositionRatio;
    j["maxSinglePositionRatio"] = maxSinglePositionRatio;
    j["maxDrawdownLimit"] = maxDrawdownLimit;
    j["stopLossRate"] = stopLossRate;
    j["enableShortSelling"] = enableShortSelling;
    j["rebalanceFrequency"] = rebalanceFrequency;
    j["useMarketOnClose"] = useMarketOnClose;
    j["maxThreads"] = maxThreads;
    j["enableCache"] = enableCache;
    j["cacheTTL"] = cacheTTL;
    
    return j.dump();
}

StrategyBacktestConfig StrategyBacktestConfig::fromJson(const std::string& jsonStr) {
    StrategyBacktestConfig config;
    
    try {
        json j = json::parse(jsonStr);
        
        config.strategyId = j.value("strategyId", "");
        config.strategyName = j.value("strategyName", "");
        config.startDate = j.value("startDate", "");
        config.endDate = j.value("endDate", "");
        config.initialCapital = j.value("initialCapital", 1000000.0);
        config.commissionRate = j.value("commissionRate", 0.0003);
        config.slippageRate = j.value("slippageRate", 0.0002);
        config.taxRate = j.value("taxRate", 0.001);
        config.symbols = j.value("symbols", std::vector<std::string>());
        config.universeId = j.value("universeId", "");
        config.sectorFilters = j.value("sectorFilters", std::vector<std::string>());
        config.marketFilters = j.value("marketFilters", std::vector<std::string>());
        config.strategyParams = j.value("strategyParams", std::map<std::string, double>());
        config.strategyOptions = j.value("strategyOptions", std::map<std::string, std::string>());
        config.maxPositionRatio = j.value("maxPositionRatio", 1.0);
        config.maxSinglePositionRatio = j.value("maxSinglePositionRatio", 0.1);
        config.maxDrawdownLimit = j.value("maxDrawdownLimit", 0.2);
        config.stopLossRate = j.value("stopLossRate", 0.05);
        config.enableShortSelling = j.value("enableShortSelling", false);
        config.rebalanceFrequency = j.value("rebalanceFrequency", 1);
        config.useMarketOnClose = j.value("useMarketOnClose", true);
        config.maxThreads = j.value("maxThreads", 4);
        config.enableCache = j.value("enableCache", true);
        config.cacheTTL = j.value("cacheTTL", 3600);
        
    } catch (const std::exception& e) {
        qWarning() << "StrategyBacktestConfig JSON:" << e.what();
    }
    
    return config;
}

// StrategyBacktestResult
std::string StrategyBacktestResult::toJson() const {
    json j;
    
    j["taskId"] = taskId;
    j["executionTime"] = executionTime;
    j["config"] = config.toJson();
    
    // 
    json perfJson;
    perfJson["totalReturn"] = performance.totalReturn;
    perfJson["annualizedReturn"] = performance.annualizedReturn;
    perfJson["volatility"] = performance.volatility;
    perfJson["sharpeRatio"] = performance.sharpeRatio;
    perfJson["sortinoRatio"] = performance.sortinoRatio;
    perfJson["calmarRatio"] = performance.calmarRatio;
    perfJson["maxDrawdown"] = performance.maxDrawdown;
    perfJson["winRate"] = performance.winRate;
    perfJson["profitFactor"] = performance.profitFactor;
    perfJson["averageWin"] = performance.averageWin;
    perfJson["averageLoss"] = performance.averageLoss;
    perfJson["alpha"] = performance.alpha;
    perfJson["beta"] = performance.beta;
    perfJson["informationRatio"] = performance.informationRatio;
    perfJson["trackingError"] = performance.trackingError;
    j["performance"] = perfJson;
    
    // 
    json tradesJson;
    tradesJson["totalTrades"] = trades.totalTrades;
    tradesJson["winningTrades"] = trades.winningTrades;
    tradesJson["losingTrades"] = trades.losingTrades;
    tradesJson["totalProfit"] = trades.totalProfit;
    tradesJson["totalLoss"] = trades.totalLoss;
    tradesJson["largestWin"] = trades.largestWin;
    tradesJson["largestLoss"] = trades.largestLoss;
    tradesJson["averageHoldingPeriod"] = trades.averageHoldingPeriod;
    j["trades"] = tradesJson;
    
    // 
    json riskJson;
    riskJson["var95"] = risk.var95;
    riskJson["cvar95"] = risk.cvar95;
    riskJson["downsideDeviation"] = risk.downsideDeviation;
    riskJson["upsideDeviation"] = risk.upsideDeviation;
    riskJson["skewness"] = risk.skewness;
    riskJson["kurtosis"] = risk.kurtosis;
    riskJson["sectorExposure"] = risk.sectorExposure;
    riskJson["factorExposure"] = risk.factorExposure;
    j["risk"] = riskJson;
    
    // 
    json tsJson;
    tsJson["dates"] = timeSeries.dates;
    tsJson["portfolioValues"] = timeSeries.portfolioValues;
    tsJson["returns"] = timeSeries.returns;
    tsJson["drawdowns"] = timeSeries.drawdowns;
    tsJson["positions"] = timeSeries.positions;
    tsJson["cash"] = timeSeries.cash;
    j["timeSeries"] = tsJson;
    
    return j.dump();
}

bool StrategyBacktestResult::saveToFile(const std::string& filepath) const {
    try {
        std::string jsonStr = toJson();
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }
        file << jsonStr;
        file.close();
        return true;
    } catch (const std::exception& e) {
        qWarning() << ":" << e.what();
        return false;
    }
}

StrategyBacktestResult StrategyBacktestResult::loadFromFile(const std::string& filepath) {
    StrategyBacktestResult result;
    
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error(": " + filepath);
        }
        
        std::string jsonStr((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        
        json j = json::parse(jsonStr);
        
        result.taskId = j.value("taskId", "");
        result.executionTime = j.value("executionTime", 0.0);
        
        // 
        std::string configJson = j.value("config", "");
        if (!configJson.empty()) {
            result.config = StrategyBacktestConfig::fromJson(configJson);
        }
        
        // 
        if (j.contains("performance")) {
            auto perfJson = j["performance"];
            result.performance.totalReturn = perfJson.value("totalReturn", 0.0);
            result.performance.annualizedReturn = perfJson.value("annualizedReturn", 0.0);
            result.performance.volatility = perfJson.value("volatility", 0.0);
            result.performance.sharpeRatio = perfJson.value("sharpeRatio", 0.0);
            result.performance.sortinoRatio = perfJson.value("sortinoRatio", 0.0);
            result.performance.calmarRatio = perfJson.value("calmarRatio", 0.0);
            result.performance.maxDrawdown = perfJson.value("maxDrawdown", 0.0);
            result.performance.winRate = perfJson.value("winRate", 0.0);
            result.performance.profitFactor = perfJson.value("profitFactor", 0.0);
            result.performance.averageWin = perfJson.value("averageWin", 0.0);
            result.performance.averageLoss = perfJson.value("averageLoss", 0.0);
            result.performance.alpha = perfJson.value("alpha", 0.0);
            result.performance.beta = perfJson.value("beta", 0.0);
            result.performance.informationRatio = perfJson.value("informationRatio", 0.0);
            result.performance.trackingError = perfJson.value("trackingError", 0.0);
        }
        
        // 
        if (j.contains("trades")) {
            auto tradesJson = j["trades"];
            result.trades.totalTrades = tradesJson.value("totalTrades", 0);
            result.trades.winningTrades = tradesJson.value("winningTrades", 0);
            result.trades.losingTrades = tradesJson.value("losingTrades", 0);
            result.trades.totalProfit = tradesJson.value("totalProfit", 0.0);
            result.trades.totalLoss = tradesJson.value("totalLoss", 0.0);
            result.trades.largestWin = tradesJson.value("largestWin", 0.0);
            result.trades.largestLoss = tradesJson.value("largestLoss", 0.0);
            result.trades.averageHoldingPeriod = tradesJson.value("averageHoldingPeriod", 0.0);
        }
        
        // 
        if (j.contains("risk")) {
            auto riskJson = j["risk"];
            result.risk.var95 = riskJson.value("var95", 0.0);
            result.risk.cvar95 = riskJson.value("cvar95", 0.0);
            result.risk.downsideDeviation = riskJson.value("downsideDeviation", 0.0);
            result.risk.upsideDeviation = riskJson.value("upsideDeviation", 0.0);
            result.risk.skewness = riskJson.value("skewness", 0.0);
            result.risk.kurtosis = riskJson.value("kurtosis", 0.0);
            
            if (riskJson.contains("sectorExposure")) {
                result.risk.sectorExposure = riskJson["sectorExposure"].get<std::map<std::string, double>>();
            }
            if (riskJson.contains("factorExposure")) {
                result.risk.factorExposure = riskJson["factorExposure"].get<std::map<std::string, double>>();
            }
        }
        
        // 
        if (j.contains("timeSeries")) {
            auto tsJson = j["timeSeries"];
            result.timeSeries.dates = tsJson.value("dates", std::vector<std::string>());
            result.timeSeries.portfolioValues = tsJson.value("portfolioValues", std::vector<double>());
            result.timeSeries.returns = tsJson.value("returns", std::vector<double>());
            result.timeSeries.drawdowns = tsJson.value("drawdowns", std::vector<double>());
            result.timeSeries.positions = tsJson.value("positions", std::vector<double>());
            result.timeSeries.cash = tsJson.value("cash", std::vector<double>());
        }
        
    } catch (const std::exception& e) {
        qWarning() << ":" << e.what();
    }
    
    return result;
}

void StrategyBacktestResult::calculatePerformanceMetrics() {
    // 
    // TODO: 
    
    if (!backtestResult) {
        return;
    }
    
    // BacktestResult
    // BacktestResult
    
    // 
    performance.totalReturn = 0.15; // 15%
    performance.annualizedReturn = 0.12; // 12%
    performance.volatility = 0.20; // 20%
    performance.sharpeRatio = performance.annualizedReturn / performance.volatility;
    performance.maxDrawdown = 0.08; // 8%
    performance.calmarRatio = performance.annualizedReturn / performance.maxDrawdown;
    performance.winRate = 0.55; // 55%
    performance.profitFactor = 1.8; // 1.8
    performance.alpha = 0.03; // 3%
    performance.beta = 0.8; // 0.8
    performance.informationRatio = 0.5; // 0.5
    
    // 
    trades.totalTrades = 100;
    trades.winningTrades = 55;
    trades.losingTrades = 45;
    trades.totalProfit = 20000.0;
    trades.totalLoss = -8000.0;
    trades.largestWin = 5000.0;
    trades.largestLoss = -2000.0;
    trades.averageHoldingPeriod = 5.2; // 5.2
}

} // namespace domain::backtest

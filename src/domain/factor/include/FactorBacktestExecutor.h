#pragma once

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <future>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "foundation/json/json_facade.h"
#include "foundation/Utils/Uuid.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "BaseFactor.h"
#include "FactorCacheManager.h"
#include "FactorInstanceManager.h"

namespace factor {

class ArrowMarketData;

namespace detail {

inline foundation::json::JsonFacade toJsonValue(const std::string& value) {
    return foundation::json::JsonFacade::createString(value);
}

inline foundation::json::JsonFacade toJsonValue(const char* value) {
    return foundation::json::JsonFacade::createString(value == nullptr ? std::string() : std::string(value));
}

inline foundation::json::JsonFacade toJsonValue(int value) {
    return foundation::json::JsonFacade::createInt(value);
}

inline foundation::json::JsonFacade toJsonValue(double value) {
    return foundation::json::JsonFacade::createDouble(value);
}

inline foundation::json::JsonFacade toJsonValue(bool value) {
    return foundation::json::JsonFacade::createBool(value);
}

} // namespace detail

// 回测配置
struct CachedMarketBar {
    std::string symbol;
    std::string tradeDate;
    double close = 0.0;
    std::unordered_map<std::string, double> numericFields;
};

struct BacktestConfig {
    std::string instanceId;
    std::string startDate;
    std::string endDate;
    int datasetId = -1;
    int forwardDays = 1;      // 预测未来几天
    int rebalanceDays = 1;    // 调仓周期（交易日）
    int numGroups = 10;       // 分组数量
    double transactionCost = 0.001;  // 交易成本
    double slippageRate = 0.0;       // 滑点成本
    double riskFreeRate = 0.0;       // 年化无风险利率
    std::string benchmarkSymbol = "000300.SH"; // 基准代码
    double stopLossRate = 0.0;
    double takeProfitRate = 0.0;
    double maxDrawdownLimit = 0.0;
    double maxDailyLoss = 0.0;           // 单日最大亏损限制
    double maxPositionPercent = 1.0;     // 单只股票最大仓位比例 (0-1)
    double maxTotalExposure = 1.0;       // 最大总仓位暴露 (0-1)
    bool enableDateParallelism = false;   // 允许按交易日分块并发
    std::vector<std::string> allowedStockCodes;
    std::vector<CachedMarketBar> cachedBars;
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("instance_id", detail::toJsonValue(instanceId));
        json.set("start_date", detail::toJsonValue(startDate));
        json.set("end_date", detail::toJsonValue(endDate));
        json.set("dataset_id", detail::toJsonValue(datasetId));
        json.set("forward_days", detail::toJsonValue(forwardDays));
        json.set("rebalance_days", detail::toJsonValue(rebalanceDays));
        json.set("num_groups", detail::toJsonValue(numGroups));
        json.set("transaction_cost", detail::toJsonValue(transactionCost));
        json.set("slippage_rate", detail::toJsonValue(slippageRate));
        json.set("risk_free_rate", detail::toJsonValue(riskFreeRate));
        json.set("benchmark_symbol", detail::toJsonValue(benchmarkSymbol));
        json.set("stop_loss_rate", detail::toJsonValue(stopLossRate));
        json.set("take_profit_rate", detail::toJsonValue(takeProfitRate));
        json.set("max_drawdown_limit", detail::toJsonValue(maxDrawdownLimit));
        json.set("max_daily_loss", detail::toJsonValue(maxDailyLoss));
        json.set("max_position_percent", detail::toJsonValue(maxPositionPercent));
        json.set("max_total_exposure", detail::toJsonValue(maxTotalExposure));
        json.set("enable_date_parallelism", detail::toJsonValue(enableDateParallelism));

        auto allowedStocksArray = foundation::json::JsonFacade::createArray();
        for (const auto& stockCode : allowedStockCodes) {
            allowedStocksArray.push_back(detail::toJsonValue(stockCode));
        }
        json.set("allowed_stock_codes", allowedStocksArray);
        return json;
    }
    
    void fromJson(const foundation::json::JsonFacade& json) {
        if (json.has("instance_id")) instanceId = json.get("instance_id").asString();
        if (json.has("start_date")) startDate = json.get("start_date").asString();
        if (json.has("end_date")) endDate = json.get("end_date").asString();
        if (json.has("dataset_id")) datasetId = json.get("dataset_id").asInt();
        if (json.has("forward_days")) forwardDays = json.get("forward_days").asInt();
        if (json.has("rebalance_days")) rebalanceDays = json.get("rebalance_days").asInt();
        if (json.has("num_groups")) numGroups = json.get("num_groups").asInt();
        if (json.has("transaction_cost")) transactionCost = json.get("transaction_cost").asDouble();
        if (json.has("slippage_rate")) slippageRate = json.get("slippage_rate").asDouble();
        if (json.has("risk_free_rate")) riskFreeRate = json.get("risk_free_rate").asDouble();
        if (json.has("benchmark_symbol")) benchmarkSymbol = json.get("benchmark_symbol").asString();
        if (json.has("stop_loss_rate")) stopLossRate = json.get("stop_loss_rate").asDouble();
        if (json.has("take_profit_rate")) takeProfitRate = json.get("take_profit_rate").asDouble();
        if (json.has("max_drawdown_limit")) maxDrawdownLimit = json.get("max_drawdown_limit").asDouble();
        if (json.has("max_daily_loss")) maxDailyLoss = json.get("max_daily_loss").asDouble();
        if (json.has("max_position_percent")) maxPositionPercent = json.get("max_position_percent").asDouble();
        if (json.has("max_total_exposure")) maxTotalExposure = json.get("max_total_exposure").asDouble();
        if (json.has("enable_date_parallelism")) enableDateParallelism = json.get("enable_date_parallelism").asBool();
        if (json.has("allowed_stock_codes")) {
            allowedStockCodes.clear();
            auto allowedStocksArray = json.get("allowed_stock_codes");
            for (size_t index = 0; index < allowedStocksArray.size(); ++index) {
                allowedStockCodes.push_back(allowedStocksArray.at(index).asString());
            }
        }
    }
};

// IC/IR结果
struct ICIRResult {
    double icMean = 0.0;
    double icStd = 0.0;
    double ir = 0.0;  // 信息比率 = icMean / icStd
    double icPositiveRatio = 0.0;  // IC>0的比例
    std::vector<double> icSeries;  // IC时间序列
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("ic_mean", detail::toJsonValue(icMean));
        json.set("ic_std", detail::toJsonValue(icStd));
        json.set("ir", detail::toJsonValue(ir));
        json.set("ic_positive_ratio", detail::toJsonValue(icPositiveRatio));
        
        auto seriesArray = foundation::json::JsonFacade::createArray();
        for (double ic : icSeries) {
            seriesArray.push_back(foundation::json::JsonFacade::createDouble(ic));
        }
        json.set("ic_series", seriesArray);
        
        return json;
    }

    static ICIRResult fromJson(const foundation::json::JsonFacade& json) {
        ICIRResult result;
        if (json.has("ic_mean")) result.icMean = json.get("ic_mean").asDouble();
        if (json.has("ic_std")) result.icStd = json.get("ic_std").asDouble();
        if (json.has("ir")) result.ir = json.get("ir").asDouble();
        if (json.has("ic_positive_ratio")) result.icPositiveRatio = json.get("ic_positive_ratio").asDouble();
        if (json.has("ic_series")) {
            auto seriesArray = json.get("ic_series");
            for (size_t i = 0; i < seriesArray.size(); ++i) {
                result.icSeries.push_back(seriesArray.at(i).asDouble());
            }
        }
        return result;
    }
};

// 分组回测结果
struct GroupBacktestResult {
    std::vector<double> groupReturns;  // 每组收益
    std::vector<int> groupStockCounts; // 每组平均股票数
    std::vector<double> minFactorValues; // 每组最小因子值
    std::vector<double> maxFactorValues; // 每组最大因子值
    double longShortReturn = 0.0;      // 多空收益（第一组-最后一组）
    double topGroupReturn = 0.0;       // 第一组收益
    double bottomGroupReturn = 0.0;    // 最后一组收益
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        
        auto returnsArray = foundation::json::JsonFacade::createArray();
        for (double ret : groupReturns) {
            returnsArray.push_back(foundation::json::JsonFacade::createDouble(ret));
        }
        json.set("group_returns", returnsArray);

        auto stockCountsArray = foundation::json::JsonFacade::createArray();
        for (int count : groupStockCounts) {
            stockCountsArray.push_back(foundation::json::JsonFacade::createInt(count));
        }
        json.set("group_stock_counts", stockCountsArray);

        auto minValuesArray = foundation::json::JsonFacade::createArray();
        for (double value : minFactorValues) {
            minValuesArray.push_back(foundation::json::JsonFacade::createDouble(value));
        }
        json.set("min_factor_values", minValuesArray);

        auto maxValuesArray = foundation::json::JsonFacade::createArray();
        for (double value : maxFactorValues) {
            maxValuesArray.push_back(foundation::json::JsonFacade::createDouble(value));
        }
        json.set("max_factor_values", maxValuesArray);

        json.set("long_short_return", detail::toJsonValue(longShortReturn));
        json.set("top_group_return", detail::toJsonValue(topGroupReturn));
        json.set("bottom_group_return", detail::toJsonValue(bottomGroupReturn));
        
        return json;
    }

    static GroupBacktestResult fromJson(const foundation::json::JsonFacade& json) {
        GroupBacktestResult result;
        if (json.has("group_returns")) {
            auto returnsArray = json.get("group_returns");
            for (size_t i = 0; i < returnsArray.size(); ++i) {
                result.groupReturns.push_back(returnsArray.at(i).asDouble());
            }
        }
        if (json.has("group_stock_counts")) {
            auto stockCountsArray = json.get("group_stock_counts");
            for (size_t i = 0; i < stockCountsArray.size(); ++i) {
                result.groupStockCounts.push_back(stockCountsArray.at(i).asInt());
            }
        }
        if (json.has("min_factor_values")) {
            auto minValuesArray = json.get("min_factor_values");
            for (size_t i = 0; i < minValuesArray.size(); ++i) {
                result.minFactorValues.push_back(minValuesArray.at(i).asDouble());
            }
        }
        if (json.has("max_factor_values")) {
            auto maxValuesArray = json.get("max_factor_values");
            for (size_t i = 0; i < maxValuesArray.size(); ++i) {
                result.maxFactorValues.push_back(maxValuesArray.at(i).asDouble());
            }
        }
        if (json.has("long_short_return")) result.longShortReturn = json.get("long_short_return").asDouble();
        if (json.has("top_group_return")) result.topGroupReturn = json.get("top_group_return").asDouble();
        if (json.has("bottom_group_return")) result.bottomGroupReturn = json.get("bottom_group_return").asDouble();
        return result;
    }
};

// 完整回测结果
struct BacktestResult {
    foundation::utils::Uuid resultId;
    std::string instanceId;
    std::string instanceName;
    BacktestConfig config;
    
    // 数据状态
    DataStatus dataStatus;
    double dataCoverage = 0.0;
    
    // 计算结果
    ICIRResult icirResult;
    GroupBacktestResult groupResult;
    
    // 性能指标
    double annualReturn = 0.0;
    double benchmarkAnnualReturn = 0.0;
    double excessAnnualReturn = 0.0;
    double sharpeRatio = 0.0;
    double trackingError = 0.0;
    double informationRatio = 0.0;
    double alpha = 0.0;
    double beta = 0.0;
    double maxDrawdown = 0.0;
    double winRate = 0.0;
    double profitFactor = 0.0;
    double turnoverRate = 0.0;
    
    // 风控指标
    double volatility = 0.0;          // 年化波动率
    double downsideDeviation = 0.0;   // 下行偏差
    double sortinoRatio = 0.0;        // Sortino比率
    double calmarRatio = 0.0;         // Calmar比率
    double valueAtRisk = 0.0;         // VaR (95%)
    double conditionalVaR = 0.0;      // CVaR / Expected Shortfall (95%)
    int riskTriggeredCount = 0;       // 风控触发次数
    std::string riskControlSummary;   // 风控摘要
    
    // 元数据
    int executionTimeMs = 0;
    std::string status;  // SUCCESS, FAILED, PARTIAL
    std::string errorMessage;
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();

        json.set("result_id", detail::toJsonValue(resultId.to_string()));
        json.set("instance_id", detail::toJsonValue(instanceId));
        json.set("instance_name", detail::toJsonValue(instanceName));
        json.set("config", config.toJson());
        json.set("data_status", dataStatus.toJson());
        json.set("data_coverage", detail::toJsonValue(dataCoverage));
        json.set("icir_result", icirResult.toJson());
        json.set("group_result", groupResult.toJson());

        json.set("annual_return", detail::toJsonValue(annualReturn));
        json.set("benchmark_annual_return", detail::toJsonValue(benchmarkAnnualReturn));
        json.set("excess_annual_return", detail::toJsonValue(excessAnnualReturn));
        json.set("sharpe_ratio", detail::toJsonValue(sharpeRatio));
        json.set("tracking_error", detail::toJsonValue(trackingError));
        json.set("information_ratio", detail::toJsonValue(informationRatio));
        json.set("alpha", detail::toJsonValue(alpha));
        json.set("beta", detail::toJsonValue(beta));
        json.set("max_drawdown", detail::toJsonValue(maxDrawdown));
        json.set("win_rate", detail::toJsonValue(winRate));
        json.set("profit_factor", detail::toJsonValue(profitFactor));
        json.set("turnover_rate", detail::toJsonValue(turnoverRate));

        json.set("volatility", detail::toJsonValue(volatility));
        json.set("downside_deviation", detail::toJsonValue(downsideDeviation));
        json.set("sortino_ratio", detail::toJsonValue(sortinoRatio));
        json.set("calmar_ratio", detail::toJsonValue(calmarRatio));
        json.set("value_at_risk", detail::toJsonValue(valueAtRisk));
        json.set("conditional_var", detail::toJsonValue(conditionalVaR));
        json.set("risk_triggered_count", detail::toJsonValue(riskTriggeredCount));
        if (!riskControlSummary.empty()) {
            json.set("risk_control_summary", detail::toJsonValue(riskControlSummary));
        }

        json.set("execution_time_ms", detail::toJsonValue(executionTimeMs));
        json.set("status", detail::toJsonValue(status));
        if (!errorMessage.empty()) {
            json.set("error_message", detail::toJsonValue(errorMessage));
        }
        
        return json;
    }

    static BacktestResult fromJson(const foundation::json::JsonFacade& json) {
        BacktestResult result;
        if (json.has("result_id")) result.resultId = foundation::utils::Uuid::from_string(json.get("result_id").asString());
        if (json.has("instance_id")) result.instanceId = json.get("instance_id").asString();
        if (json.has("instance_name")) result.instanceName = json.get("instance_name").asString();
        if (json.has("config")) result.config.fromJson(json.get("config"));
        if (json.has("data_status")) result.dataStatus = DataStatus::fromJson(json.get("data_status"));
        if (json.has("data_coverage")) result.dataCoverage = json.get("data_coverage").asDouble();
        if (json.has("icir_result")) result.icirResult = ICIRResult::fromJson(json.get("icir_result"));
        if (json.has("group_result")) result.groupResult = GroupBacktestResult::fromJson(json.get("group_result"));
        if (json.has("annual_return")) result.annualReturn = json.get("annual_return").asDouble();
        if (json.has("benchmark_annual_return")) result.benchmarkAnnualReturn = json.get("benchmark_annual_return").asDouble();
        if (json.has("excess_annual_return")) result.excessAnnualReturn = json.get("excess_annual_return").asDouble();
        if (json.has("sharpe_ratio")) result.sharpeRatio = json.get("sharpe_ratio").asDouble();
        if (json.has("tracking_error")) result.trackingError = json.get("tracking_error").asDouble();
        if (json.has("information_ratio")) result.informationRatio = json.get("information_ratio").asDouble();
        if (json.has("alpha")) result.alpha = json.get("alpha").asDouble();
        if (json.has("beta")) result.beta = json.get("beta").asDouble();
        if (json.has("max_drawdown")) result.maxDrawdown = json.get("max_drawdown").asDouble();
        if (json.has("win_rate")) result.winRate = json.get("win_rate").asDouble();
        if (json.has("profit_factor")) result.profitFactor = json.get("profit_factor").asDouble();
        if (json.has("turnover_rate")) result.turnoverRate = json.get("turnover_rate").asDouble();
        if (json.has("volatility")) result.volatility = json.get("volatility").asDouble();
        if (json.has("downside_deviation")) result.downsideDeviation = json.get("downside_deviation").asDouble();
        if (json.has("sortino_ratio")) result.sortinoRatio = json.get("sortino_ratio").asDouble();
        if (json.has("calmar_ratio")) result.calmarRatio = json.get("calmar_ratio").asDouble();
        if (json.has("value_at_risk")) result.valueAtRisk = json.get("value_at_risk").asDouble();
        if (json.has("conditional_var")) result.conditionalVaR = json.get("conditional_var").asDouble();
        if (json.has("risk_triggered_count")) result.riskTriggeredCount = json.get("risk_triggered_count").asInt();
        if (json.has("risk_control_summary")) result.riskControlSummary = json.get("risk_control_summary").asString();
        if (json.has("execution_time_ms")) result.executionTimeMs = json.get("execution_time_ms").asInt();
        if (json.has("status")) result.status = json.get("status").asString();
        if (json.has("error_message")) result.errorMessage = json.get("error_message").asString();
        return result;
    }
    
    static BacktestResult createError(const std::string& instanceId,
                                      const std::string& errorMsg) {
        BacktestResult result;
        result.resultId = foundation::utils::Uuid::generate_v4();
        result.instanceId = instanceId;
        result.status = "FAILED";
        result.errorMessage = errorMsg;
        return result;
    }
};

// 回测执行器
class FactorBacktestExecutor {
public:
    struct ExecutionHandle {
        foundation::utils::Uuid taskId;
        std::future<BacktestResult> future;
    };

    struct CachedMarketIndex {
        std::vector<std::string> tradeDates;
        std::unordered_map<std::string, std::vector<std::string>> symbolsByDate;
        struct CachedSymbolBar {
            std::string tradeDate;
            double close = 0.0;
            double futureReturn = std::numeric_limits<double>::quiet_NaN();
        };
        std::unordered_map<std::string, std::vector<CachedSymbolBar>> closeSeriesBySymbol;
    };

    FactorBacktestExecutor(std::shared_ptr<FactorInstanceManager> instanceManager,
                          std::shared_ptr<foundation::thread::ThreadPoolExecutor> threadPool,
                          std::shared_ptr<FactorCacheManager> cacheManager = nullptr);
    ~FactorBacktestExecutor() = default;
    
    // 同步执行回测
    BacktestResult execute(const BacktestConfig& config);
    
    // 异步执行回测
    std::future<BacktestResult> executeAsync(const BacktestConfig& config);

    // 异步执行回测并返回可轮询的任务句柄
    ExecutionHandle executeTrackedAsync(const BacktestConfig& config);
    
    // 批量执行回测
    std::vector<BacktestResult> executeBatch(const std::vector<BacktestConfig>& configs);
    
    // 获取回测进度
    struct ProgressInfo {
        foundation::utils::Uuid taskId;
        std::string instanceId;
        int progress = 0;  // 0-100
        std::string status;
        std::string currentStep;
        
        foundation::json::JsonFacade toJson() const {
            auto json = foundation::json::JsonFacade::createObject();
            json.set("task_id", detail::toJsonValue(taskId.to_string()));
            json.set("instance_id", detail::toJsonValue(instanceId));
            json.set("progress", detail::toJsonValue(progress));
            json.set("status", detail::toJsonValue(status));
            json.set("current_step", detail::toJsonValue(currentStep));
            return json;
        }
    };
    
    ProgressInfo getProgress(const foundation::utils::Uuid& taskId) const;
    
    // 取消回测
    bool cancel(const foundation::utils::Uuid& taskId);
    
private:
    struct ExecutionMarketContext {
        std::vector<std::string> tradeDates;
        std::unordered_set<std::string> allowedSymbols;
        std::unordered_map<std::string, std::vector<std::string>> symbolsByDate;
        std::shared_ptr<ArrowMarketData> arrowData;
    };

    std::shared_ptr<FactorInstanceManager> instanceManager_;
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> threadPool_;
    std::shared_ptr<FactorCacheManager> cacheManager_;

    mutable std::mutex cachedMarketIndexMutex_;
    std::unordered_map<std::string, CachedMarketIndex> cachedMarketIndexCache_;

    mutable std::mutex marketContextMutex_;
    std::unordered_map<std::string, ExecutionMarketContext> marketContextCache_;

    mutable std::mutex futureReturnCacheMutex_;
    std::unordered_map<std::string, double> futureReturnCache_;
    
    // 任务管理
    mutable std::mutex taskMutex_;
    std::map<foundation::utils::Uuid, ProgressInfo> activeTasks_;
    std::unordered_set<std::string> cancelledTasks_;
    
    // 内部执行方法
    BacktestResult executeInternal(const BacktestConfig& config,
                                  ProgressInfo& progress);
    
    // 计算步骤
    bool prepareData(const BacktestConfig& config,
                     ProgressInfo& progress,
                     std::shared_ptr<BaseFactor>& factor,
                     std::string* failureReason = nullptr);
    
    bool calculateFactorSeries(const BacktestConfig& config,
                               const ExecutionMarketContext& marketContext,
                               std::shared_ptr<BaseFactor> factor,
                               ProgressInfo& progress,
                               std::vector<CalculationResult>& factorResults,
                               std::function<bool(CalculationResult&&)>* resultConsumer = nullptr,
                               std::string* failureReason = nullptr,
                               size_t progressBaseUnits = 0,
                               size_t totalWorkUnits = 1,
                               size_t* completedWorkUnits = nullptr);
    
    bool calculateReturnSeries(const BacktestConfig& config,
                               const ExecutionMarketContext& marketContext,
                               const CachedMarketIndex* cachedMarketIndex,
                               ProgressInfo& progress,
                               std::vector<CalculationResult>& returnResults,
                               size_t progressBaseUnits = 0,
                               size_t totalWorkUnits = 1,
                               size_t* completedWorkUnits = nullptr);
    
    bool calculateICIR(const std::vector<CalculationResult>& factorResults,
                       const std::vector<CalculationResult>& returnResults,
                       ProgressInfo& progress,
                       ICIRResult& icirResult);
    
    bool executeGroupBacktest(const std::vector<CalculationResult>& factorResults,
                              const std::vector<CalculationResult>& returnResults,
                              const BacktestConfig& config,
                              ProgressInfo& progress,
                              GroupBacktestResult& groupResult,
                              std::vector<double>* longShortSeries = nullptr,
                              std::vector<double>* turnoverSeries = nullptr,
                              std::vector<std::string>* longShortDates = nullptr,
                              std::string* failureReason = nullptr);
    
    // 辅助方法
    std::vector<std::string> getTradeDates(const std::string& startDate,
                                                         const std::string& endDate,
                                                         const BacktestConfig& config);
    
    std::vector<std::string> getSymbols(const std::string& date,
                                                     const std::unordered_set<std::string>& allowedSymbols,
                                                     const BacktestConfig& config);
    
    double calculateFutureReturn(const std::string& symbol,
                                 const std::string& startDate,
                                 int forwardDays,
                                 const BacktestConfig& config,
                                 const CachedMarketIndex* cachedMarketIndex = nullptr);

    bool prepareExecutionMarketContext(const BacktestConfig& config,
                                       ExecutionMarketContext& marketContext,
                               CachedMarketIndex* cachedMarketIndex = nullptr,
                               std::string* failureReason = nullptr,
                               ProgressInfo* progress = nullptr);

    bool prepareCachedExecutionMarketContext(const BacktestConfig& config,
                                   ExecutionMarketContext& marketContext,
                                   CachedMarketIndex& cachedMarketIndex,
                                   std::string* failureReason = nullptr,
                                   ProgressInfo* progress = nullptr);

    CachedMarketIndex buildCachedMarketIndex(const std::vector<CachedMarketBar>& cachedBars,
                                             int forwardDays) const;
    
    // 更新进度
    void updateProgress(ProgressInfo& progress,
                        int newProgress,
                        const std::string& step);

    ProgressInfo createProgressInfo(const std::string& instanceId) const;
    void registerTask(const ProgressInfo& progress);
    void finalizeTask(const foundation::utils::Uuid& taskId);
    BacktestResult executeTracked(const BacktestConfig& config,
                                  ProgressInfo progress);

    bool isCancelled(const foundation::utils::Uuid& taskId) const;
};

} // namespace factor
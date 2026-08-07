#pragma once

#include "IOrderListener.h"
#include "StrategyServiceTypes.h"
#include "StrategySnapshotTypes.h"
#include "IFactorSvc.h"
#include "TradeJournal.h"
#include "DailyEodScheduler.h"
#include "FactorSignalProcessor.h"
#include "SignalBlendCompositor.h"
#include "RuleGate.h"
#include "RuleAttribution.h"
#include "RulePipeline.h"
#include "RiskEvaluator.h"
#include "OrderGenerator.h"
#include "MarketTimingGate.h"
#include "TimedCircuitBreaker.h"
#include "../../trading/include/OrderBuilder.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <vector>

namespace foundation {
namespace thread {
class IExecutor;
}
}

namespace engine {
struct AccountInfo;
}

namespace astock { namespace database { class ISqlDatabase; class SqlQueryResultRow; } }

namespace domain::backtest {
struct BacktestRequest;
}

namespace factor {
namespace compute {
class IMarketDataView;
class BacktestDataService;
}
}

namespace domain::strategy {

class IRuntimeFactorView {
public:
    virtual ~IRuntimeFactorView() = default;

    virtual void copySnapshots(std::vector<RuntimeFactorSnapshot>& outputSnapshots) const = 0;
};

class IRuntimeFactorService : public IRuntimeFactorView {
public:
    virtual ~IRuntimeFactorService() = default;

    [[nodiscard]] virtual StrategyServiceFlowResult updateIncremental(
        const MarketDataPoint& marketDataPoint) = 0;
    [[nodiscard]] virtual StrategyServiceFlowResult updateBatch(
        const std::vector<MarketDataPoint>& batch) = 0;

    // ── 因子配置 ──
    /// @brief 设置关注的因子实例 ID 列表（NoOpFactorService: no-op）
    virtual void setFactorIds(const std::vector<std::string>& ids) = 0;

    // ── 数据注入 ──
    /// @brief 注入回测数据服务（生命周期由调用方管理；NoOpFactorService: no-op）
    virtual void setDataService(factor::compute::BacktestDataService* dataSvc) = 0;
    /// @brief 注入实盘行情视图（生命周期由调用方管理；NoOpFactorService: no-op）
    virtual void setLiveMarketView(const factor::compute::IMarketDataView* view) = 0;

    // ── 行情视图构建与访问 ──
    /// @brief 从 SQL 查询结果构建实盘 MarketView（NoOpFactorService: no-op）
    virtual void buildLiveView(
        const std::vector<astock::database::SqlQueryResultRow>& rows,
        const std::vector<std::string>& extraFields) = 0;
    /// @brief 获取当前行情视图；无数据返回 nullptr；生命周期与 IRuntimeFactorService 实例一致
    [[nodiscard]] virtual const factor::compute::IMarketDataView* liveView() const = 0;

    // ── 因子元数据查询 ──
    /// @brief 从因子需求收集所需的数据字段（NoOpFactorService: 返回空）
    [[nodiscard]] virtual std::vector<std::string> getRequiredFields() const = 0;
    /// @brief 从因子需求计算最大回溯窗口（日历日；NoOpFactorService: 返回 90）
    [[nodiscard]] virtual int getMaxLookbackDays() const = 0;

    // ── 回测因子值查询 ──
    /// @brief 按日期查询因子的 symbol→value 映射；无数据返回 nullptr；
    ///     返回值生命周期与 IRuntimeFactorService 实例一致，调用方不应持有
    [[nodiscard]] virtual const std::map<std::string, double>* backtestValuesBySymbol(
        const std::string& instanceId, std::int32_t date) const = 0;
};

namespace rules {

using RuleId = std::uint32_t;
using RuleSetId = std::uint32_t;

inline constexpr RuleSetId kRuleSetAllPass = 0;

enum class RuleEvaluationPhase : std::uint8_t {
    Batch = 0,
    LowLatency = 1,
};

struct RuleEvaluationContext final {
private:
    RuleEvaluationPhase phase_{RuleEvaluationPhase::Batch};
    StrategyInstanceId strategyInstanceId_{0};
    StrategyCount candidateSignalCount_{0};

public:
    RuleEvaluationContext() = default;
    RuleEvaluationContext(
        RuleEvaluationPhase phase,
        StrategyInstanceId strategyInstanceId,
        StrategyCount candidateSignalCount);

    [[nodiscard]] RuleEvaluationPhase phase() const noexcept;
    [[nodiscard]] StrategyInstanceId strategyInstanceId() const noexcept;
    [[nodiscard]] StrategyCount candidateSignalCount() const noexcept;
};

struct RuleSet final {
private:
    RuleSetId id_{kRuleSetAllPass};
    std::vector<RuleId> rules_;

public:
    RuleSet() = default;
    RuleSet(RuleSetId id, const std::vector<RuleId>& rules);

    [[nodiscard]] RuleSetId id() const noexcept;
    [[nodiscard]] const std::vector<RuleId>& rules() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
};

} // namespace rules

struct RuleEvaluationResult final {
private:
    bool passed_{false};
    StrategySignal signal_{};
    RuleRejectReason rejectReason_{RuleRejectReason::RuleTemplateBlocked};
    std::chrono::microseconds latency_{0};

public:
    RuleEvaluationResult() = default;
    RuleEvaluationResult(bool passed,
                         const StrategySignal& signal,
                         RuleRejectReason rejectReason,
                         std::chrono::microseconds latency);

    [[nodiscard]] bool passed() const noexcept;
    [[nodiscard]] const StrategySignal& signal() const noexcept;
    [[nodiscard]] RuleRejectReason rejectReason() const noexcept;
    [[nodiscard]] std::chrono::microseconds latency() const noexcept;
};

class IRuleEvaluationService {
public:
    virtual ~IRuleEvaluationService() = default;

    [[nodiscard]] virtual RuleEvaluationResult evaluate(
        const StrategySignal& signal,
        rules::RuleSetId ruleSetId,
        const rules::RuleEvaluationContext& context) = 0;

    [[nodiscard]] virtual StrategyServiceFlowResult evaluateBatch(
        const std::vector<StrategySignal>& candidateSignals,
        rules::RuleSetId ruleSetId,
        const rules::RuleEvaluationContext& context,
        std::vector<RuleEvaluationResult>& outputResults) = 0;

    [[nodiscard]] virtual bool isReady() const = 0;
};

class IRuleSetManager {
public:
    virtual ~IRuleSetManager() = default;

    virtual void saveRuleSet(const rules::RuleSet& ruleSet) = 0;
    [[nodiscard]] virtual std::optional<rules::RuleSet> ruleSet(rules::RuleSetId id) const = 0;
    [[nodiscard]] virtual std::vector<rules::RuleId> availableRules() const = 0;
};

enum class PythonRuleKind : std::uint16_t {
    Invalid = 0,
    ScoreNonNegative = 1,
    TargetWeightAbsoluteLimit = 2,
    Custom = 1024,
};

struct PythonRuleDescriptor final {
private:
    PythonRuleKind kind_{PythonRuleKind::Invalid};
    bool enabled_{false};
    double thresholdA_{0.0};
    double thresholdB_{0.0};
    StrategyInstanceId strategyScopeId_{0};

public:
    PythonRuleDescriptor() = default;
    PythonRuleDescriptor(PythonRuleKind kind,
                         bool enabled,
                         double thresholdA,
                         double thresholdB,
                         StrategyInstanceId strategyScopeId);

    [[nodiscard]] PythonRuleKind kind() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] double thresholdA() const noexcept;
    [[nodiscard]] double thresholdB() const noexcept;
    [[nodiscard]] StrategyInstanceId strategyScopeId() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
};

struct PythonRuleBatchRequest final {
    std::vector<StrategySignal> candidateSignals;
    std::vector<PythonRuleDescriptor> descriptors;
};

struct PythonRuleResult final {
private:
    bool passed_{false};
    RuleRejectReason rejectReason_{RuleRejectReason::RuleTemplateBlocked};

public:
    PythonRuleResult() = default;
    PythonRuleResult(bool passed, RuleRejectReason rejectReason);

    [[nodiscard]] bool passed() const noexcept;
    [[nodiscard]] RuleRejectReason rejectReason() const noexcept;
};

class IPythonRuleAdapter {
public:
    virtual ~IPythonRuleAdapter() = default;

    [[nodiscard]] virtual StrategyServiceFlowResult checkBatch(
        const PythonRuleBatchRequest& request,
        std::vector<PythonRuleResult>& outputResults) = 0;
};

class IRuntimeStrategy {
public:
    virtual ~IRuntimeStrategy() = default;

    [[nodiscard]] virtual StrategyInstanceId instanceId() const noexcept = 0;
    [[nodiscard]] virtual bool isEnabled() const noexcept = 0;
    [[nodiscard]] virtual rules::RuleSetId ruleSetId() const noexcept = 0;

    /// @brief 策略是否依赖因子计算（因子策略=true，非因子策略=false）
    /// 在 fromDb 创建时由策略类型决定，供 prepareMarketData 等路径使用。
    [[nodiscard]] virtual bool usesFactors() const noexcept = 0;

    // 策略只消费当前因子结果快照，不持有更新职责，也不接触桥接对象。
    virtual void evaluate(const std::vector<RuntimeFactorSnapshot>& factorSnapshots,
                          const RuntimeStrategyContext& context,
                          std::vector<StrategySignal>& outputSignals) = 0;
};

class IRuntimeOrderSink {
public:
    virtual ~IRuntimeOrderSink() = default;

    [[nodiscard]] virtual StrategyServiceFlowResult submit(const OrderRequest& order) = 0;
};

class IDiagnosticsSink {
public:
    virtual ~IDiagnosticsSink() = default;

    virtual void publish(const DiagnosticsEvent& event) = 0;
};

class IOrderBuilder {
public:
    virtual ~IOrderBuilder() = default;

    [[nodiscard]] virtual StrategyServiceFlowResult buildOrder(
        const StrategySignal& signal,
        const RuntimeStrategyContext& context,
        OrderRequest& outputOrder) const = 0;
};

class IStrategyService {
public:
    virtual ~IStrategyService() = default;

    [[nodiscard]] virtual StrategyServiceFlowResult configureExecutionPlan(
        const StrategyServiceExecutionPlan& plan) = 0;
    [[nodiscard]] virtual StrategyServiceExecutionPlan executionPlan() const = 0;

    virtual void setDiagnosticsSink(IDiagnosticsSink* diagnosticsSink) = 0;
    virtual void setOrderBuilder(const IOrderBuilder* orderBuilder) = 0;

    [[nodiscard]] virtual StrategyServiceFlowResult start() = 0;
    [[nodiscard]] virtual StrategyServiceFlowResult pause() = 0;
    [[nodiscard]] virtual StrategyServiceFlowResult resume() = 0;
    [[nodiscard]] virtual StrategyServiceFlowResult stop() = 0;
    [[nodiscard]] virtual StrategyServiceState state() const = 0;

    [[nodiscard]] virtual StrategyServiceFlowResult registerStrategy(
        std::shared_ptr<IRuntimeStrategy> strategy,
        const RuntimeStrategyContext& context) = 0;
    [[nodiscard]] virtual StrategyServiceFlowResult unregisterStrategy(
        StrategyInstanceId strategyInstanceId) = 0;
    [[nodiscard]] virtual StrategyServiceFlowResult clearStrategies() = 0;

    [[nodiscard]] virtual StrategyServiceFlowResult onMarketDataPoint(
        const MarketDataPoint& marketDataPoint) = 0;
    [[nodiscard]] virtual StrategyServiceFlowResult onMarketDataBatch(
        const std::vector<MarketDataPoint>& batch) = 0;

    [[nodiscard]] virtual StrategyExecutionStats lastExecutionStats() const = 0;
    [[nodiscard]] virtual StrategyCount pendingOrderCount() const = 0;
    virtual void copyPendingOrders(std::vector<OrderRequest>& outputOrders) const = 0;

    virtual void setContextHistoricalView(const void* view) = 0;
    /// @brief 设置所有策略上下文的当前评估行号 (回测逐日推进用, -1=实盘)
    virtual void setContextEvaluationRow(int row) = 0;
    virtual void updateCurrentWeights(const std::unordered_map<std::string, double>& weights) = 0;
    /// @brief 更新所有已注册策略上下文的因子候选池（空池=扫全市场）
    virtual void updateCandidatePool(const std::unordered_set<std::string>& pool) = 0;
    /// @brief 更新所有已注册策略上下文的因子复合评分 (按策略权重加权)
    virtual void updateFactorScores(std::unordered_map<std::string, double> scores) = 0;
};

class LocalRuleEvaluationService final : public IRuleEvaluationService,
                                         public IRuleSetManager {
public:
    LocalRuleEvaluationService();

    [[nodiscard]] RuleEvaluationResult evaluate(
        const StrategySignal& signal,
        rules::RuleSetId ruleSetId,
        const rules::RuleEvaluationContext& context) override;

    [[nodiscard]] StrategyServiceFlowResult evaluateBatch(
        const std::vector<StrategySignal>& candidateSignals,
        rules::RuleSetId ruleSetId,
        const rules::RuleEvaluationContext& context,
        std::vector<RuleEvaluationResult>& outputResults) override;

    [[nodiscard]] bool isReady() const override;

    void saveRuleSet(const rules::RuleSet& ruleSet) override;
    [[nodiscard]] std::optional<rules::RuleSet> ruleSet(rules::RuleSetId id) const override;
    [[nodiscard]] std::vector<rules::RuleId> availableRules() const override;

private:
    static constexpr rules::RuleId kRuleScoreNonNegative = 1;
    static constexpr rules::RuleId kRuleTargetWeightAbsLimit = 2;
    static constexpr double kMinSignalScore = 0.0;
    static constexpr double kMaxAbsoluteTargetWeight = 1.0;

private:
    mutable std::mutex ruleSetsMutex_;
    std::vector<rules::RuleSet> ruleSets_;
};

class PythonRuleEvaluationService final : public IRuleEvaluationService,
                                          public IRuleSetManager {
public:
    explicit PythonRuleEvaluationService(IPythonRuleAdapter& adapter);

    [[nodiscard]] RuleEvaluationResult evaluate(
        const StrategySignal& signal,
        rules::RuleSetId ruleSetId,
        const rules::RuleEvaluationContext& context) override;

    [[nodiscard]] StrategyServiceFlowResult evaluateBatch(
        const std::vector<StrategySignal>& candidateSignals,
        rules::RuleSetId ruleSetId,
        const rules::RuleEvaluationContext& context,
        std::vector<RuleEvaluationResult>& outputResults) override;

    [[nodiscard]] bool isReady() const override;

    void saveRuleSet(const rules::RuleSet& ruleSet) override;
    [[nodiscard]] std::optional<rules::RuleSet> ruleSet(rules::RuleSetId id) const override;
    [[nodiscard]] std::vector<rules::RuleId> availableRules() const override;

private:
    [[nodiscard]] std::vector<PythonRuleDescriptor> buildDescriptorsForRuleSet(
        rules::RuleSetId ruleSetId) const;

    [[nodiscard]] StrategyServiceFlowResult evaluateBatchInternal(
        const std::vector<StrategySignal>& candidateSignals,
        rules::RuleSetId ruleSetId,
        std::vector<PythonRuleResult>& outputResults) const;

private:
    IPythonRuleAdapter& adapter_;
    mutable std::mutex ruleSetsMutex_;
    std::vector<rules::RuleSet> ruleSets_;
};

class StrategyService final : public IStrategyService {
public:
    StrategyService(IRuntimeFactorService& factorService,
                    IRuleEvaluationService& ruleEvaluationService);

    [[nodiscard]] StrategyServiceFlowResult configureExecutionPlan(
        const StrategyServiceExecutionPlan& plan) override;

    [[nodiscard]] StrategyServiceExecutionPlan executionPlan() const override;

    void setDiagnosticsSink(IDiagnosticsSink* diagnosticsSink) override;
    void setOrderBuilder(const IOrderBuilder* orderBuilder) override;

    [[nodiscard]] StrategyServiceFlowResult start() override;
    [[nodiscard]] StrategyServiceFlowResult pause() override;
    [[nodiscard]] StrategyServiceFlowResult resume() override;
    [[nodiscard]] StrategyServiceFlowResult stop() override;
    [[nodiscard]] StrategyServiceState state() const override;

    [[nodiscard]] StrategyServiceFlowResult registerStrategy(
        std::shared_ptr<IRuntimeStrategy> strategy,
        const RuntimeStrategyContext& context) override;

    [[nodiscard]] StrategyServiceFlowResult unregisterStrategy(
        StrategyInstanceId strategyInstanceId) override;

    [[nodiscard]] StrategyServiceFlowResult clearStrategies() override;

    [[nodiscard]] StrategyServiceFlowResult onMarketDataPoint(
        const MarketDataPoint& marketDataPoint) override;

    [[nodiscard]] StrategyServiceFlowResult onMarketDataBatch(
        const std::vector<MarketDataPoint>& batch) override;


    [[nodiscard]] StrategyExecutionStats lastExecutionStats() const override;
    [[nodiscard]] StrategyCount pendingOrderCount() const override;
    void copyPendingOrders(std::vector<OrderRequest>& outputOrders) const override;

private:
    struct StrategyRuntimeEntry final {
        std::shared_ptr<IRuntimeStrategy> strategy;
        RuntimeStrategyContext context;
    };

    [[nodiscard]] StrategyServiceFlowResult evaluateAndCheckRulesBatch();
    [[nodiscard]] StrategyServiceFlowResult evaluateAndCheckRulesLowLatency();
    [[nodiscard]] StrategyServiceFlowResult evaluateEntrySignals(
        const StrategyRuntimeEntry& entry,
        StrategyCount& generatedSignalCount);
    [[nodiscard]] StrategyServiceFlowResult handleRuleEvaluationResult(
        const RuleEvaluationResult& result,
        StrategyCount& passedCount,
        StrategyCount& rejectedCount);
    [[nodiscard]] OrderRequest buildOrderRequest(
        const StrategySignal& signal,
        const RuntimeStrategyContext& context) const;
    [[nodiscard]] const RuntimeStrategyContext* findContext(StrategyInstanceId strategyInstanceId) const;
    void publishDiagnostics(const DiagnosticsEvent& event);
    void reserveWorkingBuffers();
    void resetStats();

    /// @brief 为所有已注册策略注入历史数据视图 (非因子策略需要)
    void setContextHistoricalView(const void* view);
    /// @brief 设置所有策略上下文的当前评估行号 (回测逐日推进用, -1=实盘)
    void setContextEvaluationRow(int row) override;
    void updateCurrentWeights(const std::unordered_map<std::string, double>& weights) override;
    void updateCandidatePool(const std::unordered_set<std::string>& pool) override;
    void updateFactorScores(std::unordered_map<std::string, double> scores) override;

private:
    IRuntimeFactorService& factorService_;
    IRuleEvaluationService& ruleEvaluationService_;
    IDiagnosticsSink* diagnosticsSink_{nullptr};
    const IOrderBuilder* orderBuilder_{nullptr};
    StrategyServiceState state_{StrategyServiceState::Stopped};
    StrategyServiceExecutionPlan plan_;
    StrategyExecutionStats stats_;
    std::vector<StrategyRuntimeEntry> strategyEntries_;
    std::vector<RuntimeFactorSnapshot> factorSnapshotBuffer_;
    std::vector<StrategySignal> signalBuffer_;
    std::vector<RuleEvaluationResult> ruleResultBuffer_;
    mutable std::vector<OrderRequest> pendingOrderBuffer_;
    mutable std::mutex mutex_;
};

class DefaultOrderBuilder final : public IOrderBuilder {
public:
    [[nodiscard]] StrategyServiceFlowResult buildOrder(
        const StrategySignal& signal,
        const RuntimeStrategyContext& context,
        OrderRequest& outputOrder) const override;
};

/// @brief 因子覆盖层配置 — 纯值类型，无行为，构造后不可变
struct FactorOverlayConfig {
    std::vector<FactorFilterConfig> filters;
    std::vector<FactorScaleConfig> scalers;
    std::unordered_map<std::string, double> factorInfluence;
    int targetPositionCount{50};
    double minimumCompositeScore{0.0};
    FactorCombineMode combineMode{FactorCombineMode::RankOnly};
    bool needsMarketCapField{false};
    bool enabled{false};

    [[nodiscard]] bool isValid() const noexcept { return enabled ? !filters.empty() : true; }
};

/// @brief 规则闸门配置 — 纯值类型
struct RuleGateConfig {
    std::vector<std::string> templateIds;
    std::vector<std::string> ablatedTemplateIds;  // 消融测试: 跳过的模板
    bool ablationEnabled{false};                   // 是否启用消融模式
    bool enableCandlePatterns{false};              // 是否启用 TA-Lib 蜡烛形态计算

    [[nodiscard]] bool enabled() const noexcept { return !templateIds.empty(); }
};

/// @brief 调仓频率配置 — 纯值类型
struct RebalanceConfig {
    int interval{1};                  // 调仓间隔(交易日), 0=从不调仓
    bool isDailyFrequency{true};      // true=日频(不启动drainQueue), false=分钟频/高频

    [[nodiscard]] bool isValid() const noexcept { return interval >= 0; }
};

class StrategyEngine final {
public:
    class Builder;

    static Builder builder();

    /// @brief 从数据库通过 strategyId 加载参数并构建引擎（接管 factorSvc 所有权）
    /// @param factorSvc 因子服务 (unique_ptr, 传 nullptr 则只支持非因子策略)
    [[nodiscard]] static std::unique_ptr<StrategyEngine> fromDb(const std::string& strategyId,
                                                                 std::unique_ptr<IRuntimeFactorService> factorSvc = nullptr);

    /// @brief 从策略参数构建完整的引擎实例
    [[nodiscard]] static std::unique_ptr<StrategyEngine> fromParams(const StrategyCreationParams& params);

    StrategyEngine(std::unique_ptr<IRuntimeFactorService> factorService,
                   std::unique_ptr<IRuleEvaluationService> ruleEvaluationService,
                   std::unique_ptr<IStrategyService> strategyService);

    /// @brief 析构时自动停止实盘循环，避免后台线程访问已销毁对象
    ~StrategyEngine();

    [[nodiscard]] StrategyServiceFlowResult registerStrategy(
        std::shared_ptr<IRuntimeStrategy> strategy,
        const RuntimeStrategyContext& context);
    [[nodiscard]] StrategyServiceFlowResult registerStrategies(
        const std::vector<std::shared_ptr<IRuntimeStrategy>>& strategies,
        const std::vector<RuntimeStrategyContext>& contexts);

    [[nodiscard]] StrategyServiceFlowResult start();
    [[nodiscard]] StrategyServiceFlowResult pause();
    [[nodiscard]] StrategyServiceFlowResult resume();
    [[nodiscard]] StrategyServiceFlowResult stop();

    [[nodiscard]] std::optional<std::vector<OrderRequest>> step(const MarketDataPoint& marketDataPoint);
    [[nodiscard]] std::optional<std::vector<OrderRequest>> stepBatch(
        const std::vector<MarketDataPoint>& batch);

    [[nodiscard]] std::future<std::optional<std::vector<OrderRequest>>> stepAsync(
        const MarketDataPoint& marketDataPoint);
    [[nodiscard]] std::future<std::optional<std::vector<OrderRequest>>> stepBatchAsync(
        std::vector<MarketDataPoint> batch);

    [[nodiscard]] IStrategyService& service() noexcept;
    [[nodiscard]] const IStrategyService& service() const noexcept;
    void setAsyncExecutor(std::shared_ptr<foundation::thread::IExecutor> executor);

    // ─── 回测接口 ───

    /// @brief 执行策略回测（逐日驱动引擎 → 模拟成交 → 指标计算）
    /// @param dataSvc 已加载数据的数据服务（由调用方构建，避免重复解析 JSON）
    [[nodiscard]] StrategyBacktestResult backtest(const domain::backtest::BacktestRequest& req,
                                                   factor::compute::BacktestDataService* dataSvc,
                                                   const std::function<void(double)>& onProgress = {});

    // ─── 实盘异步专有接口 ───

    /// @brief 启动专属后台线程。
    /// 日频策略: 只启 riskPatrolLoop (止损巡检) + 注册 EOD 回调
    /// 分钟频/高频: 启 drainQueue (持续评估+下单+巡检)
    void startLiveLoop();

    /// @brief 安全停止后台线程并等待完成。
    void stopLiveLoop();

    /// @brief 查询实盘循环是否正在运行
    [[nodiscard]] bool isLiveLoopRunning() const noexcept;

    /// @brief 是否为日频策略 (fromDb 时根据 behaviorKind 自动设置)
    [[nodiscard]] bool isDailyFrequency() const noexcept { return m_isDailyFrequency; }

    /// @brief 日终评估: 跑一次完整策略评估并生成订单
    /// @param tradingDay 评估目标交易日
    /// @param isCompensation true=补单(历史收盘价), false=实时(当日 tick 价)
    /// 由 DailyEodScheduler 触发，在专用线程中执行
    /// @return 评估结果状态，用于决定是否持久化 lastEvalDay
    EodEvaluationStatus evaluateEndOfDay(const std::string& tradingDay, bool isCompensation);

    /// @brief 一键清仓：对所有持仓生成市价卖单，篮子提交
    /// @return 生成的订单数量，-1 表示失败
    int liquidateAll();

    /// @brief 设置订单回调监听器，所有订单通过此回调通知。
    void setOrderListener(IOrderListener* listener);

    /// @brief 为所有已注册策略注入历史数据视图 (非因子策略需要)
    void setContextHistoricalView(const void* view);

    /// @brief 设置实盘行情视图，供因子计算时提供 HistoricalView
    /// @param view 包含足够回溯窗口的行情数据视图 (不为 Engine 所有，调用方保证生命周期)
    void setLiveMarketView(const void* view);

    /// @brief 丢弃的 tick 计数（队列满时触发）
    [[nodiscard]] std::int64_t droppedTicks() const noexcept {
        return m_droppedTicks.load(std::memory_order_acquire);
    }

    /// @brief 设置交易账户（风控需要）
    void setAccountId(std::string id) { m_accountId = std::move(id); }
    /// @brief 设置策略ID（止损单必填）
    void setStrategyId(std::string id) { m_strategyId = std::move(id); }
    /// @brief 设置策略名称（用于交易日志目录名）
    void setStrategyName(std::string name) { m_strategyName = std::move(name); }
    /// @brief 获取交易日志（回测/实盘路径记录交易事件）
    TradeJournal* tradeJournal() { return m_tradeJournal.get(); }

    /// @brief 设置实盘数据目录（lastEvalDay JSON 持久化路径前缀）
    void setLiveDataPath(std::string path) { m_liveDataPath = std::move(path); }

    /// @brief 距上次处理 tick 的毫秒数（>5000 可能卡死）
    [[nodiscard]] std::int64_t lastProcessedMsAgo() const noexcept {
        auto last = m_lastProcessedAt.load(std::memory_order_acquire);
        if (last == 0) return -1;
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return (now - last) / 1'000'000;
    }
    /// @brief 根据策略需求自动查询 PG 历史行情并构建 MarketView
    /// 内部自行计算日期范围、连接数据库、构建视图并注入引擎。
    /// 非因子策略: 90 天 OHLCV → 注入 contextHistoricalView
    /// 因子策略:   按因子最大回溯窗口 + 额外字段 → 注入 RuntimeFactorSvc::liveMarketView
    /// @return true 表示历史数据加载成功
    bool prepareMarketData();

    /// @brief 获取当前持有的行情视图（供外部读取元数据）
    /// 因子策略返回 factorService 持有的视图，非因子策略返回 m_liveMarketView
    [[nodiscard]] const factor::compute::IMarketDataView* liveMarketView() const noexcept {
        if (factorService_) {
            auto* v = factorService_->liveView();
            if (v) return v;
        }
        return m_liveMarketView.get();
    }

    /// @brief 获取规则闸门统计数据（评估/命中/拦截/数据缺失，按模板聚合）
    [[nodiscard]] const rules::RuleGateStats& ruleGateStats() const noexcept {
        return m_ruleGate.stats();
    }

    /// @brief 获取最近一次回测的规则归因 (P&L 影响)
    [[nodiscard]] const std::map<std::string, rules::RuleAttribution>& ruleAttribution() const noexcept {
        return m_ruleAttribution;
    }
    /// @brief 最近一次回测的日期区间
    [[nodiscard]] std::string backtestDateRange() const noexcept { return m_backtestDateRange; }

private:
    [[nodiscard]] std::optional<std::vector<OrderRequest>> collectOrders(
        const StrategyServiceFlowResult& flowResult);

    /// @brief 后台线程主函数（分钟频/高频）：阻塞等待行情 → step() → 通知订单。
    void drainQueue();

    // ── evaluateEndOfDay 子函数 ──
    struct EodContext;
    struct EodDayBar;
    struct EodPriceData;
    struct EodGateResult;
    struct PendingOrder;

    [[nodiscard]] bool checkRebalanceDay(const std::string& tradingDay);
    [[nodiscard]] bool prepareEodContext(const std::string& tradingDay, EodContext& ctx);
    [[nodiscard]] bool fetchTodayPrices(const EodContext& ctx, EodPriceData& prices, bool isCompensation = false);
    void computeMarketBreadth(const EodContext& ctx, EodPriceData& prices);
    EodGateResult evaluateEodGates(const EodContext& ctx, const EodPriceData& prices);
    std::vector<PendingOrder> collectEodSignals(
        const EodContext& ctx, const EodPriceData& prices,
        const EodGateResult& gates,
        const std::unordered_map<std::string, std::int64_t>& posQtyMap,
        const std::string& tradingDay);
    EodEvaluationStatus finalizeAndSubmit(
        const EodContext& ctx,
        std::vector<PendingOrder>& pendingOrders,
        const std::unordered_map<std::string, std::int64_t>& posQtyMap,
        const EodPriceData& prices,
        const std::string& tradingDay,
        bool isCompensation);

private:
    std::unique_ptr<IRuntimeFactorService> factorService_;
    std::unique_ptr<IRuleEvaluationService> ruleEvaluationService_;
    std::unique_ptr<IStrategyService> strategyService_;
    std::shared_ptr<foundation::thread::IExecutor> asyncExecutor_;

    // 实盘异步线程 —— 每个引擎独立的专属线程池（1线程）
    std::shared_ptr<foundation::thread::IExecutor> m_dedicatedExecutor;
    std::queue<MarketDataPoint> m_mdpQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    static constexpr size_t kMaxQueueSize = 5000;
    std::atomic<bool> m_loopRunning{false};
    std::atomic<bool> m_isBacktestMode{false};  ///< 回测运行时置位，防御 drainQueue 误触发监听器
    bool m_isDailyFrequency{false};             ///< 日频策略 → 只巡检+日终评估, 不启动 drainQueue
    std::unique_ptr<DailyEodScheduler> m_dailyScheduler;  ///< 日频调度器 (EOD + 补单)
    std::atomic<std::int64_t> m_droppedTicks{0};
    std::atomic<std::int64_t> m_lastProcessedAt{0};
    IOrderListener* m_orderListener{nullptr};
    std::string m_accountId;
    std::string m_strategyId;
    std::string m_strategyName;
    std::unique_ptr<TradeJournal> m_tradeJournal;  // 交易日志 (按策略名/日期分文件)
    std::string m_liveDataPath;     // 实盘数据目录, 用于统一 JSON 持久化
    domain::trading::OrderBuilder m_orderBuilder;
    OrderGenerator m_orderGenerator{m_orderBuilder};  ///< 持仓感知建单器
    std::unique_ptr<factor::compute::IMarketDataView> m_liveMarketView;
    bool m_hasFactorStrategies{false};  ///< 是否有因子策略注册，fromDb 创建时确定
    bool m_needsMarketCapField{false};  ///< 权重方案为市值加权时置位，prepareMarketData 追加 market_cap 字段
    FactorSignalProcessor m_factorSignalProcessor;  ///< 因子信号处理(过滤+缩放)
    std::unique_ptr<ICandidatePoolSelector> m_poolSelector;  ///< 因子候选池选择器(因子定池,策略选点)
    rules::RuleGate m_ruleGate;  ///< 规则管线: 市场闸/信号审核/出场, 绑定规则库+策略模板集
    RulePipeline m_rulePipeline{m_ruleGate};  ///< 规则编排器(封装上下文构建+迭代样板代码)
    bool m_enableCandlePatterns{false};       ///< 是否启用 TA-Lib 蜡烛形态计算
    std::map<std::string, rules::RuleAttribution> m_ruleAttribution;  ///< 最近一次回测的规则归因
    std::string m_backtestDateRange;  ///< 最近一次回测的日期区间 (如 "20200102-20260717")
    RiskConfig m_riskConfig = RiskConfig::defaults();
    MarketTimingGate m_timingGate;             ///< 大盘择时闸门
    TimedCircuitBreaker m_circuitBreaker;      ///< 风控熔断器
    std::unordered_set<std::string> m_liquidationBlocklist;  ///< 当天已清仓标的, 禁止当日再次买入
    int m_rebalanceInterval{1};            ///< 调仓间隔(交易日), 0=从不调仓, 1=每日
    std::string m_lastRebalanceDate;       ///< 上次执行调仓的交易日 YYYYMMDD
    int m_minHoldDays{0};                  ///< 最少持有天数, 0=不启用
    std::unordered_map<std::string, std::int64_t> m_positionEntryDates;  ///< symbol→首次建仓日期 YYYYMMDD
};

class StrategyEngine::Builder final {
public:
    Builder();

    Builder& withFactorService(std::unique_ptr<IRuntimeFactorService> factorService);
    Builder& withRuleEvaluationService(std::unique_ptr<IRuleEvaluationService> ruleEvaluationService);
    Builder& withDiagnosticsSink(IDiagnosticsSink& diagnosticsSink);
    Builder& withOrderBuilder(const IOrderBuilder& orderBuilder);
    Builder& withAsyncExecutor(std::shared_ptr<foundation::thread::IExecutor> executor);
    Builder& maxStrategies(StrategyCount value);
    Builder& maxSignalsPerBatch(StrategyCount value);
    Builder& maxRuleResultsPerBatch(StrategyCount value);
    Builder& maxMarketDataPerBatch(StrategyCount value);

    /// @brief 设置策略 ID（用于订单标识和持久化）
    Builder& withStrategyId(std::string id);

    /// @brief 配置因子覆盖层（过滤/缩放/融合/目标持仓数等）
    Builder& withFactorOverlayConfig(const FactorOverlayConfig& cfg);

    /// @brief 配置规则闸门（模板 ID 列表 × 共享规则库）
    Builder& withRuleGateConfig(const RuleGateConfig& cfg);

    /// @brief 配置风控参数（止损/止盈/回撤/熔断等）
    Builder& withRiskConfig(const RiskConfig& cfg);

    /// @brief 配置调仓频率
    Builder& withRebalanceConfig(const RebalanceConfig& cfg);

    /// @brief 启用择时闸门 (v0.13 规则模式)
    Builder& withTimingGate(const MarketTimingGate& gate);

    /// @brief 配置风控熔断器 (v0.13)
    Builder& withCircuitBreaker(const TimedCircuitBreaker& breaker);

    [[nodiscard]] std::unique_ptr<StrategyEngine> build();

private:
    /// @brief 校验所有配置无冲突，返回 nullptr 表示通过
    [[nodiscard]] std::string validate() const;

    std::unique_ptr<IRuntimeFactorService> factorService_;
    std::unique_ptr<IRuleEvaluationService> ruleEvaluationService_;
    IDiagnosticsSink* diagnosticsSink_{nullptr};
    const IOrderBuilder* orderBuilder_{nullptr};
    std::shared_ptr<foundation::thread::IExecutor> asyncExecutor_;
    StrategyServiceExecutionPlan plan_{defaultExecutionPlan()};

    // ── 策略配置（纯值类型，build() 时移动给 StrategyEngine）──
    std::string strategyId_;
    FactorOverlayConfig factorOverlayCfg_;
    RuleGateConfig ruleGateCfg_;
    RiskConfig riskCfg_{RiskConfig::defaults()};
    RebalanceConfig rebalanceCfg_;
    MarketTimingGate timingGate_;
    TimedCircuitBreaker circuitBreaker_;
};

} // namespace domain::strategy

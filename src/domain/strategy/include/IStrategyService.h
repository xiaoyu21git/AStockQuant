#pragma once

#include "StrategyServiceTypes.h"
#include "StrategySnapshotTypes.h"
#include "IFactorSvc.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>

namespace foundation {
namespace thread {
class IExecutor;
}
}

namespace astock { namespace database { class ISqlDatabase; } }

namespace domain::backtest {
struct BacktestRequest;
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
    virtual void setContextEvaluationRow(int row) = 0;
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
    StrategyService(IRuntimeFactorService& factorService,
                    IRuleEvaluationService& ruleEvaluationService,
                    IRuntimeOrderSink& orderSink);

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
    [[nodiscard]] StrategyServiceFlowResult flushPendingOrders();
    void reserveWorkingBuffers();
    void resetStats();

    /// @brief 为所有已注册策略设置当前回测评估行号 (非因子策略需要知道"当前是第几行")
    void setContextEvaluationRow(int row) override;

    /// @brief 为所有已注册策略注入历史数据视图 (非因子策略需要)
    void setContextHistoricalView(const void* view);

private:
    IRuntimeFactorService& factorService_;
    IRuleEvaluationService& ruleEvaluationService_;
    IRuntimeOrderSink* orderSink_{nullptr};
    IDiagnosticsSink* diagnosticsSink_{nullptr};
    const IOrderBuilder* orderBuilder_{nullptr};
    StrategyServiceState state_{StrategyServiceState::Stopped};
    StrategyServiceExecutionPlan plan_;
    StrategyExecutionStats stats_;
    std::vector<StrategyRuntimeEntry> strategyEntries_;
    // 日内去重：每标的首笔 tick 触发评估，后续仅更新因子快照
    std::unordered_map<InstrumentId, int, InstrumentId::Hash> m_evaluatedDays_;
    std::vector<RuntimeFactorSnapshot> factorSnapshotBuffer_;
    std::vector<StrategySignal> signalBuffer_;
    std::vector<RuleEvaluationResult> ruleResultBuffer_;
    std::vector<OrderRequest> pendingOrderBuffer_;
    mutable std::mutex mutex_;
};

class DefaultOrderBuilder final : public IOrderBuilder {
public:
    [[nodiscard]] StrategyServiceFlowResult buildOrder(
        const StrategySignal& signal,
        const RuntimeStrategyContext& context,
        OrderRequest& outputOrder) const override;
};

/// @brief 实盘异步模式下，订单通过此回调通知上层，而非同步返回值。
class IOrderListener {
public:
    virtual ~IOrderListener() = default;

    /// @brief 当引擎后台线程处理完一批行情后调用。
    /// @param orders 本次步进产生的订单列表（可能为空）。
    virtual void onOrders(const std::vector<OrderRequest>& orders) = 0;
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
                                                   void* dataSvc,
                                                   const std::function<void(double)>& onProgress = {});

    // ─── 实盘异步专有接口 ───

    /// @brief 向后台线程推送一条实时行情数据，唤醒后台自循环线程。
    void enqueueMarketData(const MarketDataPoint& marketDataPoint);

    /// @brief 启动专属后台线程（ThreadPoolExecutor(1,1)），进入 drainQueue 事件循环。
    void startLiveLoop();

    /// @brief 安全停止后台线程并等待完成。
    void stopLiveLoop();

    /// @brief 查询实盘循环是否正在运行
    [[nodiscard]] bool isLiveLoopRunning() const noexcept;

    /// @brief 设置订单回调监听器，所有订单通过此回调通知。
    void setOrderListener(IOrderListener* listener);

    /// @brief 为所有已注册策略注入历史数据视图 (非因子策略需要)
    void setContextHistoricalView(const void* view);

    /// @brief 设置实盘行情视图，供因子计算时提供 HistoricalView
    /// @param view 包含足够回溯窗口的行情数据视图 (不为 Engine 所有，调用方保证生命周期)
    void setLiveMarketView(const void* view);

    /// @brief 查询是否为因子策略（MultiFactor / MachineLearning）
    [[nodiscard]] bool hasFactorStrategies() const noexcept { return m_hasFactorStrategies_; }

    /// @brief 丢弃的 tick 计数（队列满时触发）
    [[nodiscard]] std::int64_t droppedTicks() const noexcept {
        return m_droppedTicks.load(std::memory_order_acquire);
    }

    /// @brief 距上次处理 tick 的毫秒数（>5000 可能卡死）
    [[nodiscard]] std::int64_t lastProcessedMsAgo() const noexcept {
        auto last = m_lastProcessedAt.load(std::memory_order_acquire);
        if (last == 0) return -1;
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return (now - last) / 1'000'000;
    }

private:
    [[nodiscard]] std::optional<std::vector<OrderRequest>> collectOrders(
        const StrategyServiceFlowResult& flowResult);

    /// @brief 后台线程主函数：阻塞等待行情 → step() → 通知订单。
    void drainQueue();

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
    std::atomic<std::int64_t> m_droppedTicks{0};
    std::atomic<std::int64_t> m_lastProcessedAt{0};
    IOrderListener* m_orderListener{nullptr};
    bool m_hasFactorStrategies_{false};
};

class StrategyEngine::Builder final {
public:
    Builder();

    Builder& withFactorService(std::unique_ptr<IRuntimeFactorService> factorService);
    Builder& withRuleEvaluationService(std::unique_ptr<IRuleEvaluationService> ruleEvaluationService);
    Builder& withOrderSink(IRuntimeOrderSink& orderSink);
    Builder& withDiagnosticsSink(IDiagnosticsSink& diagnosticsSink);
    Builder& withOrderBuilder(const IOrderBuilder& orderBuilder);
    Builder& withAsyncExecutor(std::shared_ptr<foundation::thread::IExecutor> executor);
    Builder& maxStrategies(StrategyCount value);
    Builder& maxSignalsPerBatch(StrategyCount value);
    Builder& maxRuleResultsPerBatch(StrategyCount value);
    Builder& maxMarketDataPerBatch(StrategyCount value);

    [[nodiscard]] std::unique_ptr<StrategyEngine> build();

private:
    std::unique_ptr<IRuntimeFactorService> factorService_;
    std::unique_ptr<IRuleEvaluationService> ruleEvaluationService_;
    IRuntimeOrderSink* orderSink_{nullptr};
    IDiagnosticsSink* diagnosticsSink_{nullptr};
    const IOrderBuilder* orderBuilder_{nullptr};
    std::shared_ptr<foundation::thread::IExecutor> asyncExecutor_;
    StrategyServiceExecutionPlan plan_{defaultExecutionPlan()};
};

} // namespace domain::strategy

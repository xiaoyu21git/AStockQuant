#pragma once

#include "IOrderListener.h"
#include "StrategyServiceTypes.h"
#include "StrategySnapshotTypes.h"
#include "IFactorSvc.h"
#include "TradeJournal.h"
#include "EvalTypes.h"
#include "SignalEvaluationPipeline.h"
#include "EvaluationScheduler.h"
#include "FactorSignalProcessor.h"
#include "SignalBlendCompositor.h"
#include "RuleGate.h"
#include "RuleAttribution.h"
#include "../../attribution/include/StrategyAttributionTypes.h"
#include "../../attribution/include/PositionSnapshotCollector.h"
#include "IBasketInterceptor.h"
#include "RulePipeline.h"
#include "RiskEvaluator.h"
#include "OrderGenerator.h"
#include "PositionSizer.h"
#include "MarketTimingGate.h"
#include "TimedCircuitBreaker.h"
#include "../../trading/include/OrderBuilder.h"

#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <chrono>
#include <unordered_map>
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
namespace astock::infrastructure::database { class AppStateStore; class ITradingCalendar; class MarketDataRepository; }

namespace domain::backtest {
struct BacktestRequest;
class BacktestFillSimulator;
}

namespace factor {
namespace compute {
class IMarketDataView;
class BacktestDataService;
}
}

namespace domain::strategy {

class EngineListenerAssembler;
class PositionBook;
class SubmissionFinalizer;
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
    /// @brief 注入实盘行情视图 (P3: 发布即持有 — shared_ptr 发布句柄, 调用方不负责生命周期；
    ///     调用方如需长期持有可保存返回/同一 shared_ptr; NoOpFactorService: no-op)
    virtual void setLiveMarketView(
        std::shared_ptr<const factor::compute::IMarketDataView> view) = 0;
    /// @brief 设置活跃评估周期 (ADR-004 缓存分区维度; 当前全策略 Daily, 分钟策略由引擎注入)
    virtual void setActivePeriod(BarPeriod period) = 0;

    // ── 行情视图构建与访问 ──
    /// @brief 从 SQL 查询结果构建实盘 MarketView（NoOpFactorService: no-op）
    virtual void buildLiveView(
        const std::vector<astock::database::SqlQueryResultRow>& rows,
        const std::vector<std::string>& extraFields) = 0;
    /// @brief 获取当前行情视图；无数据返回 nullptr。
    /// P3: shared_ptr 发布句柄 — 调用方持有的拷贝在服务重建视图后依然有效 (禁止 .get() 长期持有)
    [[nodiscard]] virtual std::shared_ptr<const factor::compute::IMarketDataView> liveView() const = 0;

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

    // ── 前置自检探测 (preflight P2, §9) ──
    /// @brief 探测评估日因子全截面是否存在有限值
    /// 逐因子实例在评估日计算 (与 copySnapshots 实盘路径同口径);
    /// 任一实例存在 >=1 个有限值 → true。纯探测: 只读缓存, 不写入不更新状态 (无"顺带"副作用)
    /// 非因子策略 (无因子实例) → true (无因子依赖); 无视图/引擎 → false
    [[nodiscard]] virtual bool probeFactorCrossSection(
        const std::vector<std::string>& symbols, std::int32_t tradingDayInt) const = 0;

    // ── 缓存预热 (终审 4.1 + C11, P3 接线) ──
    /// @brief 纯预热: 锚点日全因子全截面计算并写入 period 对应缓存分区 (copySnapshots 后续直接命中)
    /// 由调度器 start() 经 setWarmUpFn 显式调用 (不做引擎全局预热);
    /// 无视图/无因子/无标的 → 内部早退 (NoOpFactorService: no-op)
    virtual void warmUpCache(const std::string& strategyId, BarPeriod period,
                             const std::vector<std::string>& symbols) = 0;
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
    /// @brief 重置低延迟信号去重门 (每轮评估入口调用一次; 轮内去重、跨轮不残留)
    virtual void resetLastSignalKeys() = 0;
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
    void resetLastSignalKeys() override;
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

/// @brief 调仓频率配置 — 纯值类型 (P4: 死代码清理, period 一等配置 ADR-002)
struct RebalanceConfig {
    BarPeriod period{BarPeriod::Daily};  // 评估周期 (当前全策略日频)
    int interval{1};                     // 调仓间隔(交易日), 0=从不调仓

    [[nodiscard]] bool isValid() const noexcept { return interval >= 0; }
};

class StrategyEngine final : public std::enable_shared_from_this<StrategyEngine> {
public:
    class Builder;

    static Builder builder();

    /// @brief 从数据库通过 strategyId 加载参数并构建引擎（接管 factorSvc 所有权）
    /// @param factorSvc 因子服务 (unique_ptr, 传 nullptr 则只支持非因子策略)
    [[nodiscard]] static std::unique_ptr<StrategyEngine> fromDb(const std::string& strategyId,
                                                                 std::unique_ptr<IRuntimeFactorService> factorSvc = nullptr);

    /// @brief 从数据库构建回测用途引擎 (回测/实盘实例分离, 用途不可混用)
    /// 与 fromDb 的唯一差异: EnginePurpose::Backtest + executionMode=Backtest + TradeJournal 目录隔离 (logs/<策略名>_backtest/)
    /// 禁止启动实盘循环 (startLiveLoop 守卫拒绝); 仅由回测桥/调优桥调用, 绝不注册进实盘注册表
    /// @param factorSvc 因子服务 (unique_ptr, 传 nullptr 则只支持非因子策略)
    /// @return 构建失败返回 nullptr (不产生任何注册副作用)
    [[nodiscard]] static std::unique_ptr<StrategyEngine> fromDbForBacktest(
        const std::string& strategyId,
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
                                                   const std::function<void(double)>& onProgress = {},
                                                   const std::atomic<bool>* cancelFlag = nullptr);

    // ─── 实盘异步专有接口 ───

    /// @brief 启动专属后台线程。
    /// 装配评估核心 + CronEvaluationScheduler (EOD 回调 + 补单)
    void startLiveLoop();

    /// @brief 安全停止后台线程并等待完成。
    void stopLiveLoop();

    /// @brief 查询实盘循环是否正在运行
    [[nodiscard]] bool isLiveLoopRunning() const noexcept;

    /// @brief 日终评估: 跑一次完整策略评估并生成订单
    /// @param tradingDay 评估目标交易日
    /// @param isCompensation true=补单(历史收盘价), false=实时(当日 tick 价)
    /// 由 CronEvaluationScheduler 触发，在专用线程中执行
    /// @return 评估结果状态，用于决定是否持久化 lastEvalDay
    EvalStatus evaluateEndOfDay(const std::string& tradingDay, bool isCompensation);

    /// @brief 一键清仓：对所有持仓生成市价卖单，篮子提交
    /// @return 生成的订单数量，-1 表示失败
    int liquidateAll();

    /// @brief 设置订单回调监听器，所有订单通过此回调通知。
    void setOrderListener(IOrderListener* listener);

    /// @brief 设置篮子拦截器 (v0.16.0: 半自动模式)
    /// SemiAuto 模式下，订单生成后先通知拦截器展示确认窗口，用户确认后才执行
    void setBasketInterceptor(IBasketInterceptor* interceptor) noexcept;

    /// @brief 用户确认篮子 → 引擎自行应用编辑到 m_pendingBasket.orders 并提交
    void confirmBasket(std::uint64_t basketId, const std::vector<BasketEdit>& edits);

    /// @brief 用户拒绝篮子 → 丢弃全部订单，记录日志
    void rejectBasket(std::uint64_t basketId);

    /// @brief 根据执行模式分发订单
    /// Live/Backtest → IOrderListener;  SemiAuto → IBasketInterceptor
    void dispatchOrders(const std::vector<domain::trading::OrderRequest>& orders);

    /// @brief 设置引擎执行模式 (Live/Backtest/SemiAuto)
    void setExecutionMode(EngineExecutionMode mode) noexcept {
        m_executionMode = mode;
    }

    /// @brief 获取当前引擎执行模式
    [[nodiscard]] EngineExecutionMode executionMode() const noexcept {
        return m_executionMode;
    }

    /// @brief 为所有已注册策略注入历史数据视图 (非因子策略需要)
    void setContextHistoricalView(const void* view);

    /// @brief 设置实盘行情视图，供因子计算时提供 HistoricalView
    /// @param view 包含足够回溯窗口的行情数据视图 (P3: 引擎持有 m_injectedLiveView, 外部注入的视图不再悬垂)
    void setLiveMarketView(std::shared_ptr<const factor::compute::IMarketDataView> view);

    /// @brief 设置交易账户（风控需要）
    void setAccountId(std::string id) { m_accountId = std::move(id); }
    /// @brief 设置策略ID（止损单必填）
    void setStrategyId(std::string id) { m_strategyId = std::move(id); }
    /// @brief 设置策略名称（用于交易日志目录名）
    void setStrategyName(std::string name) { m_strategyName = std::move(name); }
    /// @brief 获取交易日志（回测/实盘路径记录交易事件）
    TradeJournal* tradeJournal() { return m_tradeJournal.get(); }
    /// @brief 实盘成交确认 — 由桥接层在收到 GM 成交回报时调用
    void logExecutionFill(const std::string& symbol, const std::string& side,
                          double price, std::int64_t quantity, double commission,
                          const std::string& fillTime, const std::string& brokerOrderId,
                          const std::string& traceId = "");

    /// @brief 设置实盘数据目录（lastEvalDay JSON 持久化路径前缀）
    void setLiveDataPath(std::string path) { m_liveDataPath = std::move(path); }

    /// @brief 启用/禁用 ST 禁新买过滤 (默认关闭; 启用后惰性加载 ST 名单注入日终评估管道)
    /// 过滤口径 (与B股一致): 未持仓ST整标的跳过; 已持仓ST仅拦截加仓买单, 卖单放行
    void setStBuyFilterEnabled(bool enabled) noexcept {
        m_stBuyFilterEnabled.store(enabled, std::memory_order_release);
    }

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
    /// P3: shared_ptr 发布句柄 (禁止 .get() 长期持有)
    [[nodiscard]] std::shared_ptr<const factor::compute::IMarketDataView> liveMarketView() const noexcept {
        if (factorService_) {
            auto v = factorService_->liveView();
            if (v) return v;
        }
        return m_liveMarketView;
    }

    /// @brief 获取规则闸门统计数据（评估/命中/拦截/数据缺失，按模板聚合）
    [[nodiscard]] const rules::RuleGateStats& ruleGateStats() const noexcept {
        return m_ruleGate.stats();
    }

    /// @brief 获取最近一次回测的规则归因 (P&L 影响)
    [[nodiscard]] const std::map<std::string, rules::RuleAttribution>& ruleAttribution() const noexcept {
        return m_ruleAttribution;
    }

    /// @brief 获取最近一次回测的策略归因报告 (行业盈亏/个股盈亏/Brinson 择时选股分解)
    [[nodiscard]] const std::optional<domain::attribution::StrategyAttributionReport>&
    lastAttribution() const noexcept {
        return m_lastAttribution;
    }

    /// @brief 最近一次回测的日期区间
    [[nodiscard]] std::string backtestDateRange() const noexcept { return m_backtestDateRange; }

    /// @brief 引擎固定用途 (构造时确定, 不可变) — 回测/实盘显式区分
    [[nodiscard]] EnginePurpose purpose() const noexcept { return m_purpose; }

    /// @brief 生成回测统计不可变快照 (值拷贝三成员)
    /// 仅在 backtest() 返回后调用 (返回时统计成员已最终更新, 回测引擎无专用线程, 无并发写)
    [[nodiscard]] BacktestStatsSnapshot snapshotStats() const;

private:
    /// @brief fromDb/fromDbForBacktest 共用构建逻辑 (用途仅影响 TradeJournal 目录与 m_purpose)
    /// 封装为独立函数: 工厂语义统一、单点修改不影响两个入口
    [[nodiscard]] static std::unique_ptr<StrategyEngine> fromDbImpl(
        const std::string& strategyId,
        std::unique_ptr<IRuntimeFactorService> factorSvc,
        EnginePurpose purpose);

    [[nodiscard]] std::optional<std::vector<OrderRequest>> collectOrders(
        const StrategyServiceFlowResult& flowResult);

    // ── P2: 评估核心装配 (ADR-009④: PipelineDeps 仅 buildPipelineDeps 一处构造) ──

    /// @brief 装配评估核心 (PositionBook/簿记挂钩/Finalizer/管道 + AppStateStore)
    /// 惰性装配: setLiveDataPath 之后首次调用 (startLiveLoop/buildPipelineDeps/liquidateAll)
    /// 装配后补装簿记挂钩 (StrategyManager 的 setOrderListener 可能早于装配)
    void ensureEvaluationCore();

    /// @brief 组装管道依赖注入集 (唯一构造点, ADR-009④; C8/P4: 日历经 DbTradingCalendar 注入, 回测 gmsdk 不动)
    PipelineDeps buildPipelineDeps();

    /// @brief 引擎账户 → 管道账户投影 (转换集中处理, CLAUDE.md 1.2.2)
    AccountState buildAccountState() const;

    // ── backtest 子函数 (Phase 30b 拆分) ──

    /// @brief 回测循环全部可变状态（原 backtest() 中 ~35 个局部变量）
    /// 各子函数通过引用操作, 保持与原内联代码完全一致的语义
    struct BacktestDayContext {
        // 账户
        double cash = 0.0;
        double latestEquity = 0.0;
        double peakEquity = 0.0;
        std::unordered_map<std::string, domain::trading::Position> backtestPositions;

        // 成交统计 (累加型)
        int totalFills = 0, winningFills = 0, losingFills = 0;
        int stopLossFilled = 0, ruleExitFilled = 0, normalSellFilled = 0;
        int riskRejectedCount = 0, stopLossSkippedNoHeld = 0, totalStopLossOrders = 0;
        int stopLossExitCount = 0, ruleExitCount = 0;  // 诊断计数器
        int bShareBlockedOrders = 0;                   // B股禁买拦截笔数 (全局硬过滤)
        double totalProfit = 0.0, totalLoss = 0.0;
        double largestWin = 0.0, largestLoss = 0.0;

        // 每日临时状态 (每日 clearDaily())
        std::unordered_set<std::string> todayStopLossSyms;
        std::unordered_set<std::string> todayRuleExitSyms;
        std::unordered_set<std::string> boughtToday;

        // 买入记录 (累加型, 卖出时 erase)
        std::unordered_map<std::string, double> buyPriceMap;
        std::unordered_map<std::string, double> buySignalScoreMap;
        std::unordered_map<std::string, double> buyDateMap;       // symbol → entryRow
        std::unordered_map<std::string, double> buyFactorScoreMap2;
        std::unordered_map<std::string, double> symbolPnl;        // 逐标的累计盈亏

        // 时间序列 (追加型)
        std::vector<double> equityCurve;

        // 诊断数据 (累加型)
        std::vector<double> holdingDaysVec;
        std::vector<double> tradePnlVec;
        std::vector<double> entryFactorScores;
        int dailyPositionSum = 0;
        int daysWithTrades = 0;
        double deployedCapitalSum = 0.0;
        size_t totalBuySignals = 0;
        size_t totalPoolCandidates = 0;
        int poolSelectionDays = 0;
        std::unordered_map<std::string, int> hybridFactorCoveredDays;

        /// @brief 每日清空临时状态 (日终调用)
        void clearDaily() {
            todayStopLossSyms.clear();
            todayRuleExitSyms.clear();
            boughtToday.clear();
        }

        /// @brief 构建账户快照 (风控/日志用)
        [[nodiscard]] domain::trading::AccountSnapshot accountSnapshot() const {
            domain::trading::AccountSnapshot a;
            a.setTotalAsset(latestEquity);
            a.setAvailableCash(cash);
            a.setMarketValue(latestEquity - cash);
            return a;
        }
    };

    /// @brief 回测主循环: for(r=warmupDayCount; r<totalDays; ++r) 逐日驱动
    /// 将原 backtest() 中 ~755 行的循环体提取为独立函数,
    /// 通过 BacktestDayContext 封装 ~35 个可变状态变量
    /// @param view 扩展视图 = [回看交易日]+[数据集交易日] (WarmupViewBuilder 构建, 回看行由 PG 补全)
    /// @param warmupDayCount 回看行数 = 循环起点 (之前行仅供指标/规则窗口读取)
    /// @param attributionCollector 归因收集器, 由 backtest() 持有, 循环中写入, 后处理中读取
    void runBacktestLoop(
        BacktestDayContext& ctx,
        StrategyBacktestResult& result,
        const factor::compute::IMarketDataView* view,
        int warmupDayCount,
        const domain::backtest::BacktestRequest& req,
        domain::backtest::BacktestFillSimulator& fillSim,
        const std::unordered_map<std::string, int>& symbolToCol,
        int bmColIdx,
        const std::function<void(double)>& onProgress,
        rules::AttributionCollector& attributionCollector,
        domain::attribution::PositionSnapshotCollector& positionCollector,
        const std::atomic<bool>* cancelFlag = nullptr);

    /// @brief 回测后处理: 指标计算 (Phase 30c 拆分)
    /// 从 backtest() L1989-2099 提取, 纯计算, 无日志输出
    void computeBacktestMetrics(
        StrategyBacktestResult& result,
        const BacktestDayContext& ctx,
        const domain::backtest::BacktestRequest& req,
        const factor::compute::IMarketDataView* view,
        int totalDays);

    /// @brief 回测后处理: 诊断输出 + 归因 + 持久化 (Phase 30c 拆分)
    /// 从 backtest() L2101-2322 提取, 含日志 + 凯利 + 归因 + RankIC + 持久化
    /// 注意: ctx 非 const — 诊断中原地排序 holdingDaysVec / entryFactorScores
    void buildBacktestDiagnostics(
        StrategyBacktestResult& result,
        BacktestDayContext& ctx,
        const domain::backtest::BacktestRequest& req,
        const factor::compute::IMarketDataView* view,
        int totalDays,
        const std::function<void(double)>& onProgress,
        rules::AttributionCollector& attributionCollector);

    /// @brief 构建基准逐日成分权重 (月频采样 × 流通市值, 缺月回退上月)
    /// 返回 date → (symbol → 归一化权重); key 集合即基准覆盖日集合
    /// 同月各日共享同一份权重表 (shared_ptr 零拷贝)
    static std::map<domain::DomainDate,
                    std::shared_ptr<const std::unordered_map<std::string, double>>>
    buildBenchmarkStockWeights(astock::infrastructure::database::MarketDataRepository& repo,
                               const std::string& benchmarkIndex,
                               const std::vector<domain::DomainDate>& backtestDays);

    /// @brief 策略归因后处理: 行业/个股聚合 + Brinson 分解 (回测尾调用)
    /// 数据源: tradeLog + 持仓快照 + PG 行业映射 + 基准成分权重
    void buildStrategyAttribution(
        StrategyBacktestResult& result,
        const domain::backtest::BacktestRequest& req,
        const factor::compute::IMarketDataView* view,
        const domain::attribution::PositionSnapshotCollector& positionCollector);

    /// @brief 日终持仓快照入库 (live.daily_equity_snapshots + live.daily_position)
    /// 账户快照是券商事实, 与评估状态解耦; UPSERT 幂等 (主评估/补跑窗口重写同日)
    void persistDailyPositionSnapshot(const std::string& tradingDay);

    /// @brief ST 名单惰性加载一次 (call_once; ref.symbol_info status/name 双源判定,
    /// 与清洗链路 STFilterRule 口径一致)。DB 失败 → 名单为空 (不拦, 打印 ERROR)
    void loadStSymbolsOnce();

    /// @brief 当前持仓实时同步 (live.current_position, 券商快照推送驱动, 内部 ≥20s 节流)
    /// 策略归属: 账本持有 → 本策略 id; 券商有账本无 → NULL (手动持仓)
    void persistCurrentPositions();

private:
    std::unique_ptr<IRuntimeFactorService> factorService_;
    std::unique_ptr<IRuleEvaluationService> ruleEvaluationService_;
    std::unique_ptr<IStrategyService> strategyService_;
    std::shared_ptr<foundation::thread::IExecutor> asyncExecutor_;

    // 实盘异步线程 —— 每个引擎独立的专属线程池（1线程）
    std::shared_ptr<foundation::thread::IExecutor> m_dedicatedExecutor;
    std::atomic<bool> m_loopRunning{false};
    std::atomic<bool> m_evalCancelled{false};  ///< 停止协作取消标志: stopLiveLoop 置位, 管道逐标的检查点中断评估
    std::uint64_t m_accountCbToken{0};  ///< AccountEngine 券商推送回调注册 token (stopLiveLoop 注销)
    std::atomic<bool> m_isBacktestMode{false};  ///< 回测运行时置位: evaluateEndOfDay 早退 + BacktestGuard 防御监听器误触发
    std::unique_ptr<CronEvaluationScheduler> m_dailyScheduler;  ///< 日频评估调度器 (EOD + 补单)
    std::atomic<std::int64_t> m_lastProcessedAt{0};
    IOrderListener* m_orderListener{nullptr};

    // ── 半自动模式 (v0.16.0) ──
    IBasketInterceptor* m_basketInterceptor{nullptr};

    struct PendingBasket {
        std::uint64_t basketId{0};
        std::vector<domain::trading::OrderRequest> orders;
        bool pending{false};
    };
    PendingBasket m_pendingBasket;
    std::mutex m_basketMutex;  ///< 保护 m_pendingBasket (引擎线程 ↔ confirmBasket/rejectBasket)

    EngineExecutionMode m_executionMode{EngineExecutionMode::Live};
    EnginePurpose m_purpose{EnginePurpose::Live};  ///< 引擎固定用途 (构造时确定, 不可变; fromDbForBacktest 置 Backtest)
    std::string m_accountId;
    std::string m_strategyId;
    std::string m_strategyName;
    std::atomic<std::int64_t> m_lastPositionSyncSec{0};  ///< 上次 live.current_position 同步 epoch 秒 (节流)
    std::unique_ptr<TradeJournal> m_tradeJournal;  // 交易日志 (按策略名/日期分文件)
    std::string m_liveDataPath;     // 实盘数据目录, 用于统一 JSON 持久化
    domain::trading::OrderBuilder m_orderBuilder;
    PositionSizer m_positionSizer;                              ///< 仓位计算器(默认0, Builder/fromDb 注入 baseQty)
    OrderGenerator m_orderGenerator{m_orderBuilder, m_positionSizer};  ///< 持仓感知建单器
    std::uint32_t m_maxOrderQuantity{10000};  ///< 权重建仓基数（targetWeight × base = 目标股数），由策略配置注入
    std::shared_ptr<const factor::compute::IMarketDataView> m_liveMarketView;  // 非因子策略视图 (prepareMarketData 构建; P3: shared 统一发布)
    std::shared_ptr<const factor::compute::IMarketDataView> m_injectedLiveView;  // P3: 外部注入视图持有 (悬垂修复, Bridge/内部注入共用)
    bool m_hasFactorStrategies{false};  ///< 是否有因子策略注册，fromDb 创建时确定
    bool m_needsMarketCapField{false};  ///< 权重方案为市值加权时置位，prepareMarketData 追加 market_cap 字段
    std::atomic<bool> m_stBuyFilterEnabled{false};  ///< ST禁新买开关 (默认关闭, 现有行为零变化)
    std::unordered_set<std::string> m_stSymbols;    ///< ST名单 (纯代码; 开关启用时 call_once 加载, 评估线程只读)
    std::once_flag m_stSymbolsOnce;                 ///< ST名单一次性加载保护
    FactorSignalProcessor m_factorSignalProcessor;  ///< 因子信号处理(过滤+缩放)
    std::unique_ptr<ICandidatePoolSelector> m_poolSelector;  ///< 因子候选池选择器(因子定池,策略选点)
    rules::RuleGate m_ruleGate;  ///< 规则管线: 市场闸/信号审核/出场, 绑定规则库+策略模板集
    RulePipeline m_rulePipeline{m_ruleGate};  ///< 规则编排器(封装上下文构建+迭代样板代码)
    bool m_enableCandlePatterns{false};       ///< 是否启用 TA-Lib 蜡烛形态计算
    std::map<std::string, rules::RuleAttribution> m_ruleAttribution;  ///< 最近一次回测的规则归因
    std::optional<domain::attribution::StrategyAttributionReport> m_lastAttribution;  ///< 最近一次回测的策略归因
    std::string m_backtestDateRange;  ///< 最近一次回测的日期区间 (如 "20200102-20260717")
    RiskConfig m_riskConfig = RiskConfig::defaults();
    MarketTimingGate m_timingGate;             ///< 大盘择时闸门
    TimedCircuitBreaker m_circuitBreaker;      ///< 风控熔断器
    std::unordered_set<std::string> m_liquidationBlocklist;  ///< 当天已清仓标的, 禁止当日再次买入
    int m_rebalanceInterval{1};            ///< 调仓间隔(交易日), 0=从不调仓, 1=每日
    std::string m_lastRebalanceDate;       ///< 上次执行调仓的交易日 YYYYMMDD
    int m_minHoldDays{0};                  ///< 最少持有天数, 0=不启用
    std::unordered_map<std::string, std::int64_t> m_positionEntryDates;  ///< symbol→首次建仓日期 YYYYMMDD

    // ── P2: 评估管道装配 (ADR-007/009) ──
    BarPeriod m_period{BarPeriod::Daily};  ///< 评估周期 (Builder 从 RebalanceConfig 接线)
    std::shared_ptr<astock::infrastructure::database::AppStateStore> m_appStateStore;  ///< app_state.json 统一写者 (与调度器同路径)
    std::unique_ptr<PositionBook> m_positionBook;              ///< 内部持仓账本 (ADR-005 实时对账, 不拦下单)
    std::unique_ptr<EngineListenerAssembler> m_listenerAssembler;  ///< 簿记挂钩一次性装配 (ADR-009①)
    std::unique_ptr<SubmissionFinalizer> m_finalizer;          ///< 共享提交核心 (幂等去重+生成+投递)
    std::unique_ptr<SignalEvaluationPipeline> m_pipeline;      ///< 评估主链 (频率无关, 8 阶段)
    std::unique_ptr<astock::infrastructure::database::ITradingCalendar> m_calendar;  ///< 交易日历 (P4: 实盘路径 DB 日历, 回测 gmsdk 不动)

    /// @brief 原始创建参数 (fromDb 时保存，供回测覆写，避免重复 DB 读取)
    StrategyCreationParams m_originalCreationParams;
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

    /// @brief 设置权重建仓基数（targetWeight × base = 目标股数）
    Builder& withMaxOrderQuantity(std::uint32_t value);

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
    std::uint32_t maxOrderQuantity_{10000};  ///< 权重建仓基数，由策略 JSON 的 maxOrderQuantity 字段注入
};

} // namespace domain::strategy

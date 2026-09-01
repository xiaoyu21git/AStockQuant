#pragma once
// SignalEvaluationPipeline — 评估主链 (P2, ADR-007 命名; 频率无关)
// 8 阶段: checkRebalance → preflight → prepareContext → fetchPrices → computeBreadth
//         → evaluateGates → collectSignals → finalizeAndSubmit
// 规则: 管道内零周期分支 (ADR-002)、零 gmsdk (C8: 日历经 Deps.prevTradingDayFn 注入)、
//       EvalSession 栈上按值贯穿; Deps 仅由 StrategyEngine::buildPipelineDeps 一处构造 (ADR-009④)
// preflight 检查点 (C9): P1 视图空 / P2 锚点+因子快照 / P3 账户空; P4 价格全空在 fetchPrices 阶段
// (P5 簿记对账已迁至 PositionBook::reconcileToBroker 实时路径 — 对账只修正不拦截, 下单流程零依赖)

#include "EvalTypes.h"
#include "IOrderListener.h"
#include "MarketTimingGate.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain {
struct DomainDate;
namespace trading { class OrderBuilder; }
}

namespace factor::compute { class IMarketDataView; }

namespace domain::strategy {

namespace rules { class RuleGate; }

class IRuntimeFactorService;
class OrderGenerator;
class SubmissionFinalizer;
class TimedCircuitBreaker;
class TradeJournal;

/// @brief 持仓投影 (引擎持仓的管道内值型, 零 engine 依赖; 符号为全码含交易所后缀)
struct PositionState {
    std::string symbol;
    std::int64_t quantity{0};
    double costPrice{0.0};
    double lastPrice{0.0};
};

/// @brief 账户+持仓投影 (AccountEngine 快照的管道内值型, 零 engine 依赖)
/// Deps.accountSnapshotFn 在 Facade 集中转换 (CLAUDE.md 1.2.2: 转换集中处理)
struct AccountState {
    std::string accountId;
    double totalAsset{0.0};
    double availableCash{0.0};
    double marketValue{0.0};
    double frozenCash{0.0};
    double realizedPnl{0.0};
    double unrealizedPnl{0.0};
    std::vector<PositionState> positions;
};

/// @brief 待提交订单 (原 Facade PendingOrder 迁移; tickPrice/targetWeight/signalScore 供生成器与日志)
struct PendingOrder {
    OrderRequest order;
    double tickPrice{0.0};
    double targetWeight{0.0};
    double signalScore{0.0};
};

/// @brief 闸门结果 (原 Facade EodGateResult 迁移)
struct EvalGateResult {
    bool allowNewEntries{true};
    TimingResult timing;
};

/// @brief 管道依赖注入集 (聚合, 引用引擎成员; 仅 buildPipelineDeps 构造)
/// 观察者均不持有所有权; run() 同步栈帧内有效
struct PipelineDeps {
    // ── 行为注入 (Facade 包装) ──
    std::function<std::optional<std::vector<OrderRequest>>(const MarketDataPoint&)> stepFn;  // = StrategyEngine::step
    // P3: shared_ptr 发布句柄 — run() 同步栈帧内由 EvalSession 持有 (禁 .get() 长期持有)
    std::function<std::shared_ptr<const factor::compute::IMarketDataView>()> liveViewFn;
    std::function<std::string(const std::string&)> prevTradingDayFn;        // C8: P2 接 gmsdk 包装, P4 换 DbTradingCalendar
    std::function<AccountState()> accountSnapshotFn;                        // preflight P3 券商快照
    std::function<void()> onForceLiquidate;                                 // = liquidateAll (择时强平)
    // 停止协作取消: 返回 true 立即中断评估 (逐标的循环检查点); 空=不回测影响 (回测路径不注入)
    std::function<bool()> cancelCheck;
    // ST禁新买名单 (纯代码集合, 引擎开关启用时注入); nullptr = 功能关闭 (回测/默认零行为变化)
    const std::unordered_set<std::string>* stSymbols{nullptr};

    // ── 引擎成员引用 (观察者) ──
    OrderGenerator* orderGenerator{nullptr};
    trading::OrderBuilder* orderBuilder{nullptr};
    IOrderListener* orderListener{nullptr};
    TradeJournal* tradeJournal{nullptr};
    rules::RuleGate* ruleGate{nullptr};
    MarketTimingGate* timingGate{nullptr};
    TimedCircuitBreaker* circuitBreaker{nullptr};
    IRuntimeFactorService* factorService{nullptr};
    SubmissionFinalizer* finalizer{nullptr};
    std::unordered_map<std::string, std::int64_t>* positionEntryDates{nullptr};
    std::atomic<std::int64_t>* lastProcessedAt{nullptr};
    int* rebalanceInterval{nullptr};
    std::string* lastRebalanceDate{nullptr};

    // ── 配置值 ──
    int minHoldDays{0};
    std::uint32_t maxOrderQuantity{10000};
    std::string strategyId;
    std::string accountId;
};

/// @brief 一次评估的全部可变状态 (栈上按值贯穿 8 阶段)
struct EvalSession {
    // ── 上下文 (prepareContext 填充) ──
    // P3: shared_ptr 持有 (run() 同步栈帧期间有效; 禁 .get() 长期持有)
    std::shared_ptr<const factor::compute::IMarketDataView> view;
    const std::vector<std::string>* symbols{nullptr};
    const std::vector<domain::DomainDate>* dates{nullptr};
    int numCols{0};
    int rowStride{0};
    std::unordered_map<std::string, int> symToCol;  // 纯代码 → 列号
    std::int32_t tradingDayInt{0};
    std::string endDateStr;  // YYYY-MM-DD (Provider 查询用)

    // ── 账户/持仓 (preflight P3 快照, 全链复用同一份) ──
    AccountState account;
    std::unordered_map<std::string, std::int64_t> posQtyMap;  // 纯代码 → 股数

    // ── 价格/宽度 ──
    PriceData prices;

    // ── 闸门 ──
    EvalGateResult gates;

    // ── 信号/订单 ──
    std::vector<PendingOrder> pendingOrders;
    std::vector<OrderRequest> rawOrders;    // 审核后的原始订单 (Finalizer 入参)
    std::vector<OrderRequest> finalOrders;  // 实际投递订单副本 (建仓日期记录)

    // ── 三计数 (§8: N=原始生成, N1=规则闸门拒绝, N2=涨跌停过滤, N3=生成器过滤) ──
    std::int64_t totalGenerated{0};
    std::int64_t ruleGateRejected{0};
    std::int64_t limitFiltered{0};
    std::int64_t generatorFiltered{0};
    std::int64_t positionExits{0};
    std::int64_t bShareSymbolsSkipped{0};  // B股标的级跳过 (未持仓整标的, 不计入生成数)
    std::int64_t bShareFiltered{0};        // B股买单拦截 (已持仓加仓, 计入生成数并从审核分母扣除)
    std::int64_t stSymbolsSkipped{0};      // ST标的级跳过 (未持仓整标的, 不计入生成数)
    std::int64_t stFiltered{0};            // ST买单拦截 (已持仓加仓, 计入生成数并从审核分母扣除)
};

/// @brief 评估主链 (频率无关; 不依赖任何具体数据源/日历/引擎单例)
class SignalEvaluationPipeline {
public:
    /// @brief 执行一次评估
    /// @param req 评估请求 (priceProvider 仅本次同步调用内有效, 澄清 C1)
    /// @param deps 依赖注入集 (run() 期间不得析构)
    EvalResult run(const EvalRequest& req, PipelineDeps& deps);

private:
    // ── 8 阶段 (返回非空 EvalResult 表示链断/跳过, 立即终止) ──

    /// @brief 阶段1: 调仓日判定 (C8: 经 Deps.prevTradingDayFn, 管道零 gmsdk)
    std::optional<EvalResult> checkRebalance(const EvalRequest& req, PipelineDeps& deps);

    /// @brief 阶段2: 前置自检 P1/P2/P3/P5 (C9; 首败即截断, 簿记 adopt 双分支在 P5 前)
    std::optional<EvalResult> preflight(const EvalRequest& req, PipelineDeps& deps, EvalSession& s);

    /// @brief 阶段3: 评估上下文 (符号/日期/列映射/持仓图/风控解禁)
    void prepareContext(const EvalRequest& req, EvalSession& s);

    /// @brief 阶段4: 当日价格 (P4 全空 → Error(PriceDataEmpty), C9 落点; Provider 异常 → Exception)
    std::optional<EvalResult> fetchPrices(const EvalRequest& req, PipelineDeps& deps, EvalSession& s);

    /// @brief 阶段5: 市场宽度 (MA60/MA20 上方占比)
    void computeBreadth(EvalSession& s);

    /// @brief 阶段6: 闸门评估 (宽度冻结 + 规则闸门 + 择时)
    void evaluateGates(PipelineDeps& deps, EvalSession& s);

    /// @brief 阶段7: 逐标的信号收集 (stepFn; 涨跌停过滤计数 N2; 单股异常 WARN+continue)
    void collectSignals(const EvalRequest& req, PipelineDeps& deps, EvalSession& s);

    /// @brief 阶段8: 审核 + 提交 (规则信号审核 N1 + 持仓出场 + Finalizer + 三计数判定)
    EvalResult finalizeAndSubmit(const EvalRequest& req, PipelineDeps& deps, EvalSession& s);

    // ── 私有辅助 (原 Facade 静态函数迁移, 符合 OOP 无游离函数) ──

    /// @brief 涨跌停检测 (A股主板 ±10%, 浮点宽容差)
    static bool isAtLimitUp(double close, double preClose) noexcept;
    static bool isAtLimitDown(double close, double preClose) noexcept;

    /// @brief 两交易日间隔的交易日数 (C8: 经 Deps.prevTradingDayFn, 零 gmsdk)
    static int countTradingDaysBetween(std::int64_t fromDate, std::int64_t toDate,
                                       const std::function<std::string(const std::string&)>& prevTradingDayFn);

    /// @brief 链断失败构造 + 引擎日志/journal (ERROR 模板 §8)
    EvalResult fail(PipelineDeps& deps, const EvalRequest& req,
                    EvalStage stage, EvalFailureKind kind, const std::string& reason);

    /// @brief 停止取消结果构造 (INFO 日志; 调度器不持久化, 重启后重评)
    static EvalResult cancel(const EvalRequest& req);
};

} // namespace domain::strategy

#include "TradeExecutionEngine.h"
#include "../../engine/include/TradeEngine.h"
#include "../../engine/include/Event/EventBus.hpp"
#include "../../engine/include/Event/EventFormat.hpp"
#include "../../engine/include/GlobalEventBusRegistry.h"
#include "foundation/log/logging.hpp"
#include "foundation/Utils/Uuid.h"

#include <cmath>
#include <mutex>

namespace domain::trading {

// ============================================================================
// PIMPL — stores gateway, state, and scheduling logic
// ============================================================================
class TradeExecutionEngine::Impl final {
public:
    std::unique_ptr<IBrokerGateway> m_gateway;

    // Recent orders (max 50)
    static constexpr int kMaxRecentOrders = 50;
    std::vector<TradeOrder> m_recentOrders;

    // Execution pause state: key = "scope:{executionScopeId}"
    struct PauseState {
        std::string executionScopeId;
        std::string pausedBatchId;
        int pausedBatchIndex{0};
        std::string blockingOrderId;
        std::string blockingStatus;
    };
    std::unordered_map<std::string, PauseState> m_pausedExecutionScopes;

    // Manual checkpoint approvals: key = "scope:{id}|checkpoint:{batchId}"
    std::unordered_set<std::string> m_approvedExecutionCheckpoints;

    mutable std::mutex m_mutex;

    // Callbacks
    TradeExecutionEngine::OrderAcceptedCallback m_orderAcceptedCallback;
    TradeExecutionEngine::OrderUpdateCallback m_orderUpdateCallback;
    TradeExecutionEngine::TradeFillCallback m_tradeFillCallback;

    // EventBus 订阅
    foundation::utils::Uuid m_orderSub;
    foundation::utils::Uuid m_fillSub;

    // ── Helper ──
    static std::string scopeKey(const std::string& executionScopeId) {
        return "scope:" + executionScopeId;
    }

    static std::string checkpointKey(const std::string& executionScopeId, const std::string& batchId) {
        return scopeKey(executionScopeId) + "|checkpoint:" + batchId;
    }

    // ── Scheduling rule checks ──
    std::optional<SchedulingConflictResult> checkExecutionPause(const TradeOrder& order) const;
    std::optional<SchedulingConflictResult> checkManualCheckpoint(const TradeOrder& order) const;
    std::optional<SchedulingConflictResult> checkPartialFillAdvance(const TradeOrder& order) const;
    std::optional<SchedulingConflictResult> checkPendingOrderConflict(const TradeOrder& order) const;

    void appendRecentOrder(const TradeOrder& order);
    bool updateExecutionPauseLocked(const TradeOrder& order);
    bool isOrderClosed(const TradeOrder& order) const noexcept;
};

// ============================================================================
// Validation
// ============================================================================
ValidationResult TradeExecutionEngine::validateOrder(const TradeOrder& order) {
    if (order.symbol().empty()) {
        return ValidationResult::reject(OrderValidationCode::InvalidSymbol, "symbol is empty");
    }
    if (order.strategyId().empty()) {
        return ValidationResult::reject(OrderValidationCode::MissingRequiredFields, "strategyId is empty");
    }

    bool priceOptional = (order.actionKind() == ActionKind::CashRepay
                       || order.actionKind() == ActionKind::ShareReturn);
    if (!priceOptional && order.price() <= 0.0) {
        return ValidationResult::reject(OrderValidationCode::InvalidPrice, "invalid price");
    }

    bool quantityOptional = (order.actionKind() == ActionKind::CashRepay);
    if (!quantityOptional && order.quantity() <= 0) {
        return ValidationResult::reject(OrderValidationCode::InvalidQuantity, "invalid quantity");
    }

    bool boardLotOk = !order.isBoardLotMode()
        || order.positionEffect() == strategy::PositionEffect::Close
        || order.actionKind() == ActionKind::CashRepay
        || (order.quantity() >= 100 && order.quantity() % 100 == 0);
    if (!boardLotOk) {
        return ValidationResult::reject(OrderValidationCode::InvalidQuantity,
                                         "quantity is not board lot");
    }

    return ValidationResult::ok();
}

// ============================================================================
// Scheduling rule checks
// ============================================================================
std::optional<SchedulingConflictResult>
TradeExecutionEngine::Impl::checkExecutionPause(const TradeOrder& order) const {
    if (!order.pauseOnAbnormalReject()) return {};
    if (order.batchIndex() <= 0) return {};

    const std::string key = scopeKey(order.executionScopeId());
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pausedExecutionScopes.find(key);
    if (it == m_pausedExecutionScopes.end()) return {};
    if (order.batchIndex() <= it->second.pausedBatchIndex) return {};

    SchedulingConflictResult r;
    r.setConflict(ScheduleConflictCode::ExecutionPausedAfterReject,
                  "execution paused after reject, batch=" + order.batchId());
    return r;
}

std::optional<SchedulingConflictResult>
TradeExecutionEngine::Impl::checkManualCheckpoint(const TradeOrder& order) const {
    if (!order.requiresManualCheckpoint()) return {};

    const std::string ck = checkpointKey(order.executionScopeId(), order.batchId());
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_approvedExecutionCheckpoints.count(ck)) return {};

    SchedulingConflictResult r;
    r.setConflict(ScheduleConflictCode::ManualCheckpointRequired,
                  "manual checkpoint required for batch=" + order.batchId());
    return r;
}

std::optional<SchedulingConflictResult>
TradeExecutionEngine::Impl::checkPartialFillAdvance(const TradeOrder& order) const {
    if (!order.requiresPreviousBatchFilled()) return {};
    if (order.previousBatchId().empty() && order.batchIndex() <= 0) return {};

    std::string requiredBatchId = order.previousBatchId();
    if (requiredBatchId.empty()) {
        requiredBatchId = "batch_" + std::to_string(order.batchIndex());
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    int observed = 0;
    bool hasUnfilled = false;
    for (const auto& o : m_recentOrders) {
        if (o.batchId() != requiredBatchId) continue;
        ++observed;
        if (isOrderClosed(o)) continue;
        hasUnfilled = true;
        break;
    }

    if (observed == 0 && order.previousBatchOrderCount() > 0) {
        SchedulingConflictResult r;
        r.setConflict(ScheduleConflictCode::PreviousBatchMissingOrders,
                      "previous batch " + requiredBatchId + " has no visible orders");
        return r;
    }
    if (hasUnfilled) {
        SchedulingConflictResult r;
        r.setConflict(ScheduleConflictCode::PreviousBatchNotFilled,
                      "previous batch " + requiredBatchId + " not fully filled");
        return r;
    }
    return {};
}

std::optional<SchedulingConflictResult>
TradeExecutionEngine::Impl::checkPendingOrderConflict(const TradeOrder& order) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& o : m_recentOrders) {
        if (o.symbol() != order.symbol()) continue;
        if (isOrderClosed(o)) continue;
        if (o.side() == order.side()) continue;

        SchedulingConflictResult r;
        r.setConflict(ScheduleConflictCode::PendingConflictingOrder,
                      "pending conflicting order for symbol=" + order.symbol());
        return r;
    }
    return {};
}

// ============================================================================
// Submit pipeline
// ============================================================================
SubmitResult TradeExecutionEngine::submitOrder(const TradeOrder& order) {
    strategy::RiskInput risk;
    risk.setStrategyId(order.strategyId());
    risk.setSymbol(order.symbol());
    risk.setBuyOrder(order.side() == strategy::OrderDirection::Buy);
    risk.setPrice(order.price());
    risk.setQuantity(order.quantity());
    risk.setSignalStrength(order.signalStrength() > 0.0 ? order.signalStrength() : 0.5);
    risk.setStrategyBound(true);
    risk.setStrategyActive(true);
    risk.setAutoStrategySignal(!order.strategyId().empty());
    risk.setPositionSnapshotReady(true);
    risk.setTradingSessionOpen(true);
    return submitOrder(order, risk);
}

SubmitResult TradeExecutionEngine::submitOrder(const TradeOrder& order,
                                                const strategy::RiskInput& riskContext) {
    // Stage 1: validation
    auto vr = validateOrder(order);
    if (!vr.valid()) {
        return SubmitResult::rejected(vr.message(), vr.code());
    }

    // Stage 2: scheduling conflict checks
    auto conflict = m_impl->checkExecutionPause(order);
    if (conflict && conflict->hasConflict()) {
        return SubmitResult::scheduleBlocked(conflict->code(), conflict->message());
    }
    conflict = m_impl->checkManualCheckpoint(order);
    if (conflict && conflict->hasConflict()) {
        return SubmitResult::scheduleBlocked(conflict->code(), conflict->message());
    }
    conflict = m_impl->checkPartialFillAdvance(order);
    if (conflict && conflict->hasConflict()) {
        return SubmitResult::scheduleBlocked(conflict->code(), conflict->message());
    }
    conflict = m_impl->checkPendingOrderConflict(order);
    if (conflict && conflict->hasConflict()) {
        return SubmitResult::scheduleBlocked(conflict->code(), conflict->message());
    }

    // Stage 3: risk evaluation
    auto riskResult = strategy::RiskEvaluator::evaluateOrder(riskContext);
    if (!riskResult.approved()) {
        return SubmitResult::riskRejected(riskResult.code(), riskResult.description());
    }

    // Stage 4: submit via TradeEngine
    if (!engine::TradeEngine::instance().initialized()) {
        return SubmitResult::rejected("TradeEngine not initialized",
                                       OrderValidationCode::MissingRequiredFields);
    }

    engine::OrderRequest engineReq;
    engineReq.symbol     = order.symbol();
    engineReq.strategyId = order.strategyId();
    engineReq.price      = order.price();
    engineReq.quantity   = order.quantity();
    engineReq.side       = order.side() == strategy::OrderDirection::Buy
                           ? engine::OrderRequest::Buy : engine::OrderRequest::Sell;
    engineReq.orderType  = engine::OrderRequest::Limit;

    auto result = engine::TradeEngine::instance().submitOrder(engineReq);

    // ── 受理订单 ──
    {
        TradeOrder accepted = order;
        accepted.setStatus(OrderStatusValue::Submitted);
        accepted.setStatusMessage("accepted");
        accepted.setBrokerOrderId(result.brokerOrderId);
        m_impl->appendRecentOrder(accepted);
        if (m_impl->m_orderAcceptedCallback) {
            m_impl->m_orderAcceptedCallback(accepted);
        }
    }

    if (!result.accepted) {
        return SubmitResult::rejected(result.message);
    }
    return SubmitResult::success(BrokerOrderId(result.brokerOrderId));
}

bool TradeExecutionEngine::cancelOrder(BrokerOrderId brokerOrderId) {
    return engine::TradeEngine::instance().cancelOrder(brokerOrderId.text());
}

// ============================================================================
// Scheduling controls
// ============================================================================
bool TradeExecutionEngine::approveExecutionCheckpoint(
    const std::string& executionScopeId, const std::string& batchId) {
    std::lock_guard<std::mutex> lock(m_impl->m_mutex);
    m_impl->m_approvedExecutionCheckpoints.insert(
        Impl::checkpointKey(executionScopeId, batchId));
    return true;
}

bool TradeExecutionEngine::resumeExecutionPause(
    const std::string& executionScopeId, const std::string& pausedBatchId) {
    const std::string key = Impl::scopeKey(executionScopeId);
    std::lock_guard<std::mutex> lock(m_impl->m_mutex);
    auto it = m_impl->m_pausedExecutionScopes.find(key);
    if (it == m_impl->m_pausedExecutionScopes.end()) return false;
    if (!pausedBatchId.empty() && it->second.pausedBatchId != pausedBatchId) return false;
    m_impl->m_pausedExecutionScopes.erase(it);
    return true;
}

// ============================================================================
// Queries
// ============================================================================
const std::vector<TradeOrder>& TradeExecutionEngine::recentOrders() const noexcept {
    return m_impl->m_recentOrders;
}

// ============================================================================
// 回调注册 — 直接连 TradeEngine，不经过 BrokerGateway
// ============================================================================
void TradeExecutionEngine::initCallbacks() {
    engine::TradeEngine::instance().setOnTradeFill([this](const engine::TradeFill& fill) {
        if (m_impl->m_tradeFillCallback) {
            domain::trading::TradeFill tf;
            tf.setBrokerOrderId(BrokerOrderId(fill.brokerOrderId));
            tf.setPrice(fill.price);
            tf.setQuantity(fill.quantity);
            m_impl->m_tradeFillCallback(tf);
        }
    });
}

void TradeExecutionEngine::setGateway(std::unique_ptr<IBrokerGateway>) {
    // 不再使用 BrokerGateway — 订单直接走 TradeEngine
}

IBrokerGateway* TradeExecutionEngine::gateway() const noexcept {
    return nullptr;
}

// ============================================================================
// Callbacks
// ============================================================================
void TradeExecutionEngine::setOnOrderAccepted(OrderAcceptedCallback cb) noexcept {
    m_impl->m_orderAcceptedCallback = std::move(cb);
}

void TradeExecutionEngine::setOnOrderUpdate(OrderUpdateCallback cb) noexcept {
    m_impl->m_orderUpdateCallback = std::move(cb);
}

void TradeExecutionEngine::setOnTradeFill(TradeFillCallback cb) noexcept {
    m_impl->m_tradeFillCallback = std::move(cb);
}

// ============================================================================
// Lifecycle
// ============================================================================
TradeExecutionEngine& TradeExecutionEngine::instance() {
    static TradeExecutionEngine engine;
    return engine;
}

TradeExecutionEngine::TradeExecutionEngine()
    : m_impl(std::make_unique<Impl>()) {
    m_initialized = true;

    auto* bus = engine::get_engine_event_bus();
    if (bus) {
        m_impl->m_orderSub = bus->subscribe("trading.order.updated",
            [this](const engine::EventFormat& e) {
                auto id     = e.get<std::string>("broker_order_id");
                auto status = e.get<std::int64_t>("status");
                auto filledPrice = e.get<double>("filled_price");
                auto filledQty  = e.get<std::int64_t>("filled_quantity");
                if (!id) return;
                std::lock_guard<std::mutex> lock(m_impl->m_mutex);
                for (auto& o : m_impl->m_recentOrders) {
                    if (o.brokerOrderId() == *id) {
                        if (filledPrice) o.setFilledPrice(*filledPrice);
                        if (filledQty)  o.setFilledQuantity(*filledQty);
                        if (status) {
                            OrderStatusValue st = static_cast<OrderStatusValue>(*status + 1);
                            o.setStatus(st);
                        }
                        if (m_impl->m_orderUpdateCallback)
                            m_impl->m_orderUpdateCallback(o);
                        break;
                    }
                }
            });
        m_impl->m_fillSub = bus->subscribe("trading.execution.report",
            [this](const engine::EventFormat& e) {
                TradeFill fill;
                fill.setBrokerOrderId(BrokerOrderId(e.get<std::string>("broker_order_id").value_or("")));
                fill.setFillId(FillId(e.get<std::string>("exec_id").value_or("")));
                fill.setPrice(e.get<double>("price").value_or(0.0));
                fill.setQuantity(e.get<std::int64_t>("quantity").value_or(0));
                if (m_impl->m_tradeFillCallback)
                    m_impl->m_tradeFillCallback(fill);
            });
    }
}

TradeExecutionEngine::~TradeExecutionEngine() = default;

void TradeExecutionEngine::setOnOrderGenerated(OrderGeneratedHandler h) { m_onOrderGenerated = std::move(h); }
void TradeExecutionEngine::setOnOrderSubmitResult(OrderSubmitResultHandler h) { m_onOrderSubmitResult = std::move(h); }

void TradeExecutionEngine::onOrders(const std::vector<strategy::OrderRequest>& orders) {
    for (const auto& req : orders) {
        if (!req.isValid()) continue;

        TradeOrder order;
        order.setSymbol(std::to_string(req.instrumentId().value));
        order.setSide(req.side() == strategy::RuntimeOrderSide::Buy
                          ? strategy::OrderDirection::Buy
                          : strategy::OrderDirection::Sell);
        order.setQuantity(static_cast<std::int64_t>(req.quantity()));
        order.setStrategyId(std::to_string(req.strategyInstanceId()));
        order.setPrice(0.0);

        if (m_onOrderGenerated) m_onOrderGenerated(order);

        strategy::RiskInput risk;
        risk.setStrategyId(order.strategyId());
        risk.setSymbol(order.symbol());
        risk.setBuyOrder(order.side() == strategy::OrderDirection::Buy);
        risk.setPrice(order.price());
        risk.setQuantity(order.quantity());
        risk.setAutoStrategySignal(true);

        auto result = submitOrder(order, risk);
        if (m_onOrderSubmitResult) m_onOrderSubmitResult(order, result);
    }
}

// ============================================================================
// Impl helpers
// ============================================================================
void TradeExecutionEngine::Impl::appendRecentOrder(const TradeOrder& order) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recentOrders.insert(m_recentOrders.begin(), order);
    if (static_cast<int>(m_recentOrders.size()) > kMaxRecentOrders) {
        m_recentOrders.resize(kMaxRecentOrders);
    }
    updateExecutionPauseLocked(order);
}

bool TradeExecutionEngine::Impl::updateExecutionPauseLocked(const TradeOrder& order) {
    if (!order.pauseOnAbnormalReject()) return false;

    const std::string key = scopeKey(order.executionScopeId());
    if (key.empty()) return false;

    // On reject → pause
    if (isOrderClosed(order)) {
        PauseState ps;
        ps.executionScopeId = order.executionScopeId();
        ps.pausedBatchId = order.batchId();
        ps.pausedBatchIndex = order.batchIndex();
        m_pausedExecutionScopes.insert_or_assign(key, ps);
        return true;
    }

    // On recovery → clear
    auto it = m_pausedExecutionScopes.find(key);
    if (it != m_pausedExecutionScopes.end() && order.batchIndex() == it->second.pausedBatchIndex) {
        m_pausedExecutionScopes.erase(it);
        return true;
    }
    return false;
}

bool TradeExecutionEngine::Impl::isOrderClosed(const TradeOrder& order) const noexcept {
    return order.isClosed();
}

} // namespace domain::trading
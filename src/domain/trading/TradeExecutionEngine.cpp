#include "TradeExecutionEngine.h"

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

    // Stage 4: submit via gateway
    if (!m_impl->m_gateway) {
        return SubmitResult::rejected("no broker gateway configured",
                                       OrderValidationCode::MissingRequiredFields);
    }

    OrderRequest request = OrderRequest::create(
        StrategyId(order.strategyId()),
        SymbolCode(order.symbol()),
        order.side() == strategy::OrderDirection::Buy ? OrderSide::Buy : OrderSide::Sell,
        OrderType::Limit,
        order.price(),
        order.quantity());

    // ── 受理订单：标记 Submitted，同步通知上层 ──
    {
        TradeOrder accepted = order;
        accepted.setStatus(OrderStatusValue::Submitted);
        accepted.setStatusMessage("accepted");
        m_impl->appendRecentOrder(accepted);
        if (m_impl->m_orderAcceptedCallback) {
            m_impl->m_orderAcceptedCallback(accepted);
        }
    }

    // ── 异步网关提交：回调中读取真实状态 ──
    m_impl->m_gateway->submitOrder(request, [this, order](const OrderStatus& status) {
        TradeOrder updated = order;
        updated.setBrokerOrderId(status.brokerOrderId().text());
        updated.setStatus(status.statusValue());
        updated.setFilledPrice(status.filledPrice());
        updated.setFilledQuantity(status.filledQuantity());
        if (status.statusValue() == OrderStatusValue::Rejected) {
            updated.setStatusMessage(status.attribute("error", "gateway rejected"));
        }

        // 更新引擎内订单记录
        {
            std::lock_guard<std::mutex> lock(m_impl->m_mutex);
            for (auto& o : m_impl->m_recentOrders) {
                if (o.symbol() == updated.symbol() && o.side() == updated.side()
                    && o.price() == updated.price() && o.quantity() == updated.quantity()
                    && o.status() == OrderStatusValue::Submitted) {
                    o.setBrokerOrderId(updated.brokerOrderId());
                    o.setStatus(updated.status());
                    o.setFilledPrice(updated.filledPrice());
                    o.setFilledQuantity(updated.filledQuantity());
                    o.setStatusMessage(updated.statusMessage());
                    break;
                }
            }
        }

        if (m_impl->m_orderUpdateCallback) {
            m_impl->m_orderUpdateCallback(updated);
        }
    });

    return SubmitResult::success(BrokerOrderId(""));
}

bool TradeExecutionEngine::cancelOrder(BrokerOrderId brokerOrderId) {
    if (!m_impl->m_gateway) return false;
    m_impl->m_gateway->cancelOrder(brokerOrderId, [this, brokerOrderId](const OrderStatus& status) {
        TradeOrder updated;
        updated.setBrokerOrderId(status.brokerOrderId().text());
        updated.setStatus(status.statusValue());
        updated.setStatusMessage(status.attribute("error",
            status.statusValue() == OrderStatusValue::Cancelled ? "cancelled" : "cancel failed"));
        if (m_impl->m_orderUpdateCallback) {
            m_impl->m_orderUpdateCallback(updated);
        }
    });
    return true;
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
// Gateway injection
// ============================================================================
void TradeExecutionEngine::setGateway(std::unique_ptr<IBrokerGateway> gateway) {
    m_impl->m_gateway = std::move(gateway);
    if (m_impl->m_gateway) {
        // 注册成交回报：网关推送成交 → 更新仓位 → 通知上层
        m_impl->m_gateway->setTradeCallback([this](const TradeFill& fill) {
            // 更新 PositionAccountEngine
            if (m_impl->m_tradeFillCallback) {
                m_impl->m_tradeFillCallback(fill);
            }
        });
        m_impl->m_gateway->setErrorCallback([this](const std::string& err) {
            fprintf(stderr, "[TradeExecEngine] gateway error: %s\n", err.c_str());
            fflush(stderr);
        });
    }
}

IBrokerGateway* TradeExecutionEngine::gateway() const noexcept {
    return m_impl->m_gateway.get();
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
TradeExecutionEngine::TradeExecutionEngine()
    : m_impl(std::make_unique<Impl>()) {}

TradeExecutionEngine::~TradeExecutionEngine() = default;

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
#include "TradeExecutionEngine.h"
#include "../strategy/include/RiskManager.h"
#include "../../engine/include/TradeEngine.h"
#include "../../engine/include/AccountEngine.h"
#include "../../engine/include/Event/EventBus.hpp"
#include "../../engine/include/Event/EventFormat.hpp"
#include "../../engine/include/GlobalEventBusRegistry.h"
#include "../market/include/MarketDataService.h"
#include "../../../infrastructure/include/database/OrderRecorder.h"
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
        INTERNAL_WARN_STREAM << "[TradeExec] validateOrder FAILED: " << vr.message()
                             << " code=" << static_cast<int>(vr.code());
        return SubmitResult::rejected(vr.message(), vr.code());
    }

    // Stage 2: scheduling conflict checks
    auto conflict = m_impl->checkExecutionPause(order);
    if (conflict && conflict->hasConflict()) {
        INTERNAL_WARN_STREAM << "[TradeExec] checkExecutionPause blocked: " << conflict->message();
        return SubmitResult::scheduleBlocked(conflict->code(), conflict->message());
    }
    conflict = m_impl->checkManualCheckpoint(order);
    if (conflict && conflict->hasConflict()) {
        INTERNAL_WARN_STREAM << "[TradeExec] manualCheckpoint blocked: " << conflict->message();
        return SubmitResult::scheduleBlocked(conflict->code(), conflict->message());
    }
    conflict = m_impl->checkPartialFillAdvance(order);
    if (conflict && conflict->hasConflict()) {
        INTERNAL_WARN_STREAM << "[TradeExec] partialFillAdvance blocked: " << conflict->message();
        return SubmitResult::scheduleBlocked(conflict->code(), conflict->message());
    }
    conflict = m_impl->checkPendingOrderConflict(order);
    if (conflict && conflict->hasConflict()) {
        INTERNAL_WARN_STREAM << "[TradeExec] pendingOrderConflict blocked: " << conflict->message();
        return SubmitResult::scheduleBlocked(conflict->code(), conflict->message());
    }

    // Stage 3: risk evaluation
    auto riskResult = strategy::RiskEvaluator::evaluateOrder(riskContext);
    if (!riskResult.approved()) {
        INTERNAL_WARN_STREAM << "[TradeExec] risk rejected: " << riskResult.description();
        return SubmitResult::riskRejected(riskResult.code(), riskResult.description());
    }

    // Stage 4: submit via TradeEngine
    if (!engine::TradeEngine::instance().initialized()) {
        INTERNAL_ERROR_STREAM << "[TradeExec] TradeEngine NOT initialized";
        return SubmitResult::rejected("TradeEngine not initialized",
                                       OrderValidationCode::MissingRequiredFields);
    }

    engine::OrderRequest engineReq;
    engineReq.setSymbol(order.symbol());
    engineReq.setStrategyId(order.strategyId());
    engineReq.setPrice(order.price());
    engineReq.setQuantity(order.quantity());
    engineReq.setSide(order.side() == strategy::OrderDirection::Buy
                      ? engine::OrderSide::Buy : engine::OrderSide::Sell);
    engineReq.setOrderType(order.orderType() == OrderType::Market
                           ? engine::OrderType::Market
                           : engine::OrderType::Limit);
    // A股规则: 买入=Open, 卖出=Close; 若已显式设置则尊重原值
    {
        auto pe = order.positionEffect();
        if (pe == strategy::PositionEffect::Unspecified) {
            pe = (order.side() == strategy::OrderDirection::Buy)
                 ? strategy::PositionEffect::Open
                 : strategy::PositionEffect::Close;
        }
        engineReq.setPositionEffect(
            static_cast<PositionEffect>(static_cast<int>(pe)));
    }
    engineReq.setClOrdId(order.clOrdId());
    engineReq.setAccountId(order.accountId());
    engineReq.setCurrency(order.currency());
    engineReq.setExchange(order.exchange());

    INTERNAL_INFO_STREAM << "[TradeExecEng] submitOrder symbol=" << order.symbol()
                         << " side=" << (order.side() == strategy::OrderDirection::Buy ? "Buy" : "Sell")
                         << " price=" << order.price() << " qty=" << order.quantity()
                         << " orderType=" << (riskContext.isAutoStrategySignal() ? "Market" : "Limit")
                         << " posEffect=" << static_cast<int>(engineReq.positionEffect());

    auto result = engine::TradeEngine::instance().submitOrder(engineReq);

    // ── 持久化: 订单写入 live_order 表 ──
    {
        using Rec = astock::infrastructure::database::OrderRecorder;
        int td = static_cast<int>(domain::market::MarketDataService::instance().activeTradingDay());
        if (td <= 0) {
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            struct tm local;
#if defined(_WIN32) || defined(_WIN64)
            localtime_s(&local, &tt);
#else
            localtime_r(&tt, &local);
#endif
            td = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
        }
        auto side = (order.side() == strategy::OrderDirection::Buy)
            ? astock::infrastructure::database::RecSide::Buy
            : astock::infrastructure::database::RecSide::Sell;
        auto otype = riskContext.isAutoStrategySignal()
            ? astock::infrastructure::database::RecOrdType::Market
            : astock::infrastructure::database::RecOrdType::Limit;
        auto pe = (static_cast<int>(engineReq.positionEffect()) == 0)
            ? astock::infrastructure::database::RecPosEff::Open
            : astock::infrastructure::database::RecPosEff::Close;
        Rec::instance().insertOrder(
            order.clOrdId(), order.strategyId(), order.symbol(),
            side, otype,
            order.price(), static_cast<int>(order.quantity()),
            order.signalStrength(), pe,
            td > 0 ? td : 0,
            std::to_string(order.basketId()));
        if (result.accepted) {
            Rec::instance().updateOrderStatus(order.clOrdId(),
                astock::infrastructure::database::RecOrdStatus::Pending, result.brokerOrderId, "");
        } else {
            Rec::instance().updateOrderStatus(order.clOrdId(),
                astock::infrastructure::database::RecOrdStatus::Rejected, "", result.message);
        }
    }

    if (result.accepted) {
        TradeOrder accepted = order;
        accepted.setStatus(OrderStatusValue::New);
        accepted.setStatusMessage("accepted");
        accepted.setBrokerOrderId(result.brokerOrderId);
        m_impl->appendRecentOrder(accepted);
        if (m_impl->m_orderAcceptedCallback) {
            m_impl->m_orderAcceptedCallback(accepted);
        }
        return SubmitResult::success(BrokerOrderId(result.brokerOrderId));
    }

    return SubmitResult::rejected(result.message);
}

bool TradeExecutionEngine::cancelOrder(BrokerOrderId brokerOrderId) {
    return engine::TradeEngine::instance().cancelOrder(brokerOrderId.text());
}

void TradeExecutionEngine::registerOrder(const TradeOrder& order) {
    m_impl->appendRecentOrder(order);
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
    engine::TradeEngine::instance().setOnOrderUpdate([this](const engine::OrderUpdate& u) {
        if (m_impl->m_orderUpdateCallback) {
            TradeOrder updated;
            updated.setBrokerOrderId(u.brokerOrderId);
            updated.setSymbol(u.symbol);
            updated.setFilledPrice(u.filledPrice);
            updated.setFilledQuantity(u.filledQuantity);
            updated.setStatusMessage(u.message);
            switch (u.status) {
                case 3: updated.setStatus(OrderStatusValue::Filled); break;
                case 2: updated.setStatus(OrderStatusValue::PartiallyFilled); break;
                case 5: updated.setStatus(OrderStatusValue::Cancelled); break;
                case 6: updated.setStatus(OrderStatusValue::Rejected); break;
                case 7: updated.setStatus(OrderStatusValue::Expired); break;
                default: updated.setStatus(OrderStatusValue::New); break;
            }
            std::lock_guard<std::mutex> lock(m_impl->m_mutex);
            for (auto& o : m_impl->m_recentOrders) {
                if (o.brokerOrderId() == u.brokerOrderId) {
                    o.setStatus(updated.status());
                    o.setFilledPrice(updated.filledPrice());
                    o.setFilledQuantity(updated.filledQuantity());
                    break;
                }
            }
            m_impl->m_orderUpdateCallback(updated);
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
                if (!id) {
                    INTERNAL_ERROR_STREAM << "[TradeExecEng] order.updated event missing broker_order_id";
                    return;
                }
                std::lock_guard<std::mutex> lock(m_impl->m_mutex);
                bool found = false;
                for (auto& o : m_impl->m_recentOrders) {
                    if (o.brokerOrderId() == *id) {
                        found = true;
                        if (filledPrice) o.setFilledPrice(*filledPrice);
                        if (filledQty)  o.setFilledQuantity(*filledQty);
                        if (status) {
                            OrderStatusValue st = static_cast<OrderStatusValue>(*status + 1);
                            o.setStatus(st);
                            // ── 持久化: 更新订单状态 ──
                            {
                                using RS = astock::infrastructure::database::RecOrdStatus;
                                RS recSt = RS::Pending;
                                switch (st) {
                                    case OrderStatusValue::PartiallyFilled: recSt = RS::PartiallyFilled; break;
                                    case OrderStatusValue::Filled:          recSt = RS::Filled; break;
                                    case OrderStatusValue::Cancelled:       recSt = RS::Cancelled; break;
                                    case OrderStatusValue::Rejected:        recSt = RS::Rejected; break;
                                    default: break;
                                }
                                astock::infrastructure::database::OrderRecorder::instance()
                                    .updateOrderStatus(o.clOrdId(), recSt, o.brokerOrderId(), "");
                            }
                            INTERNAL_INFO_STREAM << "[TradeExecEng] order.updated id=" << *id
                                                 << " evtStatus=" << *status
                                                 << " newSt=" << static_cast<int>(st)
                                                 << " filledQty=" << o.filledQuantity()
                                                 << " cb=" << (m_impl->m_orderUpdateCallback ? 1 : 0);
                        }
                        if (m_impl->m_orderUpdateCallback)
                            m_impl->m_orderUpdateCallback(o);
                        break;
                    }
                }
                if (!found) {
                    INTERNAL_ERROR_STREAM << "[TradeExecEng] order.updated id=" << *id
                                          << " NOT found in recentOrders (count="
                                          << m_impl->m_recentOrders.size() << ")";
                }
            });
        m_impl->m_fillSub = bus->subscribe("trading.execution.report",
            [this](const engine::EventFormat& e) {
                TradeFill fill;
                fill.setBrokerOrderId(BrokerOrderId(e.get<std::string>("broker_order_id").value_or("")));
                fill.setFillId(FillId(e.get<std::string>("exec_id").value_or("")));
                fill.setPrice(e.get<double>("price").value_or(0.0));
                fill.setQuantity(e.get<std::int64_t>("quantity").value_or(0));

                // ── 持久化: 更新 live_order 成交字段 ──
                {
                    auto clOrdId    = e.get<std::string>("cl_ord_id").value_or("");
                    auto execId     = e.get<std::string>("exec_id").value_or("");
                    auto fillPrice  = e.get<double>("price").value_or(0.0);
                    auto fillQty    = e.get<std::int64_t>("quantity").value_or(0);
                    auto fillAmount = e.get<double>("amount").value_or(fillPrice * static_cast<double>(fillQty));
                    auto commission = e.get<double>("commission").value_or(0.0);
                    auto fillTime   = e.get<std::string>("fill_time").value_or("");
                    if (!clOrdId.empty()) {
                        astock::infrastructure::database::OrderRecorder::instance().updateOrderFill(
                            clOrdId, execId,
                            fillPrice, static_cast<int>(fillQty), fillAmount,
                            commission, fillTime);
                    }
                }

                if (m_impl->m_tradeFillCallback)
                    m_impl->m_tradeFillCallback(fill);
            });
    }
}

TradeExecutionEngine::~TradeExecutionEngine() = default;

void TradeExecutionEngine::setOnOrderGenerated(OrderGeneratedHandler h) { m_onOrderGenerated = std::move(h); }
void TradeExecutionEngine::setOnOrderSubmitResult(OrderSubmitResultHandler h) { m_onOrderSubmitResult = std::move(h); }

void TradeExecutionEngine::onOrders(const std::vector<strategy::OrderRequest>& orders) {
    if (orders.empty()) return;

    // 单条走快速路径
    if (orders.size() == 1) {
        auto& req = orders[0];
        if (!req.isValid()) return;
        TradeOrder order = buildTradeOrder(req);
        if (m_onOrderGenerated) m_onOrderGenerated(order);
        auto risk = buildRiskInput(order);
        auto result = submitOrder(order, risk);
        if (m_onOrderSubmitResult) m_onOrderSubmitResult(order, result);
        return;
    }

    // ── 篮子委托: 先卖后买, 卖款回笼支撑买单 ──
    uint64_t basketId = orders.empty() ? 0
        : orders[0].extensionAs<uint64_t>(domain::trading::ExtKey::kBasketId, 0);
    INTERNAL_INFO_STREAM << "[TradeExec] 篮子委托: basketId=" << basketId
                         << " orders=" << orders.size();

    // 拆分为卖单和买单
    std::vector<TradeOrder> sells, buys;
    for (const auto& req : orders) {
        if (!req.isValid()) continue;
        auto order = buildTradeOrder(req);
        order.setBasketId(req.extensionAs<uint64_t>(domain::trading::ExtKey::kBasketId, 0));
        if (order.side() == strategy::OrderDirection::Sell)
            sells.push_back(std::move(order));
        else
            buys.push_back(std::move(order));
    }

    // 获取当前可用现金
    auto& accEng = engine::AccountEngine::instance();
    auto account = accEng.account();
    double cashAvailable = account.availableCash;

    int submitted = 0, rejected = 0;

    // ── 阶段1: 先卖 — 释放现金 ──
    for (auto& order : sells) {
        if (m_onOrderGenerated) m_onOrderGenerated(order);
        auto risk = buildRiskInput(order);
        auto result = submitOrder(order, risk);
        if (m_onOrderSubmitResult) m_onOrderSubmitResult(order, result);
        if (result.succeeded()) {
            cashAvailable += order.price() * static_cast<double>(order.quantity());
            ++submitted;
        } else {
            ++rejected;
        }
    }

    // ── 阶段2: 后买 — 现金约束 ──
    for (auto& order : buys) {
        double cost = order.price() * static_cast<double>(order.quantity());
        if (cost > cashAvailable && cashAvailable > 0) {
            // 缩量到可承受手数 (A股100股=1手)
            auto newQty = static_cast<std::int64_t>(
                cashAvailable / order.price() / 100.0) * 100;
            if (newQty < 100) {
                INTERNAL_WARN_STREAM << "[TradeExec] 篮子买单现金不足: "
                    << order.symbol() << " need=" << static_cast<int64_t>(cost)
                    << " cash=" << static_cast<int64_t>(cashAvailable) << " 跳过";
                ++rejected;
                continue;
            }
            order.setQuantity(newQty);
            cost = order.price() * static_cast<double>(newQty);
            INTERNAL_INFO_STREAM << "[TradeExec] 篮子买单缩量: "
                << order.symbol() << " qty=" << newQty;
        }

        if (m_onOrderGenerated) m_onOrderGenerated(order);
        auto risk = buildRiskInput(order);
        auto result = submitOrder(order, risk);
        if (m_onOrderSubmitResult) m_onOrderSubmitResult(order, result);
        if (result.succeeded()) {
            cashAvailable -= cost;
            ++submitted;
        } else {
            ++rejected;
        }
    }

    INTERNAL_INFO_STREAM << "[TradeExec] 篮子完成: basketId=" << basketId
                         << " submitted=" << submitted
                         << " rejected=" << rejected;
}

TradeOrder TradeExecutionEngine::buildTradeOrder(const strategy::OrderRequest& req) const {
    TradeOrder order;
    order.setSymbol(req.symbol());
    order.setSide(req.side() == OrderSide::Buy
                      ? strategy::OrderDirection::Buy
                      : strategy::OrderDirection::Sell);
    order.setQuantity(static_cast<std::int64_t>(req.quantity()));
    order.setStrategyId(req.strategyId());
    order.setPrice(req.price());
    order.setSignalStrength(
        req.extensionAs<double>(ExtKey::kSignalScore, 0.5));
    order.setOrderType(req.orderType());
    order.setClOrdId(req.clOrdId());
    order.setAccountId(req.accountId());
    order.setCurrency(req.currency());
    order.setExchange(req.exchange());
    order.setPositionEffect(
        static_cast<strategy::PositionEffect>(req.positionEffect()));
    return order;
}

strategy::RiskInput TradeExecutionEngine::buildRiskInput(const TradeOrder& order) const {
    strategy::RiskInput risk;
    risk.setStrategyId(order.strategyId());
    risk.setSymbol(order.symbol());
    risk.setBuyOrder(order.side() == strategy::OrderDirection::Buy);
    risk.setPrice(order.price());
    risk.setQuantity(order.quantity());
    risk.setSignalStrength(order.signalStrength() > 0.0 ? order.signalStrength() : 0.5);
    risk.setStrategyBound(true);
    risk.setStrategyActive(true);
    risk.setAutoStrategySignal(true);
    risk.setPositionSnapshotReady(true);
    risk.setTradingSessionOpen(true);
    if (!risk.isBuyOrder()) {
        auto& accEng = engine::AccountEngine::instance();
        for (const auto& pos : accEng.positions()) {
            if (pos.symbol == order.symbol()) {
                risk.setCloseableQuantity(pos.availableQty);
                break;
            }
        }
    }
    // 填充前日收盘价供涨跌停检查
    {
        auto& d = domain::market::MarketDataService::instance().liveData(order.symbol());
        if (d.valid() && d.preClose() > 0.0)
            risk.setReferencePrice(d.preClose());
    }
    strategy::RiskEvaluator::applyConfig(risk,
        domain::strategy::RiskManager::instance().riskConfig());
    return risk;
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
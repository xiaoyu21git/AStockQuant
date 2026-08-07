// TradeEngine.cpp — 交易引擎实现
#include "TradeEngine.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "GmSessionEngine.h"
#include "foundation/market/AStockSymbol.h"
#include "../../../thirdparty/gmsdk/strategy.h"

namespace engine {

// ═══════════════════════════════════════════════════════════════════
// 方向 / 类型转换
// ═══════════════════════════════════════════════════════════════════

namespace {
int toGmPositionEffect(domain::trading::PositionEffect pe) { return static_cast<int>(pe) + 1; }
std::string toGm(const std::string& internal) {
    auto sym = foundation::market::AStockSymbol::fromString(internal);
    if (!sym.isValid()) return "";
    return sym.gmSymbol();
}
} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
// 单例
// ═══════════════════════════════════════════════════════════════════

TradeEngine& TradeEngine::instance() {
    static TradeEngine engine;
    return engine;
}

// ═══════════════════════════════════════════════════════════════════
// 初始化 — 接收 GmSdkSingleton 持有的 Strategy
// ═══════════════════════════════════════════════════════════════════

bool TradeEngine::initialize(::Strategy* strategy) {
    if (!strategy) return false;
    m_strategy = strategy;

    auto bus = get_engine_event_bus();
    if (bus) {
        m_orderSub = bus->subscribe("trading.order.updated",
            [this](const EventFormat& e) {
                OrderUpdate u;
                u.brokerOrderId  = e.get<std::string>("broker_order_id").value_or("");
                u.symbol         = e.get<std::string>("symbol").value_or("");
                u.filledPrice    = e.get<double>("filled_price").value_or(0.0);
                u.filledQuantity = e.get<std::int64_t>("filled_quantity").value_or(0);
                u.message        = e.get<std::string>("message").value_or("");
                auto status      = e.get<std::int64_t>("status");
                if (status) u.status = static_cast<OrderUpdate::Status>(*status);
                onOrderStatus(u);
            });
        m_fillSub = bus->subscribe("trading.execution.report",
            [this](const EventFormat& e) {
                TradeFill f;
                f.fillId        = e.get<std::string>("exec_id").value_or("");
                f.brokerOrderId = e.get<std::string>("broker_order_id").value_or("");
                f.symbol        = e.get<std::string>("symbol").value_or("");
                f.price         = e.get<double>("price").value_or(0.0);
                f.quantity      = e.get<std::int64_t>("quantity").value_or(0);
                f.commission    = e.get<double>("commission").value_or(0.0);
                onTradeFill(f);
            });
    }
    return true;
}

void TradeEngine::shutdown() {
    auto bus = get_engine_event_bus();
    if (bus) {
        if (!m_orderSub.is_null()) bus->unsubscribe(m_orderSub);
        if (!m_fillSub.is_null())  bus->unsubscribe(m_fillSub);
    }
    m_strategy = nullptr;
}

bool TradeEngine::initialized() const {
    return m_strategy != nullptr;
}

// ═══════════════════════════════════════════════════════════════════
// 下单 / 撤单
// ═══════════════════════════════════════════════════════════════════

OrderResult TradeEngine::submitOrder(const OrderRequest& req) {
    OrderResult r;
    auto* s = m_strategy;
    if (!s) { r.message = "TradeEngine not initialized"; return r; }
    std::string gmSym = toGm(req.symbol());
    if (gmSym.empty()) { r.message = "invalid symbol: " + req.symbol(); return r; }

    // 构造 gmsdk 原生结构体，字段完整映射
    ::OrderRequest gmReq{};
    std::strncpy(gmReq.symbol, gmSym.c_str(), sizeof(gmReq.symbol) - 1);
    gmReq.side            = GmSessionEngine::toGmSide(req.side());
    gmReq.position_effect = toGmPositionEffect(req.positionEffect());
    gmReq.order_type      = GmSessionEngine::toGmOrderType(req.orderType());
    gmReq.price           = req.price();
    gmReq.volume          = static_cast<long long>(req.quantity());
    gmReq.stop_price      = req.extensionAs<double>(
                                domain::trading::ExtKey::kStopPrice, 0.0);
    gmReq.order_business  = static_cast<int>(req.extensionAs<int64_t>(
                                domain::trading::ExtKey::kOrderBusiness, 0));

    INTERNAL_INFO_STREAM << "[TradeEngine] place_order: gmSym=" << gmSym
                         << " side=" << gmReq.side
                         << " posEffect=" << gmReq.position_effect
                         << " orderType=" << gmReq.order_type
                         << " price=" << gmReq.price
                         << " vol=" << gmReq.volume
                         << " account=" << req.accountId();

    Order gm = s->place_order(gmReq, req.accountId().c_str());
    if (gm.cl_ord_id[0]) {
        r.brokerOrderId = gm.cl_ord_id; r.accepted = true;
        if (!req.clOrdId().empty()) {
            std::lock_guard<std::mutex> lk(m_ordersMutex);
            m_activeOrders[req.clOrdId()] = {req.clOrdId(), gm.cl_ord_id,
                std::chrono::steady_clock::now(), false};
        }
    } else {
        auto err = s->get_last_error_detail();
        r.message = (err && err[0]) ? err : "place_order rejected";
    }
    return r;
}

bool TradeEngine::cancelOrder(const std::string& orderId) {
    if (orderId.empty()) return false;
    auto* s = m_strategy;
    if (!s) return false;

    std::string brokerId, clOrdKey;
    {
        std::lock_guard<std::mutex> lk(m_ordersMutex);
        // 先按 clOrdId 查
        auto it = m_activeOrders.find(orderId);
        if (it != m_activeOrders.end()) {
            brokerId = it->second.brokerOrderId;
            clOrdKey = orderId;
        } else {
            // 回退: 按 brokerOrderId 查
            for (auto& [key, rec] : m_activeOrders) {
                if (rec.brokerOrderId == orderId) {
                    brokerId = rec.brokerOrderId;
                    clOrdKey = key;
                    break;
                }
            }
        }
    }
    if (brokerId.empty()) return false;

    s->order_cancel(brokerId.c_str(), NULL);
    {
        std::lock_guard<std::mutex> lk(m_ordersMutex);
        m_activeOrders.erase(clOrdKey);
    }
    INTERNAL_INFO_STREAM << "[TradeEngine] cancel: " << orderId;
    return true;
}

std::vector<OrderResult> TradeEngine::submitBatch(const std::vector<OrderRequest>& reqs) {
    std::vector<OrderResult> results;
    if (reqs.empty()) return results;

    auto* s = m_strategy;
    if (!s) {
        for (size_t i = 0; i < reqs.size(); ++i) {
            OrderResult r; r.message = "TradeEngine not initialized"; results.push_back(r);
        }
        return results;
    }

    // 转换为 gmsdk 原生结构体数组
    std::vector<::OrderRequest> gmReqs(reqs.size());
    std::vector<std::string> gmSyms(reqs.size());
    for (size_t i = 0; i < reqs.size(); ++i) {
        auto& gmReq = gmReqs[i];
        std::memset(&gmReq, 0, sizeof(gmReq));
        gmSyms[i] = toGm(reqs[i].symbol());
        if (gmSyms[i].empty()) continue;
        std::strncpy(gmReq.symbol, gmSyms[i].c_str(), sizeof(gmReq.symbol) - 1);
        gmReq.side       = GmSessionEngine::toGmSide(reqs[i].side());
        gmReq.volume     = static_cast<double>(reqs[i].quantity());
        gmReq.price      = reqs[i].price();
        gmReq.order_type = reqs[i].orderType() == OrderType::Market
            ? 1 : 2;  // 1=市价, 2=限价
        gmReq.position_effect = toGmPositionEffect(reqs[i].positionEffect());
    }

    // 调用掘金原生批量下单
    auto* gmResults = s->order_batch(gmReqs.data(), static_cast<int>(gmReqs.size()),
                                      reqs[0].accountId().empty() ? NULL : reqs[0].accountId().c_str());

    if (!gmResults) {
        for (size_t i = 0; i < reqs.size(); ++i) {
            OrderResult r; r.message = "order_batch returned null"; results.push_back(r);
        }
        return results;
    }

    for (int i = 0; i < gmResults->count(); ++i) {
        OrderResult r;
        auto& gm = gmResults->at(i);
        if (gm.cl_ord_id[0]) {
            r.brokerOrderId = gm.cl_ord_id;
            r.accepted = true;
            if (i < static_cast<int>(reqs.size()) && !reqs[i].clOrdId().empty()) {
                std::lock_guard<std::mutex> lk(m_ordersMutex);
                m_activeOrders[reqs[i].clOrdId()] = {reqs[i].clOrdId(), gm.cl_ord_id,
                    std::chrono::steady_clock::now(), false};
            }
        } else {
            auto err = s->get_last_error_detail();
            r.message = (err && err[0]) ? err : "order_batch rejected";
        }
        results.push_back(r);
    }
    gmResults->release();

    INTERNAL_INFO_STREAM << "[TradeEngine] order_batch: " << results.size()
                         << " submitted, " << reqs.size() << " requested";
    return results;
}

std::vector<OrderResult> TradeEngine::submitSplit(const OrderRequest& req, SplitSpec spec) {
    std::vector<OrderResult> results;
    if (req.quantity() <= 0 || spec.chunkSize <= 0) return results;

    int64_t remaining = static_cast<int64_t>(req.quantity());
    while (remaining > 0) {
        int64_t chunk = (std::min)(static_cast<int64_t>(spec.chunkSize), remaining);
        OrderRequest sub = req;
        sub.setQuantity(static_cast<double>(chunk));
        auto r = submitOrder(sub);
        results.push_back(r);
        remaining -= chunk;
        if (remaining > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(spec.intervalMs));
    }

    INTERNAL_INFO_STREAM << "[TradeEngine] split: " << results.size()
                         << " chunks, total qty=" << req.quantity();
    return results;
}

// ═══════════════════════════════════════════════════════════════════
// 自动撤单
// ═══════════════════════════════════════════════════════════════════

void TradeEngine::setAutoCancelTimeout(std::chrono::milliseconds timeout) {
    m_autoCancelTimeout = timeout;
}

void TradeEngine::startAutoCancel() {
    if (m_autoCancelRunning.load() || m_autoCancelTimeout.count() <= 0) return;
    m_autoCancelRunning.store(true);
    m_autoCancelThread = std::make_unique<std::thread>([this]() {
        while (m_autoCancelRunning.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            auto now = std::chrono::steady_clock::now();
            std::vector<std::string> toCancel;
            {
                std::lock_guard<std::mutex> lk(m_ordersMutex);
                for (auto& [id, rec] : m_activeOrders) {
                    if (!rec.filled && (now - rec.submitTime) > m_autoCancelTimeout) {
                        toCancel.push_back(id);
                    }
                }
            }
            for (auto& id : toCancel) {
                cancelOrder(id);
            }
        }
    });
    INTERNAL_INFO_STREAM << "[TradeEngine] 自动撤单已启动 timeout="
                         << m_autoCancelTimeout.count() << "ms";
}

void TradeEngine::stopAutoCancel() {
    m_autoCancelRunning.store(false);
    if (m_autoCancelThread && m_autoCancelThread->joinable())
        m_autoCancelThread->join();
}

// ═══════════════════════════════════════════════════════════════════
// 回调
// ═══════════════════════════════════════════════════════════════════

void TradeEngine::setOnOrderUpdate(OrderUpdateFn cb) { m_onOrderUpdate = std::move(cb); }
void TradeEngine::setOnTradeFill(TradeFillFn cb)     { m_onTradeFill   = std::move(cb); }

void TradeEngine::onOrderStatus(const OrderUpdate& u) {
    // 标记成交或终态订单, 从活跃列表移除
    if (u.status == OrderUpdate::Status::Filled
        || u.status == OrderUpdate::Status::Rejected
        || u.status == OrderUpdate::Status::Cancelled) {
        std::lock_guard<std::mutex> lk(m_ordersMutex);
        for (auto it = m_activeOrders.begin(); it != m_activeOrders.end(); ) {
            if (it->second.brokerOrderId == u.brokerOrderId) {
                it = m_activeOrders.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (m_onOrderUpdate) m_onOrderUpdate(u);
}

void TradeEngine::onTradeFill(const TradeFill& f) {
    // 标记已成交
    {
        std::lock_guard<std::mutex> lk(m_ordersMutex);
        for (auto& [id, rec] : m_activeOrders) {
            if (rec.brokerOrderId == f.brokerOrderId) rec.filled = true;
        }
    }
    if (m_onTradeFill) m_onTradeFill(f);
}

} // namespace engine

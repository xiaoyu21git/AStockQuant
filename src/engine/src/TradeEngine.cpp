// TradeEngine.cpp — 交易引擎实现
#include "TradeEngine.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "../../../thirdparty/gmsdk/strategy.h"

namespace engine {

// ═══════════════════════════════════════════════════════════════════
// 方向 / 类型转换
// ═══════════════════════════════════════════════════════════════════

namespace {
int toGmSide(OrderSide s)       { return s == OrderSide::Buy ? 1 : 2; }
int toGmOrderType(OrderType t)   { return t == OrderType::Limit ? 1 : 2; }
std::string toGm(const std::string& internal) {
    auto dot = internal.find('.');
    if (dot == std::string::npos) return "";
    std::string code = internal.substr(0, dot);
    std::string exch = internal.substr(dot + 1);
    if (exch == "SH") return "SHSE." + code;
    if (exch == "SZ") return "SZSE." + code;
    if (exch == "BJ") return "BSE."  + code;
    return "";
}
std::string fromGm(const std::string& gm) {
    if (gm.compare(0, 5, "SHSE.") == 0) return gm.substr(5) + ".SH";
    if (gm.compare(0, 5, "SZSE.") == 0) return gm.substr(5) + ".SZ";
    if (gm.compare(0, 4, "BSE.")  == 0) return gm.substr(4) + ".BJ";
    return gm;
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

bool TradeEngine::initialize(void* strategy) {
    if (!strategy) return false;
    m_strategy = strategy;

    auto* bus = get_engine_event_bus();
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
    auto* bus = get_engine_event_bus();
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
    auto* s = static_cast<::Strategy*>(m_strategy);
    if (!s) { r.message = "TradeEngine not initialized"; return r; }
    std::string gmSym = toGm(req.symbol());
    if (gmSym.empty()) { r.message = "invalid symbol: " + req.symbol(); return r; }

    // 构造 gmsdk 原生结构体，字段完整映射
    ::OrderRequest gmReq{};
    std::strncpy(gmReq.symbol, gmSym.c_str(), sizeof(gmReq.symbol) - 1);
    gmReq.side            = toGmSide(req.side());
    gmReq.position_effect = static_cast<int>(req.positionEffect());
    gmReq.order_type      = toGmOrderType(req.orderType());
    gmReq.price           = req.price();
    gmReq.volume          = static_cast<long long>(req.quantity());
    gmReq.stop_price      = req.extensionAs<double>(
                                domain::trading::ExtKey::kStopPrice, 0.0);
    gmReq.order_business  = static_cast<int>(req.extensionAs<int64_t>(
                                domain::trading::ExtKey::kOrderBusiness, 0));

    Order gm = s->place_order(gmReq, req.accountId().c_str());
    if (gm.cl_ord_id[0]) {
        r.brokerOrderId = gm.cl_ord_id; r.accepted = true;
    } else {
        auto err = s->get_last_error_detail();
        r.message = (err && err[0]) ? err : "place_order rejected";
    }
    return r;
}

bool TradeEngine::cancelOrder(const std::string& brokerOrderId) {
    if (brokerOrderId.empty()) return false;
    auto* s = static_cast<::Strategy*>(m_strategy);
    if (!s) return false;
    // order_cancel(cl_ord_id, account) — account=NULL 使用策略默认账户
    s->order_cancel(brokerOrderId.c_str(), NULL);
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// 回调
// ═══════════════════════════════════════════════════════════════════

void TradeEngine::setOnOrderUpdate(OrderUpdateFn cb) { m_onOrderUpdate = std::move(cb); }
void TradeEngine::setOnTradeFill(TradeFillFn cb)     { m_onTradeFill   = std::move(cb); }

void TradeEngine::onOrderStatus(const OrderUpdate& u) {
    if (m_onOrderUpdate) m_onOrderUpdate(u);
}
void TradeEngine::onTradeFill(const TradeFill& f) {
    if (m_onTradeFill) m_onTradeFill(f);
}

} // namespace engine

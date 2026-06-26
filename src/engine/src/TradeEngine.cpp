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
int toGmSide(OrderRequest::Side s)       { return s == OrderRequest::Buy ? 1 : 2; }
int toGmOrderType(OrderRequest::Type t)   { return t == OrderRequest::Limit ? 1 : 2; }
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
    return true;
}

void TradeEngine::shutdown() {
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
    std::string gmSym = toGm(req.symbol);
    if (gmSym.empty()) { r.message = "invalid symbol: " + req.symbol; return r; }

    Order gm = s->place_order(gmSym.c_str(),
                              static_cast<int>(req.quantity),
                              toGmSide(req.side),
                              toGmOrderType(req.orderType),
                              1,        // position_effect: open
                              req.price, 0, 0, 0.0, 0,
                              req.strategyId.empty() ? nullptr : req.strategyId.c_str());
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
    s->order_cancel(brokerOrderId.c_str(), "");
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

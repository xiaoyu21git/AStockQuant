#include "JujinBrokerGateway.h"
#include "../../engine/include/GmSessionEngine.h"
#include "foundation/log/logging.hpp"

namespace app::adapters {

bool JujinBrokerGateway::connect(const std::string&)    { return engine::GmSessionEngine::instance().initialized(); }
void JujinBrokerGateway::disconnect()                    {}
bool JujinBrokerGateway::isConnected() const             { return engine::GmSessionEngine::instance().initialized(); }

domain::trading::BrokerCapability JujinBrokerGateway::capability() const {
    domain::trading::BrokerCapability c;
    c.setBrokerName(domain::trading::BrokerName("jujin"));
    return c;
}

// ── 下单 / 撤单 → GmSessionEngine ──

void JujinBrokerGateway::submitOrder(const domain::trading::OrderRequest& request,
                                     domain::trading::OrderCallback onResult) {
    if (!onResult) return;
    domain::trading::OrderStatus status;
    auto& eng = engine::GmSessionEngine::instance();
    if (!eng.initialized()) {
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", "GmSessionEngine not initialized");
        onResult(status); return;
    }
    engine::OrderRequest req;
    req.symbol    = request.symbol().text();
    req.price     = request.price();
    req.quantity  = static_cast<int64_t>(request.quantity());
    req.side      = (request.side() == domain::trading::OrderSide::Buy) ? engine::OrderRequest::Buy : engine::OrderRequest::Sell;
    req.orderType = (request.orderType() == domain::trading::OrderType::Limit) ? engine::OrderRequest::Limit : engine::OrderRequest::Market;

    auto r = eng.submitOrder(req);
    if (r.accepted) {
        status.setBrokerOrderId(domain::trading::BrokerOrderId(r.brokerOrderId));
        status.setStatusValue(domain::trading::OrderStatusValue::Submitted);
    } else {
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", r.message);
    }
    onResult(status);
}

void JujinBrokerGateway::cancelOrder(domain::trading::BrokerOrderId brokerOrderId,
                                     domain::trading::OrderCallback onResult) {
    if (!onResult) return;
    domain::trading::OrderStatus status;
    status.setBrokerOrderId(brokerOrderId);
    if (engine::GmSessionEngine::instance().cancelOrder(brokerOrderId.text())) {
        status.setStatusValue(domain::trading::OrderStatusValue::Cancelled);
    } else {
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", "cancel order failed");
    }
    onResult(status);
}

// ── 查询 → GmSessionEngine ──

void JujinBrokerGateway::queryOrder(domain::trading::BrokerOrderId,
                                    domain::trading::OrderQueryCallback onResult) {
    if (onResult) onResult(std::nullopt);
}

void JujinBrokerGateway::queryPositions(domain::trading::PositionsQueryCallback onResult) {
    if (!onResult) return;
    std::vector<domain::trading::PositionSnapshot> result;
    for (auto& p : engine::GmSessionEngine::instance().queryPositions()) {
        domain::trading::PositionSnapshot ps;
        ps.setSymbol(domain::trading::SymbolCode(p.symbol));
        if (p.quantity > 0) ps.setLongQuantity(p.quantity);
        else                ps.setShortQuantity(-p.quantity);
        ps.setAverageCost(p.costPrice);
        ps.setMarketValue(p.marketValue);
        result.push_back(ps);
    }
    onResult(result);
}

void JujinBrokerGateway::queryAccount(domain::trading::AccountQueryCallback onResult) {
    if (!onResult) return;
    auto a = engine::GmSessionEngine::instance().queryAccount();
    domain::trading::AccountInfo info;
    info.setAvailableCash(a.availableCash);
    info.setTotalAssets(a.totalAsset);
    info.setMarketValue(a.marketValue);
    onResult(info);
}

// ── 回调 ──

void JujinBrokerGateway::setTradeCallback(domain::trading::TradeCallback cb)   { m_tradeCallback = std::move(cb); }
void JujinBrokerGateway::setErrorCallback(domain::trading::ErrorCallback cb)   { m_errorCallback = std::move(cb); }
std::string JujinBrokerGateway::lastError() const { return m_lastError; }

// ── 不支持 ──

void JujinBrokerGateway::submitAlgoOrFail(const domain::trading::AlgoOrderRequest&,
                                          domain::trading::OrderCallback onResult) {
    if (onResult) {
        domain::trading::OrderStatus st;
        st.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        st.setAttribute("error", "algo not supported");
        onResult(st);
    }
}
void JujinBrokerGateway::submitAlgoOrder(const domain::trading::AlgoOrderRequest&,
                                         domain::trading::OrderCallback onResult) {
    if (onResult) {
        domain::trading::OrderStatus st;
        st.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        st.setAttribute("error", "algo not supported");
        onResult(st);
    }
}
void JujinBrokerGateway::submitBasket(const std::vector<domain::trading::OrderRequest>&,
                                      domain::trading::OrderCallback) {}
void JujinBrokerGateway::queryTrades(foundation::utils::Timestamp, foundation::utils::Timestamp,
                                     domain::trading::TradeCallback onResult) {
    if (onResult) onResult({});
}

} // namespace app::adapters

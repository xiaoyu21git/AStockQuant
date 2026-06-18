#include "JujinBrokerGateway.h"
#include "JujinApi.h"
#include "foundation/config/TradingConfig.h"
#include "../../engine/include/GlobalEventBusRegistry.h"

#include <cstring>

namespace app::adapters {

JujinBrokerGateway::JujinBrokerGateway() = default;
JujinBrokerGateway::~JujinBrokerGateway() = default;

bool JujinBrokerGateway::connect(const std::string& /*configJson*/) {
    // 复用 JMC 已创建的共享 JujinApi（内含 GmStrategySession），不重复建 SDK 实例
    m_api = engine::get_shared_jujin_api();
    if (!m_api || !m_api->is_connected()) {
        m_lastError = "shared JujinApi not connected";
        return false;
    }
    return true;
}

void JujinBrokerGateway::disconnect() {
    m_api = nullptr;
}

bool JujinBrokerGateway::isConnected() const {
    return m_api && m_api->is_connected();
}

domain::trading::BrokerCapability JujinBrokerGateway::capability() const {
    domain::trading::BrokerCapability c;
    c.setBrokerName(domain::trading::BrokerName("jujin"));
    return c;
}

void JujinBrokerGateway::submitOrder(
    const domain::trading::OrderRequest& request,
    domain::trading::OrderCallback onResult) {
    if (!onResult || !m_api) return;
    domain::trading::OrderStatus status;
    auto side = (request.side() == domain::trading::OrderSide::Buy)
        ? thirdparty::OrderSide::BUY : thirdparty::OrderSide::SELL;
    auto type = (request.orderType() == domain::trading::OrderType::Limit)
        ? thirdparty::OrderType::LIMIT : thirdparty::OrderType::MARKET;
    std::string oid = m_api->place_order(
        request.symbol().text(), side, type,
        request.price(), static_cast<double>(request.quantity()));
    if (!oid.empty()) {
        status.setBrokerOrderId(domain::trading::BrokerOrderId(oid));
        status.setStatusValue(domain::trading::OrderStatusValue::Submitted);
    } else {
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", m_api->last_error_message());
    }
    onResult(status);
}

void JujinBrokerGateway::cancelOrder(
    domain::trading::BrokerOrderId brokerOrderId,
    domain::trading::OrderCallback onResult) {
    if (!onResult || !m_api) return;
    domain::trading::OrderStatus status;
    status.setBrokerOrderId(brokerOrderId);
    if (m_api->cancel_order(brokerOrderId.text())) {
        status.setStatusValue(domain::trading::OrderStatusValue::Cancelled);
    } else {
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", m_api->last_error_message());
    }
    onResult(status);
}

void JujinBrokerGateway::queryOrder(
    domain::trading::BrokerOrderId brokerOrderId,
    domain::trading::OrderQueryCallback onResult) {
    if (!onResult || !m_api) { if (onResult) onResult(std::nullopt); return; }
    auto result = m_api->query_order(brokerOrderId.text());
    if (!result.order_id.empty()) {
        domain::trading::OrderStatus status;
        status.setBrokerOrderId(brokerOrderId);
        status.setAttribute("filled_quantity", std::to_string(result.filled_quantity));
        onResult(status);
        return;
    }
    onResult(std::nullopt);
}

void JujinBrokerGateway::queryPositions(
    domain::trading::PositionsQueryCallback onResult) {
    if (!onResult || !m_api) return;
    std::vector<domain::trading::PositionSnapshot> result;
    auto positions = m_api->query_positions();
    for (auto& p : positions) {
        domain::trading::PositionSnapshot ps;
        ps.setSymbol(domain::trading::SymbolCode(p.symbol));
        if (p.quantity > 0) ps.setLongQuantity(p.quantity);
        else ps.setShortQuantity(-p.quantity);
        ps.setAverageCost(p.price);
        ps.setMarketValue(p.market_value);
        result.push_back(ps);
    }
    onResult(result);
}

void JujinBrokerGateway::queryAccount(
    domain::trading::AccountQueryCallback onResult) {
    if (!onResult || !m_api) return;
    auto acc = m_api->query_account();
    domain::trading::AccountInfo info;
    info.setAvailableCash(acc.available);
    info.setTotalAssets(acc.total_asset);
    info.setMarketValue(acc.market_value);
    onResult(info);
}

void JujinBrokerGateway::setTradeCallback(domain::trading::TradeCallback) {}
void JujinBrokerGateway::setErrorCallback(domain::trading::ErrorCallback) {}

std::string JujinBrokerGateway::lastError() const { return m_lastError; }

void JujinBrokerGateway::submitAlgoOrFail(
    const domain::trading::AlgoOrderRequest&,
    domain::trading::OrderCallback onResult) {
    if (onResult) {
        domain::trading::OrderStatus st;
        st.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        st.setAttribute("error", "algo not supported");
        onResult(st);
    }
}

void JujinBrokerGateway::submitAlgoOrder(
    const domain::trading::AlgoOrderRequest&,
    domain::trading::OrderCallback onResult) {
    if (onResult) {
        domain::trading::OrderStatus st;
        st.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        st.setAttribute("error", "algo not supported");
        onResult(st);
    }
}

void JujinBrokerGateway::submitBasket(
    const std::vector<domain::trading::OrderRequest>&,
    domain::trading::OrderCallback) {}

void JujinBrokerGateway::queryTrades(
    foundation::utils::Timestamp,
    foundation::utils::Timestamp,
    domain::trading::TradeCallback onResult) {
    if (onResult) onResult({});
}

} // namespace app::adapters

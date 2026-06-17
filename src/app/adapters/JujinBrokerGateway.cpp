#include "JujinBrokerGateway.h"

#include <cstring>

namespace app::adapters {
namespace {

std::string jsonGet(const std::string& json, const char* key) {
    std::string search = "\"";
    search += key;
    search += "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}

} // anonymous namespace

JujinBrokerGateway::JujinBrokerGateway()
    : m_session(std::make_unique<JujinSession>()) {}

JujinBrokerGateway::~JujinBrokerGateway() = default;

bool JujinBrokerGateway::connect(const std::string& configJson) {
    std::string token = jsonGet(configJson, "token");
    std::string accountId = jsonGet(configJson, "accountId");
    if (token.empty()) {
        m_lastError = "token not found in config";
        return false;
    }
    if (!m_session->initialize(token.c_str(), accountId.c_str())) {
        m_lastError = m_session->last_error();
        return false;
    }
    if (!m_session->connect()) {
        m_lastError = m_session->last_error();
        return false;
    }
    return true;
}

void JujinBrokerGateway::disconnect() { m_session->disconnect(); }

bool JujinBrokerGateway::isConnected() const { return m_session->is_connected(); }

domain::trading::BrokerCapability JujinBrokerGateway::capability() const {
    domain::trading::BrokerCapability c;
    c.setBrokerName(domain::trading::BrokerName("jujin"));
    return c;
}

void JujinBrokerGateway::submitOrder(
    const domain::trading::OrderRequest& request,
    domain::trading::OrderCallback onResult) {
    if (!onResult) return;
    domain::trading::OrderStatus status;
    int side = (request.side() == domain::trading::OrderSide::Buy) ? 1 : 2;
    int type = (request.orderType() == domain::trading::OrderType::Limit) ? 2 : 1;
    std::string oid = m_session->place_order(
        request.symbol().text().c_str(), side, type,
        request.quantity(), request.price());
    if (!oid.empty()) {
        status.setBrokerOrderId(domain::trading::BrokerOrderId(oid));
        status.setStatusValue(domain::trading::OrderStatusValue::Submitted);
    } else {
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", m_session->last_error());
    }
    onResult(status);
}

void JujinBrokerGateway::cancelOrder(
    domain::trading::BrokerOrderId brokerOrderId,
    domain::trading::OrderCallback onResult) {
    if (!onResult) return;
    domain::trading::OrderStatus status;
    status.setBrokerOrderId(brokerOrderId);
    if (m_session->cancel_order(brokerOrderId.text().c_str())) {
        status.setStatusValue(domain::trading::OrderStatusValue::Cancelled);
    } else {
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", m_session->last_error());
    }
    onResult(status);
}

void JujinBrokerGateway::queryOrder(
    domain::trading::BrokerOrderId brokerOrderId,
    domain::trading::OrderQueryCallback onResult) {
    if (!onResult) return;
    auto orders = m_session->query_orders();
    for (auto& o : orders) {
        if (o->order_id() == brokerOrderId.text()) {
            domain::trading::OrderStatus status;
            status.setBrokerOrderId(brokerOrderId);
            status.setStatusValue(static_cast<domain::trading::OrderStatusValue>(o->status()));
            onResult(status);
            return;
        }
    }
    onResult(std::nullopt);
}

void JujinBrokerGateway::queryPositions(
    domain::trading::PositionsQueryCallback onResult) {
    if (!onResult) return;
    std::vector<domain::trading::PositionSnapshot> result;
    auto positions = m_session->query_positions();
    for (auto& p : positions) {
        domain::trading::PositionSnapshot ps;
        ps.setSymbol(domain::trading::SymbolCode(p->symbol()));
        if (p->volume() > 0) ps.setLongQuantity(p->volume());
        else ps.setShortQuantity(-p->volume());
        ps.setAverageCost(p->price());
        ps.setMarketValue(p->market_value());
        result.push_back(ps);
    }
    onResult(result);
}

void JujinBrokerGateway::queryAccount(
    domain::trading::AccountQueryCallback onResult) {
    if (!onResult) return;
    auto acc = m_session->query_account();
    if (!acc) { onResult(std::nullopt); return; }
    domain::trading::AccountInfo info;
    info.setAvailableCash(acc->available_cash());
    info.setTotalAssets(acc->total_asset());
    info.setMarketValue(acc->market_value());
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

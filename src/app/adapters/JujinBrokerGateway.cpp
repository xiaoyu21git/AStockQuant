#include "JujinBrokerGateway.h"
#include "../../engine/include/JujinApi.h"
#include "foundation/config/TradingConfig.h"
#include "../../engine/include/GlobalEventBusRegistry.h"

#include <cstring>
#include <iostream>

namespace app::adapters {

JujinBrokerGateway::JujinBrokerGateway() = default;
JujinBrokerGateway::~JujinBrokerGateway() = default;

// ── 懒解析共享 API：JMC 可能在 gateway connect 之后才启动 ──
void JujinBrokerGateway::tryResolveApi() const {
    if (m_api && m_api->is_connected()) return;
    auto* resolved = engine::get_shared_jujin_api();
    if (resolved && resolved->is_connected()) {
        m_api = resolved;
    }
}

bool JujinBrokerGateway::connect(const std::string& /*configJson*/) {
    tryResolveApi();
    if (!m_api) {
        INTERNAL_ERROR_STREAM << "[JujinBrokerGateway] shared JujinApi not yet available, will retry on first order";
    }
    return true;
}

void JujinBrokerGateway::disconnect() { m_api = nullptr; }

bool JujinBrokerGateway::isConnected() const {
    if (!m_api) {
        tryResolveApi();  // const 安全：m_api 已声明为 mutable
    }
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
    if (!onResult) return;
    tryResolveApi();
    if (!m_api) {
        domain::trading::OrderStatus status;
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", "shared JujinApi not available (JMC not started?)");
        onResult(status);
        return;
    }
    domain::trading::OrderStatus status;
    auto side = (request.side() == domain::trading::OrderSide::Buy)
                    ? thirdparty::OrderSide::BUY
                    : thirdparty::OrderSide::SELL;
    auto type = (request.orderType() == domain::trading::OrderType::Limit)
                    ? thirdparty::OrderType::LIMIT
                    : thirdparty::OrderType::MARKET;
    std::string oid = m_api->place_order(request.symbol().text(), side, type,
                                         request.price(),
                                         static_cast<double>(request.quantity()));
    if (!oid.empty()) {
        status.setBrokerOrderId(domain::trading::BrokerOrderId(oid));
        status.setStatusValue(domain::trading::OrderStatusValue::Submitted);
    } else {
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", m_api->last_error_message());
    }
    onResult(status);
}

void JujinBrokerGateway::cancelOrder(domain::trading::BrokerOrderId brokerOrderId,
                                     domain::trading::OrderCallback onResult) {
    if (!onResult) return;
    tryResolveApi();
    if (!m_api) {
        domain::trading::OrderStatus status;
        status.setBrokerOrderId(brokerOrderId);
        status.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        status.setAttribute("error", "shared JujinApi not available");
        onResult(status);
        return;
    }
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

void JujinBrokerGateway::queryOrder(domain::trading::BrokerOrderId brokerOrderId,
                                    domain::trading::OrderQueryCallback onResult) {
    if (!onResult) return;
    tryResolveApi();
    if (!m_api) {
        onResult(std::nullopt);
        return;
    }
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
    if (!onResult) return;
    tryResolveApi();
    if (!m_api) {
        onResult({});
        return;
    }
    std::vector<domain::trading::PositionSnapshot> result;
    auto positions = m_api->query_positions();
    for (auto& p : positions) {
        domain::trading::PositionSnapshot ps;
        ps.setSymbol(domain::trading::SymbolCode(p.symbol));
        if (p.quantity > 0)
            ps.setLongQuantity(p.quantity);
        else
            ps.setShortQuantity(-p.quantity);
        ps.setAverageCost(p.price);
        ps.setMarketValue(p.market_value);
        result.push_back(ps);
    }
    onResult(result);
}

void JujinBrokerGateway::queryAccount(
    domain::trading::AccountQueryCallback onResult) {
    if (!onResult) return;
    tryResolveApi();
    if (!m_api) {
        onResult(std::nullopt);
        return;
    }
    auto acc = m_api->query_account();
    domain::trading::AccountInfo info;
    info.setAvailableCash(acc.available);
    info.setTotalAssets(acc.total_asset);
    info.setMarketValue(acc.market_value);
    onResult(info);
}

void JujinBrokerGateway::setTradeCallback(domain::trading::TradeCallback cb) {
    m_tradeCallback = std::move(cb);
}

void JujinBrokerGateway::setErrorCallback(domain::trading::ErrorCallback cb) {
    m_errorCallback = std::move(cb);
}

std::string JujinBrokerGateway::lastError() const { return m_lastError; }

void JujinBrokerGateway::submitAlgoOrFail(
    const domain::trading::AlgoOrderRequest& /*request*/,
    domain::trading::OrderCallback onResult) {
    if (onResult) {
        domain::trading::OrderStatus st;
        st.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        st.setAttribute("error", "algo not supported");
        onResult(st);
    }
}

void JujinBrokerGateway::submitAlgoOrder(
    const domain::trading::AlgoOrderRequest& /*request*/,
    domain::trading::OrderCallback onResult) {
    if (onResult) {
        domain::trading::OrderStatus st;
        st.setStatusValue(domain::trading::OrderStatusValue::Rejected);
        st.setAttribute("error", "algo not supported");
        onResult(st);
    }
}

void JujinBrokerGateway::submitBasket(
    const std::vector<domain::trading::OrderRequest>& /*orders*/,
    domain::trading::OrderCallback /*onResult*/) {}

void JujinBrokerGateway::queryTrades(foundation::utils::Timestamp /*startDate*/,
                                     foundation::utils::Timestamp /*endDate*/,
                                     domain::trading::TradeCallback onResult) {
    if (onResult) onResult({});
}

}  // namespace app::adapters

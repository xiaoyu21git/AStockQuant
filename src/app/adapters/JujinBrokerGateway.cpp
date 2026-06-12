#include "JujinBrokerGateway.h"

namespace app {
namespace adapters {

using namespace domain::trading;

class JujinBrokerGateway::Impl {
public:
    bool connected{false};
    std::string lastError_;
    BrokerCapability capability_;
    OrderCallback orderCallback;
    TradeCallback tradeCallback;
    ErrorCallback errorCallback;

    Impl()
    {
        capability_.setBrokerName(BrokerName("jujin"));
        capability_.setSupportsAlgo(true);
        capability_.setSupportsBasket(false);
        capability_.setSupportsConditional(false);
        capability_.setSupportsShortSelling(true);
        capability_.setSupportsFutures(false);
        capability_.setSupportsOptions(false);
    }
};

JujinBrokerGateway::JujinBrokerGateway()
    : m_impl(std::make_unique<Impl>()) {}

JujinBrokerGateway::~JujinBrokerGateway()
{
    if (m_impl && m_impl->connected) {
        disconnect();
    }
}

bool JujinBrokerGateway::connect(const std::string& /*configJson*/)
{
    m_impl->connected = true;
    return true;
}

void JujinBrokerGateway::disconnect()
{
    m_impl->connected = false;
}

bool JujinBrokerGateway::isConnected() const
{
    return m_impl->connected;
}

BrokerCapability JujinBrokerGateway::capability() const
{
    return m_impl->capability_;
}

void JujinBrokerGateway::submitOrder(
    const OrderRequest& /*request*/,
    OrderCallback onResult)
{
    OrderStatus status;
    status.setBrokerOrderId(BrokerOrderId("jujin-pending"));
    status.setCorrelationId(CorrelationId("placeholder"));
    status.setStatusValue(OrderStatusValue::Submitted);
    if (onResult) {
        onResult(status);
    }
}

void JujinBrokerGateway::cancelOrder(
    BrokerOrderId /*brokerOrderId*/,
    OrderCallback onResult)
{
    if (onResult) {
        OrderStatus status;
        status.setStatusValue(OrderStatusValue::Cancelled);
        onResult(status);
    }
}

void JujinBrokerGateway::queryOrder(
    BrokerOrderId /*brokerOrderId*/,
    OrderQueryCallback onResult)
{
    if (onResult) {
        onResult(std::nullopt);
    }
}

void JujinBrokerGateway::queryPositions(
    PositionsQueryCallback onResult)
{
    if (onResult) {
        onResult({});
    }
}

void JujinBrokerGateway::queryAccount(
    AccountQueryCallback onResult)
{
    if (onResult) {
        onResult(std::nullopt);
    }
}

void JujinBrokerGateway::submitAlgoOrFail(
    const AlgoOrderRequest& /*request*/,
    OrderCallback onResult)
{
    if (!m_impl->capability_.supportsAlgo()) {
        if (onResult) {
            OrderStatus status;
            status.setStatusValue(OrderStatusValue::Rejected);
            status.setAttribute("error", "Algo trading not supported");
            onResult(status);
        }
        return;
    }
    // TODO: real implementation
}

void JujinBrokerGateway::submitAlgoOrder(
    const AlgoOrderRequest& /*request*/,
    OrderCallback onResult)
{
    if (onResult) {
        OrderStatus status;
        status.setStatusValue(OrderStatusValue::Submitted);
        onResult(status);
    }
}

void JujinBrokerGateway::submitBasket(
    const std::vector<OrderRequest>& /*orders*/,
    OrderCallback /*onResult*/)
{
    m_impl->lastError_ = "Basket orders not supported by Jujin";
    if (m_impl->errorCallback) {
        m_impl->errorCallback(m_impl->lastError_);
    }
}

void JujinBrokerGateway::queryTrades(
    foundation::utils::Timestamp /*startDate*/,
    foundation::utils::Timestamp /*endDate*/,
    TradeCallback /*onResult*/)
{
    // TODO: query trades
}

void JujinBrokerGateway::setTradeCallback(TradeCallback callback)
{
    m_impl->tradeCallback = std::move(callback);
}

void JujinBrokerGateway::setErrorCallback(ErrorCallback callback)
{
    m_impl->errorCallback = std::move(callback);
}

std::string JujinBrokerGateway::lastError() const
{
    return m_impl->lastError_;
}

} // namespace adapters
} // namespace app
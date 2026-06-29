#include "SimulatedBrokerGateway.h"

#include <chrono>
#include <sstream>
#include <thread>

namespace app {
namespace adapters {

using namespace domain::trading;

class SimulatedBrokerGateway::Impl {
public:
    bool connected{false};
    std::string lastError_;
    int simulatedLatencyMs{5};
    BrokerCapability capability_;
    OrderCallback orderCallback;
    TradeCallback tradeCallback;
    ErrorCallback errorCallback;
    int nextOrderId{1};

    Impl()
    {
        capability_.setBrokerName(BrokerName("simulated"));
        capability_.setSupportsAlgo(false);
        capability_.setSupportsBasket(false);
        capability_.setSupportsConditional(false);
        capability_.setSupportsShortSelling(true);
        capability_.setSupportsFutures(false);
        capability_.setSupportsOptions(false);
    }

    BrokerOrderId generateBrokerOrderId()
    {
        std::ostringstream oss;
        oss << "sim-" << nextOrderId++;
        return BrokerOrderId(oss.str());
    }
};

SimulatedBrokerGateway::SimulatedBrokerGateway()
    : m_impl(std::make_unique<Impl>()) {}

SimulatedBrokerGateway::~SimulatedBrokerGateway()
{
    if (m_impl && m_impl->connected) {
        disconnect();
    }
}

bool SimulatedBrokerGateway::connect(const std::string& /*configJson*/)
{
    m_impl->connected = true;
    return true;
}

void SimulatedBrokerGateway::disconnect()
{
    m_impl->connected = false;
}

bool SimulatedBrokerGateway::isConnected() const
{
    return m_impl->connected;
}

BrokerCapability SimulatedBrokerGateway::capability() const
{
    return m_impl->capability_;
}

void SimulatedBrokerGateway::submitOrder(
    const OrderRequest& request,
    OrderCallback onResult)
{
    if (!m_impl->connected) {
        m_impl->lastError_ = "Not connected";
        if (m_impl->errorCallback) {
            m_impl->errorCallback(m_impl->lastError_);
        }
        return;
    }

    if (m_impl->simulatedLatencyMs > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_impl->simulatedLatencyMs));
    }

    OrderStatus status;
    status.setBrokerOrderId(m_impl->generateBrokerOrderId());
    status.setStatusValue(OrderStatusValue::New);
    status.setUpdateTime(foundation::utils::Timestamp::now());

    if (onResult) {
        onResult(status);
    }
}

void SimulatedBrokerGateway::cancelOrder(
    BrokerOrderId brokerOrderId,
    OrderCallback onResult)
{
    if (onResult) {
        OrderStatus status;
        status.setBrokerOrderId(brokerOrderId);
        status.setStatusValue(OrderStatusValue::Cancelled);
        onResult(status);
    }
}

void SimulatedBrokerGateway::queryOrder(
    BrokerOrderId /*brokerOrderId*/,
    OrderQueryCallback onResult)
{
    if (onResult) {
        onResult(std::nullopt);
    }
}

void SimulatedBrokerGateway::queryPositions(
    PositionsQueryCallback onResult)
{
    if (onResult) {
        onResult({});
    }
}

void SimulatedBrokerGateway::queryAccount(
    AccountQueryCallback onResult)
{
    if (onResult) {
        AccountInfo info;
        info.setTotalAssets(1000000.0);
        info.setAvailableCash(500000.0);
        info.setMarketValue(500000.0);
        onResult(info);
    }
}

void SimulatedBrokerGateway::setTradeCallback(TradeCallback callback)
{
    m_impl->tradeCallback = std::move(callback);
}

void SimulatedBrokerGateway::setErrorCallback(ErrorCallback callback)
{
    m_impl->errorCallback = std::move(callback);
}

std::string SimulatedBrokerGateway::lastError() const
{
    return m_impl->lastError_;
}

} // namespace adapters
} // namespace app
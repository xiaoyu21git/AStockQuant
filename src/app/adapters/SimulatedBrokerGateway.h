#pragma once

#include "IBrokerGateway.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace app {
namespace adapters {

using namespace domain::trading;

class SimulatedBrokerGateway : public IBrokerGateway {
public:
    SimulatedBrokerGateway();
    ~SimulatedBrokerGateway() override;

    bool connect(const std::string& configJson) override;
    void disconnect() override;
    bool isConnected() const override;
    BrokerCapability capability() const override;

    void submitOrder(const OrderRequest& request,
                     OrderCallback onResult) override;
    void cancelOrder(BrokerOrderId brokerOrderId,
                     OrderCallback onResult) override;

    void queryOrder(BrokerOrderId brokerOrderId,
                    OrderQueryCallback onResult) override;
    void queryPositions(PositionsQueryCallback onResult) override;
    void queryAccount(AccountQueryCallback onResult) override;

    void setTradeCallback(TradeCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;

    std::string lastError() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace adapters
} // namespace app
#pragma once

#include "IBrokerGateway.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace thirdparty {
class JujinApi;
struct ConfigParams;
} // namespace thirdparty

namespace app {
namespace adapters {

using namespace domain::trading;

class JujinBrokerGateway : public IBrokerGatewayEx {
public:
    JujinBrokerGateway();
    ~JujinBrokerGateway() override;

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

    void submitAlgoOrFail(const AlgoOrderRequest& request,
                          OrderCallback onResult) override;
    void submitAlgoOrder(const AlgoOrderRequest& request,
                         OrderCallback onResult) override;
    void submitBasket(const std::vector<OrderRequest>& orders,
                      OrderCallback onResult) override;
    void queryTrades(foundation::utils::Timestamp startDate,
                     foundation::utils::Timestamp endDate,
                     TradeCallback onResult) override;

    void setTradeCallback(TradeCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;

    std::string lastError() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace adapters
} // namespace app
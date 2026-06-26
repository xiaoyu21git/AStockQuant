#pragma once
// JujinBrokerGateway — 掘金券商网关，直连 TradeEngine

#include "../../domain/trading/IBrokerGateway.h"
#include <string>

namespace app::adapters {

class JujinBrokerGateway final : public domain::trading::IBrokerGatewayEx {
public:
    JujinBrokerGateway() = default;
    ~JujinBrokerGateway() override = default;

    bool connect(const std::string& configJson) override;
    void disconnect() override;
    bool isConnected() const override;
    domain::trading::BrokerCapability capability() const override;

    void submitOrder(const domain::trading::OrderRequest& request,
                     domain::trading::OrderCallback onResult) override;
    void cancelOrder(domain::trading::BrokerOrderId brokerOrderId,
                     domain::trading::OrderCallback onResult) override;
    void queryOrder(domain::trading::BrokerOrderId brokerOrderId,
                    domain::trading::OrderQueryCallback onResult) override;
    void queryPositions(domain::trading::PositionsQueryCallback onResult) override;
    void queryAccount(domain::trading::AccountQueryCallback onResult) override;

    void setTradeCallback(domain::trading::TradeCallback callback) override;
    void setErrorCallback(domain::trading::ErrorCallback callback) override;
    std::string lastError() const override;

    void submitAlgoOrFail(const domain::trading::AlgoOrderRequest& request,
                          domain::trading::OrderCallback onResult) override;
    void submitAlgoOrder(const domain::trading::AlgoOrderRequest& request,
                         domain::trading::OrderCallback onResult) override;
    void submitBasket(const std::vector<domain::trading::OrderRequest>& orders,
                      domain::trading::OrderCallback onResult) override;
    void queryTrades(foundation::utils::Timestamp startDate,
                     foundation::utils::Timestamp endDate,
                     domain::trading::TradeCallback onResult) override;

private:
    domain::trading::TradeCallback m_tradeCallback;
    domain::trading::ErrorCallback m_errorCallback;
    mutable std::string m_lastError;
};

} // namespace app::adapters

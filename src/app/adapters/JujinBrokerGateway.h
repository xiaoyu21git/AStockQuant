#pragma once
// JujinBrokerGateway — 掘金券商网关 (纯 C++, 零 Qt)
// 复用 JMC 已创建的 JujinApi（共享 GmStrategySession），不重复建 SDK 实例

#include "../../domain/trading/IBrokerGateway.h"

#include <string>

namespace thirdparty { class JujinApi; }

namespace app::adapters {

class JujinBrokerGateway final : public domain::trading::IBrokerGatewayEx {
public:
    JujinBrokerGateway();
    ~JujinBrokerGateway() override;

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
    thirdparty::JujinApi* m_api = nullptr;  // 共享实例，不持有所有权
    mutable std::string m_lastError;
};

} // namespace app::adapters

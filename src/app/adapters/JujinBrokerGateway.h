#pragma once
// JujinBrokerGateway — 掘金券商网关 (纯 C++, 零 Qt)
// 实现 IBrokerGatewayEx 接口, 内部使用 JujinSession 封装 SDK

#include "../../domain/trading/IBrokerGateway.h"
#include "JujinSession.h"

#include <memory>
#include <string>

namespace app::adapters {

class JujinBrokerGateway final : public domain::trading::IBrokerGatewayEx {
public:
    JujinBrokerGateway();
    ~JujinBrokerGateway() override;

    // ── IBrokerGateway ──
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

    // ── IBrokerGatewayEx ──
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
    std::unique_ptr<JujinSession> m_session;
    mutable std::string m_lastError;
};

} // namespace app::adapters

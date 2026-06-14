#pragma once

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"

#include <functional>
#include <string>

namespace astock::event {

struct OrderEventDto {
    std::string orderId;
    std::string clientOrderId;
    std::string brokerOrderId;
    std::string strategyId;
    std::string strategyName;
    std::string runtimeStrategyId;
    std::string symbol;
    std::string side;
    std::string status;
    std::string statusOrigin;
    std::string message;
    double price = 0.0;
    int64_t quantity = 0;
    int64_t filledQuantity = 0;
    double filledNotional = 0.0;
    double cashAmount = 0.0;
    std::string createdAt;
    std::string updatedAt;
};

class OrderEventSubscriber {
public:
    using OrderUpdateCallback = std::function<void(const OrderEventDto&)>;
    using TradeFillCallback = std::function<void(const OrderEventDto&)>;
    using RiskApprovalCallback = std::function<void(const OrderEventDto&)>;
    using RiskRejectCallback = std::function<void(const OrderEventDto&)>;

    explicit OrderEventSubscriber(engine::EventBus* bus);
    ~OrderEventSubscriber();

    void setOnOrderUpdate(OrderUpdateCallback cb) { m_onOrderUpdate = std::move(cb); }
    void setOnTradeFill(TradeFillCallback cb) { m_onTradeFill = std::move(cb); }
    void setOnRiskApproval(RiskApprovalCallback cb) { m_onRiskApproval = std::move(cb); }
    void setOnRiskReject(RiskRejectCallback cb) { m_onRiskReject = std::move(cb); }

private:
    void handleOrderUpdate(const engine::EventFormat& event);
    void handleTradeFill(const engine::EventFormat& event);
    void handleRiskApproval(const engine::EventFormat& event);
    void handleRiskReject(const engine::EventFormat& event);

    OrderEventDto fromEvent(const engine::EventFormat& event);

    engine::EventBus* m_bus = nullptr;
    foundation::utils::Uuid m_orderUpdateSub;
    foundation::utils::Uuid m_tradeFillSub;
    foundation::utils::Uuid m_riskApprovalSub;
    foundation::utils::Uuid m_riskRejectSub;

    OrderUpdateCallback m_onOrderUpdate;
    TradeFillCallback m_onTradeFill;
    RiskApprovalCallback m_onRiskApproval;
    RiskRejectCallback m_onRiskReject;
};

} // namespace astock::event
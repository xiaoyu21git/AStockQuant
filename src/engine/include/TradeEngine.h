// TradeEngine.h — 交易引擎（engine 层，零 Qt）
// 哑管道：接单 → gmsdk。不判断，不风控，不查账户，不管订单对账。
#pragma once

#include "GmSessionEngine.h"

namespace engine {

class TradeEngine {
public:
    static TradeEngine& instance();

    bool initialize(void* strategy);
    void shutdown();
    bool initialized() const;

    OrderResult submitOrder(const OrderRequest& req);
    bool        cancelOrder(const std::string& brokerOrderId);

    using OrderUpdateFn = std::function<void(const OrderUpdate&)>;
    using TradeFillFn   = std::function<void(const TradeFill&)>;
    void setOnOrderUpdate(OrderUpdateFn cb);
    void setOnTradeFill(TradeFillFn cb);

    void onOrderStatus(const OrderUpdate& u);
    void onTradeFill(const TradeFill& f);

private:
    TradeEngine() = default;
    ~TradeEngine() = default;

    void* m_strategy = nullptr;
    OrderUpdateFn m_onOrderUpdate;
    TradeFillFn   m_onTradeFill;
};

} // namespace engine

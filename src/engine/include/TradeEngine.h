// TradeEngine.h — 交易引擎（engine 层，零 Qt）
// 哑管道：接单 → gmsdk。不判断，不风控，不查账户，不管订单对账。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace engine {

struct OrderRequest {
    std::string symbol;
    std::string strategyId;
    double      price    = 0.0;
    int64_t     quantity = 0;
    enum Side   { Buy, Sell };
    enum Type   { Limit, Market };
    Side side      = Buy;
    Type orderType = Limit;
};

struct OrderResult {
    std::string brokerOrderId;
    bool        accepted = false;
    std::string message;
};

struct OrderUpdate {
    std::string brokerOrderId;
    std::string symbol;
    double      filledPrice    = 0.0;
    int64_t     filledQuantity = 0;
    enum Status { Submitted, PartialFilled, Filled, Cancelled, Rejected, Expired };
    Status      status = Submitted;
    std::string message;
};

struct TradeFill {
    std::string fillId, brokerOrderId, symbol;
    double      price = 0.0;
    int64_t     quantity = 0;
    double      commission = 0.0;
};

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

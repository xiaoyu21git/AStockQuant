#pragma once

#include "TradingTypes.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace domain {
namespace trading {

// ============================================================
// IBrokerGateway — 券商网关抽象接口 (纯 C++, 零 Qt)
//
// 所有方法异步执行, 结果通过回调返回。
// 仅主线程调用 (非线程安全)。
// ============================================================

class IBrokerGateway {
public:
    virtual ~IBrokerGateway() = default;

    // -- 生命周期 --
    virtual bool connect(const std::string& configJson) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual BrokerCapability capability() const = 0;

    // -- 订单操作 (异步, 结果通过回调返回) --
    virtual void submitOrder(const OrderRequest& request,
                             OrderCallback onResult) = 0;
    virtual void cancelOrder(BrokerOrderId brokerOrderId,
                             OrderCallback onResult) = 0;

    // -- 查询操作 (异步) --
    virtual void queryOrder(BrokerOrderId brokerOrderId,
                            OrderQueryCallback onResult) = 0;
    virtual void queryPositions(PositionsQueryCallback onResult) = 0;
    virtual void queryAccount(AccountQueryCallback onResult) = 0;

    // -- 回调注册 --
    virtual void setTradeCallback(TradeCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;

    // -- 错误 --
    virtual std::string lastError() const = 0;
};

// ============================================================
// IBrokerGatewayEx — 扩展接口 (券商特有功能)
// ============================================================

class IBrokerGatewayEx : public IBrokerGateway {
public:
    using IBrokerGateway::IBrokerGateway;

    // 算法订单 (刚性: 不支持则报错)
    virtual void submitAlgoOrFail(const AlgoOrderRequest& request,
                                  OrderCallback onResult) = 0;

    // 算法订单 (柔性: 不支持可降级)
    virtual void submitAlgoOrder(const AlgoOrderRequest& request,
                                 OrderCallback onResult) = 0;

    // 篮子委托 (保留接口, 具体实现按券商扩展)
    virtual void submitBasket(const std::vector<OrderRequest>& orders,
                              OrderCallback onResult) = 0;

    // 查询当日历史成交
    virtual void queryTrades(foundation::utils::Timestamp startDate,
                             foundation::utils::Timestamp endDate,
                             TradeCallback onResult) = 0;
};

} // namespace trading
} // namespace domain
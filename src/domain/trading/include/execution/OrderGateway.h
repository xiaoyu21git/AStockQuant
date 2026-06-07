#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "../../factor/include/factor_compute/FactorSignalTypes.h"

namespace domain::trading {

using InstrumentId = factor::compute::InstrumentId;

/// @brief 订单方向
enum class OrderSide : uint8_t {
    Buy = 0,
    Sell = 1,
};

/// @brief 订单类型
enum class OrderType : uint8_t {
    Limit = 0,
    Market = 1,
};

/// @brief 订单状态
enum class OrderStatus : uint8_t {
    Pending = 0,
    Submitted = 1,
    PartiallyFilled = 2,
    Filled = 3,
    Cancelled = 4,
    Rejected = 5,
};

/// @brief 委托单请求
struct OrderRequest final {
    InstrumentId instrument{};
    OrderSide side{OrderSide::Buy};
    int32_t quantity{0};
    double price{0.0};           ///< 限价，0 表示市价
    OrderType type{OrderType::Market};
    std::string clientOrderId;   ///< 客户端订单 ID（唯一标识）

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && quantity > 0 && !clientOrderId.empty();
    }
};

/// @brief 成交回报
struct FillReport final {
    std::string clientOrderId;
    std::string exchangeOrderId;     ///< 交易所订单 ID
    InstrumentId instrument{};
    OrderSide side{OrderSide::Buy};
    int32_t filledQuantity{0};
    double filledPrice{0.0};
    OrderStatus status{OrderStatus::Pending};
    std::chrono::system_clock::time_point timestamp;
};

/// @brief 交易网关抽象接口
///
/// 实盘路径：TradingUI -> StrategyEngine -> OrderGateway -> Broker API
/// 回测路径：使用 SimulatedOrderGateway 包装现有 IFillEngine
class IOrderGateway {
public:
    virtual ~IOrderGateway() = default;

    /// @brief 发送委托单
    /// @return 异步订单状态
    virtual std::future<OrderStatus> sendOrder(const OrderRequest& order) = 0;

    /// @brief 撤销委托单
    virtual std::future<bool> cancelOrder(const std::string& clientOrderId) = 0;

    /// @brief 查询订单状态
    virtual std::future<OrderStatus> queryOrder(const std::string& clientOrderId) = 0;

    /// @brief 批量发送
    virtual std::vector<std::future<OrderStatus>> sendBatch(
        const std::vector<OrderRequest>& orders) = 0;

    /// @brief 注册成交回调
    virtual void registerFillCallback(
        std::function<void(const FillReport&)> callback) = 0;
};

} // namespace domain::trading
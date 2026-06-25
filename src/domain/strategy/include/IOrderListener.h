#pragma once
// ═════════════════════════════════════════════════════════════════════════
// IOrderListener — 策略订单监听器接口 (最小编译依赖)
// ═════════════════════════════════════════════════════════════════════════

#include "StrategyServiceTypes.h"

#include <vector>

namespace domain::strategy {

/// @brief 实盘异步模式下，订单通过此回调通知上层，而非同步返回值。
class IOrderListener {
public:
    virtual ~IOrderListener() = default;

    /// @brief 当引擎后台线程处理完一批行情后调用。
    /// @param orders 本次步进产生的订单列表（可能为空）。
    virtual void onOrders(const std::vector<OrderRequest>& orders) = 0;
};

} // namespace domain::strategy

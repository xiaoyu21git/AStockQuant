#include "execution/OrderGateway.h"

#include <future>
#include <mutex>
#include <unordered_map>

namespace domain::trading {

/// @brief 模拟订单网关（用于回测场景兼容）
///
/// 将 IOrderGateway 委托路由到已有的 IFillEngine。
/// 所有订单立即成交（市价单），无延迟，无滑点。
class SimulatedOrderGateway final : public IOrderGateway {
public:
    SimulatedOrderGateway() = default;

    std::future<OrderStatus> sendOrder(const OrderRequest& order) override
    {
        std::promise<OrderStatus> promise;
        if (!order.isValid()) {
            promise.set_value(OrderStatus::Rejected);
        } else {
            promise.set_value(OrderStatus::Filled);
        }
        return promise.get_future();
    }

    std::future<bool> cancelOrder(const std::string& clientOrderId) override
    {
        std::promise<bool> promise;
        promise.set_value(true);
        return promise.get_future();
    }

    std::future<OrderStatus> queryOrder(const std::string& clientOrderId) override
    {
        std::promise<OrderStatus> promise;
        promise.set_value(OrderStatus::Filled);
        return promise.get_future();
    }

    std::vector<std::future<OrderStatus>> sendBatch(
        const std::vector<OrderRequest>& orders) override
    {
        std::vector<std::future<OrderStatus>> futures;
        futures.reserve(orders.size());
        for (const auto& order : orders) {
            std::promise<OrderStatus> promise;
            if (!order.isValid()) {
                promise.set_value(OrderStatus::Rejected);
            } else {
                promise.set_value(OrderStatus::Filled);
            }
            futures.push_back(promise.get_future());
        }
        return futures;
    }

    void registerFillCallback(
        std::function<void(const FillReport&)> callback) override
    {
        // 模拟网关不产生异步成交回调
        // 实盘对接时由 Broker API 回调触发
    }
};

} // namespace domain::trading
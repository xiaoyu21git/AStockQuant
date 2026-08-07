// OrderManager.h — 订单管理器（engine 层，零 Qt）
// 职责：订单查询、对账。不持有 Strategy，共享 GmSessionEngine 的连接。
#pragma once

#include "GmSessionEngine.h"
#include <unordered_map>

namespace engine {

class OrderManager {
public:
    static OrderManager& instance();

    bool initialize(::Strategy* strategy);
    void shutdown();
    bool initialized() const;

    // 查询（同步 gmsdk）
    std::vector<OrderRecord> queryOrders(const std::string& account = "");
    std::vector<OrderRecord> queryUnfinishedOrders(const std::string& account = "");

    // 同步：从 gmsdk 拉全量订单，返回状态变更的订单
    std::vector<OrderRecord> syncOrders(const std::string& account = "");

private:
    OrderManager() = default;
    ~OrderManager() = default;

    ::Strategy* m_strategy = nullptr;
    std::unordered_map<std::string, OrderRecord> m_orders; // brokerOrderId → order
};

} // namespace engine

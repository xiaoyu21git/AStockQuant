// OrderManager.h — 订单管理器（engine 层，零 Qt）
// 职责：订单查询、对账。不持有 Strategy，共享 GmSessionEngine 的连接。
#pragma once

#include "GmSessionEngine.h"

namespace engine {

class OrderManager {
public:
    static OrderManager& instance();

    bool initialize(void* strategy);
    void shutdown();
    bool initialized() const;

    std::vector<OrderRecord> queryOrders(const std::string& account = "") const;
    std::vector<OrderRecord> queryUnfinishedOrders(const std::string& account = "") const;

private:
    OrderManager() = default;
    ~OrderManager() = default;

    void* m_strategy = nullptr;
};

} // namespace engine

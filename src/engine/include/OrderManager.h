// OrderManager.h — 订单管理器（engine 层，零 Qt）
// 职责：订单查询、对账。不持有 Strategy，共享 GmSdkSingleton 的连接。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

struct OrderRecord {
    std::string brokerOrderId;
    std::string symbol;
    std::string strategyId;
    double      price      = 0.0;
    int64_t     quantity   = 0;
    int64_t     filledQty  = 0;
    double      filledVwap = 0.0;
    int         side       = 0;       // 1=buy, 2=sell
    int         status     = 0;       // gmsdk OrderStatus enum
    std::string statusText;
};

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

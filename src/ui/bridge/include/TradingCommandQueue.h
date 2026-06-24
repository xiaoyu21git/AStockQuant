#pragma once

#include "../../../engine/include/JujinApi.h"

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace thirdparty {

enum class TradingCommandType {
    Subscribe,
    Unsubscribe,
    PlaceOrder,
    CancelOrder,
    QueryOrders,
    QueryPositions,
    QueryAccount,
    AddTimer,
    StopTimer
};

struct TradingCommand {
    TradingCommandType type = TradingCommandType::QueryAccount;
    std::string correlation_id;
    std::string account_id;
    std::string strategy_id;
    std::string symbol;
    std::string frequency;
    std::string order_id;
    OrderSide side = OrderSide::BUY;
    OrderType order_type = OrderType::LIMIT;
    double price = 0.0;
    double quantity = 0.0;
    int timer_period_ms = 0;
    int timer_delay_ms = 0;
    int timer_id = 0;
    std::map<std::string, std::string> metadata;
    std::chrono::system_clock::time_point created_at = std::chrono::system_clock::now();
};

class TradingCommandQueue {
public:
    bool enqueue(const TradingCommand& command);
    bool try_dequeue(TradingCommand* command);
    std::vector<TradingCommand> dequeue_all(size_t max_count = 0);
    size_t size() const;
    bool empty() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::deque<TradingCommand> commands_;
};

} // namespace thirdparty

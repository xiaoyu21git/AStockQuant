#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace domain::trading {

// ---- 订单方向 ----
enum class OrderSide : uint8_t {
    Buy = 0,
    Sell = 1
};

inline OrderSide orderSideFromString(std::string_view s) {
    if (s == "BUY") return OrderSide::Buy;
    return OrderSide::Sell;
}

inline std::string_view orderSideToString(OrderSide side) {
    return side == OrderSide::Buy ? "BUY" : "SELL";
}

// ---- 订单状态 ----
enum class OrderStatus : uint8_t {
    Requested = 0,
    PendingRisk = 1,
    Submitted = 2,
    PartiallyFilled = 3,
    Filled = 4,
    Rejected = 5,
    Cancelled = 6
};

// ---- 执行决策 ----
enum class ExecutionDecision : uint8_t {
    Allow = 0,
    Block = 1,
    Warn = 2,
    Pause = 3
};

// ---- 订单类型 ----
enum class OrderType : uint8_t {
    Limit = 0,
    Market = 1
};

inline OrderType orderTypeFromString(std::string_view s) {
    if (s == "MARKET") return OrderType::Market;
    return OrderType::Limit;
}

// ---- 持仓方向 ----
enum class PositionSide : uint8_t {
    Long = 0,
    Short = 1
};

// ---- 风控审批结果 ----
enum class RiskReviewDecision : uint8_t {
    Approved = 0,
    Rejected = 1,
    Pending = 2
};

// ---- 策略生命周期状态 ----
enum class StrategyLifecycleState : uint8_t {
    Inactive = 0,
    Starting = 1,
    Running = 2,
    Stopping = 3,
    Error = 4
};

} // namespace domain::trading
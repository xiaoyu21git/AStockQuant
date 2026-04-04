// EventTopic.hpp
#pragma once

#include "Event/EventFormat.hpp"

#include <string_view>

namespace engine::topic {

// ======================= 系统事件 =======================
namespace system {
inline constexpr std::string_view STARTUP = EventTypes::SYSTEM_STARTUP;
inline constexpr std::string_view SHUTDOWN = EventTypes::SYSTEM_SHUTDOWN;
inline constexpr std::string_view SYSTEMERROR = EventTypes::SYSTEM_ERROR;
inline constexpr std::string_view WARNING = EventTypes::SYSTEM_WARNING;
inline constexpr std::string_view INFO = EventTypes::SYSTEM_INFO;
inline constexpr std::string_view HEARTBEAT = EventTypes::SYSTEM_HEARTBEAT;
}

// ======================= 市场事件 =======================
namespace market {
inline constexpr std::string_view TICK = EventTypes::MARKET_TICK;
inline constexpr std::string_view BAR = EventTypes::MARKET_BAR;
inline constexpr std::string_view SNAPSHOT = EventTypes::MARKET_SNAPSHOT;
inline constexpr std::string_view DEPTH = EventTypes::MARKET_DEPTH;
inline constexpr std::string_view TRADE = EventTypes::MARKET_TRADE;
inline constexpr std::string_view QUOTE = EventTypes::MARKET_QUOTE;
}

// ======================= 交易事件 =======================
namespace order {
inline constexpr std::string_view NEW = EventTypes::ORDER_NEW;
inline constexpr std::string_view FILL = EventTypes::ORDER_FILL;
inline constexpr std::string_view CANCEL = EventTypes::ORDER_CANCEL;
inline constexpr std::string_view REJECT = EventTypes::ORDER_REJECT;
}

// ======================= 策略事件 =======================
namespace strategy {
inline constexpr std::string_view SIGNAL_BUY   = "strategy.signal.buy";
inline constexpr std::string_view SIGNAL_SELL  = "strategy.signal.sell";
inline constexpr std::string_view POSITION = EventTypes::STRATEGY_POSITION;
inline constexpr std::string_view PNL = EventTypes::STRATEGY_PNL;
inline constexpr std::string_view PARAM_UPDATE = EventTypes::STRATEGY_PARAM;
inline constexpr std::string_view STATUS = EventTypes::STRATEGY_STATUS;
}

// ======================= 风控事件 =======================
namespace risk {
inline constexpr std::string_view WARNING = EventTypes::RISK_WARNING;
inline constexpr std::string_view LIMIT = EventTypes::RISK_LIMIT;
inline constexpr std::string_view BREACH = EventTypes::RISK_BREACH;
inline constexpr std::string_view ALERT = EventTypes::RISK_ALERT;
inline constexpr std::string_view CHECK = EventTypes::RISK_CHECK;
}

// ======================= 回测事件 =======================
namespace backtest {
inline constexpr std::string_view START = EventTypes::BACKTEST_START;
inline constexpr std::string_view RESULT = EventTypes::BACKTEST_RESULT;
inline constexpr std::string_view BACKTESTERROR = EventTypes::BACKTEST_ERROR;
inline constexpr std::string_view PROGRESS = EventTypes::BACKTEST_PROGRESS;
}

// ======================= 数据事件 =======================
namespace data {
inline constexpr std::string_view UPDATE = EventTypes::DATA_UPDATE;
inline constexpr std::string_view SYNC = EventTypes::DATA_SYNC;
inline constexpr std::string_view DATAERROR = EventTypes::DATA_ERROR;
}

// ======================= 性能事件 =======================
namespace performance {
inline constexpr std::string_view METRIC = EventTypes::PERFORMANCE_METRIC;
inline constexpr std::string_view ALERT = EventTypes::PERFORMANCE_ALERT;
}

// ======================= 用户自定义事件 =======================
namespace user {
inline constexpr std::string_view CUSTOM = "user.custom";
}

} // namespace engine::event::topic

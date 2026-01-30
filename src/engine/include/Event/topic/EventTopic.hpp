// EventTopic.hpp
#pragma once
#include <string_view>

namespace engine::topic {

// ======================= 系统事件 =======================
namespace system {
inline constexpr std::string_view STARTUP   = "system.startup";
inline constexpr std::string_view SHUTDOWN  = "system.shutdown";
inline constexpr std::string_view SYSTEMERROR     = "system.error";
inline constexpr std::string_view WARNING   = "system.warning";
inline constexpr std::string_view INFO      = "system.info";
inline constexpr std::string_view HEARTBEAT = "system.heartbeat";
}

// ======================= 市场事件 =======================
namespace market {
inline constexpr std::string_view TICK     = "market.tick";
inline constexpr std::string_view BAR      = "market.bar";
inline constexpr std::string_view SNAPSHOT = "market.snapshot";
inline constexpr std::string_view DEPTH    = "market.depth";
inline constexpr std::string_view TRADE    = "market.trade";
inline constexpr std::string_view QUOTE    = "market.quote";
}

// ======================= 交易事件 =======================
namespace order {
inline constexpr std::string_view NEW       = "order.new";
inline constexpr std::string_view FILL      = "order.fill";
inline constexpr std::string_view CANCEL    = "order.cancel";
inline constexpr std::string_view REJECT    = "order.reject";
inline constexpr std::string_view STATUS    = "order.status";
}

// ======================= 策略事件 =======================
namespace strategy {
inline constexpr std::string_view SIGNAL_BUY   = "strategy.signal.buy";
inline constexpr std::string_view SIGNAL_SELL  = "strategy.signal.sell";
inline constexpr std::string_view POSITION     = "strategy.position";
inline constexpr std::string_view PNL          = "strategy.pnl";
inline constexpr std::string_view PARAM_UPDATE = "strategy.param";
inline constexpr std::string_view STATUS       = "strategy.status";
}

// ======================= 风控事件 =======================
namespace risk {
inline constexpr std::string_view WARNING = "risk.warning";
inline constexpr std::string_view LIMIT   = "risk.limit";
inline constexpr std::string_view BREACH  = "risk.breach";
inline constexpr std::string_view ALERT   = "risk.alert";
inline constexpr std::string_view CHECK   = "risk.check";
}

// ======================= 回测事件 =======================
namespace backtest {
inline constexpr std::string_view START    = "backtest.start";
inline constexpr std::string_view RESULT   = "backtest.result";
inline constexpr std::string_view BACKTESTERROR    = "backtest.error";
inline constexpr std::string_view PROGRESS = "backtest.progress";
}

// ======================= 数据事件 =======================
namespace data {
inline constexpr std::string_view UPDATE = "data.update";
inline constexpr std::string_view SYNC   = "data.sync";
inline constexpr std::string_view DATAERROR  = "data.error";
}

// ======================= 性能事件 =======================
namespace performance {
inline constexpr std::string_view METRIC = "performance.metric";
inline constexpr std::string_view ALERT  = "performance.alert";
}

// ======================= 用户自定义事件 =======================
namespace user {
inline constexpr std::string_view CUSTOM = "user.custom";
}

} // namespace engine::event::topic

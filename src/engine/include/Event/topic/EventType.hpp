// EventType.hpp
#pragma once
#include <cstdint>

namespace engine::Event_Core {

// 事件大类（用于调度 / 队列分流 / 线程策略）
enum class Type : uint16_t {
    SYSTEM      = 0,     // 系统事件（启动、停止、错误、心跳）
    MARKETDATA  = 1,     // 市场行情事件（tick, bar, depth）
    TRADING     = 2,     // 交易/订单事件
    SIGNAL      = 3,     // 策略信号事件
    RISK        = 4,     // 风控事件
    ALERT       = 5,     // 可恢复告警
    WARNING     = 6,     // 非致命异常
    NEWS        = 7,     // 新闻、公告、外部信息
    BACKTEST    = 8,     // 回测事件
    DATA        = 9,     // 数据同步/更新事件
    PERFORMANCE = 10,    // 性能/监控事件
    USER        = 11,    //用户事件
    CUSTOM  = 1000   // 用户自定义事件
};
// 事件来源分类
enum class EventSource : uint8_t {
    SYSTEM      = 0,    // 系统事件
    MARKET_DATA = 1,    // 市场数据
    TRADING     = 2,    // 交易事件
    STRATEGY    = 3,    // 策略信号
    RISK        = 4,    // 风控事件
    DATABASE    = 5,    // 数据库事件
    NETWORK     = 6,    // 网络事件
    BACKTEST    = 7,    // 回测事件
    USER        = 8,     // 用户事件
    CUSTOM      = 1000  // 自定义
};

} // namespace engine::event
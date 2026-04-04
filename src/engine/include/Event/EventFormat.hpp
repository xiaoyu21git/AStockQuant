// astock_engine/core/EventFormat.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <chrono>
#include <variant>
#include <optional>
#include "Event.h"
#include "EventValue.h"
namespace engine {



// 事件优先级
enum class EventPriority : uint8_t {
    CRITICAL = 0,    // 系统关键事件（错误、异常）
    HIGH     = 1,    // 实时交易事件
    NORMAL   = 2,    // 普通市场数据
    LOW      = 3,    // 日志、监控事件
    BACKGROUND = 4   // 后台处理事件
};


// 统一事件格式
struct EventFormat {
    // 元数据
    std::string id;               // 事件唯一ID（UUID）
    std::string type;             // 事件类型标识
    Event_Core::EventSource source;           // 事件来源
    EventPriority priority;       // 事件优先级
    int64_t timestamp;           // 时间戳（微秒）
    int64_t created_at;          // 创建时间戳
    std::string correlation_id;   // 关联ID（用于追踪事件链）

    // 业务数据
    std::unordered_map<std::string, EventValue> data;

    // 扩展字段
    std::unordered_map<std::string, std::string> metadata;

    // 构造方法
    EventFormat() = default;

    explicit EventFormat(std::string event_type, Event_Core::EventSource src);
    // 扩展构造函数1：带时间戳
    EventFormat(std::string event_type, Event_Core::EventSource src, int64_t timestamp_us);
    
    // 扩展构造函数2：字符串 source + 时间戳
    EventFormat(std::string event_type, std::string source_str, int64_t timestamp_us = 0);
    
    // 工厂方法：从字符串创建（便于类型转换）
    static EventFormat create_from_strings(
        std::string event_type,
        std::string source_str,
        int64_t timestamp_us = 0);
    // 生成唯一ID
    void generate_id();
    
    // 设置数据（类型安全）
    template<typename T>
    void set(const std::string& key, T&& value);

// 获取数据（类型安全）
    template<typename T>
    std::optional<T> get(const std::string& key) const;
    
    // 检查是否包含键
    bool has(const std::string& key) const {
return data.find(key) != data.end();
    }
    
    // 移除数据
    void remove(const std::string& key) {
data.erase(key);
}

// 转换为JSON字符串
    std::string to_json() const;

    // 从JSON字符串解析
    static std::optional<EventFormat> from_json(const std::string& json_str);

// 转换为引擎内部格式
    engine::Event::Attributes to_attributes() const;

    // 转换为字符串表示（调试用）
    std::string to_string() const;

// 检查是否有数据
    bool has_data() const { return !data.empty(); }

    // 检查是否有元数据
    bool has_metadata() const { return !metadata.empty(); }

    // 静态工厂方法
    static EventFormat create_market_data(const std::string& symbol, 
                                         double price, 
                                         int64_t volume);
    
    static EventFormat create_order_event(const std::string& order_id,
                                         const std::string& symbol,
const std::string& side,
                                         double price,
                                         int64_t quantity);
        
    static EventFormat create_signal_event(const std::string& strategy_id,
                                              const std::string& symbol,
                                              const std::string& signal,
                                              double strength);

    static EventFormat create_system_event(const std::string& component,
                                          const std::string& message,
                                          EventPriority priority = EventPriority::NORMAL);
    
    static EventFormat create_risk_event(const std::string& rule_id,
                                        const std::string& description,
                                        double current_value,
                                        double limit_value);
};
// EventFormat.cpp 或 EventFormat.h

// 预定义的事件类型
namespace EventTypes {
    // 系统事件
    constexpr auto SYSTEM_STARTUP = "system.startup";
    constexpr auto SYSTEM_SHUTDOWN = "system.shutdown";
    constexpr auto SYSTEM_HEARTBEAT = "system.heartbeat";
    constexpr auto SYSTEM_ERROR = "system.error";
    constexpr auto SYSTEM_WARNING = "system.warning";
    constexpr auto SYSTEM_INFO = "system.info";
    
    // 市场数据事件
    constexpr auto MARKET_TICK = "market.tick";
    constexpr auto MARKET_BAR = "market.bar";
    constexpr auto MARKET_SNAPSHOT = "market.snapshot";
    constexpr auto MARKET_DEPTH = "market.depth";
    constexpr auto MARKET_TRADE = "market.trade";
    constexpr auto MARKET_QUOTE = "market.quote";
    
    // 交易事件
    constexpr auto ORDER_NEW = "order.new";
    constexpr auto ORDER_UPDATE = "order.update";
    constexpr auto ORDER_FILL = "order.fill";
    constexpr auto ORDER_CANCEL = "order.cancel";
    constexpr auto ORDER_REJECT = "order.reject";
    constexpr auto TRADING_ORDER_SUBMIT_REQUEST = "trading.order.submit.request";
    constexpr auto TRADING_ORDER_CANCEL_REQUEST = "trading.order.cancel.request";
    constexpr auto TRADING_MARKET_TICK = "trading.market.tick";
    constexpr auto TRADING_MARKET_BAR = "trading.market.bar";
    constexpr auto TRADING_ORDER_UPDATED = "trading.order.updated";
    constexpr auto TRADING_POSITION_UPDATED = "trading.position.updated";
    constexpr auto TRADING_ACCOUNT_UPDATED = "trading.account.updated";
    constexpr auto TRADING_EXECUTION_REPORT = "trading.execution.report";
    constexpr auto TRADING_SESSION_ERROR = "trading.session.error";
    constexpr auto TRADING_MARKET_CONNECTED = "trading.market.connected";
    constexpr auto TRADING_MARKET_DISCONNECTED = "trading.market.disconnected";
    constexpr auto MARKET_WATCH_ENSURE = "market.watch.ensure";
    
    // 策略事件
    constexpr auto STRATEGY_SIGNAL = "strategy.signal";
    constexpr auto STRATEGY_STATUS = "strategy.status";
    constexpr auto STRATEGY_POSITION = "strategy.position";
    constexpr auto STRATEGY_PNL = "strategy.pnl";
    constexpr auto STRATEGY_PARAM = "strategy.param";
    
    // 风控事件
    constexpr auto RISK_WARNING = "risk.warning";
    constexpr auto RISK_LIMIT = "risk.limit";
    constexpr auto RISK_BREACH = "risk.breach";
    constexpr auto RISK_CHECK = "risk.check";
    constexpr auto RISK_ALERT = "risk.alert";
    constexpr auto RISK_APPROVAL = "risk.approval";
    constexpr auto RISK_REJECT = "risk.reject";
    
    // 回测事件
    constexpr auto BACKTEST_START = "backtest.start";
    constexpr auto BACKTEST_RESULT = "backtest.result";
    constexpr auto BACKTEST_ERROR = "backtest.error";
    constexpr auto BACKTEST_PROGRESS = "backtest.progress";
    
    // 数据事件
    constexpr auto DATA_UPDATE = "data.update";
    constexpr auto DATA_SYNC = "data.sync";
    constexpr auto DATA_ERROR = "data.error";
    
    // 性能事件
    constexpr auto PERFORMANCE_METRIC = "performance.metric";
    constexpr auto PERFORMANCE_ALERT = "performance.alert";
}

// 工具函数
inline std::string event_source_to_string(Event_Core::EventSource source) {
    switch (source) {
        case Event_Core::EventSource::SYSTEM:      return "SYSTEM";
        case Event_Core::EventSource::MARKET_DATA: return "MARKET_DATA";
        case Event_Core::EventSource::TRADING:     return "TRADING";
        case Event_Core::EventSource::STRATEGY:    return "STRATEGY";
        case Event_Core::EventSource::RISK:        return "RISK";
        case Event_Core::EventSource::DATABASE:    return "DATABASE";
        case Event_Core::EventSource::NETWORK:     return "NETWORK";
        case Event_Core::EventSource::BACKTEST:    return "BACKTEST";
        case Event_Core::EventSource::USER:        return "USER";
        default:                       return "UNKNOWN";
    }
}

inline std::string event_priority_to_string(EventPriority priority) {
    switch (priority) {
        case EventPriority::CRITICAL:   return "CRITICAL";
        case EventPriority::HIGH:       return "HIGH";
        case EventPriority::NORMAL:     return "NORMAL";
        case EventPriority::LOW:        return "LOW";
        case EventPriority::BACKGROUND: return "BACKGROUND";
        default:                        return "UNKNOWN";
    }
}

inline EventPriority string_to_event_priority(const std::string& str) {
    if (str == "CRITICAL") return EventPriority::CRITICAL;
    if (str == "HIGH") return EventPriority::HIGH;
    if (str == "NORMAL") return EventPriority::NORMAL;
    if (str == "LOW") return EventPriority::LOW;
    if (str == "BACKGROUND") return EventPriority::BACKGROUND;
    return EventPriority::NORMAL;
}

inline Event_Core::EventSource string_to_event_source(const std::string& str) {
    if (str == "SYSTEM") return Event_Core::EventSource::SYSTEM;
    if (str == "MARKET_DATA") return Event_Core::EventSource::MARKET_DATA;
    if (str == "TRADING") return Event_Core::EventSource::TRADING;
    if (str == "STRATEGY") return Event_Core::EventSource::STRATEGY;
    if (str == "RISK") return Event_Core::EventSource::RISK;
    if (str == "DATABASE") return Event_Core::EventSource::DATABASE;
    if (str == "NETWORK") return Event_Core::EventSource::NETWORK;
    if (str == "BACKTEST") return Event_Core::EventSource::BACKTEST;
    if (str == "USER") return Event_Core::EventSource::USER;
    return Event_Core::EventSource::SYSTEM;
}

// ===== 模板方法实现 =====
template<typename T>
void EventFormat::set(const std::string& key, T&& value) {
    data[key] = EventValue(std::forward<T>(value));
}

template<typename T>
std::optional<T> EventFormat::get(const std::string& key) const {
    auto it = data.find(key);
    if (it == data.end()) {
        return std::nullopt;
    }
    return it->second.get<T>();
}

} // namespace engine
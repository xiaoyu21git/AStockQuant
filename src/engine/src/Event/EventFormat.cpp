// astock_engine/core/EventFormat.cpp
#include "EventFormat.h"
#include "foundation/json/json_facade.h"
#include "foundation/Utils/Uuid.h"
#include "foundation/Utils/Timestamp.h"
#include <sstream>
#include <iomanip>

namespace engine {

// ===== EventFormat 实现 =====

EventFormat::EventFormat(std::string event_type, EventSource src)
    : type(std::move(event_type))
    , source(src)
    , priority(EventPriority::NORMAL) {
    
    // 使用自定义时间戳
    timestamp = foundation::timestamp_now().microseconds();
    created_at = timestamp;
    
    // 生成唯一ID
    generate_id();
}

void EventFormat::generate_id() {
    // 使用自定义的 UUID 工具生成唯一ID
    id = foundation::create_uuid_v4().str();
}

std::string EventFormat::to_json() const {
    try {
        // 使用自定义的 JsonFacade
        auto json = foundation::json::JsonFacade::createObject();
        
        // 元数据
        json.set("id", foundation::json::JsonFacade::createString(id));
        json.set("type", foundation::json::JsonFacade::createString(type));
        json.set("source", foundation::json::JsonFacade::createInt(static_cast<int>(source)));
        json.set("priority", foundation::json::JsonFacade::createInt(static_cast<int>(priority)));
        json.set("timestamp", foundation::json::JsonFacade::createlong(timestamp));
        json.set("created_at", foundation::json::JsonFacade::createlong(created_at));
        
        if (!correlation_id.empty()) {
            json.set("correlation_id", foundation::json::JsonFacade::createString(correlation_id));
        }
        
        // 业务数据
        auto data_obj = foundation::json::JsonFacade::createObject();
        for (const auto& [key, value] : data) {
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    data_obj.set(key, foundation::json::JsonFacade::createString(arg));
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    data_obj.set(key, foundation::json::JsonFacade::createlong(arg));
                } else if constexpr (std::is_same_v<T, double>) {
                    data_obj.set(key, foundation::json::JsonFacade::createDouble(arg));
                } else if constexpr (std::is_same_v<T, bool>) {
                    data_obj.set(key, foundation::json::JsonFacade::createBool(arg));
                } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                    auto arr = foundation::json::JsonFacade::createArray();
                    for (const auto& item : arg) {
                        arr.push_back(foundation::json::JsonFacade::createString(item));
                    }
                    data_obj.set(key, arr);
                } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                    auto arr = foundation::json::JsonFacade::createArray();
                    for (const auto& item : arg) {
                        arr.push_back(foundation::json::JsonFacade::createDouble(item));
                    }
                    data_obj.set(key, arr);
                }
            }, value);
        }
        json.set("data", data_obj);
        
        // 元数据
        if (!metadata.empty()) {
            auto meta_obj = foundation::json::JsonFacade::createObject();
            for (const auto& [key, value] : metadata) {
                meta_obj.set(key, foundation::json::JsonFacade::createString(value));
            }
            json.set("metadata", meta_obj);
        }
        
        // 版本信息
        json.set("version", foundation::json::JsonFacade::createString("1.0.0"));
        json.set("format", foundation::json::JsonFacade::createString("EventFormat"));
        
        return json.toPrettyString();
        
    } catch (const std::exception& e) {
        // 返回错误信息
        std::string error = R"({"error": "Failed to serialize event to JSON", "message": ")";
        error += e.what();
        error += "\"}";
        return error;
    }
}

std::optional<EventFormat> EventFormat::from_json(const std::string& json_str) {
    try {
        // 使用自定义的 JsonFacade 解析
        auto json = foundation::json::JsonFacade::parse(json_str);
        
        if (json.empty() || !json.isObject()) {
            return std::nullopt;
        }
        
        EventFormat event;
        
        // 解析元数据
        if (json.has("id") && json.get("id").isString()) {
            event.id = json.get("id").asString();
        }
        
        if (json.has("type") && json.get("type").isString()) {
            event.type = json.get("type").asString();
        }
        
        if (json.has("source") && json.get("source").isNumber()) {
            int source_int = json.get("source").asInt();
            event.source = static_cast<EventSource>(source_int);
        }
        
        if (json.has("priority") && json.get("priority").isNumber()) {
            int priority_int = json.get("priority").asInt();
            event.priority = static_cast<EventPriority>(priority_int);
        }
        
        if (json.has("timestamp") && json.get("timestamp").isNumber()) {
            event.timestamp = json.get("timestamp").asInt();
        }
        
        if (json.has("created_at") && json.get("created_at").isNumber()) {
            event.created_at = json.get("created_at").asInt();
        }
        
        if (json.has("correlation_id") && json.get("correlation_id").isString()) {
            event.correlation_id = json.get("correlation_id").asString();
        }
        
        // 解析业务数据
        if (json.has("data") && json.get("data").isObject()) {
            auto data_obj = json.get("data");
            
            // 注意：这里假设我们知道可能的键名，或者JsonFacade提供了遍历方法
            // 在实际项目中，可能需要根据事件类型动态解析
            // 这里简化处理，只解析基本类型
            
        }
        
        // 解析元数据
        if (json.has("metadata") && json.get("metadata").isObject()) {
            auto meta_obj = json.get("metadata");
            // 类似处理
        }
        
        // 验证必要字段
        if (event.id.empty() || event.type.empty()) {
            return std::nullopt;
        }
        
        return event;
        
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

engine::Event::Attributes EventFormat::to_attributes() const {
    engine::Event::Attributes attrs;
    
    // 元数据
    attrs["event_id"] = id;
    attrs["event_type"] = type;
    attrs["event_source"] = event_source_to_string(source);
    attrs["event_priority"] = event_priority_to_string(priority);
    attrs["timestamp"] = std::to_string(timestamp);
    attrs["created_at"] = std::to_string(created_at);
    
    if (!correlation_id.empty()) {
        attrs["correlation_id"] = correlation_id;
    }
    
    // 序列化数据到JSON
    attrs["data_json"] = to_json();
    
    // 添加metadata
    for (const auto& [key, value] : metadata) {
        attrs["meta_" + key] = value;
    }
    
    // 添加原始数据字段（便于查询）
    for (const auto& [key, value] : data) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                attrs["data_" + key] = arg;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                attrs["data_" + key] = std::to_string(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                attrs["data_" + key] = std::to_string(arg);
            } else if constexpr (std::is_same_v<T, bool>) {
                attrs["data_" + key] = arg ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                std::ostringstream oss;
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << arg[i];
                }
                attrs["data_" + key] = oss.str();
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                std::ostringstream oss;
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << std::fixed << std::setprecision(6) << arg[i];
                }
                attrs["data_" + key] = oss.str();
            }
        }, value);
    }
    
    // 添加时间格式化字符串
    auto ts_time = std::chrono::microseconds(timestamp);
    auto ts_sys = std::chrono::system_clock::time_point(ts_time);
    auto ts_time_t = std::chrono::system_clock::to_time_t(ts_sys);
    std::tm tm_buf;
    localtime_r(&ts_time_t, &tm_buf);
    std::ostringstream time_oss;
    time_oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    attrs["timestamp_str"] = time_oss.str();
    
    return attrs;
}

std::string EventFormat::to_string() const {
    std::ostringstream oss;
    
    oss << "EventFormat{"
        << "id=" << id
        << ", type=" << type
        << ", source=" << event_source_to_string(source)
        << ", priority=" << event_priority_to_string(priority)
        << ", timestamp=" << timestamp;
    
    if (!correlation_id.empty()) {
        oss << ", correlation_id=" << correlation_id;
    }
    
    oss << ", data_fields=" << data.size()
        << ", metadata_fields=" << metadata.size()
        << "}";
    
    return oss.str();
}

// ===== 静态工厂方法实现 =====

EventFormat EventFormat::create_market_data(const std::string& symbol, 
                                           double price, 
                                           int64_t volume) {
    EventFormat event(EventTypes::MARKET_TICK, EventSource::MARKET_DATA);
    event.set("symbol", symbol);
    event.set("price", price);
    event.set("volume", volume);
    event.set("exchange", "DEFAULT");
    
    // 添加市场数据特定元数据
    event.metadata["data_type"] = "market_tick";
    event.metadata["asset_class"] = "equity";
    event.metadata["price_type"] = "last_trade";
    
    return event;
}

EventFormat EventFormat::create_order_event(const std::string& order_id,
                                           const std::string& symbol,
                                           const std::string& side,
                                           double price,
                                           int64_t quantity) {
    EventFormat event(EventTypes::ORDER_NEW, EventSource::TRADING);
    event.priority = EventPriority::HIGH;
    event.set("order_id", order_id);
    event.set("symbol", symbol);
    event.set("side", side);
    event.set("price", price);
    event.set("quantity", quantity);
    event.set("status", "NEW");
    event.set("order_type", "LIMIT");
    
    // 添加交易特定元数据
    event.metadata["order_venue"] = "exchange";
    event.metadata["time_in_force"] = "DAY";
    event.metadata["account_type"] = "simulation";
    
    return event;
}

EventFormat EventFormat::create_signal_event(const std::string& strategy_id,
                                            const std::string& symbol,
                                            const std::string& signal,
                                            double strength) {
    EventFormat event(EventTypes::STRATEGY_SIGNAL, EventSource::STRATEGY);
    event.set("strategy_id", strategy_id);
    event.set("symbol", symbol);
    event.set("signal", signal);
    event.set("strength", strength);
    event.set("confidence", 0.8);  // 默认置信度
    
    // 添加策略特定元数据
    event.metadata["signal_type"] = "alpha";
    event.metadata["time_horizon"] = "short_term";
    event.metadata["generated_by"] = "strategy_engine";
    
    return event;
}

EventFormat EventFormat::create_system_event(const std::string& component,
                                            const std::string& message,
                                            EventPriority priority) {
    EventFormat event(EventTypes::SYSTEM_ERROR, EventSource::SYSTEM);
    event.priority = priority;
    event.set("component", component);
    event.set("message", message);
    
    std::string level;
    switch (priority) {
        case EventPriority::CRITICAL: level = "CRITICAL"; break;
        case EventPriority::HIGH: level = "HIGH"; break;
        case EventPriority::NORMAL: level = "NORMAL"; break;
        case EventPriority::LOW: level = "LOW"; break;
        case EventPriority::BACKGROUND: level = "BACKGROUND"; break;
        default: level = "UNKNOWN";
    }
    event.set("level", level);
    
    event.metadata["system_component"] = component;
    event.metadata["error_category"] = "system";
    
    return event;
}

EventFormat EventFormat::create_risk_event(const std::string& rule_id,
                                          const std::string& description,
                                          double current_value,
                                          double limit_value) {
    EventFormat event(EventTypes::RISK_WARNING, EventSource::RISK);
    event.priority = EventPriority::HIGH;
    event.set("rule_id", rule_id);
    event.set("description", description);
    event.set("current_value", current_value);
    event.set("limit_value", limit_value);
    
    double breach_percentage = (current_value / limit_value) * 100.0;
    event.set("breach_percentage", breach_percentage);
    
    event.metadata["risk_category"] = "position_limit";
    event.metadata["severity"] = current_value > limit_value ? "BREACH" : "WARNING";
    event.metadata["check_type"] = "threshold";
    
    return event;
}

// ===== 模板方法显式实例化 =====

template void EventFormat::set<std::string>(const std::string&, std::string&&);
template void EventFormat::set<int64_t>(const std::string&, int64_t&&);
template void EventFormat::set<double>(const std::string&, double&&);
template void EventFormat::set<bool>(const std::string&, bool&&);
template void EventFormat::set<std::vector<std::string>>(const std::string&, std::vector<std::string>&&);
template void EventFormat::set<std::vector<double>>(const std::string&, std::vector<double>&&);

template std::optional<std::string> EventFormat::get<std::string>(const std::string&) const;
template std::optional<int64_t> EventFormat::get<int64_t>(const std::string&) const;
template std::optional<double> EventFormat::get<double>(const std::string&) const;
template std::optional<bool> EventFormat::get<bool>(const std::string&) const;
template std::optional<std::vector<std::string>> EventFormat::get<std::vector<std::string>>(const std::string&) const;
template std::optional<std::vector<double>> EventFormat::get<std::vector<double>>(const std::string&) const;

} // namespace engine
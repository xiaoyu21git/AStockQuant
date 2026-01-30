// astock_engine/core/EventSubscriber.h
#pragma once

#include <vector>
#include <functional>
#include <memory>
#include "Event.h"
#include "foundation.h"
#include "EventFormat.hpp"
namespace engine {

// ===== 原始 Event 订阅者 =====

class EventSubscriber {
private:
    foundation::Uuid id_;
    std::function<void(std::unique_ptr<Event>)> callback_;
    std::vector<Event_Core::Type> event_types_;
    std::chrono::steady_clock::time_point created_time_;
    
public:
    EventSubscriber(std::function<void(std::unique_ptr<Event>)> cb, 
                   std::vector<Event_Core::Type> types)
        : id_(foundation::Uuid::generate())
        , callback_(std::move(cb))
        , event_types_(std::move(types))
        , created_time_(std::chrono::steady_clock::now()) {}
    
    foundation::Uuid id() const { return id_; }
    const std::vector<Event_Core::Type>& event_types() const { return event_types_; }
    std::chrono::steady_clock::time_point created_time() const { return created_time_; }
    
    void notify(std::unique_ptr<Event> evt) {
        if (callback_) {
            callback_(std::move(evt));
        }
    }
    
    bool is_interested(Event_Core::Type type) const {
        return std::find(event_types_.begin(), event_types_.end(), type) != event_types_.end();
    }
};

// ===== EventFormat 订阅者 =====

class EventFormatSubscriber {
private:
    foundation::Uuid id_;
    std::string event_type_;
    std::function<void(const engine::EventFormat&)> callback_;
    std::function<bool(const engine::EventFormat&)> filter_;
    int priority_;
    std::chrono::steady_clock::time_point created_time_;
    
public:
    EventFormatSubscriber(std::string event_type,
                         std::function<void(const engine::EventFormat&)> callback,
                         std::function<bool(const engine::EventFormat&)> filter = nullptr,
                         int priority = 0)
        : id_(foundation::Uuid::generate())
        , event_type_(std::move(event_type))
        , callback_(std::move(callback))
        , filter_(std::move(filter))
        , priority_(priority)
        , created_time_(std::chrono::steady_clock::now()) {}
    
    foundation::Uuid id() const { return id_; }
    const std::string& event_type() const { return event_type_; }
    int priority() const { return priority_; }
    std::chrono::steady_clock::time_point created_time() const { return created_time_; }
    
    void notify(const engine::EventFormat& event) {
        // 应用过滤器
        if (filter_ && !filter_(event)) {
            return;
        }
        
        if (callback_) {
            callback_(event);
        }
    }
    
    bool should_notify(const engine::EventFormat& event) const {
        if (event.type != event_type_) {
            return false;
        }
        
        if (filter_) {
            return filter_(event);
        }
        
        return true;
    }
    
    void update_priority(int new_priority) {
        priority_ = new_priority;
    }
    
    void update_filter(std::function<bool(const engine::EventFormat&)> new_filter) {
        filter_ = std::move(new_filter);
    }
};

// ===== 通用订阅者包装器（支持两种类型） =====

class UniversalSubscriber {
private:
    foundation::Uuid id_;
    
    // 原始 Event 订阅
    std::function<void(std::unique_ptr<Event>)> engine_callback_;
    std::vector<Event_Core::Type> engine_event_types_;
    
    // EventFormat 订阅
    std::function<void(const engine::EventFormat&)> format_callback_;
    std::vector<std::string> format_event_types_;
    std::function<bool(const engine::EventFormat&)> format_filter_;
    
    int priority_;
    std::chrono::steady_clock::time_point created_time_;
    
public:
    UniversalSubscriber()
        : id_(foundation::Uuid::generate())
        , priority_(0)
        , created_time_(std::chrono::steady_clock::now()) {}
    
    foundation::Uuid id() const { return id_; }
    int priority() const { return priority_; }
    std::chrono::steady_clock::time_point created_time() const { return created_time_; }
    
    // ===== 原始 Event 配置 =====
    
    UniversalSubscriber& set_engine_callback(
        std::function<void(std::unique_ptr<Event>)> callback,
        std::vector<Event_Core::Type> event_types) {
        
        engine_callback_ = std::move(callback);
        engine_event_types_ = std::move(event_types);
        return *this;
    }
    
    const std::vector<Event_Core::Type>& engine_event_types() const {
        return engine_event_types_;
    }
    
    void notify_engine(std::unique_ptr<Event> evt) {
        if (engine_callback_) {
            engine_callback_(std::move(evt));
        }
    }
    
    bool is_interested_in_engine(Event_Core::Type type) const {
        return std::find(engine_event_types_.begin(), 
                        engine_event_types_.end(), type) != engine_event_types_.end();
    }
    
    // ===== EventFormat 配置 =====
    
    UniversalSubscriber& set_format_callback(
        std::function<void(const engine::EventFormat&)> callback,
        std::vector<std::string> event_types,
        std::function<bool(const engine::EventFormat&)> filter = nullptr) {
        
        format_callback_ = std::move(callback);
        format_event_types_ = std::move(event_types);
        format_filter_ = std::move(filter);
        return *this;
    }
    
    const std::vector<std::string>& format_event_types() const {
        return format_event_types_;
    }
    
    void notify_format(const engine::EventFormat& event) {
        // 检查事件类型
        if (std::find(format_event_types_.begin(), 
                     format_event_types_.end(), 
                     event.type) == format_event_types_.end()) {
            return;
        }
        
        // 应用过滤器
        if (format_filter_ && !format_filter_(event)) {
            return;
        }
        
        if (format_callback_) {
            format_callback_(event);
        }
    }
    
    bool is_interested_in_format(const std::string& event_type) const {
        return std::find(format_event_types_.begin(),
                        format_event_types_.end(),
                        event_type) != format_event_types_.end();
    }
    
    bool should_notify_format(const engine::EventFormat& event) const {
        if (!is_interested_in_format(event.type)) {
            return false;
        }
        
        if (format_filter_) {
            return format_filter_(event);
        }
        
        return true;
    }
    
    // ===== 通用配置 =====
    
    UniversalSubscriber& set_priority(int priority) {
        priority_ = priority;
        return *this;
    }
    
    UniversalSubscriber& add_engine_event_type(Event_Core::Type type) {
        engine_event_types_.push_back(type);
        return *this;
    }
    
    UniversalSubscriber& add_format_event_type(const std::string& event_type) {
        format_event_types_.push_back(event_type);
        return *this;
    }
    
    void clear_engine_subscriptions() {
        engine_event_types_.clear();
        engine_callback_ = nullptr;
    }
    
    void clear_format_subscriptions() {
        format_event_types_.clear();
        format_callback_ = nullptr;
        format_filter_ = nullptr;
    }
};

} // namespace engine
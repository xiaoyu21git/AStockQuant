// astock_engine/core/SubscriptionManager.h
#pragma once

#include "EventSubscriber.h"
#include "foundation.h"
#include "EventFormat.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <string>

namespace engine {

class SubscriptionManager {
private:
    mutable std::shared_mutex mutex_;
    
    // 原始 Event 订阅
    std::unordered_map<Event_Core::Type, 
        std::vector<std::shared_ptr<EventSubscriber>>> subs_map_;
    
    // EventFormat 订阅
    using FormatHandler = std::function<void(const engine::EventFormat&)>;
    using FormatFilter = std::function<bool(const engine::EventFormat&)>;
    struct FormatSubscription {
        foundation::Uuid  id;
        std::string event_type;
        FormatHandler handler;
        FormatFilter filter;
        int priority;
        std::chrono::steady_clock::time_point created_time;

        FormatSubscription(
            foundation::Uuid id_,
            std::string event_type_,
            FormatHandler handler_,
            FormatFilter filter_,
            int priority_
        )
        : id(id_)
        , event_type(event_type_)
        , handler(std::move(handler_))
        , filter(std::move(filter_))
        , priority(priority_)
        , created_time(std::chrono::steady_clock::now())
        {}
    };
    std::unordered_map<std::string, std::vector<FormatSubscription>> format_subs_map_;
    std::unordered_map<foundation::Uuid, std::string> format_sub_to_type_;
    
public:
    SubscriptionManager() = default;
    
    // 禁止拷贝
    SubscriptionManager(const SubscriptionManager&) = delete;
    SubscriptionManager& operator=(const SubscriptionManager&) = delete;
    
    // 允许移动
    SubscriptionManager(SubscriptionManager&&) noexcept = default;
    SubscriptionManager& operator=(SubscriptionManager&&) noexcept = default;
    
    // ===== 原始 Event 订阅管理 =====
    
    foundation::Uuid add_subscriber(std::shared_ptr<EventSubscriber> sub) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        for (auto type : sub->event_types()) {
            subs_map_[type].push_back(sub);
        }
        
        return sub->id();
    }
    
    bool remove_subscriber(foundation::Uuid id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        bool removed = false;
        for (auto& [type, vec] : subs_map_) {
            auto it = std::remove_if(vec.begin(), vec.end(),
                [&id](const std::shared_ptr<EventSubscriber>& s) { 
                    return s->id() == id; 
                });
            
            if (it != vec.end()) {
                vec.erase(it, vec.end());
                removed = true;
            }
        }
        
        // 清理空条目
        for (auto it = subs_map_.begin(); it != subs_map_.end();) {
            if (it->second.empty()) {
                it = subs_map_.erase(it);
            } else {
                ++it;
            }
        }
        
        return removed;
    }
    
    std::vector<std::shared_ptr<EventSubscriber>> get_subscribers(
        Event_Core::Type type) const {
        
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = subs_map_.find(type);
        if (it != subs_map_.end()) {
            return it->second;
        }
        return {};
    }
    
    // ===== EventFormat 订阅管理 =====
    
    foundation::Uuid add_format_subscriber(
        const std::string& event_type,
        FormatHandler handler,
        FormatFilter filter = nullptr,
        int priority = 0) {
        
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        foundation::Uuid id = foundation::Uuid::generate();
        
        format_subs_map_[event_type].push_back( FormatSubscription{id, event_type, std::move(handler), std::move(filter), priority});
        
        format_sub_to_type_[id] = event_type;
        
        return id;
    }
    
    bool remove_format_subscriber(foundation::Uuid id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto type_it = format_sub_to_type_.find(id);
        if (type_it == format_sub_to_type_.end()) {
            return false;
        }
        
        const std::string& event_type = type_it->second;
        auto subs_it = format_subs_map_.find(event_type);
        if (subs_it == format_subs_map_.end()) {
            return false;
        }
        
        auto& subscriptions = subs_it->second;
        auto sub_it = std::remove_if(subscriptions.begin(), subscriptions.end(),
            [id](const FormatSubscription& sub) {
                return sub.id == id;
            });
        
        bool removed = (sub_it != subscriptions.end());
        if (removed) {
            subscriptions.erase(sub_it, subscriptions.end());
            format_sub_to_type_.erase(type_it);
            
            // 清理空条目
            if (subscriptions.empty()) {
                format_subs_map_.erase(subs_it);
            }
        }
        
        return removed;
    }
    
    std::vector<FormatSubscription> get_format_subscribers(
        const std::string& event_type) const {
        
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = format_subs_map_.find(event_type);
        if (it != format_subs_map_.end()) {
            return it->second;
        }
        return {};
    }
    
    // ===== 批量操作 =====
    
    void notify_format_subscribers(const engine::EventFormat& event) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        auto it = format_subs_map_.find(event.type);
        if (it == format_subs_map_.end()) {
            return;
        }
        
        for (const auto& subscription : it->second) {
            // 应用过滤器
            if (subscription.filter && !subscription.filter(event)) {
                continue;
            }
            
            if (subscription.handler) {
                try {
                    subscription.handler(event);
                } catch (...) {
                    // 记录错误但继续处理
                }
            }
        }
    }
    
    // ===== 查询统计 =====
    
    size_t get_subscriber_count(Event_Core::Type type) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = subs_map_.find(type);
        if (it != subs_map_.end()) {
            return it->second.size();
        }
        return 0;
    }
    
    size_t get_format_subscriber_count(const std::string& event_type) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = format_subs_map_.find(event_type);
        if (it != format_subs_map_.end()) {
            return it->second.size();
        }
        return 0;
    }
    
    size_t get_total_subscriber_count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        size_t count = 0;
        for (const auto& [type, subscribers] : subs_map_) {
            count += subscribers.size();
        }
        return count;
    }
    
    size_t get_total_format_subscriber_count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        size_t count = 0;
        for (const auto& [event_type, subscriptions] : format_subs_map_) {
            count += subscriptions.size();
        }
        return count;
    }
    
    std::vector<Event_Core::Type> get_subscribed_event_types() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<Event_Core::Type> types;
        types.reserve(subs_map_.size());
        
        for (const auto& [type, _] : subs_map_) {
            types.push_back(type);
        }
        
        return types;
    }
    
    std::vector<std::string> get_subscribed_format_types() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::string> types;
        types.reserve(format_subs_map_.size());
        
        for (const auto& [type, _] : format_subs_map_) {
            types.push_back(type);
        }
        
        return types;
    }
    
    bool has_subscribers(Event_Core::Type type) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = subs_map_.find(type);
        return it != subs_map_.end() && !it->second.empty();
    }
    
    bool has_format_subscribers(const std::string& event_type) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = format_subs_map_.find(event_type);
        return it != format_subs_map_.end() && !it->second.empty();
    }
    
    // ===== 清理 =====
    
    void clear_all() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        subs_map_.clear();
        format_subs_map_.clear();
        format_sub_to_type_.clear();
    }
    
    void clear_format_subscriptions() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        format_subs_map_.clear();
        format_sub_to_type_.clear();
    }
    
    void clear_format_subscriptions(const std::string& event_type) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto it = format_subs_map_.find(event_type);
        if (it != format_subs_map_.end()) {
            // 从反向映射中移除
            for (const auto& subscription : it->second) {
                format_sub_to_type_.erase(subscription.id);
            }
            
            format_subs_map_.erase(it);
        }
    }
    
    // ===== 订阅超时管理 =====
    
    void cleanup_idle_subscriptions(
        std::chrono::seconds max_age = std::chrono::hours(24)) {
        
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        
        for (auto it = format_subs_map_.begin(); it != format_subs_map_.end();) {
            auto& subscriptions = it->second;
            
            // 移除超时的订阅
            auto sub_it = std::remove_if(subscriptions.begin(), subscriptions.end(),
                [now, max_age](const FormatSubscription& sub) {
                    return (now - sub.created_time) > max_age;
                });
            
            // 从反向映射中移除
            for (auto rm_it = sub_it; rm_it != subscriptions.end(); ++rm_it) {
                format_sub_to_type_.erase(rm_it->id);
            }
            
            // 删除超时的订阅
            if (sub_it != subscriptions.end()) {
                subscriptions.erase(sub_it, subscriptions.end());
            }
            
            // 如果该类型没有订阅者了，清理条目
            if (subscriptions.empty()) {
                it = format_subs_map_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // ===== 优先级排序 =====
    
    void sort_format_subscriptions_by_priority() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        for (auto& [event_type, subscriptions] : format_subs_map_) {
            std::sort(subscriptions.begin(), subscriptions.end(),
                [](const FormatSubscription& a, const FormatSubscription& b) {
                    return a.priority > b.priority; // 优先级高的在前
                });
        }
    }
};

} // namespace engine
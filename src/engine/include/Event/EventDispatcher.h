// astock_engine/core/EventDispatcher.h
#pragma once

#include "Event.h"
#include "EventSubscriber.h"
#include "SubscriptionManager.h"
#include "foundation/thread/IExecutor.h"
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <future>

namespace engine {

class IEventDispatcher {
public:
    virtual ~IEventDispatcher() = default;
    
    // ===== 原始 Event 分发 =====
    virtual void dispatch(std::unique_ptr<Event> event, 
                         const SubscriptionManager& subs_mgr) = 0;
    virtual void dispatch(const std::vector<std::unique_ptr<Event>>& events,
                         const SubscriptionManager& subs_mgr) = 0;
    
    // ===== EventFormat 分发 =====
    virtual void register_format_subscriber(
        const std::string& event_type,
        std::function<void(const engine::EventFormat&)> subscriber) = 0;
    
    virtual bool unregister_format_subscriber(
        const std::string& event_type,
        const std::function<void(const engine::EventFormat&)>& subscriber) = 0;
    
    virtual void dispatch(const engine::EventFormat& event) = 0;
    virtual void dispatch(const std::vector<engine::EventFormat>& events) = 0;
    
    // ===== 控制接口 =====
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;
    virtual void wait_for_completion() = 0;
};

// ===== 同步事件分发器 =====

class SyncEventDispatcher : public IEventDispatcher {
private:
    // EventFormat 订阅者
    using FormatHandler = std::function<void(const engine::EventFormat&)>;
    std::unordered_map<std::string, std::vector<FormatHandler>> format_subscribers_;
    mutable std::mutex format_subscribers_mutex_;
    
public:
    SyncEventDispatcher() = default;
    ~SyncEventDispatcher() override = default;
    
    // ===== 原始 Event 分发（同步）=====
    void dispatch(std::unique_ptr<Event> event, 
                 const SubscriptionManager& subs_mgr) override {
        if (!event) return;
        
        auto subscribers = subs_mgr.get_subscribers(event->type());
        for (const auto& subscriber : subscribers) {
            subscriber->notify(event->clone());
        }
    }
    
    void dispatch(const std::vector<std::unique_ptr<Event>>& events,
                 const SubscriptionManager& subs_mgr) override {
        for (const auto& event : events) {
            if (!event) continue;
            dispatch(event->clone(), subs_mgr);
        }
    }
    
    // ===== EventFormat 分发（同步）=====
    void register_format_subscriber(
        const std::string& event_type,
        std::function<void(const engine::EventFormat&)> subscriber) override {
        
        std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
        format_subscribers_[event_type].push_back(std::move(subscriber));
    }
    bool unregister_format_subscriber(
        const std::string& event_type,
        const std::function<void(const engine::EventFormat&)>& subscriber) override { 
        std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
        auto it = format_subscribers_.find(event_type);
        if (it == format_subscribers_.end()) return false;
        auto& handlers = it->second;
        // 修复：使用 remove_if 而不是 remove
        auto original_size = handlers.size();
        // 获取订阅者的目标函数指针
        auto subscriber_target = subscriber.target<void(const engine::EventFormat&)>();
        if (!subscriber_target) {
            // 如果无法获取目标，尝试使用 lambda 捕获的地址
            return false;
        }
        handlers.erase(
            std::remove_if(handlers.begin(), handlers.end(),
                [subscriber_target](const std::function<void(const engine::EventFormat&)>& handler) {
                    auto handler_target = handler.target<void(const engine::EventFormat&)>();
                    return handler_target && handler_target == subscriber_target;
                }),
            handlers.end()
        );
        bool removed = (handlers.size() < original_size); 
        if (removed && handlers.empty()) {
            format_subscribers_.erase(it);
        }
        return removed;
    }
    void dispatch(const engine::EventFormat& event) override {
        // ✅ 修复：复制处理器列表后再执行，避免长期持锁
        std::vector<FormatHandler> handlers_copy;
        {
            std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
            auto it = format_subscribers_.find(event.type);
            if (it == format_subscribers_.end()) return;
            handlers_copy = it->second;
        }
        
        // 执行处理器时不持锁
        for (const auto& handler : handlers_copy) {
            handler(event);
        }
    }
    
    void dispatch(const std::vector<engine::EventFormat>& events) override {
        for (const auto& event : events) {
            dispatch(event);
        }
    }
    
    // ===== 控制接口 =====
    void start() override {}
    void stop() override {}
    bool is_running() const override { return true; }
    void wait_for_completion() override {}
};

// ===== 异步事件分发器 =====

class AsyncEventDispatcher : public IEventDispatcher {
private:
    std::shared_ptr<foundation::thread::IExecutor> executor_;
    std::atomic<bool> running_{false};
    
    // EventFormat 订阅者
    using FormatHandler = std::function<void(const engine::EventFormat&)>;
    std::unordered_map<std::string, std::vector<FormatHandler>> format_subscribers_;
    mutable std::mutex format_subscribers_mutex_;
    
public:
    explicit AsyncEventDispatcher(std::shared_ptr<foundation::thread::IExecutor> executor)
        : executor_(std::move(executor)) {}
    
    ~AsyncEventDispatcher() override {
        stop();
    }
    void dispatch(std::unique_ptr<Event> event,
              const SubscriptionManager& subs_mgr) override {
    if (!event || !running_) return;

    auto subscribers = subs_mgr.get_subscribers(event->type());
    if (subscribers.empty()) return;

    for (const auto& subscriber : subscribers) {
        if (!subscriber) continue;

        if (!executor_) {
            // ===== 同步：直接 clone =====
            auto evt = event->clone();
            subscriber->notify(std::move(evt));
        } else {
            // ===== 异步：clone 放到 lambda 内 =====
            Event* raw = event.get(); // ⚠ 只用来 clone，不存储

            executor_->post([subscriber, raw]() {
                auto evt = raw->clone();   // 每次独立 clone
                subscriber->notify(std::move(evt));
            });
        }
    }
}


    
    void dispatch(const std::vector<std::unique_ptr<Event>>& events,
                 const SubscriptionManager& subs_mgr) override {
        if (!running_) return;
        
        for (const auto& event : events) {
            if (!event) continue;
            dispatch(event->clone(), subs_mgr);
        }
    }
    
    // ===== EventFormat 分发（异步）=====
    void register_format_subscriber(
        const std::string& event_type,
        std::function<void(const engine::EventFormat&)> subscriber) override {
        
        std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
        format_subscribers_[event_type].push_back(std::move(subscriber));
    }
    
    bool unregister_format_subscriber(
        const std::string& event_type,
        const std::function<void(const engine::EventFormat&)>& subscriber) override {
        std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
        auto it = format_subscribers_.find(event_type);
        if (it == format_subscribers_.end()) return false;
        auto& handlers = it->second;
        // ==== 修改开始 ====
        // 获取订阅者的目标函数指针
        using FuncType = void(const engine::EventFormat&);
        auto* subscriber_target = subscriber.target<FuncType>();
    
        if (!subscriber_target) {
            // 对于lambda等，无法获取目标地址
            return false;
        }
        // 记录原始大小
        size_t original_size = handlers.size();
        // 使用 remove_if 替代 remove
        handlers.erase(
            std::remove_if(handlers.begin(), handlers.end(),
                [subscriber_target](const std::function<void(const engine::EventFormat&)>& handler) {
                    auto* handler_target = handler.target<FuncType>();
                    return handler_target && handler_target == subscriber_target;
                }),
            handlers.end()
        );
        bool removed = (handlers.size() < original_size);
        // ==== 修改结束 ====
        if (removed && handlers.empty()) {
            format_subscribers_.erase(it);
        }
        return removed;
    }
    
    void dispatch(const engine::EventFormat& event) override {
        if (!running_) return;
        
        // ✅ 修复：先复制处理器列表，再执行（避免死锁和竞态）
        std::vector<FormatHandler> handlers_copy;
        {
            std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
            auto it = format_subscribers_.find(event.type);
            if (it == format_subscribers_.end()) return;
            handlers_copy = it->second;
        }
        
        if (!executor_) {
            // 如果没有执行器，退化为同步处理
            for (const auto& handler : handlers_copy) {
                handler(event);
            }
        } else {
            // 异步执行所有处理器
            for (const auto& handler : handlers_copy) {
                executor_->post([handler, event]() {
                    handler(event);
                });
            }
        }
    }
    
    void dispatch(const std::vector<engine::EventFormat>& events) override {
        if (!running_) return;
        
        for (const auto& event : events) {
            dispatch(event);
        }
    }
    
    // ===== 控制接口 =====
    void start() override {
        running_ = true;
    }
    
    void stop() override {
        running_ = false;
        wait_for_completion();
    }
    
    bool is_running() const override {
        return running_;
    }
    
    void wait_for_completion() override {
        if (executor_) {
            executor_->waitForCompletion();
        }
    }
    
    // ===== 批量异步分发（优化性能）=====
    void dispatch_batch_async(
        const std::vector<std::unique_ptr<Event>>& engine_events,
        const std::vector<engine::EventFormat>& format_events,
        const SubscriptionManager& subs_mgr) {
        
        if (!running_) return;
        
        // ✅ 修复：先构建事件数据快照，再异步处理
        std::vector<engine::EventFormat> format_snapshot;
        {
            std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
            format_snapshot = format_events;
        }
        
        if (!executor_) {
            // 同步处理
            for (const auto& event : engine_events) {
                if (!event) continue;
                auto subscribers = subs_mgr.get_subscribers(event->type());
                for (const auto& subscriber : subscribers) {
                    subscriber->notify(event->clone());
                }
            }
            
            for (const auto& event : format_snapshot) {
                std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
                auto it = format_subscribers_.find(event.type);
                if (it == format_subscribers_.end()) continue;
                
                for (const auto& handler : it->second) {
                    handler(event);
                }
            }
        } else {
            // 异步批量处理
            executor_->post([format_snapshot, &engine_events, &subs_mgr, this]() {
                // 处理原始 Event
                for (const auto& event : engine_events) {
                    if (!event) continue;
                    auto subscribers = subs_mgr.get_subscribers(event->type());
                    for (const auto& subscriber : subscribers) {
                        subscriber->notify(event->clone());
                    }
                }
                
                // 处理 EventFormat
                for (const auto& event : format_snapshot) {
                    std::lock_guard<std::mutex> lock(format_subscribers_mutex_);
                    auto it = format_subscribers_.find(event.type);
                    if (it == format_subscribers_.end()) continue;
                    
                    for (const auto& handler : it->second) {
                        handler(event);
                    }
                }
            });
        }
    }
};

// ===== 工厂方法 =====

class EventDispatcherFactory {
public:
    static std::unique_ptr<IEventDispatcher> create_sync() {
        return std::make_unique<SyncEventDispatcher>();
    }
    
    static std::unique_ptr<IEventDispatcher> create_async(
        std::shared_ptr<foundation::thread::IExecutor> executor) {
        
        auto dispatcher = std::make_unique<AsyncEventDispatcher>(std::move(executor));
        dispatcher->start();
        return dispatcher;
    }
    
    static std::unique_ptr<IEventDispatcher> create_async_default() {
        auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(
            std::thread::hardware_concurrency()
        );
        return create_async(std::move(executor));
    }
};

} // namespace engine
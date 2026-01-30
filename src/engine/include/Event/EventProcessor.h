// EventProcessor.h
#pragma once
#define NOMINMAX
#include <windows.h>
#include <memory>
#include <chrono>
#include "EventQueue.hpp"
#include "EventDispatcher.h"
#include "SubscriptionManager.h"

namespace engine {

class EventProcessor {
private:
    std::shared_ptr<EventQueue> queue_;
    std::shared_ptr<IEventDispatcher> dispatcher_;
    std::shared_ptr<SubscriptionManager> subs_mgr_;
    
public:
    EventProcessor(
        std::shared_ptr<EventQueue> queue,
        std::shared_ptr<IEventDispatcher> dispatcher,
        std::shared_ptr<SubscriptionManager> subs_mgr)
        : queue_(std::move(queue))
        , dispatcher_(std::move(dispatcher))
        , subs_mgr_(std::move(subs_mgr)) {}
    
    // 处理单个事件
    bool process_one() {
        auto now = std::chrono::steady_clock::now();
        auto [engine_events, format_events] = queue_->poll_due_events_grouped(now);
        
        if (engine_events.empty() && format_events.empty()) {
            return false;
        }
        
        // 处理原始 Event
        for (auto& event : engine_events) {
            if (event) {
                dispatcher_->dispatch(std::move(event), *subs_mgr_);
            }
        }
        
        // 处理 EventFormat
        if (!format_events.empty()) {
            dispatcher_->dispatch(format_events);
        }
        
        return true;
    }
    
    // 批量处理
    size_t process_batch(size_t max_events) {
        size_t processed = 0;
        auto now = std::chrono::steady_clock::now();
        
        while (processed < max_events) {
            auto [engine_events, format_events] = queue_->poll_due_events_grouped(now);
            
            size_t batch_size = engine_events.size() + format_events.size();
            if (batch_size == 0) break;
            
            // 处理事件
            for (auto& event : engine_events) {
                if (event) {
                    dispatcher_->dispatch(std::move(event), *subs_mgr_);
                    processed++;
                }
            }
            
            if (!format_events.empty()) {
                dispatcher_->dispatch(format_events);
                processed += format_events.size();
            }
        }
        
        return processed;
    }
    
    // 智能批量处理
    size_t process_smart_batch(size_t preferred_batch_size) {
        size_t queue_size = queue_->size();
        size_t actual_batch_size = preferred_batch_size < queue_size ? 
                           preferred_batch_size : queue_size;
        return process_batch(actual_batch_size);
    }
    
    // 提交事件
    void submit(std::unique_ptr<Event> event) {
        queue_->enqueue(std::move(event));
    }
    
    void submit(const EventFormat& event) {
        queue_->enqueue(event);
    }
    
    // 延迟提交
    void submit_delayed(std::unique_ptr<Event> event, 
                       std::chrono::steady_clock::time_point scheduled_time,
                       int priority = 0) {
        queue_->enqueue(std::move(event), scheduled_time, priority);
    }
    
    // 状态查询
    size_t queue_size() const { return queue_->size(); }
    bool has_pending() const { return !queue_->empty(); }
    
    // 组件访问
    std::shared_ptr<EventQueue> get_queue() const { return queue_; }
    std::shared_ptr<IEventDispatcher> get_dispatcher() const { return dispatcher_; }
    std::shared_ptr<SubscriptionManager> get_subscription_manager() const { return subs_mgr_; }
};

} // namespace engine
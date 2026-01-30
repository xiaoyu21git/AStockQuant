#pragma once
#include <queue>
#include <vector>
#include <mutex>
#include <chrono>
#include <memory>
#include "Event.h"
#include "EventBus.hpp"
#include <variant>
#include <optional>


namespace engine {

struct DelayedEventPtr {
    std::unique_ptr<Event> event;
    std::chrono::steady_clock::time_point scheduled_time;

    DelayedEventPtr(std::unique_ptr<Event> evt, std::chrono::steady_clock::time_point time)
        : event(std::move(evt)), scheduled_time(time) {}

    DelayedEventPtr(const DelayedEventPtr&) = delete;
    DelayedEventPtr& operator=(const DelayedEventPtr&) = delete;
    DelayedEventPtr(DelayedEventPtr&& other) noexcept = default;
    DelayedEventPtr& operator=(DelayedEventPtr&& other) noexcept = default;
};

struct DelayedEventCompare {
    bool operator()(const DelayedEventPtr& a, const DelayedEventPtr& b) const {
        return a.scheduled_time > b.scheduled_time;
    }
};



// 支持 EventFormat 的事件包装器
struct EventWrapper {
    std::variant<std::unique_ptr<Event>, engine::EventFormat> event_data;
    std::chrono::steady_clock::time_point scheduled_time;
    int priority = 0;
    
    EventWrapper(std::unique_ptr<Event> evt, 
                std::chrono::steady_clock::time_point time,
                int prio = 0)
        : event_data(std::move(evt)), scheduled_time(time), priority(prio) {}
    
    EventWrapper(const engine::EventFormat& fmt,
                std::chrono::steady_clock::time_point time,
                int prio = 0)
        : event_data(fmt), scheduled_time(time), priority(prio) {}
    
    EventWrapper(EventWrapper&& other) = default;
    EventWrapper& operator=(EventWrapper&& other) = default;
    
    EventWrapper(const EventWrapper&) = delete;
    EventWrapper& operator=(const EventWrapper&) = delete;
    
    bool operator<(const EventWrapper& other) const {
        if (priority != other.priority) {
            return priority < other.priority; // 优先级高的先出队
        }
        return scheduled_time > other.scheduled_time; // 时间早的先出队
    }
};

class EventQueue {
private:
    // 内部队列
    std::priority_queue<EventWrapper> queue_;
    mutable std::mutex mutex_;
    
    // 统计信息
    size_t max_size_ = 0;
    size_t total_enqueued_ = 0;
    size_t total_dequeued_ = 0;
    
public:
    EventQueue() = default;
    ~EventQueue() = default;
    
    // ===== 基本操作 =====
    
    /**
     * @brief 入队原始 Event
     */
    void enqueue(std::unique_ptr<Event> evt) {
        enqueue(std::move(evt), std::chrono::steady_clock::now(), 0);
    }
    
    /**
     * @brief 入队原始 Event（带延迟和优先级）
     */
    void enqueue(std::unique_ptr<Event> evt,
                std::chrono::steady_clock::time_point scheduled_time,
                int priority = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace(std::move(evt), scheduled_time, priority);
        update_stats();
    }
    
    /**
     * @brief 入队 EventFormat
     */
    void enqueue(const engine::EventFormat& fmt) {
        enqueue(fmt, std::chrono::steady_clock::now(), 0);
    }
    
    /**
     * @brief 入队 EventFormat（带延迟和优先级）
     */
    void enqueue(const engine::EventFormat& fmt,
                std::chrono::steady_clock::time_point scheduled_time,
                int priority = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace(fmt, scheduled_time, priority);
        update_stats();
    }
    
    /**
     * @brief 批量入队
     */
    template<typename EventType>
    void enqueue_batch(const std::vector<EventType>& events) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        
        for (const auto& event : events) {
            if constexpr (std::is_same_v<EventType, std::unique_ptr<Event>>) {
                queue_.emplace(std::move(event), now, 0);
            } else {
                queue_.emplace(event, now, 0);
            }
        }
        
        update_stats();
    }
    
    /**
     * @brief 出队一个事件
     */
    std::optional<EventWrapper> dequeue() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.empty()) {
            return std::nullopt;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto& top = queue_.top();
        
        if (top.scheduled_time > now) {
            return std::nullopt; // 事件还未到期
        }
        
        EventWrapper event = std::move(const_cast<EventWrapper&>(top));
        queue_.pop();
        total_dequeued_++;
        
        return event;
    }
    
    /**
     * @brief 获取到期事件
     */
    std::vector<EventWrapper> poll_due_events(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<EventWrapper> due_events;
        
        while (!queue_.empty()) {
            auto& top = queue_.top();
            if (top.scheduled_time <= now) {
                due_events.push_back(std::move(const_cast<EventWrapper&>(top)));
                queue_.pop();
                total_dequeued_++;
            } else {
                break;
            }
        }
        
        return due_events;
    }
    
    /**
     * @brief 批量获取到期事件（按类型分组）
     */
    std::pair<std::vector<std::unique_ptr<Event>>, std::vector<engine::EventFormat>> 
    poll_due_events_grouped(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::unique_ptr<Event>> engine_events;
        std::vector<engine::EventFormat> format_events;
        
        while (!queue_.empty()) {
            auto& top = queue_.top();
            if (top.scheduled_time <= now) {
                std::visit([&](auto&& event) {
                    using T = std::decay_t<decltype(event)>;
                    if constexpr (std::is_same_v<T, std::unique_ptr<Event>>) {
                        engine_events.push_back(std::move(event));
                    } else {
                        format_events.push_back(std::move(event));
                    }
                }, const_cast<EventWrapper&>(top).event_data);
                
                queue_.pop();
                total_dequeued_++;
            } else {
                break;
            }
        }
        
        return {std::move(engine_events), std::move(format_events)};
    }
    
    // ===== 队列状态 =====
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    size_t estimated_wait_time() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return 0;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto& top = queue_.top();
        
        if (top.scheduled_time <= now) {
            return 0;
        }
        
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            top.scheduled_time - now).count();
    }
    
    // ===== 队列管理 =====
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
        max_size_ = 0;
    }
    
    void resize(size_t max_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        // 如果超过最大大小，移除低优先级事件
        while (queue_.size() > max_size) {
            queue_.pop();
        }
    }
    
    // ===== 统计信息 =====
    
    struct Stats {
        size_t current_size = 0;
        size_t max_size = 0;
        size_t total_enqueued = 0;
        size_t total_dequeued = 0;
        size_t estimated_wait_ms = 0;
        Stats(size_t current_size_,size_t max_size_ ,size_t total_enqueued_, size_t total_dequeued_,size_t estimated_wait_ms_)
        :current_size(current_size_),max_size(max_size_),total_enqueued(total_enqueued_),total_dequeued(total_dequeued_),estimated_wait_ms(estimated_wait_ms_){

        }
        std::string to_string() const {
            return "Stats{size=" + std::to_string(current_size) +
                   ", max=" + std::to_string(max_size) +
                   ", enqueued=" + std::to_string(total_enqueued) +
                   ", dequeued=" + std::to_string(total_dequeued) +
                   ", wait_ms=" + std::to_string(estimated_wait_ms) + "}";
        }
    };
    
    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return Stats(queue_.size(),max_size_,total_enqueued_,total_dequeued_,estimated_wait_time());
    }
    
    void reset_stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        max_size_ = queue_.size();
        total_enqueued_ = 0;
        total_dequeued_ = 0;
    }
    
private:
    void update_stats() {
        total_enqueued_++;
        if (queue_.size() > max_size_) {
            max_size_ = queue_.size();
        }
    }
};

} // namespace engine
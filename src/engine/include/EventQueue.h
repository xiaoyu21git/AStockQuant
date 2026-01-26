#pragma once
#include <queue>
#include <vector>
#include <mutex>
#include <chrono>
#include <memory>
#include "Event.h"

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

class EventQueue {
private:
    std::queue<std::unique_ptr<Event>> queue_;
    std::priority_queue<DelayedEventPtr, std::vector<DelayedEventPtr>, DelayedEventCompare> delayed_queue_;
    mutable std::mutex mutex_;

public:
    void enqueue(std::unique_ptr<Event> evt); 
    void enqueue_delayed(std::unique_ptr<Event> evt, std::chrono::steady_clock::time_point tp);
    std::unique_ptr<Event> dequeue();
    std::vector<std::unique_ptr<Event>> poll_due_events(std::chrono::steady_clock::time_point now);
    size_t size() const;
};

} // namespace engine

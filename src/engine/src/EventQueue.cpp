#include "EventQueue.h"

namespace engine {

void EventQueue::enqueue(std::unique_ptr<Event> evt) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(evt));
}

void EventQueue::enqueue_delayed(std::unique_ptr<Event> evt, std::chrono::steady_clock::time_point tp) {
    std::lock_guard<std::mutex> lock(mutex_);
    delayed_queue_.push(DelayedEventPtr(std::move(evt), tp));
}

std::unique_ptr<Event> EventQueue::dequeue() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!queue_.empty()) {
        auto evt = std::move(queue_.front());
        queue_.pop();
        return evt;
    }
    auto now = std::chrono::steady_clock::now();
    if (!delayed_queue_.empty() && delayed_queue_.top().scheduled_time <= now) {
        auto evt = std::move(const_cast<DelayedEventPtr&>(delayed_queue_.top()).event);
        delayed_queue_.pop();
        return evt;
    }
    return nullptr;
}

std::vector<std::unique_ptr<Event>> EventQueue::poll_due_events(std::chrono::steady_clock::time_point now) {
    std::vector<std::unique_ptr<Event>> due;
    std::lock_guard<std::mutex> lock(mutex_);
    // 先处理普通队列
    while (!queue_.empty()) {
        due.push_back(std::move(queue_.front()));
        queue_.pop();
    }
    while (!delayed_queue_.empty() && delayed_queue_.top().scheduled_time <= now) {
        due.push_back(std::move(const_cast<DelayedEventPtr&>(delayed_queue_.top()).event));
        delayed_queue_.pop();
    }
    return due;
}

size_t EventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() + delayed_queue_.size();
}

} // namespace engine

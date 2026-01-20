#pragma once
#include "EventBus.h"
#include "Event.h"

#include <queue>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace engine {
class EventSubscriber {
public:
    virtual ~EventSubscriber() = default;
    virtual void on_event(const Event& event) = 0;
};



class EventBusImpl final : public EventBus {
public:
    explicit EventBusImpl(std::shared_ptr<foundation::thread::IExecutor> executor);

    Error publish(std::unique_ptr<Event>) override;
    Error subscribe(Event::Type, EventSubscriber*) override;
    Error unsubscribe(Event::Type, EventSubscriber*) override;

    size_t dispatch() override;
    void clear() override;

    void set_policy(const DispatchPolicy&) override;
    DispatchPolicy policy() const override;

private:
    void deliver(Event& event);

private:
    std::shared_ptr<foundation::thread::IExecutor> executor_;
    DispatchPolicy policy_;

    mutable std::mutex mutex_;
    std::queue<std::unique_ptr<Event>> queue_;
    std::unordered_map<Event::Type, std::vector<EventSubscriber*>> subscribers_;
};

}

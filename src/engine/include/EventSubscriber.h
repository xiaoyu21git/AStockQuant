#pragma once
#include <vector>
#include <functional>
#include <memory>
#include "Event.h"
#include "foundation.h"

namespace engine {

class EventSubscriber {
private:
    foundation::Uuid id_;
    std::function<void(std::unique_ptr<Event>)> callback_;
    std::vector<Event::Type> event_types_;

public:
    EventSubscriber(std::function<void(std::unique_ptr<Event>)> cb, std::vector<Event::Type> types)
        : id_(foundation::Uuid::generate()), callback_(std::move(cb)), event_types_(std::move(types)) {}

    foundation::Uuid id() const { return id_; } // 内联
    const std::vector<Event::Type>& event_types() const { return event_types_; } // 内联

    void notify(std::unique_ptr<Event> evt) { // 内联
        if (callback_) callback_(std::move(evt));
    }
};

} // namespace engine

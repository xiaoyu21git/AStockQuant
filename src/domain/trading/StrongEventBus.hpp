#pragma once
// ---------------------------------------------------------------------------
// StrongEventBus.hpp -- strongly-typed wrapper around EventBus
//
// Wraps EventBus::subscribe(string, handler) and publish(EventFormat)
// into compile-time type-safe publish<Tag>(Payload) / subscribe<Tag>(handler).
// ---------------------------------------------------------------------------

#include "TypedEvent.hpp"
#include "Event/EventBus.hpp"
#include "foundation/Utils/Uuid.h"

#include <functional>

namespace domain::trading {

class StrongEventBus final {
public:
    explicit StrongEventBus(engine::EventBus& bus) : m_bus(bus) {}

    template <typename Tag>
    engine::PublishResult publish(const typename Tag::Payload& payload, int priority = 5) {
        auto fmt = StrongEvent<Tag>::toEventFormat(payload);
        return m_bus.publish(fmt, priority);
    }

    template <typename Tag>
    foundation::Uuid subscribe(
        std::function<void(const StrongEvent<Tag>&)> handler,
        int priority = 0)
    {
        return m_bus.subscribe(
            std::string(Tag::name),
            [handler](const engine::EventFormat& raw) {
                handler(StrongEvent<Tag>::fromEventFormat(raw));
            },
            nullptr,
            priority);
    }

    template <typename Tag, typename Fn>
    foundation::Uuid subscribe(Fn&& fn, int priority = 0) {
        return subscribe<Tag>(std::function<void(const StrongEvent<Tag>&)>(std::forward<Fn>(fn)), priority);
    }

    foundation::Uuid subscribeRaw(
        const std::string& eventType,
        engine::EventFormatHandler handler,
        int priority = 0)
    {
        return m_bus.subscribe(eventType, handler, nullptr, priority);
    }

    bool unsubscribe(foundation::Uuid id) {
        return m_bus.unsubscribe(id);
    }

    engine::EventBus& raw() noexcept { return m_bus; }

private:
    engine::EventBus& m_bus;
};

} // namespace domain::trading
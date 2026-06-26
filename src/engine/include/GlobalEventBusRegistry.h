#pragma once

#include "Event/EventBus.hpp"

namespace engine {

void register_engine_event_bus(EventBus* bus);
EventBus* get_engine_event_bus();

} // namespace engine
#pragma once

#include "Event/EventBus.hpp"

#include <memory>

namespace engine {

void register_engine_event_bus(std::shared_ptr<EventBus> bus);
std::shared_ptr<EventBus> get_engine_event_bus();

} // namespace engine
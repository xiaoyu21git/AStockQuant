#include "GlobalEventBusRegistry.h"

#include <mutex>

namespace engine {

namespace {
EventBus* g_engine_event_bus = nullptr;
std::mutex g_engine_event_bus_mutex;
} // namespace

void register_engine_event_bus(EventBus* bus) {
    std::lock_guard<std::mutex> lock(g_engine_event_bus_mutex);
    g_engine_event_bus = bus;
}

EventBus* get_engine_event_bus() {
    std::lock_guard<std::mutex> lock(g_engine_event_bus_mutex);
    return g_engine_event_bus;
}

} // namespace engine

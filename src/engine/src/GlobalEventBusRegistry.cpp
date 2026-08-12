#include "GlobalEventBusRegistry.h"

#include <mutex>
#include <foundation/log/logging.hpp>

namespace engine {

namespace {
std::shared_ptr<EventBus> g_engine_event_bus;
std::mutex g_engine_event_bus_mutex;
} // namespace

void register_engine_event_bus(std::shared_ptr<EventBus> bus) {
    std::lock_guard<std::mutex> lock(g_engine_event_bus_mutex);
    g_engine_event_bus = std::move(bus);
    INTERNAL_INFO_STREAM << "[GlobalEventBusRegistry] 引擎事件总线已注册";
}

std::shared_ptr<EventBus> get_engine_event_bus() {
    std::lock_guard<std::mutex> lock(g_engine_event_bus_mutex);
    return g_engine_event_bus;
}

} // namespace engine

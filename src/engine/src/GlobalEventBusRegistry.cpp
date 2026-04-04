#include "GlobalEventBusRegistry.h"

#include <mutex>

namespace engine {

namespace {
EventBus* g_engine_event_bus = nullptr;
std::mutex g_engine_event_bus_mutex;
thirdparty::JujinApi* g_shared_jujin_api = nullptr;
std::mutex g_shared_jujin_api_mutex;
} // namespace

void register_engine_event_bus(EventBus* bus) {
    std::lock_guard<std::mutex> lock(g_engine_event_bus_mutex);
    g_engine_event_bus = bus;
}

EventBus* get_engine_event_bus() {
    std::lock_guard<std::mutex> lock(g_engine_event_bus_mutex);
    return g_engine_event_bus;
}

void register_shared_jujin_api(thirdparty::JujinApi* api) {
    std::lock_guard<std::mutex> lock(g_shared_jujin_api_mutex);
    g_shared_jujin_api = api;
}

thirdparty::JujinApi* get_shared_jujin_api() {
    std::lock_guard<std::mutex> lock(g_shared_jujin_api_mutex);
    return g_shared_jujin_api;
}

} // namespace engine

#pragma once

#include "Event/EventBus.hpp"

namespace thirdparty {
class JujinApi;
}

namespace engine {

void register_engine_event_bus(EventBus* bus);
EventBus* get_engine_event_bus();

void register_shared_jujin_api(thirdparty::JujinApi* api);
thirdparty::JujinApi* get_shared_jujin_api();

} // namespace engine
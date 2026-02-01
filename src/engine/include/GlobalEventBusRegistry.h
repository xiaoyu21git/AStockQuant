#pragma once

#include <memory>
#include "Event/EventBus.hpp"

namespace engine {

// 全局共享的 C++ EventBus 注册表，用于在不同模块/绑定之间共享同一实例

// 注册引擎内部使用的 EventBus 指针
void register_engine_event_bus(EventBus* bus);

// 获取已经注册的引擎 EventBus 指针（可能为 nullptr）
EventBus* get_engine_event_bus();

} // namespace engine

#pragma once

#include <functional>
#include <memory>

#include "BaseInterface.h"
#include "Event/Event.h"

namespace engine {

// 触发器产生的新事件如何回注入引擎的发布管线
// 由 EngineImpl 在初始化时注册一个全局发布函数
using TriggerEventPublisher = std::function<Error(std::unique_ptr<Event>)>;

// 注册全局 Trigger 事件发布器
void set_trigger_event_publisher(TriggerEventPublisher publisher);

// 供 Trigger 动作调用，将新事件交给引擎处理（通常是 EngineImpl::publish_event）
Error publish_trigger_event(std::unique_ptr<Event> event);

} // namespace engine

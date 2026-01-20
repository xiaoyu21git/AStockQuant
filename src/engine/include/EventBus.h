#pragma once
#include <memory>
#include "DispatchPolicy.h"
#include "foundation/thread/IExecutor.h"
#include "Event.h"
namespace engine {

class Event;
class EventSubscriber;
struct Error;

class EventBus {
public:
    virtual ~EventBus() = default;

    // 发布 / 订阅
    virtual Error publish(std::unique_ptr<Event>) = 0;
    virtual Error subscribe(Event::Type, EventSubscriber*) = 0;
    virtual Error unsubscribe(Event::Type, EventSubscriber*) = 0;

    // 分发 / 清理
    virtual size_t dispatch() = 0;
    virtual void clear() = 0;

    // 运行期策略
    virtual void set_policy(const DispatchPolicy&) = 0;
    virtual DispatchPolicy policy() const = 0;

    // 新增控制接口
    virtual void stop() = 0;
    virtual void start() = 0;
    virtual bool is_stopped() const = 0;
    virtual void reset() = 0;

    // 工厂方法
    static std::unique_ptr<EventBus>
    create(std::shared_ptr<foundation::thread::IExecutor> executor);
};

} // namespace engine

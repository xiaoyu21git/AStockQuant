#pragma once
#include <memory>
#include "DispatchPolicy.h"
#include "foundation/thread/IExecutor.h"
#include "Event.h"
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <vector>
namespace engine {

class Event;
class EventSubscriber;
struct Error;

class EventBus {
public:

    virtual ~EventBus() = default;

    // 发布 / 订阅
    virtual Error publish(std::unique_ptr<Event> evt) = 0;
    virtual foundation::Uuid subscribe(Event::Type type, std::function<void(std::unique_ptr<Event>)> callback) = 0;
    virtual Error unsubscribe(Event::Type type, foundation::Uuid subscription_id)= 0;

    // 分发 / 清理
    virtual size_t dispatch() = 0;
    virtual void clear() = 0;

    // 运行期策略
    virtual void set_policy(std::shared_ptr<DispatchPolicy> policy) = 0;
    virtual std::shared_ptr<DispatchPolicy> policy() const = 0;

    // 新增控制接口
    virtual void stop() = 0;
    virtual void start() = 0;
    virtual bool is_stopped() const = 0;
    virtual void reset() = 0;

    // 工厂方法
    static std::unique_ptr<EventBus>create(std::shared_ptr<foundation::thread::IExecutor> executor);
};

} // namespace engine




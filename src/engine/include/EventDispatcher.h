#pragma once
#include <vector>
#include <memory>
#include "Event.h"
#include "EventSubscriber.h"
#include "SubscriptionManager.h"
#include "foundation/thread/IExecutor.h"

namespace engine {

class EventDispatcher {
private:
    std::shared_ptr<foundation::thread::IExecutor> executor_; // 可选线程池

public:
    void set_executor(std::shared_ptr<foundation::thread::IExecutor> exec) { executor_ = std::move(exec); } // 内联
    void dispatch(const std::vector<std::unique_ptr<Event>>& events, const SubscriptionManager& subs_mgr);
private:
    void notify_subscribers(const std::shared_ptr<Event>& event, const SubscriptionManager& subs_mgr);
};

} // namespace engine

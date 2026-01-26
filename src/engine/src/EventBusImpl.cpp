#include "EventBusImpl.h"

namespace engine {
    EventBusImpl::EventBusImpl(std::shared_ptr<foundation::thread::IExecutor> executor ,ExecutionMode Mode)
        : queue_(std::make_shared<EventQueue>()),
        dispatcher_(std::make_shared<EventDispatcher>()),
        subs_mgr_(std::make_shared<SubscriptionManager>()),
        threadMode_(Mode)
    {
        // 关键修复：在构造函数体中初始化
        dispatch_controller_ = std::make_unique<DispatchController>(
        queue_, subs_mgr_, dispatcher_,Mode,executor);
        if (threadMode_ == ExecutionMode::Sync)
            dispatch_controller_->start(); // 启动同步分发循环
        dispatcher_->set_executor(executor);
    }
    // 发布事件
inline Error EventBusImpl::publish(std::unique_ptr<Event> evt) {
    if (!evt) return Error::Code::INVALID_ARGUMENT;
    queue_->enqueue(std::move(evt));
    dispatch_controller_->notify();
    return Error::success();
}

    // 订阅事件
inline foundation::Uuid EventBusImpl::subscribe(Event::Type type, std::function<void(std::unique_ptr<Event>)> callback)  {
    auto sub = std::make_shared<EventSubscriber>(std::move(callback), std::vector<Event::Type>{type});
    return subs_mgr_->add_subscriber(sub);
}

    // 取消订阅
Error EventBusImpl::unsubscribe(Event::Type, foundation::Uuid id)  {
    if (!subs_mgr_->remove_subscriber(id))
    {
        return Error::Code::NOT_FOUND;    /* code */
    }
    
    return Error::Code::OK ;
}
    // 清空所有事件
void EventBusImpl::clear()  {
    // executor_->shutdown();
    //     // 可以根据需要清空队列和延迟队列
 }
} // namespace engine

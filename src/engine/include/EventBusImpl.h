#pragma once
#include <memory>
#include <vector>
#include <functional>
#include "EventBus.h"
#include "EventQueue.h"
#include "EventSubscriber.h"
#include "EventDispatcher.h"
#include "SubscriptionManager.h"
#include "EventBus.h"

namespace engine {

class DispatchController {
public:
    DispatchController(std::shared_ptr<EventQueue> queue,
                    std::shared_ptr<SubscriptionManager> subs_mgr,
                    std::shared_ptr<EventDispatcher> dispatcher,
                    ExecutionMode mode,
                    std::shared_ptr<foundation::thread::IExecutor> executor_
                    )
        : queue_(queue),
          subs_mgr_(subs_mgr),
          dispatcher_(dispatcher),
          strategy_(std::make_shared<DispatchStrategy>()),
          stop_flag_(false),
          threadMode_(mode),
          executor_(executor_)
    {
        auto default_policy = std::make_shared<ImmediatePolicy>();
        strategy_->set_policy(default_policy); // 默认策略
    }

    ~DispatchController() {
        //stop();
    }

    // ================== 生命周期 ==================
    void start() {
        stop_flag_ = false;
        worker_thread_ = std::thread([this]() { run_loop(); });
    }
    void stop() {
        stop_flag_ = true;
        //executor_->wait();       // 等所有任务完成
        cv_.notify_all();
        if (worker_thread_.joinable()) worker_thread_.join();
        executor_->shutdown();   // 停线程池（关键）
    }
    void notify() { 
        if (threadMode_ == ExecutionMode::Async) {
            auto queue = queue_;
            auto dispatcher = dispatcher_;
            auto subs = subs_mgr_;

            executor_->post([queue, dispatcher, subs]() {
                if (!queue || !dispatcher || !subs) return;
                auto events = queue->poll_due_events(std::chrono::steady_clock::now());
                dispatcher->dispatch(events, *subs);
            });
        } else {
            cv_.notify_one();
        }
    }

    // ================== 策略 ==================
    void set_policy(std::shared_ptr<DispatchPolicy> policy) {
        strategy_->set_policy(policy);
    }

    std::shared_ptr<DispatchPolicy> policy() const {
        return strategy_->get_policy();
    }

private:
    // ================== 线程主循环 ==================
    void run_loop() {
        while (!stop_flag_) {
            std::vector<std::unique_ptr<Event>> events;

            // 等待新事件或超时
            {
                std::unique_lock<std::mutex> lock(cv_mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(50)); // 每50ms轮询一次
            }

            // 获取队列中到期事件
            if (queue_) {
                events = queue_->poll_due_events(std::chrono::steady_clock::now());
            }
            if (threadMode_ == ExecutionMode::Sync) {
                if (!events.empty() && should_dispatch()) {
                    dispatcher_->dispatch(events, *subs_mgr_);
                    strategy_->update_last_dispatch();
                }
            }
        }
    }

    bool should_dispatch() const {
        if (!queue_ || !strategy_->get_policy()) return false;
        size_t queue_size = queue_->size();
        return strategy_->should_dispatch(queue_size);
    }

private:
    std::shared_ptr<EventQueue> queue_;
    std::shared_ptr<SubscriptionManager> subs_mgr_;
    std::shared_ptr<EventDispatcher> dispatcher_;
    std::shared_ptr<DispatchStrategy> strategy_;
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_;
    mutable std::mutex cv_mutex_;
    std::condition_variable cv_;
    ExecutionMode threadMode_;
    std::shared_ptr<foundation::thread::IExecutor> executor_;
};
class EventBusImpl : public EventBus {
public:
   
    // 构造函数，初始化事件总线
    EventBusImpl(std::shared_ptr<foundation::thread::IExecutor> executor,ExecutionMode mode= ExecutionMode::Sync);
    ~EventBusImpl(){
      stop();
    }
    // 发布事件
    Error publish(std::unique_ptr<Event> evt) override ;
    // 订阅事件
    foundation::Uuid subscribe(Event::Type type, std::function<void(std::unique_ptr<Event>)> callback) ;

    // 取消订阅
    Error unsubscribe(Event::Type, foundation::Uuid id) override ;

    // 执行事件分发
    size_t dispatch() override {
        auto due = queue_->poll_due_events(std::chrono::steady_clock::now());
        dispatcher_->dispatch(due, *subs_mgr_);
        return due.size();
    }

    // 清空所有事件
    void clear() override ;

    // 设置分发策略
    void set_policy(std::shared_ptr<DispatchPolicy> policy) override{
        //dispatch_controller_.set_policy(std::make_shared<DispatchPolicy>(policy));
        dispatch_controller_->set_policy(std::move(policy));
        
    } 
    // 获取分发策略
    std::shared_ptr<DispatchPolicy> policy() const override {
         return dispatch_controller_->policy();
    } 

    // 停止事件总线
    void stop() override {
        dispatch_controller_->stop();
    }
    // 启动事件总线
    void start() override {
        dispatch_controller_->start();
    }

    // 判断是否停止
    bool is_stopped() const override {
        return false;
    }

    // 重置事件总线
    void reset() override {
        dispatch_controller_->set_policy(dispatch_controller_->policy());
    }
private:
    std::shared_ptr<EventQueue> queue_;
    std::shared_ptr<EventDispatcher> dispatcher_;
    std::unique_ptr<DispatchController> dispatch_controller_;
    std::shared_ptr<SubscriptionManager> subs_mgr_;
    std::shared_ptr<foundation::thread::IExecutor> executor_;
    ExecutionMode threadMode_;
};
}


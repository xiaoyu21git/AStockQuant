#include "DispatchWorker.h"

namespace engine {

DispatchWorker::DispatchWorker(std::shared_ptr<EventQueue> queue, std::shared_ptr<SubscriptionManager> subs, std::shared_ptr<EventDispatcher> disp
    , std::shared_ptr<DispatchStrategy> strat)
    : queue_(std::move(queue)), subs_mgr_(std::move(subs)), dispatcher_(std::move(disp)), strategy_(std::move(strat)) {

    }

DispatchWorker::~DispatchWorker() {
    stop();
}

void DispatchWorker::run_loop() {
    worker_thread_ = std::thread([this]{ WorkLoop(); });
}

int DispatchWorker::WorkLoop(){
    try
    {
        while (!m_WorkExit.IsExit(10))
        {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            auto queue_size = queue_->size();
            if (strategy_ && strategy_->should_dispatch(queue_size)) {
                auto due = queue_->poll_due_events(std::chrono::steady_clock::now());
                dispatcher_->dispatch(due, *subs_mgr_);
                strategy_->update_last_dispatch();
            }
        }
        
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return 0;
}


void DispatchWorker::stop() {
    m_WorkExit.ExitThread();
    if (worker_thread_.joinable())
        worker_thread_.join();    // 等线程安全退出
   
    //notify();
}

void DispatchWorker::notify() {
    cv_.notify_all();
}

} // namespace engine

// DispatchWorkerCompat.h - 兼容性头文件
#pragma once

#include "DispatchWorkerAdapter.h"

namespace engine {

// 类型别名：让现有代码无缝衔接
using DispatchWorker = DispatchWorkerAdapter;

// 保持原有工厂函数
inline std::unique_ptr<DispatchWorker> create_sync_worker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs_mgr,
    std::shared_ptr<IEventDispatcher> dispatcher = nullptr,
    std::shared_ptr<DispatchStrategy> strategy = nullptr) {
    
    if (!dispatcher) {
        dispatcher = EventDispatcherFactory::create_sync();
    }
    
    return std::make_unique<DispatchWorker>(
        std::move(queue),
        std::move(subs_mgr),
        std::move(dispatcher),
        std::move(strategy)
    );
}

inline std::unique_ptr<DispatchWorker> create_async_worker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs_mgr,
    std::shared_ptr<foundation::thread::IExecutor> executor = nullptr,
    std::shared_ptr<IEventDispatcher> dispatcher = nullptr,
    std::shared_ptr<DispatchStrategy> strategy = nullptr,
    size_t worker_threads = 1) {
    
    DispatchWorkerConfig config;
    config.mode = ExecutionMode::Async;
    config.worker_threads = worker_threads;
    
    if (executor) {
        config.executor = executor;
    }
    
    if (!dispatcher) {
        if (executor) {
            dispatcher = EventDispatcherFactory::create_async(executor);
        } else {
            dispatcher = EventDispatcherFactory::create_async_default();
        }
    }
    
    return std::make_unique<DispatchWorker>(
        std::move(queue),
        std::move(subs_mgr),
        std::move(dispatcher),
        std::move(strategy),
        config
    );
}

inline std::unique_ptr<DispatchWorker> create_high_performance_worker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs_mgr,
    std::shared_ptr<foundation::thread::IExecutor> executor = nullptr) {
    
    auto worker = create_async_worker(
        std::move(queue),
        std::move(subs_mgr),
        executor
    );
    
    // 配置为高性能
    DispatchWorkerConfig config = worker->get_config();
    config.enable_batch_processing = true;
    config.batch_size = 200;
    config.poll_interval = std::chrono::milliseconds(10);
    worker->update_config(config);
    
    return worker;
}

} // namespace engine
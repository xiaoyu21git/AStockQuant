// DispatchWorker.cpp - 适配器实现
#include "Event/DispatchWorker.h"
#include <chrono>
#include <algorithm>


namespace engine {
// DispatchWorker.cpp

// 构造函数1：基础版
DispatchWorker::DispatchWorker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs,
    std::shared_ptr<IEventDispatcher> disp)
    : queue_(std::move(queue))
    , subs_mgr_(std::move(subs))
    , dispatcher_(std::move(disp))
    , config_(DispatchWorkerConfig{})  // 默认配置
{
    validate_components();
    initialize_policy();  // 创建默认策略
    initialize();
}

// 构造函数2：带配置版
DispatchWorker::DispatchWorker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs,
    std::shared_ptr<IEventDispatcher> disp,
    const DispatchWorkerConfig& config)
    : queue_(std::move(queue))
    , subs_mgr_(std::move(subs))
    , dispatcher_(std::move(disp))
    , config_(config)
{
    validate_components();
    initialize_policy();
    initialize();
}

// 构造函数3：带策略版
DispatchWorker::DispatchWorker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs,
    std::shared_ptr<IEventDispatcher> disp,
    std::shared_ptr<DispatchPolicy> policy)
    : queue_(std::move(queue))
    , subs_mgr_(std::move(subs))
    , dispatcher_(std::move(disp))
    , policy_(policy ? std::move(policy) : create_default_policy())
    , config_(DispatchWorkerConfig{})
{
    validate_components();
    
    // 根据策略更新配置
    config_.batch_size = policy_->batch_size();
    auto mode = policy_->mode();
    config_.enable_batch_processing = 
        (mode == DispatchMode::Batch || mode == DispatchMode::Hybrid);
    
    initialize();
}

// 构造函数4：完整版
DispatchWorker::DispatchWorker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs,
    std::shared_ptr<IEventDispatcher> disp,
    std::shared_ptr<DispatchPolicy> policy,
    const DispatchWorkerConfig& config)
    : queue_(std::move(queue))
    , subs_mgr_(std::move(subs))
    , dispatcher_(std::move(disp))
    , policy_(policy ? std::move(policy) : create_default_policy())
    , config_(config)
{
    validate_components();
    
    // 如果传入了策略，可能覆盖配置中的一些设置
    if (policy_) {
        config_.batch_size = policy_->batch_size();
        auto mode = policy_->mode();
        config_.enable_batch_processing = 
            (mode == DispatchMode::Batch || mode == DispatchMode::Hybrid);
    }
    
    initialize();
}


DispatchWorker::~DispatchWorker() {
    stop();
}

void DispatchWorker::init_processor(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs,
    std::shared_ptr<IEventDispatcher> disp) {
    
    processor_ = std::make_shared<EventProcessor>(
        std::move(queue), std::move(disp), std::move(subs));
}

// ===== 原有的基础接口 =====

void DispatchWorker::run_loop() {
    if (config_.mode == ExecutionMode::Sync) {
        sync_work_loop();
    } else {
        async_work_loop();
    }
}

void DispatchWorker::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    stop_flag_ = true;
    cv_.notify_all();
    work_exit_.ExitThread();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void DispatchWorker::notify() {
    cv_.notify_one();
}

// ===== 新增的高级接口 =====

void DispatchWorker::start() {
    if (running_.exchange(true)) {
        return;
    }
    
    stop_flag_ = false;
    if (config_.mode == ExecutionMode::Async) {
        worker_thread_ = std::thread([this]() {
            async_work_loop();
        });
    }
}

size_t DispatchWorker::dispatch() {
    if (config_.mode == ExecutionMode::Sync) {
        return dispatch_sync();
    } else {
        dispatch_async();
        return 0;
    }
}

size_t DispatchWorker::dispatch_sync() {
    size_t processed = 0;
    if (config_.enable_batch_processing) {
        processed = dispatch_batch(config_.batch_size);
    } else {
        processed = process_once();
    }
    return processed;
}

void DispatchWorker::dispatch_async() {
    if (executor_) {
        executor_->post([this]() {
            dispatch_sync();
        });
    } else if (running_) {
        notify();
    } else {
        std::thread([this]() {
            dispatch_sync();
        }).detach();
    }
}

size_t DispatchWorker::dispatch_batch(size_t max_events) {
    if (!processor_) return 0;
    
    size_t processed = processor_->process_batch(max_events);
    if (processed > 0) {
        update_stats(processed, true);
    }
    return processed;
}

bool DispatchWorker::wait_for_completion(int timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeout_ms);
    
    while (processor_ && processor_->queue_size() > 0 && running_) {
        if (timeout_ms > 0) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= timeout) {
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return true;
}

// ===== 配置管理 =====

void DispatchWorker::update_config(const DispatchWorkerConfig& new_config) {
    config_ = new_config;
    apply_config();
}

void DispatchWorker::set_execution_mode(ExecutionMode mode) {
    config_.mode = mode;
    apply_config();
}

void DispatchWorker::set_executor(std::shared_ptr<foundation::thread::IExecutor> executor) {
    executor_ = std::move(executor);
}

void DispatchWorker::set_poll_interval(std::chrono::milliseconds interval) {
    config_.poll_interval = interval;
}

// ===== 策略管理 =====

void DispatchWorker::set_policy(std::shared_ptr<DispatchPolicy> policy) {
    if (policy) {
        // 根据实际的 DispatchPolicy 接口设置配置
        
        // 1. 设置是否启用批量处理（根据策略模式）
        DispatchMode mode = policy->mode();
        config_.enable_batch_processing = (mode == DispatchMode::Batch || 
                                          mode == DispatchMode::Hybrid);
        
        // 2. 设置批量大小
        size_t batch_size = policy->batch_size();
        if (batch_size > 0) {
            config_.batch_size = batch_size;
        }
        
        // 3. 设置轮询间隔（从 interval 方法获取）
        auto interval = policy->interval();
        if (interval.count() > 0) {
            config_.poll_interval = interval;
        }
        
        // 4. 存储策略对象供后续使用
        //strategy_ = policy;
    } else {
        // 如果没有策略，使用默认值
        config_.enable_batch_processing = true;
        config_.batch_size = 100;
        config_.poll_interval = std::chrono::milliseconds(50);
       // strategy_.reset();
    }
}

std::shared_ptr<DispatchPolicy> DispatchWorker::policy() const {
    // 返回一个简单的策略包装器
    return nullptr; // 简化实现
}

// ===== 状态查询 =====

size_t DispatchWorker::get_queue_size() const {
    return processor_ ? processor_->queue_size() : 0;
}

std::chrono::steady_clock::duration DispatchWorker::get_idle_time() const {
    auto now = std::chrono::steady_clock::now();
    return now - stats_.last_activity_time;
}

// ===== 组件访问 =====

std::shared_ptr<EventQueue> DispatchWorker::get_queue() const {
    return processor_ ? processor_->get_queue() : nullptr;
}

std::shared_ptr<SubscriptionManager> DispatchWorker::get_subscription_manager() const {
    return processor_ ? processor_->get_subscription_manager() : nullptr;
}

std::shared_ptr<IEventDispatcher> DispatchWorker::get_dispatcher() const {
    return processor_ ? processor_->get_dispatcher() : nullptr;
}

// ===== 统计信息 =====

DispatchWorker::DetailedStats DispatchWorker::get_detailed_stats() const {
    auto now = std::chrono::steady_clock::now();
    auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - stats_.last_activity_time);
    
    return {
        get_queue_size(),
        static_cast<size_t>(stats_.total_events_processed.load()),
        static_cast<size_t>(stats_.total_batches_processed.load()),
        static_cast<size_t>(stats_.max_queue_size.load()),
        idle_time,
        running_,
        config_.mode
    };
}

void DispatchWorker::reset_stats() {
    stats_.reset();
}

// ===== 新增简洁接口 =====
void DispatchWorker::update_queue_stats() {
    size_t queue_size = get_queue_size();
    int64_t old_max = stats_.max_queue_size.load();
    
    while (queue_size > static_cast<size_t>(old_max) && 
           !stats_.max_queue_size.compare_exchange_weak(old_max, 
                                                       static_cast<int64_t>(queue_size))) {
        // 循环直到更新成功
    }
}

void DispatchWorker::submit(std::unique_ptr<Event> event) {
    if (processor_) {
        processor_->submit(std::move(event));
        update_queue_stats();
        notify();
    }
}

void DispatchWorker::submit(const EventFormat& event) {
    if (processor_) {
        processor_->submit(event);
        update_queue_stats();
        notify();
    }
}

size_t DispatchWorker::process_once() {
    if (!processor_) return 0;
    
    if (processor_->process_one()) {
        update_stats(1, false);
        return 1;
    }
    return 0;
}

// ===== 内部实现方法 =====

void DispatchWorker::work_loop_internal() {
    // 兼容旧接口，实际使用 sync_work_loop 或 async_work_loop
    if (config_.mode == ExecutionMode::Sync) {
        sync_work_loop();
    } else {
        async_work_loop();
    }
}

void DispatchWorker::sync_work_loop() {
    while (!work_exit_.IsExit(100)) {
        if (should_process()) {
            process_events_internal();
        }
        std::this_thread::sleep_for(config_.poll_interval);
    }
}

void DispatchWorker::async_work_loop() {
    while (!stop_flag_) {
        if (should_process()) {
            process_events_internal();
        }
        
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, config_.poll_interval);
    }
}

size_t DispatchWorker::process_events_internal() {
    size_t processed = 0;
    if (config_.enable_batch_processing) {
        processed = processor_->process_batch(config_.batch_size);
    } else {
        if (processor_->process_one()) {
            processed = 1;
        }
    }
    
    if (processed > 0) {
        update_stats(processed, config_.enable_batch_processing);
    }
    
    return processed;
}

bool DispatchWorker::should_process() const {
    if (!processor_) return false;
    
    // 简单策略：队列不为空就处理
    return processor_->queue_size() > 0;
}

void DispatchWorker::update_stats(size_t events_processed, bool is_batch) {
    stats_.total_events_processed += static_cast<int64_t>(events_processed);
    if (is_batch) {
        stats_.total_batches_processed++;
    }
    stats_.last_activity_time = std::chrono::steady_clock::now();
    
    // 更新最大队列大小（修复类型转换）
    size_t queue_size = get_queue_size();
    int64_t old_max = stats_.max_queue_size.load();
    int64_t queue_size_int64 = static_cast<int64_t>(queue_size);
    
    while (queue_size_int64 > old_max && 
           !stats_.max_queue_size.compare_exchange_weak(old_max, queue_size_int64)) {
        // 循环直到更新成功
    }
}
void DispatchWorker::apply_config() {
    // 应用执行器配置
    if (config_.executor) {
        executor_ = config_.executor;
    }
}

// ===== 工厂函数 =====

std::unique_ptr<DispatchWorker> create_sync_worker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs_mgr,
    std::shared_ptr<IEventDispatcher> dispatcher,
    std::shared_ptr<DispatchWorkerConfig> default_config) {
    if (!dispatcher) {
        dispatcher = EventDispatcherFactory::create_sync();
    }
    
    return std::make_unique<DispatchWorker>(
        std::move(queue),
        std::move(subs_mgr),
        std::move(dispatcher),
        *default_config
    );
}

std::unique_ptr<DispatchWorker> create_async_worker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs_mgr,
    std::shared_ptr<foundation::thread::IExecutor> executor,
    std::shared_ptr<IEventDispatcher> dispatcher,
    std::shared_ptr<DispatchStrategy> strategy,
    size_t worker_threads) {
    
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
        std::move(strategy->get_policy()),
        config
    );
}

std::unique_ptr<DispatchWorker> create_high_performance_worker(
    std::shared_ptr<EventQueue> queue,
    std::shared_ptr<SubscriptionManager> subs_mgr,
    std::shared_ptr<foundation::thread::IExecutor> executor) {
    
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
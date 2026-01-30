// DispatchWorkerAdapter.h - 核心适配器
#pragma once

#include "SlimDispatchWorker.h"
#include "EventDispatcher.h"
#include "SubscriptionManager.h"
#include "DispatchPolicy.h"
#include "foundation/thread/ThreadExit.h"
#include "foundation/thread/IExecutor.h"
#include <atomic>
#include <memory>

namespace engine {

// DispatchWorkerConfig.h（直接在现有结构体上添加）
#pragma once
#include <chrono>
#include <memory>
#include "DispatchPolicy.h"      // 需要包含新的头文件
#include "HotDispatchManager.h"  // 需要包含新的头文件

struct DispatchWorkerConfig {
    // ===== 原有配置（保持原样）=====
    ExecutionMode mode = ExecutionMode::Sync;
    size_t worker_threads = 1;
    std::chrono::milliseconds poll_interval = std::chrono::milliseconds(50);
    bool enable_priority_queue = true;
    bool enable_batch_processing = true;
    size_t batch_size = 100;
    std::shared_ptr<foundation::thread::IExecutor> executor;
    bool enable_load_balancing = false;
    
    // ===== 新增策略相关配置 =====
    std::shared_ptr<DispatchPolicy> dispatch_policy{nullptr};
    DispatchMode dispatch_mode = DispatchMode::Hybrid;
    std::chrono::milliseconds dispatch_interval = std::chrono::milliseconds(50);
    
    // ===== 新增热更新相关配置 =====
    std::shared_ptr<HotDispatchManager> hot_manager{nullptr};
    bool enable_hot_update = false;
    std::chrono::milliseconds hot_update_check_interval = std::chrono::seconds(1);
    
    DispatchWorkerConfig() = default;
    
    // ===== 工厂方法（保持原有，新增策略相关）=====
    static DispatchWorkerConfig default_config() {
        return DispatchWorkerConfig{};
    }
    
    // 新增：创建带策略的配置
    static DispatchWorkerConfig with_policy(std::shared_ptr<DispatchPolicy> policy) {
        DispatchWorkerConfig config;
        config.dispatch_policy = std::move(policy);
        return config;
    }
    
    // 新增：创建带热更新的配置
    static DispatchWorkerConfig with_hot_update(std::shared_ptr<HotDispatchManager> manager) {
        DispatchWorkerConfig config;
        config.hot_manager = std::move(manager);
        config.enable_hot_update = true;
        return config;
    }
    
    // 新增：创建批量策略配置
    static DispatchWorkerConfig batch_config(size_t batch_size = 100) {
        DispatchWorkerConfig config;
        config.dispatch_mode = DispatchMode::Batch;
        config.batch_size = batch_size;  // 注意：这里设置两个batch_size
        return config;
    }
    
    // 新增：创建定时策略配置
    static DispatchWorkerConfig time_config(std::chrono::milliseconds interval = 
                                            std::chrono::milliseconds(50)) {
        DispatchWorkerConfig config;
        config.dispatch_mode = DispatchMode::TimeBased;
        config.dispatch_interval = interval;
        return config;
    }
    
    // ===== 验证方法 =====
    bool validate() const {
        // 验证原有配置
        if (worker_threads == 0) {
            return false;
        }
        if (poll_interval.count() <= 0) {
            return false;
        }
        
        // 验证策略配置
        if (enable_batch_processing && batch_size == 0) {
            return false;  // 启用了批量处理但批量为0
        }
        
        // 验证热更新配置
        if (enable_hot_update && !hot_manager) {
            // 可以记录警告，但不一定返回false
            // 因为hot_manager可能稍后设置
        }
        
        return true;
    }
    
    // ===== 辅助方法 =====
    
    // 获取实际的策略对象
    std::shared_ptr<DispatchPolicy> get_policy_or_create() const {
        if (dispatch_policy) {
            return dispatch_policy;
        }
        
        // 如果没有指定具体策略，根据配置创建
        // 注意：dispatch_interval 可能为0，需要处理
        auto actual_interval = dispatch_interval;
        if (actual_interval.count() <= 0) {
            actual_interval = std::chrono::milliseconds(50);  // 默认值
        }
        
        // 使用配置的 batch_size 作为策略的批量大小
        return DispatchPolicyFactory::instance().create(
            dispatch_mode,
            batch_size,      // 复用配置中的 batch_size
            actual_interval);
    }
    
    // 检查是否启用了热更新
    bool is_hot_update_enabled() const {
        return enable_hot_update && hot_manager != nullptr;
    }
};
// 无缝衔接适配器 - 实现所有旧接口
class DispatchWorkerAdapter {
private:
    // 核心：瘦身版工作者
    std::shared_ptr<SlimDispatchWorker> slim_worker_;
    
    // 旧架构需要的成员
    DispatchWorkerConfig config_;
    std::shared_ptr<foundation::thread::IExecutor> executor_;
    std::shared_ptr<DispatchStrategy> strategy_;
    CThreadExit work_exit_;
    
    // 线程管理
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_flag_{false};
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    
public:
    // ===== 保持原有构造函数 =====
    
    DispatchWorkerAdapter(
        std::shared_ptr<EventQueue> queue,
        std::shared_ptr<SubscriptionManager> subs_mgr,
        std::shared_ptr<IEventDispatcher> dispatcher,
        std::shared_ptr<DispatchStrategy> strategy = nullptr)
        : config_()
        , strategy_(std::move(strategy)) {
        
        init_slim_worker(queue, subs_mgr, dispatcher);
    }
    
    DispatchWorkerAdapter(
        std::shared_ptr<EventQueue> queue,
        std::shared_ptr<SubscriptionManager> subs_mgr,
        std::shared_ptr<IEventDispatcher> dispatcher,
        std::shared_ptr<DispatchStrategy> strategy,
        const DispatchWorkerConfig& config)
        : config_(config)
        , strategy_(std::move(strategy)) {
        
        init_slim_worker(queue, subs_mgr, dispatcher);
        apply_config();
    }
    
    ~DispatchWorkerAdapter() {
        stop();
    }
    
    // 禁止拷贝
    DispatchWorkerAdapter(const DispatchWorkerAdapter&) = delete;
    DispatchWorkerAdapter& operator=(const DispatchWorkerAdapter&) = delete;
    
    // 允许移动
    DispatchWorkerAdapter(DispatchWorkerAdapter&&) = default;
    DispatchWorkerAdapter& operator=(DispatchWorkerAdapter&&) = default;
    
    // ===== 原有接口完全保留 =====
    
    /**
     * @brief 启动工作线程（原有接口）
     */
    void run_loop() {
        if (config_.mode == ExecutionMode::Async) {
            start();
        } else {
            // 同步模式：在当前线程运行循环
            sync_run_loop();
        }
    }
    
    /**
     * @brief 安全停止工作线程（原有接口）
     */
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        
        stop_flag_ = true;
        cv_.notify_all();
        work_exit_.ExitThread();
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        
        // 停止瘦身版工作者
        if (slim_worker_) {
            slim_worker_->stop();
        }
    }
    
    /**
     * @brief 通知工作线程有新事件（原有接口）
     */
    void notify() {
        cv_.notify_one();
    }
    
    // ===== 新增的高级接口 =====
    
    /**
     * @brief 启动工作者（带自动模式选择）
     */
    void start() {
        if (running_.exchange(true)) {
            return;
        }
        
        stop_flag_ = false;
        
        // 启动瘦身版工作者
        if (slim_worker_) {
            slim_worker_->start();
        }
        
        // 如果需要额外的线程管理
        if (config_.mode == ExecutionMode::Async && 
            config_.worker_threads > 0) {
            start_worker_thread();
        }
    }
    
    /**
     * @brief 分发事件（根据模式自动选择同步/异步）
     * @return 处理的事件数量
     */
    size_t dispatch() {
        if (config_.mode == ExecutionMode::Sync) {
            return dispatch_sync();
        } else {
            dispatch_async();
            return 0;  // 异步不返回数量
        }
    }
    
    /**
     * @brief 同步分发事件
     */
    size_t dispatch_sync() {
        if (!slim_worker_) return 0;
        
        size_t processed = 0;
        if (config_.enable_batch_processing) {
            processed = slim_worker_->process_batch(config_.batch_size);
        } else {
            processed = slim_worker_->process_once();
        }
        
        return processed;
    }
    
    /**
     * @brief 异步分发事件
     */
    void dispatch_async() {
        if (!slim_worker_) return;
        
        if (executor_) {
            // 使用外部执行器
            executor_->post([this]() {
                dispatch_sync();
            });
        } else if (slim_worker_->is_running()) {
            // 使用瘦身版工作者的线程
            notify();
        } else {
            // 临时线程
            std::thread([this]() {
                dispatch_sync();
            }).detach();
        }
    }
    
    /**
     * @brief 批量分发事件
     */
    size_t dispatch_batch(size_t max_events = 100) {
        if (!slim_worker_) return 0;
        return slim_worker_->process_batch(max_events);
    }
    
    /**
     * @brief 等待所有任务完成（异步模式）
     */
    bool wait_for_completion(int timeout_ms = 5000) {
        if (!slim_worker_) return true;
        
        slim_worker_->wait_for_completion();
        return true;
    }
    
    // ===== 配置管理 =====
    
    const DispatchWorkerConfig& get_config() const { return config_; }
    
    void update_config(const DispatchWorkerConfig& new_config) {
        config_ = new_config;
        apply_config();
    }
    
    void set_execution_mode(ExecutionMode mode) {
        config_.mode = mode;
        apply_config();
    }
    
    void set_executor(std::shared_ptr<foundation::thread::IExecutor> executor) {
        executor_ = std::move(executor);
    }
    
    void set_poll_interval(std::chrono::milliseconds interval) {
        config_.poll_interval = interval;
        apply_config();
    }
    
    // ===== 策略管理 =====
    
    void set_policy(std::shared_ptr<DispatchPolicy> policy) {
        if (policy) {
            // 如果不需要存储policy，直接使用
            config_.batch_size = policy->batch_size();  // ✅ 这个可以
        
            auto mode = policy->mode();
            config_.enable_batch_processing = 
            (mode == DispatchMode::Batch || mode == DispatchMode::Hybrid);
        
        // 如果需要存储
        // policy_ = std::move(policy);
    }
    }
    
    std::shared_ptr<DispatchPolicy> policy() const {
        // 创建兼容的策略对象
        return nullptr;  // 简化实现
    }
    
    // ===== 状态查询 =====
    
    bool is_running() const { 
        return slim_worker_ ? slim_worker_->is_running() : running_; 
    }
    
    bool is_stopped() const { return !is_running(); }
    
    ExecutionMode get_execution_mode() const { return config_.mode; }
    
    size_t get_queue_size() const {
        return slim_worker_ ? slim_worker_->queue_size() : 0;
    }
    
    size_t get_processed_count() const {
        return slim_worker_ ? slim_worker_->processed_count() : 0;
    }
    
    size_t get_batch_count() const {
        return slim_worker_ ? slim_worker_->batch_count() : 0;
    }
    
    std::chrono::steady_clock::duration get_idle_time() const {
        // 简化实现
        return std::chrono::steady_clock::duration::zero();
    }
    
    // ===== 组件访问 =====
    
    std::shared_ptr<EventQueue> get_queue() const {
        return slim_worker_ ? slim_worker_->get_processor()->get_queue() : nullptr;
    }
    
    std::shared_ptr<SubscriptionManager> get_subscription_manager() const {
        return slim_worker_ ? slim_worker_->get_processor()->get_subscription_manager() : nullptr;
    }
    
    std::shared_ptr<IEventDispatcher> get_dispatcher() const {
        return slim_worker_ ? slim_worker_->get_processor()->get_dispatcher() : nullptr;
    }
    
    std::shared_ptr<DispatchStrategy> get_strategy() const { 
        return strategy_; 
    }
    
    // ===== 统计信息 =====
    
    struct DetailedStats {
        size_t queue_size;
        size_t total_processed;
        size_t total_batches;
        size_t max_queue_size;
        std::chrono::milliseconds idle_time_ms;
        bool is_running;
        ExecutionMode mode;
    };
    
    DetailedStats get_detailed_stats() const {
        if (!slim_worker_) {
            return {0, 0, 0, 0, std::chrono::milliseconds(0), false, config_.mode};
        }
        
        auto stats = slim_worker_->get_stats_collector()->get_statistics(
            slim_worker_->queue_size());
        
        return {
            stats.current_size,
            stats.total_events_processed,
            stats.total_batches_processed,
            stats.max_size,
            stats.idle_time_ms,
            slim_worker_->is_running(),
            config_.mode
        };
    }
    
    void reset_stats() {
        if (slim_worker_) {
            // StatsCollector 可能需要添加 reset 方法
            // 简化处理
        }
    }
    
    // ===== 事件提交（扩展接口）=====
    
    void submit(std::unique_ptr<Event> event) {
        if (slim_worker_) {
            slim_worker_->submit(std::move(event));
            notify();
        }
    }
    
    void submit(const EventFormat& event) {
        if (slim_worker_) {
            slim_worker_->submit(event);
            notify();
        }
    }
    
    // ===== 获取瘦身版工作者（用于新代码）=====
    
    std::shared_ptr<SlimDispatchWorker> get_slim_worker() const {
        return slim_worker_;
    }
    
private:
    void init_slim_worker(std::shared_ptr<EventQueue> queue,
                         std::shared_ptr<SubscriptionManager> subs_mgr,
                         std::shared_ptr<IEventDispatcher> dispatcher) {
        
        slim_worker_ = std::make_shared<SlimDispatchWorker>(
            std::move(queue),
            std::move(subs_mgr),
            std::move(dispatcher)
        );
    }
    
    void apply_config() {
        if (!slim_worker_) return;
        
        // 应用批量配置
        slim_worker_->enable_batch(config_.enable_batch_processing);
        slim_worker_->set_batch_size(config_.batch_size);
        slim_worker_->set_poll_interval(config_.poll_interval);
        
        // 应用执行器
        if (config_.executor) {
            executor_ = config_.executor;
        }
    }
    
    void start_worker_thread() {
        worker_thread_ = std::thread([this]() {
            adapter_work_loop();
        });
    }
    
    void adapter_work_loop() {
        while (!stop_flag_) {
            // 使用原有的退出检查
            if (work_exit_.IsExit(100)) {
                break;
            }
            
            // 处理事件
            if (slim_worker_) {
                if (config_.enable_batch_processing) {
                    slim_worker_->process_batch(config_.batch_size);
                } else {
                    slim_worker_->process_once();
                }
            }
            
            // 等待
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, config_.poll_interval);
        }
    }
    
    void sync_run_loop() {
        while (!work_exit_.IsExit(100)) {
            // 同步处理
            dispatch_sync();
            
            // 短暂休眠避免CPU占用过高
            std::this_thread::sleep_for(config_.poll_interval);
        }
    }
};

} // namespace engine
// DispatchWorker.h - 替换为适配器版本
#pragma once

// 包含必要的头文件
#include "EventQueue.hpp"
#include "EventDispatcher.h"
#include "SubscriptionManager.h"
#include "DispatchPolicy.h"
#include "foundation/thread/ThreadExit.h"
#include "foundation/thread/IExecutor.h"
#include "HotDispatchManager.h"
#include "DispatchWorkerAdapter.h"
#include <chrono>
#include <atomic>
#include <memory>
#include <thread>
#include <condition_variable>
#include "EventProcessor.h"

namespace engine {



// ===== 前置声明（保持原样）=====
class EventQueue;
class IEventDispatcher;
class SubscriptionManager;
class DispatchWorker;
class DispatchStrategy;

// ===== 瘦身版核心组件（内部使用）=====

// ===== 主类：DispatchWorker（适配器模式）=====
class DispatchWorker {
private:
    // 核心：瘦身版事件处理器
    std::shared_ptr<EventProcessor> processor_;
    
    // 原有成员（保持兼容）
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_{false};
    std::atomic<bool> running_{false};
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    CThreadExit work_exit_;
    std::shared_ptr<EventQueue> queue_;
    std::shared_ptr<IEventDispatcher> dispatcher_;
    std::shared_ptr<SubscriptionManager> subs_mgr_;
    DispatchWorkerConfig config_;
    std::shared_ptr<DispatchPolicy> policy_;
    std::chrono::steady_clock::time_point last_dispatch_;
    std::shared_ptr<HotDispatchManager> hot_manager_;  // 新增
    std::shared_ptr<foundation::thread::IExecutor> executor_;
    std::mutex policy_mutex_;
    
    // 统计信息（简化版）
    struct Stats {
        std::atomic<int64_t> total_events_processed{0};
        std::atomic<int64_t> total_batches_processed{0};
        std::atomic<int64_t> max_queue_size{0};
        std::chrono::steady_clock::time_point last_activity_time;
        
        void reset() {
            total_events_processed = 0;
            total_batches_processed = 0;
            max_queue_size = 0;
            last_activity_time = std::chrono::steady_clock::now();
        }
    };
    
    Stats stats_;
    
public:
// 基础版：只有必要参数
    explicit DispatchWorker(
        std::shared_ptr<EventQueue> queue,
        std::shared_ptr<SubscriptionManager> subs,
        std::shared_ptr<IEventDispatcher> disp);
    
    // 带配置版
    DispatchWorker(
        std::shared_ptr<EventQueue> queue,
        std::shared_ptr<SubscriptionManager> subs,
        std::shared_ptr<IEventDispatcher> disp,
        const DispatchWorkerConfig& config);
    
    // 带策略版（使用默认配置）
    DispatchWorker(
        std::shared_ptr<EventQueue> queue,
        std::shared_ptr<SubscriptionManager> subs,
        std::shared_ptr<IEventDispatcher> disp,
        std::shared_ptr<DispatchPolicy> policy);
    
    // 完整版（明确所有参数）
    DispatchWorker(
        std::shared_ptr<EventQueue> queue,
        std::shared_ptr<SubscriptionManager> subs,
        std::shared_ptr<IEventDispatcher> disp,
        std::shared_ptr<DispatchPolicy> policy,
        const DispatchWorkerConfig& config);
    ~DispatchWorker();
     void initialize_policy() {
        if (hot_manager_) {
            // 有热更新管理器：从管理器获取初始策略
            policy_ = hot_manager_->get_current_policy();
        } else if (config_.dispatch_policy) {
            // 配置中有策略：使用配置的策略
            policy_ = config_.dispatch_policy;
        } else {
            // 默认情况：根据配置参数创建策略
            policy_ = DispatchPolicyFactory::instance().create(
                config_.dispatch_mode,
                config_.batch_size,
                config_.dispatch_interval);
        }
    }
    // 热管理器设置
void DispatchWorker::set_hot_manager(std::shared_ptr<HotDispatchManager> manager) {
    hot_manager_ = std::move(manager);
    
    if (hot_manager_) {
        // 注册到热管理器（如果需要）
        // hot_manager_->register_worker(shared_from_this());
    }
}
std::shared_ptr<HotDispatchManager> DispatchWorker::get_hot_manager() const {
    return hot_manager_;
}
void DispatchWorker::validate_components() const {
    if (!queue_) {
        throw std::invalid_argument("EventQueue cannot be null");
    }
    if (!subs_mgr_) {
        throw std::invalid_argument("SubscriptionManager cannot be null");
    }
    if (!dispatcher_) {
        throw std::invalid_argument("IEventDispatcher cannot be null");
    }
}
std::shared_ptr<DispatchPolicy> DispatchWorker::create_default_policy() const {
    return std::make_shared<HybridPolicy>(100, std::chrono::milliseconds(50));
}

void DispatchWorker::initialize() {
    // 其他初始化逻辑
    // 例如：初始化线程、统计信息等
}
    void check_and_update_policy() {
        if (hot_manager_) {
            auto new_policy = hot_manager_->get_current_policy();
            if (new_policy != policy_) {
                std::lock_guard lock(policy_mutex_);
                policy_ = new_policy;
               // log_info("Policy hot-updated via manager");
            }
        }
    }
    // 禁止拷贝和赋值
    DispatchWorker(const DispatchWorker&) = delete;
    DispatchWorker& operator=(const DispatchWorker&) = delete;
    
    // 允许移动
    DispatchWorker(DispatchWorker&&) = default;
    DispatchWorker& operator=(DispatchWorker&&) = default;
     // 成员函数声明
    void update_queue_stats();
    
    // 提供统计信息的访问方法
    const Stats& get_stats() const { return stats_; }
    Stats& get_stats() { return stats_; }  // 非const版本
    // ===== 原有的基础接口（完全兼容）=====
    
    /**
     * @brief 启动工作线程（原有接口）
     */
    void run_loop();
    
    /**
     * @brief 安全停止工作线程（原有接口）
     */
    void stop();
    
    /**
     * @brief 通知工作线程有新事件（原有接口）
     */
    void notify();
    
    // ===== 新增的高级接口（保持兼容）=====
    
    /**
     * @brief 启动工作者（带自动模式选择）
     */
    void start();
    
    /**
     * @brief 分发事件（根据模式自动选择同步/异步）
     * @return 处理的事件数量
     */
    size_t dispatch();
    
    /**
     * @brief 同步分发事件
     */
    size_t dispatch_sync();
    
    /**
     * @brief 异步分发事件
     */
    void dispatch_async();
    
    /**
     * @brief 批量分发事件
     */
    size_t dispatch_batch(size_t max_events = 100);
    
    /**
     * @brief 等待所有任务完成（异步模式）
     * @param timeout_ms 超时时间（毫秒）
     */
    bool wait_for_completion(int timeout_ms = 5000);
    
    // ===== 配置管理（保持兼容）=====
    
    const DispatchWorkerConfig& get_config() const { return config_; }
    
    void update_config(const DispatchWorkerConfig& new_config);
    
    void set_execution_mode(ExecutionMode mode);
    
    void set_executor(std::shared_ptr<foundation::thread::IExecutor> executor);
    
    void set_poll_interval(std::chrono::milliseconds interval);
    
    // ===== 策略管理（保持兼容）=====
    
    void set_policy(std::shared_ptr<DispatchPolicy> policy);
    
    std::shared_ptr<DispatchPolicy> policy() const;
    
    // ===== 状态查询（保持兼容）=====
    
    bool is_running() const { return running_; }
    
    bool is_stopped() const { return stop_flag_; }
    
    ExecutionMode get_execution_mode() const { return config_.mode; }
    
    size_t get_queue_size() const;
    
    size_t get_processed_count() const { return stats_.total_events_processed; }
    
    size_t get_batch_count() const { return stats_.total_batches_processed; }
    
    std::chrono::steady_clock::duration get_idle_time() const;
    
    // ===== 组件访问（保持兼容）=====
    
    std::shared_ptr<EventQueue> get_queue() const;
    
    std::shared_ptr<SubscriptionManager> get_subscription_manager() const;
    
    std::shared_ptr<IEventDispatcher> get_dispatcher() const;
    
    
    // ===== 统计信息（保持兼容）=====
    
    struct DetailedStats {
        size_t queue_size;
        size_t total_processed;
        size_t total_batches;
        size_t max_queue_size;
        std::chrono::milliseconds idle_time_ms;
        bool is_running;
        ExecutionMode mode;
        
        // 添加字符串表示，方便调试
        std::string to_string() const {
            char buffer[256];
            snprintf(buffer, sizeof(buffer),
                    "Queue: %zu, Processed: %zu, Batches: %zu, MaxQueue: %zu, "
                    "Idle: %lldms, Running: %s, Mode: %d",
                    queue_size, total_processed, total_batches, max_queue_size,
                    static_cast<long long>(idle_time_ms.count()),
                    is_running ? "true" : "false",
                    static_cast<int>(mode));
            return std::string(buffer);
        }
    };
    
    DetailedStats get_detailed_stats() const;
    
    void reset_stats();
    
    // ===== 新增简洁接口（用于新代码）=====
    
    /**
     * @brief 提交事件（新接口，更简洁）
     */
    void submit(std::unique_ptr<Event> event);
    
    void submit(const EventFormat& event);
    
    /**
     * @brief 处理一次事件（新接口）
     */
    size_t process_once();
    
    /**
     * @brief 获取事件处理器（内部使用）
     */
    std::shared_ptr<EventProcessor> get_processor() const { return processor_; }
    
private:
    // ===== 内部实现方法 =====
    
    /**
     * @brief 初始化处理器
     */
    void init_processor(std::shared_ptr<EventQueue> queue,
                       std::shared_ptr<SubscriptionManager> subs,
                       std::shared_ptr<IEventDispatcher> disp);
    
    /**
     * @brief 工作线程主循环
     */
    void work_loop_internal();
    
    /**
     * @brief 同步处理循环
     */
    void sync_work_loop();
    
    /**
     * @brief 异步处理循环
     */
    void async_work_loop();
    
    /**
     * @brief 处理事件
     */
    size_t process_events_internal();
    
    /**
     * @brief 检查是否应该处理事件
     */
    bool should_process() const;
    
    /**
     * @brief 更新统计信息
     */
    void update_stats(size_t events_processed, bool is_batch = false);
    
    /**
     * @brief 应用配置
     */
    void apply_config();
};

// ===== 工厂函数（保持兼容）=====

std::unique_ptr<DispatchWorker> create_sync_worker(
    std::shared_ptr<EventQueue> queue_,
    std::shared_ptr<SubscriptionManager> subs_mgr_,
    std::shared_ptr<IEventDispatcher> dispatcher_ = nullptr,
    std::shared_ptr<DispatchStrategy> strategy = nullptr);

std::unique_ptr<DispatchWorker> create_async_worker(
    std::shared_ptr<EventQueue> queue_,
    std::shared_ptr<SubscriptionManager> subs_mgr_,
    std::shared_ptr<foundation::thread::IExecutor> executor = nullptr,
    std::shared_ptr<IEventDispatcher> dispatcher = nullptr,
    std::shared_ptr<DispatchStrategy> strategy = nullptr,
    size_t worker_threads = 1);

std::unique_ptr<DispatchWorker> create_high_performance_worker(
    std::shared_ptr<EventQueue> queue_,
    std::shared_ptr<SubscriptionManager> subs_mgr_,
    std::shared_ptr<foundation::thread::IExecutor> executor = nullptr);

} // namespace engine
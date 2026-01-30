#pragma once
#include <vector>
#include <functional>
#include "EventBus.hpp"  // 新的统一接口
#include "EventQueue.hpp"
#include "EventDispatcher.h"
#include "SubscriptionManager.h"
#include <memory>
#include <atomic>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace engine {

class DispatchController {
public:
    DispatchController(std::shared_ptr<EventQueue> queue,
                    std::shared_ptr<SubscriptionManager> subs_mgr,
                    std::shared_ptr<IEventDispatcher> dispatcher,
                    ExecutionMode mode,
                    std::shared_ptr<foundation::thread::IExecutor> executor_
                    )
                : queue_(queue),
                subs_mgr_(subs_mgr),
          stop_flag_(false),
          threadMode_(mode),
          executor_(executor_)
    {
        auto default_policy = std::make_shared<ImmediatePolicy>();
         // 在构造函数体内初始化
        dispatcher_ = std::shared_ptr<IEventDispatcher>(
            EventDispatcherFactory::create_async_default().release()
    );
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
    auto queue = queue_;
    auto dispatcher = dispatcher_;
    auto subs = subs_mgr_;

    if (threadMode_ == ExecutionMode::Async) {
        executor_->post([queue, dispatcher, subs]() {
            if (!queue || !dispatcher || !subs) return;

            auto queued_events = queue->poll_due_events(std::chrono::steady_clock::now());
            if (queued_events.empty()) return;

            // 直接逐个处理，不需要 vector
            for (auto& qe : queued_events) {
                if (std::holds_alternative<std::unique_ptr<Event>>(qe.event_data)) {
                    auto event = std::move(std::get<std::unique_ptr<Event>>(qe.event_data));
                    if (event) {
                        dispatcher->dispatch(std::move(event), *subs);
                    }
                }
                // 如果需要处理 EventFormat 类型，可以在这里添加
                else if (std::holds_alternative<engine::EventFormat>(qe.event_data)) {
                    auto event = std::get<engine::EventFormat>(qe.event_data);
                    dispatcher->dispatch(event);  // 使用另一个 dispatch 重载
                }
            }
        });
    } else {
        // Sync 模式同理
        auto queued_events = queue_->poll_due_events(std::chrono::steady_clock::now());
        if (!queued_events.empty()) {
            for (auto& qe : queued_events) {
                if (std::holds_alternative<std::unique_ptr<Event>>(qe.event_data)) {
                    auto event = std::move(std::get<std::unique_ptr<Event>>(qe.event_data));
                    if (event) {
                        dispatcher_->dispatch(std::move(event), *subs_mgr_);
                    }
                }
            }
        }
        cv_.notify_one();
    }
}
    std::shared_ptr<DispatchPolicy> policy() const {
        return policy_;
    }

private:
    std::shared_ptr<DispatchPolicy> policy_;
    // ================== 线程主循环 ==================
    // ===== run_loop 也改成统一调度 =====
void run_loop() {
    while (!stop_flag_) {
        {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(50));
        }

        if (threadMode_ == ExecutionMode::Sync) {
            auto queued_events = queue_->poll_due_events(
                std::chrono::steady_clock::now()
            );

            if (!queued_events.empty()) {
                std::vector<std::unique_ptr<Event>> engine_events;
                engine_events.reserve(queued_events.size());

                for (auto& qe : queued_events) {
                    if (std::holds_alternative<std::unique_ptr<Event>>(qe.event_data)) {
                        engine_events.push_back(
                            std::move(std::get<std::unique_ptr<Event>>(qe.event_data))
                        );
                    }
                }

                if (!engine_events.empty()) {
                    dispatcher_->dispatch(engine_events, *subs_mgr_);
                }
            }
        }
    }
}
private:
    std::shared_ptr<EventQueue> queue_;
    std::shared_ptr<SubscriptionManager> subs_mgr_;
    std::shared_ptr<IEventDispatcher> dispatcher_; 
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_;
    mutable std::mutex cv_mutex_;
    std::condition_variable cv_;
    ExecutionMode threadMode_;
    std::shared_ptr<foundation::thread::IExecutor> executor_;
};
class EventBusImpl : public EventBus {
public:
    // ===== 内部结构 =====
    struct FormatSubscription {
        foundation::Uuid id;
        std::string event_type;
        EventFormatHandler handler;
        EventFormatFilter filter;
        int priority;
        FormatSubscription(foundation::Uuid id_,std::string event_type_,EventFormatHandler handler_,EventFormatFilter filter_,int priority_)
        :id(id_),event_type(event_type_),handler(handler_),filter(filter_),priority(priority_){}
    };
    
    struct QueuedEvent {
        std::variant<std::unique_ptr<Event>, engine::EventFormat> event_data;
        int64_t enqueue_time_us;
        int priority;
        QueuedEvent(std::variant<std::unique_ptr<Event>, engine::EventFormat> event_data_,int64_t enqueue_time_,int priority_)
        :event_data(std::move(event_data_)),enqueue_time_us(enqueue_time_),priority(priority_){}
        // 添加默认构造函数
        QueuedEvent() 
            : enqueue_time_us(0)
            , priority(0) 
        {
            // event_data 会被默认构造为第一个类型（std::unique_ptr<Event>）
            // 但 index() 会是 0
        }
        bool operator<(const QueuedEvent& other) const {
            // 优先级高的先处理
            if (priority != other.priority) {
                return priority < other.priority;
            }
            // 同优先级按时间顺序
            return enqueue_time_us > other.enqueue_time_us;
        }
    };
    
    // ===== 构造函数 =====
    explicit EventBusImpl(const Config& config = Config());
    
    // ===== 析构函数 =====
    ~EventBusImpl() override;
    
    // 禁止拷贝
    EventBusImpl(const EventBusImpl&) = delete;
    EventBusImpl& operator=(const EventBusImpl&) = delete;
    
    // ===== EventBus 接口实现 =====
    
    // 原始 Event 接口
    Error publish(std::unique_ptr<Event> evt) override;
    foundation::Uuid subscribe(
        Event_Core::Type type,
        std::function<void(std::unique_ptr<Event>)> callback) override;
    Error unsubscribe(Event_Core::Type type, foundation::Uuid subscription_id) override;
    size_t dispatch() override;
    void clear() override;
    void set_policy(std::shared_ptr<DispatchPolicy> policy) override;
    std::shared_ptr<DispatchPolicy> policy() const override;
    void stop() override;
    void start() override;
    bool is_stopped() const override;
    void reset() override;
    
    // EventFormat 接口
    void publish(const engine::EventFormat& event) override;
    void publish_async(const engine::EventFormat& event) override;
    void publish_batch(const std::vector<engine::EventFormat>& events) override;
    foundation::Uuid subscribe(
        const std::string& event_type,
        EventFormatHandler handler,
        EventFormatFilter filter = nullptr,
        int priority = 0) override;
    bool unsubscribe(foundation::Uuid subscription_id) override;
    
    // 高级控制接口
    void wait_for_empty(double timeout_seconds = 5.0) override;
    void clear_queue() override;
    void set_drop_policy_on_full(bool drop_oldest) override;
    void add_global_filter(EventFormatFilter filter) override;
    size_t get_subscription_count() const override;
    size_t get_format_subscription_count() const override;
    bool is_running() const override;
    const Config& get_config() const override;
    
    // 工具方法
    std::unique_ptr<Event> convert_to_engine_event(
        const engine::EventFormat& fmt) override;
    std::optional<engine::EventFormat> convert_from_engine_event(
        const Event& evt) override;
    void register_event_type(const std::string& event_type, Event_Core::Type engine_type) override;
    std::optional<Event_Core::Type> get_mapped_engine_type(
        const std::string& event_type) const override;
    std::optional<std::string> get_mapped_event_type(
        Event_Core::Type engine_type) const override;
    
private:
    
    // 统计相关（在cpp文件中提到但头文件未声明）
    struct EventBusStats {
        std::atomic<int64_t> total_events_published{0};
        std::atomic<int64_t> total_events_processed{0};
        std::atomic<int64_t> total_format_events_published{0};
        std::atomic<int64_t> total_engine_events_published{0};
        std::atomic<int64_t> max_queue_size{0};
        std::atomic<int64_t> current_queue_size{0};
        
        void reset();
        std::string to_string() const;
    };
       // ============ 批量处理优化 ============
    void process_batch_events();  // 在cpp中有完整实现
    
    // ============ 线程安全的队列操作 ============
    bool try_publish(std::unique_ptr<Event> evt, std::chrono::milliseconds timeout);
    bool try_publish(const engine::EventFormat& event, std::chrono::milliseconds timeout);
    
    // ============ 优先级支持 ============
    void publish_with_priority(std::unique_ptr<Event> evt, int priority);
    void publish_with_priority(const engine::EventFormat& event, int priority);
    
    // ============ 订阅管理工具 ============
    std::vector<foundation::Uuid> get_format_subscriptions(
        const std::string& event_type) const;
    std::vector<foundation::Uuid> get_engine_subscriptions(
        Event_Core::Type type) const;
    bool has_format_subscriptions(const std::string& event_type) const;
    bool has_engine_subscriptions(Event_Core::Type type) const;
    
    // ============ 配置更新 ============
    void update_config(const Config& new_config);
    
    // ============ 错误处理和恢复 ============
    void set_exception_handler(std::function<void(const std::exception&)> handler);
    void recover_from_error();
    
    // ============ 序列化和反序列化支持 ============
    std::string serialize_state() const;
    bool deserialize_state(const std::string& state_json);
    
    // ============ 清理和优化 ============
    void cleanup_idle_subscriptions();
    void optimize_memory_usage();
    
    // ============ 调试和诊断 ============
    std::string get_diagnostic_info() const;
    std::vector<std::string> get_active_event_types() const;
    EventBusStats stats_;  // 统计对象需要声明
     // ============ 事件类型管理 ============
    std::vector<std::string> get_registered_event_types() const ;
    std::vector<Event_Core::Type> get_registered_engine_types() const ;
    bool is_event_type_registered(const std::string& event_type) const ;
    bool is_engine_type_registered(Event_Core::Type engine_type) const ;
    // 类型映射
    Event_Core::Type map_event_type(const std::string& event_type);
    
    // 事件处理
    void worker_thread_func();
    void process_event(const QueuedEvent& queued_event);
    void process_engine_event(Event* event);
    void process_format_event(const engine::EventFormat& event);
    
    // 转换辅助
    engine::EventFormat convert_attributes_to_format(
        const Event::Attributes& attrs,
        const std::string& event_type,
        const std::string& source,
        int64_t timestamp_us);
    
    // ===== 成员变量 =====
    Config config_;
    
    // 线程池
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    
    // 事件队列
    std::priority_queue<QueuedEvent> event_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 订阅管理
    std::unordered_map<Event_Core::Type,
        std::unordered_map<foundation::Uuid,
            std::function<void(std::unique_ptr<Event>)>>> engine_subscriptions_;
    mutable std::shared_mutex engine_subscriptions_mutex_;
    
    std::unordered_map<std::string, std::vector<FormatSubscription>> format_subscriptions_;
    std::unordered_map<foundation::Uuid, std::string> format_subscription_to_type_;
    mutable std::shared_mutex format_subscriptions_mutex_;
    
    // 全局过滤器
    std::vector<EventFormatFilter> global_filters_;
    mutable std::mutex filters_mutex_;
    
    // 类型映射
    std::unordered_map<std::string, Event_Core::Type> type_mapping_;
    std::unordered_map<Event_Core::Type, std::string> reverse_type_mapping_;
    std::atomic<Event_Core::Type> next_dynamic_type_ = Event_Core::Type::CUSTOM; // 动态类型从1000开始
    mutable std::mutex mapping_mutex_;
    
    // 原有组件（保持兼容）
    std::shared_ptr<EventQueue> event_queue_impl_;
    std::shared_ptr<IEventDispatcher> event_dispatcher_;
    std::unique_ptr<DispatchController> dispatch_controller_;
    std::shared_ptr<SubscriptionManager> subscription_manager_;
    
    // 队列策略
    std::atomic<bool> drop_oldest_on_full_{false};
    
    // 批量处理
    std::vector<engine::EventFormat> batch_buffer_;
};

} // namespace engine


// astock_engine/core/EventBusImpl.cpp
#include "Event/EventBusImpl.h"
#include "foundation/json/json_facade.h"
#include <chrono>
#include <algorithm>
#include "Event/Event.h"
#include <atomic>      // 主要头文件
#include <optional>


namespace engine {

// ============ 构造函数和析构函数 ============

EventBusImpl::EventBusImpl(const Config& config)
    : config_(config) {
    
    // 初始化原有组件
    event_queue_impl_ = std::make_shared<EventQueue>();
    // 正确3：根据配置选择
    if (config_.execution_mode == ExecutionMode::Sync) {
        event_dispatcher_ = EventDispatcherFactory::create_sync();
    } else {
        event_dispatcher_ = EventDispatcherFactory::create_async_default();
    }
    subscription_manager_ = std::make_shared<SubscriptionManager>();
    
    // 初始化分发控制器
    dispatch_controller_ = std::make_unique<DispatchController>(
        event_queue_impl_,
        subscription_manager_,
        event_dispatcher_,
        config.execution_mode,
        config.executor
    );
    
    // 初始化预定义类型映射
    register_event_type("system.startup", static_cast<Event_Core::Type>(1001));
    register_event_type("system.shutdown", static_cast<Event_Core::Type>(1002));
    register_event_type("market.tick", static_cast<Event_Core::Type>(2001));
    register_event_type("market.bar.1m", static_cast<Event_Core::Type>(2002));
    register_event_type("order.new", static_cast<Event_Core::Type>(3001));
    register_event_type("order.filled", static_cast<Event_Core::Type>(3002));
    
    // 如果需要，预启动线程
    if (config.execution_mode == ExecutionMode::Async) {
        start();
    }
}

EventBusImpl::~EventBusImpl() {
    stop();
    wait_for_empty(2.0);
}

// ============ 原始 Event 接口实现 ============

PublishResult EventBusImpl::publish(std::unique_ptr<Event> evt) {
    if (!evt) {
        return PublishResult{PublishError::DISPATCHER_NOT_RUNNING, "Null event"};
    }
    
    // 使用原有的事件队列
    event_queue_impl_->enqueue(std::move(evt));
    dispatch_controller_->notify();
    
    return PublishResult{PublishError::OK, ""}; // 成功
}

foundation::Uuid EventBusImpl::subscribe(
    Event_Core::Type type,
    std::function<void(std::unique_ptr<Event>)> callback) {
    
    // 使用原有的订阅管理器
    auto subscriber = std::make_shared<EventSubscriber>(std::move(callback), 
                                                        std::vector<Event_Core::Type>{type});
    return subscription_manager_->add_subscriber(subscriber);
}

bool EventBusImpl::unsubscribe(Event_Core::Type type, foundation::Uuid subscription_id) {
    // 注意：这里简化处理，实际可能需要更复杂的逻辑
    if (subscription_manager_->remove_subscriber(subscription_id)) {
        return true;
    }
    return false;
}

size_t EventBusImpl::dispatch() {
     size_t processed_count = 0;
    
    // 1. 首先处理你自己的事件队列
    {
        std::unique_lock lock(queue_mutex_);
        if (!event_queue_.empty()) {
            // 处理所有待处理事件
            while (!event_queue_.empty()) {
                auto queued_event = std::move(const_cast<QueuedEvent&>(event_queue_.top()));
                event_queue_.pop();
                
                lock.unlock();  // 解锁以便处理事件
                process_event(queued_event);
                processed_count++;
                lock.lock();    // 重新加锁
            }
        }
    }
    
    // 2. 然后处理原有的 EventQueue 系统
    if (event_queue_impl_ && dispatch_controller_) {
        // 触发原有系统的事件处理
        dispatch_controller_->notify();
        
        // 获取处理数量（可能需要添加统计功能）
        // processed_count += event_queue_impl_->get_processed_count();
    }
    
    return processed_count;
}

void EventBusImpl::clear() {
    if (event_queue_impl_) {
        // 清空原有队列
        // 这里需要根据 EventQueue 的实际接口调整
    }
    
    // 清空新队列
    clear_queue();
}

size_t EventBusImpl::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return event_queue_.size();
}

void EventBusImpl::set_policy(std::shared_ptr<DispatchPolicy> policy) {
    // 如果你有 policy_ 成员变量，直接设置
}

std::shared_ptr<DispatchPolicy> EventBusImpl::get_policy() const {
    return dispatch_controller_ ? dispatch_controller_->policy() : nullptr;
}

void EventBusImpl::stop(bool wait_completion, int timeout_ms) {
    stopping_ = true;
    running_ = false;
    
    // 停止原有控制器
    if (dispatch_controller_) {
        dispatch_controller_->stop();
    }
    
    // 停止工作线程
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_cv_.notify_all();
    }
    
    if (wait_completion) {
        // 等待所有线程完成
        auto deadline = std::chrono::milliseconds(timeout_ms);
        auto start = std::chrono::high_resolution_clock::now();
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    workers_.clear();
}

bool EventBusImpl::start() {
    if (running_) return true;
    
    stopping_ = false;
    running_ = true;
    
    // 启动原有控制器
    if (dispatch_controller_) {
        dispatch_controller_->start();
    }
    
    // 如果需要，启动 EventFormat 的工作线程
    if (config_.enable_event_format) {
        size_t thread_count = config_.worker_threads;
        if (thread_count == 0) {
            thread_count = std::thread::hardware_concurrency();
            if (thread_count == 0) thread_count = 1;
        }
        
        workers_.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back(&EventBusImpl::worker_thread_func, this);
        }
    }
    
    return true;
}

bool EventBusImpl::is_running() const {
    return running_;
}

void EventBusImpl::reset() {
    stop();
    clear();
    
    // 重置订阅
    {
        std::unique_lock<std::shared_mutex> lock1(engine_subscriptions_mutex_);
        std::unique_lock<std::shared_mutex> lock2(format_subscriptions_mutex_);
        engine_subscriptions_.clear();
        format_subscriptions_.clear();
    }
    
    // 重置原有组件
    if (subscription_manager_) {
        // 重置订阅管理器
    }
    
    // // 重置策略
    // if (dispatch_controller_) {
    //     dispatch_controller_->reset();
    // }
}

// ============ EventFormat 接口实现 ============

PublishResult EventBusImpl::publish(const engine::EventFormat& event, int priority) {
    if (!config_.enable_event_format) {
        // 如果未启用 EventFormat，尝试转换为原始 Event
        if (config_.auto_convert_formats) {
            auto engine_event = convert_to_engine_event(event);
            if (engine_event) {
                return publish(std::move(engine_event));
            }
        }
        return PublishResult{PublishError::DISPATCHER_NOT_RUNNING, "EventFormat not enabled"};
    }
    
    // 检查队列是否已满
    bool should_drop = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (event_queue_.size() >= config_.max_queue_size) {
            if (drop_oldest_on_full_) {
                event_queue_.pop();
                should_drop = true;
            } else {
                return PublishResult{PublishError::QUEUE_FULL, "Event queue is full"};
            }
        }
        
        if (!should_drop) {
            //event_queue_.push(QueuedEvent(event,engine::EventFormat::timestamp,0));
            event_queue_.emplace(event, event.timestamp, 0);
        }
    }
    
    if (!should_drop) {
        queue_cv_.notify_one();
        return PublishResult{PublishError::OK, ""};
    }
    return PublishResult{PublishError::QUEUE_FULL, "Event queue is full"};
}

size_t EventBusImpl::publish_batch(const std::vector<engine::EventFormat>& events) {
    if (!config_.enable_event_format || events.empty()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    size_t count = 0;
    for (const auto& event : events) {
        if (event_queue_.size() >= config_.max_queue_size) {
            if (drop_oldest_on_full_) {
                event_queue_.pop();
            } else {
                break; // 停止添加
            }
        }
        
        event_queue_.push(QueuedEvent(event,event.timestamp,0));
        count++;
    }
    
    if (count > 0) {
        queue_cv_.notify_all();
    }
    
    return count;
}

foundation::Uuid EventBusImpl::subscribe(
    const std::string& event_type,
    EventFormatHandler handler,
    EventFormatFilter filter,
    int priority) {
    
    if (!config_.enable_event_format) {
        // 如果未启用 EventFormat，返回空 UUID
        return foundation::Uuid::null();
    }
    
    foundation::Uuid id = foundation::Uuid::generate();
    
    {
        std::unique_lock<std::shared_mutex> lock(format_subscriptions_mutex_);
        format_subscriptions_[event_type].push_back(FormatSubscription(id,event_type,std::move(handler),std::move(filter),priority));
        
        format_subscription_to_type_[id] = event_type;
    }
    
    return id;
}

bool EventBusImpl::unsubscribe(foundation::Uuid subscription_id) {
    std::unique_lock<std::shared_mutex> lock(format_subscriptions_mutex_);
    
    auto type_it = format_subscription_to_type_.find(subscription_id);
    if (type_it == format_subscription_to_type_.end()) {
        return false;
    }
    
    const std::string& event_type = type_it->second;
    auto subs_it = format_subscriptions_.find(event_type);
    if (subs_it == format_subscriptions_.end()) {
        return false;
    }
    
    auto& subscriptions = subs_it->second;
    auto sub_it = std::remove_if(subscriptions.begin(), subscriptions.end(),
        [subscription_id](const FormatSubscription& sub) {
            return sub.id == subscription_id;
        });
    
    bool removed = (sub_it != subscriptions.end());
    if (removed) {
        subscriptions.erase(sub_it, subscriptions.end());
        format_subscription_to_type_.erase(type_it);
        
        // 如果该类型没有订阅者了，清理空条目
        if (subscriptions.empty()) {
            format_subscriptions_.erase(subs_it);
        }
    }
    
    return removed;
}

// ============ 高级控制接口 ============

bool EventBusImpl::wait_for_empty(double timeout_seconds) {
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::duration<double>(timeout_seconds);
    
    while (std::chrono::steady_clock::now() - start < timeout) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (event_queue_.empty()) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return false;
}

void EventBusImpl::clear_queue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!event_queue_.empty()) {
        event_queue_.pop();
    }
}

void EventBusImpl::set_drop_policy_on_full(bool drop_oldest) {
    drop_oldest_on_full_ = drop_oldest;
}

void EventBusImpl::add_global_filter(EventFormatFilter filter) {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    global_filters_.push_back(std::move(filter));
}

size_t EventBusImpl::get_subscription_count() const {
    // 原始 Event 订阅数量
    size_t count = 0;
    {
        std::shared_lock<std::shared_mutex> lock(engine_subscriptions_mutex_);
        for (const auto& [type, subscribers] : engine_subscriptions_) {
            count += subscribers.size();
        }
    }
    return count;
}

size_t EventBusImpl::get_format_subscription_count() const {
    std::shared_lock<std::shared_mutex> lock(format_subscriptions_mutex_);
    size_t count = 0;
    for (const auto& [event_type, subscriptions] : format_subscriptions_) {
        count += subscriptions.size();
    }
    return count;
}

const EventBus::Config& EventBusImpl::get_config() const {
    return config_;
}

// ============ 工具方法 ============

std::unique_ptr<Event> EventBusImpl::convert_to_engine_event(
    const engine::EventFormat& fmt) {
    
    // 映射事件类型
    Event_Core::Type engine_type = map_event_type(fmt.type);
    
    // 转换为属性
    auto attrs = fmt.to_attributes();
    
    // 创建 Event
    return Event::create(
        engine_type,
        foundation::Timestamp::from_microseconds(fmt.timestamp),
        std::move(attrs)
    );
}

std::optional<engine::EventFormat> EventBusImpl::convert_from_engine_event(
    const Event& evt) {
    
    // 获取映射的事件类型
    auto event_type = get_mapped_event_type(evt.type());
    if (!event_type) {
        return std::nullopt;
    }
    
    // 创建 EventFormat
    engine::EventFormat fmt(*event_type, evt.source());
    fmt.timestamp = evt.timestamp().to_microseconds();
    
    // 转换属性
    const auto& attrs = evt.attributes();
    for (const auto& [key, value] : attrs) {
        // 跳过内部字段
        if (key == "event_type" || key == "source" || key == "timestamp_us") {
            continue;
        }
        
        // 将 EventValue 转换为字符串
        std::string str_value = value.to_string();
        
        // 尝试解析类型
        try {
            // 尝试解析为 int64
            try {
                int64_t int_val = std::stoll(str_value);
                fmt.set(key, int_val);
                continue;
            } catch (...) {}
            
            // 尝试解析为 double
            try {
                double dbl_val = std::stod(str_value);
                fmt.set(key, dbl_val);
                continue;
            } catch (...) {}
            
            // 尝试解析为 bool
            if (str_value == "true" || str_value == "false") {
                fmt.set(key, str_value == "true");
                continue;
            }
            
            // 默认为字符串
            fmt.set(key, str_value);
        } catch (...) {
            fmt.set(key, str_value);
        }
    }
    
    return fmt;
}

void EventBusImpl::register_event_type(
    const std::string& event_type, Event_Core::Type engine_type) {
    
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    type_mapping_[event_type] = engine_type;
    reverse_type_mapping_[engine_type] = event_type;
}

std::optional<Event_Core::Type> EventBusImpl::get_mapped_engine_type(
    const std::string& event_type) const {
    
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    auto it = type_mapping_.find(event_type);
    if (it != type_mapping_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> EventBusImpl::get_mapped_event_type(
    Event_Core::Type engine_type) const {
    
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    auto it = reverse_type_mapping_.find(engine_type);
    if (it != reverse_type_mapping_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ============ 私有方法 ============

Event_Core::Type EventBusImpl::map_event_type(const std::string& event_type) {
   std::lock_guard<std::mutex> lock(mapping_mutex_);
    
    auto it = type_mapping_.find(event_type);
    if (it != type_mapping_.end()) {
        return it->second;
    }
    
    // 简单的非原子递增（因为已经有 mutex 保护）
    Event_Core::Type current_value = next_dynamic_type_.load();
    
    // 转换为整数递增
    auto current_int = static_cast<int>(current_value);
    auto new_int = current_int + 1;
    Event_Core::Type new_type = static_cast<Event_Core::Type>(new_int);
    
    // 存储新值
    next_dynamic_type_.store(new_type);
    
    type_mapping_[event_type] = new_type;
    reverse_type_mapping_[new_type] = event_type;
    
    return new_type;
}

void EventBusImpl::worker_thread_func() {
   while (!stopping_) {
    std::optional<QueuedEvent> queued_event_opt;  // 这是 std::optional
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this]() {
            return !event_queue_.empty() || stopping_;
        });
        
        if (stopping_ && event_queue_.empty()) {
            break;
        }
        
        if (!event_queue_.empty()) {
            // 对 std::optional 调用 emplace()
            queued_event_opt.emplace(
                std::move(const_cast<QueuedEvent&>(event_queue_.top()))
            );
            event_queue_.pop();
        }
    }
    
    if (queued_event_opt) {
        // 处理事件...
    }
}
}

void EventBusImpl::process_event(const QueuedEvent& queued_event) {
    std::visit([this](auto&& event) {
        using T = std::decay_t<decltype(event)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<Event>>) {
            process_engine_event(event.get());
        } else if constexpr (std::is_same_v<T, engine::EventFormat>) {
            process_format_event(event);
        }
    }, queued_event.event_data);
}

void EventBusImpl::process_engine_event(Event* event) {
    if (!event) return;
    
    // 应用全局过滤器（如果需要转换为 EventFormat）
    if (config_.auto_convert_formats) {
        auto fmt = convert_from_engine_event(*event);
        if (fmt) {
            std::lock_guard<std::mutex> lock(filters_mutex_);
            for (const auto& filter : global_filters_) {
                if (!filter(*fmt)) {
                    return; // 被过滤器拒绝
                }
            }
        }
    }
    
    // 查找订阅者
    std::shared_lock<std::shared_mutex> lock(engine_subscriptions_mutex_);
    auto it = engine_subscriptions_.find(event->type());
    if (it != engine_subscriptions_.end()) {
        for (const auto& [id, callback] : it->second) {
            if (callback) {
                try {
                    callback(event->clone());
                } catch (...) {
                    // 记录错误但继续处理
                }
            }
        }
    }
}

void EventBusImpl::process_format_event(const engine::EventFormat& event) {
    // 应用全局过滤器
    {
        std::lock_guard<std::mutex> lock(filters_mutex_);
        for (const auto& filter : global_filters_) {
            if (!filter(event)) {
                return; // 被过滤器拒绝
            }
        }
    }
    
    // 查找订阅者
    std::shared_lock<std::shared_mutex> lock(format_subscriptions_mutex_);
    auto it = format_subscriptions_.find(event.type);
    if (it != format_subscriptions_.end()) {
        for (const auto& subscription : it->second) {
            // 应用订阅者过滤器
            if (subscription.filter && !subscription.filter(event)) {
                continue;
            }
            
            if (subscription.handler) {
                try {
                    subscription.handler(event);
                } catch (...) {
                    // 记录错误但继续处理
                }
            }
        }
    }
    
    // 如果需要，自动转换为原始 Event
    if (config_.auto_convert_formats) {
        auto engine_event = convert_to_engine_event(event);
        if (engine_event) {
            process_engine_event(engine_event.get());
        }
    }
}

engine::EventFormat EventBusImpl::convert_attributes_to_format(
    const Event::Attributes& attrs,
    const std::string& event_type,
    const std::string& source,
    int64_t timestamp_us) {
    
    engine::EventFormat fmt(event_type, source, timestamp_us);
    
    for (const auto& [key, value] : attrs) {
        // 跳过内部字段
        if (key == "event_type" || key == "source" || key == "timestamp_us") {
            continue;
        }
        
        // 将 EventValue 转换为字符串
        std::string str_value = value.to_string();
        
        // 尝试解析类型
        try {
            // 尝试解析为 int64
            try {
                int64_t int_val = std::stoll(str_value);
                fmt.set(key, int_val);
                continue;
            } catch (...) {}
            
            // 尝试解析为 double
            try {
                double dbl_val = std::stod(str_value);
                fmt.set(key, dbl_val);
                continue;
            } catch (...) {}
            
            // 尝试解析为 bool
            if (str_value == "true" || str_value == "false") {
                fmt.set(key, str_value == "true");
                continue;
            }
            
            // 默认为字符串
            fmt.set(key, str_value);
        } catch (...) {
            fmt.set(key, str_value);
        }
    }
    
    return fmt;
}

// ============ 工厂方法实现 ============

std::unique_ptr<EventBus> EventBus::create(
    const Config& config,
    std::shared_ptr<foundation::thread::IExecutor> executor) {
    
    Config actual_config = config;
    actual_config.executor = executor;
    actual_config.enable_event_format = true; // 默认启用
    actual_config.auto_convert_formats = true;
    
    return std::make_unique<EventBusImpl>(actual_config);
}

// ============ 全局事件总线实现 ============

EventBus& GlobalEventBus::instance() {
    static auto global_bus = EventBus::create();
    return *global_bus;
}

void GlobalEventBus::start_default() {
    instance().start();
}

void GlobalEventBus::stop_default() {
    instance().stop();
}

bool GlobalEventBus::is_running() {
    return instance().is_running();
}

// ============ 批量处理优化 ============

void EventBusImpl::process_batch_events() {
    std::vector<QueuedEvent> batch;
    batch.reserve(config_.batch_size);
    
    // 从队列中取出批量事件
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (size_t i = 0; i < config_.batch_size && !event_queue_.empty(); ++i) {
            batch.push_back(std::move(const_cast<QueuedEvent&>(event_queue_.top())));
            event_queue_.pop();
        }
    }
    
    // 按类型分组处理
    std::unordered_map<std::string, std::vector<engine::EventFormat>> format_batches;
    std::unordered_map<Event_Core::Type, std::vector<std::unique_ptr<Event>>> engine_batches;
    
    for (auto& queued_event : batch) {
        std::visit([&](auto&& event) {
            using T = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<Event>>) {
                if (event) {
                    engine_batches[event->type()].push_back(std::move(event));
                }
            } else if constexpr (std::is_same_v<T, engine::EventFormat>) {  // 修正类型名
                format_batches[event.type].push_back(std::move(event));  // 使用 event.type
            }
        }, queued_event.event_data);
    }
    
    // 批量处理 EventFormat 事件
    for (auto& [event_type, events] : format_batches) {
        std::shared_lock<std::shared_mutex> lock(format_subscriptions_mutex_);
        auto it = format_subscriptions_.find(event_type);  // 修正：使用 event_type
        
        if (it != format_subscriptions_.end()) {
            for (const auto& subscription : it->second) {
                if (!subscription.handler) continue;
                
                for (const auto& event : events) {  // 使用 const 引用
                    // 应用过滤器
                    if (subscription.filter && !subscription.filter(event)) {
                        continue;
                    }
                    
                    try {
                        subscription.handler(event);
                    } catch (...) {
                        // 错误处理
                        LOG_ERROR("Error in batch event handler for type: {}", event_type);
                    }
                }
            }
        }
    }
    
    // 批量处理原始 Event 事件
    for (auto& [event_type, events] : engine_batches) {
        std::shared_lock<std::shared_mutex> lock(engine_subscriptions_mutex_);
        auto it = engine_subscriptions_.find(event_type);
        
        if (it != engine_subscriptions_.end()) {
            for (const auto& [id, callback] : it->second) {
                if (!callback) continue;
                
                // 创建事件的副本（如果 Event 支持拷贝）
                for (auto& event : events) {
                    try {
                        // 尝试不同的调用方式
                        // 方式1：如果 callback 接受 unique_ptr
                        auto event_copy = event->clone();  // 假设有 clone() 方法
                        callback(std::move(event_copy));
                        
                        // 方式2：如果 callback 接受原始指针（不推荐）
                        // callback(event.get());
                        
                        // 方式3：如果 Event 不可拷贝，只能处理一次
                        // callback(std::move(event));  // 这会清空 vector
                    } catch (...) {
                        LOG_ERROR("Error in batch engine event handler for type: {}", 
                                 static_cast<int>(event_type));
                    }
                }
            }
        }
    }
}

// ============ 性能监控和统计 ============

struct EventBusStats {
    std::atomic<int64_t> total_events_published{0};
    std::atomic<int64_t> total_events_processed{0};
    std::atomic<int64_t> total_format_events_published{0};
    std::atomic<int64_t> total_engine_events_published{0};
    std::atomic<int64_t> max_queue_size{0};
    std::atomic<int64_t> current_queue_size{0};
    
    void reset() {
        total_events_published = 0;
        total_events_processed = 0;
        total_format_events_published = 0;
        total_engine_events_published = 0;
        max_queue_size = 0;
        current_queue_size = 0;
    }
    
    std::string to_string() const {
        std::ostringstream oss;
        oss << "EventBusStats{"
            << "total_published=" << total_events_published
            << ", total_processed=" << total_events_processed
            << ", format_events=" << total_format_events_published
            << ", engine_events=" << total_engine_events_published
            << ", max_queue=" << max_queue_size
            << ", current_queue=" << current_queue_size
            << "}";
        return oss.str();
    }
};

// ============ 线程安全的队列操作 ============

bool EventBusImpl::try_publish(std::unique_ptr<Event> evt, 
                              std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start < timeout) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (event_queue_.size() < config_.max_queue_size) {
                event_queue_.push(QueuedEvent(std::move(evt),evt->timestamp().to_microseconds(),
                    0
                ));
                queue_cv_.notify_one();
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return false;
}

bool EventBusImpl::try_publish(const engine::EventFormat& event,
                              std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start < timeout) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (event_queue_.size() < config_.max_queue_size) {
                event_queue_.push(QueuedEvent(event,event.timestamp,0 ));
                queue_cv_.notify_one();
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return false;
}

// ============ 优先级支持 ============

void EventBusImpl::publish_with_priority(std::unique_ptr<Event> evt, int priority) {
    if (!evt) return;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (event_queue_.size() >= config_.max_queue_size) {
            if (drop_oldest_on_full_) {
                event_queue_.pop();
            } else {
                return;
            }
        }
        
        event_queue_.push(QueuedEvent(std::move(evt),evt->timestamp().to_seconds(),priority));
    }
    
    queue_cv_.notify_one();
}

void EventBusImpl::publish_with_priority(const engine::EventFormat& event, int priority) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (event_queue_.size() >= config_.max_queue_size) {
            if (drop_oldest_on_full_) {
                event_queue_.pop();
            } else {
                return;
            }
        }
        
        event_queue_.push(QueuedEvent(event, event.timestamp,priority));
    }
    
    queue_cv_.notify_one();
}

// ============ 事件类型管理 ============

std::vector<std::string> EventBusImpl::get_registered_event_types() const {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    std::vector<std::string> types;
    types.reserve(type_mapping_.size());
    
    for (const auto& [type, _] : type_mapping_) {
        types.push_back(type);
    }
    
    return types;
}

std::vector<Event_Core::Type> EventBusImpl::get_registered_engine_types() const {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    std::vector<Event_Core::Type> types;
    types.reserve(reverse_type_mapping_.size());
    
    for (const auto& [type, _] : reverse_type_mapping_) {
        types.push_back(type);
    }
    
    return types;
}

bool EventBusImpl::is_event_type_registered(const std::string& event_type) const {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    return type_mapping_.find(event_type) != type_mapping_.end();
}

bool EventBusImpl::is_engine_type_registered(Event_Core::Type engine_type) const {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    return reverse_type_mapping_.find(engine_type) != reverse_type_mapping_.end();
}

// ============ 订阅管理工具 ============

std::vector<foundation::Uuid> EventBusImpl::get_format_subscriptions(
    const std::string& event_type) const {
    
    std::shared_lock<std::shared_mutex> lock(format_subscriptions_mutex_);
    auto it = format_subscriptions_.find(event_type);
    if (it == format_subscriptions_.end()) {
        return {};
    }
    
    std::vector<foundation::Uuid> ids;
    ids.reserve(it->second.size());
    for (const auto& sub : it->second) {
        ids.push_back(sub.id);
    }
    
    return ids;
}

std::vector<foundation::Uuid> EventBusImpl::get_engine_subscriptions(
    Event_Core::Type type) const {
    
    std::shared_lock<std::shared_mutex> lock(engine_subscriptions_mutex_);
    auto it = engine_subscriptions_.find(type);
    if (it == engine_subscriptions_.end()) {
        return {};
    }
    
    std::vector<foundation::Uuid> ids;
    ids.reserve(it->second.size());
    for (const auto& [id, _] : it->second) {
        ids.push_back(id);
    }
    
    return ids;
}

bool EventBusImpl::has_format_subscriptions(const std::string& event_type) const {
    std::shared_lock<std::shared_mutex> lock(format_subscriptions_mutex_);
    auto it = format_subscriptions_.find(event_type);
    return it != format_subscriptions_.end() && !it->second.empty();
}

bool EventBusImpl::has_engine_subscriptions(Event_Core::Type type) const {
    std::shared_lock<std::shared_mutex> lock(engine_subscriptions_mutex_);
    auto it = engine_subscriptions_.find(type);
    return it != engine_subscriptions_.end() && !it->second.empty();
}

// ============ 配置更新 ============

void EventBusImpl::update_config(const Config& new_config) {
    bool needs_restart = false;
    
    // 检查是否需要重启
    if (config_.execution_mode != new_config.execution_mode ||
        config_.worker_threads != new_config.worker_threads) {
        needs_restart = true;
    }
    
    // 更新配置
    config_ = new_config;
    
    // 如果需要重启
    if (needs_restart && running_) {
        stop();
        start();
    }
    
    // 更新队列策略
    drop_oldest_on_full_ = false; // 可以从配置中读取
}

// ============ 错误处理和恢复 ============

void EventBusImpl::set_exception_handler(
    std::function<void(const std::exception&)> handler) {
    // 存储异常处理器
}

void EventBusImpl::recover_from_error() {
    // 清理错误状态
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    // 可以添加错误恢复逻辑，比如：
    // 1. 清理损坏的事件
    // 2. 重置内部状态
    // 3. 重新启动工作线程
}

// ============ 序列化和反序列化支持 ============

std::string EventBusImpl::serialize_state() const {
    // 序列化当前状态（用于持久化）
    foundation::json::JsonFacade json = foundation::json::JsonFacade::createObject();
    
    // 序列化配置
    auto config_obj = foundation::json::JsonFacade::createObject();
    config_obj.set("worker_threads", 
                  foundation::json::JsonFacade::createInt(config_.worker_threads));
    config_obj.set("max_queue_size",
                  foundation::json::JsonFacade::createInt(config_.max_queue_size));
    config_obj.set("enable_event_format",
                  foundation::json::JsonFacade::createBool(config_.enable_event_format));
    
    json.set("config", config_obj);
    
    // 序列化类型映射
    auto mapping_array = foundation::json::JsonFacade::createArray();
    {
        std::lock_guard<std::mutex> lock(mapping_mutex_);
        for (const auto& [event_type, engine_type] : type_mapping_) {
            auto mapping_obj = foundation::json::JsonFacade::createObject();
            mapping_obj.set("event_type", 
                          foundation::json::JsonFacade::createString(event_type));
            mapping_obj.set("engine_type",
                          foundation::json::JsonFacade::createInt(static_cast<int>(engine_type)));
            mapping_array.push_back(mapping_obj);
        }
    }
    
    json.set("type_mappings", mapping_array);
    
    return json.toString();
}

bool EventBusImpl::deserialize_state(const std::string& state_json) {
    try {
        auto json = foundation::json::JsonFacade::parse(state_json);
        if (json.empty() || !json.isObject()) {
            return false;
        }
        
        // 解析配置
        if (json.has("config") && json.get("config").isObject()) {
            auto config_obj = json.get("config");
            if (config_obj.has("worker_threads")) {
                config_.worker_threads = config_obj.get("worker_threads").asInt();
            }
            if (config_obj.has("max_queue_size")) {
                config_.max_queue_size = config_obj.get("max_queue_size").asInt();
            }
            if (config_obj.has("enable_event_format")) {
                config_.enable_event_format = config_obj.get("enable_event_format").asBool();
            }
        }
        
        // 解析类型映射
        if (json.has("type_mappings") && json.get("type_mappings").isArray()) {
            std::lock_guard<std::mutex> lock(mapping_mutex_);
            type_mapping_.clear();
            reverse_type_mapping_.clear();
            
            auto mapping_array = json.get("type_mappings");
            for (size_t i = 0; i < mapping_array.size(); ++i) {
                auto mapping_obj = mapping_array.at(i);
                if (mapping_obj.isObject() &&
                    mapping_obj.has("event_type") &&
                    mapping_obj.has("engine_type")) {
                    
                    std::string event_type = mapping_obj.get("event_type").asString();
                    Event_Core::Type engine_type = static_cast<Event_Core::Type>(
                        mapping_obj.get("engine_type").asInt());
                    
                    type_mapping_[event_type] = engine_type;
                    reverse_type_mapping_[engine_type] = event_type;
                }
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

// ============ 清理和优化 ============

void EventBusImpl::cleanup_idle_subscriptions() {
    // 清理长时间未使用的订阅
    // 可以根据需要实现订阅超时机制
}

void EventBusImpl::optimize_memory_usage() {
    // 优化内存使用
    // 1. 压缩队列
    // 2. 清理缓冲区
    // 3. 释放未使用的资源
    
    batch_buffer_.shrink_to_fit();
}

// ============ 调试和诊断 ============

std::string EventBusImpl::get_diagnostic_info() const {
    std::ostringstream oss;
    
    oss << "EventBus Diagnostic Info:\n";
    oss << "=========================\n";
    oss << "Config:\n";
    oss << "  Worker Threads: " << config_.worker_threads << "\n";
    oss << "  Max Queue Size: " << config_.max_queue_size << "\n";
    oss << "  Execution Mode: " 
        << (config_.execution_mode == ExecutionMode::Sync ? "Sync" : "Async") << "\n";
    oss << "  Enable EventFormat: " << (config_.enable_event_format ? "Yes" : "No") << "\n";
    oss << "  Auto Convert: " << (config_.auto_convert_formats ? "Yes" : "No") << "\n";
    
    oss << "\nCurrent State:\n";
    oss << "  Running: " << (running_ ? "Yes" : "No") << "\n";
    oss << "  Stopping: " << (stopping_ ? "Yes" : "No") << "\n";
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        oss << "  Queue Size: " << event_queue_.size() << "\n";
    }
    
    oss << "  Format Subscriptions: " << get_format_subscription_count() << "\n";
    oss << "  Engine Subscriptions: " << get_subscription_count() << "\n";
    
    {
        std::lock_guard<std::mutex> lock(mapping_mutex_);
        oss << "  Registered Types: " << type_mapping_.size() << "\n";
    }
    
    return oss.str();
}

std::vector<std::string> EventBusImpl::get_active_event_types() const {
    std::vector<std::string> active_types;
    
    // 获取有订阅者的事件类型
    {
        std::shared_lock<std::shared_mutex> lock(format_subscriptions_mutex_);
        for (const auto& [event_type, subscriptions] : format_subscriptions_) {
            if (!subscriptions.empty()) {
                active_types.push_back(event_type);
            }
        }
    }
    
    // 获取有原始 Event 订阅的事件类型
    {
        std::lock_guard<std::mutex> lock(mapping_mutex_);
        std::shared_lock<std::shared_mutex> sub_lock(engine_subscriptions_mutex_);
        
        for (const auto& [engine_type, _] : reverse_type_mapping_) {
            auto it = engine_subscriptions_.find(engine_type);
            if (it != engine_subscriptions_.end() && !it->second.empty()) {
                auto type_str = reverse_type_mapping_.at(engine_type);
                if (std::find(active_types.begin(), active_types.end(), type_str) == 
                    active_types.end()) {
                    active_types.push_back(type_str);
                }
            }
        }
    }
    
    return active_types;
}

} // namespace engine

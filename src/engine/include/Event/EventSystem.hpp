// astock_engine/core/EventSystem.hpp
#pragma once

//#include "EventFormat.h"
#include "EventQueue.hpp"
#include "EventDispatcher.h"
#include "SubscriptionManager.h"
#include "DispatchWorker.h"
#include "foundation/json/json_facade.h"
#include "foundation/config/ConfigManager.hpp"
#include "DispatchPolicy.h"
#include <memory>
#include <string>
#include <unordered_map> 
#include <atomic>
#include <functional>
#include <chrono>
#include "Event/Event.h"
#include "DispatchConfig.h"
namespace engine {
    class EventQueue;
    class IEventDispatcher;
    class SubscriptionManager;
    class DispatchWorker;
    class DispatchStrategy;
}
namespace engine {

/**
 * @brief 事件系统主类 - 综合事件管理、分发和监控
 * @note 提供完整的JSON序列化支持，便于配置持久化和状态监控
 */
class EventSystem {
public:
    // ===== 构造函数和析构函数 =====
    
    /**
     * @brief 默认构造函数
     * @param config_name 配置名称，用于从配置管理器加载配置
     */
    explicit EventSystem(const std::string& config_name = "event_system");
    
    /**
     * @brief 带配置构造函数
     * @param config_json 配置JSON
     */
    explicit EventSystem(const foundation::json::JsonFacade& config_json);
    
    /**
     * @brief 析构函数
     */
    ~EventSystem();
    
    // 禁止拷贝
    EventSystem(const EventSystem&) = delete;
    EventSystem& operator=(const EventSystem&) = delete;
    
    // 允许移动
    EventSystem(EventSystem&&) = default;
    EventSystem& operator=(EventSystem&&) = default;
    
    // ===== 生命周期管理 =====
    
    /**
     * @brief 初始化事件系统
     * @param config_name 配置名称
     * @return 是否初始化成功
     */
    bool initialize(const std::string& config_name = "event_system");
    
    /**
     * @brief 启动事件系统
     * @return 是否启动成功
     */
    bool start();
    
    /**
     * @brief 停止事件系统
     * @param wait_completion 是否等待所有事件处理完成
     * @param timeout_ms 等待超时时间（毫秒）
     */
    void stop(bool wait_completion = true, int timeout_ms = 5000);
    
    /**
     * @brief 重新配置事件系统
     * @param config_json 新的配置
     * @return 是否重新配置成功
     */
    bool reconfigure(const foundation::json::JsonFacade& config_json);
    
    // ===== 事件发布 =====
    
    /**
     * @brief 发布事件（EventFormat格式）
     * @param event 事件
     * @param priority 优先级（0-10，0最高）
     * @return 是否发布成功
     */
    bool publish_event(const engine::EventFormat& event, int priority = 5);
    
    /**
     * @brief 发布事件（JSON格式）
     * @param event_json 事件JSON
     * @param priority 优先级（0-10，0最高）
     * @return 是否发布成功
     */
    bool publish_json(const foundation::json::JsonFacade& event_json, int priority = 5);
    
    /**
     * @brief 批量发布事件
     * @param events 事件列表
     * @return 成功发布的数量
     */
    size_t publish_batch(const std::vector<engine::EventFormat>& events);
    
    /**
     * @brief 立即处理所有待处理事件（同步）
     * @return 处理的事件数量
     */
    size_t flush();
    
    // ===== 订阅管理 =====
    
    /**
     * @brief 订阅事件类型
     * @param event_type 事件类型
     * @param handler 事件处理器
     * @param subscriber_id 订阅者ID
     * @return 订阅ID，用于取消订阅
     */
    std::string subscribe(const std::string& event_type,
                          std::function<void(const engine::EventFormat&)> handler,
                          const std::string& subscriber_id = "");
    
    /**
     * @brief 订阅事件类型（正则表达式匹配）
     * @param pattern 正则表达式模式
     * @param handler 事件处理器
     * @param subscriber_id 订阅者ID
     * @return 订阅ID，用于取消订阅
     */
    std::string subscribe_pattern(const std::string& pattern,
                                  std::function<void(const engine::EventFormat&)> handler,
                                  const std::string& subscriber_id = "");
    
    /**
     * @brief 取消订阅
     * @param subscription_id 订阅ID
     */
    void unsubscribe(const std::string& subscription_id);
    
    /**
     * @brief 取消订阅者的所有订阅
     * @param subscriber_id 订阅者ID
     */
    void unsubscribe_all(const std::string& subscriber_id);
    
    // ===== 状态查询 =====
    
    /**
     * @brief 检查事件系统是否正在运行
     */
    bool is_running() const { return running_; }
    
    /**
     * @brief 获取当前队列大小
     */
    size_t get_queue_size() const;
    
    /**
     * @brief 获取已处理事件总数
     */
    uint64_t get_processed_count() const;
    
    /**
     * @brief 获取待处理事件数量
     */
    size_t get_pending_count() const;
    
    /**
     * @brief 获取事件系统启动时间
     */
    std::chrono::system_clock::time_point get_start_time() const { return start_time_; }
    
    /**
     * @brief 获取事件系统运行时长（毫秒）
     */
    std::chrono::milliseconds get_uptime() const;
    
    // ===== JSON序列化接口 =====
    
    /**
     * @brief 导出系统配置到JSON
     */
    foundation::json::JsonFacade export_config() const;
    
    /**
     * @brief 从JSON导入系统配置
     */
    bool import_config(const foundation::json::JsonFacade& config_json);
    
    /**
     * @brief 获取系统运行时状态（JSON格式）
     */
    foundation::json::JsonFacade get_status_json() const;
    
    /**
     * @brief 获取系统统计信息（JSON格式）
     */
    foundation::json::JsonFacade get_statistics_json() const;
    
    /**
     * @brief 导出当前订阅关系（JSON格式）
     */
    foundation::json::JsonFacade export_subscriptions_json() const;
    
    /**
     * @brief 导入订阅关系（JSON格式）
     */
    bool import_subscriptions_json(const foundation::json::JsonFacade& subscriptions_json);
    
    /**
     * @brief 导出队列中的事件（JSON格式）
     * @param max_events 最多导出的事件数量，0表示导出所有
     */
    foundation::json::JsonFacade export_events_json(size_t max_events = 100) const;
    
    /**
     * @brief 从JSON导入事件到队列
     */
    bool import_events_json(const foundation::json::JsonFacade& events_json);
    
    /**
     * @brief 将事件格式转换为JSON
     */
    static foundation::json::JsonFacade event_to_json(const engine::EventFormat& event);
    
    /**
     * @brief 从JSON创建事件格式
     */
    static std::optional<engine::EventFormat> json_to_event(const foundation::json::JsonFacade& json);
    
    /**
     * @brief 保存系统状态到文件
     * @param filepath 文件路径
     * @param include_events 是否包含队列中的事件
     * @param include_subscriptions 是否包含订阅关系
     * @return 是否保存成功
     */
    bool save_state_to_file(const std::string& filepath,
                           bool include_events = false,
                           bool include_subscriptions = true);
    
    /**
     * @brief 从文件恢复系统状态
     * @param filepath 文件路径
     * @return 是否恢复成功
     */
    bool load_state_from_file(const std::string& filepath);
    
    // ===== 配置管理 =====
    
    /**
     * @brief 获取事件系统配置
     */
    const foundation::config::ConfigNode::Ptr& get_config() const { return config_; }
    
    /**
     * @brief 更新配置项
     * @param key 配置键
     * @param value 配置值
     * @param persist 是否持久化到配置文件
     */
    void update_config(const std::string& key, 
                      const foundation::json::JsonFacade& value,
                      bool persist = false);
    
    /**
     * @brief 设置性能监控开关
     */
    void enable_monitoring(bool enable);
    
    /**
     * @brief 设置批处理大小
     */
    void set_batch_size(size_t batch_size);
    
    /**
     * @brief 设置工作线程数量
     */
    void set_worker_threads(size_t num_threads);
    
    /**
     * @brief 设置执行模式（同步/异步）
     */
    void set_execution_mode(ExecutionMode mode);
    
    // ===== 事件处理 =====
    
    /**
     * @brief 注册全局事件过滤器
     * @param filter 过滤器函数，返回true表示允许通过
     */
    void add_event_filter(std::function<bool(const engine::EventFormat&)> filter);
    
    /**
     * @brief 移除事件过滤器
     */
    void remove_event_filter(size_t index);
    
    /**
     * @brief 设置事件处理器线程池
     */
    void set_handler_executor(std::shared_ptr<foundation::thread::IExecutor> executor);
    
    /**
     * @brief 等待所有事件处理完成
     * @param timeout_ms 超时时间（毫秒）
     * @return 是否所有事件都已处理完成
     */
    bool wait_for_completion(int timeout_ms = 5000);
    
    // ===== 诊断和监控 =====
    
    /**
     * @brief 获取性能监控数据（JSON格式）
     */
    foundation::json::JsonFacade get_performance_metrics() const;
    
    /**
     * @brief 重置统计信息
     */
    void reset_statistics();
    
    /**
     * @brief 获取事件类型统计
     */
    std::unordered_map<std::string, uint64_t> get_event_type_stats() const;
    
    /**
     * @brief 获取订阅者统计
     */
    std::unordered_map<std::string, size_t> get_subscriber_stats() const;
    
    /**
     * @brief 生成系统健康报告（JSON格式）
     */
    foundation::json::JsonFacade get_health_report() const;
    
private:
    // ===== 内部类型定义 =====
    struct SystemStats {
        uint64_t total_events_published{0};
        uint64_t total_events_processed{0};
        uint64_t total_events_dropped{0};
        uint64_t max_queue_size{0};
        std::chrono::microseconds total_processing_time{0};
        std::chrono::microseconds max_processing_time{0};
        std::chrono::system_clock::time_point last_reset_time;
        
        // 事件类型统计
        std::unordered_map<std::string, uint64_t> event_type_counts;
        
        // 订阅者统计
        std::unordered_map<std::string, size_t> subscriber_counts;
        
        void reset() {
            total_events_published = 0;
            total_events_processed = 0;
            total_events_dropped = 0;
            max_queue_size = 0;
            total_processing_time = std::chrono::microseconds(0);
            max_processing_time = std::chrono::microseconds(0);
            last_reset_time = std::chrono::system_clock::now();
            event_type_counts.clear();
            subscriber_counts.clear();
        }
    };
     // 更新统计信息
    void update_statistics(const EventFormat& event) {
        std::unique_lock lock(stats_mutex_);
        stats_.total_events_published++;
        stats_.event_type_counts[event.type]++;  // 使用字符串类型统计
    }
    std::unique_ptr<Event> convert_using_format_methods(const EventFormat& event);
    std::unique_ptr<Event> convert_to_engine_event(
        const EventFormat& event, 
        int priority_override);
    std::string convert_source(const Event_Core::EventSource& source);
    Event_Core::Type convert_event_type(const std::string& event_type_str);
    // ===== 内部方法 =====
    
    /**
     * @brief 初始化组件
     */
    bool initialize_components();
    
    /**
     * @brief 从配置加载设置
     */
    bool load_configuration();
    
    /**
     * @brief 创建默认配置
     */
    foundation::json::JsonFacade create_default_config() const;
    
    /**
     * @brief 应用过滤器
     */
    bool apply_filters(const engine::EventFormat& event);
    
    /**
     * @brief 更新统计信息
     */
    void update_stats(const engine::EventFormat& event, 
                     std::chrono::microseconds processing_time);
    
    /**
     * @brief 保存配置到文件
     */
    bool save_config_to_file() const;
    
    /**
     * @brief 序列化订阅关系
     */
    foundation::json::JsonFacade serialize_subscriptions() const;
    
    /**
     * @brief 反序列化订阅关系
     */
    bool deserialize_subscriptions(const foundation::json::JsonFacade& json);
    
    // ===== 成员变量 =====
    
    // 核心组件
    std::shared_ptr<EventQueue> event_queue_;
    std::shared_ptr<IEventDispatcher> event_dispatcher_;
    std::shared_ptr<SubscriptionManager> subscription_manager_;
    std::shared_ptr<DispatchWorker> dispatch_worker_;
    std::shared_ptr<DispatchPolicy> dispatch_strategy_;
    DispatchConfig dispatch_config_;
    // 配置
    std::string config_name_;
    foundation::config::ConfigNode::Ptr config_;
    std::string config_file_path_;
    
    // 状态
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point initialization_time_;
    
    // 统计
    mutable std::shared_mutex stats_mutex_;
    SystemStats stats_;
    
    // 过滤器
    std::vector<std::function<bool(const engine::EventFormat&)>> event_filters_;
    
    // 执行器
    std::shared_ptr<foundation::thread::IExecutor> handler_executor_;
    
    // 性能监控
    bool enable_performance_monitoring_{false};
    std::unordered_map<std::string, std::chrono::microseconds> handler_execution_times_;
    
    // 订阅ID映射
    std::unordered_map<std::string, std::vector<std::string>> subscriber_to_subscriptions_;
};

// ===== 工具函数 =====

/**
 * @brief 创建默认事件系统
 */
std::unique_ptr<EventSystem> create_default_event_system();

/**
 * @brief 创建高性能事件系统
 */
std::unique_ptr<EventSystem> create_high_performance_event_system(
    size_t worker_threads = 4,
    size_t batch_size = 100);

/**
 * @brief 创建轻量级事件系统（用于测试或简单场景）
 */
std::unique_ptr<EventSystem> create_lightweight_event_system();

/**
 * @brief 从配置文件创建事件系统
 */
std::unique_ptr<EventSystem> create_event_system_from_file(
    const std::string& config_file_path);

/**
 * @brief 事件系统管理器（管理多个事件系统实例）
 */
class EventSystemManager {
public:
    static EventSystemManager& instance();
    
    /**
     * @brief 注册事件系统
     */
    bool register_system(const std::string& name, std::shared_ptr<EventSystem> system);
    
    /**
     * @brief 获取事件系统
     */
    std::shared_ptr<EventSystem> get_system(const std::string& name);
    
    /**
     * @brief 移除事件系统
     */
    bool remove_system(const std::string& name);
    
    /**
     * @brief 获取所有系统状态（JSON格式）
     */
    foundation::json::JsonFacade get_all_status_json() const;
    
    /**
     * @brief 启动所有系统
     */
    void start_all();
    
    /**
     * @brief 停止所有系统
     */
    void stop_all();
    
private:
    EventSystemManager() = default;
    ~EventSystemManager() = default;
    
    std::unordered_map<std::string, std::shared_ptr<EventSystem>> systems_;
    mutable std::shared_mutex mutex_;
};

} // namespace engine
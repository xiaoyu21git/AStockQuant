// astock_engine/core/EventBus.hpp
#pragma once
#include "EventFormat.hpp"
#include "EventSystem.hpp"
#include "foundation/Utils/Uuid.h"
#include "foundation/thread/IExecutor.h"
#include "Event.h"
#include "DispatchPolicy.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "BaseInterface.h"
namespace engine {

// ============ 前置声明 ============
class Event;
struct Error;
// ============ EventFormat 处理器类型 ============
using EventFormatHandler = std::function<void(const engine::EventFormat&)>;
using EventFormatFilter = std::function<bool(const engine::EventFormat&)>;

// ============ 统一的 EventBus 接口 ============
class EventBus {
public:
    // ===== 配置结构 =====
    struct Config {
        // 线程配置
        size_t worker_threads = 4;                     // 工作线程数
        ExecutionMode execution_mode = ExecutionMode::Sync; // 执行模式
        
        // 队列配置
        size_t max_queue_size = 10000;                 // 最大队列大小
        bool enable_priority_queue = true;             // 启用优先级队列
        size_t batch_size = 100;                       // 批量处理大小
        
        // 执行器
        std::shared_ptr<foundation::thread::IExecutor> executor;
        
        // 事件格式配置
        bool enable_event_format = true;               // 启用 EventFormat 支持
        bool auto_convert_formats = true;              // 自动转换 EventFormat ↔ Event
        
        Config() = default;
    };
    
    // ===== 构造函数和析构函数 =====
    virtual ~EventBus() = default;
    
    // ===== 工厂方法（保持兼容） =====
    static std::unique_ptr<EventBus> create(
        std::shared_ptr<foundation::thread::IExecutor> executor = nullptr);
    
    // =============================================
    // 第一部分：原始 Event 接口（完全保持现有API）
    // =============================================
    
    /**
     * @brief 发布原始 Event
     */
    virtual Error publish(std::unique_ptr<Event> evt) = 0;
    
    /**
     * @brief 订阅原始 Event
     * @param type 事件类型
     * @param callback 回调函数
     * @return 订阅ID
     */
    virtual foundation::Uuid subscribe(
        Event_Core::Type type,
        std::function<void(std::unique_ptr<Event>)> callback) = 0;
    
    /**
     * @brief 取消订阅原始 Event
     */
    virtual Error unsubscribe(Event_Core::Type type, foundation::Uuid subscription_id) = 0;
    
    /**
     * @brief 分发事件（同步模式）
     * @return 处理的事件数量
     */
    virtual size_t dispatch() = 0;
    
    /**
     * @brief 清空所有事件
     */
    virtual void clear() = 0;
    
    /**
     * @brief 设置分发策略
     */
    virtual void set_policy(std::shared_ptr<DispatchPolicy> policy) = 0;
    
    /**
     * @brief 获取当前分发策略
     */
    virtual std::shared_ptr<DispatchPolicy> policy() const = 0;
    
    /**
     * @brief 停止事件总线
     */
    virtual void stop() = 0;
    
    /**
     * @brief 启动事件总线
     */
    virtual void start() = 0;
    
    /**
     * @brief 检查是否已停止
     */
    virtual bool is_stopped() const = 0;
    
    /**
     * @brief 重置事件总线
     */
    virtual void reset() = 0;
    
    // =============================================
    // 第二部分：EventFormat 扩展接口
    // =============================================
    
    /**
     * @brief 发布 EventFormat（同步）
     */
    virtual void publish(const engine::EventFormat& event) = 0;
    
    /**
     * @brief 发布 EventFormat（异步）
     */
    virtual void publish_async(const engine::EventFormat& event) = 0;
    
    /**
     * @brief 批量发布 EventFormat
     */
    virtual void publish_batch(const std::vector<engine::EventFormat>& events) = 0;
    
    /**
     * @brief 订阅 EventFormat
     * @param event_type 事件类型字符串
     * @param handler 处理器函数
     * @param filter 过滤器（可选）
     * @param priority 优先级（0最高，默认0）
     * @return 订阅ID
     */
    virtual foundation::Uuid subscribe(
        const std::string& event_type,
        EventFormatHandler handler,
        EventFormatFilter filter = nullptr,
        int priority = 0) = 0;
    
    /**
     * @brief 取消订阅 EventFormat
     * @return 是否成功取消
     */
    virtual bool unsubscribe(foundation::Uuid subscription_id) = 0;
    
    // =============================================
    // 第三部分：高级控制接口
    // =============================================
    
    /**
     * @brief 等待队列为空
     * @param timeout_seconds 超时时间（秒）
     */
    virtual void wait_for_empty(double timeout_seconds = 5.0) = 0;
    
    /**
     * @brief 清空事件队列
     */
    virtual void clear_queue() = 0;
    
    /**
     * @brief 设置队列满时的丢弃策略
     * @param drop_oldest true:丢弃最旧事件, false:阻塞（默认）
     */
    virtual void set_drop_policy_on_full(bool drop_oldest) = 0;
    
    /**
     * @brief 添加全局过滤器
     */
    virtual void add_global_filter(EventFormatFilter filter) = 0;
    
    /**
     * @brief 获取订阅数量
     */
    virtual size_t get_subscription_count() const = 0;
    
    /**
     * @brief 获取 EventFormat 订阅数量
     */
    virtual size_t get_format_subscription_count() const = 0;
    
    /**
     * @brief 检查是否正在运行
     */
    virtual bool is_running() const = 0;
    
    /**
     * @brief 获取配置
     */
    virtual const Config& get_config() const = 0;
    
    // =============================================
    // 第四部分：工具方法
    // =============================================
    
    /**
     * @brief 将 EventFormat 转换为 Event
     */
    virtual std::unique_ptr<Event> convert_to_engine_event(
        const engine::EventFormat& fmt) = 0;
    
    /**
     * @brief 将 Event 转换为 EventFormat
     */
    virtual std::optional<engine::EventFormat> convert_from_engine_event(
        const Event& evt) = 0;
    
    /**
     * @brief 注册事件类型映射
     */
    virtual void register_event_type(const std::string& event_type, Event_Core::Type engine_type) = 0;
    
    /**
     * @brief 获取事件类型映射
     */
    virtual std::optional<Event_Core::Type> get_mapped_engine_type(
        const std::string& event_type) const = 0;
    
    /**
     * @brief 获取事件类型字符串
     */
    virtual std::optional<std::string> get_mapped_event_type(
        Event_Core::Type engine_type) const = 0;
};

// ============ 全局事件总线单例 ============
class GlobalEventBus {
public:
    /**
     * @brief 获取全局事件总线实例
     */
    static EventBus& instance();
    
    /**
     * @brief 启动全局事件总线
     */
    static void start_default();
    
    /**
     * @brief 停止全局事件总线
     */
    static void stop_default();
    
    /**
     * @brief 检查全局事件总线是否运行
     */
    static bool is_running();
    
private:
    GlobalEventBus() = delete;
};

} // namespace engine
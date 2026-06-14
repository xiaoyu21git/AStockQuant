// astock_engine/core/EventBus.hpp
#pragma once
#include "EventFormat.hpp"
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
class EventSystem;  // ✅ 前置声明，隐藏实现细节
struct Error;

// ============ EventFormat 处理器类型 ============
using EventFormatHandler = std::function<void(const engine::EventFormat&)>;
using EventFormatFilter = std::function<bool(const engine::EventFormat&)>;

// ✅ 新增：统一的错误结果类型
enum class PublishError {
    OK,
    QUEUE_FULL,
    INVALID_SUBSCRIPTION,
    DISPATCHER_NOT_RUNNING,
    SERIALIZATION_FAILED,
    INTERNAL_ERROR
};

struct PublishResult {
    PublishError error = PublishError::OK;
    std::string message;
    
    explicit operator bool() const { return error == PublishError::OK; }
};

// ============ 统一的 EventBus 接口 ============
/**
 * @brief EventBus - 统一的事件总线接口（公开API）
 * 
 * EventBus 是外部唯一的访问入口，隐藏所有内部实现细节。
 * 内部使用 EventSystem 进行实际的事件管理。
 * 
 * 职责：
 * ✅ 提供统一的事件发布/订阅接口
 * ✅ 支持 Event 和 EventFormat 两种格式
 * ✅ 生命周期管理
 * ✅ 配置管理
 * ❌ 不处理具体的分发逻辑（交给 EventSystem）
 */
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
        
        // ✅ 新增：流控配置
        bool drop_oldest_on_full = false;              // 队列满时的策略
        
        Config() = default;
    };
    
    // ===== 构造函数和析构函数 =====
    virtual ~EventBus() = default;
    
    // ===== 工厂方法（保持兼容） =====
    static std::unique_ptr<EventBus> create(
        const Config& config = Config(),
        std::shared_ptr<foundation::thread::IExecutor> executor = nullptr);
    
    // =============================================
    // 生命周期管理
    // =============================================
    
    /**
     * @brief 启动事件总线
     * @return 是否成功启动
     */
    virtual bool start() = 0;
    
    /**
     * @brief 停止事件总线
     * @param wait_completion 是否等待所有事件处理完成
     * @param timeout_ms 等待超时时间（毫秒）
     */
    virtual void stop(bool wait_completion = true, int timeout_ms = 5000) = 0;
    
    /**
     * @brief 检查是否已启动
     */
    virtual bool is_running() const = 0;
    
    /**
     * @brief 重置事件总线（清空队列和订阅）
     */
    virtual void reset() = 0;
    
    // =============================================
    // Event 接口（原始事件，保持向后兼容）
    // =============================================
    
    /**
     * @brief 发布原始 Event
     * @param evt 事件对象
     * @return 发布结果
     */
    virtual PublishResult publish(std::unique_ptr<Event> evt) = 0;
    
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
     * @return 是否成功取消
     */
    virtual bool unsubscribe(Event_Core::Type type, foundation::Uuid subscription_id) = 0;
    
    // =============================================
    // EventFormat 接口（新统一格式）
    // =============================================
    
    /**
     * @brief 发布 EventFormat（同步）
     * @param event 事件格式
     * @param priority 优先级（0最高）
     * @return 发布结果
     */
    virtual PublishResult publish(
        const engine::EventFormat& event, 
        int priority = 5) = 0;
    
    /**
     * @brief 批量发布 EventFormat
     * @param events 事件列表
     * @return 成功发布的事件数
     */
    virtual size_t publish_batch(const std::vector<engine::EventFormat>& events) = 0;
    
    /**
     * @brief 订阅 EventFormat
     * @param event_type 事件类型字符串
     * @param handler 处理器函数
     * @param filter 过滤器（可选）
     * @param priority 优先级（0最高）
     * @return 订阅ID
     */
    virtual foundation::Uuid subscribe(
        const std::string& event_type,
        EventFormatHandler handler,
        EventFormatFilter filter = nullptr,
        int priority = 0) = 0;
    
    /**
     * @brief 取消订阅 EventFormat
     * @param subscription_id 订阅ID
     * @return 是否成功取消
     */
    virtual bool unsubscribe(foundation::Uuid subscription_id) = 0;
    
    // =============================================
    // 分发和处理
    // =============================================
    
    /**
     * @brief 手动分发队列中的事件（同步模式）
     * @return 处理的事件数量
     */
    virtual size_t dispatch() = 0;
    
    /**
     * @brief 等待队列为空
     * @param timeout_seconds 超时时间（秒）
     * @return 是否在超时前完成
     */
    virtual bool wait_for_empty(double timeout_seconds = 5.0) = 0;
    
    // =============================================
    // 队列管理
    // =============================================
    
    /**
     * @brief 清空事件队列
     */
    virtual void clear_queue() = 0;
    
    /**
     * @brief 获取当前队列大小
     */
    virtual size_t queue_size() const = 0;
    
    /**
     * @brief 设置队列满时的丢弃策略
     * @param drop_oldest true:丢弃最旧事件, false:阻塞（默认）
     */
    virtual void set_drop_policy_on_full(bool drop_oldest) = 0;
    
    // =============================================
    // 分发策略管理
    // =============================================
    
    /**
     * @brief 设置分发策略
     * @param policy 新的分发策略
     */
    virtual void set_policy(std::shared_ptr<DispatchPolicy> policy) = 0;
    
    /**
     * @brief 获取当前分发策略
     */
    virtual std::shared_ptr<DispatchPolicy> get_policy() const = 0;
    
    // =============================================
    // 统计和监控
    // =============================================
    
    /**
     * @brief 获取订阅数量
     */
    virtual size_t get_subscription_count() const = 0;
    
    /**
     * @brief 获取 EventFormat 订阅数量
     */
    virtual size_t get_format_subscription_count() const = 0;
    
    /**
     * @brief 获取配置
     */
    virtual const Config& get_config() const = 0;
    
protected:
    // ✅ EventBus 隐藏内部实现，不暴露 EventSystem
    // 使用原始指针避免 forward declaration 问题
    EventSystem* system_ = nullptr;
};


} // namespace engine
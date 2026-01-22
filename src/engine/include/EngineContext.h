#pragma once

#include <memory>
#include <string>
#include <map>
#include <any>
#include "Event.h"
#include "foundation.h"
using Duration = foundation::Duration;
using Timestamp = foundation::Timestamp;
namespace engine {

// 前向声明
class Engine;
class Clock;
class EventBus;
class DataSource;
class Event;
/**
 * @brief 引擎运行时上下文
 * 
 * 提供引擎运行时的环境信息和控制接口，传递给各个模块使用
 * 统一管理运行时的共享状态和资源
 */
class EngineContext {
public:
    virtual ~EngineContext() = default;
        // 必须要有公共构造函数（或工厂方法能访问的构造函数）
    explicit EngineContext() = default;
    // 禁止拷贝
    EngineContext(const EngineContext&) = delete;
    EngineContext& operator=(const EngineContext&) = delete;
    
    /**
     * @brief 获取引擎实例
     * @return 引擎指针
     * 
     * 提供对主引擎的访问，用于执行高级操作
     */
    virtual Engine* engine() = 0;
    
    /**
     * @brief 获取引擎实例（const版本）
     * @return const引擎指针
     */
    virtual const Engine* engine() const = 0;
    
    /**
     * @brief 获取当前时间
     * @return 当前时间戳
     * 
     * 从引擎的时钟获取当前时间
     * 根据不同的时钟模式（回测/实时）返回对应的时间
     */
    virtual Timestamp current_time() const = 0;
    
    /**
     * @brief 获取时钟对象
     * @return 时钟指针
     * 
     * 提供对时钟的直接访问，用于时间相关操作
     */
    virtual Clock* clock() = 0;
    
    /**
     * @brief 获取事件总线
     * @return 事件总线指针
     * 
     * 用于发布和订阅事件
     */
    virtual EventBus* event_bus() = 0;
    
    /**
     * @brief 查找数据源
     * @param name 数据源名称
     * @return 数据源指针，如果不存在返回nullptr
     * 
     * 通过名称查找已注册的数据源
     */
    virtual DataSource* find_data_source(const std::string& name) = 0;
    
    /**
     * @brief 获取所有数据源名称
     * @return 数据源名称列表
     */
    virtual std::vector<std::string> all_data_source_names() const = 0;
    
    /**
     * @brief 便捷方法：发布事件
     * @param event 事件对象
     * @return 成功或错误
     * 
     * 通过事件总线发布事件，比直接使用event_bus()->publish()更方便
     */
    virtual Error publish_event(std::unique_ptr<Event> event) = 0;
    
    /**
     * @brief 设置用户数据
     * @param key 数据键
     * @param value 数据值（任意类型）
     * 
     * 用于在模块间共享自定义数据
     * 例如：配置参数、中间计算结果等
     */
    virtual void set_user_data(const std::string& key, std::any value) = 0;
    
    /**
     * @brief 获取用户数据
     * @param key 数据键
     * @return 数据值，如果不存在返回空的any
     */
    virtual std::any get_user_data(const std::string& key) const = 0;
    
    /**
     * @brief 检查用户数据是否存在
     * @param key 数据键
     * @return 是否存在
     */
    virtual bool has_user_data(const std::string& key) const = 0;
    
    /**
     * @brief 删除用户数据
     * @param key 数据键
     * @return 是否成功删除
     */
    virtual bool remove_user_data(const std::string& key) = 0;
    
    /**
     * @brief 获取所有用户数据键
     * @return 键列表
     */
    virtual std::vector<std::string> all_user_data_keys() const = 0;
    
    /**
     * @brief 设置引擎状态标记
     * @param flag 标记名称
     * @param value 标记值
     * 
     * 用于标记引擎的运行时状态，如：是否在回测、是否暂停等
     * 与用户数据不同，这些是预定义的引擎状态
     */
    virtual void set_engine_flag(const std::string& flag, bool value) = 0;
    
    /**
     * @brief 获取引擎状态标记
     * @param flag 标记名称
     * @return 标记值，如果不存在返回false
     */
    virtual bool get_engine_flag(const std::string& flag) const = 0;
    
    /**
     * @brief 获取引擎配置参数
     * @param key 参数键
     * @return 参数值，如果不存在返回空字符串
     * 
     * 提供对引擎配置参数的访问
     */
    virtual std::string get_config_param(const std::string& key) const = 0;
    
    /**
     * @brief 获取引擎运行统计
     * @return 统计信息字符串（可格式化为JSON或文本）
     * 
     * 获取引擎运行的统计信息，如处理事件数量、运行时间等
     */
    virtual std::string get_runtime_stats() const = 0;
    /**
     * @brief 检查引擎是否正在运行
     * @return 是否运行中
     */
    virtual bool is_engine_running() const = 0;
    
    /**
     * @brief 检查引擎是否处于回测模式
     * @return 是否回测模式
     */
    virtual bool is_backtest_mode() const = 0;
    
    /**
     * @brief 检查引擎是否处于实时模式
     * @return 是否实时模式
     */
    virtual bool is_realtime_mode() const = 0;
    
    /**
     * @brief 获取上下文ID
     * @return 上下文唯一标识符
     * 
     * 用于区分不同的引擎上下文实例
     * 在多引擎场景下特别有用
     */
    virtual foundation::Uuid context_id() const = 0;
    
    /**
     * @brief 获取引擎启动时间
     * @return 启动时间戳
     */
    virtual Timestamp engine_start_time() const = 0;
    
    /**
     * @brief 获取引擎运行时长
     * @return 运行时长
     */
    virtual Duration engine_uptime() const = 0;
    
    /**
     * @brief 创建引擎上下文
     * @param engine 引擎实例
     * @return 上下文指针
     * 
     * 工厂方法，由引擎在初始化时创建并管理
     */
    static std::unique_ptr<EngineContext> create(Engine* engine);

    
};

} // namespace engine
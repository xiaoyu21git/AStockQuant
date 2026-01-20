#pragma once
#include "BaseInterface.h"
#include "IClock.h"
#include "EventBus.h"
#include "IDataSource.h"
#include "ITrigger.h"
#include "Event.h"
namespace engine {

class EngineListener {
public:
    virtual ~EngineListener() = default;
    
    /**
     * @brief 引擎状态变化回调
     * @param old_state 旧状态
     * @param new_state 新状态
     */
    enum class State { Created, Initialized, Running, Paused, Stopped, Error };
    virtual void on_state_changed(State old_state, State new_state) = 0;
    
    /**
     * @brief 引擎错误回调
     * @param error 错误信息
     */
    virtual void on_error(const Error& error) = 0;
    
    /**
     * @brief 统计数据更新回调
     * @param stats 统计信息
     */
    struct Statistics {
        size_t total_events_processed;
        size_t total_triggers_fired;
        Duration total_runtime;
        Timestamp start_time;
    };
    virtual void on_statistics_updated(const Statistics& stats) = 0;
};

class Engine {
public:
    // 禁止拷贝
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    virtual ~Engine() = default;
    
    /**
     * @brief 初始化引擎
     * @param config 配置参数
     * @return 成功或错误
     * 
     * 初始化引擎内部组件，必须在使用前调用
     * 只能调用一次，重复调用返回错误
     */
    struct Config {
        Clock::Mode time_mode = Clock::Mode::Realtime;
        Duration event_dispatch_interval = Duration(10'000'000); // 10ms
        size_t max_pending_events = 10000;
        bool enable_trigger_system = true;
        std::map<std::string, std::string> parameters;
    };
    virtual Error initialize(const Config& config) = 0;
    
    /**
     * @brief 启动引擎
     * @return 成功或错误
     * 
     * 启动时钟和数据源，开始事件处理循环
     * 引擎必须在初始化后调用
     */
    virtual Error start() = 0;
    
    /**
     * @brief 停止引擎
     * @return 成功或错误
     * 
     * 停止所有活动，等待当前事件处理完成
     * 优雅停止，可以重新启动
     */
    virtual Error stop() = 0;
    
    /**
     * @brief 暂停引擎
     * @return 成功或错误
     * 
     * 暂停事件处理，但保持内部状态
     * 可以继续（continue）而不需要重新初始化
     */
    virtual Error pause() = 0;
    
    /**
     * @brief 继续引擎
     * @return 成功或错误
     * 
     * 从暂停状态恢复运行
     */
    virtual Error resume() = 0;
    
    /**
     * @brief 重置引擎
     * @return 成功或错误
     * 
     * 重置引擎到初始状态，清空所有数据和状态
     * 需要重新初始化才能使用
     */
    virtual Error reset() = 0;
    
    /**
     * @brief 获取当前状态
     * @return 引擎状态
     */
    virtual EngineListener::State state() const = 0;
    
    /**
     * @brief 获取时钟对象
     * @return 时钟指针
     */
    virtual Clock* clock() = 0;
    
    /**
     * @brief 获取事件总线
     * @return 事件总线指针
     */
    virtual EventBus* event_bus() = 0;
    
    // --- 数据源管理 ---
    
    /**
     * @brief 注册数据源
     * @param data_source 数据源对象
     * @return 成功或错误
     * 
     * 将数据源注册到引擎，引擎接管所有权
     * 数据源产生的事件会自动发布到事件总线
     */
    virtual Error register_data_source(std::unique_ptr<DataSource> data_source) = 0;
    
    /**
     * @brief 注销数据源
     * @param name 数据源名称
     * @return 成功或错误
     */
    virtual Error unregister_data_source(const std::string& name) = 0;
    
    /**
     * @brief 获取数据源列表
     * @return 数据源名称列表
     */
    virtual std::vector<std::string> data_source_names() const = 0;
    
    /**
     * @brief 获取数据源
     * @param name 数据源名称
     * @return 数据源指针，如果不存在返回nullptr
     */
    virtual DataSource* get_data_source(const std::string& name) = 0;
    
    // --- 触发器管理 ---
    
    /**
     * @brief 注册触发器
     * @param trigger 触发器对象
     * @return 成功或错误
     * 
     * 将触发器注册到引擎，引擎接管所有权
     * 触发器会自动订阅相关事件并执行
     */
    virtual Error register_trigger(std::unique_ptr<Trigger> trigger) = 0;
    
    /**
     * @brief 注销触发器
     * @param id 触发器ID
     * @return 成功或错误
     */
    virtual Error unregister_trigger(const foundation::utils::Uuid& id) = 0;
    
    /**
     * @brief 获取触发器列表
     * @return 触发器ID列表
     */
    virtual std::vector<foundation::utils::Uuid> trigger_ids() const = 0;
    
    /**
     * @brief 获取触发器
     * @param id 触发器ID
     * @return 触发器指针，如果不存在返回nullptr
     */
    virtual Trigger* get_trigger(const foundation::Uuid& id) = 0;
    
    // --- 监听器管理 ---
    
    /**
     * @brief 注册引擎监听器
     * @param listener 监听器指针
     */
    virtual void register_listener(EngineListener* listener) = 0;
    
    /**
     * @brief 取消注册引擎监听器
     * @param listener 监听器指针
     */
    virtual void unregister_listener(EngineListener* listener) = 0;
    
    // --- 统计信息 ---
    
    /**
     * @brief 获取统计信息
     * @return 当前统计信息
     */
    virtual EngineListener::Statistics statistics() const = 0;
    
    // --- 工具方法 ---
    
    /**
     * @brief 手动发布事件
     * @param event 事件对象
     * @return 成功或错误
     * 
     * 手动发布事件到引擎，绕过数据源
     * 可用于测试或手动触发场景
     */
    virtual Error publish_event(std::unique_ptr<Event> event) = 0;
    
    /**
     * @brief 获取配置信息
     * @return 当前配置
     */
    virtual const Config& config() const = 0;
    
    /**
     * @brief 创建引擎实例
     * @return 引擎指针
     */
    static std::unique_ptr<Engine> create() ;
protected:
    // 保护性构造函数，只能被派生类调用
    Engine() = default;
};
} // namespace engine
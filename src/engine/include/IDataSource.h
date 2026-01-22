#pragma once
#include "BaseInterface.h"
#include "Event.h"
#include "foundation.h"
using Duration = foundation::Duration;
using Timestamp = foundation::Timestamp;
namespace engine {

class DataListener {
public:
    virtual ~DataListener() = default;
    
    /**
     * @brief 数据接收回调
     * @param event 数据事件
     * 
     * 当数据源有新数据时调用
     * 数据源负责将原始数据转换为Event
     */
    virtual void on_data_received(std::unique_ptr<Event> event) = 0;
    
    /**
     * @brief 状态变化回调
     * @param old_state 旧状态
     * @param new_state 新状态
     */
    enum class State { Disconnected, Connecting, Connected, Error };
    virtual void on_state_changed(State old_state, State new_state) = 0;
};

class DataSource {
public:
    /**
     * @brief 构造函数
     * @param name 数据源名称
     * @param uri 数据源地址
     */
    DataSource(std::string name, std::string uri);
    virtual ~DataSource() = default;
    
    /**
     * @brief 连接数据源
     * @return 成功或错误
     * 
     * 建立与数据源的连接，开始接收数据
     */
    virtual Error connect() = 0;
    
    /**
     * @brief 断开连接
     * @return 成功或错误
     * 
     * 关闭与数据源的连接，停止接收数据
     */
    virtual Error disconnect() = 0;
    
    /**
     * @brief 轮询数据（主动拉取模式）
     * @return 成功或错误
     * 
     * 对于不支持推送的数据源，需要主动拉取数据
     */
    virtual Error poll() = 0;
    
    /**
     * @brief 获取数据源名称
     * @return 名称字符串
     */
    virtual std::string name() const = 0;
    
    /**
     * @brief 获取数据源URI
     * @return URI字符串
     */
    virtual std::string uri() const = 0;
    
    /**
     * @brief 获取当前状态
     * @return 连接状态
     */
    virtual DataListener::State state() const = 0;
    
    /**
     * @brief 注册数据监听器
     * @param listener 监听器指针
     */
    virtual void register_listener(DataListener* listener) = 0;
    
    /**
     * @brief 取消注册监听器
     * @param listener 监听器指针
     */
    virtual void unregister_listener(DataListener* listener) = 0;
    
    /**
     * @brief 配置轮询间隔
     * @param interval 轮询间隔
     */
    virtual void set_poll_interval(Duration interval) = 0;
    
    /**
     * @brief 创建数据源实例
     * @param name 数据源名称
     * @param uri 数据源地址
     * @return 数据源指针
     */
    static std::unique_ptr<DataSource> create(const std::string& name, const std::string& uri);
protected:
    std::string name_;
    std::string uri_;
};

} // namespace engine
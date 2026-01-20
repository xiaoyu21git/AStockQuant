#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <chrono>
#include <functional>
#include <shared_mutex>
#include <atomic>

#include "foundation/include/foundation.h"
#include "core/DataType.h"

namespace astock {
namespace market {
namespace observer {

/**
 * @brief 数据事件类型枚举
 */
enum class DataEventType {
    KLINE_UPDATE,       // K线更新
    KLINE_COMPLETE,     // K线完成（周期结束）
    TICK_UPDATE,        // Tick更新
    SYMBOL_ADDED,       // 新增交易对
    SYMBOL_REMOVED,     // 移除交易对
    DATA_ERROR,         // 数据错误
    CONNECTION_CHANGE,  // 连接状态变化
    MARKET_OPEN,        // 市场开盘
    MARKET_CLOSE,       // 市场收盘
    CUSTOM_EVENT        // 自定义事件
};

/**
 * @brief 数据事件结构
 */
struct DataEvent {
    DataEventType type;              // 事件类型
    std::string symbol;              // 交易对符号
    uint64_t timestamp;              // 事件时间戳
    uint64_t event_id;               // 事件ID（唯一标识）
    std::string source;              // 事件源
    std::string message;             // 事件消息
    
    // 事件数据（根据类型使用不同的字段）
    union {
        struct {
            uint16_t period;         // K线周期
            const foundation::KLine* kline_data;  // K线数据指针
        } kline_event;
        
        struct {
            const foundation::TickData* tick_data;  // Tick数据指针
        } tick_event;
        
        struct {
            bool is_connected;       // 连接状态
            std::string reason;      // 原因
        } connection_event;
        
        struct {
            int error_code;          // 错误码
            std::string error_msg;   // 错误信息
        } error_event;
    };
    
    // 自定义数据
    std::map<std::string, std::string> custom_data;
    
    DataEvent() : timestamp(0), event_id(0), type(DataEventType::CUSTOM_EVENT) {
        kline_event.kline_data = nullptr;
        tick_event.tick_data = nullptr;
    }
    
    bool is_kline_event() const {
        return type == DataEventType::KLINE_UPDATE || 
               type == DataEventType::KLINE_COMPLETE;
    }
    
    bool is_tick_event() const {
        return type == DataEventType::TICK_UPDATE;
    }
    
    bool is_error_event() const {
        return type == DataEventType::DATA_ERROR;
    }
    
    bool is_connection_event() const {
        return type == DataEventType::CONNECTION_CHANGE;
    }
    
    std::string to_string() const;
};

/**
 * @brief 数据观察者接口
 */
class IDataObserver {
public:
    virtual ~IDataObserver() = default;
    
    /**
     * @brief 处理数据事件
     * @param event 数据事件
     */
    virtual void on_data_event(const DataEvent& event) = 0;
    
    /**
     * @brief 获取观察者名称
     */
    virtual std::string get_name() const = 0;
    
    /**
     * @brief 获取观察者描述
     */
    virtual std::string get_description() const = 0;
    
    /**
     * @brief 检查观察者是否激活
     */
    virtual bool is_active() const = 0;
    
    /**
     * @brief 激活/停用观察者
     */
    virtual void set_active(bool active) = 0;
};

/**
 * @brief 数据事件总线（事件分发器）
 */
class DataEventBus {
public:
    using ObserverPtr = std::shared_ptr<IDataObserver>;
    using ObserverList = std::vector<ObserverPtr>;
    using EventHandler = std::function<void(const DataEvent&)>;
    
    /**
     * @brief 注册观察者（全局观察所有事件）
     */
    void register_observer(ObserverPtr observer);
    
    /**
     * @brief 注册观察者（指定事件类型）
     */
    void register_observer(ObserverPtr observer, DataEventType event_type);
    
    /**
     * @brief 注册观察者（指定交易对）
     */
    void register_observer(ObserverPtr observer, const std::string& symbol);
    
    /**
     * @brief 注册观察者（指定事件类型和交易对）
     */
    void register_observer(ObserverPtr observer, 
                          DataEventType event_type,
                          const std::string& symbol);
    
    /**
     * @brief 取消注册观察者
     */
    void unregister_observer(ObserverPtr observer);
    
    /**
     * @brief 取消注册观察者（指定事件类型）
     */
    void unregister_observer(ObserverPtr observer, DataEventType event_type);
    
    /**
     * @brief 取消注册观察者（指定交易对）
     */
    void unregister_observer(ObserverPtr observer, const std::string& symbol);
    
    /**
     * @brief 注册事件处理器（轻量级，无需实现完整接口）
     */
    size_t register_handler(EventHandler handler, DataEventType event_type);
    
    /**
     * @brief 注册事件处理器（指定交易对）
     */
    size_t register_handler(EventHandler handler, 
                           DataEventType event_type,
                           const std::string& symbol);
    
    /**
     * @brief 取消注册事件处理器
     */
    void unregister_handler(size_t handler_id);
    
    /**
     * @brief 发布事件（同步）
     */
    void publish_event(const DataEvent& event);
    
    /**
     * @brief 发布事件（异步）
     */
    void publish_event_async(const DataEvent& event);
    
    /**
     * @brief 批量发布事件
     */
    void publish_events(const std::vector<DataEvent>& events);
    
    /**
     * @brief 获取观察者数量
     */
    size_t get_observer_count() const;
    
    /**
     * @brief 获取处理器数量
     */
    size_t get_handler_count() const;
    
    /**
     * @brief 清空所有观察者和处理器
     */
    void clear();
    
    /**
     * @brief 获取事件统计
     */
    struct EventStats {
        size_t total_events_published = 0;
        size_t total_events_processed = 0;
        size_t total_observers_notified = 0;
        std::chrono::nanoseconds total_processing_time{0};
        std::map<DataEventType, size_t> events_by_type;
        std::map<std::string, size_t> events_by_symbol;
        
        double average_processing_time_ms() const {
            return total_events_processed > 0 ?
                total_processing_time.count() / (total_events_processed * 1000000.0) : 0.0;
        }
        
        void reset();
    };
    
    EventStats get_stats() const;
    void reset_stats();
    
    // 单例访问
    static DataEventBus& instance();
    
private:
    DataEventBus() = default;
    ~DataEventBus() = default;
    
    // 禁止拷贝
    DataEventBus(const DataEventBus&) = delete;
    DataEventBus& operator=(const DataEventBus&) = delete;
    
    struct ObserverRegistration {
        ObserverPtr observer;
        DataEventType event_type;
        std::string symbol;
        bool match_all_events;
        bool match_all_symbols;
    };
    
    struct HandlerRegistration {
        size_t id;
        EventHandler handler;
        DataEventType event_type;
        std::string symbol;
        bool match_all_symbols;
    };
    
    mutable std::shared_mutex mutex_;
    std::vector<ObserverRegistration> observers_;
    std::map<size_t, HandlerRegistration> handlers_;
    size_t next_handler_id_ = 1;
    
    EventStats stats_;
    
    void notify_observers(const DataEvent& event);
    void notify_handlers(const DataEvent& event);
    
    bool should_notify_observer(const ObserverRegistration& reg, const DataEvent& event) const;
    bool should_notify_handler(const HandlerRegistration& reg, const DataEvent& event) const;
};

/**
 * @brief 数据观察者管理器
 */
class DataObserverManager {
public:
    using ObserverFactory = std::function<std::shared_ptr<IDataObserver>()>;
    
    /**
     * @brief 注册观察者工厂
     */
    void register_factory(const std::string& observer_type, ObserverFactory factory);
    
    /**
     * @brief 创建观察者实例
     */
    std::shared_ptr<IDataObserver> create_observer(const std::string& observer_type);
    
    /**
     * @brief 注册预创建观察者
     */
    void register_observer(std::shared_ptr<IDataObserver> observer);
    
    /**
     * @brief 获取所有观察者
     */
    std::vector<std::shared_ptr<IDataObserver>> get_all_observers() const;
    
    /**
     * @brief 获取活跃观察者
     */
    std::vector<std::shared_ptr<IDataObserver>> get_active_observers() const;
    
    /**
     * @brief 按名称获取观察者
     */
    std::shared_ptr<IDataObserver> get_observer(const std::string& name) const;
    
    /**
     * @brief 按类型获取观察者
     */
    std::vector<std::shared_ptr<IDataObserver>> get_observers_by_type(const std::string& type) const;
    
    /**
     * @brief 激活/停用观察者
     */
    bool set_observer_active(const std::string& name, bool active);
    
    /**
     * @brief 移除观察者
     */
    bool remove_observer(const std::string& name);
    
    /**
     * @brief 清空所有观察者
     */
    void clear();
    
    /**
     * @brief 获取观察者统计
     */
    struct ManagerStats {
        size_t total_observers = 0;
        size_t active_observers = 0;
        size_t observer_types = 0;
        std::map<std::string, size_t> observers_by_type;
    };
    
    ManagerStats get_stats() const;
    
    // 单例访问
    static DataObserverManager& instance();
    
private:
    DataObserverManager() = default;
    
    mutable std::shared_mutex mutex_;
    std::map<std::string, ObserverFactory> factories_;
    std::map<std::string, std::shared_ptr<IDataObserver>> observers_;
    std::multimap<std::string, std::string> observers_by_type_;  // type -> name
};

} // namespace observer
} // namespace market
} // namespace astock
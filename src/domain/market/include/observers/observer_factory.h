// observer_factory.h
#pragma once

#include "data_observer.h"
#include "kline_observer.h"
#include "tick_observer.h"
#include "storage_observer.h"
#include "alert_observer.h"

namespace astock {
namespace market {
namespace observer {

/**
 * @brief 数据观察者工厂 - 创建各种类型的观察者
 */
class ObserverFactory {
public:
    /**
     * @brief 创建K线观察者
     */
    static std::shared_ptr<KLineObserver> create_kline_observer(
        const std::string& name = "KLineObserver",
        const std::string& description = "K线数据观察者");
    
    /**
     * @brief 创建Tick观察者
     */
    static std::shared_ptr<TickObserver> create_tick_observer(
        const std::string& name = "TickObserver",
        const std::string& description = "Tick数据观察者");
    
    /**
     * @brief 创建存储观察者
     */
    static std::shared_ptr<StorageObserver> create_storage_observer(
        const std::string& name = "StorageObserver",
        const StorageObserver::StorageConfig& config = StorageObserver::StorageConfig(),
        const std::string& description = "数据存储观察者");
    
    /**
     * @brief 创建报警观察者
     */
    static std::shared_ptr<AlertObserver> create_alert_observer(
        const std::string& name = "AlertObserver",
        const std::string& description = "数据报警观察者");
    
    /**
     * @brief 创建日志观察者
     */
    static std::shared_ptr<BaseDataObserver> create_log_observer(
        const std::string& name = "LogObserver",
        const std::string& description = "数据日志观察者");
    
    /**
     * @brief 创建统计观察者
     */
    static std::shared_ptr<BaseDataObserver> create_stats_observer(
        const std::string& name = "StatsObserver",
        const std::string& description = "数据统计观察者");
    
    /**
     * @brief 创建自定义观察者
     */
    template<typename T, typename... Args>
    static std::shared_ptr<T> create_custom_observer(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
    
    /**
     * @brief 注册观察者到事件总线
     */
    static void register_to_event_bus(
        std::shared_ptr<IDataObserver> observer,
        DataEventType event_type = DataEventType::CUSTOM_EVENT,
        const std::string& symbol = "");
    
    /**
     * @brief 从事件总线取消注册
     */
    static void unregister_from_event_bus(std::shared_ptr<IDataObserver> observer);
    
    /**
     * @brief 创建并配置默认观察者集合
     */
    static std::vector<std::shared_ptr<IDataObserver>> create_default_observers();
    
    /**
     * @brief 从配置文件加载观察者配置
     */
    static std::vector<std::shared_ptr<IDataObserver>> load_from_config(
        const std::string& config_file);
    
    /**
     * @brief 保存观察者配置到文件
     */
    static bool save_to_config(
        const std::vector<std::shared_ptr<IDataObserver>>& observers,
        const std::string& config_file);
};

} // namespace observer
} // namespace market
} // namespace astock
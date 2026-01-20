// data_observer_base.h
#pragma once

#include "data_observer.h"
#include <queue>
#include <condition_variable>

namespace astock {
namespace market {
namespace observer {

/**
 * @brief 基础数据观察者（提供通用功能）
 */
class BaseDataObserver : public IDataObserver {
public:
    BaseDataObserver(const std::string& name, const std::string& description = "");
    virtual ~BaseDataObserver();
    
    // IDataObserver 接口实现
    void on_data_event(const DataEvent& event) override;
    std::string get_name() const override;
    std::string get_description() const override;
    bool is_active() const override;
    void set_active(bool active) override;
    
    // 配置管理
    void set_config(const std::string& key, const std::string& value);
    std::string get_config(const std::string& key, const std::string& default_value = "") const;
    
    // 事件过滤
    void add_event_filter(DataEventType event_type);
    void add_symbol_filter(const std::string& symbol);
    void clear_filters();
    
    // 统计信息
    struct ObserverStats {
        size_t total_events_received = 0;
        size_t total_events_processed = 0;
        size_t events_filtered_out = 0;
        std::chrono::nanoseconds total_processing_time{0};
        std::map<DataEventType, size_t> events_by_type;
        
        double processing_rate() const {
            return total_processing_time.count() > 0 ?
                static_cast<double>(total_events_processed) / 
                (total_processing_time.count() / 1000000000.0) : 0.0;
        }
    };
    
    ObserverStats get_stats() const;
    void reset_stats();
    
protected:
    /**
     * @brief 处理事件的具体实现（由子类重写）
     */
    virtual void process_event(const DataEvent& event) = 0;
    
    /**
     * @brief 事件过滤检查
     */
    virtual bool should_process_event(const DataEvent& event) const;
    
    /**
     * @brief 错误处理
     */
    virtual void handle_error(const std::string& error_msg, const DataEvent* event = nullptr);
    
    /**
     * @brief 日志记录
     */
    void log_info(const std::string& message) const;
    void log_warn(const std::string& message) const;
    void log_error(const std::string& message) const;
    void log_debug(const std::string& message) const;
    
private:
    std::string name_;
    std::string description_;
    std::atomic<bool> active_{true};
    
    std::map<std::string, std::string> config_;
    std::vector<DataEventType> event_filters_;
    std::vector<std::string> symbol_filters_;
    
    mutable std::mutex stats_mutex_;
    ObserverStats stats_;
    
    // Foundation 工具引用
    foundation::log::LoggerImpl& logger_;
};

/**
 * @brief 异步数据观察者（事件处理在独立线程中）
 */
class AsyncDataObserver : public BaseDataObserver {
public:
    AsyncDataObserver(const std::string& name, 
                     const std::string& description = "",
                     size_t max_queue_size = 10000);
    ~AsyncDataObserver() override;
    
    // 覆盖基类方法
    void on_data_event(const DataEvent& event) override;
    void set_active(bool active) override;
    
    // 队列管理
    size_t get_queue_size() const;
    size_t get_max_queue_size() const;
    void set_max_queue_size(size_t size);
    void clear_queue();
    
    // 性能配置
    void set_batch_size(size_t batch_size);
    void set_process_interval(std::chrono::milliseconds interval);
    
protected:
    /**
     * @brief 批量处理事件（由子类重写）
     */
    virtual void process_events_batch(const std::vector<DataEvent>& events);
    
    /**
     * @brief 处理单个事件（默认实现调用基类方法）
     */
    void process_event(const DataEvent& event) override;
    
private:
    void worker_thread_func();
    
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<DataEvent> event_queue_;
    size_t max_queue_size_;
    
    size_t batch_size_ = 100;
    std::chrono::milliseconds process_interval_{10};
};

/**
 * @brief 过滤数据观察者（根据条件过滤事件）
 */
class FilteringDataObserver : public BaseDataObserver {
public:
    using FilterCondition = std::function<bool(const DataEvent&)>;
    
    FilteringDataObserver(const std::string& name, 
                         const std::string& description = "");
    
    /**
     * @brief 添加过滤条件
     */
    void add_filter_condition(const FilterCondition& condition);
    
    /**
     * @brief 清空过滤条件
     */
    void clear_filter_conditions();
    
    /**
     * @brief 设置是否传递过滤掉的事件
     */
    void set_pass_filtered_events(bool pass);
    
protected:
    bool should_process_event(const DataEvent& event) const override;
    
private:
    std::vector<FilterCondition> filter_conditions_;
    bool pass_filtered_events_ = false;
};

/**
 * @brief 路由数据观察者（将事件路由到不同的处理函数）
 */
class RoutingDataObserver : public BaseDataObserver {
public:
    using EventHandler = std::function<void(const DataEvent&)>;
    
    RoutingDataObserver(const std::string& name, 
                       const std::string& description = "");
    
    /**
     * @brief 注册事件处理器
     */
    void register_handler(DataEventType event_type, EventHandler handler);
    
    /**
     * @brief 注册默认处理器
     */
    void register_default_handler(EventHandler handler);
    
    /**
     * @brief 清除所有处理器
     */
    void clear_handlers();
    
protected:
    void process_event(const DataEvent& event) override;
    
private:
    std::map<DataEventType, EventHandler> handlers_;
    EventHandler default_handler_;
};

} // namespace observer
} // namespace market
} // namespace astock
#pragma once

#include "Engine.h"
#include "IClock.h"
#include "EventBus.h"
#include "IDataSource.h"
#include "ITrigger.h"
#include "foundation/Utils/Uuid.h"
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <vector>
#include <atomic>

namespace engine {

class EngineImpl : public Engine {
public:
    EngineImpl();
    ~EngineImpl() override;
    
    // Engine接口实现
    Error initialize(const Config& config) override;
    Error start() override;
    Error stop() override;
    Error pause() override;
    Error resume() override;
    Error reset() override;
    
    EngineListener::State state() const override;
    Clock* clock() override;
    EventBus* event_bus() override;
    
    // 数据源管理
    Error register_data_source(std::unique_ptr<DataSource> data_source) override;
    Error unregister_data_source(const std::string& name) override;
    std::vector<std::string> data_source_names() const override;
    DataSource* get_data_source(const std::string& name) override;
    
    // 触发器管理
    Error register_trigger(std::unique_ptr<Trigger> trigger) override;
    Error unregister_trigger(const foundation::utils::Uuid& id) ;
    std::vector<foundation::utils::Uuid> trigger_ids() const ;
    Trigger* get_trigger(const foundation::utils::Uuid& id) ;
    
    // 监听器管理
    void register_listener(EngineListener* listener) override;
    void unregister_listener(EngineListener* listener) override;
    
    // 统计信息
    EngineListener::Statistics statistics() const override;
    
    // 工具方法
    Error publish_event(std::unique_ptr<Event> event) override;
    const Config& config() const override;
    static foundation::Timestamp parse_timestamp(const std::string& str);
private:
    
    // 内部状态
    enum class InternalState {
        CREATED,
        INITIALIZING,
        INITIALIZED,
        STARTING,
        RUNNING,
        PAUSING,
        PAUSED,
        STOPPING,
        STOPPED,
        INTER_ERROR
    };
    
    // 事件队列项
    struct EventItem {
        foundation::Timestamp timestamp;
        std::unique_ptr<Event> event;
        
        bool operator>(const EventItem& other) const {
            return timestamp > other.timestamp;
        }
    };
    
    // 统计信息
    struct InternalStatistics {
        std::atomic<size_t> total_events_processed{0};
        std::atomic<size_t> total_triggers_fired{0};
        std::atomic<size_t> total_errors{0};
        foundation::Timestamp start_time;
        foundation::Timestamp last_statistics_update;
    };
    
    // 状态管理
    bool transition_state(InternalState new_state, const std::string& reason);
    EngineListener::State to_external_state(InternalState state) const;
    std::string internal_state_to_string(InternalState state) const;
    
    // 监听器通知
    void notify_state_changed(EngineListener::State old_state, EngineListener::State new_state);
    void notify_error(const Error& error);
    void notify_statistics_updated();
    
    // 事件处理
    void event_loop();
    Error process_event(std::unique_ptr<Event> event);
    void evaluate_triggers(const Event& event);
    
    // 成员变量
    std::atomic<InternalState> internal_state_;
    Config config_;
    
    // 核心组件
    std::unique_ptr<Clock> clock_;
    std::unique_ptr<EventBus> event_bus_;
    
    // 数据管理
    std::unordered_map<std::string, std::unique_ptr<DataSource>> data_sources_;
    mutable std::mutex data_sources_mutex_;
    
    std::unordered_map<foundation::utils::Uuid, std::unique_ptr<Trigger>> triggers_;
    mutable std::mutex triggers_mutex_;
    
    std::vector<EngineListener*> listeners_;
    mutable std::mutex listeners_mutex_;
    
    // 事件队列
    std::priority_queue<EventItem, std::vector<EventItem>, std::greater<EventItem>> event_queue_;
    mutable std::mutex event_queue_mutex_;
    std::condition_variable event_queue_cv_;
    
    // 线程管理
    std::thread event_loop_thread_;
    std::atomic<bool> shutdown_requested_{false};
    
    // 统计
    InternalStatistics stats_;
    mutable std::mutex stats_mutex_;
};

} // namespace engine
#include "EngineImpl.h"
#include "TriggerBusBridge.h"
#include "GlobalEventBusRegistry.h"
#include <foundation.h>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace engine {


// ============================================================================
// 静态方法
// ============================================================================

foundation::Timestamp EngineImpl::parse_timestamp(const std::string& str) {
    // 简单实现：假设是Unix时间戳（秒）
    try {
        return foundation::from_microseconds(std::stoll(str));
    } catch (...) {
        // 如果失败，返回当前时间
        INTERNAL_WARN_STREAM << "Failed to parse timestamp: " << str << ", using current time";
        return foundation:: timestamp_now();
    }
}

// ============================================================================
// 构造函数和析构函数
// ============================================================================

EngineImpl::EngineImpl() : Engine()
    , internal_state_(InternalState::CREATED) {
    
    INTERNAL_DEBUG_STREAM << "EngineImpl created";
    
    // 在构造函数体内初始化 stats_ 的成员
    stats_.total_events_processed = 0;
    stats_.total_triggers_fired = 0;
    stats_.total_errors = 0;
    stats_.start_time = foundation::timestamp_now();
    stats_.last_statistics_update = stats_.start_time;
}

EngineImpl::~EngineImpl() {
    INTERNAL_DEBUG_STREAM << "EngineImpl destructor called";
    
    try {
        // 确保引擎已停止
        if (internal_state_ != InternalState::STOPPED && 
            internal_state_ != InternalState::CREATED) {
            INTERNAL_WARN_STREAM << "Engine not stopped before destruction, stopping now";
            stop();
        }
        
        // 等待事件循环线程结束
        if (event_loop_thread_.joinable()) {
            shutdown_requested_ = true;
            event_queue_cv_.notify_all();
            event_loop_thread_.join();
        }
        
        // 清理资源
        {
            std::lock_guard<std::mutex> lock(data_sources_mutex_);
            data_sources_.clear();
        }
        
        {
            std::lock_guard<std::mutex> lock(triggers_mutex_);
            triggers_.clear();
        }
        
        {
            std::lock_guard<std::mutex> lock(listeners_mutex_);
            listeners_.clear();
        }
        
        INTERNAL_DEBUG_STREAM << "EngineImpl destroyed";
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Exception in EngineImpl destructor: " << e.what();
    }
}

// ============================================================================
// 状态管理函数
// ============================================================================

bool EngineImpl::transition_state(InternalState new_state, const std::string& reason) {
    auto old_state = internal_state_.load();
    
    // 检查状态转换是否合法
    bool valid_transition = false;
    switch (old_state) {
        case InternalState::CREATED:
            valid_transition = (new_state == InternalState::INITIALIZING || 
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::INITIALIZING:
            valid_transition = (new_state == InternalState::INITIALIZED || 
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::INITIALIZED:
            valid_transition = (new_state == InternalState::STARTING || 
                              new_state == InternalState::STOPPING ||
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::STARTING:
            valid_transition = (new_state == InternalState::RUNNING || 
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::RUNNING:
            valid_transition = (new_state == InternalState::PAUSING || 
                              new_state == InternalState::STOPPING ||
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::PAUSING:
            valid_transition = (new_state == InternalState::PAUSED || 
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::PAUSED:
            valid_transition = (new_state == InternalState::STARTING || 
                              new_state == InternalState::STOPPING ||
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::STOPPING:
            valid_transition = (new_state == InternalState::STOPPED || 
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::STOPPED:
            valid_transition = (new_state == InternalState::INITIALIZING || 
                              new_state == InternalState::INTER_ERROR);
            break;
        case InternalState::INTER_ERROR:
            valid_transition = (new_state == InternalState::STOPPING || 
                              new_state == InternalState::INITIALIZING);
            break;
    }
    
    if (!valid_transition) {
        INTERNAL_ERROR_STREAM << "Invalid state transition: "
                 << internal_state_to_string(old_state) << " -> "
                 << internal_state_to_string(new_state);
        return false;
    }
    
    INTERNAL_DEBUG_STREAM << "State transition: "
              << internal_state_to_string(old_state) << " -> "
              << internal_state_to_string(new_state) << " (" << reason << ")";
    
    internal_state_.store(new_state);
    
    // 通知状态变化
    notify_state_changed(to_external_state(old_state), to_external_state(new_state));
    
    return true;
}

EngineListener::State EngineImpl::to_external_state(InternalState state) const {
    switch (state) {
        case InternalState::CREATED:
        case InternalState::INITIALIZING:
        case InternalState::INITIALIZED:
            return EngineListener::State::Created;
        case InternalState::STARTING:
        case InternalState::RUNNING:
            return EngineListener::State::Running;
        case InternalState::PAUSING:
        case InternalState::PAUSED:
            return EngineListener::State::Paused;
        case InternalState::STOPPING:
        case InternalState::STOPPED:
            return EngineListener::State::Stopped;
        case InternalState::INTER_ERROR:
            return EngineListener::State::Error;
        default:
            INTERNAL_WARN_STREAM << "Unknown internal state: " << static_cast<int>(state);
            return EngineListener::State::Error;
    }
}

std::string EngineImpl::internal_state_to_string(InternalState state) const {
    switch (state) {
        case InternalState::CREATED: return "CREATED";
        case InternalState::INITIALIZING: return "INITIALIZING";
        case InternalState::INITIALIZED: return "INITIALIZED";
        case InternalState::STARTING: return "STARTING";
        case InternalState::RUNNING: return "RUNNING";
        case InternalState::PAUSING: return "PAUSING";
        case InternalState::PAUSED: return "PAUSED";
        case InternalState::STOPPING: return "STOPPING";
        case InternalState::STOPPED: return "STOPPED";
        case InternalState::INTER_ERROR: return "INTER_ERROR";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// 监听器通知
// ============================================================================

void EngineImpl::notify_state_changed(EngineListener::State old_state, 
                                      EngineListener::State new_state) {
    std::lock_guard<std::mutex> lock(listeners_mutex_);
    
    for (auto listener : listeners_) {
        try {
            listener->on_state_changed(old_state, new_state);
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "Listener threw exception in on_state_changed: " << e.what();
        }
    }
}

void EngineImpl::notify_error(const Error& error) {
    std::lock_guard<std::mutex> lock(listeners_mutex_);
    
    for (auto listener : listeners_) {
        try {
            listener->on_error(error);
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "Listener threw exception in on_error: " << e.what();
        }
    }
}

void EngineImpl::notify_statistics_updated() {
    std::lock_guard<std::mutex> lock(listeners_mutex_);
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    
    auto current_time = foundation::timestamp_now();
    int64_t elapsed_since_last_update = (current_time - stats_.last_statistics_update).to_milliseconds();
    
    // 限制更新频率（至少100ms）
    if (elapsed_since_last_update < 100) {
        return;
    }
    
    EngineListener::Statistics stats;
    stats.total_events_processed = stats_.total_events_processed.load();
    stats.total_triggers_fired = stats_.total_triggers_fired.load();
    //foundation::Timestamp current_time = foundation::Timestamp::now();
    stats.total_runtime = current_time - stats_.start_time;  // 直接得到 Duration
    //stats.total_runtime = std::chrono::duration_cast<Duration>(current_time - stats_.start_time);
    // 方法A：通过微秒数
    
    stats.start_time = stats_.start_time;
    
    for (auto listener : listeners_) {
        try {
            listener->on_statistics_updated(stats);
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "Listener threw exception in on_statistics_updated: " << e.what();
        }
    }
    
    stats_.last_statistics_update = current_time;
}

// ============================================================================
// Engine接口实现
// ============================================================================

Error EngineImpl::initialize(const Config& config) {
    INTERNAL_INFO_STREAM << "Initializing engine with config";
    
    // 检查状态
    if (internal_state_ != InternalState::CREATED && 
        internal_state_ != InternalState::STOPPED) {
        return Error{Error::Code::NOT_FOUND, "Engine must be in CREATED or STOPPED state to initialize"};
    }
    
    if (!transition_state(InternalState::INITIALIZING, "Begin initialization")) {
        return Error{Error::Code::NOT_FOUND, "Failed to transition to INITIALIZING state"};
    }
    
    try {
        // 保存配置
        config_ = config;
        
        // 创建时钟
        switch (config_.time_mode) {
            case Clock::Mode::Realtime: {
                clock_ = Clock::create_realtime_clock();
                break;
            }
            case Clock::Mode::Backtest: {
                auto start_str = config_.parameters.find("backtest_start_time");
                auto end_str = config_.parameters.find("backtest_end_time");
                foundation::Timestamp start_time, end_time;
                
                // 使用默认值
                auto now =foundation::Timestamp::now();;
                start_time = end_time - foundation::Duration::hours(24);
                end_time = now;
                
                if (start_str != config_.parameters.end() && 
                    end_str != config_.parameters.end()) {
                    start_time = parse_timestamp(start_str->second);
                    end_time = parse_timestamp(end_str->second);
                }
                
                // 默认步长间隔
                Duration step_interval = Duration(1'000'000'000); // 1秒
                
                clock_ = Clock::create_backtest_clock(start_time, end_time, step_interval);
                break;
            }
            case Clock::Mode::Accelerated: {
                double speed_factor = 2.0; // 默认2倍速
                auto factor_str = config_.parameters.find("acceleration_factor");
                if (factor_str != config_.parameters.end()) {
                    try {
                        speed_factor = std::stod(factor_str->second);
                    } catch (...) {
                        INTERNAL_WARN_STREAM << "Failed to parse acceleration_factor, using default";
                    }
                }
                
                clock_ = Clock::create_accelerated_clock(
                    Duration::microseconds(
                        static_cast<int64_t>(speed_factor * 1000000)  // 秒转微秒
                    )
                );
                break;
            }
            default: {
                throw std::invalid_argument("Unsupported clock mode");
            }
        }
        
        if (!clock_) {
            throw std::runtime_error("Failed to create clock");
        }
        
        // 使用Foundation的时间格式化
        auto now_str = foundation::Foundation::current_time_string();
        INTERNAL_DEBUG_STREAM << "Clock created at: " << now_str;
        
        // 创建事件总线
        event_bus_ = std::shared_ptr<EventBus>(EventBus::create());
        if (!event_bus_) {
            throw std::runtime_error("Failed to create event bus");
        }

        INTERNAL_DEBUG_STREAM << "Event bus created";

        // 向全局注册这只引擎 EventBus，供 Python 绑定等模块共享
        register_engine_event_bus(event_bus_);

        // 注册 Trigger → 引擎 事件发布桥接，使 EventEmitAction 产生的新事件
        // 能通过 EngineImpl::publish_event 回到统一事件处理管线
        set_trigger_event_publisher([this](std::unique_ptr<Event> evt) {
            return this->publish_event(std::move(evt));
        });
        
        // 启动事件循环线程
        shutdown_requested_ = false;
        event_loop_thread_ = std::thread(&EngineImpl::event_loop, this);
        
        INTERNAL_DEBUG_STREAM << "Event loop thread started";
        
        if (!transition_state(InternalState::INITIALIZED, "Initialization completed")) {
            throw std::runtime_error("Failed to transition to INITIALIZED state");
        }
        
        INTERNAL_INFO_STREAM << "Engine initialized successfully";
        return Error{Error::Code::OK, "Engine initialized successfully"};
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Engine initialization failed: " << e.what();
        transition_state(InternalState::INTER_ERROR, std::string("Initialization failed: ") + e.what());
        return Error{Error::Code::NOT_FOUND, std::string("Initialization failed: ") + e.what()};
    }
}

Error EngineImpl::start() {
    INTERNAL_INFO_STREAM << "Starting engine";
    
    if (internal_state_ != InternalState::INITIALIZED && 
        internal_state_ != InternalState::PAUSED) {
        return Error{Error::Code::NOT_FOUND, "Engine must be in INITIALIZED or PAUSED state to start"};
    }
    
    if (!transition_state(InternalState::STARTING, "Begin starting")) {
        return Error{Error::Code::NOT_FOUND, "Failed to transition to STARTING state"};
    }
    
    try {
        // 启动时钟
        if (clock_->mode() == Clock::Mode::Backtest || 
            clock_->mode() == Clock::Mode::Accelerated) {
            auto clock_result = clock_->start();
            if (clock_result.code() != Error::Code::OK) {
                throw std::runtime_error(std::string("Failed to start clock: ") + clock_result.message());
            }
        }
        
        // 启动所有数据源
        {
            std::lock_guard<std::mutex> lock(data_sources_mutex_);
            for (const auto& [name, data_source] : data_sources_) {
                auto result = data_source->connect();
                if (result.code() != Error::Code::OK) {
                    INTERNAL_WARN_STREAM << "Failed to connect data source " << name << ": " << result.message();
                } else {
                    INTERNAL_DEBUG_STREAM << "Data source " << name << " connected";
                }
            }
        }
        
        // 通知事件循环可以开始处理
        event_queue_cv_.notify_all();
        
        if (!transition_state(InternalState::RUNNING, "Started successfully")) {
            throw std::runtime_error("Failed to transition to RUNNING state");
        }
        
        INTERNAL_INFO_STREAM << "Engine started successfully";
        return Error{Error::Code::OK, "Engine started successfully"};
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Engine start failed: " << e.what();
        transition_state(InternalState::INTER_ERROR, std::string("Start failed: ") + e.what());
        return Error{Error::Code::NOT_FOUND, std::string("Start failed: ") + e.what()};
    }
}

Error EngineImpl::stop() {
    INTERNAL_INFO_STREAM << "Stopping engine";
    
    if (internal_state_ == InternalState::STOPPED) {
        return Error{Error::Code::OK, "Engine already stopped"};
    }
    
    if (internal_state_ == InternalState::CREATED) {
        return Error{Error::Code::NOT_FOUND, "Engine not initialized"};
    }
    
    if (!transition_state(InternalState::STOPPING, "Begin stopping")) {
        return Error{Error::Code::NOT_FOUND, "Failed to transition to STOPPING state"};
    }
    
    try {
        // 设置停止标志
        shutdown_requested_ = true;
        
        // 停止所有数据源
        {
            std::lock_guard<std::mutex> lock(data_sources_mutex_);
            for (const auto& [name, data_source] : data_sources_) {
                data_source->disconnect();
                INTERNAL_DEBUG_STREAM << "Data source " << name << " disconnected";
            }
        }
        
        // 停止时钟
        if (clock_) {
            clock_->stop();
        }
        
        // 唤醒事件循环线程
        event_queue_cv_.notify_all();
        
        // 等待事件循环线程结束
        if (event_loop_thread_.joinable()) {
            event_loop_thread_.join();
        }
        
        // 清空事件队列
        {
            std::lock_guard<std::mutex> lock(event_queue_mutex_);
            std::priority_queue<EventItem, std::vector<EventItem>, std::greater<EventItem>> empty_queue;
            std::swap(event_queue_, empty_queue);
        }
        
        if (!transition_state(InternalState::STOPPED, "Stopped successfully")) {
            throw std::runtime_error("Failed to transition to STOPPED state");
        }
        
        INTERNAL_INFO_STREAM << "Engine stopped successfully";
        return Error{Error::Code::OK, "Engine stopped successfully"};
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Engine stop failed: " << e.what();
        transition_state(InternalState::INTER_ERROR, std::string("Stop failed: ") + e.what());
        return Error{Error::Code::NOT_FOUND, std::string("Stop failed: ") + e.what()};
    }
}

Error EngineImpl::pause() {
    INTERNAL_INFO_STREAM << "Pausing engine";
    
    if (internal_state_ != InternalState::RUNNING) {
        return Error{Error::Code::NOT_FOUND, "Engine must be in RUNNING state to pause"};
    }
    
    if (!transition_state(InternalState::PAUSING, "Begin pausing")) {
        return Error{Error::Code::NOT_FOUND, "Failed to transition to PAUSING state"};
    }
    
    try {
        // 停止时钟（暂停）
        if (clock_) {
            clock_->stop();
        }
        
        if (!transition_state(InternalState::PAUSED, "Paused successfully")) {
            throw std::runtime_error("Failed to transition to PAUSED state");
        }
        
        INTERNAL_INFO_STREAM << "Engine paused successfully";
        return Error{Error::Code::OK, "Engine paused successfully"};
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Engine pause failed: " << e.what();
        transition_state(InternalState::INTER_ERROR, std::string("Pause failed: ") + e.what());
        return Error{Error::Code::NOT_FOUND, std::string("Pause failed: ") + e.what()};
    }
}

Error EngineImpl::resume() {
    INTERNAL_INFO_STREAM << "Resuming engine";
    
    if (internal_state_ != InternalState::PAUSED) {
        return Error{Error::Code::NOT_FOUND, "Engine must be in PAUSED state to resume"};
    }
    
    return start(); // 重用start逻辑
}

Error EngineImpl::reset() {
    INTERNAL_INFO_STREAM << "Resetting engine";
    
    // 先停止引擎
    if (internal_state_ != InternalState::STOPPED && 
        internal_state_ != InternalState::CREATED) {
        auto stop_result = stop();
        if (stop_result.code() != Error::Code::OK) {
            return Error{Error::Code::OK, "Failed to stop engine before reset: " + stop_result.message()};
        }
    }
    
    try {
        // 清理所有组件
        {
            std::lock_guard<std::mutex> lock(data_sources_mutex_);
            data_sources_.clear();
        }
        
        {
            std::lock_guard<std::mutex> lock(triggers_mutex_);
            triggers_.clear();
        }
        
        {
            std::lock_guard<std::mutex> lock(listeners_mutex_);
            listeners_.clear();
        }
        
        // 重置事件总线
        if (event_bus_) {
            // EventBus没有reset方法，创建新的
            event_bus_ = std::shared_ptr<EventBus>(EventBus::create());
        }
        
        // 重置时钟
        if (clock_) {
            clock_->reset(foundation::timestamp_now());
        }
        
        // 重置统计信息
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_events_processed = 0;
            stats_.total_triggers_fired = 0;
            stats_.total_errors = 0;
            stats_.start_time = foundation::timestamp_now();
            stats_.last_statistics_update = stats_.start_time;
        }
        
        // 清空事件队列
        {
            std::lock_guard<std::mutex> lock(event_queue_mutex_);
            std::priority_queue<EventItem, std::vector<EventItem>, std::greater<EventItem>> empty_queue;
            std::swap(event_queue_, empty_queue);
        }
        
        // 重置状态
        internal_state_.store(InternalState::CREATED);
        shutdown_requested_ = false;
        
        INTERNAL_INFO_STREAM << "Engine reset successfully";
        return Error{Error::Code::OK, "Engine reset successfully"};
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Engine reset failed: " << e.what();
        transition_state(InternalState::INTER_ERROR, std::string("Reset failed: ") + e.what());
        return Error{Error::Code::NOT_FOUND, std::string("Reset failed: ") + e.what()};
    }
}

EngineListener::State EngineImpl::state() const {
    return to_external_state(internal_state_.load());
}

Clock* EngineImpl::clock() {
    return clock_.get();
}

EventBus* EngineImpl::event_bus() {
    return event_bus_.get();
}

// ============================================================================
// 数据源管理
// ============================================================================

Error EngineImpl::register_data_source(std::unique_ptr<DataSource> data_source) {
    if (!data_source) {
        return Error{Error::Code::NOT_FOUND, "Data source cannot be null"};
    }
    
    auto name = data_source->name();
    INTERNAL_DEBUG_STREAM << "Registering data source: " << name;
    
    std::lock_guard<std::mutex> lock(data_sources_mutex_);
    
    if (data_sources_.find(name) != data_sources_.end()) {
        return Error{Error::Code::OK, "Data source with name '" + name + "' already registered"};
    }
    
    data_sources_[name] = std::move(data_source);
    INTERNAL_DEBUG_STREAM << "Data source '" << name << "' registered successfully";
    
    return Error{Error::Code::OK, "Data source registered successfully"};
}

Error EngineImpl::unregister_data_source(const std::string& name) {
    INTERNAL_DEBUG_STREAM << "Unregistering data source: " << name;
    
    std::lock_guard<std::mutex> lock(data_sources_mutex_);
    
    auto it = data_sources_.find(name);
    if (it == data_sources_.end()) {
        return Error{Error::Code::NOT_FOUND, "Data source with name '" + name + "' not found"};
    }
    
    // 断开数据源
    it->second->disconnect();
    
    // 移除数据源
    data_sources_.erase(it);
    INTERNAL_DEBUG_STREAM << "Data source '" << name << "' unregistered successfully";
    
    return Error{Error::Code::OK, "Data source unregistered successfully"};
}

std::vector<std::string> EngineImpl::data_source_names() const {
    std::lock_guard<std::mutex> lock(data_sources_mutex_);
    
    std::vector<std::string> names;
    names.reserve(data_sources_.size());
    
    for (const auto& [name, _] : data_sources_) {
        names.push_back(name);
    }
    
    return names;
}

DataSource* EngineImpl::get_data_source(const std::string& name) {
    std::lock_guard<std::mutex> lock(data_sources_mutex_);
    
    auto it = data_sources_.find(name);
    if (it == data_sources_.end()) {
        return nullptr;
    }
    
    return it->second.get();
}

// ============================================================================
// 触发器管理
// ============================================================================

Error EngineImpl::register_trigger(std::unique_ptr<Trigger> trigger) {
    if (!trigger) {
        return Error{Error::Code::NOT_FOUND, "Trigger cannot be null"};
    }
    
    auto id = trigger->id();
    INTERNAL_DEBUG_STREAM << "Registering trigger: " << trigger->name() << " (ID: " << id << ")";
    
    std::lock_guard<std::mutex> lock(triggers_mutex_);
    
    if (triggers_.find(id) != triggers_.end()) {
        return Error{Error::Code::INVALID_ARGUMENT, "Trigger with ID already registered"};
    }
    
    triggers_[id] = std::move(trigger);
    INTERNAL_DEBUG_STREAM << "Trigger registered successfully";
    
    return Error{Error::Code::OK, "Trigger registered successfully"};
}

Error EngineImpl::unregister_trigger(const foundation::utils::Uuid& id) {
    INTERNAL_DEBUG_STREAM << "Unregistering trigger with ID: " << id;
    
    std::lock_guard<std::mutex> lock(triggers_mutex_);
    
    auto it = triggers_.find(id);
    if (it == triggers_.end()) {
        return Error{Error::Code::NOT_FOUND, "Trigger with specified ID not found"};
    }
    
    triggers_.erase(it);
    INTERNAL_DEBUG_STREAM << "Trigger unregistered successfully";
    
    return Error{Error::Code::OK, "Trigger unregistered successfully"};
}

std::vector<foundation::utils::Uuid> EngineImpl::trigger_ids() const {
    std::lock_guard<std::mutex> lock(triggers_mutex_);
    
    std::vector<foundation::utils::Uuid> ids;
    ids.reserve(triggers_.size());
    
    for (const auto& [id, _] : triggers_) {
        ids.push_back(id);
    }
    
    return ids;
}

Trigger* EngineImpl::get_trigger(const foundation::utils::Uuid& id) {
    std::lock_guard<std::mutex> lock(triggers_mutex_);
    
    auto it = triggers_.find(id);
    if (it == triggers_.end()) {
        return nullptr;
    }
    
    return it->second.get();
}

// ============================================================================
// 监听器管理
// ============================================================================

void EngineImpl::register_listener(EngineListener* listener) {
    if (!listener) {
        INTERNAL_WARN_STREAM << "Attempt to register null listener";
        return;
    }
    
    std::lock_guard<std::mutex> lock(listeners_mutex_);
    
    // 检查是否已注册
    if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
        INTERNAL_WARN_STREAM << "Listener already registered";
        return;
    }
    
    listeners_.push_back(listener);
    INTERNAL_DEBUG_STREAM << "Listener registered";
}

void EngineImpl::unregister_listener(EngineListener* listener) {
    if (!listener) {
        INTERNAL_WARN_STREAM << "Attempt to unregister null listener";
        return;
    }
    
    std::lock_guard<std::mutex> lock(listeners_mutex_);
    
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
        INTERNAL_DEBUG_STREAM << "Listener unregistered";
    } else {
        INTERNAL_WARN_STREAM << "Listener not found for unregistration";
    }
}

// ============================================================================
// 统计信息
// ============================================================================

EngineListener::Statistics EngineImpl::statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    EngineListener::Statistics stats;
    stats.total_events_processed = stats_.total_events_processed.load();
    stats.total_triggers_fired = stats_.total_triggers_fired.load();
    
    auto current_time = foundation::timestamp_now();
    stats.total_runtime = current_time - stats_.start_time;
    stats.start_time = stats_.start_time;
    
    return stats;
}

// ============================================================================
// 工具方法
// ============================================================================

Error EngineImpl::publish_event(std::unique_ptr<Event> event) {
    if (!event) {
        return Error{Error::Code::NOT_FOUND, "Event cannot be null"};
    }
    
    if (internal_state_ != InternalState::RUNNING && 
        internal_state_ != InternalState::STARTING) {
        INTERNAL_WARN_STREAM << "Attempt to publish event while engine is not running";
        return Error{Error::Code::NOT_FOUND, "Engine is not running"};
    }
    
    try {
        EventItem item;
        item.timestamp = event->timestamp();
        item.event = std::move(event);
        
        {
            std::lock_guard<std::mutex> lock(event_queue_mutex_);
            
            // 检查队列大小限制
            if (event_queue_.size() >= config_.max_pending_events) {
                INTERNAL_WARN_STREAM << "Event queue full, dropping event";
                return Error{Error::Code::NOT_FOUND, "Event queue is full"};
            }
            
            event_queue_.push(std::move(item));
        }
        
        // 通知事件循环线程
        event_queue_cv_.notify_one();
        
        return Error{Error::Code::OK, "Event published successfully"};
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to publish event: " << e.what();
        return Error{Error::Code::OK, std::string("Failed to publish event: ") + e.what()};
    }
}

const Engine::Config& EngineImpl::config() const {
    return config_;
}

// ============================================================================
// 事件处理循环 - 修复版本（使用 priority_queue）
// ============================================================================

void EngineImpl::event_loop() {
    INTERNAL_DEBUG_STREAM << "Event loop started";
    
    try {
        while (!shutdown_requested_) {
            // 等待事件或停止信号
            std::unique_lock<std::mutex> lock(event_queue_mutex_);
            
            if (event_queue_.empty()) {
                // 等待配置的时间间隔
                event_queue_cv_.wait_for(lock, std::chrono::milliseconds(config_.event_dispatch_interval.to_milliseconds()));
                
                // 定期更新统计信息
                notify_statistics_updated();
                
                // 检查是否需要退出
                if (shutdown_requested_) {
                    break;
                }
                
                // 检查是否应该处理事件
                if (event_queue_.empty()) {
                    continue;
                }
            }
            
            // 获取当前时间
            auto current_time = clock_ ? clock_->current_time() : foundation::timestamp_now();
            
            // 检查是否需要等待（因为 priority_queue.top() 返回 const 引用，需要先检查再弹出）
            bool should_wait = false;
            std::chrono::milliseconds wait_time_ms(0);
            
            // 检查堆顶事件时间
            if (!event_queue_.empty()) {
                const auto& next_item = event_queue_.top();
                
                if (next_item.timestamp > current_time) {
                    auto wait_time = next_item.timestamp - current_time;
                    wait_time_ms = std::chrono::milliseconds(wait_time.to_milliseconds());
                    should_wait = (wait_time_ms > std::chrono::milliseconds(0));
                }
            }
            
            if (should_wait) {
                event_queue_cv_.wait_for(lock, wait_time_ms);
                continue;
            }
            
            // 取出事件（此时事件时间已到或已过时）
            if (!event_queue_.empty()) {
                // 移动事件（priority_queue.pop() 不返回值，所以需要先获取再弹出）
                auto event = std::move(const_cast<EventItem&>(event_queue_.top()).event);
                event_queue_.pop();
                
                lock.unlock();
                
                // 处理事件
                auto result = process_event(std::move(event));
                if (result.code() != Error::Code::OK) {
                    INTERNAL_ERROR_STREAM << "Event processing failed: " << result.message();
                    stats_.total_errors++;
                    notify_error(result);
                }
                
                stats_.total_events_processed++;
                notify_statistics_updated();
            }
        }
        
        INTERNAL_DEBUG_STREAM << "Event loop stopped";
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Event loop crashed: " << e.what();
        transition_state(InternalState::INTER_ERROR, std::string("Event loop crashed: ") + e.what());
    }
}

Error EngineImpl::process_event(std::unique_ptr<Event> event) {
    if (!event) {
        return Error{Error::Code::NOT_FOUND, "Event is null"};
    }
    
    try {
        auto timestamp_ms = foundation::Foundation::timestamp_ms();
        
        INTERNAL_TRACE_STREAM << "Processing event: type="
                            << Event::event_type_to_string(event->type())
                            << ", timestamp=" << timestamp_ms << "ms";
        
        // 在移动前获取事件的引用
        Event& event_ref = *event;
        
        // 先评估触发器
        if (config_.enable_trigger_system) {
            evaluate_triggers(event_ref);
        }
        
        // 然后发布到事件总线（转移所有权）
        auto bus_result = event_bus_->publish(std::move(event));
        if (!bus_result) {
            return Error{Error::Code::BUSY, std::string("Failed to publish event to bus: ") + bus_result.message};
        }
        
        return Error{Error::Code::OK, "Event processed successfully"};
        
    } catch (const std::exception& e) {
        return Error{Error::Code::NOT_FOUND, std::string("Event processing failed: ") + e.what()};
    }
}

void EngineImpl::evaluate_triggers(const Event& event) {
    std::lock_guard<std::mutex> lock(triggers_mutex_);
    
    auto current_time = clock_ ? clock_->current_time() :foundation::timestamp_now();
    
    for (const auto& [id, trigger] : triggers_) {
        if (!trigger->is_enabled()) {
            continue;
        }
        
        auto result = trigger->evaluate(event, current_time);
        if (result.code() == Error::Code::OK) {
            stats_.total_triggers_fired++;
        } else if (result.code() < 0) {
            INTERNAL_WARN_STREAM << "Trigger " << trigger->name() << " evaluation error: " << result.message();
        }
    }
}
 std::unique_ptr<Engine> Engine::create() {
        return std::make_unique<EngineImpl>();
    }
} // namespace engine
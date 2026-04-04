// astock_engine/core/EventSystem.cpp
#include "Event/EventSystem.hpp"
#include "Event/EventBusFactory.hpp"
#include "Event/EventTypeRegistry.hpp"
#include "foundation/json/json_facade.h"
#include "foundation/config/ConfigManager.hpp"
#include "foundation/log/logging.hpp"
#include "Event/DispatchConfig.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace engine {

// ===== EventSystem 实现 =====

EventSystem::EventSystem(const std::string& config_name)
    : config_name_(config_name)
    , config_(nullptr)
    , start_time_()
    , initialization_time_(std::chrono::system_clock::now()) {
    
    // 创建默认配置
    config_ = std::make_shared<foundation::config::ConfigNode>(
        create_default_config().getValue()
    );
    
    // 初始化组件
    if (!initialize_components()) {
        LOG_ERROR("Failed to initialize EventSystem components");
    }
}

EventSystem::EventSystem(const foundation::json::JsonFacade& config_json)
    : config_name_("event_system_custom")
    , config_(nullptr)
    , start_time_()
    , initialization_time_(std::chrono::system_clock::now()) {
    
    // 从提供的JSON创建配置
    config_ = std::make_shared<foundation::config::ConfigNode>(config_json.getValue());
    
    // 初始化组件
    if (!initialize_components()) {
        LOG_ERROR("Failed to initialize EventSystem components");
    }
}

EventSystem::~EventSystem() {
    stop(true, 3000); // 停止时等待3秒
}

bool EventSystem::initialize(const std::string& config_name) {
    if (initialized_) {
        LOG_WARN("EventSystem already initialized");
        return true;
    }
    
    config_name_ = config_name.empty() ? config_name_ : config_name;
    
    // 从配置管理器加载配置
    try {
        auto& config_mgr = foundation::config::ConfigManager::instance();
        config_ = config_mgr.getConfig(foundation::config::ConfigManager::Domain::MODULE); //先试用枚举配置，后续要使用配置文件的字符串初始化
        
        if (!config_ || config_->isNull()) {
            LOG_WARN("No configuration found for {}, using default", config_name_);
            config_ = std::make_shared<foundation::config::ConfigNode>(
                create_default_config().getValue()
            );
        }
        
        // 保存配置路径
       // config_file_path_ = config_mgr.getConfigFilePath(config_name_);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load configuration: {}", e.what());
        return false;
    }
    
    // 初始化组件
    if (!initialize_components()) {
        return false;
    }
    
    initialized_ = true;
    LOG_INFO("EventSystem initialized with config: {}", config_name_);
    
    return true;
}

bool EventSystem::start() {
    if (running_) {
        LOG_WARN("EventSystem already running");
        return true;
    }
    
    if (!initialized_ && !initialize(config_name_)) {
        LOG_ERROR("Failed to initialize EventSystem before starting");
        return false;
    }
    
    if (!dispatch_worker_) {
        LOG_ERROR("DispatchWorker not initialized");
        return false;
    }
    
    try {
        // 启动调度工作者
        dispatch_worker_->start();
        
        // 启动事件分发器
        event_dispatcher_->start();
        
        running_ = true;
        start_time_ = std::chrono::system_clock::now();
        
        LOG_INFO("EventSystem started successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start EventSystem: {}", e.what());
        return false;
    }
}

void EventSystem::stop(bool wait_completion, int timeout_ms) {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping EventSystem...");
    
    running_ = false;
    
    if (dispatch_worker_) {
        dispatch_worker_->stop();
    }
    
    if (event_dispatcher_) {
        event_dispatcher_->stop();
    }
    
    if (wait_completion && dispatch_worker_) {
        if (!dispatch_worker_->wait_for_completion(timeout_ms)) {
            LOG_WARN("Timeout while waiting for event processing completion");
        }
    }
    
    LOG_INFO("EventSystem stopped");
}

bool EventSystem::reconfigure(const foundation::json::JsonFacade& config_json) {
    if (running_) {
        LOG_ERROR("Cannot reconfigure while EventSystem is running");
        return false;
    }
    
    try {
        // 更新配置
        config_ = std::make_shared<foundation::config::ConfigNode>(config_json.getValue());
        
        // 重新初始化组件
        return initialize_components();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to reconfigure EventSystem: {}", e.what());
        return false;
    }
}
  // 转换事件类型
Event_Core::Type EventSystem::convert_event_type(const std::string& event_type) {
    const Event_Core::Type resolved = resolve_event_type(event_type);
    if (resolved != Event_Core::Type::CUSTOM) {
        return resolved;
    }

    LOG_DEBUG("Unknown event type '{}', using CUSTOM_EVENT", event_type);
    return Event_Core::Type::CUSTOM;
}

// 转换事件来源
std::string EventSystem::convert_source(const Event_Core::EventSource& source) {
        switch (source) {
            case Event_Core::EventSource::SYSTEM: return "system";
            case Event_Core::EventSource::MARKET_DATA: return "market_data";
            case Event_Core::EventSource::TRADING: return "trading_engine";
            case Event_Core::EventSource::RISK: return "risk_manager";
            case Event_Core::EventSource::STRATEGY: return "strategy";
            case Event_Core::EventSource::NETWORK : return "api";
            case Event_Core::EventSource::DATABASE: return "database";
           // case Event_Core::EventSource::SYSTEM : return "external";
            default: return "unknown";
        }
    }
        // 主要的转换函数
std::unique_ptr<Event> EventSystem::convert_to_engine_event(
        const EventFormat& event, 
        int priority_override) 
    {
        try {
            // 1. 转换事件类型
            Event_Core::Type event_type = convert_event_type(event.type);
            
            // 2. 转换时间戳（从微秒转换为 Timestamp）
            foundation::utils::Timestamp timestamp;
            if (event.timestamp > 0) {
                // 假设 Timestamp 有 from_microseconds 方法
                timestamp = foundation::utils::Timestamp::from_microseconds(event.timestamp);
            } else if (event.created_at > 0) {
                timestamp = foundation::utils::Timestamp::from_microseconds(event.created_at);
            } else {
                timestamp = foundation::utils::Timestamp::now();
            }
            
            // 3. 准备属性
            std::map<std::string, std::string> attributes;
            
            // 3.1 添加元数据
            attributes["id"] = event.id;
            attributes["type"] = event.type;
            attributes["source"] = convert_source(event.source);
            attributes["priority"] = std::to_string(static_cast<int>(event.priority));
            attributes["correlation_id"] = event.correlation_id;
            attributes["timestamp"] = std::to_string(event.timestamp);
            attributes["created_at"] = std::to_string(event.created_at);
            
            // 3.2 添加业务数据
            for (const auto& [key, value] : event.data) {
                // 将 EventValue 转换为字符串

                attributes[key] =  value.to_string();
            }
            
            // 3.3 添加扩展元数据
            for (const auto& [key, value] : event.metadata) {
                attributes["metadata." + key] = value;
            }
            
            // 3.4 添加系统信息
            attributes["system_time"] = std::to_string(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );
            
            // 4. 使用工厂方法创建事件
            return Event::create_from_strings(event_type, timestamp, attributes);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to convert EventFormat to engine Event: {}", e.what());
            return nullptr;
        }
    }
// 使用 EventFormat 自带的转换方法（如果存在）
std::unique_ptr<Event> EventSystem::convert_using_format_methods(const EventFormat& event) {
        try {
            // 如果 EventFormat 有 to_attributes() 方法
            auto attrs = event.to_attributes();
            
            // 转换事件类型
            Event_Core::Type event_type = convert_event_type(event.type);
            
            // 转换时间戳
            foundation::utils::Timestamp timestamp = 
                foundation::utils::Timestamp::from_microseconds(event.timestamp);
            
            // 添加额外的元数据（直接添加到 EventValue 映射）
            auto event_attrs = attrs;
            event_attrs["original_id"] = EventValue(event.id);
            event_attrs["correlation_id"] = EventValue(event.correlation_id);
            
            return Event::create(event_type, timestamp, event_attrs);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Conversion using format methods failed: {}", e.what());
            return nullptr;
        }
    }
bool EventSystem::publish_event(const EventFormat& event, int priority_override) {
    if (!running_) {
        LOG_ERROR("Cannot publish event: EventSystem not running");
        return false;
    }
    
    // 应用过滤器
    if (!apply_filters(event)) {
        LOG_DEBUG("Event filtered out: {}", event.type);
        return false;
    }
    
    try {
        // 1. 更新统计
        update_statistics(event);
        
        // 2. 转换为引擎事件
        auto engine_event = convert_to_engine_event(event, priority_override);
        if (!engine_event) {
            LOG_ERROR("Failed to convert event to engine format: {}", event.type);
            return false;
        }
        
        // 3. 添加到队列（使用事件优先级或覆盖优先级）
        int final_priority = priority_override >= 0 ? 
                           priority_override : 
                           static_cast<int>(event.priority);
        
        event_queue_->enqueue(std::move(event), std::chrono::steady_clock::now(),final_priority);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to publish event '{}': {}", event.type, e.what());
        return false;
    }
}

bool EventSystem::publish_json(const foundation::json::JsonFacade& event_json, int priority) {
    try {
        // 将JSON转换为EventFormat
        auto event_opt = json_to_event(event_json);
        if (!event_opt) {
            LOG_ERROR("Invalid event JSON");
            return false;
        }
        
        // 发布事件
        return publish_event(*event_opt, priority);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to publish JSON event: {}", e.what());
        return false;
    }
}

std::string EventSystem::subscribe(const std::string& event_type,
                                  std::function<void(const engine::EventFormat&)> handler,
                                  const std::string& subscriber_id) {
    if (!subscription_manager_) {
        LOG_ERROR("SubscriptionManager not initialized");
        return "";
    }
    
    try {
        // 注册订阅
        std::string sub_id = subscriber_id.empty() ? 
            foundation::utils::Uuid::generate().to_string() : subscriber_id;
        
        subscription_manager_->add_format_subscriber(event_type, handler);
        
        // 记录订阅关系
        {
            std::unique_lock lock(stats_mutex_);
            subscriber_to_subscriptions_[sub_id].push_back(sub_id);
            stats_.subscriber_counts[sub_id]++;
        }
        
        LOG_DEBUG("Subscribed to event type: {} with ID: {}", event_type, sub_id);
        return sub_id;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to subscribe to event type {}: {}", event_type, e.what());
        return "";
    }
}

foundation::json::JsonFacade EventSystem::export_config() const {
    try {
        if (!config_) {
            return foundation::json::JsonFacade::createObject();
        }
        
        // 创建配置JSON
        auto config_json = foundation::json::JsonFacade::createObject();
        
        // 系统配置
        auto system_config = foundation::json::JsonFacade::createObject();
        system_config.set("name", foundation::json::JsonFacade::createString(config_name_));
        system_config.set("config_file", foundation::json::JsonFacade::createString(config_file_path_));
        system_config.set("initialized", foundation::json::JsonFacade::createBool(initialized_));
        system_config.set("running", foundation::json::JsonFacade::createBool(running_));
        
        // 队列配置
        if (event_queue_) {
            auto queue_config = foundation::json::JsonFacade::createObject();
            queue_config.set("size", foundation::json::JsonFacade::createInt(
                static_cast<int>(event_queue_->size())));
            // queue_config.set("capacity", foundation::json::JsonFacade::createInt(
            //     static_cast<int>(event_queue_->capacity())));
            // system_config.set("queue", queue_config);
        }
        
        // 工作者配置
        if (dispatch_worker_) {
            auto worker_config = foundation::json::JsonFacade::createObject();
            worker_config.set("mode", foundation::json::JsonFacade::createString(
                dispatch_worker_->get_execution_mode() == ExecutionMode::Sync ? "sync" : "async"));
            auto worker_stats = dispatch_worker_->get_detailed_stats();
            worker_config.set("processed_count", foundation::json::JsonFacade::createInt(
                static_cast<int>(worker_stats.total_processed)));
            worker_config.set("queue_size", foundation::json::JsonFacade::createInt(
                static_cast<int>(worker_stats.queue_size)));
            system_config.set("worker", worker_config);
        }
        
        config_json.set("system", system_config);
        
        // 统计信息
        {
            std::shared_lock lock(stats_mutex_);
            auto stats_json = foundation::json::JsonFacade::createObject();
            stats_json.set("total_published", foundation::json::JsonFacade::createInt(
                static_cast<int>(stats_.total_events_published)));
            stats_json.set("total_processed", foundation::json::JsonFacade::createInt(
                static_cast<int>(stats_.total_events_processed)));
            stats_json.set("total_dropped", foundation::json::JsonFacade::createInt(
                static_cast<int>(stats_.total_events_dropped)));
            stats_json.set("max_queue_size", foundation::json::JsonFacade::createInt(
                static_cast<int>(stats_.max_queue_size)));
            stats_json.set("uptime_ms", foundation::json::JsonFacade::createInt(
                static_cast<int>(get_uptime().count())));
            
            // 事件类型统计
            auto type_stats = foundation::json::JsonFacade::createObject();
            for (const auto& [type, count] : stats_.event_type_counts) {
                type_stats.set(type, foundation::json::JsonFacade::createInt(
                    static_cast<int>(count)));
            }
            stats_json.set("event_types", type_stats);
            
            config_json.set("statistics", stats_json);
        }
        
        // 订阅关系
        auto subs_json = serialize_subscriptions();
        config_json.set("subscriptions", subs_json);
        
        return config_json;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to export config: {}", e.what());
        return foundation::json::JsonFacade::createObject();
    }
}

foundation::json::JsonFacade EventSystem::get_status_json() const {
    auto status_json = foundation::json::JsonFacade::createObject();
    
    // 基础状态
    status_json.set("name", foundation::json::JsonFacade::createString(config_name_));
    status_json.set("initialized", foundation::json::JsonFacade::createBool(initialized_));
    status_json.set("running", foundation::json::JsonFacade::createBool(running_));
    status_json.set("start_time", foundation::json::JsonFacade::createString(
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            start_time_.time_since_epoch()).count())));
    status_json.set("uptime_ms", foundation::json::JsonFacade::createInt(
        static_cast<int>(get_uptime().count())));
    
    // 队列状态
    if (event_queue_) {
        auto queue_status = foundation::json::JsonFacade::createObject();
        queue_status.set("size", foundation::json::JsonFacade::createInt(
            static_cast<int>(event_queue_->size())));
        queue_status.set("pending", foundation::json::JsonFacade::createInt(
            static_cast<int>(get_pending_count())));
        // queue_status.set("capacity", foundation::json::JsonFacade::createInt(
        //     static_cast<int>(event_queue_->capacity())));
        // status_json.set("queue", queue_status);
    }
    
    // 工作者状态
    if (dispatch_worker_) {
        auto worker_status = foundation::json::JsonFacade::createObject();
        auto worker_stats = dispatch_worker_->get_detailed_stats();
        worker_status.set("mode", foundation::json::JsonFacade::createString(
            dispatch_worker_->get_execution_mode() == ExecutionMode::Sync ? "sync" : "async"));
        worker_status.set("processed", foundation::json::JsonFacade::createInt(
            static_cast<int>(worker_stats.total_processed)));
        worker_status.set("queue_size", foundation::json::JsonFacade::createInt(
            static_cast<int>(worker_stats.queue_size)));
        worker_status.set("idle_time_ms", foundation::json::JsonFacade::createInt(
            static_cast<int>(worker_stats.idle_time_ms.count())));
        status_json.set("worker", worker_status);
    }
    
    // 订阅状态
    if (subscription_manager_) {
        auto sub_status = foundation::json::JsonFacade::createObject();
        auto sub_stats = get_subscriber_stats();
        sub_status.set("total_subscribers", foundation::json::JsonFacade::createInt(
            static_cast<int>(sub_stats.size())));
        
        auto subscribers_json = foundation::json::JsonFacade::createObject();
        for (const auto& [id, count] : sub_stats) {
            subscribers_json.set(id, foundation::json::JsonFacade::createInt(
                static_cast<int>(count)));
        }
        sub_status.set("subscribers", subscribers_json);
        
        status_json.set("subscriptions", sub_status);
    }
    
    return status_json;
}

foundation::json::JsonFacade EventSystem::event_to_json(const engine::EventFormat& event) {
    auto json = foundation::json::JsonFacade::createObject();
    
    // 核心字段
    json.set("event_type", foundation::json::JsonFacade::createString(event.type));
    json.set("source", foundation::json::JsonFacade::createString(event_source_to_string(event.source)));
    json.set("timestamp_us", foundation::json::JsonFacade::createInt(
        static_cast<int>(event.timestamp)));
    
    // 数据字段
    auto data_json = foundation::json::JsonFacade::createObject();
   for (const auto& [key, value] : event.data) {
        // 直接使用 EventValue 的 to_json() 方法
        data_json.set(key, value.to_json());
    }
    json.set("data", data_json);
    
    return json;
}

std::optional<engine::EventFormat> EventSystem::json_to_event(const foundation::json::JsonFacade& json) {
    try {
        if (!json.isObject()) {
            LOG_ERROR("JSON is not an object");
            return std::nullopt;
        }
        
        engine::EventFormat event;
        
        // 解析核心字段
        if (json.has("event_type")) {
            event.type = json.get("event_type").asString();
        }
        
        if (json.has("source")) {
            event.source = string_to_event_source(json.get("source").asString());
        }
        
        if (json.has("timestamp_us")) {
            event.timestamp = json.get("timestamp_us").asInt();
        }
        
        // 解析数据字段
        if (json.has("data") && json.get("data").isObject()) {
            auto data_json = json.get("data");
            
            // 遍历所有键值对
            // 注意：这里简化处理，实际需要根据数据类型推断
            // 这里假设所有值都是字符串，需要类型推断
            // 实际实现中可能需要更复杂的类型推断逻辑
            
            // 简化实现：只处理基础类型
            // 实际项目中应该根据数据类型标记来处理
        }
        
        return event;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse event from JSON: {}", e.what());
        return std::nullopt;
    }
}

bool EventSystem::save_state_to_file(const std::string& filepath,
                                    bool include_events,
                                    bool include_subscriptions) {
    try {
        auto state_json = foundation::json::JsonFacade::createObject();
        
        // 版本信息
        state_json.set("version", foundation::json::JsonFacade::createString("1.0"));
        state_json.set("timestamp", foundation::json::JsonFacade::createString(
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())));
        
        // 配置
        state_json.set("config", export_config());
        
        // 事件（如果需要）
        if (include_events && event_queue_) {
            auto events_json = export_events_json(1000); // 最多1000个事件
            state_json.set("events", events_json);
        }
        
        // 订阅关系（如果需要）
        if (include_subscriptions) {
            auto subs_json = export_subscriptions_json();
            state_json.set("subscriptions", subs_json);
        }
        
        // 写入文件
        std::ofstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open file: {}", filepath);
            return false;
        }
        
        file << state_json.toPrettyString();
        file.close();
        
        LOG_INFO("EventSystem state saved to: {}", filepath);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save state to file: {}", e.what());
        return false;
    }
}
bool EventSystem::initialize_components() {
    try {
        // 创建事件队列
        size_t queue_capacity = 10000;
        if (config_->has("queue_capacity")) {
            queue_capacity = config_->get("queue_capacity").asInt();
        }
        event_queue_ = std::make_shared<EventQueue>();// 这里后期可能要加入最大队列容量
        // 创建订阅管理器
        subscription_manager_ = std::make_shared<SubscriptionManager>();
        
        // 创建事件分发器
        //event_dispatcher_ = std::make_shared<IEventDispatcher>();
        auto event_dispatcher_ = EventDispatcherFactory::create_async_default();
        
        // 创建调度策略（修改这里）
        std::shared_ptr<engine::DispatchPolicy> policy;
        engine::DispatchConfig policy_cfg;
        
        // 配置调度策略
        if (config_->has("dispatch_policy")) {
            auto policy_config = config_->get("dispatch_policy");
            
            // 直接创建策略，不使用工厂
            std::string mode = "hybrid"; // 默认值
            if (policy_config.has("mode")) {
                mode = policy_config.get("mode").asString();
            }
            
            // 转换为小写
            std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
            
            if (mode == "immediate") {
                policy = std::make_shared<engine::ImmediatePolicy>();
                policy_cfg.mode = engine::DispatchMode::Immediate;
                
            } else if (mode == "batch") {
                size_t batch_size = 100;
                if (policy_config.has("batch_size")) {
                    batch_size = static_cast<size_t>(policy_config.get("batch_size").asInt());
                }
                policy = std::make_shared<engine::BatchPolicy>(batch_size);
                policy_cfg.mode = engine::DispatchMode::Batch;
                policy_cfg.batch_size = batch_size;
                
            } else if (mode == "interval" || mode == "timebased") {
                std::chrono::milliseconds interval = std::chrono::milliseconds(50);
                if (policy_config.has("dispatch_interval_ms")) {
                    int interval_ms = policy_config.get("dispatch_interval_ms").asInt();
                    interval = std::chrono::milliseconds(interval_ms);
                }
                policy = std::make_shared<engine::TimePolicy>(interval);
                policy_cfg.mode = engine::DispatchMode::TimeBased;
                policy_cfg.interval = interval;
                
            } else if (mode == "hybrid") {
                size_t batch_size = 100;
                std::chrono::milliseconds interval = std::chrono::milliseconds(50);
                
                if (policy_config.has("batch_size")) {
                    batch_size = static_cast<size_t>(policy_config.get("batch_size").asInt());
                }
                
                if (policy_config.has("dispatch_interval_ms")) {
                    int interval_ms = policy_config.get("dispatch_interval_ms").asInt();
                    interval = std::chrono::milliseconds(interval_ms);
                }
                
                policy = std::make_shared<engine::HybridPolicy>(batch_size, interval);
                policy_cfg.mode = engine::DispatchMode::Hybrid;
                policy_cfg.batch_size = batch_size;
                policy_cfg.interval = interval;
                
            } else {
                // 未知模式，使用默认混合策略
                LOG_WARN("Unknown dispatch mode: {}, using hybrid", mode);
                policy = std::make_shared<engine::HybridPolicy>(100, std::chrono::milliseconds(50));
                policy_cfg.mode = engine::DispatchMode::Hybrid;
                policy_cfg.batch_size = 100;
                policy_cfg.interval = std::chrono::milliseconds(50);
            }
            
            // 保存配置
            dispatch_config_ = policy_cfg;
        } else {
            // 使用默认策略
            policy = std::make_shared<engine::HybridPolicy>(100, std::chrono::milliseconds(50));
            policy_cfg.mode = engine::DispatchMode::Hybrid;
            policy_cfg.batch_size = 100;
            policy_cfg.interval = std::chrono::milliseconds(50);
        }
        
        // 创建调度策略包装器
        dispatch_strategy_ = std::make_shared<engine::DispatchStrategy>()->get_policy();
        
        // 创建调度工作者
        DispatchWorkerConfig worker_config;
        
        // 配置执行模式
        if (config_->has("execution_mode")) {
            std::string mode = config_->get("execution_mode").asString();
            worker_config.mode = (mode == "async") ? ExecutionMode::Async : ExecutionMode::Sync;
        } else {
            // 根据策略模式设置默认执行模式
            worker_config.mode = (policy_cfg.mode == engine::DispatchMode::Immediate) 
                               ? ExecutionMode::Sync 
                               : ExecutionMode::Async;
        }
        
        // 配置工作线程
        if (config_->has("worker_threads")) {
            worker_config.worker_threads = config_->get("worker_threads").asInt();
        } else {
            worker_config.worker_threads = (policy_cfg.mode == engine::DispatchMode::Immediate) 
                                         ? 1 
                                         : 4; // 其他模式用多线程
        }
        
        // 配置批处理
        if (config_->has("batch_processing")) {
            worker_config.enable_batch_processing = config_->get("batch_processing").asBool();
        } else {
            worker_config.enable_batch_processing = 
                (policy_cfg.mode == engine::DispatchMode::Batch || 
                 policy_cfg.mode == engine::DispatchMode::Hybrid);
        }
        
        if (config_->has("batch_size")) {
            worker_config.batch_size = config_->get("batch_size").asInt();
        } else {
            worker_config.batch_size = policy_cfg.batch_size;
        }
        
        // 配置轮询间隔
        if (config_->has("poll_interval_ms")) {
            worker_config.poll_interval = std::chrono::milliseconds(
                config_->get("poll_interval_ms").asInt());
        } else {
            worker_config.poll_interval = policy_cfg.interval;
        }
        std::shared_ptr<IEventDispatcher> dispatcher = EventDispatcherFactory::create_async_default();
        // 创建调度工作者
        dispatch_worker_ = std::make_shared<DispatchWorker>(
            event_queue_,
            subscription_manager_,
            dispatcher,
            dispatch_strategy_,
            worker_config  
        );
        
        // 配置性能监控
        if (config_->has("enable_monitoring")) {
            enable_performance_monitoring_ = config_->get("enable_monitoring").asBool();
        }
        
        LOG_INFO("EventSystem components initialized");
        LOG_INFO("Dispatch policy: {}, batch_size={}, interval={}ms", 
                policy->mode() == engine::DispatchMode::Immediate ? "Immediate" :
                policy->mode() == engine::DispatchMode::Batch ? "Batch" :
                policy->mode() == engine::DispatchMode::TimeBased ? "TimeBased" : "Hybrid",
                policy->batch_size(),
                policy->interval().count());
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize EventSystem components: {}", e.what());
        return false;
    }
}


foundation::json::JsonFacade EventSystem::create_default_config() const {
    auto config_json = foundation::json::JsonFacade::createObject();
    
    // 基本配置
    config_json.set("queue_capacity", foundation::json::JsonFacade::createInt(10000));
    config_json.set("execution_mode", foundation::json::JsonFacade::createString("async"));
    config_json.set("worker_threads", foundation::json::JsonFacade::createInt(2));
    config_json.set("enable_monitoring", foundation::json::JsonFacade::createBool(true));
    
    // 批处理配置
    config_json.set("batch_processing", foundation::json::JsonFacade::createBool(true));
    config_json.set("batch_size", foundation::json::JsonFacade::createInt(100));
    
    // 调度策略配置
    auto policy_config = foundation::json::JsonFacade::createObject();
    policy_config.set("mode", foundation::json::JsonFacade::createString("batch"));
    policy_config.set("batch_size", foundation::json::JsonFacade::createInt(100));
    policy_config.set("dispatch_interval_ms", foundation::json::JsonFacade::createInt(100));
    policy_config.set("max_queue_size", foundation::json::JsonFacade::createInt(5000));
    config_json.set("dispatch_policy", policy_config);
    
    // 性能配置
    config_json.set("poll_interval_ms", foundation::json::JsonFacade::createInt(50));
    config_json.set("handler_timeout_ms", foundation::json::JsonFacade::createInt(5000));
    
    return config_json;
}

// ===== 工厂函数实现 =====

std::unique_ptr<EventSystem> create_default_event_system() {
    return std::make_unique<EventSystem>("event_system_default");
}

std::unique_ptr<EventSystem> create_high_performance_event_system(
    size_t worker_threads, size_t batch_size) {
    
    // 创建高性能配置
    auto config_json = foundation::json::JsonFacade::createObject();
    config_json.set("execution_mode", foundation::json::JsonFacade::createString("async"));
    config_json.set("worker_threads", foundation::json::JsonFacade::createInt(
        static_cast<int>(worker_threads)));
    config_json.set("batch_processing", foundation::json::JsonFacade::createBool(true));
    config_json.set("batch_size", foundation::json::JsonFacade::createInt(
        static_cast<int>(batch_size)));
    config_json.set("queue_capacity", foundation::json::JsonFacade::createInt(50000));
    config_json.set("enable_monitoring", foundation::json::JsonFacade::createBool(true));
    config_json.set("poll_interval_ms", foundation::json::JsonFacade::createInt(10));
    
    return std::make_unique<EventSystem>(config_json);
}

std::unique_ptr<EventSystem> create_event_system_from_file(
    const std::string& config_file_path) {
    
    try {
        // 从文件加载JSON配置
        auto config_json = foundation::json::JsonFacade::parseFile(config_file_path);
        return std::make_unique<EventSystem>(config_json);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create EventSystem from file {}: {}", 
                  config_file_path, e.what());
        return nullptr;
    }
}

// ===== EventSystemManager 实现 =====

EventSystemManager& EventSystemManager::instance() {
    static EventSystemManager instance;
    return instance;
}

bool EventSystemManager::register_system(const std::string& name, 
                                         std::shared_ptr<EventSystem> system) {
    std::unique_lock lock(mutex_);
    
    if (systems_.find(name) != systems_.end()) {
        LOG_WARN("EventSystem with name '{}' already registered", name);
        return false;
    }
    
    systems_[name] = std::move(system);
    LOG_INFO("EventSystem '{}' registered", name);
    return true;
}

std::shared_ptr<EventSystem> EventSystemManager::get_system(const std::string& name) {
    std::shared_lock lock(mutex_);
    
    auto it = systems_.find(name);
    if (it != systems_.end()) {
        return it->second;
    }
    
    return nullptr;
}

foundation::json::JsonFacade EventSystemManager::get_all_status_json() const {
    std::shared_lock lock(mutex_);
    
    auto systems_json = foundation::json::JsonFacade::createObject();
    
    for (const auto& [name, system] : systems_) {
        if (system) {
            systems_json.set(name, system->get_status_json());
        }
    }
    
    auto result = foundation::json::JsonFacade::createObject();
    result.set("total_systems", foundation::json::JsonFacade::createInt(
        static_cast<int>(systems_.size())));
    result.set("systems", systems_json);
    
    return result;
}

} // namespace engine
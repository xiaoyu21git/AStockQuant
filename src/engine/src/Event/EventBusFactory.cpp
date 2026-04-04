// astock_engine/core/EventBusFactory.cpp
#include "Event/EventBus.hpp"
#include "Event/EventBusImpl.h"
#include "Event/EventTypeRegistry.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"
#include <memory>

namespace engine {

// ============ 默认配置 ============

EventBus::Config create_default_config() {
    EventBus::Config config;
    
    // 基础配置
    config.worker_threads = std::thread::hardware_concurrency();
    if (config.worker_threads == 0) {
        config.worker_threads = 4;
    }
    
    config.max_queue_size = 10000;
    config.enable_priority_queue = true;
    config.batch_size = 100;
    config.execution_mode = ExecutionMode::Async;
    config.enable_event_format = true;
    config.auto_convert_formats = true;
    
    return config;
}

EventBus::Config create_sync_config() {
    EventBus::Config config = create_default_config();
    config.execution_mode = ExecutionMode::Sync;
    config.worker_threads = 0; // 同步模式不需要工作线程
    return config;
}

EventBus::Config create_high_performance_config() {
    EventBus::Config config = create_default_config();
    config.worker_threads = std::thread::hardware_concurrency() * 2;
    config.max_queue_size = 100000;
    config.batch_size = 500;
    return config;
}

EventBus::Config create_low_memory_config() {
    EventBus::Config config = create_default_config();
    config.max_queue_size = 1000;
    config.batch_size = 10;
    config.enable_priority_queue = false;
    return config;
}

// ============ 工厂方法实现 ============

std::unique_ptr<EventBus> EventBus::create(
    std::shared_ptr<foundation::thread::IExecutor> executor) {
    
    Config config = create_default_config();
    config.executor = executor;
    
    return std::make_unique<EventBusImpl>(config);
}

std::unique_ptr<EventBus> EventBusFactory::create_default() {
    // 创建默认线程池执行器
    auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(
        std::thread::hardware_concurrency()
    );
    
    return EventBus::create(executor);
}

std::unique_ptr<EventBus> EventBusFactory::create_sync() {
    Config config = create_sync_config();
    return std::make_unique<EventBusImpl>(config);
}

std::unique_ptr<EventBus> EventBusFactory::create_async(
    size_t worker_threads,
    std::shared_ptr<foundation::thread::IExecutor> executor) {
    
    Config config = create_default_config();
    config.execution_mode = ExecutionMode::Async;
    config.worker_threads = worker_threads;
    config.executor = executor;
    
    return std::make_unique<EventBusImpl>(config);
}

std::unique_ptr<EventBus> EventBusFactory::create_high_performance() {
    Config config = create_high_performance_config();
    
    // 创建高性能线程池
    auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(
        config.worker_threads,
        config.worker_threads * 2,
        std::chrono::seconds(60),
        "EventBus-HighPerf"
    );
    
    config.executor = executor;
    return std::make_unique<EventBusImpl>(config);
}

std::unique_ptr<EventBus> EventBusFactory::create_low_memory() {
    Config config = create_low_memory_config();
    return std::make_unique<EventBusImpl>(config);
}

std::unique_ptr<EventBus> EventBusFactory::create_with_config(const Config& config) {
    return std::make_unique<EventBusImpl>(config);
}

std::unique_ptr<EventBus> EventBusFactory::create_from_json(const std::string& json_config) {
    try {
        auto json = foundation::json::JsonFacade::parse(json_config);
        if (json.empty() || !json.isObject()) {
            return create_default();
        }
        
        Config config = create_default_config();
        
        // 解析配置
        if (json.has("worker_threads")) {
            config.worker_threads = json.get("worker_threads").asInt();
        }
        
        if (json.has("max_queue_size")) {
            config.max_queue_size = json.get("max_queue_size").asInt();
        }
        
        if (json.has("execution_mode")) {
            std::string mode = json.get("execution_mode").asString();
            if (mode == "sync") {
                config.execution_mode = ExecutionMode::Sync;
            } else if (mode == "async") {
                config.execution_mode = ExecutionMode::Async;
            }
        }
        
        if (json.has("enable_event_format")) {
            config.enable_event_format = json.get("enable_event_format").asBool();
        }
        
        if (json.has("auto_convert_formats")) {
            config.auto_convert_formats = json.get("auto_convert_formats").asBool();
        }
        
        if (json.has("enable_priority_queue")) {
            config.enable_priority_queue = json.get("enable_priority_queue").asBool();
        }
        
        if (json.has("batch_size")) {
            config.batch_size = json.get("batch_size").asInt();
        }
        
        // 创建执行器（如果需要）
        if (config.execution_mode == ExecutionMode::Async && !config.executor) {
            auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(
                config.worker_threads
            );
            config.executor = executor;
        }
        
        return std::make_unique<EventBusImpl>(config);
        
    } catch (...) {
        // 解析失败，返回默认配置
        return create_default();
    }
}

// ============ 配置构建器 ============

class ConfigBuilder::Impl {
public:
    EventBus::Config config;
    
    Impl() : config(create_default_config()) {}
};

ConfigBuilder::ConfigBuilder() : impl_(std::make_unique<Impl>()) {}
ConfigBuilder::~ConfigBuilder() = default;

ConfigBuilder& ConfigBuilder::worker_threads(size_t threads) {
    impl_->config.worker_threads = threads;
    return *this;
}

ConfigBuilder& ConfigBuilder::max_queue_size(size_t size) {
    impl_->config.max_queue_size = size;
    return *this;
}

ConfigBuilder& ConfigBuilder::execution_mode(ExecutionMode mode) {
    impl_->config.execution_mode = mode;
    return *this;
}

ConfigBuilder& ConfigBuilder::enable_event_format(bool enable) {
    impl_->config.enable_event_format = enable;
    return *this;
}

ConfigBuilder& ConfigBuilder::auto_convert_formats(bool auto_convert) {
    impl_->config.auto_convert_formats = auto_convert;
    return *this;
}

ConfigBuilder& ConfigBuilder::enable_priority_queue(bool enable) {
    impl_->config.enable_priority_queue = enable;
    return *this;
}

ConfigBuilder& ConfigBuilder::batch_size(size_t size) {
    impl_->config.batch_size = size;
    return *this;
}

ConfigBuilder& ConfigBuilder::executor(std::shared_ptr<foundation::thread::IExecutor> executor) {
    impl_->config.executor = executor;
    return *this;
}

ConfigBuilder& ConfigBuilder::from_json(const std::string& json_config) {
    try {
        auto json = foundation::json::JsonFacade::parse(json_config);
        if (!json.empty() && json.isObject()) {
            if (json.has("worker_threads")) {
                impl_->config.worker_threads = json.get("worker_threads").asInt();
            }
            if (json.has("max_queue_size")) {
                impl_->config.max_queue_size = json.get("max_queue_size").asInt();
            }
            if (json.has("execution_mode")) {
                std::string mode = json.get("execution_mode").asString();
                if (mode == "sync") {
                    impl_->config.execution_mode = ExecutionMode::Sync;
                } else if (mode == "async") {
                    impl_->config.execution_mode = ExecutionMode::Async;
                }
            }
            if (json.has("enable_event_format")) {
                impl_->config.enable_event_format = json.get("enable_event_format").asBool();
            }
            if (json.has("auto_convert_formats")) {
                impl_->config.auto_convert_formats = json.get("auto_convert_formats").asBool();
            }
            if (json.has("enable_priority_queue")) {
                impl_->config.enable_priority_queue = json.get("enable_priority_queue").asBool();
            }
            if (json.has("batch_size")) {
                impl_->config.batch_size = json.get("batch_size").asInt();
            }
        }
    } catch (...) {
        // 保持当前配置
    }
    return *this;
}

EventBus::Config ConfigBuilder::build() {
    return impl_->config;
}

std::unique_ptr<EventBus> ConfigBuilder::build_bus() {
    // 如果没有执行器且需要异步模式，创建默认执行器
    if (!impl_->config.executor && impl_->config.execution_mode == ExecutionMode::Async) {
        auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(
            impl_->config.worker_threads
        );
        impl_->config.executor = executor;
    }
    
    return std::make_unique<EventBusImpl>(impl_->config);
}

// ============ 预配置的 EventBus 实例 ============

namespace {
    std::once_flag default_bus_flag;
    std::unique_ptr<EventBus> default_bus;
    
    std::once_flag sync_bus_flag;
    std::unique_ptr<EventBus> sync_bus;
    
    std::once_flag high_perf_bus_flag;
    std::unique_ptr<EventBus> high_perf_bus;
}

EventBus& EventBusFactory::default_bus() {
    std::call_once(default_bus_flag, []() {
        default_bus = create_default();
        default_bus->start();
    });
    return *default_bus;
}

EventBus& EventBusFactory::sync_bus() {
    std::call_once(sync_bus_flag, []() {
        sync_bus = create_sync();
        sync_bus->start();
    });
    return *sync_bus;
}

EventBus& EventBusFactory::high_performance_bus() {
    std::call_once(high_perf_bus_flag, []() {
        high_perf_bus = create_high_performance();
        high_perf_bus->start();
    });
    return *high_perf_bus;
}

void EventBusFactory::shutdown_all() {
    if (default_bus) {
        default_bus->stop();
        default_bus.reset();
    }
    if (sync_bus) {
        sync_bus->stop();
        sync_bus.reset();
    }
    if (high_perf_bus) {
        high_perf_bus->stop();
        high_perf_bus.reset();
    }
    
    // 重置 once_flag
    default_bus_flag = std::once_flag();
    sync_bus_flag = std::once_flag();
    high_perf_bus_flag = std::once_flag();
}

// ============ 工具函数 ============

std::string EventBusFactory::get_default_config_json() {
    auto config = create_default_config();
    
    foundation::json::JsonFacade json = foundation::json::JsonFacade::createObject();
    json.set("worker_threads", foundation::json::JsonFacade::createInt(config.worker_threads));
    json.set("max_queue_size", foundation::json::JsonFacade::createInt(config.max_queue_size));
    json.set("execution_mode", foundation::json::JsonFacade::createString("async"));
    json.set("enable_event_format", foundation::json::JsonFacade::createBool(config.enable_event_format));
    json.set("auto_convert_formats", foundation::json::JsonFacade::createBool(config.auto_convert_formats));
    json.set("enable_priority_queue", foundation::json::JsonFacade::createBool(config.enable_priority_queue));
    json.set("batch_size", foundation::json::JsonFacade::createInt(config.batch_size));
    
    return json.toPrettyString(2);
}

std::vector<std::string> EventBusFactory::get_available_presets() {
    return {
        "default",
        "sync",
        "async",
        "high_performance", 
        "low_memory",
        "custom"
    };
}

std::unique_ptr<EventBus> EventBusFactory::create_from_preset(const std::string& preset) {
    if (preset == "default") {
        return create_default();
    } else if (preset == "sync") {
        return create_sync();
    } else if (preset == "async") {
        return create_async();
    } else if (preset == "high_performance") {
        return create_high_performance();
    } else if (preset == "low_memory") {
        return create_low_memory();
    } else {
        return create_default();
    }
}

// ============ 配置验证 ============

bool EventBusFactory::validate_config(const Config& config, std::string& error_message) {
    if (config.execution_mode == ExecutionMode::Async && config.worker_threads == 0) {
        error_message = "Async mode requires worker_threads > 0";
        return false;
    }
    
    if (config.max_queue_size == 0) {
        error_message = "max_queue_size must be greater than 0";
        return false;
    }
    
    if (config.batch_size == 0) {
        error_message = "batch_size must be greater than 0";
        return false;
    }
    
    if (config.batch_size > config.max_queue_size) {
        error_message = "batch_size cannot be larger than max_queue_size";
        return false;
    }
    
    return true;
}

// ============ 事件类型注册辅助 ============

void EventBusFactory::register_common_event_types(EventBus& bus) {
    auto* impl = dynamic_cast<EventBusImpl*>(&bus);
    if (!impl) {
        return;
    }

    for (const auto& mapping : kCanonicalEventTypeMappings) {
        impl->register_event_type(std::string(mapping.event_type), mapping.engine_type);
    }
}

std::unordered_map<std::string, Event_Core::Type> EventBusFactory::get_common_event_types() {
    return build_registered_event_type_map();
}

// ============ 批量创建 ============

std::vector<std::unique_ptr<EventBus>> EventBusFactory::create_multiple(
    size_t count,
    const Config& base_config) {
    
    std::vector<std::unique_ptr<EventBus>> buses;
    buses.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        Config config = base_config;
        
        // 为每个实例设置唯一名称（如果有的话）
        if (config.executor) {
            // 可以在这里定制每个实例
        }
        
        buses.push_back(std::make_unique<EventBusImpl>(config));
    }
    
    return buses;
}

} // namespace engine
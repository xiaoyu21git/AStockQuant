// astock_engine/core/EventBusFactory.hpp
#pragma once

#include "EventBus.hpp"
#include <memory>
#include <string>
#include <vector>

namespace engine {

class EventBusFactory {
public:
    // ===== 预置配置创建 =====
    static std::unique_ptr<EventBus> create_default();
    static std::unique_ptr<EventBus> create_sync();
    static std::unique_ptr<EventBus> create_async(
        size_t worker_threads = 0,
        std::shared_ptr<foundation::thread::IExecutor> executor = nullptr);
    static std::unique_ptr<EventBus> create_high_performance();
    static std::unique_ptr<EventBus> create_low_memory();
    
    // ===== 自定义配置创建 =====
    static std::unique_ptr<EventBus> create_with_config(const EventBus::Config& config);
    static std::unique_ptr<EventBus> create_from_json(const std::string& json_config);
    
    // ===== 预配置的单例实例 =====
    static EventBus& default_bus();
    static EventBus& sync_bus();
    static EventBus& high_performance_bus();
    static void shutdown_all();
    
    // ===== 配置构建器 =====
    class ConfigBuilder {
    public:
        ConfigBuilder();
        ~ConfigBuilder();
        
        ConfigBuilder& worker_threads(size_t threads);
        ConfigBuilder& max_queue_size(size_t size);
        ConfigBuilder& execution_mode(ExecutionMode mode);
        ConfigBuilder& enable_event_format(bool enable);
        ConfigBuilder& auto_convert_formats(bool auto_convert);
        ConfigBuilder& enable_priority_queue(bool enable);
        ConfigBuilder& batch_size(size_t size);
        ConfigBuilder& executor(std::shared_ptr<foundation::thread::IExecutor> executor);
        ConfigBuilder& from_json(const std::string& json_config);
        
        EventBus::Config build();
        std::unique_ptr<EventBus> build_bus();
        
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
    
    // ===== 工具函数 =====
    static std::string get_default_config_json();
    static std::vector<std::string> get_available_presets();
    static std::unique_ptr<EventBus> create_from_preset(const std::string& preset);
    
    // ===== 配置验证 =====
    static bool validate_config(const EventBus::Config& config, std::string& error_message);
    
    // ===== 事件类型管理 =====
    static void register_common_event_types(EventBus& bus);
    static std::unordered_map<std::string, Event_Core::Type> get_common_event_types();
    
    // ===== 批量创建 =====
    static std::vector<std::unique_ptr<EventBus>> create_multiple(
        size_t count,
        const EventBus::Config& base_config = EventBus::Config());
    
private:
    EventBusFactory() = delete;
};

} // namespace engine
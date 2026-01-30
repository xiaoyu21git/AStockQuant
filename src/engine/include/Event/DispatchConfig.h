// DispatchConfig.h
#pragma once
#include <string>
#include <chrono>
#include <atomic>
#include <filesystem>
#include "foundation.h"
namespace engine {

struct DispatchConfig {
    // 原有的策略字段
    DispatchMode mode = DispatchMode::Hybrid;
    size_t batch_size = 100;
    std::chrono::milliseconds interval = std::chrono::milliseconds(50);
    std::string name = "default";
    static DispatchConfig default_config() {
        return DispatchConfig{};  // 返回默认构造的对象
    }
     // JSON序列化/反序列化
    std::string to_json()  {
        return config_to_json(*this).toString();
    }
    // 你需要的额外字段（根据你的代码添加）
    ExecutionMode exec_mode = ExecutionMode::Sync;  // 执行模式
    size_t worker_threads = 1;
    std::chrono::milliseconds poll_interval = std::chrono::milliseconds(50);
    bool enable_priority_queue = true;
    bool enable_batch_processing = true;
    bool enable_load_balancing = false;
    std::shared_ptr<foundation::thread::IExecutor> executor;  // 可能为nullptr
     // 反序列化：从JSON字符串解析
    static DispatchConfig from_json(const std::string& json_str) {
        auto json_facade = foundation::json::JsonFacade::parse(json_str);
        return parse_config_from_json(json_facade);
    }
    bool validate() const {
        return batch_size > 0 && interval.count() > 0 && worker_threads > 0;
    }
    
    DispatchConfig() = default;
    // 函数声明
    static  DispatchConfig parse_config_from_json(const foundation::json::JsonFacade& json);
    static foundation::json::JsonFacade config_to_json(const DispatchConfig& config) ;
};



// 函数实现（放在同一个头文件中，因为是模板/简单函数）
inline DispatchConfig parse_config_from_json(const foundation::json::JsonFacade& json) {
    DispatchConfig config;
    
    if (json.empty() || !json.isObject()) {
        return config;
    }
    
    // 解析策略 mode
    if (json.has("mode")) {
        std::string mode_str = json.get("mode").asString();
        if (mode_str == "Immediate") config.mode = DispatchMode::Immediate;
        else if (mode_str == "Batch") config.mode = DispatchMode::Batch;
        else if (mode_str == "TimeBased") config.mode = DispatchMode::TimeBased;
        else if (mode_str == "Hybrid") config.mode = DispatchMode::Hybrid;
    }
    
    // 解析 batch_size
    if (json.has("batch_size")) {
        config.batch_size = static_cast<size_t>(json.get("batch_size").asInt());
    }
    
    // 解析 interval_ms
    if (json.has("interval_ms")) {
        int interval_ms = json.get("interval_ms").asInt();
        config.interval = std::chrono::milliseconds(interval_ms);
    }
    
    // 解析 name
    if (json.has("name")) {
        config.name = json.get("name").asString();
    }
    
    // 解析 exec_mode (执行模式)
    if (json.has("exec_mode")) {
        std::string exec_mode_str = json.get("exec_mode").asString();
        if (exec_mode_str == "Sync") {
            config.exec_mode = ExecutionMode::Sync;
        } else if (exec_mode_str == "Async") {
            config.exec_mode = ExecutionMode::Async;
        }
    }
    
    // 解析 worker_threads
    if (json.has("worker_threads")) {
        config.worker_threads = static_cast<size_t>(json.get("worker_threads").asInt());
    }
    
    // 解析 poll_interval_ms
    if (json.has("poll_interval_ms")) {
        int poll_interval_ms = json.get("poll_interval_ms").asInt();
        config.poll_interval = std::chrono::milliseconds(poll_interval_ms);
    }
    
    // 解析 enable_priority_queue
    if (json.has("enable_priority_queue")) {
        config.enable_priority_queue = json.get("enable_priority_queue").asBool();
    }
    
    // 解析 enable_batch_processing
    if (json.has("enable_batch_processing")) {
        config.enable_batch_processing = json.get("enable_batch_processing").asBool();
    }
    
    // 解析 enable_load_balancing
    if (json.has("enable_load_balancing")) {
        config.enable_load_balancing = json.get("enable_load_balancing").asBool();
    }
    
    // executor 通常不从JSON解析，因为是指针
    
    return config;
}

inline foundation::json::JsonFacade DispatchConfig::config_to_json(const DispatchConfig& config) {
    auto json = foundation::json::JsonFacade::createObject();
    
    // mode (策略模式)
    std::string mode_str;
    switch (config.mode) {
        case DispatchMode::Immediate: mode_str = "Immediate"; break;
        case DispatchMode::Batch: mode_str = "Batch"; break;
        case DispatchMode::TimeBased: mode_str = "TimeBased"; break;
        case DispatchMode::Hybrid: mode_str = "Hybrid"; break;
        default: mode_str = "Hybrid";
    }
    json.set("mode", foundation::json::JsonFacade::createString(mode_str));
    
    // batch_size
    json.set("batch_size", 
             foundation::json::JsonFacade::createInt(static_cast<int>(config.batch_size)));
    
    // interval_ms
    json.set("interval_ms", 
             foundation::json::JsonFacade::createInt(static_cast<int>(config.interval.count())));
    
    // name
    if (!config.name.empty()) {
        json.set("name", foundation::json::JsonFacade::createString(config.name));
    }
    
    // exec_mode (执行模式)
    std::string exec_mode_str = (config.exec_mode == ExecutionMode::Sync) ? "Sync" : "Async";
    json.set("exec_mode", foundation::json::JsonFacade::createString(exec_mode_str));
    
    // worker_threads
    json.set("worker_threads", 
             foundation::json::JsonFacade::createInt(static_cast<int>(config.worker_threads)));
    
    // poll_interval_ms
    json.set("poll_interval_ms", 
             foundation::json::JsonFacade::createInt(static_cast<int>(config.poll_interval.count())));
    
    // enable_priority_queue
    json.set("enable_priority_queue", 
             foundation::json::JsonFacade::createBool(config.enable_priority_queue));
    
    // enable_batch_processing
    json.set("enable_batch_processing", 
             foundation::json::JsonFacade::createBool(config.enable_batch_processing));
    
    // enable_load_balancing
    json.set("enable_load_balancing", 
             foundation::json::JsonFacade::createBool(config.enable_load_balancing));
    
    // executor 通常不序列化到JSON
    
    return json;
}


class ConfigManager {
public:
    ConfigManager(const std::string& config_dir = "./config")
        : config_dir_(config_dir) {}
    
    // 加载配置
    DispatchConfig load_config(const std::string& name = "default") {
        std::shared_lock lock(mutex_);
        auto it = config_cache_.find(name);
        if (it != config_cache_.end()) {
            return it->second;
        }
        return load_config_from_file(name);
    }
    // 方法A：使用你的JsonFacade（如果有parseFile方法）
    DispatchConfig load_config_from_file(const std::string& file_path) {
        try {
            auto json = foundation::json::JsonFacade::parseFile(file_path);  // 使用你的类
            return  parse_config_from_json(json);
        } catch (...) {
            return DispatchConfig{};  // 返回默认配置
        }
    }
    bool save_config_to_file(const DispatchConfig& config, const std::string& file_path) {
        auto json = DispatchConfig::config_to_json(config);  // 先把配置转成JSON
        return json.saveToFile(file_path);   // 使用你的类的方法
    }
    // 热更新配置
    bool update_config(const std::string& name, const DispatchConfig& new_config) {
        if (!new_config.validate()) return false;
        
        std::unique_lock lock(mutex_);
        config_cache_[name] = new_config;
        
        // 触发更新回调
        notify_config_changed(name, new_config);
        
        // 可选：保存到文件
        save_config_to_file(new_config,name);
        
        return true;
    }
    
    // 注册配置变更监听器
    using ConfigChangeCallback = std::function<void(const std::string&, const DispatchConfig&)>;
    void register_listener(const std::string& listener_id, ConfigChangeCallback callback) {
        std::unique_lock lock(callback_mutex_);
        listeners_[listener_id] = std::move(callback);
    }
    
    // 监听配置文件变化（热更新核心）
    void start_file_watcher() {
        watcher_thread_ = std::thread([this]() {
            watch_config_files();
        });
    }
    
private:
    void watch_config_files() {
        namespace fs = std::filesystem;
        
        std::unordered_map<std::string, fs::file_time_type> last_modified;
        
        while (!stop_watcher_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            for (const auto& entry : fs::directory_iterator(config_dir_)) {
                if (entry.path().extension() == ".json") {
                    auto filename = entry.path().filename().string();
                    auto current_time = fs::last_write_time(entry);
                    
                    if (last_modified[filename] != current_time) {
                        last_modified[filename] = current_time;
                        
                        // 配置文件发生变化，重新加载
                        try {
                            auto new_config = load_config_from_file(filename);
                            update_config(filename, new_config);
                            //log_info("Config file {} updated", filename);
                        } catch (const std::exception& e) {
                            //log_error("Failed to reload config {}: {}", filename, e.what());
                        }
                    }
                }
            }
        }
    }
    
    void notify_config_changed(const std::string& name, const DispatchConfig& config) {
        std::shared_lock lock(callback_mutex_);
        for (const auto& [id, callback] : listeners_) {
            try {
                callback(name, config);
            } catch (...) {
                // 忽略回调异常
            }
        }
    }
    
private:
    std::string config_dir_;
    std::shared_mutex mutex_;
    std::unordered_map<std::string, DispatchConfig> config_cache_;
    std::shared_mutex callback_mutex_;
    std::unordered_map<std::string, ConfigChangeCallback> listeners_;
    
    std::thread watcher_thread_;
    std::atomic<bool> stop_watcher_{false};
};

} // namespace engine
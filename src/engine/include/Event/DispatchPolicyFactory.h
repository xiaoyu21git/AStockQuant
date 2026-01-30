// DispatchPolicyFactory.h
#pragma once
#include "DispatchPolicy.h"
#include <memory>
#include <unordered_map>
#include <functional>
#include <shared_mutex>
#include "DispatchConfig.h"
#include "DispatchPolicy.h"
namespace engine {

class DispatchPolicyFactory {
public:
    using PolicyCreator = std::function<std::shared_ptr<DispatchPolicy>()>;
    
    // 单例模式
    static DispatchPolicyFactory& instance() {
        static DispatchPolicyFactory instance;
        return instance;
    }
    
    // 注册策略创建器
    void register_creator(DispatchMode mode, PolicyCreator creator) {
        std::unique_lock lock(mutex_);
        creators_[mode] = std::move(creator);
    }
    
    // 创建策略（支持热更新参数）
    std::shared_ptr<DispatchPolicy> create(
        DispatchMode mode,
        size_t batch_size = 0,
        std::chrono::milliseconds interval = std::chrono::milliseconds(0),
        const std::string& config_json = "") {
        
        std::shared_lock lock(mutex_);
        
        // 1. 首先尝试从注册表创建
        auto it = creators_.find(mode);
        if (it != creators_.end()) {
            return it->second();
        }
        
        // 2. 默认创建器（支持热更新参数）
        return create_default(mode, batch_size, interval, config_json);
    }
    
    // 从JSON配置创建（支持热更新）
    std::shared_ptr<DispatchPolicy> from_json(const std::string& json_config) {
        auto config = parse_json_config(json_config);
        return create(config.mode, config.batch_size, config.interval, json_config);
    }
    
    // 从文件创建（支持文件热更新）
    std::shared_ptr<DispatchPolicy> from_file(const std::string& file_path) {
        try {
        // 1. 解析JSON文件
        auto json = foundation::json::JsonFacade::parseFile(file_path);
        
        // 2. 从JSON创建策略
        return from_json(json.toString());  // 或者直接使用json对象
        
    } catch (...) {
        // 解析失败，返回默认策略
        return DispatchStrategy::create_default_policy();
    }
    }
    
    // 热更新现有策略
    bool update_policy(std::shared_ptr<DispatchPolicy>& policy,
                       const std::string& new_config_json) {
        auto new_policy = from_json(new_config_json);
        if (new_policy) {
            policy.swap(new_policy);
            return true;
        }
        return false;
    }
    DispatchConfig parse_json_config(const std::string& json_config) {
        using namespace foundation::json;
    
        DispatchConfig config;
    
        try {
        // 使用你的JsonFacade解析
        JsonFacade json = JsonFacade::parse(json_config);
        
        if (json.isObject()) {
            // 解析dispatch_mode
            if (json.has("mode")) {
                std::string mode_str = json.get("mode").asString();
                if (mode_str == "Immediate") {
                    config.mode = DispatchMode::Immediate;
                } else if (mode_str == "Batch") {
                    config.mode = DispatchMode::Batch;
                } else if (mode_str == "TimeBased") {
                    config.mode = DispatchMode::TimeBased;
                } else if (mode_str == "Hybrid") {
                    config.mode = DispatchMode::Hybrid;
                }
            }
            
            // 解析batch_size
            if (json.has("batch_size")) {
                config.batch_size = static_cast<size_t>(json.get("batch_size").asInt());
            }
            
            // 解析interval_ms（JSON中可能是毫秒）
            if (json.has("interval_ms")) {
                int interval_ms = json.get("interval_ms").asInt();
                config.interval = std::chrono::milliseconds(interval_ms);
            }
            
            // 解析其他配置...
            if (json.has("name")) {
                config.name = json.get("name").asString();
            }
        }
    } catch (...) {
        // 解析失败，返回默认配置
        return DispatchConfig::default_config();
    }
    
    return config;
}


private:
    DispatchPolicyFactory() {
        register_default_creators();
    }
    
    void register_default_creators() {
        // 注册内置策略
        register_creator(DispatchMode::Immediate, []() {
            return std::make_shared<ImmediatePolicy>();
        });
        
        register_creator(DispatchMode::Batch, []() {
            return std::make_shared<BatchPolicy>(100); // 默认值
        });
        
        register_creator(DispatchMode::TimeBased, []() {
            return std::make_shared<TimePolicy>(std::chrono::milliseconds(50));
        });
        
        register_creator(DispatchMode::Hybrid, []() {
            return std::make_shared<HybridPolicy>(100, std::chrono::milliseconds(50));
        });
    }
    
    std::shared_ptr<DispatchPolicy> create_default(
        DispatchMode mode,
        size_t batch_size,
        std::chrono::milliseconds interval,
        const std::string& config_json) {
        
        switch (mode) {
            case DispatchMode::Immediate:
                return std::make_shared<ImmediatePolicy>();
                
            case DispatchMode::Batch:
                return std::make_shared<BatchPolicy>(batch_size);
                
            case DispatchMode::TimeBased:
                return std::make_shared<TimePolicy>(interval);
                
            case DispatchMode::Hybrid:
                return std::make_shared<HybridPolicy>(batch_size, interval);
                
            default:
                return std::make_shared<ImmediatePolicy>();
        }
    }
private:
    std::shared_mutex mutex_;
    std::unordered_map<DispatchMode, PolicyCreator> creators_;
};

} // namespace engine
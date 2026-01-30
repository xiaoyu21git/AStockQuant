// HotDispatchManager.h
#pragma once
#include "DispatchPolicyFactory.h"
#include "DispatchConfig.h"
#include <atomic>
#include <memory>
#include <shared_mutex>

namespace engine {

class HotDispatchManager {
public:
    HotDispatchManager() {
        // 初始策略
        current_policy_ = DispatchPolicyFactory::instance().create(
            DispatchMode::Hybrid, 100, std::chrono::milliseconds(50));
        
        // 注册配置监听
        config_manager_.register_listener("dispatch_manager", 
            [this](const std::string& name, const DispatchConfig& config) {
                this->on_config_changed(config);
            });
        
        // 启动文件监听
        config_manager_.start_file_watcher();
    }
    
    ~HotDispatchManager() {
        // 清理
    }
    
    // 获取当前策略（线程安全）
    std::shared_ptr<DispatchPolicy> get_current_policy() const {
        std::shared_lock lock(mutex_);
        return current_policy_;
    }
    
    // 热更新策略
    bool update_policy(const DispatchConfig& new_config) {
        if (!new_config.validate()) return false;
        
        auto new_policy = DispatchPolicyFactory::instance().create(
            new_config.mode, new_config.batch_size, new_config.interval);
        
        if (!new_policy) return false;
        
        std::unique_lock lock(mutex_);
        previous_policy_ = current_policy_;
        current_policy_ = std::move(new_policy);
        current_config_ = new_config;
        
        // log_info("Policy updated: mode={}, batch={}, interval={}ms",
        //          static_cast<int>(new_config.mode),
        //          new_config.batch_size,
        //          new_config.interval.count());
        
        return true;
    }
    
    // 热更新策略（从JSON）
    bool update_policy_from_json(const std::string& json_config) {
        try {
            auto config = DispatchConfig::from_json(json_config);
            return update_policy(config);
        } catch (const std::exception& e) {
            //log_error("Failed to update policy from JSON: {}", e.what());
            return false;
        }
    }
    
    // 回滚到上一个策略
    bool rollback() {
        std::unique_lock lock(mutex_);
        if (previous_policy_) {
            current_policy_.swap(previous_policy_);
            //log_info("Policy rolled back");
            return true;
        }
        return false;
    }
    
    // A/B测试：同时运行两个策略并比较
    void start_ab_test(const DispatchConfig& config_a, 
                      const DispatchConfig& config_b,
                      std::chrono::seconds duration = std::chrono::seconds(60)) {
        ab_test_thread_ = std::thread([=]() {
            run_ab_test(config_a, config_b, duration);
        });
    }
    
private:
    void on_config_changed(const DispatchConfig& config) {
        if (config.name == "dispatch_policy") {
            update_policy(config);
        }
    }
    
    void run_ab_test(const DispatchConfig& config_a,
                    const DispatchConfig& config_b,
                    std::chrono::seconds duration) {
        auto policy_a = DispatchPolicyFactory::instance().create(
            config_a.mode, config_a.batch_size, config_a.interval);
        auto policy_b = DispatchPolicyFactory::instance().create(
            config_b.mode, config_b.batch_size, config_b.interval);
        
        // TestMetrics metrics_a, metrics_b;
        
        // // 分时段运行测试
        // // ... 实现A/B测试逻辑
        
        // // 选择更好的策略
        // if (metrics_a.throughput > metrics_b.throughput) {
        //     update_policy(config_a);
        // } else {
        //     update_policy(config_b);
        // }
    }
    
private:
    mutable std::shared_mutex mutex_;
    std::shared_ptr<DispatchPolicy> current_policy_;
    std::shared_ptr<DispatchPolicy> previous_policy_;  // 用于回滚
    DispatchConfig current_config_;
    
    ConfigManager config_manager_;
    std::thread ab_test_thread_;
};

} // namespace engine
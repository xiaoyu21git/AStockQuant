// config/ConfigManager.hpp
#pragma once
#include "ConfigNode.hpp"
#include <memory>
#include <map>
#include <vector>
#include <functional>
#include <mutex>
#include <shared_mutex>
namespace foundation {
namespace config {

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    // 配置域
    enum class Domain {
        FOUNDATION,    // 基础配置
        PROFILE,       // 环境配置
        SYSTEM,        // 系统配置
        APPLICATION,   // 应用配置
        MODULE,        // 模块配置
        RUNTIME        // 运行时配置
    };
    
    // 监听器
    using ConfigChangeListener = std::function<void(
        Domain domain,
        const std::string& path,
        const ConfigNode& oldValue,
        const ConfigNode& newValue
    )>;
    
    // 验证结果
    struct ValidationResult {
        bool success = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        
        std::string toString() const {
            std::string result;
            if (!success) {
                result += "Validation failed:\n";
                for (const auto& err : errors) {
                    result += "  ERROR: " + err + "\n";
                }
            }
            for (const auto& warn : warnings) {
                result += "  WARNING: " + warn + "\n";
            }
            return result;
        }
    };
    
    // 获取单例
    static ConfigManager& instance();
    
    // 初始化
    void initialize(
        const std::string& profile = "development",
        const std::string& configDir = "./config"
    );
    
    // ============ 配置获取接口 ============
    
    // 获取配置域
    ConfigNode::Ptr getConfig(Domain domain)const;
    
    // 获取应用配置（合并所有域）
    ConfigNode::Ptr getAppConfig();
    
    // 获取模块配置
    ConfigNode::Ptr getModuleConfig(
        const std::string& moduleName,
        const std::string& moduleConfigDir = "modules"
    );
    
    // ============ 动态配置 ============
    
    // 设置运行时配置
    void setRuntimeConfig(
        const std::string& path,
        const ConfigNode& value,
        bool persist = false
    );
    
    // 获取运行时配置
    ConfigNode::Ptr getRuntimeConfig();
    
    // ============ 配置管理 ============
    
    // 重新加载
    void reload(Domain domain);
    void reloadAll();
    
    // 监听器
    void addDomainListener(Domain domain, ConfigChangeListener listener);
    void addPathListener(const std::string& pathPattern, ConfigChangeListener listener);
    
    // 验证
    ValidationResult validate(Domain domain) const;
    ValidationResult validateAppConfig() const;
    
    // 导出
    void exportConfig(
        Domain domain,
        const std::string& format,
        const std::string& outputPath
    ) const;
    // 配置值获取（带默认值）
    template<typename T>
     T get_config_value(const config::ConfigNode::Ptr& config,
                             const std::string& key,
                             const T& default_value);
    
    // 快速配置访问（通过配置管理器）
     std::string get_app_config_string(const std::string& key,
                                           const std::string& default_value = "");
     int get_app_config_int(const std::string& key, int default_value = 0);
     double get_app_config_double(const std::string& key, 
                                       double default_value = 0.0);
     bool get_app_config_bool(const std::string& key, 
                                   bool default_value = false);
    
    // ============ 工具方法 ============
    
    const std::string& getCurrentProfile() const { return currentProfile_; }
    const std::string& getConfigBaseDir() const { return configBaseDir_; }
    ConfigNode::Ptr buildAppConfig();
private:
    
    // 禁止拷贝
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    // 加载方法
    void loadFoundationConfigs();
    void loadProfileConfig(const std::string& profile);
    void loadSystemConfigs();
    void loadAppConfigs();
    void loadDynamicConfigs();
    
    // 合并方法
    ConfigNode::Ptr mergeConfigs(
        const std::vector<ConfigNode::Ptr>& configs,
        Domain domain
    );
    
    // 域管理
    bool registerDomain(const std::string& name, Domain domain);
    ConfigNode::Ptr loadDomainConfig(Domain domain);
    
    // 监听器通知
    void notifyListeners(
        Domain domain,
        const std::string& path,
        const ConfigNode& oldValue,
        const ConfigNode& newValue
    );
    
private:
    std::unique_ptr<class ConfigLoader> loader_;
    std::map<Domain, ConfigNode::Ptr> domainConfigs_;
    std::map<std::string, ConfigNode::Ptr> moduleConfigs_;
    ConfigNode::Ptr runtimeConfig_;
    ConfigNode::Ptr appConfig_;  // 缓存的应用配置
    
    std::string currentProfile_;
    std::string configBaseDir_;
    
    // 监听器
    std::map<Domain, std::vector<ConfigChangeListener>> domainListeners_;
    std::map<std::string, std::vector<ConfigChangeListener>> pathListeners_;
    
    // 锁
    mutable std::shared_mutex configMutex_;
    mutable std::mutex listenersMutex_;
    mutable std::mutex runtimeConfigMutex_;
};

// ============ 便捷宏 ============

#define CONFIG_DOMAIN(domain, path, defaultValue) \
    trader::config::ConfigManager::instance().getConfig(domain)->get(path, defaultValue)

#define APP_CONFIG(path, defaultValue) \
    trader::config::ConfigManager::instance().getAppConfig()->get(path, defaultValue)

#define MODULE_CONFIG(module, path, defaultValue) \
    trader::config::ConfigManager::instance().getModuleConfig(module)->get(path, defaultValue)

} // namespace config
} // namespace trader
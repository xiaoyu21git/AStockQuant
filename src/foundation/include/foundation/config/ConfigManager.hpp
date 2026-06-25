    #pragma once
    #include <string>
    #include <vector>
    #include <map>
    #include <functional>
    #include <memory>
    #include <mutex>
    #include <shared_mutex>
    #include "ConfigNode.hpp"
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>
#include <mutex>
#include <shared_mutex>

namespace foundation {
namespace config {

/// @brief 配置文件名枚举 —— 每个值对应 config/ 下唯一的具体文件
/// @note 仅包含已通过 ConfigManager 统一加载的配置文件；
///       QML 视图 UI 配置 (factor_common.json 等) 不在本枚举范围内
enum class ConfigFile {
    TradingConnection,   // config/trading_connection.json
    RiskConfig,          // config/risk_config.json
    Jujin,               // config/jujin.json
};

class ConfigManager {
    // 配置变更结构体
public:
    // 配置域
    enum class Domain {
        FOUNDATION,    // 基础配置
        PROFILE,       // 环境配置
        SYSTEM,        // 系统配置
        APPLICATION,   // 应用配置
        MODULE,        // 模块配置
        RUNTIME        // 运行时配置
    };

    struct ConfigChange {
        Domain domain;
        std::string path;
        std::string old_value;
        std::string new_value;
    };

    // 变更通知回调（由外部注入，基础层不依赖事件总线）
    void setChangeCallback(const std::function<void(const foundation::json::JsonFacade&)>& cb);

    // 批量变更接口
    void batch_set(const std::vector<ConfigChange>& changes);

    // 快照存储
    std::map<std::string, ConfigNode::Ptr> snapshots_;
    // 变更历史
    std::vector<std::vector<ConfigChange>> change_history_;

    // 快照保存与回滚
    void save_snapshot(const std::string& snapshot_id);
    void rollback(const std::string& snapshot_id);
    void undo();

public:
    ConfigManager();
    ~ConfigManager();
    
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

    // ── 命名配置文件操作 (ConfigFile 枚举 → 具体文件) ──

    /// @brief 解析配置文件完整路径 (configBaseDir_ + "/" + 相对路径)
    /// @note 若 ConfigManager 尚未初始化 (configBaseDir_ 为空) 则 abort
    std::string configFilePath(ConfigFile file) const;

    /// @brief 加载配置文件 (双重检查锁 + 自动缓存)
    /// @return 成功返回 ConfigNode；文件不存在/解析失败 → 返回空 ConfigNode (isNull()==true)
    ConfigNode::Ptr loadConfigFile(ConfigFile file);

    /// @brief 保存配置文件 (原子写入: 临时文件 → flush → rename)
    /// 成功后自动更新缓存 (read-after-write 一致性)
    /// @return true 成功, false 失败 (原文件不受影响)
    bool saveConfigFile(ConfigFile file, const ConfigNode& config);

    /// @brief 清除指定文件的缓存 (下次 loadConfigFile 将重新读磁盘)
    void invalidateConfigFileCache(ConfigFile file);

    // ── 命名配置文件快速取值 (内部自动调用 loadConfigFile，按需加载) ──

    std::string get_config_file_string(ConfigFile file, const std::string& key,
                                       const std::string& defaultVal = "");
    int         get_config_file_int(ConfigFile file, const std::string& key, int def = 0);
    double      get_config_file_double(ConfigFile file, const std::string& key, double def = 0.0);
    bool        get_config_file_bool(ConfigFile file, const std::string& key, bool def = false);

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
        std::function<void(const foundation::json::JsonFacade&)> change_callback_;
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

    // 命名配置文件缓存 (ConfigFile → ConfigNode)
    mutable std::shared_mutex m_fileCacheMutex;
    std::map<ConfigFile, ConfigNode::Ptr> m_fileConfigCache;
};

// ============ 便捷宏 ============

#define CONFIG_DOMAIN(domain, path, defaultValue) \
    trader::config::ConfigManager::instance().getConfig(domain)->get(path, defaultValue)

#define APP_CONFIG(path, defaultValue) \
    trader::config::ConfigManager::instance().getAppConfig()->get(path, defaultValue)

#define MODULE_CONFIG(module, path, defaultValue) \
    trader::config::ConfigManager::instance().getModuleConfig(module)->get(path, defaultValue)

} // namespace config
} // namespace foundation
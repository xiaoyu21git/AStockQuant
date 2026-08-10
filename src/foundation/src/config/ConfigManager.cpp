// 移除事件总线头文件依赖
// config/ConfigManager.cpp
#include "foundation/config/ConfigManager.hpp"
#include "foundation/config/ConfigLoader.hpp"
#include "foundation/config/JsonConfigProvider.hpp"
#include "foundation/json/json_facade.h"
#include "foundation/config/YamlConfigProvider.hpp"
#include "foundation/fs/File.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/utils/String.hpp"
#include <algorithm>
#include <regex>
#include <unordered_map>


namespace foundation {
namespace config {
void ConfigManager::setChangeCallback(const std::function<void(const foundation::json::JsonFacade&)>& cb) {
    change_callback_ = cb;
}

void ConfigManager::save_snapshot(const std::string& snapshot_id) {
    foundation::config::ConfigNode::Ptr current = this->getConfig(foundation::config::ConfigManager::Domain::APPLICATION);
    if (!current) {
        INTERNAL_WARN_STREAM << "Cannot save snapshot: current config is null";
        return;
    }
    // 假设ConfigNode支持深拷贝构造
    this->snapshots_[snapshot_id] = std::make_shared<ConfigNode>(*current);
    INTERNAL_INFO_STREAM << "Saved config snapshot: " << snapshot_id;
}

void ConfigManager::rollback(const std::string& snapshot_id) {
    auto it = this->snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        INTERNAL_WARN_STREAM << "No snapshot found for id: " << snapshot_id;
        return;
    }
    foundation::config::ConfigNode::Ptr snapshot = it->second;
    std::vector<foundation::config::ConfigManager::ConfigChange> rollback_changes;
    foundation::config::ConfigNode::Ptr current = this->getConfig(foundation::config::ConfigManager::Domain::APPLICATION);
    if (!current || !snapshot) {
        INTERNAL_WARN_STREAM << "Current or snapshot config is null";
        return;
    }
    // 遍历快照顶层key
    std::vector<std::string> keys;
    // 需补充ConfigNode::getKeys()接口，暂用对象遍历
    if (snapshot->isObject()) {
        // 假设ConfigNode有getKeys()
        keys = snapshot->getKeys();
    }
    for (const auto& key : keys) {
        auto snap_node = snapshot->get(key);
        auto cur_node = current->get(key);
        std::string from_val = cur_node.isNull() ? "" : cur_node.toJsonString();
        std::string to_val = snap_node.isNull() ? "" : snap_node.toJsonString();
        if (from_val != to_val) {
            current->merge(snap_node, true); // 用merge替代set
            foundation::config::ConfigManager::ConfigChange change;
            change.domain = foundation::config::ConfigManager::Domain::APPLICATION;
            change.path = key;
            change.old_value = from_val;
            change.new_value = to_val;
            rollback_changes.push_back(change);
        }
    }
    // 回滚变更后通知外部（如事件总线），通过回调接口实现
    if (this->change_callback_) {
        foundation::json::JsonFacade event_json = foundation::json::JsonFacade::createObject();
        event_json.set("type", foundation::json::JsonFacade::createString("CONFIG_ROLLBACK"));
        event_json.set("snapshot_id", foundation::json::JsonFacade::createString(snapshot_id));
        auto changes_arr = foundation::json::JsonFacade::createArray();
        for (const auto& c : rollback_changes) {
            auto change = foundation::json::JsonFacade::createObject();
            change.set("domain", foundation::json::JsonFacade::createInt(static_cast<int>(c.domain)));
            change.set("path", foundation::json::JsonFacade::createString(c.path));
            change.set("old_value", foundation::json::JsonFacade::createString(c.old_value));
            change.set("new_value", foundation::json::JsonFacade::createString(c.new_value));
            changes_arr.push_back(change);
        }
        event_json.set("changes", changes_arr);
        this->change_callback_(event_json);
    }
}

void ConfigManager::undo() {
    if (this->change_history_.empty()) {
        INTERNAL_WARN_STREAM << "No change history to undo.";
        return;
    }
    auto last_changes = this->change_history_.back();
    this->change_history_.pop_back();
    std::vector<foundation::config::ConfigManager::ConfigChange> undo_changes;
    // 恢复每个变更项
    for (const auto& change : last_changes) {
        ConfigNode::Ptr config = getConfig(change.domain);
        std::string from_val;
        if (config) {
            auto node = config->getPath(change.path, '.');
            from_val = node.isNull() ? "" : node.toJsonString();
            // 恢复为old_value
            ConfigNode old_node;
            // 需补充ConfigNode::fromJsonString实现，暂用构造+parse
            if (!change.old_value.empty()) {
                old_node = ConfigNode(foundation::json::JsonFacade::parse(change.old_value));
            }
            config->merge(old_node, true); // 用merge替代setPath
        }
        foundation::config::ConfigManager::ConfigChange undo_change;
        undo_change.domain = change.domain;
        undo_change.path = change.path;
        undo_change.old_value = from_val;
        undo_change.new_value = change.old_value;
        undo_changes.push_back(undo_change);
    }
    // 撤销变更后通知外部（如事件总线），通过回调接口实现
    if (this->change_callback_) {
        foundation::json::JsonFacade event_json = foundation::json::JsonFacade::createObject();
        event_json.set("type", foundation::json::JsonFacade::createString("CONFIG_UNDO"));
        auto changes_arr = foundation::json::JsonFacade::createArray();
        for (const auto& c : undo_changes) {
            auto change = foundation::json::JsonFacade::createObject();
            change.set("domain", foundation::json::JsonFacade::createInt(static_cast<int>(c.domain)));
            change.set("path", foundation::json::JsonFacade::createString(c.path));
            change.set("old_value", foundation::json::JsonFacade::createString(c.old_value));
            change.set("new_value", foundation::json::JsonFacade::createString(c.new_value));
            changes_arr.push_back(change);
        }
        event_json.set("changes", changes_arr);
        this->change_callback_(event_json);
    }
}

void ConfigManager::batch_set(const std::vector<ConfigChange>& changes) {
    std::vector<foundation::config::ConfigManager::ConfigChange> actual_changes;
    // 批量变更
    for (const auto& change : changes) {
        // 获取旧值
        ConfigNode::Ptr config = getConfig(change.domain);
        std::string old_val;
        if (config) {
            auto node = config->getPath(change.path, '.');
            old_val = node.isNull() ? "" : node.toJsonString();
        }
        // 设置新值（简化：直接覆盖，实际可细化为嵌套路径赋值）
        ConfigNode new_node;
        if (!change.new_value.empty()) {
            new_node = ConfigNode(foundation::json::JsonFacade::parse(change.new_value));
        }
        if (config) {
            config->merge(new_node, true); // 用merge替代setPath
        }
        // 记录实际变更
        foundation::config::ConfigManager::ConfigChange actual_change;
        actual_change.domain = change.domain;
        actual_change.path = change.path;
        actual_change.old_value = old_val;
        actual_change.new_value = change.new_value;
        actual_changes.push_back(actual_change);
    }
    // 记录变更历史
    this->change_history_.push_back(actual_changes);

    // 批量变更后通知外部（如事件总线），通过回调接口实现
    if (this->change_callback_) {
        foundation::json::JsonFacade event_json = foundation::json::JsonFacade::createObject();
        event_json.set("type", foundation::json::JsonFacade::createString("CONFIG_BATCH_CHANGED"));
        auto changes_arr = foundation::json::JsonFacade::createArray();
        for (const auto& c : actual_changes) {
            auto change = foundation::json::JsonFacade::createObject();
            change.set("domain", foundation::json::JsonFacade::createInt(static_cast<int>(c.domain)));
            change.set("path", foundation::json::JsonFacade::createString(c.path));
            change.set("old_value", foundation::json::JsonFacade::createString(c.old_value));
            change.set("new_value", foundation::json::JsonFacade::createString(c.new_value));
            changes_arr.push_back(change);
        }
        event_json.set("changes", changes_arr);
        this->change_callback_(event_json);
    }
}

// ============ ConfigManager 单例实现 ============

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::ConfigManager() 
    : loader_(std::make_unique<ConfigLoader>())
    , runtimeConfig_(std::make_shared<ConfigNode>()) {
    
    // 注册默认的配置提供者
    loader_->registerProvider(".json", 
        std::make_shared<JsonConfigProvider>(true, true));
    loader_->registerProvider(".yaml", 
        std::make_shared<YamlConfigProvider>());
    loader_->registerProvider(".yml", 
        std::make_shared<YamlConfigProvider>());
    
    INTERNAL_DEBUG_STREAM << "ConfigManager constructed";
}

ConfigManager::~ConfigManager() {
    INTERNAL_DEBUG_STREAM << "ConfigManager destroyed";
}

// ============ 初始化方法 ============

void ConfigManager::initialize(
    const std::string& profile,
    const std::string& configDir) {
    
    INTERNAL_INFO_STREAM << "Initializing ConfigManager with profile: " << profile << ", configDir: " << configDir;
    
    currentProfile_ = profile;
    configBaseDir_ = configDir;
    
    try {
        // 清空现有配置
        {
            std::unique_lock<std::shared_mutex> lock(configMutex_);
            domainConfigs_.clear();
            moduleConfigs_.clear();
            appConfig_.reset();
        }
        
        // 加载各个配置域
        loadFoundationConfigs();
        loadProfileConfig(profile);
        loadSystemConfigs();
        loadAppConfigs();
        loadDynamicConfigs();
        
        // 构建应用配置缓存
        buildAppConfig();
        
        INTERNAL_INFO_STREAM << "ConfigManager initialized successfully";
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to initialize ConfigManager: " << e.what();
        throw foundation::ConfigException(
            foundation::utils::String::format(
                "ConfigManager initialization failed: {}", e.what()
            )
        );
    }
}

// ============ 配置加载方法 ============

void ConfigManager::loadFoundationConfigs() {
    INTERNAL_DEBUG_STREAM << "Loading foundation configurations";
    
    std::vector<ConfigNode::Ptr> foundationConfigs;
    std::string foundationDir = foundation::utils::String::endsWith(configBaseDir_, "/")
        ? configBaseDir_ + "foundation"
        : configBaseDir_ + "/foundation";
    
    // 检查基础配置目录是否存在
    if (!foundation::fs::File::exists(foundationDir)) {
        INTERNAL_WARN_STREAM << "Foundation config directory not found: " << foundationDir;
        return;
    }
    
    if (!foundation::fs::File::isDirectory(foundationDir)) {
        INTERNAL_WARN_STREAM << "Foundation config path is not a directory: " << foundationDir;
        return;
    }
    
    // 遍历目录中的所有配置文件
    auto files = foundation::fs::File::listFiles(foundationDir);
    for (const auto& file : files) {
        // 只处理支持的配置文件
        if (foundation::utils::String::endsWith(file, ".yaml") ||
            foundation::utils::String::endsWith(file, ".yml") ||
            foundation::utils::String::endsWith(file, ".json")) {
            
            std::string filePath = foundationDir + "/" + file;
            
            try {
                ConfigLoader::LoadOptions options;
                options.profile = currentProfile_;
                
                auto config = loader_->load(filePath, options);
                foundationConfigs.push_back(config);
                
                INTERNAL_DEBUG_STREAM << "Loaded foundation config: " << file;
                
            } catch (const std::exception& e) {
                INTERNAL_WARN_STREAM << "Failed to load foundation config " << file << ": " << e.what();
            }
        }
    }
    
    // 合并所有基础配置
    if (!foundationConfigs.empty()) {
        auto merged = mergeConfigs(foundationConfigs, Domain::FOUNDATION);
        
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        domainConfigs_[Domain::FOUNDATION] = merged;
        
        INTERNAL_INFO_STREAM << "Loaded " << foundationConfigs.size() << " foundation configurations";
    }
}

void ConfigManager::loadProfileConfig(const std::string& profile) {
    INTERNAL_DEBUG_STREAM << "Loading profile configuration: " << profile;
    
    std::string profilePath = foundation::utils::String::format(
        "{}/profiles/{}.yaml", configBaseDir_, profile);
    
    // 检查配置文件是否存在
    if (!foundation::fs::File::exists(profilePath)) {
        INTERNAL_WARN_STREAM << "Profile config not found: " << profilePath;
        
        // 尝试使用其他扩展名
        profilePath = foundation::utils::String::format(
            "{}/profiles/{}.yml", configBaseDir_, profile);
        
        if (!foundation::fs::File::exists(profilePath)) {
            INTERNAL_WARN_STREAM << "Profile config not found with .yml extension: " << profilePath;
            return;
        }
    }
    
    try {
        ConfigLoader::LoadOptions options;
        options.profile = "";  // profile文件本身不包含环境变量
        
        auto config = loader_->load(profilePath, options);
        
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        domainConfigs_[Domain::PROFILE] = config;
        
        INTERNAL_INFO_STREAM << "Profile configuration loaded: " << profile;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to load profile config " << profilePath << ": " << e.what();
        throw foundation::ConfigException(
            foundation::utils::String::format(
                "Failed to load profile config {}: {}", profile, e.what()
            )
        );
    }
}

void ConfigManager::loadSystemConfigs() {
    INTERNAL_DEBUG_STREAM << "Loading system configurations";
    
    std::vector<ConfigNode::Ptr> systemConfigs;
    std::string systemDir = configBaseDir_ + "/system";
    
    // 检查系统配置目录是否存在
    if (!foundation::fs::File::exists(systemDir)) {
        INTERNAL_DEBUG_STREAM << "System config directory not found: " << systemDir;
        return;
    }
    
    if (foundation::fs::File::isDirectory(systemDir)) {
        auto files = foundation::fs::File::listFiles(systemDir);
        for (const auto& file : files) {
            std::string filePath = systemDir + "/" + file;
            
            try {
                ConfigLoader::LoadOptions options;
                options.profile = currentProfile_;
                
                auto config = loader_->load(filePath, options);
                systemConfigs.push_back(config);
                
                INTERNAL_DEBUG_STREAM << "Loaded system config: " << file;
                
            } catch (const std::exception& e) {
                INTERNAL_WARN_STREAM << "Failed to load system config " << file << ": " << e.what();
            }
        }
    }
    
    // 合并系统配置
    if (!systemConfigs.empty()) {
        auto merged = mergeConfigs(systemConfigs, Domain::SYSTEM);
        
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        domainConfigs_[Domain::SYSTEM] = merged;
        
        INTERNAL_INFO_STREAM << "Loaded " << systemConfigs.size() << " system configurations";
    }
}

void ConfigManager::loadAppConfigs() {
    INTERNAL_DEBUG_STREAM << "Loading application configurations";
    
    // 尝试加载应用主配置文件
    // 优先查找 configBaseDir 目录下的配置文件，然后查找 app/ 目录
    std::vector<std::string> appConfigPaths = {
        configBaseDir_ + "/config.json",
        configBaseDir_ + "/config.yaml",
        configBaseDir_ + "/config.yml",
        configBaseDir_ + "/app.json",
        configBaseDir_ + "/app.yaml",
        configBaseDir_ + "/app.yml",
        "app/trader.yaml",
        "app/config.yaml",
        "app/trader.yml",
        "app/config.yml",
        "app/trader.json",
        "app/config.json"
    };
    
    ConfigNode::Ptr appConfig;
    
    for (const auto& path : appConfigPaths) {
        INTERNAL_DEBUG_STREAM << "Trying to load app config from: " << path;
        if (foundation::fs::File::exists(path)) {
            try {
                ConfigLoader::LoadOptions options;
                options.profile = currentProfile_;
                
                appConfig = loader_->load(path, options);
                INTERNAL_INFO_STREAM << "Application configuration loaded: " << path;
                break;
                
            } catch (const std::exception& e) {
                INTERNAL_WARN_STREAM << "Failed to load app config " << path << ": " << e.what();
            }
        }
    }
    
    if (!appConfig) {
        INTERNAL_WARN_STREAM << "No application configuration found, using empty config";
        appConfig = std::make_shared<ConfigNode>();
    }
    
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    domainConfigs_[Domain::APPLICATION] = appConfig;
}

void ConfigManager::loadDynamicConfigs() {
    INTERNAL_DEBUG_STREAM << "Loading dynamic configurations";
    
    // 这里可以加载从数据库、API等动态源获取的配置
    // 目前仅初始化一个空的动态配置节点
    
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    domainConfigs_[Domain::RUNTIME] = runtimeConfig_;
    
    INTERNAL_DEBUG_STREAM << "Dynamic configurations initialized";
}

// ============ 配置获取方法 ============

ConfigNode::Ptr ConfigManager::getConfig(Domain domain)const {
    std::shared_lock<std::shared_mutex> lock(configMutex_);
    
    auto it = domainConfigs_.find(domain);
    if (it != domainConfigs_.end()) {
        return it->second;
    }
    
    // 返回空配置节点
    return std::make_shared<ConfigNode>();
}

ConfigNode::Ptr ConfigManager::getAppConfig() {
    // 检查是否有缓存的应用配置
    {
        std::shared_lock<std::shared_mutex> lock(configMutex_);
        if (appConfig_) {
            return appConfig_;
        }
    }
    
    // 重新构建应用配置
    return buildAppConfig();
}

ConfigNode::Ptr ConfigManager::buildAppConfig() {
    std::vector<ConfigNode::Ptr> configLayers;
    
    // 按优先级收集配置层（优先级从低到高）
    const std::vector<Domain> domainOrder = {
        Domain::FOUNDATION,  // 基础配置（最低优先级）
        Domain::PROFILE,     // 环境配置
        Domain::SYSTEM,      // 系统配置
        Domain::APPLICATION, // 应用配置
        Domain::RUNTIME      // 运行时配置（最高优先级）
    };
    
    {
        std::shared_lock<std::shared_mutex> lock(configMutex_);
        
        for (auto domain : domainOrder) {
            auto it = domainConfigs_.find(domain);
            if (it != domainConfigs_.end() && it->second) {
                configLayers.push_back(it->second);
            }
        }
    }
    
    // 合并所有配置层
    ConfigNode::Ptr mergedConfig;
    
    if (configLayers.empty()) {
        mergedConfig = std::make_shared<ConfigNode>();
    } else {
        mergedConfig = configLayers[0];
        for (size_t i = 1; i < configLayers.size(); ++i) {
            mergedConfig->overlay(*configLayers[i]);
        }
    }
    
    // 缓存应用配置
    {
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        appConfig_ = mergedConfig;
    }
    
    return mergedConfig;
}

ConfigNode::Ptr ConfigManager::getModuleConfig(
    const std::string& moduleName,
    const std::string& moduleConfigDir) {
    
    // 构建模块配置键
    std::string moduleKey = moduleName + "@" + moduleConfigDir;
    
    // 检查缓存
    {
        std::shared_lock<std::shared_mutex> lock(configMutex_);
        auto it = moduleConfigs_.find(moduleKey);
        if (it != moduleConfigs_.end()) {
            return it->second;
        }
    }
    
    // 获取基础应用配置
    auto baseConfig = getAppConfig();
    
    // 尝试加载模块特定配置
    std::vector<std::string> moduleConfigPaths = {
        foundation::utils::String::format("app/{}/{}.yaml", moduleConfigDir, moduleName),
        foundation::utils::String::format("app/{}/{}.yml", moduleConfigDir, moduleName),
        foundation::utils::String::format("app/{}/{}.json", moduleConfigDir, moduleName),
        foundation::utils::String::format("{}/{}/{}.yaml", configBaseDir_, moduleConfigDir, moduleName),
        foundation::utils::String::format("{}/{}/{}.yml", configBaseDir_, moduleConfigDir, moduleName),
        foundation::utils::String::format("{}/{}/{}.json", configBaseDir_, moduleConfigDir, moduleName)
    };
    
    ConfigNode::Ptr moduleSpecificConfig;
    
    for (const auto& path : moduleConfigPaths) {
        if (foundation::fs::File::exists(path)) {
            try {
                ConfigLoader::LoadOptions options;
                options.profile = currentProfile_;
                
                moduleSpecificConfig = loader_->load(path, options);
                INTERNAL_DEBUG_STREAM << "Module config loaded: " << moduleName << " from " << path;
                break;
                
            } catch (const std::exception& e) {
                INTERNAL_WARN_STREAM << "Failed to load module config " << path << ": " << e.what();
            }
        }
    }
    
    // 合并配置
    ConfigNode::Ptr mergedConfig;
    
    if (moduleSpecificConfig) {
        // 复制基础配置并覆盖模块特定配置
        mergedConfig = std::make_shared<ConfigNode>(*baseConfig);
        mergedConfig->overlay(*moduleSpecificConfig);
    } else {
        // 使用基础配置
        mergedConfig = baseConfig;
    }
    
    // 缓存模块配置
    {
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        moduleConfigs_[moduleKey] = mergedConfig;
    }
    
    return mergedConfig;
}

// ============ 动态配置方法 ============

void ConfigManager::setRuntimeConfig(
    const std::string& path,
    const ConfigNode& value,
    bool persist) {
    
    // 记录旧值用于通知
    ConfigNode oldValue;
    {
        std::shared_lock<std::shared_mutex> lock(configMutex_);
        if (runtimeConfig_) {
            oldValue = std::move(runtimeConfig_->getPath(path,'.'));
        }
    }
    
    // 设置新值
    {
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        if (!runtimeConfig_) {
            runtimeConfig_ = std::make_shared<ConfigNode>();
        }
        
        // 这里需要实现路径设置功能
        // 简化实现：直接设置到顶层
        // 实际应该支持嵌套路径
        runtimeConfig_->overlay(value);
        
        // 清除应用配置缓存
        appConfig_.reset();
    }
    
    // 通知监听器
    notifyListeners(Domain::RUNTIME, path, oldValue, value);
    
    INTERNAL_INFO_STREAM << "Runtime config updated: " << path << " = " << value.toString();
    
    // 如果要求持久化，保存到文件
    if (persist) {
        // 这里可以添加持久化逻辑
        INTERNAL_DEBUG_STREAM << "Runtime config persist requested (not implemented)";
    }
}

ConfigNode::Ptr ConfigManager::getRuntimeConfig() {
    std::shared_lock<std::shared_mutex> lock(configMutex_);
    return runtimeConfig_;
}

// ============ 配置管理方法 ============

void ConfigManager::reload(Domain domain) {
    INTERNAL_INFO_STREAM << "Reloading configuration domain: " << static_cast<int>(domain);
    
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    
    // 根据域重新加载配置
    switch (domain) {
        case Domain::FOUNDATION:
            loadFoundationConfigs();
            break;
        case Domain::PROFILE:
            loadProfileConfig(currentProfile_);
            break;
        case Domain::SYSTEM:
            loadSystemConfigs();
            break;
        case Domain::APPLICATION:
            loadAppConfigs();
            break;
        case Domain::RUNTIME:
            // 运行时配置不需要重新加载
            break;
        case Domain::MODULE:
            // 模块配置在getModuleConfig时动态加载
            break;
    }
    
    // 清除缓存
    appConfig_.reset();
    moduleConfigs_.clear();
    
    INTERNAL_INFO_STREAM << "Configuration domain " << static_cast<int>(domain) << " reloaded";
}

void ConfigManager::reloadAll() {
    INTERNAL_INFO_STREAM << "Reloading all configurations";
    
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    
    // 重新加载所有域
    loadFoundationConfigs();
    loadProfileConfig(currentProfile_);
    loadSystemConfigs();
    loadAppConfigs();
    loadDynamicConfigs();
    
    // 清除缓存
    appConfig_.reset();
    moduleConfigs_.clear();
    
    INTERNAL_INFO_STREAM << "All configurations reloaded";
}

// ============ 监听器管理 ============

void ConfigManager::addDomainListener(Domain domain, ConfigChangeListener listener) {
    std::unique_lock<std::mutex> lock(listenersMutex_);
    domainListeners_[domain].push_back(listener);
    INTERNAL_DEBUG_STREAM << "Added domain listener for domain: " << static_cast<int>(domain);
}

void ConfigManager::addPathListener(const std::string& pathPattern, ConfigChangeListener listener) {
    std::unique_lock<std::mutex> lock(listenersMutex_);
    pathListeners_[pathPattern].push_back(listener);
    INTERNAL_DEBUG_STREAM << "Added path listener for pattern: " << pathPattern;
}

void ConfigManager::notifyListeners(
    Domain domain,
    const std::string& path,
    const ConfigNode& oldValue,
    const ConfigNode& newValue) {
    
    std::unique_lock<std::mutex> lock(listenersMutex_);
    
    // 通知域监听器
    auto domainIt = domainListeners_.find(domain);
    if (domainIt != domainListeners_.end()) {
        for (auto& listener : domainIt->second) {
            try {
                listener(domain, path, oldValue, newValue);
            } catch (const std::exception& e) {
                INTERNAL_ERROR_STREAM << "Error in domain listener: " << e.what();
            }
        }
    }
    
    // 通知路径监听器
    for (auto& [pattern, listeners] : pathListeners_) {
        // 简单的通配符匹配（支持 * 通配符）
        bool match = false;
        
        if (pattern == "*") {
            match = true;
        } else if (pattern.back() == '*' && 
                   path.find(pattern.substr(0, pattern.length() - 1)) == 0) {
            match = true;
        } else if (pattern == path) {
            match = true;
        } else {
            // 尝试正则表达式匹配
            try {
                std::regex regexPattern(pattern);
                match = std::regex_match(path, regexPattern);
            } catch (const std::regex_error&) {
                // 如果正则表达式无效，回退到简单匹配
                match = (pattern == path);
            }
        }
        
        if (match) {
            for (auto& listener : listeners) {
                try {
                    listener(domain, path, oldValue, newValue);
                } catch (const std::exception& e) {
                    INTERNAL_ERROR_STREAM << "Error in path listener: " << e.what();
                }
            }
        }
    }
}


// ============ 配置验证 ============

ConfigManager::ValidationResult ConfigManager::validate(Domain domain) const {
    ValidationResult result;
    
    // 获取域配置
    ConfigNode::Ptr config;
    {
        std::shared_lock<std::shared_mutex> lock(configMutex_);
        auto it = domainConfigs_.find(domain);
        if (it != domainConfigs_.end()) {
            config = it->second;
        }
    }
    
    if (!config || config->isNull()) {
        result.warnings.push_back("Configuration is empty or null");
        return result;
    }
    
    // 这里可以添加具体的验证规则
    // 例如：检查必需字段、类型验证、范围验证等
    
    // 示例验证：检查应用配置
    if (domain == Domain::APPLICATION) {
        // 检查必需字段
        std::vector<std::string> requiredFields = {
            "app.name",
            "app.version"
        };
        
        for (const auto& field : requiredFields) {
            if (config->getPath(field,'.').isNull()) {
                result.errors.push_back(
                    foundation::utils::String::format("Required field missing: %s", field.c_str())
                );
                result.success = false;
            }
        }
        
        // 检查端口范围
        auto portNode = config->getPath("server.port",'.');
        if (!portNode.isNull() && portNode.isNumber()) {
            int port = portNode.asInt();
            if (port < 1 || port > 65535) {
                result.errors.push_back(
                    foundation::utils::String::format("Invalid port number: %d", port)
                );
                result.success = false;
            }
        }
    }
    
    return result;
}

ConfigManager::ValidationResult ConfigManager::validateAppConfig() const {
    return validate(Domain::APPLICATION);
}

// ============ 配置导出 ============

void ConfigManager::exportConfig(
    Domain domain,
    const std::string& format,
    const std::string& outputPath) const {
    
    INTERNAL_INFO_STREAM << "Exporting " << static_cast<int>(domain) << " configuration to " << outputPath << " as " << format;
    
    ConfigNode::Ptr config = getConfig(domain);
    
    if (!config || config->isNull()) {
        INTERNAL_WARN_STREAM << "No configuration to export for domain: " << static_cast<int>(domain);
        return;
    }
    
    try {
        if (format == "json" || format == "JSON") {
            std::string jsonStr = config->toJsonString(true);
            bool success = foundation::fs::File::writeText(outputPath, jsonStr);
            
            if (success) {
                INTERNAL_INFO_STREAM << "Configuration exported to " << outputPath << " as JSON";
            } else {
                INTERNAL_ERROR_STREAM << "Failed to write configuration to " << outputPath;
            }
            
        } else if (format == "yaml" || format == "YAML" || format == "yml") {
            std::string yamlStr = config->toYamlString();
            bool success = foundation::fs::File::writeText(outputPath, yamlStr);
            
            if (success) {
                INTERNAL_INFO_STREAM << "Configuration exported to " << outputPath << " as YAML";
            } else {
                INTERNAL_ERROR_STREAM << "Failed to write configuration to " << outputPath;
            }
            
        } else {
            INTERNAL_ERROR_STREAM << "Unsupported export format: " << format;
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to export configuration: " << e.what();
    }
}

// ============ 内部辅助方法 ============

ConfigNode::Ptr ConfigManager::mergeConfigs(
    const std::vector<ConfigNode::Ptr>& configs,
    Domain domain) {
    
    if (configs.empty()) {
        return std::make_shared<ConfigNode>();
    }
    
    // 从第一个配置开始
    auto result = std::make_shared<ConfigNode>(*configs[0]);
    
    // 按顺序合并其他配置
    for (size_t i = 1; i < configs.size(); ++i) {
        if (configs[i]) {
            result->overlay(*configs[i]);
        }
    }
    
    return result;
}

bool ConfigManager::registerDomain(const std::string& name, Domain domain) {
    // 这里可以添加域注册逻辑
    // 目前使用固定的域枚举
    INTERNAL_DEBUG_STREAM << "Domain registered: " << name << " -> " << static_cast<int>(domain);
    return true;
}
// ============ 静态快速配置访问方法实现 ============

std::string ConfigManager::get_app_config_string(const std::string& key,
                                             const std::string& default_value) {
     try {
        auto& config = getAppConfig();
        auto node = config->getPath(key, '.');  // 返回 ConfigNode
        if (node.isNull()) {
            return default_value;
        }
        return node.asString();  // 直接调用转换方法
    } catch (...) {
        return default_value;
    }
}

int ConfigManager::get_app_config_int(const std::string& key, int default_value) {

    try {
        auto& config =getAppConfig();
        // 1. 先获取 ConfigNode 对象
        auto node = config->getPath(key, '.');
        
        // 2. 检查是否为空
        if (node.isNull()) {
            return default_value;
        }
        // 3. 转换为 int
        return node.asInt(default_value);  // 如果 asInt 支持默认值
        // 或者: return node.asInt();
        
    } catch (...) {
        return default_value;
    }
}

double ConfigManager::get_app_config_double(const std::string& key, 
                                        double default_value) { 
    try {
        auto& config = getAppConfig();
        return config->get<double>(key, default_value, '.');
    } catch (...) {
        return default_value;
    }
}

bool ConfigManager::get_app_config_bool(const std::string& key, 
                                    bool default_value) {

    
    try {
        auto& config = getAppConfig();
        return config->get<bool>(key, default_value, '.');
    } catch (...) {
        return default_value;
    }
}

// ============ 命名配置文件操作 (ConfigFile 枚举) ============

namespace {

const std::unordered_map<ConfigFile, std::string>& s_configFilePaths() {
    static const std::unordered_map<ConfigFile, std::string> map = {
        {ConfigFile::TradingConnection, "trading_connection.json"},
        {ConfigFile::RiskConfig,        "risk_config.json"},
        {ConfigFile::Jujin,             "jujin.json"},
    };
    return map;
}

} // namespace

std::string ConfigManager::configFilePath(ConfigFile file) const {
    if (configBaseDir_.empty()) {
        INTERNAL_ERROR_STREAM << "[ConfigManager] 致命错误：ConfigManager 未初始化即调用 configFilePath";
        std::abort();
    }
    auto it = s_configFilePaths().find(file);
    if (it == s_configFilePaths().end()) {
        INTERNAL_ERROR_STREAM << "[ConfigManager] 未知的配置文件枚举值: "
                              << static_cast<int>(file);
        std::abort();
    }
    return configBaseDir_ + "/" + it->second;
}

ConfigNode::Ptr ConfigManager::loadConfigFile(ConfigFile file) {
    std::string path = configFilePath(file);
    ConfigNode::Ptr node;

    if (foundation::fs::File::exists(path)) {
        ConfigLoader::LoadOptions opts;
        opts.profile = currentProfile_;
        opts.enableCache = false;
        try {
            node = loader_->load(path, opts);
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[ConfigManager] Failed to parse " << path
                                 << ": " << e.what();
            node = std::make_shared<ConfigNode>();
        }
    } else if (file == ConfigFile::Jujin) {
        auto appCfg = getAppConfig();
        auto legacy = appCfg->getPath("jujin.config", '.');
        if (!legacy.isNull()) {
            node = std::make_shared<ConfigNode>(legacy);
            saveConfigFile(ConfigFile::Jujin, *node);
            INTERNAL_INFO_STREAM << "[ConfigManager] migrated jujin.config → jujin.json";
        } else {
            node = std::make_shared<ConfigNode>();
        }
    } else {
        node = std::make_shared<ConfigNode>();
    }

    return node;
}


bool ConfigManager::saveConfigFile(ConfigFile file, const ConfigNode& config) {
    std::string targetPath = configFilePath(file);
    if (!foundation::fs::File::atomicWrite(targetPath, config.toJsonString(true))) {
        return false;
    }

    // 更新缓存 (写后读一致性)
    {
        std::unique_lock<std::shared_mutex> lock(m_fileCacheMutex);
        m_fileConfigCache[file] = std::make_shared<ConfigNode>(config);
    }
    return true;
}

void ConfigManager::invalidateConfigFileCache(ConfigFile file) {
    std::unique_lock<std::shared_mutex> lock(m_fileCacheMutex);
    m_fileConfigCache.erase(file);
}

std::string ConfigManager::get_config_file_string(ConfigFile file,
    const std::string& key, const std::string& defaultVal) {
    auto cfg = loadConfigFile(file);
    auto node = cfg->getPath(key, '.');
    return node.isNull() ? defaultVal : node.asString();
}

int ConfigManager::get_config_file_int(ConfigFile file,
    const std::string& key, int def) {
    auto cfg = loadConfigFile(file);
    auto node = cfg->getPath(key, '.');
    return node.isNull() ? def : node.asInt(def);
}

double ConfigManager::get_config_file_double(ConfigFile file,
    const std::string& key, double def) {
    auto cfg = loadConfigFile(file);
    auto node = cfg->getPath(key, '.');
    return node.isNull() ? def : node.asDouble(def);
}

bool ConfigManager::get_config_file_bool(ConfigFile file,
    const std::string& key, bool def) {
    auto cfg = loadConfigFile(file);
    auto node = cfg->getPath(key, '.');
    return node.isNull() ? def : node.asBool(def);
}

ConfigNode::Ptr ConfigManager::loadDomainConfig(Domain domain) {
    // 根据域类型加载配置
    switch (domain) {
        case Domain::FOUNDATION:
            // 基础配置已经加载
            break;
        case Domain::PROFILE:
            // 环境配置已经加载
            break;
        case Domain::SYSTEM:
            // 系统配置已经加载
            break;
        case Domain::APPLICATION:
            // 应用配置已经加载
            break;
        case Domain::MODULE:
            // 模块配置动态加载
            break;
        case Domain::RUNTIME:
            // 运行时配置不需要加载
            break;
    }
    
    return getConfig(domain);
}

} // namespace config
} // namespace trader
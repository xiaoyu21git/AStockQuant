// config/ConfigManager.cpp
#include "foundation/config/ConfigManager.hpp"
#include "foundation/config/ConfigLoader.hpp"
#include "foundation/config/JsonConfigProvider.hpp"
#include "foundation/config/YamlConfigProvider.hpp"
#include "foundation/fs/File.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/utils/String.hpp"
#include "foundation/utils/Time.hpp"
#include <algorithm>
#include <regex>

namespace foundation {
namespace config {

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
    
    INTERNAL_DEBUG("ConfigManager constructed");
}

ConfigManager::~ConfigManager() {
    INTERNAL_DEBUG("ConfigManager destroyed");
}

// ============ 初始化方法 ============

void ConfigManager::initialize(
    const std::string& profile,
    const std::string& configDir) {
    
    INTERNAL_INFO("Initializing ConfigManager with profile: {}, configDir: {}", 
             profile, configDir);
    
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
        
        INTERNAL_INFO("ConfigManager initialized successfully");
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Failed to initialize ConfigManager: {}", e.what());
        throw foundation::ConfigException(
            foundation::utils::String::format(
                "ConfigManager initialization failed: {}", e.what()
            )
        );
    }
}

// ============ 配置加载方法 ============

void ConfigManager::loadFoundationConfigs() {
    INTERNAL_DEBUG("Loading foundation configurations");
    
    std::vector<ConfigNode::Ptr> foundationConfigs;
    std::string foundationDir = foundation::utils::String::endsWith(configBaseDir_, "/")
        ? configBaseDir_ + "foundation"
        : configBaseDir_ + "/foundation";
    
    // 检查基础配置目录是否存在
    if (!foundation::fs::File::exists(foundationDir)) {
        INTERNAL_WARN("Foundation config directory not found: {}", foundationDir);
        return;
    }
    
    if (!foundation::fs::File::isDirectory(foundationDir)) {
        INTERNAL_WARN("Foundation config path is not a directory: {}", foundationDir);
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
                
                INTERNAL_DEBUG("Loaded foundation config: {}", file);
                
            } catch (const std::exception& e) {
                INTERNAL_WARN("Failed to load foundation config {}: {}", file, e.what());
            }
        }
    }
    
    // 合并所有基础配置
    if (!foundationConfigs.empty()) {
        auto merged = mergeConfigs(foundationConfigs, Domain::FOUNDATION);
        
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        domainConfigs_[Domain::FOUNDATION] = merged;
        
        INTERNAL_INFO("Loaded {} foundation configurations", foundationConfigs.size());
    }
}

void ConfigManager::loadProfileConfig(const std::string& profile) {
    INTERNAL_DEBUG("Loading profile configuration: {}", profile);
    
    std::string profilePath = foundation::utils::String::format(
        "{}/profiles/{}.yaml", configBaseDir_, profile);
    
    // 检查配置文件是否存在
    if (!foundation::fs::File::exists(profilePath)) {
        INTERNAL_WARN("Profile config not found: {}", profilePath);
        
        // 尝试使用其他扩展名
        profilePath = foundation::utils::String::format(
            "{}/profiles/{}.yml", configBaseDir_, profile);
        
        if (!foundation::fs::File::exists(profilePath)) {
            INTERNAL_WARN("Profile config not found with .yml extension: {}", profilePath);
            return;
        }
    }
    
    try {
        ConfigLoader::LoadOptions options;
        options.profile = "";  // profile文件本身不包含环境变量
        
        auto config = loader_->load(profilePath, options);
        
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        domainConfigs_[Domain::PROFILE] = config;
        
        INTERNAL_INFO("Profile configuration loaded: {}", profile);
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Failed to load profile config {}: {}", profilePath, e.what());
        throw foundation::ConfigException(
            foundation::utils::String::format(
                "Failed to load profile config {}: {}", profile, e.what()
            )
        );
    }
}

void ConfigManager::loadSystemConfigs() {
    INTERNAL_DEBUG("Loading system configurations");
    
    std::vector<ConfigNode::Ptr> systemConfigs;
    std::string systemDir = configBaseDir_ + "/system";
    
    // 检查系统配置目录是否存在
    if (!foundation::fs::File::exists(systemDir)) {
        INTERNAL_DEBUG("System config directory not found: {}", systemDir);
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
                
                INTERNAL_DEBUG("Loaded system config: {}", file);
                
            } catch (const std::exception& e) {
                INTERNAL_WARN("Failed to load system config {}: {}", file, e.what());
            }
        }
    }
    
    // 合并系统配置
    if (!systemConfigs.empty()) {
        auto merged = mergeConfigs(systemConfigs, Domain::SYSTEM);
        
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        domainConfigs_[Domain::SYSTEM] = merged;
        
        INTERNAL_INFO("Loaded {} system configurations", systemConfigs.size());
    }
}

void ConfigManager::loadAppConfigs() {
    INTERNAL_DEBUG("Loading application configurations");
    
    // 尝试加载应用主配置文件
    std::vector<std::string> appConfigPaths = {
        "app/trader.yaml",
        "app/config.yaml",
        "app/trader.yml",
        "app/config.yml",
        "app/trader.json",
        "app/config.json"
    };
    
    ConfigNode::Ptr appConfig;
    
    for (const auto& path : appConfigPaths) {
        if (foundation::fs::File::exists(path)) {
            try {
                ConfigLoader::LoadOptions options;
                options.profile = currentProfile_;
                
                appConfig = loader_->load(path, options);
                INTERNAL_INFO("Application configuration loaded: {}", path);
                break;
                
            } catch (const std::exception& e) {
                INTERNAL_WARN("Failed to load app config {}: {}", path, e.what());
            }
        }
    }
    
    if (!appConfig) {
        INTERNAL_WARN("No application configuration found, using empty config");
        appConfig = std::make_shared<ConfigNode>();
    }
    
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    domainConfigs_[Domain::APPLICATION] = appConfig;
}

void ConfigManager::loadDynamicConfigs() {
    INTERNAL_DEBUG("Loading dynamic configurations");
    
    // 这里可以加载从数据库、API等动态源获取的配置
    // 目前仅初始化一个空的动态配置节点
    
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    domainConfigs_[Domain::RUNTIME] = runtimeConfig_;
    
    INTERNAL_DEBUG("Dynamic configurations initialized");
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
                INTERNAL_DEBUG("Module config loaded: {} from {}", moduleName, path);
                break;
                
            } catch (const std::exception& e) {
                INTERNAL_WARN("Failed to load module config {}: {}", path, e.what());
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
    
    INTERNAL_INFO("Runtime config updated: {} = {}", path, value.toString());
    
    // 如果要求持久化，保存到文件
    if (persist) {
        // 这里可以添加持久化逻辑
        INTERNAL_DEBUG("Runtime config persist requested (not implemented)");
    }
}

ConfigNode::Ptr ConfigManager::getRuntimeConfig() {
    std::shared_lock<std::shared_mutex> lock(configMutex_);
    return runtimeConfig_;
}

// ============ 配置管理方法 ============

void ConfigManager::reload(Domain domain) {
    INTERNAL_INFO("Reloading configuration domain: {}", static_cast<int>(domain));
    
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
    
    INTERNAL_INFO("Configuration domain {} reloaded", static_cast<int>(domain));
}

void ConfigManager::reloadAll() {
    INTERNAL_INFO("Reloading all configurations");
    
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
    
    INTERNAL_INFO("All configurations reloaded");
}

// ============ 监听器管理 ============

void ConfigManager::addDomainListener(Domain domain, ConfigChangeListener listener) {
    std::unique_lock<std::mutex> lock(listenersMutex_);
    domainListeners_[domain].push_back(listener);
    INTERNAL_DEBUG("Added domain listener for domain: {}", static_cast<int>(domain));
}

void ConfigManager::addPathListener(const std::string& pathPattern, ConfigChangeListener listener) {
    std::unique_lock<std::mutex> lock(listenersMutex_);
    pathListeners_[pathPattern].push_back(listener);
    INTERNAL_DEBUG("Added path listener for pattern: {}", pathPattern);
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
                INTERNAL_ERROR("Error in domain listener: {}", e.what());
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
                    INTERNAL_ERROR("Error in path listener: {}", e.what());
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
    
    INTERNAL_INFO("Exporting {} configuration to {} as {}", 
             static_cast<int>(domain), outputPath, format);
    
    ConfigNode::Ptr config = getConfig(domain);
    
    if (!config || config->isNull()) {
        INTERNAL_WARN("No configuration to export for domain: {}", static_cast<int>(domain));
        return;
    }
    
    try {
        if (format == "json" || format == "JSON") {
            std::string jsonStr = config->toJsonString(true);
            bool success = foundation::fs::File::writeText(outputPath, jsonStr);
            
            if (success) {
                INTERNAL_INFO("Configuration exported to {} as JSON", outputPath);
            } else {
                INTERNAL_ERROR("Failed to write configuration to {}", outputPath);
            }
            
        } else if (format == "yaml" || format == "YAML" || format == "yml") {
            std::string yamlStr = config->toYamlString();
            bool success = foundation::fs::File::writeText(outputPath, yamlStr);
            
            if (success) {
                INTERNAL_INFO("Configuration exported to {} as YAML", outputPath);
            } else {
                INTERNAL_ERROR("Failed to write configuration to {}", outputPath);
            }
            
        } else {
            INTERNAL_ERROR("Unsupported export format: {}", format);
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Failed to export configuration: {}", e.what());
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
    INTERNAL_DEBUG("Domain registered: {} -> {}", name, static_cast<int>(domain));
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
    // if (!instance().initialized_) {
    //     return default_value;
    // }
    
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
    // if (!instance().initialized_) {
    //     return default_value;
    // }
    
    try {
        auto& config = getAppConfig();
        return config->get<double>(key, default_value, '.');
    } catch (...) {
        return default_value;
    }
}

bool ConfigManager::get_app_config_bool(const std::string& key, 
                                    bool default_value) {
    // if (!instance().initialized_) {
    //     return default_value;
    // }
    
    try {
        auto& config = getAppConfig();
        return config->get<bool>(key, default_value, '.');
    } catch (...) {
        return default_value;
    }
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
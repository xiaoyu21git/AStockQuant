// config/providers/JsonConfigProvider.cpp
#include "foundation/config/JsonConfigProvider.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/core/exception.hpp"
namespace foundation {
namespace config {

JsonConfigProvider::JsonConfigProvider(
    bool enableEnvSubstitution,
    bool enableIncludes
) : enableEnvSubstitution_(enableEnvSubstitution),
    enableIncludes_(enableIncludes),
    envVarRegex_(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)(?::([^}]+))?\})") {
    
    // 添加默认环境变量前缀
    envPrefixes_.push_back("APP_");
    envPrefixes_.push_back("CONFIG_");
    
    // 初始化日志
    INTERNAL_DEBUG("JsonConfigProvider initialized with envSubstitution={}, includes={}",
              enableEnvSubstitution_, enableIncludes_);
}

std::shared_ptr<ConfigNode> JsonConfigProvider::load(
    const std::string& path,
    const std::string& profile) const{
    
    INTERNAL_INFO("Loading JSON config from: {}", path);
    
    try {
        // 加载JSON文件
        auto json = loadJsonFile(path);
        
        // 处理include指令
        if (enableIncludes_ && json.has("include")) {
            std::string baseDir = foundation::fs::File::directory(path);
            json = processIncludes(json, baseDir, profile);
        }
        
        // 提取环境特定配置
        if (!profile.empty() && json.has(profile)) {
            INTERNAL_DEBUG("Using profile-specific config: {}", profile);
            json = extractProfileConfig(json, profile);
        }
        
        // 环境变量替换
        if (enableEnvSubstitution_) {
            std::unordered_set<std::string> visited;
            substituteEnvironmentVariables(json, visited);
        }
        
        // 创建配置节点
        auto configNode = std::make_shared<ConfigNode>(json);
        
        // 设置源信息
        ConfigNode::SourceInfo sourceInfo;
        sourceInfo.path = path;
        sourceInfo.provider = type();
        sourceInfo.loadTime = std::chrono::system_clock::now();
        sourceInfo.size = foundation::fs::File::size(path);
        configNode->setSourceInfo(sourceInfo);
        
        INTERNAL_INFO("JSON config loaded successfully: {}", path);
        return configNode;
        
    } catch (const foundation::FileException& e) {
        INTERNAL_ERROR("File error loading config {}: {}", path, e.what());
        throw foundation::ConfigException(
            foundation::utils::String::format("Failed to load config {}: {}", 
                                             path, e.what()));
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Error loading config {}: {}", path, e.what());
        throw foundation::ConfigException(
            foundation::utils::String::format("Failed to load config {}: {}", 
                                             path, e.what()));
    }
}

void JsonConfigProvider::watch(
    const std::string& path,
    std::function<void(const std::shared_ptr<ConfigNode>&)> callback) {
    
    INTERNAL_DEBUG("Setting up watch for JSON config: {}", path);
    
    // 简化实现：使用文件修改时间检查
    // 实际应使用文件系统监控
    static std::map<std::string, std::chrono::system_clock::time_point> fileTimes;
    
    auto lastTime = fileTimes[path];
    auto currentTime = std::chrono::system_clock::now();
    
    // 检查文件是否被修改（简化版）
    // 实际实现应使用foundation::fs的文件监控功能
    INTERNAL_WARN("File watching not fully implemented for: {}", path);
}

bool JsonConfigProvider::save(
    const std::shared_ptr<ConfigNode>& config,
    const std::string& path) {
    
    try {
        INTERNAL_INFO("Saving config to: {}", path);
        return config->saveToFile(path);
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Failed to save config to {}: {}", path, e.what());
        return false;
    }
}

foundation::json::JsonFacade JsonConfigProvider::loadJsonFile(
    const std::string& path) const {
    
    if (!foundation::fs::File::exists(path)) {
        throw foundation::FileException(
            foundation::utils::String::format("Config file not found: {}", path));
    }
    
    try {
        std::string content = foundation::fs::File::readText(path);
        return foundation::json::JsonFacade::parse(content);
    } catch (const std::exception& e) {
        throw foundation::ParseException(
            foundation::utils::String::format("Failed to parse JSON file {}: {}", 
                                             path, e.what()));
    }
}

void JsonConfigProvider::substituteEnvironmentVariables(
    foundation::json::JsonFacade& json,
    std::unordered_set<std::string>& visited) const {
    
    // 递归遍历JSON并进行环境变量替换
    // 这里需要根据JsonFacade的实际接口实现
    // 简化实现：假设JsonFacade有遍历方法
    INTERNAL_DEBUG("Substituting environment variables in JSON");
    
    // 实现细节取决于JsonFacade的接口
    // 这里提供一个框架
}

foundation::json::JsonFacade JsonConfigProvider::processIncludes(
    const foundation::json::JsonFacade& json,
    const std::string& baseDir,
    const std::string& profile) const {
    
    INTERNAL_DEBUG("Processing includes from base directory: {}", baseDir);
    
    // 克隆输入JSON
    auto result = json; // 假设JsonFacade支持拷贝
    
    // 获取include列表
    if (!json.has("include")) return result;
    
    auto includeArray = json.get("include");
    if (!includeArray.isArray()) return result;
    
    // 处理每个include文件
    for (size_t i = 0; i < includeArray.size(); ++i) {
        try {
            std::string includeFile = includeArray.at(i).asString();
            std::string includePath = foundation::utils::String::endsWith(baseDir, "/") 
                ? baseDir + includeFile 
                : baseDir + "/" + includeFile;
            
            INTERNAL_DEBUG("Including config file: {}", includePath);
            
            // 递归加载include文件
            auto includedConfig = load(includePath, profile);
            auto includedJson = includedConfig->toString();
            auto parsedIncluded = foundation::json::JsonFacade::parse(includedJson);
            
            // 合并配置
            // 这里需要根据JsonFacade的合并接口实现
            
        } catch (const std::exception& e) {
            INTERNAL_WARN("Failed to include config file: {}", e.what());
        }
    }
    
    // 移除include节点
    // result.remove("include"); // 假设有这个方法
    
    return result;
}

foundation::json::JsonFacade JsonConfigProvider::extractProfileConfig(
    const foundation::json::JsonFacade& json,
    const std::string& profile) const {
    
    if (!json.has(profile)) {
        INTERNAL_WARN("Profile '{}' not found in config, using default", profile);
        return json;
    }
    
    return json.get(profile);
}

void JsonConfigProvider::addEnvPrefix(const std::string& prefix) {
    envPrefixes_.push_back(prefix);
}

void JsonConfigProvider::setStrictMode(bool strict) {
    strictMode_ = strict;
}

} // namespace config
} // namespace trader
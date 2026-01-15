// config/providers/JsonConfigProvider.hpp
#pragma once
#include "foundation/config/IConfigProvider.hpp"
#include "foundation/config/ConfigNode.hpp"
#include "foundation/json/json_facade.h"
#include "foundation/fs/File.hpp"
#include "foundation/log/Logger.hpp"
#include "foundation/utils/String.hpp"
#include <memory>
#include <regex>
#include <unordered_set>

namespace foundation {
namespace config {

class JsonConfigProvider : public IConfigProvider {
public:
    explicit JsonConfigProvider(
        bool enableEnvSubstitution = true,
        bool enableIncludes = true
    );
    
    ~JsonConfigProvider() override = default;
    
    // IConfigProvider接口实现
    std::shared_ptr<ConfigNode> load(
        const std::string& path,
        const std::string& profile = ""
    )  const override;
    
    void watch(
        const std::string& path,
        std::function<void(const std::shared_ptr<ConfigNode>&)> callback
    ) override;
    
    std::string type() const override {
        return "json";
    }
    
    std::vector<std::string> supportedExtensions() const override {
        return {".json", ".json5", ".conf"};
    }
    
    bool save(
        const std::shared_ptr<ConfigNode>& config,
        const std::string& path
    ) override;
    
    bool exists(const std::string& path) const override {
        return foundation::fs::File::exists(path);
    }
    
    // 环境变量替换相关配置
    void addEnvPrefix(const std::string& prefix);
    void setStrictMode(bool strict);
    
private:
    // 环境变量替换
    void substituteEnvironmentVariables(
        foundation::json::JsonFacade& json,
        std::unordered_set<std::string>& visited
    ) const;
    
    // 处理include指令
    foundation::json::JsonFacade processIncludes(
        const foundation::json::JsonFacade& json,
        const std::string& baseDir,
        const std::string& profile
    ) const;
    
    // 提取环境特定配置
    foundation::json::JsonFacade extractProfileConfig(
        const foundation::json::JsonFacade& json,
        const std::string& profile
    ) const;
    
    // 加载JSON文件
    foundation::json::JsonFacade loadJsonFile(
        const std::string& path
    ) const;
    
private:
    mutable bool enableEnvSubstitution_ = true;
    mutable bool enableIncludes_ = true;
    mutable bool strictMode_ = false;
    mutable std::vector<std::string> envPrefixes_;
    mutable std::regex envVarRegex_;
    
    // 日志标签
    static constexpr const char* LOG_TAG = "JsonConfigProvider";
};

} // namespace config
} // namespace trader
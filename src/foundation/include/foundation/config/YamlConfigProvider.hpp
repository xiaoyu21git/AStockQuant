// config/providers/YamlConfigProvider.hpp
#pragma once
#include "foundation/config/IConfigProvider.hpp"
#include "foundation/config/ConfigNode.hpp"
#include "foundation/yaml/Yaml_Facade.h"
#include "foundation/fs/File.hpp"
#include "foundation/log/Logger.hpp"
#include "foundation/utils/String.hpp"
#include <memory>
#include <unordered_map>

namespace foundation {
namespace config {

class YamlConfigProvider : public IConfigProvider {
public:
    explicit YamlConfigProvider(
        bool enableAnchors = true,
        bool multiDocument = false
    );
    
    ~YamlConfigProvider() override = default;
    
    // IConfigProvider接口实现
    std::shared_ptr<ConfigNode> load(
        const std::string& path,
        const std::string& profile
    ) const override;
    
    void watch(
        const std::string& path,
        std::function<void(const std::shared_ptr<ConfigNode>&)> callback
    ) override;
    
    std::string type() const override {
        return "yaml";
    }
    
    std::vector<std::string> supportedExtensions() const override {
        return {".yaml", ".yml"};
    }
    
    bool save(
        const std::shared_ptr<ConfigNode>& config,
        const std::string& path
    ) override;
    
    bool exists(const std::string& path) const override {
        return foundation::fs::File::exists(path);
    }
    
    // 自定义配置
    void setMultiDocumentMode(bool enable);
    void setEnableAnchors(bool enable);
    
private:
    // 处理多文档YAML
    foundation::yaml::YamlFacade processMultiDocument(
        const foundation::yaml::YamlFacade& yaml,
        const std::string& profile
    ) const;
    
    // 加载YAML文件
    foundation::yaml::YamlFacade loadYamlFile(
        const std::string& path
    ) const;
    
    // 合并YAML文档
    void mergeYamlDocuments(
        foundation::yaml::YamlFacade& target,
        const foundation::yaml::YamlFacade& source
    ) const;
    void printDocumentInfo(const foundation::yaml::YamlFacade& yaml) const;
    bool isMultiDocumentFile(const std::string& path) const ;
    foundation::yaml::YamlFacade mergeWithDefaultDocument(
    foundation::yaml::YamlFacade& multiDoc,
    const std::string& profile) const;
private:
    bool enableAnchors_;
    bool multiDocument_;
    
    // 日志标签
    static constexpr const char* LOG_TAG = "YamlConfigProvider";
};

} // namespace config
} // namespace trader
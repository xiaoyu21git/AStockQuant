// config/providers/YamlConfigProvider.cpp
#include "foundation/config/YamlConfigProvider.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/core/exception.hpp"

namespace foundation {
namespace config {

YamlConfigProvider::YamlConfigProvider(
    bool enableAnchors,
    bool multiDocument
) : enableAnchors_(enableAnchors),
    multiDocument_(multiDocument) {
    
    INTERNAL_DEBUG("YamlConfigProvider initialized with anchors={}, multiDocument={}",
              enableAnchors_, multiDocument_);
}

std::shared_ptr<ConfigNode> YamlConfigProvider::load(const std::string& path,
                         const std::string& profile) const
{
    INTERNAL_INFO("Loading YAML config from: {} (profile: {})", path, profile);

    try {
        // 1. 加载 YAML（Facade 仅作为局部使用）
        foundation::yaml::YamlFacade yaml = loadYamlFile(path);

        printDocumentInfo(yaml);

        // 2. 得到“最终 YAML 字符串结果”
        std::string yamlText;

        if (yaml.isMultiDocument() && multiDocument_) {
            // processMultiDocument 不返回 Facade，而返回 string / YAML::Node
            yamlText = processMultiDocument(yaml, profile).toString();
        } else {
            yamlText = yaml.toString();
        }

        // 3. YAML → JSON（字符串桥接）
        auto json = foundation::json::JsonFacade::parse(yamlText);

        // 4. JSON → ConfigNode（系统内唯一配置表示）
        auto configNode = std::make_shared<ConfigNode>(json);

        // 5. SourceInfo
        ConfigNode::SourceInfo sourceInfo;
        sourceInfo.path = path;
        sourceInfo.provider = type();
        sourceInfo.loadTime = std::chrono::system_clock::now();
        sourceInfo.size = foundation::fs::File::size(path);
        configNode->setSourceInfo(sourceInfo);

        INTERNAL_INFO("YAML config loaded successfully: {} (profile: {})", path, profile);
        return configNode;

    } catch (const foundation::FileException& e) {
        INTERNAL_ERROR("File error loading config {}: {}", path, e.what());
        throw foundation::ConfigException(
            foundation::utils::String::format(
                "Failed to load config {}: {}", path, e.what()));
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Error loading config {}: {}", path, e.what());
        throw foundation::ConfigException(
            foundation::utils::String::format(
                "Failed to load config {}: {}", path, e.what()));
    }
}

void YamlConfigProvider::watch(
    const std::string& path,
    std::function<void(const std::shared_ptr<ConfigNode>&)> callback) {
    
    INTERNAL_DEBUG("Setting up watch for YAML config: {}", path);
    
    // 简化实现，同JsonConfigProvider
    INTERNAL_WARN("File watching not fully implemented for: {}", path);
}

bool YamlConfigProvider::save(
    const std::shared_ptr<ConfigNode>& config,
    const std::string& path) {
    
    try {
        INTERNAL_INFO("Saving YAML config to: {}", path);
        
        // 获取YAML字符串
        std::string yamlStr = config->toYamlString();
        
        // 创建YamlFacade对象
        foundation::yaml::YamlFacade yaml;
        
        if (multiDocument_) {
            // 尝试解析为多文档
            if (!yaml.loadMultiDocumentFromString(yamlStr)) {
                // 如果解析失败，作为单文档处理
                INTERNAL_DEBUG("Not a multi-document, saving as single document");
                return foundation::fs::File::writeText(path, yamlStr);
            }
            
            // 保存为多文档
            return yaml.saveAsMultiDocument(path);
        } else {
            // 单文档保存
            return foundation::fs::File::writeText(path, yamlStr);
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Failed to save YAML config to {}: {}", path, e.what());
        return false;
    }
}

foundation::yaml::YamlFacade YamlConfigProvider::loadYamlFile(
    const std::string& path) const {
    
    if (!foundation::fs::File::exists(path)) {
        throw foundation::FileException(
            foundation::utils::String::format("Config file not found: {}", path));
    }
    
    try {
        auto yaml = foundation::yaml::YamlFacade::createFrom(path);
        if (!yaml.hasValue("")) {
            throw foundation::ParseException("Invalid YAML file");
        }
        return yaml;
    } catch (const std::exception& e) {
        throw foundation::ParseException(
            foundation::utils::String::format("Failed to parse YAML file {}: {}", 
                                             path, e.what()));
    }
}

foundation::yaml::YamlFacade YamlConfigProvider::processMultiDocument(
    const foundation::yaml::YamlFacade& yaml,
    const std::string& profile) const {
    
    INTERNAL_DEBUG("Processing multi-document YAML with profile: {}", profile);
    
    // 如果不启用多文档模式或者文档数量小于2，创建新对象并合并
    if (!multiDocument_ || yaml.getDocumentCount() < 2) {
        INTERNAL_DEBUG("Not processing as multi-document (mode={}, count={})", 
                 multiDocument_, yaml.getDocumentCount());
        // 创建新对象并合并原始内容
        foundation::yaml::YamlFacade result = foundation::yaml::YamlFacade::createEmpty();
        result.merge(yaml);  // 使用 merge 而不是拷贝
        return result;  // 支持移动构造
    }
    
    INTERNAL_INFO("Processing multi-document YAML with {} documents, profile: {}", 
             yaml.getDocumentCount(), profile);
    
    try {
        // 1. 创建新对象而不是尝试拷贝
        foundation::yaml::YamlFacade result = foundation::yaml::YamlFacade::createEmpty();
        result.merge(yaml);  // 复制内容
        
        // 2. 查找匹配的profile文档
        bool profileFound = result.selectDocumentByProfile(profile);
        
        if (profileFound) {
            INTERNAL_INFO("Found profile-specific document for: {}", profile);
            
            // 3. 检查是否需要合并默认文档
            auto merged = mergeWithDefaultDocument(result, profile);
            return merged;  // 支持移动
            
        } else {
            // 4. 如果没有找到特定profile，查找默认文档
            foundation::yaml::YamlFacade::DocumentSelector defaultSelector;
            defaultSelector.isDefault = true;
            
            bool defaultFound = result.findDocument(defaultSelector);
            
            if (defaultFound) {
                INTERNAL_INFO("No profile-specific document found, using default document");
                return result;  // 支持移动
            } else {
                // 5. 既没有profile文档也没有默认文档，使用第一个文档
                INTERNAL_WARN("No profile-specific or default document found, using first document");
                result.setCurrentDocument(0);
                return result;  // 支持移动
            }
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Error processing multi-document YAML: {}", e.what());
        
        // 出错时创建新对象
        foundation::yaml::YamlFacade fallback = foundation::yaml::YamlFacade::createEmpty();
        // 尝试复制第一个文档的内容
        // 这里需要简化处理，因为不能直接拷贝
        fallback.setCurrentDocument(0);
        // 如果可以，从原始yaml复制一些基本内容
        try {
            // 尝试复制一些基本信息
            // 具体实现取决于 YamlFacade 的功能
        } catch (...) {
            // 忽略复制错误
        }
        return fallback;  // 支持移动
    }
}
// ============ 辅助方法：合并默认文档 ============

foundation::yaml::YamlFacade YamlConfigProvider::mergeWithDefaultDocument(
    foundation::yaml::YamlFacade& multiDoc,
    const std::string& profile) const {
    
    INTERNAL_DEBUG("Merging documents for profile: {}", profile);
    
    // 保存当前profile文档的索引
    size_t profileIndex = multiDoc.getCurrentDocumentIndex();
    auto profileMeta = multiDoc.getCurrentDocumentMeta();
    
    // 查找默认文档
    foundation::yaml::YamlFacade::DocumentSelector defaultSelector;
    defaultSelector.isDefault = true;
    
    bool hasDefault = multiDoc.findDocument(defaultSelector);
    
    if (!hasDefault) {
        INTERNAL_DEBUG("No default document found, using profile document directly");
        // 创建profile文档的副本
        foundation::yaml::YamlFacade result = foundation::yaml::YamlFacade::createEmpty();
        
        // 切换回profile文档
        multiDoc.setCurrentDocument(profileIndex);
        
        // 合并profile文档内容
        result.merge(multiDoc);
        return result;
    }
    
    size_t defaultIndex = multiDoc.getCurrentDocumentIndex();
    
    INTERNAL_DEBUG("Merging profile document (index={}) with default document (index={})",
             profileIndex, defaultIndex);
    
    try {
        // 1. 创建合并结果对象
        foundation::yaml::YamlFacade merged = foundation::yaml::YamlFacade::createEmpty();
        
        // 2. 先合并默认文档（作为基础）
        multiDoc.setCurrentDocument(defaultIndex);
        merged.merge(multiDoc);
        
        // 3. 再合并profile文档（覆盖默认配置）
        multiDoc.setCurrentDocument(profileIndex);
        merged.merge(multiDoc);
        
        // 4. 如果需要保持多文档结构，可以这样处理：
        if (multiDoc.isMultiDocument()) {
            // 创建一个新的多文档对象
            foundation::yaml::YamlFacade finalResult = foundation::yaml::YamlFacade::createEmpty();
            
            // 添加合并后的文档作为第一个文档
            finalResult.addDocument(merged, profileMeta);
            
            // 复制原始的其他文档（除了已合并的两个）
            for (size_t i = 0; i < multiDoc.getDocumentCount(); ++i) {
                if (i != profileIndex && i != defaultIndex) {
                    // 切换到该文档
                    multiDoc.setCurrentDocument(i);
                    auto docMeta = multiDoc.getCurrentDocumentMeta();
                    
                    // 创建该文档的副本
                    foundation::yaml::YamlFacade docCopy = foundation::yaml::YamlFacade::createEmpty();
                    docCopy.merge(multiDoc);
                    
                    // 添加到结果
                    finalResult.addDocument(docCopy, docMeta);
                }
            }
            
            // 设置当前文档为合并后的文档
            finalResult.setCurrentDocument(0);
            INTERNAL_INFO("Successfully merged profile '{}' with default document in multi-doc context", profile);
            return finalResult;
        } else {
            INTERNAL_INFO("Successfully merged profile '{}' with default document", profile);
            return merged;
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR("Failed to merge documents: {}, using profile document only", e.what());
        
        // 出错时只返回profile文档的副本
        foundation::yaml::YamlFacade fallback = foundation::yaml::YamlFacade::createEmpty();
        
        // 切换回profile文档
        multiDoc.setCurrentDocument(profileIndex);
        fallback.merge(multiDoc);
        
        return fallback;
    }
}

// ============ 添加配置文件自动检测方法 ============

bool YamlConfigProvider::isMultiDocumentFile(const std::string& path) const {
    try {
        if (!foundation::fs::File::exists(path)) {
            return false;
        }
        
        // 读取文件内容检查是否有多个 "---" 分隔符
        std::string content = foundation::fs::File::readText(path);
        
        size_t firstDash = content.find("---");
        if (firstDash == std::string::npos) {
            return false;  // 没有分隔符，是单文档
        }
        
        // 查找第二个分隔符（跳过第一个）
        size_t secondDash = content.find("---", firstDash + 3);
        return secondDash != std::string::npos;
        
    } catch (...) {
        return false;
    }
}
// ============ 新增方法：文档信息打印 ============

void YamlConfigProvider::printDocumentInfo(const foundation::yaml::YamlFacade& yaml) const {
    if (!yaml.isMultiDocument()) {
        INTERNAL_DEBUG("Single document YAML");
        return;
    }
    
    size_t count = yaml.getDocumentCount();
    INTERNAL_DEBUG("Multi-document YAML with {} documents:", count);
    
    for (size_t i = 0; i < count; ++i) {
        foundation::yaml::YamlFacade doc = foundation::yaml::YamlFacade::createEmpty();
        doc.setCurrentDocument(i);
        auto meta = doc.getCurrentDocumentMeta();
        
        INTERNAL_DEBUG("  Document {}: profile='{}', tag='{}', default={}, hasValues={}",
                 i, meta.profile, meta.tag, meta.isDefault, !doc.toString().empty());
    }
}
void YamlConfigProvider::setMultiDocumentMode(bool enable) {
    multiDocument_ = enable;
    INTERNAL_DEBUG("YamlConfigProvider multi-document mode: {}", enable);
}

void YamlConfigProvider::setEnableAnchors(bool enable) {
    enableAnchors_ = enable;
    INTERNAL_DEBUG("YamlConfigProvider anchors enabled: {}", enable);
}

} // namespace config
} // namespace trader
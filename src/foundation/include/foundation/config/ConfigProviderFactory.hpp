// ConfigProviderFactory.hpp
#pragma once
#include "config/IConfigProvider.hpp"
#include "config/providers/JsonConfigProvider.hpp"
#include "config/providers/YamlConfigProvider.hpp"
#include "Utils/Singleton.hpp"
#include <map>
#include <functional>

namespace foundation {
namespace config {

/**
 * @brief 配置提供者工厂
 * 
 * 用于创建和管理各种格式的配置提供者
 */
class ConfigProviderFactory : public Utils::Singleton<ConfigProviderFactory> {
public:
    using ProviderCreator = std::function<IConfigProvider::Ptr()>;
    
    /**
     * @brief 注册配置提供者
     * @param name 提供者名称
     * @param extensions 支持的扩展名
     * @param creator 创建函数
     */
    void registerProvider(
        const std::string& name,
        const std::vector<std::string>& extensions,
        ProviderCreator creator
    );
    
    /**
     * @brief 根据扩展名创建提供者
     * @param extension 文件扩展名
     * @return 配置提供者指针
     */
    IConfigProvider::Ptr createByExtension(const std::string& extension);
    
    /**
     * @brief 根据类型名创建提供者
     * @param type 提供者类型名
     * @return 配置提供者指针
     */
    IConfigProvider::Ptr createByType(const std::string& type);
    
    /**
     * @brief 获取所有支持的扩展名
     */
    std::vector<std::string> getAllExtensions() const;
    
    /**
     * @brief 获取所有支持的提供者类型
     */
    std::vector<std::string> getAllProviderTypes() const;
    
    /**
     * @brief 注册默认提供者（JSON和YAML）
     */
    void registerDefaultProviders();
    
private:
    struct ProviderInfo {
        std::string name;
        std::vector<std::string> extensions;
        ProviderCreator creator;
    };
    
    std::map<std::string, ProviderInfo> providers_;
    std::map<std::string, std::string> extensionToType_;
    
    mutable thread::RWLock lock_;
};

} // namespace config
} // namespace trader
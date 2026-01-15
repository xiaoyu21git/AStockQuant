// config/IConfigProvider.hpp
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace foundation {
namespace config {

// 前向声明
class ConfigNode;

/**
 * @brief 配置提供者接口
 */
class IConfigProvider {
public:
    using Ptr = std::shared_ptr<IConfigProvider>;
    
    virtual ~IConfigProvider() = default;
    
    /**
     * @brief 加载配置
     * @param path 配置文件路径
     * @param profile 环境配置（如development, production）
     * @return 配置节点指针
     */
    virtual std::shared_ptr<ConfigNode> load(
        const std::string& path,
        const std::string& profile = ""
    ) const = 0;
    
    /**
     * @brief 监听配置文件变化
     * @param path 配置文件路径
     * @param callback 变化回调函数
     */
    virtual void watch(
        const std::string& path,
        std::function<void(const std::shared_ptr<ConfigNode>&)> callback
    ) = 0;
    
    /**
     * @brief 获取提供者类型
     * @return 类型字符串
     */
    virtual std::string type() const = 0;
    
    /**
     * @brief 支持的扩展名
     * @return 扩展名列表
     */
    virtual std::vector<std::string> supportedExtensions() const = 0;
    
    /**
     * @brief 保存配置
     * @param config 配置节点
     * @param path 保存路径
     * @return 是否保存成功
     */
    virtual bool save(
        const std::shared_ptr<ConfigNode>& config,
        const std::string& path
    ) = 0;
    
    /**
     * @brief 检查配置文件是否存在
     */
    virtual bool exists(const std::string& path) const = 0;
};

} // namespace config
} // namespace trader
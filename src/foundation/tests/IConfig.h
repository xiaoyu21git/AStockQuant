#ifndef ICONFIG_H
#define ICONFIG_H

#include <string>
#include <optional>
#include <vector>

// 配置项结构体
struct ConfigItem {
    std::string key;
    std::string value;
    std::string description;
    bool isRequired;
    std::string defaultValue;
};

// 配置接口抽象
class IConfig {
public:
    virtual ~IConfig() = default;
    
    // 加载配置
    virtual bool load(const std::string& source) = 0;
    virtual bool save(const std::string& destination) = 0;
    
    // 获取配置值
    virtual std::optional<std::string> getString(const std::string& key) const = 0;
    virtual std::optional<int> getInt(const std::string& key) const = 0;
    virtual std::optional<double> getDouble(const std::string& key) const = 0;
    virtual std::optional<bool> getBool(const std::string& key) const = 0;
    
    // 设置配置值
    virtual bool setString(const std::string& key, const std::string& value) = 0;
    virtual bool setInt(const std::string& key, int value) = 0;
    virtual bool setBool(const std::string& key, bool value) = 0;
    
    // 配置验证
    virtual bool validate() const = 0;
    virtual std::vector<std::string> getErrors() const = 0;
    
    // 配置管理
    virtual bool hasKey(const std::string& key) const = 0;
    virtual size_t count() const = 0;
    virtual void clear() = 0;
    
    // 配置项注册
    virtual void registerItem(const ConfigItem& item) = 0;
    virtual std::optional<ConfigItem> getItem(const std::string& key) const = 0;
};
#endif // ICONFIG_H
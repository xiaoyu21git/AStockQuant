// config/ConfigNode.hpp
#pragma once
#include "foundation/json/json_facade.h"
#include "foundation/yaml/yaml_facade.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <shared_mutex>  // 添加头文件

namespace foundation {
namespace config {
class ConfigNode {
public:
    using Ptr = std::shared_ptr<ConfigNode>;
    
    // 配置源信息
    struct SourceInfo {
        std::string path;
        std::string provider;
        std::chrono::system_clock::time_point loadTime;
        size_t size = 0;
        
        std::string toString() const {
            return path + " (via " + provider + ")";
        }
    };

    // 构造函数
    ConfigNode();
    ~ConfigNode();
    // 拷贝构造函数（必须显式定义）
    ConfigNode(const ConfigNode& other);
    
    // 拷贝赋值运算符
    ConfigNode& operator=(const ConfigNode& other);
    explicit ConfigNode(const foundation::json::JsonFacade& json);
    // 添加 YamlFacade 构造函数
    explicit ConfigNode(const foundation::yaml::YamlFacade& yaml);
    explicit ConfigNode(bool value);
    explicit ConfigNode(int value);
    explicit ConfigNode(double value);
    explicit ConfigNode(const std::string& value);
    explicit ConfigNode(const char* value);
    // 必须添加这些移动操作声明
    ConfigNode(ConfigNode&&) noexcept;
    ConfigNode& operator=(ConfigNode&&) noexcept;
    
    // 如果需要禁用拷贝（推荐，因为 unique_ptr 不可拷贝）
    // ConfigNode(const ConfigNode&);
    // ConfigNode& operator=(const ConfigNode&) ;
    
    // 或者如果需要拷贝，添加 clone 方法
    ConfigNode clone() const;
    // 类型检查
    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isInt() const;
    bool isDouble() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    // 值获取方法
    bool asBool(bool defaultValue = false) const;
    int asInt(int defaultValue = 0) const;
    double asDouble(double defaultValue = 0.0) const;
    std::string asString(const std::string& defaultValue = "") const;

    // 模板方法
    template<typename T>
    T as() const;

    template<typename T>
    T as(const T& defaultValue) const;

    // 数组操作
    size_t size() const;
    ConfigNode at(size_t index) const;
    std::vector<ConfigNode> asArray() const;

    // 对象操作
    bool has(const std::string& key) const;
    ConfigNode get(const std::string& key) const;

    template<typename T>
    T get(const std::string& key, const T& defaultValue) const;
    
    // // 路径访问（支持点分路径）
    ConfigNode getPath(const std::string& path, char delimiter = '.') const;

    template<typename T>
    T get(const std::string& path, const T& defaultValue, char delimiter = '.') const;

    // 合并操作
    void merge(const ConfigNode& other, bool overwrite = true);
    void overlay(const ConfigNode& other);

    // 序列化
    std::string toString(bool pretty = false) const;
    std::string toJsonString(bool pretty = true) const;
    std::string toYamlString() const;
    void convertToYaml(yaml::YamlFacade& yaml, const std::string& path) const ;

    // 源信息
    const SourceInfo& getSourceInfo() const;
    void setSourceInfo(const SourceInfo& info);

    // 便捷方法
    bool isEmpty() const { return isNull(); }
    bool isNotEmpty() const { return !isNull(); }
    // 文件操作
    bool saveToFile(const std::string& filename) const;
    bool saveToJsonFile(const std::string& filename, bool pretty = true) const;
    bool saveToYamlFile(const std::string& filename) const;
    
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};



} // namespace config
} // namespace trader
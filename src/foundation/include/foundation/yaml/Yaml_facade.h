// yaml_facade.h
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <map>

namespace foundation::yaml {
    
class YamlFacade {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
public:
    YamlFacade();
    ~YamlFacade();
    
    // 禁止拷贝
    YamlFacade(const YamlFacade&) = delete;
    YamlFacade& operator=(const YamlFacade&) = delete;
    
    // 允许移动
    YamlFacade(YamlFacade&&) noexcept;
    YamlFacade& operator=(YamlFacade&&) noexcept;
    
    // 加载和保存
    bool loadFromFile(const std::string& filename);
    bool loadFromString(const std::string& yaml);
    bool saveToFile(const std::string& filename) const;
    std::string toString() const;
    
    // 值操作
    bool hasValue(const std::string& path) const;
    
    // 获取值（简化接口）
    std::string getString(const std::string& path, 
                         const std::string& defaultValue = "") const;
    int getInt(const std::string& path, int defaultValue = 0) const;
    double getDouble(const std::string& path, double defaultValue = 0.0) const;
    bool getBool(const std::string& path, bool defaultValue = false) const;
    
    // 获取数组
    std::vector<std::string> getStringArray(const std::string& path) const;
    std::vector<int> getIntArray(const std::string& path) const;
    std::vector<double> getDoubleArray(const std::string& path) const;
    
    // 获取映射
    std::map<std::string, std::string> getStringMap(const std::string& path) const;
    std::map<std::string, int> getIntMap(const std::string& path) const;
    
    // 设置值
    void setString(const std::string& path, const std::string& value);
    void setInt(const std::string& path, int value);
    void setDouble(const std::string& path, double value);
    void setBool(const std::string& path, bool value);
    
    // 设置数组
    void setStringArray(const std::string& path, 
                       const std::vector<std::string>& values);
    void setIntArray(const std::string& path, 
                    const std::vector<int>& values);
    
    // 设置映射
    void setStringMap(const std::string& path,
                     const std::map<std::string, std::string>& values);
    
    // 合并配置
    void merge(const YamlFacade& other);
    
    // 清除配置
    void clear();
    
    // 工具方法
    static YamlFacade createEmpty();
    static YamlFacade loadFrom(const std::string& filename);
    static YamlFacade parse(const std::string& yaml);
    
private:
    // 内部实现
    explicit YamlFacade(std::unique_ptr<Impl> impl);
};

} // namespace foundation::yaml
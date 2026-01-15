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
    // ============ 构造函数和析构函数 ============
    YamlFacade();
    ~YamlFacade();
    
    // 禁止拷贝
    YamlFacade(const YamlFacade&) = delete;
    YamlFacade& operator=(const YamlFacade&) = delete;
    
    // 允许移动
    YamlFacade(YamlFacade&&) noexcept;
    YamlFacade& operator=(YamlFacade&&) noexcept;
    
    // ============ 单文档接口（原有功能） ============
    
    // 加载和保存
    bool loadFromFile(const std::string& filename);
    bool loadFromString(const std::string& yaml);
    bool saveToFile(const std::string& filename) const;
    std::string toString() const;
    
    // 值检查
    bool hasValue(const std::string& path) const;
    
    // 值获取（简化接口）
    std::string getString(const std::string& path, 
                         const std::string& defaultValue = "") const;
    int getInt(const std::string& path, int defaultValue = 0) const;
    double getDouble(const std::string& path, double defaultValue = 0.0) const;
    bool getBool(const std::string& path, bool defaultValue = false) const;
    
    // 数组获取
    std::vector<std::string> getStringArray(const std::string& path) const;
    std::vector<int> getIntArray(const std::string& path) const;
    std::vector<double> getDoubleArray(const std::string& path) const;
    
    // 映射获取
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
     // 添加缺失的方法
    void setDoubleArray(const std::string& path, const std::vector<double>& values);
    
    // 设置映射
    void setStringMap(const std::string& path,
                     const std::map<std::string, std::string>& values);
    
    // 合并配置
    void merge(const YamlFacade& other);
    
    // 清除配置
    void clear();
    
    // ============ 多文档支持（新增功能） ============
    
    // 多文档加载
    bool loadMultiDocument(const std::string& filename);
    bool loadMultiDocumentFromString(const std::string& yaml);
    
    // 多文档保存
    bool saveAsMultiDocument(const std::string& filename) const;
    
    // 文档管理
    size_t getDocumentCount() const;
    size_t getCurrentDocumentIndex() const;
    bool setCurrentDocument(size_t index);
    
    // 文档选择器
    struct DocumentSelector {
        std::string profile;  // 环境标识
        std::string tag;      // 标签
        bool isDefault;       // 是否默认
        
        DocumentSelector() : profile(""), tag(""), isDefault(false) {}
        DocumentSelector(const std::string& p, const std::string& t, bool d) 
            : profile(p), tag(t), isDefault(d) {}
    };
    
    // 根据选择器查找文档
    bool findDocument(const DocumentSelector& selector);
    
    // 获取当前文档的元数据
    DocumentSelector getCurrentDocumentMeta() const;
    
    // 添加新文档
    void addDocument(const YamlFacade& document, 
                    const DocumentSelector& selector = DocumentSelector());
    
    // 移除文档
    bool removeDocument(size_t index);
    
    // 检查是否多文档
    bool isMultiDocument() const;
    
    // 根据profile选择文档（便捷方法）
    bool selectDocumentByProfile(const std::string& profile);
    
    // ============ 静态工厂方法 ============
    
    static YamlFacade createEmpty();
    static YamlFacade createFrom(const std::string& filename);
    static YamlFacade parse(const std::string& yaml);
    
private:
    explicit YamlFacade(std::unique_ptr<Impl> impl);
};

} // namespace foundation::yaml
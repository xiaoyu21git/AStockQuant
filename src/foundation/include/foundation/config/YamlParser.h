#pragma once
#include <string>
#include "IConfigParser.h"
#include "foundation/yaml/Yaml_facade.h"

// ================================
// YamlParser.h
// 说明：
// 1. 依赖现有 YAML 封装（例如 YAML::Node 封装）
// 2. 不直接暴露底层 YAML 类型给上层
// 3. 可扩展接口标注：
//    - parseString 可扩展为 parseFile
//    - getValue 模板可扩展为支持更多类型
// ================================

namespace foundation::config {

class YamlParser : public IConfigParser {
public:
    YamlParser() = default;
    ~YamlParser() override = default;

    // ----------------------
    // 可修改接口：parseString
    // 说明：现在只解析字符串，未来可以改成解析文件或网络流
    // ----------------------
    void parseString(const std::string& text) override;

    // ----------------------
    // 可修改接口：getValue<T>
    // 说明：可扩展类型 T
    // ----------------------
    template<typename T>
    T getValue(const std::string& key) const override;

    bool contains(const std::string& key) const override;

private:
    // ----------------------
    // 不可修改区域：封装内部 YAML 对象
    // 说明：直接使用你现有 YAML 封装类型
    // ----------------------
    foundation::yaml::YamlFacade yaml_;  // <-- 这里替换成你的现有 YAML 封装类型
};

} // namespace foundation::config

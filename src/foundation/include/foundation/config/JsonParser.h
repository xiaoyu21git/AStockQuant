#pragma once
#include <string>
#include "IConfigParser.h"
#include "foundation/json/json_facade.h"

// ================================
// JsonParser.h
// 说明：
// 1. 依赖现有 JSON 封装（例如 nlohmann::json 封装）
// 2. 不直接暴露底层 JSON 类型给上层
// 3. 可扩展接口标注：
//    - parseString 可扩展为 parseFile
//    - getValue 模板可扩展为支持更多类型
// ================================

namespace foundation::config {

class JsonParser : public IConfigParser {
public:
    JsonParser() = default;
    ~JsonParser() override = default;

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
    // 不可修改区域：封装内部 JSON 对象
    // 说明：直接使用你现有 JSON 封装类型
    // ----------------------
   foundation::json::JsonFacade json_Facade;   // <-- 这里替换成你的现有 JSON 封装类型
};

} // namespace foundation::config

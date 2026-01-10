#include "foundation/config/JsonParser.h"
#include <stdexcept>
#include <iostream>

namespace foundation::config {

// ----------------------
// parseString: 可扩展
// 现在只解析字符串，未来可增加文件解析、网络解析等
// ----------------------
void JsonParser::parseString(const std::string& text) {
    try {
        json_Facade.parse(text); // 调用现有 JSON 封装解析方法
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("JSON parse error: ") + e.what());
    }
}

// ----------------------
// contains: 不可改，保持语义
// ----------------------
bool JsonParser::contains(const std::string& key) const {
    return json_Facade.contains(key); // 直接调用封装
}

// ----------------------
// getValue<T>: 可扩展
// 说明：可以增加对更多类型支持
// ----------------------
template<typename T>
T JsonParser::getValue(const std::string& key) const {
    if (!json_Facade.contains(key)) {
        throw std::runtime_error("Key not found: " + key);
    }

    try {
        return json_Facade.get<T>(key); // 调用现有封装
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("JSON getValue error: ") + e.what());
    }
}

// 显式实例化常用类型
template int JsonParser::getValue<int>(const std::string&) const;
template double JsonParser::getValue<double>(const std::string&) const;
template std::string JsonParser::getValue<std::string>(const std::string&) const;

} // namespace foundation::config

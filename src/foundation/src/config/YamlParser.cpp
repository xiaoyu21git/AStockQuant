#include "foundation/config/YamlParser.h"
#include <stdexcept>
#include <iostream>

namespace foundation::config {

// ----------------------
// parseString: 可扩展
// 现在只解析字符串，未来可增加文件解析、网络解析等
// ----------------------
void YamlParser::parseString(const std::string& text) {
    try {
        yaml_.parse(text); // 调用现有 YAML 封装解析方法
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("YAML parse error: ") + e.what());
    }
}

// ----------------------
// contains: 不可改，保持语义
// ----------------------
bool YamlParser::contains(const std::string& key) const {
    return yaml_.contains(key); // 直接调用封装
}

// ----------------------
// getValue<T>: 可扩展
// 说明：可以增加对更多类型支持
// ----------------------
template<typename T>
T YamlParser::getValue(const std::string& key) const {
    if (!yaml_.contains(key)) {
        throw std::runtime_error("Key not found: " + key);
    }

    try {
        return yaml_.get<T>(key); // 调用现有封装
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("YAML getValue error: ") + e.what());
    }
}

// 显式实例化常用类型
template int YamlParser::getValue<int>(const std::string&) const;
template double YamlParser::getValue<double>(const std::string&) const;
template std::string YamlParser::getValue<std::string>(const std::string&) const;

} // namespace foundation::config

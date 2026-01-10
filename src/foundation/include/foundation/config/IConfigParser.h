#pragma once
#include <string>
#include <stdexcept>

namespace foundation::config {

struct IConfigParser {
    virtual ~IConfigParser() = default;

    // 解析字符串
    virtual void parseString(const std::string& text) = 0;

    // 获取 key 的值
    template<typename T>
    T getValue(const std::string& key) const {
        throw std::runtime_error("Not implemented");
    }

    // 是否包含 key
    virtual bool contains(const std::string& key) const = 0;
};

} // namespace foundation::config

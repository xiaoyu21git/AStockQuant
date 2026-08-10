// SafeParse.hpp — 安全解析工具: stoi/stod/stoll 的 std::optional 包装
// 不抛异常, 解析失败返回 std::nullopt。调用方自行决定如何处理缺失值。
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace foundation {
namespace utils {

/// @brief 安全解析 int
inline std::optional<int> parseInt(const std::string& str) {
    if (str.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        int v = std::stoi(str, &pos);
        if (pos != str.size()) return std::nullopt;  // 尾部有非数字字符
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

/// @brief 安全解析 int64_t
inline std::optional<std::int64_t> parseInt64(const std::string& str) {
    if (str.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        std::int64_t v = std::stoll(str, &pos);
        if (pos != str.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

/// @brief 安全解析 double
inline std::optional<double> parseDouble(const std::string& str) {
    if (str.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        double v = std::stod(str, &pos);
        if (pos != str.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

/// @brief 安全解析 uint64_t
inline std::optional<std::uint64_t> parseUInt64(const std::string& str) {
    if (str.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        std::uint64_t v = std::stoull(str, &pos);
        if (pos != str.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

/// @brief 安全解析, 带默认值 (失败时返回默认值)
inline int parseIntOrDefault(const std::string& str, int defaultVal = 0) {
    return parseInt(str).value_or(defaultVal);
}
inline std::int64_t parseInt64OrDefault(const std::string& str, std::int64_t defaultVal = 0) {
    return parseInt64(str).value_or(defaultVal);
}
inline double parseDoubleOrDefault(const std::string& str, double defaultVal = 0.0) {
    return parseDouble(str).value_or(defaultVal);
}

} // namespace utils
} // namespace foundation

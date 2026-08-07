// SqlEscape.h — SQL 字符串转义工具
// 安全拼接用户字符串到 SQL 语句中（防止单引号注入）
#pragma once

#include <string>

namespace astock {
namespace database {

/// @brief 将字符串值转为 SQL 安全的 quoted literal
/// 包围单引号，转义内部引号和反斜杠
inline std::string safeStr(const std::string& v) {
    std::string escaped;
    escaped.reserve(v.size() + 2);
    escaped += '\'';
    for (char c : v) {
        if (c == '\'' || c == '\\') escaped += '\\';
        escaped += c;
    }
    escaped += '\'';
    return escaped;
}

} // namespace database
} // namespace astock

// DateUtils.h — 交易日日期格式化/解析统一工具
// 用于消除分散在各模块中的 yyyymmdd ↔ "yyyy-mm-dd" 手工转换
#pragma once

#include <cstdio>
#include <string>

namespace foundation {
namespace utils {

/// @brief 将 yyyymmdd 整数分解为 (年, 月, 日)
inline void decomposeDate(int yyyymmdd, int& y, int& m, int& d) {
    y = yyyymmdd / 10000;
    m = (yyyymmdd / 100) % 100;
    d = yyyymmdd % 100;
}

/// @brief 将 yyyymmdd 整数格式化为 "yyyy-mm-dd" 字符串
inline std::string formatTradingDay(int yyyymmdd) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  yyyymmdd / 10000, (yyyymmdd / 100) % 100, yyyymmdd % 100);
    return std::string(buf);
}

/// @brief 将 yyyymmdd 整数格式化到指定缓冲区（避免 std::string 分配）
inline void formatTradingDayTo(int yyyymmdd, char* buf, size_t bufSize) {
    std::snprintf(buf, bufSize, "%04d-%02d-%02d",
                  yyyymmdd / 10000, (yyyymmdd / 100) % 100, yyyymmdd % 100);
}

/// @brief 将 "yyyy-mm-dd" 或 "yyyy/mm/dd" 字符串解析为 yyyymmdd 整数
inline int parseTradingDay(const std::string& dateStr) {
    int y = 0, m = 0, d = 0;
    std::sscanf(dateStr.c_str(), "%d-%*c%d-%*c%d", &y, &m, &d);
    return y * 10000 + m * 100 + d;
}

} // namespace utils
} // namespace foundation

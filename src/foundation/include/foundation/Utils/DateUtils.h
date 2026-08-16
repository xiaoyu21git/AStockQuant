// DateUtils.h — 交易日日期格式化/解析统一工具
// 用于消除分散在各模块中的 yyyymmdd ↔ "yyyy-mm-dd" 手工转换
#pragma once

#include <cstddef>
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
    std::sscanf(dateStr.c_str(), "%d-%d-%d", &y, &m, &d);
    return y * 10000 + m * 100 + d;
}

/// @brief 将 "yyyy-mm-dd" 字符串 (const char* + 长度) 严格解析为 yyyymmdd 整数
/// 校验: 长度 10、第 4/7 位为 '-'、其余位为数字、月 1-12、日 1-31
/// @return 解析成功返回 yyyymmdd, 失败返回 -1
inline int parseIsoDateToInt(const char* str, size_t len) {
    if (str == nullptr || len != 10 || str[4] != '-' || str[7] != '-') return -1;
    for (size_t i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) continue;
        if (str[i] < '0' || str[i] > '9') return -1;
    }
    const int year = (str[0] - '0') * 1000 + (str[1] - '0') * 100
        + (str[2] - '0') * 10 + (str[3] - '0');
    const int month = (str[5] - '0') * 10 + (str[6] - '0');
    const int day = (str[8] - '0') * 10 + (str[9] - '0');
    constexpr int kMonthsPerYear = 12;
    constexpr int kMaxDayInMonth = 31;
    if (month < 1 || month > kMonthsPerYear || day < 1 || day > kMaxDayInMonth) return -1;
    return year * 10000 + month * 100 + day;
}

/// @brief 从 yyyymmdd 往前回滚 calendarDays 个日历日 (28天/月近似, 回测回看窗口估算用)
/// 仅用于回看窗口的查询下界估算, 精确交易日以 trade_calendar 为准
inline int backScrollCalendarDays(int yyyymmdd, int calendarDays) {
    int y, m, d;
    decomposeDate(yyyymmdd, y, m, d);
    while (calendarDays-- > 0) {
        if (--d < 1) { if (--m < 1) { m = 12; --y; } d = 28; }
    }
    return y * 10000 + m * 100 + d;
}

} // namespace utils
} // namespace foundation

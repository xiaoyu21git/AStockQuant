#pragma once
// ─────────────────────────────────────────────────────────────────────
// MarketDataUtils.h — 行情数据工具函数 (纯 C++, 零 Qt, 无状态)
// 替代 QML 中内联的 hasRealtimeQuote / boardLimitRatio / priceDigitsForMode
// ─────────────────────────────────────────────────────────────────────

#include <string>
#include <algorithm>

namespace domain::trading {

// ── 价格小数位 ──
inline int priceDigitsForMode(const std::string& mode) noexcept {
    if (mode == "futures") return 0;
    if (mode == "options") return 4;
    return 2;
}

// ── 板块涨跌停比例 ──
// A股: 主板10%, 创业板/科创板20%, 北交所30%
inline double boardLimitRatio(const std::string& symbol) noexcept {
    // 提取纯代码部分 (去掉 .SH/.SZ/.BJ 后缀)
    std::string code = symbol;
    auto dotPos = code.find('.');
    if (dotPos != std::string::npos) code = code.substr(0, dotPos);

    // 北交所: 8/4 开头 → 30%
    if (!code.empty() && (code[0] == '8' || code[0] == '4'))
        return 0.30;

    // 创业板/科创板: 300/301/688 开头 → 20%
    if (code.rfind("300", 0) == 0 || code.rfind("301", 0) == 0 || code.rfind("688", 0) == 0)
        return 0.20;

    // 主板 → 10%
    return 0.10;
}

// ── 行情来源判断 ──
inline bool hasRealtimeQuote(const std::string& source, const std::string& updatedAt) noexcept {
    return !source.empty() && source != "seed" && source != "watchlist"
        && source != "daily_snapshot" && !updatedAt.empty() && updatedAt != "--";
}

inline bool hasSnapshotQuote(const std::string& source, const std::string& updatedAt) noexcept {
    if (source == "seed" || source == "watchlist") return false;
    return !updatedAt.empty() && updatedAt != "--";
}

inline bool hasDisplayQuote(const std::string& source, const std::string& updatedAt) noexcept {
    if (!source.empty() && (source == "seed" || source == "watchlist" || source == "database_name"))
        return true;
    return hasRealtimeQuote(source, updatedAt) || hasSnapshotQuote(source, updatedAt);
}

// ── 无效代码消息 ──
inline const char* invalidSymbolMessageForMode(const std::string& mode) noexcept {
    if (mode == "futures") return "请输入有效期货合约代码";
    if (mode == "options") return "请输入有效期权合约代码";
    return "请输入有效6位股票代码";
}

} // namespace domain::trading

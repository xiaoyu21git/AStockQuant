#pragma once
// ─────────────────────────────────────────────────────────────────────
// PositionUtils.h — 持仓展示工具函数 (纯 C++, 零 Qt, 无状态)
// 供 Bridge 层调用, 替代 QML 中内联的 JS 字符串比较
// ─────────────────────────────────────────────────────────────────────

#include <string>
#include <cstdint>

namespace domain::trading {

// ── 数量标准化 ──
// 期货/期权: 支持小数手数; 股票/两融: 整数股数
inline std::int64_t normalizePositionQuantity(double rawQuantity, const std::string& type) noexcept {
    if (type == "futures" || type == "options") {
        double diff = std::abs(rawQuantity - std::round(rawQuantity));
        if (diff < 0.000001)
            return static_cast<std::int64_t>(std::round(rawQuantity));
        return static_cast<std::int64_t>(rawQuantity);
    }
    return static_cast<std::int64_t>(std::round(rawQuantity));
}

// ── 类型中文标签 ──
inline const char* positionTypeTitle(const std::string& type) noexcept {
    if (type == "margin_buy")  return "融资";
    if (type == "margin_sell") return "融券";
    if (type == "futures")     return "期货";
    if (type == "options")     return "期权";
    return "股票";
}

// ── 数量单位 ──
inline const char* positionUnit(const std::string& type) noexcept {
    if (type == "futures" || type == "options") return "手";
    return "股";
}

// ── 多空标签 ──
inline const char* positionSideLabel(const std::string& side) noexcept {
    if (side == "SHORT" || side == "Short" || side == "short") return "空头";
    return "多头";
}

// ── 可平/可卖标签 ──
inline const char* closeableLabel(const std::string& type, const std::string& side) noexcept {
    if (type == "futures" || type == "options" || side == "SHORT") return "可平";
    return "可卖";
}

} // namespace domain::trading

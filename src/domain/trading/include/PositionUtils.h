#pragma once
// ─────────────────────────────────────────────────────────────────────
// PositionUtils.h — 持仓展示工具函数 (纯 C++, 零 Qt, 无状态)
// 供 Bridge 层调用, 替代 QML 中内联的 JS 字符串比较
// ─────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <string>

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

// ── A股涨跌停板比例 ──
// 主板 10%, 创业板/科创板 20%, 北交所/新三板 30%
// symbol 应为纯代码 (如 "300001") 或完整代码 (如 "300001.SZ")，函数内部取 codeOnly
inline double boardLimitRatio(const std::string& symbol) noexcept {
    // 提取纯代码：取最后一个 '.' 之前的部分，或无点则原样
    std::string code = symbol;
    auto dotPos = code.rfind('.');
    if (dotPos != std::string::npos)
        code = code.substr(0, dotPos);
    // 创业板: 300/301开头; 科创板: 688开头
    if (code.rfind("300", 0) == 0 || code.rfind("301", 0) == 0 || code.rfind("688", 0) == 0)
        return 0.20;
    // 北交所: 8开头; 新三板: 4开头
    if (code.rfind("8", 0) == 0 || code.rfind("4", 0) == 0)
        return 0.30;
    return 0.10;  // 主板
}

} // namespace domain::trading

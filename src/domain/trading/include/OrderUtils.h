#pragma once
// ─────────────────────────────────────────────────────────────────────
// OrderUtils.h — 订单工具函数 (纯 C++, 零 Qt, 无状态)
// 替代 QML 中内联的 resolveLiveOrderType / resolveLiveOrderAction 等
// ─────────────────────────────────────────────────────────────────────

#include <string>

namespace domain::trading {

// ── 期货交易所判断 ──
inline bool isFuturesExchange(const std::string& exchange) noexcept {
    return exchange == "CFFEX" || exchange == "SHFE" || exchange == "DCE"
        || exchange == "CZCE" || exchange == "INE" || exchange == "GFEX";
}

// ── 订单品种解析 ──
// 从 rawType / optionType / underlying / exchange 推断 PositionType
inline const char* resolveLiveOrderType(
    const std::string& rawType,
    const std::string& optionType,
    const std::string& underlying,
    const std::string& exchange) noexcept {

    if (!rawType.empty()) {
        if (rawType == "futures")  return "futures";
        if (rawType == "options")  return "options";
        if (rawType == "margin_buy" || rawType == "marginBuy")   return "margin_buy";
        if (rawType == "margin_sell" || rawType == "marginSell") return "margin_sell";
        if (rawType == "stock")    return "stock";
    }
    if (!optionType.empty() || !underlying.empty()) return "options";
    if (isFuturesExchange(exchange)) return "futures";
    return "stock";
}

// ── 订单方向翻译 ──
inline const char* translateOrderSide(const std::string& side) noexcept {
    if (side == "BUY")  return "买入";
    if (side == "SELL") return "卖出";
    return side.empty() ? "待处理" : side.c_str();
}

// ── 订单动作解析 ──
// 根据品种/方向/positionEffect 解析为中文动作标签
inline const char* resolveLiveOrderAction(
    const std::string& rawType,
    const std::string& side,
    const std::string& positionEffect,
    const std::string& action) noexcept {

    // 如果有显式 action 且不在 futures/options 场景 → 直接用
    if (!action.empty()) {
        if (action == "buy")        return "买入";
        if (action == "sell")       return "卖出";
        if (action == "long")       return "开多";
        if (action == "short")      return "开空";
        if (action == "closeLong")  return "平多";
        if (action == "closeShort") return "平空";
        if (action == "marginBuy")  return "融资买入";
        if (action == "closeLong" && rawType == "margin_buy") return "卖券还款";
        if (action == "repay")      return "现金还款";
        if (action == "marginSell") return "融券卖出";
        if (action == "closeShort" && rawType == "margin_sell") return "买券还券";
        if (action == "returnStock") return "现券还券";
        if (action == "optionBuy")  return "买入开仓";
        if (action == "optionSell") return "卖出平仓";
        if (action == "optionClose") return "备兑开仓";
        if (action == "optionCoveredClose") return "备兑平仓";
        if (action == "optionExercise") return "行权";
    }

    // — 期货场景 —
    if (rawType == "futures") {
        if (side == "BUY"  && positionEffect == "OPEN")  return "开多";
        if (side == "SELL" && positionEffect == "OPEN")  return "开空";
        if (side == "SELL" && positionEffect == "CLOSE") return "平多";
        if (side == "BUY"  && positionEffect == "CLOSE") return "平空";
    }

    // — 期权场景 —
    if (rawType == "options") {
        if (side == "BUY"  && positionEffect == "OPEN")  return "买入开仓";
        if (side == "SELL" && positionEffect == "CLOSE") return "卖出平仓";
        if (side == "SELL" && positionEffect == "OPEN")  return "备兑开仓";
        if (side == "BUY"  && positionEffect == "CLOSE") return "买入平仓";
    }

    // — 默认: 用 side 翻译 —
    return translateOrderSide(side.empty() ? action : side);
}

// ── 布尔值安全转换 ──
inline bool boolishOrderValue(const std::string& value) noexcept {
    if (value == "1" || value == "true" || value == "yes"
        || value == "True" || value == "TRUE" || value == "YES")
        return true;
    return false;
}

} // namespace domain::trading

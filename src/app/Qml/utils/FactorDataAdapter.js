// FactorDataAdapter.js
// 因子数据映射适配器，将因子数据转换为统一的卡片数据格式

/**
 * 因子数据适配器
 * 提供因子数据到统一卡片数据格式的转换功能
 */

function normalizeCategoryKey(category) {
    var rawCategory = String(category || "").trim()
    var normalized = rawCategory.toLowerCase()

    switch (normalized) {
    case "value":
    case "价值类":
    case "价值因子":
        return "价值因子"
    case "momentum":
    case "动量类":
    case "动量因子":
        return "动量因子"
    case "size":
    case "规模类":
    case "规模因子":
        return "规模因子"
    case "quality":
    case "质量类":
    case "质量因子":
        return "质量因子"
    case "growth":
    case "成长类":
    case "成长因子":
        return "成长因子"
    case "low_volatility":
    case "低波类":
    case "低波因子":
    case "波动类":
        return "低波因子"
    case "dividend":
    case "红利因子":
        return "红利因子"
    case "technical":
    case "技术类":
    case "技术因子":
        return "技术因子"
    case "macro":
    case "宏观因子":
        return "宏观因子"
    case "industry":
    case "行业因子":
        return "行业因子"
    case "liquidity":
    case "流动性类":
    case "流动性因子":
        return "流动性因子"
    case "sentiment":
    case "情绪类":
    case "情绪因子":
        return "情绪因子"
    case "custom":
    case "自定义":
    case "自定义因子":
        return "自定义因子"
    case "reversal":
    case "反转类":
    case "反转因子":
        return "反转因子"
    case "high_freq":
    case "高频类":
    case "高频因子":
        return "高频因子"
    case "dl":
    case "ai":
    case "ai因子":
    case "AI因子":
        return "AI因子"
    case "supply_chain":
    case "传导链因子":
    case "传导链":
        return "传导链因子"
    default:
        return rawCategory || "动量因子"
    }
}

/**
 * 根据值获取趋势方向
 * @param {number} value - 值
 * @returns {string} 趋势方向: "up", "down", "neutral"
 */
function getTrendDirection(value) {
    if (value > 0.03) return "up";
    if (value < 0.02) return "down";
    return "neutral";
}
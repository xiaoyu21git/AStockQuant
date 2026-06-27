.pragma library
// TradeUtils.js — 交易组件纯工具函数
// 仅做数据转换和纯计算，不参与 UI 创建

function formatPrice(price, digits) {
    if (!price || price <= 0) return "--"
    digits = digits || 2
    return Number(price).toFixed(digits)
}

function formatAmount(amount) {
    if (!amount || amount === 0) return "0"
    var abs = Math.abs(amount)
    if (abs >= 1e8)  return (amount / 1e8).toFixed(2) + "亿"
    if (abs >= 1e4)  return (amount / 1e4).toFixed(2) + "万"
    return amount.toFixed(2)
}

function formatPercent(pct) {
    if (pct === undefined || pct === null) return "--"
    var v = Number(pct)
    var sign = v >= 0 ? "+" : ""
    return sign + v.toFixed(2) + "%"
}

function priceColor(changePct) {
    if (!changePct) return "#94a3b8"
    return changePct > 0 ? "#ef4444" : changePct < 0 ? "#10b981" : "#94a3b8"
}

function computeFee(price, qty, feeRate) {
    var rate = feeRate || { commission: 0.0003, stampTax: 0.001, minCommission: 5.0 }
    var notional = price * qty
    var commission = Math.max(notional * rate.commission, rate.minCommission || 5.0)
    var tax = notional * rate.stampTax
    return {
        notional: notional,
        commission: commission,
        tax: tax,
        total: notional + commission + tax
    }
}

function orderSideLabel(side) {
    if (side === "buy")  return "买入"
    if (side === "sell") return "卖出"
    return side
}

function orderStatusLabel(status) {
    var map = {
        "pending": "待报", "PENDING": "待报",
        "submitted": "已报", "SUBMITTED": "已报",
        "partial": "部成", "PARTIAL": "部成", "PARTIAL_FILLED": "部成", "PARTIALLY_FILLED": "部成",
        "filled": "全成", "FILLED": "全成",
        "cancelled": "已撤", "CANCELLED": "已撤",
        "rejected": "废单", "REJECTED": "废单",
        "expired": "过期", "EXPIRED": "过期"
    }
    return map[status] || status || "--"
}

function quickShareLabel(level) {
    if (level === "quarter") return "1/4仓"
    if (level === "half")    return "半仓"
    if (level === "full")    return "全仓"
    return level
}

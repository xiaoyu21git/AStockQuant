// OrderUtils.js — 订单工具函数（共享，单一数据源）
// 供 TradingPage、TradingFormPanel、OrderBook 等所有交易组件引用
// 用法: import "OrderUtils.js" as OU; OU.translateOrderStatus(...)
.pragma library

function translateOrderStatus(status) {
    var text = String(status || "").trim().toUpperCase()
    if (text === "REQUESTED")          return "已请求"
    if (text === "PENDING_RISK")       return "风控审批中"
    if (text === "SUBMITTED")          return "已报"
    if (text === "PENDING")            return "待处理"
    if (text === "PARTIALLY_FILLED")   return "部分成交"
    if (text === "PARTIAL_FILLED")      return "部分成交"
    if (text === "FILLED")             return "已成"
    if (text === "PENDING_CANCEL")     return "撤单中"
    if (text === "CANCELLED")          return "已撤"
    if (text === "REJECTED")           return "已拒"
    if (text === "EXPIRED")            return "已过期"
    return text || "待处理"
}

function orderUnit(order) {
    if (!order) return "股"
    var t = String(order.type || "").toLowerCase()
    return (t === "futures" || t === "options") ? "手" : "股"
}

function normalizedOrderStatus(statusValue) {
    if (typeof statusValue === "string") {
        var v = statusValue.trim().toUpperCase()
        if (v === "FILLED" || v === "3" || v === "PARTIAL_FILLED" || v === "PARTIALLY_FILLED") return "FILLED"
        if (v === "CANCELLED" || v === "5") return "CANCELLED"
        if (v === "REJECTED" || v === "8") return "REJECTED"
        if (v === "SUBMITTED" || v === "1") return "SUBMITTED"
        if (v === "PENDING" || v === "0") return "PENDING"
        return v
    }
    if (typeof statusValue === "number") {
        if (statusValue === 3) return "FILLED"
        if (statusValue === 5) return "CANCELLED"
        if (statusValue === 8) return "REJECTED"
        if (statusValue === 1) return "SUBMITTED"
        return String(statusValue)
    }
    return "UNKNOWN"
}

function displayOrderSide(side) {
    var sideText = String(side || "").toUpperCase()
    if (sideText === "BUY") {
        return "买入"
    }
    if (sideText === "SELL") {
        return "卖出"
    }
    return sideText || "--"
}

function isUnfinishedOrderStatus(status) {
    var statusText = normalizedOrderStatus(status)
    return statusText === "SUBMITTED" || statusText === "PENDING" || statusText === "PARTIAL_FILLED"
}

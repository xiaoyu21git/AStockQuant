.pragma library
// TradingWidgetBase.js — 交易控件通用动态布局适配逻辑
// 所有新交易控件导入此 library, 按需调用其函数
//
// 提供:
//   1. computeScaleFactor — 带下限保护的 scaleFactor 计算
//   2. computeDensityMode  — 带滞后阈值的三级密度模式
//   3. scaleValue          — 统一缩放函数
//   4. applyColumnPriority — 列优先级裁剪
//   5. scheduleUpdate      — 帧合并调度
//   6. throttle            — 高频数据节流
//   7. computeOptimalHeight— optimalHeight 计算辅助
//   8. fmtAmount           — 金额格式化 (亿/万)
//   9. fmtPnlColor         — 盈亏颜色
//  10. statusColor         — 订单状态颜色
//  11. statusBadge         — 订单状态中文标签

// ============================================================================
// 1. scaleFactor — 带下限保护
//    height: 当前控件高度 (px)
//    optimalHeight: 控件最佳显示高度 (px)
//    返回值: 0.6 ~ 1.2, 确保最小可读
// ============================================================================
function computeScaleFactor(height, optimalHeight) {
    if (height <= 0 || optimalHeight <= 0) return 1.0
    return Math.min(1.2, Math.max(0.6, height / optimalHeight))
}

// ============================================================================
// 2. 密度模式 — 带滞后阈值, 避免临界高度反复切换
//    height: 当前控件高度 (px)
//    optimalHeight: 控件最佳显示高度 (px)
//    currentMode: 当前模式 ("compact" | "normal" | "expanded")
//    返回值: 新模式字符串
//
//    阈值设计:
//      compactIn  = optimalHeight * 0.55  (进入 compact)
//      compactOut = optimalHeight * 0.65  (退出 compact)
//      expandedIn  = optimalHeight * 1.15 (进入 expanded)
//      expandedOut = optimalHeight * 1.05 (退出 expanded)
//    滞后区间 ≈ optimalHeight 的 10%, 确保模式切换稳定
// ============================================================================
function computeDensityMode(height, optimalHeight, currentMode) {
    if (height <= 0 || optimalHeight <= 0) return "normal"

    var compactIn   = optimalHeight * 0.55
    var compactOut  = optimalHeight * 0.65
    var expandedIn  = optimalHeight * 1.15
    var expandedOut = optimalHeight * 1.05

    switch (currentMode) {
        case "compact":
            return height >= compactOut ? "normal" : "compact"
        case "normal":
            if (height <= compactIn) return "compact"
            if (height >= expandedIn) return "expanded"
            return "normal"
        case "expanded":
            return height <= expandedOut ? "normal" : "expanded"
        default:
            // 首次计算, 无滞后
            if (height <= compactIn) return "compact"
            if (height >= expandedIn) return "expanded"
            return "normal"
    }
}

// ============================================================================
// 3. 缩放函数 — 保证最小不低于原始值的 60%
//    baseValue: 基准尺寸 (px)
//    scaleFactor: 当前缩放系数
//    返回值: 缩放后的整数像素值
// ============================================================================
function scaleValue(baseValue, scaleFactor) {
    return Math.max(Math.round(baseValue * 0.6), Math.round(baseValue * scaleFactor))
}

// ============================================================================
// 4. 列优先级裁剪
//    columns: [{ baseWidth, priority, visible }] — 会被原地修改
//    availableWidth: 可用总宽度 (px)
//    scaleFactor: 当前缩放系数
//
//    逻辑: 按 priority 升序排列, 低优先级列先被裁剪
//    优先级含义: 1=必须显示, 2=高优先, 3=中优先, 4=低优先, 5=仅Expanded
// ============================================================================
function applyColumnPriority(columns, availableWidth, scaleFactor) {
    if (!columns || columns.length === 0) return

    // 复制并按 priority 升序排列
    var sorted = columns.slice().sort(function(a, b) {
        return (a.priority || 5) - (b.priority || 5)
    })

    var fixedOverhead = 16  // 边距开销
    var availW = Math.max(0, availableWidth - fixedOverhead)
    // 先全部设为不可见, 再按优先级逐一开启
    for (var i = 0; i < columns.length; i++) {
        columns[i].visible = false
    }

    var usedW = 0
    for (i = 0; i < sorted.length; i++) {
        var needW = usedW + (sorted[i].baseWidth || 40) * scaleFactor
        if (needW <= availW) {
            sorted[i].visible = true
            usedW = needW
        }
    }
}

// ============================================================================
// 5. 帧合并调度 — 同一帧内多次调用只执行最后一次
//    使用全局 pending 表, 按 key 去重
//    key: 唯一标识 (如 "depth_refresh")
//    callback: 延迟执行的函数
// ============================================================================
var _pendingUpdates = {}

function scheduleUpdate(key, callback) {
    if (!_pendingUpdates[key]) {
        _pendingUpdates[key] = true
        Qt.callLater(function() {
            _pendingUpdates[key] = false
            if (typeof callback === "function") {
                callback()
            }
        })
    }
}

// ============================================================================
// 6. 节流 — 限制高频回调
//    key: 唯一标识
//    intervalMs: 最小间隔 (毫秒)
//    callback: 满足间隔时执行的函数
// ============================================================================
var _throttleTimers = {}

function throttle(key, intervalMs, callback) {
    var now = Date.now()
    var last = _throttleTimers[key] || 0
    if (now - last >= intervalMs) {
        _throttleTimers[key] = now
        if (typeof callback === "function") {
            callback()
        }
    }
}

// ============================================================================
// 7. optimalHeight 计算辅助
//    公式: fullHeaderH + visibleRows * rowH + padding
//    fullHeaderH: 标题栏 + 表头高度
//    visibleRows: Normal 模式默认可见行数
//    rowH: 每行高度
//    padding: 上下内边距
// ============================================================================
function computeOptimalHeight(fullHeaderH, visibleRows, rowH, padding) {
    return fullHeaderH + visibleRows * rowH + padding
}

// ============================================================================
// 8. 金额格式化
//    v: 数值 (或 null/undefined)
//    >= 1亿   → "X.XX亿"
//    >= 1万   → "X.XX万"
//    否则      → toFixed(2)
// ============================================================================
function fmtAmount(v) {
    if (v === undefined || v === null) return "--"
    var n = Number(v)
    if (isNaN(n)) return "--"
    if (Math.abs(n) >= 1e8) return (n / 1e8).toFixed(2) + "亿"
    if (Math.abs(n) >= 1e4) return (n / 1e4).toFixed(2) + "万"
    return n.toFixed(2)
}

// ============================================================================
// 9. 盈亏颜色
//    v: 数值
//    >= 0 → "#EF4444" (红色, A股涨)
//    < 0  → "#10B981" (绿色, A股跌)
// ============================================================================
function fmtPnlColor(v) {
    return (Number(v) || 0) >= 0 ? "#EF4444" : "#10B981"
}

// ============================================================================
// 10. 订单状态颜色
//     FILLED → 绿, REJECTED/CANCELLED → 红/灰, PARTIAL_FILLED → 黄, 其余 → 白
// ============================================================================
function orderStatusColor(st) {
    var t = String(st || "")
    if (t === "FILLED") return "#10B981"
    if (t === "REJECTED") return "#EF4444"
    if (t === "CANCELLED" || t === "EXPIRED") return "#94A3B8"
    if (t === "PARTIAL_FILLED") return "#F59E0B"
    return "#F1F5F9"
}

// ============================================================================
// 11. 订单状态中文标签
// ============================================================================
function orderStatusBadge(st) {
    var t = String(st || "")
    switch (t) {
        case "SUBMITTED":      return "已报"
        case "PARTIAL_FILLED": return "部成"
        case "FILLED":         return "已成"
        case "CANCELLED":      return "已撤"
        case "REJECTED":       return "拒单"
        case "PENDING":        return "待报"
        case "EXPIRED":        return "过期"
        case "PENDING_CANCEL": return "撤单中"
        default:               return t || "--"
    }
}

// ============================================================================
// 12. 买卖方向颜色
//     BUY  → "#EF4444" 红,  SELL → "#10B981" 绿
// ============================================================================
function sideColor(sd) {
    return String(sd || "") === "SELL" ? "#10B981" : "#EF4444"
}

// ============================================================================
// 13. 买卖方向中文标签
// ============================================================================
function sideLabel(sd) {
    return String(sd || "") === "SELL" ? "卖" : "买"
}

// ============================================================================
// 14. 执行日志严重程度颜色
// ============================================================================
function severityColor(level) {
    switch (String(level || "")) {
        case "error":   return "#EF4444"
        case "success": return "#10B981"
        case "warning": return "#F59E0B"
        default:        return "#3B82F6"  // info / blue
    }
}

// ============================================================================
// 15. 执行日志类型徽章文本
// ============================================================================
function logBadgeText(kind) {
    switch (String(kind || "")) {
        case "request":  return "提交"
        case "rule":     return "规则"
        case "status":   return "状态"
        case "fill":     return "成交"
        case "runtime":  return "策略"
        case "position": return "仓位"
        default:         return "系统"
    }
}

// ============================================================================
// 16. 数值量格式化 (量/手)
//    用于持仓数量和成交量
// ============================================================================
function fmtVolume(v) {
    var n = Number(v) || 0
    if (n >= 1e8) return (n / 1e8).toFixed(1) + "亿"
    if (n >= 1e4) return (n / 1e4).toFixed(0) + "万"
    return String(Math.round(n))
}

// ============================================================================
// 17. 持仓类型中文标题
// ============================================================================
function positionTypeTitle(type) {
    switch (String(type || "")) {
        case "stock":       return "股票持仓"
        case "margin_buy":  return "融资持仓"
        case "margin_sell": return "融券持仓"
        case "futures":     return "期货持仓"
        case "options":     return "期权持仓"
        default:            return "其他持仓"
    }
}

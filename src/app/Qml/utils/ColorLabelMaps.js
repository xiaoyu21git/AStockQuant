.pragma library

function tradingPreviewStatusLabel(status) {
    switch (String(status || "").trim().toLowerCase()) {
    case "pass":
        return "通过"
    case "warn":
        return "预警"
    case "blocked":
        return "阻断"
    case "force_reduce":
        return "强制减仓"
    case "trading_halt":
        return "停牌"
    case "no_order_plan":
        return "无委托计划"
    case "invalid_backtest_result":
        return "结果无效"
    case "missing_factor_snapshot":
        return "缺少因子截面"
    case "invalid_batch":
        return "交易批次无效"
    case "invalid_context":
        return "执行上下文无效"
    default:
        return "未生成"
    }
}

function tradingPreviewAccentColor(status) {
    switch (String(status || "").trim().toLowerCase()) {
    case "pass":
        return "#10B981"
    case "warn":
    case "no_order_plan":
        return "#F59E0B"
    case "blocked":
    case "force_reduce":
    case "trading_halt":
    case "invalid_backtest_result":
    case "missing_factor_snapshot":
    case "invalid_batch":
    case "invalid_context":
        return "#EF4444"
    default:
        return "#64748B"
    }
}

function formalTradingStatusLabel(status) {
    switch (String(status || "").trim().toUpperCase()) {
    case "SUCCESS":
        return "已完成"
    case "PARTIAL":
        return "部分完成"
    case "FAILED":
        return "执行失败"
    case "NOT_RUN":
        return "未执行"
    default:
        return "未生成"
    }
}

function formalTradingAccentColor(status) {
    switch (String(status || "").trim().toUpperCase()) {
    case "SUCCESS":
        return "#10B981"
    case "PARTIAL":
        return "#F59E0B"
    case "FAILED":
        return "#EF4444"
    case "NOT_RUN":
        return "#64748B"
    default:
        return "#64748B"
    }
}

function coreRatingLabel(value, fallbackLabel) {
    var resolvedLabel = formatTextMetric(fallbackLabel, "")
    if (resolvedLabel.length > 0) {
        return resolvedLabel
    }

    var numeric = Number(value)
    if (!isFinite(numeric)) {
        numeric = 0
    }

    switch (numeric) {
    case 3:
        return "优秀"
    case 2:
        return "良好"
    case 1:
        return "合格"
    default:
        return "不合格"
    }
}

function coreRatingColor(value) {
    var numeric = Number(value)
    if (!isFinite(numeric)) {
        numeric = 0
    }

    switch (numeric) {
    case 3:
        return "#10B981"
    case 2:
        return "#38BDF8"
    case 1:
        return "#F59E0B"
    default:
        return "#EF4444"
    }
}

function returnMetricColor(value) {
    var numericValue = hasNumericMetricValue(value) ? Number(value) : 0
    if (numericValue > 0) {
        return "#EF4444"
    }
    if (numericValue < 0) {
        return "#10B981"
    }
    return "#94A3B8"
}

function returnMetricTrend(value) {
    var numericValue = hasNumericMetricValue(value) ? Number(value) : 0
    if (numericValue > 0) {
        return "up"
    }
    if (numericValue < 0) {
        return "down"
    }
    return "neutral"
}

function returnTrendColor(trend) {
    return trend === "up" ? "#EF4444" : (trend === "down" ? "#10B981" : "#94A3B8")
}

function backtestResultStatusLabel(status) {
    switch (String(status || "").trim().toUpperCase()) {
    case "SUCCESS":
        return "成功"
    case "PARTIAL":
        return "部分完成"
    case "FAILED":
        return "失败"
    case "RUNNING":
        return "运行中"
    default:
        return "待查看"
    }
}

function backtestResultStatusColor(status) {
    switch (String(status || "").trim().toUpperCase()) {
    case "SUCCESS":
        return "#10B981"
    case "PARTIAL":
        return "#F59E0B"
    case "FAILED":
        return "#EF4444"
    case "RUNNING":
        return "#3B82F6"
    default:
        return "#64748B"
    }
}

function badgeBackground(type) {
    if (type === "warning") {
        return "#3A2A10"
    }
    if (type === "danger") {
        return "#3B1215"
    }
    return "#0F2F22"
}

function badgeTextColor(type) {
    if (type === "warning") {
        return "#FDBA74"
    }
    if (type === "danger") {
        return "#FCA5A5"
    }
    return "#6EE7B7"
}

function roleDisplayName(role) {
    var mapping = {
        must_pass: "必须满足",
        any_pass: "任一满足",
        veto: "否决条件",
        score_boost: "评分增强",
        position_management: "仓位管理",
        execution_constraint: "执行限制",
        account_guard: "账户保护"
    }
    return mapping[role] || role || "规则组"
}

function operatorDisplayName(operatorValue) {
    var mapping = {
        all: "全部满足",
        any: "任一满足",
        at_least: "至少命中",
        score_sum: "累计评分",
        first_match: "首个命中"
    }
    return mapping[operatorValue] || operatorValue || "未设置"
}

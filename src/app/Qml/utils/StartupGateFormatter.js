.pragma library

function hasStartupGate(startupGate) {
    return !!startupGate && Object.keys(startupGate).length > 0
}

function summaryText(startupGate, emptyText) {
    if (!hasStartupGate(startupGate)) {
        return String(emptyText || "尚未获取 StartupGate 状态")
    }

    if (startupGate.ready) {
        return "当前已保存配置已通过 StartupGate，可进入 broker readiness 的下一层校验"
    }

    return String(startupGate.reason || startupGate.message || "当前配置未通过 StartupGate")
}

function metaText(startupGate) {
    if (!hasStartupGate(startupGate)) {
        return ""
    }

    var parts = []
    if (startupGate.ruleId) {
        parts.push("规则: " + startupGate.ruleId)
    }
    if (startupGate.reasonCode) {
        parts.push("原因码: " + startupGate.reasonCode)
    }
    if (startupGate.checkedAt) {
        parts.push("检查时间: " + startupGate.checkedAt)
    }
    return parts.join(" | ")
}

function checkSummaryText(startupGate) {
    if (!hasStartupGate(startupGate) || !startupGate.checks) {
        return ""
    }

    var checks = startupGate.checks || ({})
    var items = []

    function pushCheck(label, key) {
        if (checks[key] === undefined) {
            return
        }
        items.push(label + ": " + (checks[key] ? "是" : "否"))
    }

    pushCheck("连接已启用", "enabled")
    pushCheck("只读模式", "readOnly")
    pushCheck("显式解锁", "liveUnlockConfirmed")
    pushCheck("Token 已配置", "tokenPresent")
    pushCheck("账户已绑定", "accountBound")
    pushCheck("业务策略已绑定", "boundStrategyPresent")
    pushCheck("运行时策略已绑定", "runtimeStrategyPresent")
    if (startupGate.requireClientProcess) {
        pushCheck("客户端进程已运行", "clientProcessRunning")
    }

    return items.join("\n")
}

function compactHintText(startupGate) {
    if (!hasStartupGate(startupGate) || startupGate.ready) {
        return ""
    }

    switch (String(startupGate.reasonCode || "")) {
    case "explicit_live_unlock_required":
        return "StartupGate: 未显式解锁实盘"
    case "token_missing":
        return "StartupGate: Token 未配置"
    case "account_missing":
        return "StartupGate: 交易账户未配置"
    case "client_process_missing":
        return "StartupGate: 掘金客户端未运行"
    default:
        var summary = summaryText(startupGate, "")
        return summary ? ("StartupGate: " + summary) : ""
    }
}

function blockedActionMessage(startupGate, fallbackText) {
    var lines = []
    var fallback = String(fallbackText || "当前操作未通过 StartupGate")
    var summary = summaryText(startupGate, "")
    var meta = metaText(startupGate)
    var checks = checkSummaryText(startupGate)

    lines.push(summary ? summary : fallback)
    if (meta) {
        lines.push(meta)
    }
    if (checks) {
        lines.push(checks)
    }

    return lines.join("\n")
}
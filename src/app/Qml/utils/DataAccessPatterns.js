.pragma library

function getStrategyParameters(strategy) {
    if (!strategy) {
        return ({})
    }
    return strategy.parameters || ({})
}

function getStrategyPerformance(strategy) {
    if (!strategy) {
        return ({})
    }
    return strategy.performanceMetrics || ({})
}

function getLatestBacktest(strategy) {
    var performance = getStrategyPerformance(strategy)
    return performance.latestBacktest || ({})
}

function getBacktestHistory(strategy) {
    var performance = getStrategyPerformance(strategy)
    return performance.backtestHistory || []
}

function getBacktestTradeRecords(backtestRecord) {
    if (!backtestRecord || typeof backtestRecord !== "object") {
        return []
    }
    if (backtestRecord.tradeRecords instanceof Array) {
        return backtestRecord.tradeRecords
    }
    return []
}

function resolveStrategyNameFromBacktest(strategy) {
    if (!strategy) {
        return "未命名策略"
    }
    return strategy.strategyName || "未命名策略"
}

function resolveStrategyId(strategy) {
    if (!strategy) {
        return ""
    }
    return strategy.strategyId || ""
}

function hasBacktestRecord(strategy) {
    var latest = getLatestBacktest(strategy)
    return latest && Object.keys(latest).length > 0
}

function getStrategyDisplayStatus(strategy) {
    // 由 C++ StrategyBridge::get/list 中的 computeDisplayStatus 计算，查询实际引擎状态
    return (strategy && strategy.displayStatus) ? strategy.displayStatus : "已停止"
}

function isRunningStrategy(strategy) {
    return (strategy && strategy.displayStatus) === "运行中"
}

function hasRuntimeSnapshotData(snapshot) {
    return snapshot && Object.keys(snapshot).length > 0
}

function resolveStrategyIdentifier(strategyCandidate) {
    if (!strategyCandidate) {
        return ""
    }

    return strategyCandidate.strategyId || ""
}

function resolveStrategyName(strategyCandidate, strategyId) {
    if (!strategyCandidate) {
        return strategyId || ""
    }

    return strategyCandidate.strategyName || strategyCandidate.name || strategyId || ""
}

function isStrategyBoundToTradingConfiguration(strategy, configuration) {
    var strategyId = strategy ? (strategy.strategyId || "") : ""
    if (!strategyId) return false
    var config = configuration || ({})
    var boundStrategies = config.boundStrategies || []
    for (var i = 0; i < boundStrategies.length; ++i) {
        var entry = boundStrategies[i] || ({})
        var bid = typeof entry === "string" ? String(entry).trim() : String(entry.strategyId || "").trim()
        if (bid === strategyId) return true
    }
    return String(config.boundStrategyId || "").trim() === strategyId
}

function getStrategySubscriptionSyncLabel(strategy, configuration) {
    var config = configuration || ({})
    var strategyId = strategy ? (strategy.strategyId || "") : ""
    if (!strategyId || !isStrategyBoundToTradingConfiguration(strategy, config)) {
        return "--"
    }

    var configSymbols = getConfigurationSymbols(config)
    if (configSymbols.length === 0) {
        return "全市场"   // SDK 自动订阅全市场，无需手动配置 symbols
    }

    return "已配置"
}

function getConfigurationSymbols(configuration) {
    var config = configuration || ({})
    var source = config.symbols || []
    var values = Array.isArray(source) ? source : String(source || "").split(/[,;\s，；]+/)
    var normalized = []
    for (var index = 0; index < values.length; ++index) {
        var token = String(values[index] || "").trim().toUpperCase()
        if (!token || normalized.indexOf(token) !== -1) {
            continue
        }
        normalized.push(token)
    }
    return normalized
}

function getStrategyStartGateState(strategyCandidate) {
    return {
        canStart: true
    }
}

function getStrategyStartActionLabel(strategyCandidate) {
    return "启动实盘"
}

function getMarketCalendarSourceTag(snapshot) {
    if (!snapshot || Object.keys(snapshot).length === 0) {
        return "本地时间窗"
    }

    if (snapshot.holidayAware) {
        return "真实日历"
    }

    return snapshot.error ? "日历降级" : "本地回退"
}

function getMarketCalendarPhaseLabel(snapshot) {
    return normalizeRuntimeDisplayValue(snapshot && snapshot.sessionPhaseLabel, "--")
}

function getStrategyDisplayStatusLabel(status) {
    // displayStatus 已经是中文，直接返回
    return status || "已停止"
}

function strategyHasEditableRulePayload(strategyObject) {
    var strategyData = toPlainJsValue(strategyObject) || ({})
    var parameters = toPlainJsValue(strategyData.parameters) || ({})
    return !!(parameters.rule_profile
              || parameters.rule_composer_state
              || parameters.factor_overlay)
}

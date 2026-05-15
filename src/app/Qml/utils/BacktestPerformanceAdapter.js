// BacktestPerformanceAdapter.js
// 统一策略回测结果回写策略库时的绩效载荷结构

function clonePlainObject(value) {
    return JSON.parse(JSON.stringify(value || {}))
}

function hasValue(value) {
    return value !== undefined && value !== null && value !== ""
}

function asNumber(value, fallback) {
    var parsed = Number(value)
    return Number.isFinite(parsed) ? parsed : fallback
}

function normalizeTradeRecord(record) {
    if (!record || typeof record !== "object") {
        return null
    }
    return {
        tradeId: record.tradeId || "",
        entryTime: record.entryTime || "",
        exitTime: record.exitTime || "",
        symbol: record.symbol || "",
        direction: record.direction || "",
        entryPrice: asNumber(record.entryPrice, 0),
        exitPrice: asNumber(record.exitPrice, 0),
        quantity: asNumber(record.quantity, 0),
        commission: asNumber(record.commission, 0),
        profit: asNumber(record.profit, 0),
        profitPct: asNumber(record.profitPct, 0),
        notes: record.notes || ""
    }
}

function collectTradeRecords(result) {
    if (!result || typeof result !== "object") {
        return []
    }
    var tradeRecords = Array.isArray(result.tradeRecords)
        ? result.tradeRecords
        : []
    return tradeRecords
        .map(normalizeTradeRecord)
        .filter(function(record) { return !!record && !!record.symbol })
}

function normalizeRuleTemplateEvent(event) {
    if (!event || typeof event !== "object") {
        return null
    }
    return {
        timestamp: event.timestamp || "",
        symbol: event.symbol || "",
        action: event.action || "",
        eventType: event.eventType || event.event_type || "",
        ruleId: event.ruleId || event.rule_id || "",
        reasonCode: event.reasonCode || event.reason_code || "",
        message: event.message || "",
        resultType: event.resultType || event.result_type || "",
        groupId: event.groupId || event.group_id || "",
        groupTitle: event.groupTitle || event.group_title || "",
        groupRole: event.groupRole || event.group_role || "",
        groupOperator: event.groupOperator || event.group_operator || ""
    }
}

function normalizeRuleTemplateGroupDecision(decision) {
    if (!decision || typeof decision !== "object") {
        return null
    }
    return {
        stage: decision.stage || "",
        groupId: decision.groupId || decision.group_id || "",
        groupTitle: decision.groupTitle || decision.group_title || "",
        groupRole: decision.groupRole || decision.group_role || "",
        groupOperator: decision.groupOperator || decision.group_operator || "",
        disposition: decision.disposition || "",
        outcome: decision.outcome || "",
        skipReason: decision.skipReason || decision.skip_reason || "",
        matchedRuleId: decision.matchedRuleId || decision.matched_rule_id || "",
        matchedResultType: decision.matchedResultType || decision.matched_result_type || "",
        matchedReasonCode: decision.matchedReasonCode || decision.matched_reason_code || "",
        memberCount: asNumber(decision.memberCount !== undefined ? decision.memberCount : decision.member_count, 0),
        applicableCount: asNumber(decision.applicableCount !== undefined ? decision.applicableCount : decision.applicable_count, 0),
        matchedCount: asNumber(decision.matchedCount !== undefined ? decision.matchedCount : decision.matched_count, 0),
        filteredCount: asNumber(decision.filteredCount !== undefined ? decision.filteredCount : decision.filtered_count, 0)
    }
}

function normalizeRuleTemplateSummary(result) {
    if (!result || typeof result !== "object") {
        return null
    }

    var summary = result.ruleTemplateSummary || result.rule_template_summary
    if (!summary || typeof summary !== "object") {
        return null
    }

    var recentEvents = Array.isArray(summary.recentEvents)
        ? summary.recentEvents
        : (Array.isArray(summary.recent_events) ? summary.recent_events : [])
    var latestGroupDecisions = Array.isArray(summary.latestGroupDecisions)
        ? summary.latestGroupDecisions
        : (Array.isArray(summary.latest_group_decisions) ? summary.latest_group_decisions : [])
    var normalized = {
        hasTemplate: !!(summary.hasTemplate !== undefined ? summary.hasTemplate : summary.has_template),
        templateFilePath: summary.templateFilePath || summary.template_file_path || "",
        templateFileName: summary.templateFileName || summary.template_file_name || "",
        templateNamespace: summary.templateNamespace || summary.template_namespace || "",
        groupId: summary.groupId || summary.group_id || "",
        groupTitle: summary.groupTitle || summary.group_title || "",
        groupRole: summary.groupRole || summary.group_role || "",
        groupOperator: summary.groupOperator || summary.group_operator || "",
        triggeredCount: asNumber(summary.triggeredCount !== undefined ? summary.triggeredCount : summary.triggered_count, 0),
        entryBlockCount: asNumber(summary.entryBlockCount !== undefined ? summary.entryBlockCount : summary.entry_block_count, 0),
        forcedExitCount: asNumber(summary.forcedExitCount !== undefined ? summary.forcedExitCount : summary.forced_exit_count, 0),
        latestGroupDecisions: latestGroupDecisions
            .map(normalizeRuleTemplateGroupDecision)
            .filter(function(decision) { return !!decision }),
        recentEvents: recentEvents.map(normalizeRuleTemplateEvent).filter(function(event) { return !!event })
    }

    if (!normalized.hasTemplate
            && normalized.triggeredCount <= 0
            && normalized.entryBlockCount <= 0
            && normalized.forcedExitCount <= 0
            && normalized.recentEvents.length === 0) {
        return null
    }

    return normalized
}

function buildBacktestHistoryEntry(result, performancePayload, context) {
    var timeSeries = result.timeSeries || {}
    var dates = timeSeries.dates || []
    var portfolioValues = timeSeries.portfolioValues || []
    var recordedAt = Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm:ss")
    var tradeRecords = collectTradeRecords(result)
    var appliedSymbolPool = Array.isArray(context.appliedSymbolPool) ? context.appliedSymbolPool.slice() : []
    var ruleTemplateSummary = normalizeRuleTemplateSummary(result)

    return {
        recordedAt: recordedAt,
        strategyId: context.selectedStrategyId || "",
        strategyName: context.selectedStrategyName || "",
        universeType: context.selectedUniverseType || "market",
        universeLabel: context.universeLabel || context.selectedUniverseType || "全市场",
        indexSymbol: context.selectedUniverseType === "index" ? (context.selectedIndexSymbol || "") : "",
        indexLabel: context.selectedUniverseType === "index" ? (context.indexLabel || "") : "",
        dataSourceMode: context.dataSourceMode || "raw",
        startDate: context.startDate || "",
        endDate: context.endDate || "",
        backtest_symbol_pool: appliedSymbolPool,
        backtestSymbolPool: appliedSymbolPool,
        symbol_pool: appliedSymbolPool,
        symbolPool: appliedSymbolPool,
        selectedSymbols: appliedSymbolPool,
        universeSourceKey: context.universeSourceKey || "",
        universeSourceLabel: context.universeSourceLabel || "",
        tradingDays: dates.length,
        equityPointCount: portfolioValues.length,
        runtimeParameters: clonePlainObject(context.runtimeParameters),
        summary: {
            returns: performancePayload.returns,
            maxDrawdown: performancePayload.maxDrawdown,
            sharpeRatio: performancePayload.sharpeRatio,
            winRate: performancePayload.winRate,
            runningDays: performancePayload.runningDays,
            tradesCount: performancePayload.tradesCount,
            totalReturn: performancePayload.totalReturn,
            annualReturn: performancePayload.annualReturn,
            annualizedReturn: performancePayload.annualizedReturn,
            volatility: performancePayload.volatility,
            sortinoRatio: performancePayload.sortinoRatio,
            calmarRatio: performancePayload.calmarRatio,
            profitFactor: performancePayload.profitFactor
        },
        tradeRecords: tradeRecords,
        ruleTemplateSummary: ruleTemplateSummary
    }
}

function buildStrategyPerformancePayload(result, context) {
    var safeResult = result || {}
    var performance = safeResult.performance || {}
    var trades = safeResult.trades || {}
    var dates = safeResult.timeSeries && safeResult.timeSeries.dates ? safeResult.timeSeries.dates : []
    var totalReturnPercent = hasValue(safeResult.totalReturn) ? Number(safeResult.totalReturn) * 100 : 0
    var maxDrawdownPercent = hasValue(safeResult.maxDrawdown) ? Number(safeResult.maxDrawdown) * 100 : 0
    var winRatePercent = hasValue(safeResult.winRate) ? Number(safeResult.winRate) * 100 : 0

    var performancePayload = {
        returns: totalReturnPercent.toFixed(2),
        maxDrawdown: maxDrawdownPercent.toFixed(2),
        sharpeRatio: hasValue(safeResult.sharpeRatio) ? Number(safeResult.sharpeRatio).toFixed(2) : "0.00",
        winRate: winRatePercent.toFixed(2),
        runningDays: dates.length,
        tradesCount: trades.totalTrades || safeResult.totalTrades || 0,
        position: 0,
        dailyPnL: 0,
        totalReturn: hasValue(safeResult.totalReturn) ? Number(safeResult.totalReturn) : 0,
        annualReturn: hasValue(safeResult.annualReturn) ? Number(safeResult.annualReturn) : 0,
        annualizedReturn: hasValue(performance.annualizedReturn) ? Number(performance.annualizedReturn) : (hasValue(safeResult.annualReturn) ? Number(safeResult.annualReturn) : 0),
        volatility: hasValue(performance.volatility) ? Number(performance.volatility) : 0,
        sortinoRatio: hasValue(performance.sortinoRatio) ? Number(performance.sortinoRatio) : 0,
        calmarRatio: hasValue(performance.calmarRatio) ? Number(performance.calmarRatio) : 0,
        profitFactor: hasValue(performance.profitFactor) ? Number(performance.profitFactor) : (hasValue(safeResult.profitLossRatio) ? Number(safeResult.profitLossRatio) : 0),
        averageWin: hasValue(performance.averageWin) ? Number(performance.averageWin) : 0,
        averageLoss: hasValue(performance.averageLoss) ? Number(performance.averageLoss) : 0,
        alpha: hasValue(performance.alpha) ? Number(performance.alpha) : 0,
        beta: hasValue(performance.beta) ? Number(performance.beta) : 0,
        informationRatio: hasValue(performance.informationRatio) ? Number(performance.informationRatio) : 0,
        trackingError: hasValue(performance.trackingError) ? Number(performance.trackingError) : 0,
        totalTrades: trades.totalTrades || safeResult.totalTrades || 0,
        winningTrades: trades.winningTrades || safeResult.winningTrades || 0,
        losingTrades: trades.losingTrades || safeResult.losingTrades || 0,
        totalProfit: hasValue(trades.totalProfit) ? Number(trades.totalProfit) : 0,
        totalLoss: hasValue(trades.totalLoss) ? Number(trades.totalLoss) : 0,
        largestWin: hasValue(trades.largestWin) ? Number(trades.largestWin) : 0,
        largestLoss: hasValue(trades.largestLoss) ? Number(trades.largestLoss) : 0,
        averageHoldingPeriod: hasValue(trades.averageHoldingPeriod) ? Number(trades.averageHoldingPeriod) : 0,
        tradingDays: dates.length,
        lastBacktestAt: Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm:ss")
    }

    performancePayload.backtestHistoryEntry = buildBacktestHistoryEntry(safeResult, performancePayload, context || {})
    performancePayload.backtestSymbolPool = Array.isArray(context && context.appliedSymbolPool)
        ? context.appliedSymbolPool.slice()
        : []
    performancePayload.latestBacktest = performancePayload.backtestHistoryEntry
    performancePayload.replaceLatestBacktest = context && context.replaceLatestBacktest !== undefined
        ? !!context.replaceLatestBacktest
        : true
    return performancePayload
}
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
        tradeId: record.tradeId || record.trade_id || "",
        entryTime: record.entryTime || record.entry_time || "",
        exitTime: record.exitTime || record.exit_time || "",
        symbol: record.symbol || "",
        direction: record.direction || "",
        entryPrice: asNumber(record.entryPrice !== undefined ? record.entryPrice : record.entry_price, 0),
        exitPrice: asNumber(record.exitPrice !== undefined ? record.exitPrice : record.exit_price, 0),
        quantity: asNumber(record.quantity, 0),
        commission: asNumber(record.commission, 0),
        profit: asNumber(record.profit, 0),
        profitPct: asNumber(record.profitPct !== undefined ? record.profitPct : record.profit_pct, 0),
        notes: record.notes || ""
    }
}

function collectTradeRecords(result) {
    if (!result || typeof result !== "object") {
        return []
    }
    var tradeRecords = Array.isArray(result.tradeRecords)
        ? result.tradeRecords
        : (Array.isArray(result.trade_records) ? result.trade_records : [])
    return tradeRecords
        .map(normalizeTradeRecord)
        .filter(function(record) { return !!record && !!record.symbol })
}

function buildBacktestHistoryEntry(result, performancePayload, context) {
    var timeSeries = result.timeSeries || {}
    var dates = timeSeries.dates || []
    var portfolioValues = timeSeries.portfolioValues || []
    var recordedAt = Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm:ss")
    var tradeRecords = collectTradeRecords(result)

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
        tradeRecords: tradeRecords
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
    performancePayload.latestBacktest = performancePayload.backtestHistoryEntry
    return performancePayload
}
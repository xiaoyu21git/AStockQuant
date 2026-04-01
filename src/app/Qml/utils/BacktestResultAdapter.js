// BacktestResultAdapter.js
// 统一策略回测结果的前端展示结构

function cloneResult(result) {
    return JSON.parse(JSON.stringify(result || {}))
}

function hasValue(value) {
    return value !== undefined && value !== null && value !== ""
}

function normalizeBacktestResult(result) {
    var normalizedResult = cloneResult(result)
    var config = normalizedResult.config || {}
    var strategyParams = config.strategyParams || {}
    var strategyOptions = config.strategyOptions || {}

    if (normalizedResult.performance) {
        normalizedResult.totalReturn = normalizedResult.performance.totalReturn
        normalizedResult.annualReturn = normalizedResult.performance.annualizedReturn !== undefined
            ? normalizedResult.performance.annualizedReturn
            : normalizedResult.performance.annualReturn
        normalizedResult.sharpeRatio = normalizedResult.performance.sharpeRatio
        normalizedResult.maxDrawdown = Math.abs(normalizedResult.performance.maxDrawdown || 0)
        normalizedResult.winRate = normalizedResult.performance.winRate
        normalizedResult.profitLossRatio = normalizedResult.performance.profitFactor
    }

    if (normalizedResult.trades) {
        normalizedResult.totalTrades = normalizedResult.trades.totalTrades
        normalizedResult.winningTrades = normalizedResult.trades.winningTrades
        normalizedResult.losingTrades = normalizedResult.trades.losingTrades
    }

    if (!hasValue(normalizedResult.annualReturn) && hasValue(normalizedResult.annualizedReturn)) {
        normalizedResult.annualReturn = normalizedResult.annualizedReturn
    }

    normalizedResult.portfolioSource = strategyOptions.portfolio_source || strategyParams.portfolio_source || ""
    normalizedResult.portfolioName = strategyOptions.portfolio_name || strategyParams.portfolio_name || ""
    normalizedResult.portfolioFactorCount = strategyParams.portfolio_factor_count || 0
    normalizedResult.portfolioFactorIds = strategyOptions.portfolio_factor_ids || ""
    normalizedResult.portfolioAllocationsJson = strategyParams.portfolio_allocations_json || ""
    normalizedResult.portfolioStrategySubtype = strategyOptions.portfolio_strategy_subtype || ""

    normalizedResult.executionStopLossRate = hasValue(config.stopLossRate)
        ? config.stopLossRate
        : strategyParams.stopLossPercent
    normalizedResult.executionTakeProfitRate = hasValue(config.takeProfitRate)
        ? config.takeProfitRate
        : strategyParams.takeProfitPercent
    normalizedResult.executionMaxDrawdownLimit = hasValue(config.maxDrawdownLimit)
        ? config.maxDrawdownLimit
        : strategyParams.maxDrawdownLimit
    normalizedResult.executionRebalanceFrequency = hasValue(config.rebalanceFrequency)
        ? config.rebalanceFrequency
        : strategyParams.rebalanceDays
    normalizedResult.executionMaxPositionRatio = hasValue(config.maxPositionRatio)
        ? config.maxPositionRatio
        : strategyParams.maxTotalExposure
    normalizedResult.executionMaxSinglePositionRatio = hasValue(config.maxSinglePositionRatio)
        ? config.maxSinglePositionRatio
        : strategyParams.maxPositionPercent

    return normalizedResult
}
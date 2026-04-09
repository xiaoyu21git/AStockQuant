// BacktestResultAdapter.js
// 统一策略回测结果的前端展示结构

.pragma library
.import "./StrategyStructureAdapter.js" as StructureAdapter

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
    var scopeContext = StructureAdapter.resolveStrategyScopeContext(normalizedResult)
    var ruleProfile = StructureAdapter.resolveRuleProfile(normalizedResult)
    var executionPolicy = StructureAdapter.resolveExecutionPolicy(normalizedResult)
    var backtestAssumptions = StructureAdapter.resolveBacktestAssumptions(normalizedResult)
    var allocations = StructureAdapter.resolvePortfolioAllocations(normalizedResult)

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

    normalizedResult.portfolioSource = scopeContext.portfolio_source || strategyOptions.portfolio_source || strategyParams.portfolio_source || ""
    normalizedResult.portfolioName = scopeContext.portfolio_name || strategyOptions.portfolio_name || strategyParams.portfolio_name || ""
    normalizedResult.portfolioFactorCount = allocations.length > 0 ? allocations.length : (strategyParams.portfolio_factor_count || 0)
    normalizedResult.portfolioFactorIds = strategyOptions.portfolio_factor_ids
        || allocations.map(function(item) { return item.factor_id || item.factorId || "" }).filter(function(item) { return item !== "" }).join(",")
    normalizedResult.portfolioAllocationsJson = allocations.length > 0
        ? JSON.stringify(allocations)
        : (scopeContext.portfolio_allocations_json || strategyParams.portfolio_allocations_json || "")
    normalizedResult.portfolioStrategySubtype = scopeContext.selectedStrategySubtype || strategyOptions.portfolio_strategy_subtype || ""

    normalizedResult.executionStopLossRate = hasValue(ruleProfile.stopLossPercent)
        ? ruleProfile.stopLossPercent
        : (hasValue(config.stopLossRate) ? config.stopLossRate : strategyParams.stopLossPercent)
    normalizedResult.executionTakeProfitRate = hasValue(ruleProfile.takeProfitPercent)
        ? ruleProfile.takeProfitPercent
        : (hasValue(config.takeProfitRate) ? config.takeProfitRate : strategyParams.takeProfitPercent)
    normalizedResult.executionMaxDrawdownLimit = hasValue(ruleProfile.maxDrawdownLimit)
        ? ruleProfile.maxDrawdownLimit
        : (hasValue(config.maxDrawdownLimit) ? config.maxDrawdownLimit : strategyParams.maxDrawdownLimit)
    normalizedResult.executionRebalanceFrequency = hasValue(executionPolicy.rebalanceDays)
        ? executionPolicy.rebalanceDays
        : (hasValue(config.rebalanceFrequency) ? config.rebalanceFrequency : strategyParams.rebalanceDays)
    normalizedResult.executionMaxPositionRatio = hasValue(ruleProfile.maxTotalExposure)
        ? ruleProfile.maxTotalExposure
        : (hasValue(config.maxPositionRatio) ? config.maxPositionRatio : strategyParams.maxTotalExposure)
    normalizedResult.executionMaxSinglePositionRatio = hasValue(ruleProfile.maxPositionPercent)
        ? ruleProfile.maxPositionPercent
        : (hasValue(config.maxSinglePositionRatio) ? config.maxSinglePositionRatio : strategyParams.maxPositionPercent)
    normalizedResult.dataSourceMode = normalizedResult.dataSourceMode || backtestAssumptions.dataSourceMode || config.dataSourceMode || ""

    return normalizedResult
}
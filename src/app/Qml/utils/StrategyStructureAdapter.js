.pragma library

function hasValue(value) {
    return value !== undefined && value !== null && value !== ""
}

function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value)
}

function firstDefinedValue(source, keys) {
    var map = isObject(source) ? source : ({})
    for (var index = 0; index < keys.length; ++index) {
        var key = keys[index]
        if (hasValue(map[key])) {
            return map[key]
        }
    }
    return undefined
}

function appendMap(target, candidate) {
    if (!isObject(candidate)) {
        return
    }
    target.push(candidate)
}

function appendSymbolCollection(target, seenSymbols, rawCollection) {
    var appendSymbol = function(rawSymbol) {
        var symbol = String(rawSymbol || "").trim().toUpperCase()
        if (!symbol || seenSymbols[symbol]) {
            return
        }
        seenSymbols[symbol] = true
        target.push(symbol)
    }

    if (rawCollection === undefined || rawCollection === null) {
        return
    }

    if (Array.isArray(rawCollection)) {
        for (var index = 0; index < rawCollection.length; ++index) {
            appendSymbol(rawCollection[index])
        }
        return
    }

    if (typeof rawCollection === "string") {
        var rawText = String(rawCollection).trim()
        if (!rawText) {
            return
        }

        if (rawText.charAt(0) === "[") {
            try {
                var parsed = JSON.parse(rawText)
                if (Array.isArray(parsed)) {
                    for (var parsedIndex = 0; parsedIndex < parsed.length; ++parsedIndex) {
                        appendSymbol(parsed[parsedIndex])
                    }
                    return
                }
            } catch (error) {
            }
        }

        rawText.split(/[,;\s，；]+/).forEach(appendSymbol)
        return
    }

    appendSymbol(rawCollection)
}

function normalizeSymbolPool(source) {
    var normalized = []
    appendSymbolCollection(normalized, ({}), source)
    return normalized
}

function parseAllocations(rawAllocations) {
    if (!hasValue(rawAllocations)) {
        return []
    }

    if (Array.isArray(rawAllocations)) {
        return rawAllocations.slice()
    }

    if (typeof rawAllocations === "string") {
        var rawText = String(rawAllocations).trim()
        if (!rawText) {
            return []
        }

        try {
            var parsed = JSON.parse(rawText)
            return Array.isArray(parsed) ? parsed : []
        } catch (error) {
            return []
        }
    }

    return []
}

function mergeConfiguredValues(target, source) {
    if (!isObject(source)) {
        return target
    }

    for (var key in source) {
        if (hasValue(source[key])) {
            target[key] = source[key]
        }
    }
    return target
}

function collectSourceMaps(source) {
    var maps = []
    var root = isObject(source) ? source : ({})
    var config = isObject(root.config) ? root.config : ({})
    var parameters = isObject(root.parameters) ? root.parameters : ({})
    var strategyParams = isObject(config.strategyParams) ? config.strategyParams : ({})
    var strategyOptions = isObject(config.strategyOptions) ? config.strategyOptions : ({})
    var runtimeParameters = isObject(root.runtimeParameters) ? root.runtimeParameters : ({})
    var runtimeParametersLegacy = isObject(root.runtime_parameters) ? root.runtime_parameters : ({})
    var backtestRuntime = isObject(root.backtest_runtime) ? root.backtest_runtime : ({})
    var backtestSettings = isObject(root.backtest_settings) ? root.backtest_settings : ({})
    var parameterBacktestRuntime = isObject(parameters.backtest_runtime) ? parameters.backtest_runtime : ({})
    var parameterBacktestSettings = isObject(parameters.backtest_settings) ? parameters.backtest_settings : ({})
    var advancedOptions = isObject(root.advanced_options) ? root.advanced_options : (isObject(root.advancedOptions) ? root.advancedOptions : ({}))
    var optimizationConfig = isObject(advancedOptions.optimization_config)
        ? advancedOptions.optimization_config
        : (isObject(advancedOptions.optimizationConfig) ? advancedOptions.optimizationConfig : ({}))

    appendMap(maps, parameters)
    appendMap(maps, strategyParams)
    appendMap(maps, strategyOptions)
    appendMap(maps, root)
    appendMap(maps, config)
    appendMap(maps, backtestSettings)
    appendMap(maps, parameterBacktestSettings)
    appendMap(maps, advancedOptions)
    appendMap(maps, optimizationConfig)
    appendMap(maps, backtestRuntime)
    appendMap(maps, parameterBacktestRuntime)
    appendMap(maps, runtimeParametersLegacy)
    appendMap(maps, runtimeParameters)
    return maps
}

function resolveStructureSnapshot(source, snapshotKey) {
    var containers = []
    var root = isObject(source) ? source : ({})
    appendMap(containers, root)
    appendMap(containers, root.config)
    appendMap(containers, root.parameters)

    for (var index = 0; index < containers.length; ++index) {
        var candidate = containers[index][snapshotKey]
        if (isObject(candidate)) {
            return candidate
        }
    }

    return ({})
}

function resolveStructuredValues(source, snapshotKey, aliasGroups) {
    var resolved = ({})
    mergeConfiguredValues(resolved, resolveStructureSnapshot(source, snapshotKey))

    var candidates = collectSourceMaps(source)
    for (var groupIndex = 0; groupIndex < aliasGroups.length; ++groupIndex) {
        var group = aliasGroups[groupIndex]
        if (hasValue(resolved[group.canonicalKey])) {
            continue
        }

        var keys = [group.canonicalKey].concat(group.aliases || [])
        for (var sourceIndex = candidates.length - 1; sourceIndex >= 0; --sourceIndex) {
            var value = firstDefinedValue(candidates[sourceIndex], keys)
            if (hasValue(value)) {
                resolved[group.canonicalKey] = value
                break
            }
        }
    }

    return resolved
}

function resolveRuleProfile(source) {
    return resolveStructuredValues(source, "ruleProfileSnapshot", [
        { canonicalKey: "maxTotalExposure", aliases: ["maxPositionRatio"] },
        { canonicalKey: "maxPositionPercent", aliases: ["maxSinglePositionRatio", "positionPercent", "position_size", "positionSize"] },
        { canonicalKey: "maxDrawdownLimit", aliases: ["max_drawdown_limit"] },
        { canonicalKey: "stopLossPercent", aliases: ["stop_loss", "stopLoss"] },
        { canonicalKey: "takeProfitPercent", aliases: ["take_profit", "takeProfit"] },
        { canonicalKey: "varWarningPercent", aliases: [] },
        { canonicalKey: "level1Breaker", aliases: [] },
        { canonicalKey: "level2Breaker", aliases: [] },
        { canonicalKey: "level3Breaker", aliases: [] },
        { canonicalKey: "autoStopEnabled", aliases: ["auto_stop_enabled"] },
        { canonicalKey: "maxPositions", aliases: [] }
    ])
}

function resolveExecutionPolicy(source) {
    return resolveStructuredValues(source, "executionPolicySnapshot", [
        { canonicalKey: "positionSizingMethod", aliases: ["position_sizing_method"] },
        { canonicalKey: "rebalanceDays", aliases: ["rebalance_days", "rebalancingPeriod", "rebalanceFrequency"] },
        { canonicalKey: "orderSizeLimit", aliases: ["maxOrderSize"] },
        { canonicalKey: "turnoverLimit", aliases: [] },
        { canonicalKey: "maxBatchOrders", aliases: ["batchOrderLimit"] },
        { canonicalKey: "maxBatchNotionalWan", aliases: ["batchNotionalLimitWan"] },
        { canonicalKey: "maxBatchNotional", aliases: ["batchNotionalLimit"] },
        { canonicalKey: "minWeightPercent", aliases: ["min_weight_percent", "minPositionPercent", "minSinglePositionRatio"] },
        { canonicalKey: "maxWeightPercent", aliases: ["max_weight_percent"] },
        { canonicalKey: "maxPositions", aliases: [] }
    ])
}

function resolveBacktestAssumptions(source) {
    return resolveStructuredValues(source, "backtestAssumptionsSnapshot", [
        { canonicalKey: "startDate", aliases: ["start_date"] },
        { canonicalKey: "endDate", aliases: ["end_date"] },
        { canonicalKey: "backtestYears", aliases: ["backtestPeriod", "years"] },
        { canonicalKey: "initialCapital", aliases: [] },
        { canonicalKey: "benchmark", aliases: [] },
        { canonicalKey: "commissionRate", aliases: ["commission", "transactionCost", "transaction_cost"] },
        { canonicalKey: "slippageRate", aliases: ["slippage", "slippageCost", "slippageLimit"] },
        { canonicalKey: "dataSourceMode", aliases: [] }
    ])
}

function resolveStrategyScopeContext(source) {
    return resolveStructuredValues(source, "strategyScopeContextSnapshot", [
        { canonicalKey: "symbol_pool", aliases: ["symbolPool", "selectedSymbols", "symbols"] },
        { canonicalKey: "universeType", aliases: ["selectedUniverseType"] },
        { canonicalKey: "universeId", aliases: ["indexSymbol", "selectedIndexSymbol"] },
        { canonicalKey: "selectedStrategyType", aliases: ["strategy_type", "strategyType"] },
        { canonicalKey: "selectedStrategySubtype", aliases: ["sub_type", "subType"] },
        { canonicalKey: "selectedStrategyName", aliases: ["strategy_name", "strategyName", "name"] },
        { canonicalKey: "portfolio_source", aliases: ["source"] },
        { canonicalKey: "portfolio_name", aliases: [] },
        { canonicalKey: "portfolio_allocations_json", aliases: ["factor_allocations", "allocations"] }
    ])
}

function resolveFactorOverlay(source) {
    return resolveStructuredValues(source, "factorOverlaySnapshot", [
        { canonicalKey: "enabled", aliases: ["factorOverlayEnabled"] },
        { canonicalKey: "targetPositionCount", aliases: ["target_position_count", "top_n", "topN"] },
        { canonicalKey: "minimumCompositeScore", aliases: ["minimum_composite_score", "minCompositeScore"] },
        { canonicalKey: "allocations", aliases: [] },
        { canonicalKey: "factorIds", aliases: ["factor_ids", "selectedFactorIds"] },
        { canonicalKey: "combineMode", aliases: ["combine_mode"] },
        { canonicalKey: "selectionScope", aliases: ["selection_scope"] }
    ])
}

function resolveBacktestSessionView(source) {
    var resolved = ({})
    var candidates = collectSourceMaps(source)
    for (var index = 0; index < candidates.length; ++index) {
        mergeConfiguredValues(resolved, candidates[index])
    }

    mergeConfiguredValues(resolved, resolveBacktestAssumptions(source))
    mergeConfiguredValues(resolved, resolveExecutionPolicy(source))
    mergeConfiguredValues(resolved, resolveRuleProfile(source))
    mergeConfiguredValues(resolved, resolveStrategyScopeContext(source))
    mergeConfiguredValues(resolved, resolveFactorOverlay(source))
    return resolved
}

function resolvePortfolioAllocations(source, fallbackSource) {
    var scopeContext = resolveStrategyScopeContext(source)
    var rawAllocations = firstDefinedValue(scopeContext, ["portfolio_allocations_json", "factor_allocations", "allocations"])
    if (!hasValue(rawAllocations)) {
        rawAllocations = firstDefinedValue(resolveBacktestSessionView(source), ["portfolio_allocations_json", "factor_allocations", "allocations"])
    }
    if (!hasValue(rawAllocations) && fallbackSource) {
        rawAllocations = firstDefinedValue(resolveBacktestSessionView(fallbackSource), ["portfolio_allocations_json", "factor_allocations", "allocations"])
    }
    return parseAllocations(rawAllocations)
}

function resolveLinkedStockPoolSymbols(source) {
    var root = isObject(source) ? source : ({})
    var parameters = isObject(root.parameters) ? root.parameters : ({})
    var resolved = []
    var seen = ({})
    appendSymbolCollection(resolved, seen, root.linked_stock_pool_symbols)
    appendSymbolCollection(resolved, seen, root.linkedStockPoolSymbols)
    appendSymbolCollection(resolved, seen, parameters.linked_stock_pool_symbols)
    appendSymbolCollection(resolved, seen, parameters.linkedStockPoolSymbols)
    return resolved
}

function resolvePersistedStrategySymbolPool(source) {
    var root = isObject(source) ? source : ({})
    var parameters = isObject(root.parameters) ? root.parameters : ({})
    var resolved = []
    var seen = ({})
    appendSymbolCollection(resolved, seen, root.symbol_pool)
    appendSymbolCollection(resolved, seen, root.symbolPool)
    appendSymbolCollection(resolved, seen, parameters.symbol_pool)
    appendSymbolCollection(resolved, seen, parameters.symbolPool)
    if (resolved.length > 0) {
        return resolved
    }

    if (resolveLinkedStockPoolSymbols(source).length > 0) {
        return []
    }

    var scopeContext = resolveStrategyScopeContext(source)
    return normalizeSymbolPool(firstDefinedValue(scopeContext, ["symbol_pool", "symbolPool"]))
}

function resolvePersistedBacktestSymbolPool(source) {
    var root = isObject(source) ? source : ({})
    var parameters = isObject(root.parameters) ? root.parameters : ({})
    var resolved = []
    var seen = ({})

    appendSymbolCollection(resolved, seen, root.backtest_symbol_pool)
    appendSymbolCollection(resolved, seen, root.backtestSymbolPool)
    appendSymbolCollection(resolved, seen, parameters.backtest_symbol_pool)
    appendSymbolCollection(resolved, seen, parameters.backtestSymbolPool)
    if (resolved.length > 0) {
        return resolved
    }

    return resolvePersistedStrategySymbolPool(source)
}

function resolveSymbolPool(source) {
    var resolved = []
    var seen = ({})
    var scopeContext = resolveStrategyScopeContext(source)
    appendSymbolCollection(resolved, seen, firstDefinedValue(scopeContext, ["symbol_pool", "symbolPool"]))

    var candidates = collectSourceMaps(source)
    for (var index = 0; index < candidates.length; ++index) {
        appendSymbolCollection(resolved, seen, candidates[index].selectedSymbols)
        appendSymbolCollection(resolved, seen, candidates[index].symbols)
        appendSymbolCollection(resolved, seen, candidates[index].symbol_pool)
        appendSymbolCollection(resolved, seen, candidates[index].symbolPool)
    }

    return resolved
}

function resolveBacktestRecordSymbolPool(source) {
    return resolveSymbolPool(source)
}

function resolveUniverseContext(source) {
    var scopeContext = resolveStrategyScopeContext(source)
    var merged = resolveBacktestSessionView(source)
    var universeType = String(firstDefinedValue(scopeContext, ["universeType"]) || firstDefinedValue(merged, ["universeType"]) || "").trim()
    var universeId = String(firstDefinedValue(scopeContext, ["universeId", "indexSymbol"]) || firstDefinedValue(merged, ["universeId", "indexSymbol", "selectedIndexSymbol"]) || "").trim()
    return {
        universeType: universeType,
        universeId: universeId,
        indexSymbol: universeType === "index" ? universeId : universeId
    }
}

var StrategyStructureAdapter = {
    resolveRuleProfile: resolveRuleProfile,
    resolveExecutionPolicy: resolveExecutionPolicy,
    resolveBacktestAssumptions: resolveBacktestAssumptions,
    resolveStrategyScopeContext: resolveStrategyScopeContext,
    resolveFactorOverlay: resolveFactorOverlay,
    resolveBacktestSessionView: resolveBacktestSessionView,
    resolvePortfolioAllocations: resolvePortfolioAllocations,
    resolveLinkedStockPoolSymbols: resolveLinkedStockPoolSymbols,
    resolvePersistedStrategySymbolPool: resolvePersistedStrategySymbolPool,
    resolvePersistedBacktestSymbolPool: resolvePersistedBacktestSymbolPool,
    resolveSymbolPool: resolveSymbolPool,
    resolveBacktestRecordSymbolPool: resolveBacktestRecordSymbolPool,
    resolveUniverseContext: resolveUniverseContext
}
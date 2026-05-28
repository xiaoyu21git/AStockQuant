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
        { canonicalKey: "maxTotalExposure", aliases: [] },
        { canonicalKey: "maxPositionPercent", aliases: [] },
        { canonicalKey: "maxDrawdownLimit", aliases: [] },
        { canonicalKey: "stopLossPercent", aliases: [] },
        { canonicalKey: "takeProfitPercent", aliases: [] },
        { canonicalKey: "varWarningPercent", aliases: [] },
        { canonicalKey: "level1Breaker", aliases: [] },
        { canonicalKey: "level2Breaker", aliases: [] },
        { canonicalKey: "level3Breaker", aliases: [] },
        { canonicalKey: "autoStopEnabled", aliases: [] },
        { canonicalKey: "maxPositions", aliases: [] }
    ])
}

function resolveExecutionPolicy(source) {
    return resolveStructuredValues(source, "executionPolicySnapshot", [
        { canonicalKey: "positionSizingMethod", aliases: [] },
        { canonicalKey: "rebalanceDays", aliases: [] },
        { canonicalKey: "orderSizeLimit", aliases: [] },
        { canonicalKey: "turnoverLimit", aliases: [] },
        { canonicalKey: "maxBatchOrders", aliases: [] },
        { canonicalKey: "maxBatchNotionalWan", aliases: [] },
        { canonicalKey: "maxBatchNotional", aliases: [] },
        { canonicalKey: "minWeightPercent", aliases: [] },
        { canonicalKey: "maxWeightPercent", aliases: [] },
        { canonicalKey: "maxPositions", aliases: [] }
    ])
}

function resolveBacktestAssumptions(source) {
    return resolveStructuredValues(source, "backtestAssumptionsSnapshot", [
        { canonicalKey: "startDate", aliases: [] },
        { canonicalKey: "endDate", aliases: [] },
        { canonicalKey: "backtestPeriod", aliases: [] },
        { canonicalKey: "initialCapital", aliases: [] },
        { canonicalKey: "benchmark", aliases: [] },
        { canonicalKey: "commissionRate", aliases: [] },
        { canonicalKey: "slippageRate", aliases: [] },
        { canonicalKey: "dataSourceMode", aliases: [] },
        { canonicalKey: "dataSourceDatasetId", aliases: [] }
    ])
}

function resolveStrategyScopeContext(source) {
    return resolveStructuredValues(source, "strategyScopeContextSnapshot", [
        { canonicalKey: "universeType", aliases: [] },
        { canonicalKey: "universeId", aliases: [] },
        { canonicalKey: "selectedStrategyType", aliases: [] },
        { canonicalKey: "selectedStrategySubtype", aliases: [] },
        { canonicalKey: "selectedStrategyName", aliases: [] },
        { canonicalKey: "portfolio_source", aliases: [] },
        { canonicalKey: "portfolio_name", aliases: [] },
        { canonicalKey: "portfolio_allocations_json", aliases: [] }
    ])
}

function resolveFactorOverlay(source) {
    return resolveStructuredValues(source, "factorOverlaySnapshot", [
        { canonicalKey: "enabled", aliases: [] },
        { canonicalKey: "targetPositionCount", aliases: [] },
        { canonicalKey: "minimumCompositeScore", aliases: [] },
        { canonicalKey: "allocations", aliases: [] },
        { canonicalKey: "factorIds", aliases: [] },
        { canonicalKey: "combineMode", aliases: [] },
        { canonicalKey: "selectionScope", aliases: [] }
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
    var rawAllocations = scopeContext.portfolio_allocations_json
    if (!hasValue(rawAllocations)) {
        rawAllocations = resolveBacktestSessionView(source).portfolio_allocations_json
    }
    if (!hasValue(rawAllocations) && fallbackSource) {
        rawAllocations = resolveBacktestSessionView(fallbackSource).portfolio_allocations_json
    }
    return parseAllocations(rawAllocations)
}

function resolveUniverseContext(source) {
    var scopeContext = resolveStrategyScopeContext(source)
    var merged = resolveBacktestSessionView(source)
    var universeType = String(firstDefinedValue(scopeContext, ["universeType"]) || firstDefinedValue(merged, ["universeType"]) || "").trim()
    var universeId = String(firstDefinedValue(scopeContext, ["universeId"]) || firstDefinedValue(merged, ["universeId"]) || "").trim()
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
    resolveUniverseContext: resolveUniverseContext
}
.pragma library

function hasValue(value) {
    return value !== undefined && value !== null && value !== ""
}

function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value)
}

function objectOrEmpty(value) {
    return isObject(value) ? value : ({})
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

function rootParameters(source) {
    var root = objectOrEmpty(source)
    return objectOrEmpty(root.parameters)
}

function resolveRuleProfile(source) {
    return objectOrEmpty(rootParameters(source).rule_profile)
}

function resolveExecutionPolicy(source) {
    return objectOrEmpty(rootParameters(source).execution_policy)
}

function resolveBacktestAssumptions(source) {
    return objectOrEmpty(rootParameters(source).backtest_assumptions)
}

function resolveStrategyScopeContext(source) {
    return objectOrEmpty(rootParameters(source).strategy_scope_context)
}

function resolveFactorOverlay(source) {
    return objectOrEmpty(rootParameters(source).factor_overlay)
}

function resolveBacktestSessionView(source) {
    var merged = ({})
    mergeConfiguredValues(merged, resolveBacktestAssumptions(source))
    mergeConfiguredValues(merged, resolveExecutionPolicy(source))
    mergeConfiguredValues(merged, resolveRuleProfile(source))
    mergeConfiguredValues(merged, resolveStrategyScopeContext(source))
    mergeConfiguredValues(merged, resolveFactorOverlay(source))
    return merged
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

function resolvePortfolioAllocations(source) {
    var scopeContext = resolveStrategyScopeContext(source)
    return parseAllocations(scopeContext.portfolio_allocations_json)
}

function resolveUniverseContext(source) {
    var scopeContext = resolveStrategyScopeContext(source)
    var universeType = String(scopeContext.universeType || "").trim()
    var universeId = String(scopeContext.universeId || "").trim()
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

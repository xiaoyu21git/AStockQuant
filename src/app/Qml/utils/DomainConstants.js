.pragma library

function preferredRiskParamGroups() {
    return [
        {
            id: "riskCore",
            name: "基础风险控制",
            description: "先配置止损、止盈、最大回撤和自动止损等基础保护参数。",
            minColumnWidth: 560,
            maxColumns: 2,
            params: ["stopLossPercent", "takeProfitPercent", "maxDrawdownLimit", "autoStopEnabled"]
        },
        {
            id: "exposureControl",
            name: "仓位与暴露控制",
            description: "集中处理仓位分配、总暴露、集中度和持仓数量限制。",
            minColumnWidth: 620,
            maxColumns: 2,
            params: ["positionSizingMethod", "maxTotalExposure", "maxPositionPercent", "maxIndustryExposure", "maxThemeExposure"]
        },
        {
            id: "executionLimits",
            name: "执行与交易限制",
            description: "约束日内成交规模、VaR 预警和单日损失等执行风险。",
            minColumnWidth: 760,
            maxColumns: 1,
            params: ["varWarningPercent", "orderSizeLimit", "turnoverLimit", "slippageLimit", "maxDailyLoss"]
        },
        {
            id: "breakerRules",
            name: "熔断与相关性限制",
            description: "用于控制极端波动场景下的熔断阈值和持仓相关性。",
            minColumnWidth: 760,
            maxColumns: 1,
            params: ["level1Breaker", "level2Breaker", "level3Breaker", "maxCorrelation"]
        }
    ]
}

function resolveStrategyConfigAliases(key) {
    switch (key) {
    case "maxPositionPercent":
        return ["maxPositionPercent"]
    case "maxTotalExposure":
        return ["maxTotalExposure"]
    default:
        return [key]
    }
}

function getStrategyAdvancedOptions(strategy) {
    return ({})
}

function defaultFactorOverlay() {
    return {
        enabled: false,
        targetPositionCount: 50,
        minimumCompositeScore: 0,
        combineMode: "rank_only",
        selectionScope: "rule_eligible",
        allocations: []
    }
}

function buildExecutionPolicyPayload(sourceParameters) {
    return {
        version: 1
    }
}

function buildBacktestAssumptionsPayload(sourceParameters) {
    return {
        version: 1
    }
}

function composerStagePhaseIndex(stageId) {
    var normalizedStageId = String(stageId || "").trim().toLowerCase()
    var mapping = {
        market: 0,
        eligibility: 1,
        signal: 1,
        portfolio: 5,
        rebalance: 3,
        execution: 5,
        account_risk: 5
    }
    return mapping.hasOwnProperty(normalizedStageId) ? mapping[normalizedStageId] : -1
}

function supportedRuleBindingPhaseIndex(value) {
    var parsed = Number(value)
    if (!isFinite(parsed)) {
        return -1
    }
    parsed = Math.floor(parsed)
    return parsed >= 0 && parsed <= 6 ? parsed : -1
}

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

function toArray(value) {
    return Array.isArray(value) ? value : []
}

function fileNameFromPath(filePath) {
    if (!hasValue(filePath)) {
        return ""
    }

    var normalizedPath = String(filePath).replace(/\\/g, "/")
    var segments = normalizedPath.split("/")
    return segments.length > 0 ? segments[segments.length - 1] : normalizedPath
}

function normalizeRuleTemplateEvent(event) {
    if (!event || typeof event !== "object") {
        return null
    }

    return {
        timestamp: String(event.timestamp || ""),
        symbol: String(event.symbol || ""),
        action: String(event.action || ""),
        eventType: String(event.eventType || event.event_type || ""),
        ruleId: String(event.ruleId || event.rule_id || ""),
        reasonCode: String(event.reasonCode || event.reason_code || ""),
        message: String(event.message || ""),
        resultType: String(event.resultType || event.result_type || ""),
        groupId: String(event.groupId || event.group_id || ""),
        groupTitle: String(event.groupTitle || event.group_title || ""),
        groupRole: String(event.groupRole || event.group_role || ""),
        groupOperator: String(event.groupOperator || event.group_operator || "")
    }
}

function normalizeRuleTemplateGroupDecision(decision) {
    if (!decision || typeof decision !== "object") {
        return null
    }

    return {
        stage: String(decision.stage || ""),
        groupId: String(decision.groupId || decision.group_id || ""),
        groupTitle: String(decision.groupTitle || decision.group_title || ""),
        groupRole: String(decision.groupRole || decision.group_role || ""),
        groupOperator: String(decision.groupOperator || decision.group_operator || ""),
        disposition: String(decision.disposition || ""),
        outcome: String(decision.outcome || ""),
        skipReason: String(decision.skipReason || decision.skip_reason || ""),
        matchedRuleId: String(decision.matchedRuleId || decision.matched_rule_id || ""),
        matchedResultType: String(decision.matchedResultType || decision.matched_result_type || ""),
        matchedReasonCode: String(decision.matchedReasonCode || decision.matched_reason_code || ""),
        memberCount: Number(decision.memberCount || decision.member_count || 0),
        applicableCount: Number(decision.applicableCount || decision.applicable_count || 0),
        matchedCount: Number(decision.matchedCount || decision.matched_count || 0),
        filteredCount: Number(decision.filteredCount || decision.filtered_count || 0)
    }
}

function normalizeRuleTemplateSummary(summary, strategyOptions) {
    var safeSummary = summary && typeof summary === "object" ? cloneResult(summary) : {}
    var recentEvents = toArray(safeSummary.recentEvents || safeSummary.recent_events)
        .map(normalizeRuleTemplateEvent)
        .filter(function(item) { return !!item })
    var latestGroupDecisions = toArray(safeSummary.latestGroupDecisions || safeSummary.latest_group_decisions)
        .map(normalizeRuleTemplateGroupDecision)
        .filter(function(item) { return !!item })

    var normalizedSummary = {
        hasTemplate: !!safeSummary.hasTemplate,
        templateFilePath: String(safeSummary.templateFilePath || safeSummary.template_file_path || ""),
        templateFileName: String(safeSummary.templateFileName || safeSummary.template_file_name || ""),
        templateNamespace: String(safeSummary.templateNamespace || safeSummary.template_namespace || ""),
        groupId: String(safeSummary.groupId || safeSummary.group_id || ""),
        groupTitle: String(safeSummary.groupTitle || safeSummary.group_title || ""),
        groupRole: String(safeSummary.groupRole || safeSummary.group_role || ""),
        groupOperator: String(safeSummary.groupOperator || safeSummary.group_operator || ""),
        triggeredCount: Number(safeSummary.triggeredCount || safeSummary.triggered_count || 0),
        entryBlockCount: Number(safeSummary.entryBlockCount || safeSummary.entry_block_count || 0),
        forcedExitCount: Number(safeSummary.forcedExitCount || safeSummary.forced_exit_count || 0),
        latestGroupDecisions: latestGroupDecisions,
        recentEvents: recentEvents
    }

    if (!normalizedSummary.templateFileName) {
        normalizedSummary.templateFileName = String(
            strategyOptions.rule_template_file_name
            || fileNameFromPath(normalizedSummary.templateFilePath)
            || "")
    }

    if (!normalizedSummary.hasTemplate) {
        normalizedSummary.hasTemplate = normalizedSummary.templateFileName.length > 0
            || normalizedSummary.templateFilePath.length > 0
            || normalizedSummary.triggeredCount > 0
            || recentEvents.length > 0
    }

    normalizedSummary.latestEvent = recentEvents.length > 0 ? recentEvents[recentEvents.length - 1] : null
    normalizedSummary.statusText = normalizedSummary.triggeredCount > 0
        ? ("已触发 " + normalizedSummary.triggeredCount + " 次")
        : (normalizedSummary.hasTemplate
            ? (latestGroupDecisions.length > 0
                ? ("最近裁决 " + latestGroupDecisions.length + " 组")
                : "已绑定未触发")
            : "")
    return normalizedSummary
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
    var factorOverlay = StructureAdapter.resolveFactorOverlay(normalizedResult)
    var allocations = StructureAdapter.resolvePortfolioAllocations(normalizedResult)
    var ruleTemplateSummary = normalizeRuleTemplateSummary(
        normalizedResult.ruleTemplateSummary || normalizedResult.rule_template_summary,
        strategyOptions)

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

    normalizedResult.portfolioSource = scopeContext.portfolio_source || ""
    normalizedResult.portfolioName = scopeContext.portfolio_name || ""
    normalizedResult.portfolioFactorCount = allocations.length
    normalizedResult.portfolioFactorIds = allocations.map(function(item) {
        return item.factor_id || ""
    }).filter(function(item) { return item !== "" }).join(",")
    normalizedResult.portfolioAllocationsJson = allocations.length > 0
        ? JSON.stringify(allocations)
        : ""
    normalizedResult.portfolioStrategySubtype = scopeContext.selectedStrategySubtype || ""
    normalizedResult.factorOverlayEnabled = !!factorOverlay.enabled
    normalizedResult.factorOverlayTargetPositionCount = Number(factorOverlay.targetPositionCount || 0)
    normalizedResult.factorOverlayMinimumCompositeScore = Number(factorOverlay.minimumCompositeScore || 0)
    normalizedResult.factorOverlayAllocationsJson = Array.isArray(factorOverlay.allocations)
        ? JSON.stringify(factorOverlay.allocations)
        : ""
    normalizedResult.factorOverlayFactorIds = Array.isArray(factorOverlay.allocations)
        ? factorOverlay.allocations.map(function(item) {
            return item.factor_id || ""
        }).filter(function(item) { return item !== "" }).join(",")
        : ""

    normalizedResult.executionStopLossRate = hasValue(ruleProfile.stopLossPercent)
        ? ruleProfile.stopLossPercent
        : config.stopLossRate
    normalizedResult.executionTakeProfitRate = hasValue(ruleProfile.takeProfitPercent)
        ? ruleProfile.takeProfitPercent
        : config.takeProfitRate
    normalizedResult.executionMaxDrawdownLimit = hasValue(ruleProfile.maxDrawdownLimit)
        ? ruleProfile.maxDrawdownLimit
        : config.maxDrawdownLimit
    normalizedResult.executionRebalanceFrequency = hasValue(executionPolicy.rebalanceDays)
        ? executionPolicy.rebalanceDays
        : config.rebalanceFrequency
    normalizedResult.executionMaxPositionRatio = hasValue(ruleProfile.maxTotalExposure)
        ? ruleProfile.maxTotalExposure
        : config.maxPositionRatio
    normalizedResult.executionMaxSinglePositionRatio = hasValue(ruleProfile.maxPositionPercent)
        ? ruleProfile.maxPositionPercent
        : config.maxSinglePositionRatio
    normalizedResult.dataSourceMode = normalizedResult.dataSourceMode || backtestAssumptions.dataSourceMode || config.dataSourceMode || ""
    normalizedResult.ruleTemplateSummary = ruleTemplateSummary
    normalizedResult.ruleTemplateFileName = ruleTemplateSummary.templateFileName
    normalizedResult.ruleTemplateTriggeredCount = ruleTemplateSummary.triggeredCount
    normalizedResult.ruleTemplateEntryBlockCount = ruleTemplateSummary.entryBlockCount
    normalizedResult.ruleTemplateForcedExitCount = ruleTemplateSummary.forcedExitCount
    normalizedResult.ruleTemplateLatestEvent = ruleTemplateSummary.latestEvent

    return normalizedResult
}
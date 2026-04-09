import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Rectangle {
    id: root

    property var strategyService: Bridge.StrategyService
    property var configuredRuntimeRuleDefaults: ({})
    property string highlightStrategyId: ""
    property string highlightStrategyName: ""
    property var highlightedStrategyDetail: ({})
    property var latestRuntimeRuleEvaluation: ({})
    property var runtimeRuleEvaluationFeed: []
    property int runtimeRuleFeedLimit: 8
    property string exportFeedbackMessage: ""

    readonly property string latestRuntimeRuleDecision: String(latestRuntimeRuleEvaluation.decision || "")
    readonly property bool hasRuntimeRuleEvaluation: latestRuntimeRuleDecision.length > 0
    readonly property string observedStrategyId: String(latestRuntimeRuleEvaluation.strategyId || highlightStrategyId || "")

    radius: 24
    color: "#0F172A"
    border.color: "#1E293B"
    border.width: 1
    implicitHeight: runtimeRuleColumn.implicitHeight + 32

    function localEvaluationTimestamp() {
        var now = new Date()
        function pad(value) {
            return value < 10 ? ("0" + value) : String(value)
        }
        return pad(now.getHours()) + ":" + pad(now.getMinutes()) + ":" + pad(now.getSeconds())
    }

    function normalizedRuntimeRuleEntry(evaluation) {
        var entry = {}
        var key
        for (key in (evaluation || ({}))) {
            entry[key] = evaluation[key]
        }
        entry.observedAt = localEvaluationTimestamp()
        return entry
    }

    function rememberRuntimeRuleEvaluation(evaluation) {
        var entry = normalizedRuntimeRuleEntry(evaluation)
        latestRuntimeRuleEvaluation = entry

        var nextFeed = [entry]
        var currentFeed = runtimeRuleEvaluationFeed || []
        for (var index = 0; index < currentFeed.length && nextFeed.length < runtimeRuleFeedLimit; ++index) {
            nextFeed.push(currentFeed[index])
        }
        runtimeRuleEvaluationFeed = nextFeed
    }

    function runtimeDecisionTone(decision) {
        switch (String(decision || "")) {
        case "candidate_ready":
            return "#22C55E"
        case "shadow_only":
            return "#38BDF8"
        case "suppressed":
            return "#F59E0B"
        case "blocked":
            return "#F97316"
        default:
            return "#94A3B8"
        }
    }

    function runtimeDecisionLabel(decision) {
        switch (String(decision || "")) {
        case "candidate_ready":
            return "候选已就绪"
        case "shadow_only":
            return "仅影子评估"
        case "suppressed":
            return "重复动作已抑制"
        case "blocked":
            return "门禁阻断"
        default:
            return "等待评估"
        }
    }

    function runtimeGateSummary(evaluation) {
        var payload = evaluation || ({})
        return "环境 " + String(payload.marketEnvironmentGate || "pending")
            + " / 范围 " + String(payload.scopeGate || "pending")
            + " / 信号 " + String(payload.signalGate || "pending")
            + " / 执行 " + String(payload.executionGate || "pending")
    }

    function runtimeSessionSummary(evaluation) {
        var payload = evaluation || ({})
        var marketSession = payload.marketSession || ({})
        var runtimeSession = payload.runtimeSession || ({})
        var parts = []
        if (payload.marketSessionKnown) {
            parts.push("交易时段=" + (payload.marketSessionOpen ? "open" : "closed"))
            if (marketSession.sessionPhase) {
                parts.push("phase=" + marketSession.sessionPhase)
            }
        }
        if (payload.runtimeSessionKnown) {
            parts.push("runtime=" + String(runtimeSession.state || (payload.runtimeSessionReady ? "RUNNING" : "UNKNOWN")))
            parts.push("connected=" + (runtimeSession.connected ? "yes" : "no"))
        }
        return parts.join(" / ")
    }

    function autoExecutionStatusTone(status) {
        switch (String(status || "")) {
        case "submitted":
        case "risk_pending":
        case "pending":
        case "broker_pending":
        case "runtime_pending":
        case "broker_submitted":
            return "#38BDF8"
        case "partial_filled":
            return "#F59E0B"
        case "filled":
            return "#22C55E"
        case "risk_rejected":
        case "broker_rejected":
        case "execution_rule_reject":
        case "rejected":
        case "cancelled":
            return "#F97316"
        default:
            return "#94A3B8"
        }
    }

    function autoExecutionStatusLabel(status) {
        switch (String(status || "")) {
        case "submitted":
            return "已提交"
        case "risk_pending":
            return "等待风控"
        case "pending":
            return "等待处理"
        case "broker_pending":
            return "券商排队中"
        case "runtime_pending":
            return "运行时排队中"
        case "broker_submitted":
            return "已报券商"
        case "partial_filled":
            return "部分成交"
        case "filled":
            return "已成交"
        case "risk_rejected":
            return "风控拒绝"
        case "broker_rejected":
            return "券商拒绝"
        case "execution_rule_reject":
            return "执行规则阻断"
        case "rejected":
            return "已拒绝"
        case "cancelled":
            return "已撤销"
        default:
            return "--"
        }
    }

    function autoExecutionSummary(evaluation) {
        var payload = evaluation || ({})
        var status = String(payload.autoExecutionStatus || "").trim()
        if (status.length === 0) {
            return ""
        }

        var parts = ["自动执行 " + autoExecutionStatusLabel(status)]
        if (payload.autoExecutionOrderStatus) {
            parts.push("订单状态 " + String(payload.autoExecutionOrderStatus))
        }
        if (payload.autoExecutionStatusOrigin) {
            parts.push("来源 " + String(payload.autoExecutionStatusOrigin))
        }
        if (payload.autoExecutionFilledQuantity !== undefined && payload.autoExecutionFilledQuantity !== null && Number(payload.autoExecutionFilledQuantity) > 0) {
            parts.push("已成 " + String(payload.autoExecutionFilledQuantity))
        }
        if (payload.executionScopeId) {
            parts.push("执行域 " + String(payload.executionScopeId))
        }
        if (payload.batchId) {
            parts.push("批次 " + String(payload.batchId))
        }
        if (payload.autoExecutionMessage) {
            parts.push(String(payload.autoExecutionMessage))
        }
        return parts.join(" · ")
    }

    function formatPercent(value, decimals) {
        if (value === undefined || value === null || value === "") {
            return "--"
        }
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return String(value)
        }
        return (numericValue * 100).toFixed(decimals === undefined ? 2 : decimals) + "%"
    }

    function formatNumber(value, decimals) {
        if (value === undefined || value === null || value === "") {
            return "--"
        }
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return String(value)
        }
        return decimals === undefined ? String(numericValue) : numericValue.toFixed(decimals)
    }

    function symbolListText(value) {
        if (value === undefined || value === null || value === "") {
            return "--"
        }
        var list = []
        if (Array.isArray(value)) {
            list = value
        } else if (typeof value === "string") {
            list = String(value).split(/[,;\s，；]+/).filter(function(entry) { return String(entry).trim().length > 0 })
        } else {
            list = [value]
        }
        return list.length > 0 ? list.join(", ") : "--"
    }

    function ruleProfileSummary(source) {
        var payload = source || ({})
        return "最大仓位: " + formatPercent(payload.maxPositionPercent, 2)
            + "\n止损: " + formatPercent(payload.stopLossPercent, 2)
            + "\n止盈: " + formatPercent(payload.takeProfitPercent, 2)
            + "\n最大回撤: " + formatPercent(payload.maxDrawdownLimit, 2)
    }

    function executionPolicySummary(source) {
        var payload = source || ({})
        return "调仓周期: " + formatNumber(payload.rebalanceDays)
            + " 天\n仓位方法: " + String(payload.positionSizingMethod || "--")
    }

    function backtestAssumptionsSummary(source) {
        var payload = source || ({})
        return "初始资金: " + formatNumber(payload.initialCapital)
            + "\n佣金率: " + formatPercent(payload.commissionRate, 3)
            + "\n回测滑点: " + formatPercent(payload.slippageRate, 3)
    }

    function strategyScopeContextSummary(source) {
        var payload = source || ({})
        return "执行周期: " + String(payload.executionTimeframe || payload.execution_timeframe || "--")
            + "\n策略类型: " + String(payload.selectedStrategyType || "--")
            + "\n兜底标的池: " + symbolListText(payload.symbol_pool !== undefined ? payload.symbol_pool : payload.symbolPool)
    }

    function hasConfiguredDefaults() {
        var payload = configuredRuntimeRuleDefaults || ({})
        return Object.keys(payload).length > 0
    }

    function hasValue(value) {
        if (value === undefined || value === null) {
            return false
        }
        if (Array.isArray(value)) {
            return value.length > 0
        }
        if (typeof value === "string") {
            return String(value).trim().length > 0
        }
        return true
    }

    function normalizedComparable(value) {
        if (Array.isArray(value)) {
            var list = value.slice(0)
            list.sort(function(left, right) {
                return String(left).localeCompare(String(right))
            })
            return list
        }
        return value
    }

    function valuesEqual(left, right) {
        return JSON.stringify(normalizedComparable(left)) === JSON.stringify(normalizedComparable(right))
    }

    function sectionSpec(sectionKey) {
        switch (sectionKey) {
        case "ruleProfile":
            return {
                effectiveKey: "ruleProfile",
                snapshotKey: "ruleProfileSnapshot",
                parameterKey: "rule_profile",
                fields: [
                    { key: "maxPositionPercent", label: "最大仓位", format: "percent" },
                    { key: "stopLossPercent", label: "止损比例", format: "percent" },
                    { key: "takeProfitPercent", label: "止盈比例", format: "percent" },
                    { key: "maxDrawdownLimit", label: "最大回撤", format: "percent" }
                ]
            }
        case "executionPolicy":
            return {
                effectiveKey: "executionPolicy",
                snapshotKey: "executionPolicySnapshot",
                parameterKey: "execution_policy",
                fields: [
                    { key: "rebalanceDays", label: "调仓周期", format: "days" },
                    { key: "positionSizingMethod", label: "仓位方法", format: "text" }
                ]
            }
        case "backtestAssumptions":
            return {
                effectiveKey: "backtestAssumptions",
                snapshotKey: "backtestAssumptionsSnapshot",
                parameterKey: "backtest_assumptions",
                fields: [
                    { key: "initialCapital", label: "初始资金", format: "number" },
                    { key: "commissionRate", label: "佣金率", format: "percent3" },
                    { key: "slippageRate", label: "回测滑点", format: "percent3" }
                ]
            }
        case "strategyScopeContext":
            return {
                effectiveKey: "strategyScopeContext",
                snapshotKey: "strategyScopeContextSnapshot",
                parameterKey: "strategy_scope_context",
                fields: [
                    { key: "executionTimeframe", aliases: ["execution_timeframe"], fallbackKeys: ["execution_timeframe", "time_frame", "timeFrame"], label: "执行周期", format: "text" },
                    { key: "selectedStrategyType", fallbackKeys: ["strategy_type", "strategyType"], label: "策略类型", format: "text" },
                    { key: "selectedStrategySubtype", fallbackKeys: ["sub_type", "subType"], label: "策略子类", format: "text" },
                    { key: "symbol_pool", aliases: ["symbolPool"], fallbackKeys: ["symbol_pool", "symbolPool"], label: "兜底标的池", format: "symbols" }
                ]
            }
        default:
            return { effectiveKey: "", snapshotKey: "", parameterKey: "", fields: [] }
        }
    }

    function mapValue(map, fieldSpec) {
        if (!map) {
            return undefined
        }
        if (map[fieldSpec.key] !== undefined) {
            return map[fieldSpec.key]
        }
        var aliases = fieldSpec.aliases || []
        for (var index = 0; index < aliases.length; ++index) {
            if (map[aliases[index]] !== undefined) {
                return map[aliases[index]]
            }
        }
        return undefined
    }

    function fallbackValue(strategyObject, fieldSpec) {
        var strategy = strategyObject || ({})
        var parameters = strategy.parameters || ({})
        var fallbackKeys = fieldSpec.fallbackKeys || []
        for (var index = 0; index < fallbackKeys.length; ++index) {
            var key = fallbackKeys[index]
            if (parameters[key] !== undefined) {
                return parameters[key]
            }
            if (strategy[key] !== undefined) {
                return strategy[key]
            }
        }
        return undefined
    }

    function strategySectionMap(sectionKey) {
        var spec = sectionSpec(sectionKey)
        var strategy = highlightedStrategyDetail || ({})
        var snapshot = strategy[spec.snapshotKey] || ({})
        if (Object.keys(snapshot).length > 0) {
            return snapshot
        }
        var parameters = strategy.parameters || ({})
        return parameters[spec.parameterKey] || ({})
    }

    function configuredSectionMap(sectionKey) {
        return (configuredRuntimeRuleDefaults || ({}))[sectionKey] || ({})
    }

    function effectiveSectionMap(sectionKey) {
        var spec = sectionSpec(sectionKey)
        return (latestRuntimeRuleEvaluation || ({}))[spec.effectiveKey] || ({})
    }

    function formatFieldValue(fieldSpec, value) {
        switch (fieldSpec.format) {
        case "percent":
            return formatPercent(value, 2)
        case "percent3":
            return formatPercent(value, 3)
        case "days":
            return hasValue(value) ? (formatNumber(value) + " 天") : "--"
        case "symbols":
            return symbolListText(value)
        case "number":
            return formatNumber(value)
        default:
            return hasValue(value) ? String(value) : "--"
        }
    }

    function configuredDefaultGap(sectionKey, fieldSpec) {
        var strategyMap = strategySectionMap(sectionKey)
        var configuredMap = configuredSectionMap(sectionKey)
        var effectiveMap = effectiveSectionMap(sectionKey)
        var strategyValue = mapValue(strategyMap, fieldSpec)
        if (strategyValue === undefined) {
            strategyValue = fallbackValue(highlightedStrategyDetail, fieldSpec)
        }
        var configuredValue = mapValue(configuredMap, fieldSpec)
        var effectiveValue = mapValue(effectiveMap, fieldSpec)
        if (effectiveValue === undefined) {
            effectiveValue = fallbackValue(latestRuntimeRuleEvaluation, fieldSpec)
        }

        if (!hasValue(configuredValue)) {
            return null
        }

        if (hasValue(strategyValue) && hasValue(effectiveValue)
                && valuesEqual(strategyValue, effectiveValue)
                && !valuesEqual(configuredValue, effectiveValue)) {
            return {
                category: "策略原值覆盖",
                color: "#F59E0B",
                detail: "策略里已显式存在该字段，运行时优先使用策略原值，默认配置不会覆盖它"
            }
        }

        if (!hasValue(effectiveValue)) {
            if (sectionKey === "strategyScopeContext"
                    || fieldSpec.key === "symbol_pool"
                    || fieldSpec.key === "executionTimeframe"
                    || latestRuntimeRuleEvaluation.reason === "symbol_outside_scope"
                    || latestRuntimeRuleEvaluation.scopeGate === "blocked") {
                return {
                    category: "作用域门禁未接住",
                    color: "#EF4444",
                    detail: "默认作用域字段没有反映到最近一次评估里，当前 scope gate 可能仍在按旧范围或空范围判定"
                }
            }

            if (Object.keys(effectiveMap || {}).length === 0) {
                return {
                    category: "快照字段缺失",
                    color: "#EF4444",
                    detail: "最近一次评估结果没有带出这一类快照，默认值没有进入评估快照"
                }
            }

            return {
                category: "字段未带出",
                color: "#EF4444",
                detail: "评估结果里缺少这个字段，默认值没有进入最近一次运行时评估"
            }
        }

        if (!valuesEqual(configuredValue, effectiveValue)) {
            return {
                category: "运行时值不一致",
                color: "#F97316",
                detail: hasValue(strategyValue)
                    ? "实际生效值与当前默认配置不同，而且策略本身也有候选值，需要核对是否被旧快照或策略原值替换"
                    : "字段进入了评估，但实际值不是当前默认配置，可能被旧快照或运行时补全改写"
            }
        }

        return null
    }

    function fieldSource(sectionKey, fieldSpec) {
        var strategyMap = strategySectionMap(sectionKey)
        var configuredMap = configuredSectionMap(sectionKey)
        var effectiveMap = effectiveSectionMap(sectionKey)
        var strategyValue = mapValue(strategyMap, fieldSpec)
        if (strategyValue === undefined) {
            strategyValue = fallbackValue(highlightedStrategyDetail, fieldSpec)
        }
        var configuredValue = mapValue(configuredMap, fieldSpec)
        var effectiveValue = mapValue(effectiveMap, fieldSpec)

        var configuredGap = configuredDefaultGap(sectionKey, fieldSpec)
        if (configuredGap !== null && !hasValue(effectiveValue)) {
            return { label: configuredGap.category, color: configuredGap.color, detail: configuredGap.detail }
        }

        if (!hasValue(effectiveValue)) {
            return { label: "未生效", color: "#64748B", detail: "最近一次运行时评估里没有这个字段" }
        }
        if (hasValue(strategyValue) && valuesEqual(strategyValue, effectiveValue)) {
            if (configuredGap !== null) {
                return { label: configuredGap.category, color: configuredGap.color, detail: configuredGap.detail }
            }
            return { label: "策略原值", color: "#22C55E", detail: "字段已在策略中显式存在，运行时沿用了策略自身值" }
        }
        if (!hasValue(strategyValue) && hasValue(configuredValue) && valuesEqual(configuredValue, effectiveValue)) {
            return { label: "默认覆盖", color: "#38BDF8", detail: "策略原本缺这个字段，运行时用了交易配置里的默认值" }
        }
        if (!hasValue(strategyValue) && !hasValue(configuredValue) && hasValue(effectiveValue)) {
            return { label: "运行时派生", color: "#F59E0B", detail: "既不是策略显式值，也不是当前默认配置，通常来自运行时补全或旧快照" }
        }
        if (configuredGap !== null) {
            return { label: configuredGap.category, color: configuredGap.color, detail: configuredGap.detail }
        }
        return { label: "运行时调整", color: "#F97316", detail: "实际生效值和策略原值、默认值都不完全一致，需进一步核对来源" }
    }

    function unresolvedConfiguredDefaultRows() {
        var sections = ["ruleProfile", "executionPolicy", "backtestAssumptions", "strategyScopeContext"]
        var rows = []
        for (var sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
            var sectionKey = sections[sectionIndex]
            var spec = sectionSpec(sectionKey)
            for (var fieldIndex = 0; fieldIndex < spec.fields.length; ++fieldIndex) {
                var fieldSpec = spec.fields[fieldIndex]
                var configuredGap = configuredDefaultGap(sectionKey, fieldSpec)
                if (configuredGap !== null) {
                    var configuredValue = mapValue(configuredSectionMap(sectionKey), fieldSpec)
                    var effectiveValue = mapValue(effectiveSectionMap(sectionKey), fieldSpec)
                    if (effectiveValue === undefined) {
                        effectiveValue = fallbackValue(latestRuntimeRuleEvaluation, fieldSpec)
                    }
                    rows.push({
                        sectionLabel: spec.effectiveKey,
                        fieldLabel: fieldSpec.label,
                        configuredValueText: formatFieldValue(fieldSpec, configuredValue),
                        effectiveValueText: formatFieldValue(fieldSpec, effectiveValue),
                        issueCategory: configuredGap.category,
                        issueColor: configuredGap.color,
                        issueText: configuredGap.detail
                    })
                }
            }
        }
        return rows
    }

    function exportSnapshotText() {
        var lines = []
        lines.push("运行时规则来源快照")
        lines.push("策略: " + String(latestRuntimeRuleEvaluation.strategyName || highlightStrategyName || latestRuntimeRuleEvaluation.strategyId || highlightStrategyId || "--"))
        lines.push("决策: " + String(latestRuntimeRuleEvaluation.decision || "--"))
        lines.push("时间: " + String(latestRuntimeRuleEvaluation.observedAt || "--"))
        lines.push("标的: " + String(latestRuntimeRuleEvaluation.symbol || "--"))
        lines.push("门禁: " + runtimeGateSummary(latestRuntimeRuleEvaluation || ({})))
        var runtimeSummary = runtimeSessionSummary(latestRuntimeRuleEvaluation || ({}))
        if (runtimeSummary.length > 0) {
            lines.push("运行态: " + runtimeSummary)
        var autoExecution = autoExecutionSummary(latestRuntimeRuleEvaluation || ({}))
        if (autoExecution.length > 0)
            lines.push("自动执行: " + autoExecution)
        }
        lines.push("")

        var sections = ["ruleProfile", "executionPolicy", "backtestAssumptions", "strategyScopeContext"]
        for (var sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
            var sectionKey = sections[sectionIndex]
            lines.push("[" + sectionKey + "]")
            var rows = sectionFieldRows(sectionKey)
            for (var rowIndex = 0; rowIndex < rows.length; ++rowIndex) {
                var row = rows[rowIndex]
                lines.push("- " + row.label + ": " + row.valueText + " | 来源=" + row.sourceLabel + " | 说明=" + row.sourceDetail)
            }
            lines.push("")
        }

        var unresolvedRows = unresolvedConfiguredDefaultRows()
        lines.push("[默认未生效字段]")
        if (unresolvedRows.length === 0) {
            lines.push("- 无")
        } else {
            for (var unresolvedIndex = 0; unresolvedIndex < unresolvedRows.length; ++unresolvedIndex) {
                var unresolved = unresolvedRows[unresolvedIndex]
                lines.push("- " + unresolved.sectionLabel + "." + unresolved.fieldLabel
                    + " | 默认=" + unresolved.configuredValueText
                    + " | 生效=" + unresolved.effectiveValueText
                    + " | 分类=" + unresolved.issueCategory
                    + " | 问题=" + unresolved.issueText)
            }
        }

        return lines.join("\n")
    }

    function sectionFieldRows(sectionKey) {
        var spec = sectionSpec(sectionKey)
        var rows = []
        var effectiveMap = effectiveSectionMap(sectionKey)
        for (var index = 0; index < spec.fields.length; ++index) {
            var fieldSpec = spec.fields[index]
            var effectiveValue = mapValue(effectiveMap, fieldSpec)
            if (effectiveValue === undefined) {
                effectiveValue = fallbackValue(latestRuntimeRuleEvaluation, fieldSpec)
            }
            var source = fieldSource(sectionKey, fieldSpec)
            rows.push({
                label: fieldSpec.label,
                valueText: formatFieldValue(fieldSpec, effectiveValue),
                sourceLabel: source.label,
                sourceColor: source.color,
                sourceDetail: source.detail
            })
        }
        return rows
    }

    function refreshHighlightedStrategyDetail() {
        if (!strategyService || typeof strategyService.getStrategyById !== "function") {
            highlightedStrategyDetail = ({})
            return
        }

        var strategyId = String(observedStrategyId || "").trim()
        if (!strategyId) {
            highlightedStrategyDetail = ({})
            return
        }

        try {
            if (typeof strategyService.initialize === "function") {
                strategyService.initialize()
            }
            highlightedStrategyDetail = strategyService.getStrategyById(strategyId) || ({})
        } catch (error) {
            highlightedStrategyDetail = ({})
        }
    }

    onObservedStrategyIdChanged: refreshHighlightedStrategyDetail()

    Component.onCompleted: {
        if (strategyService && typeof strategyService.initialize === "function") {
            strategyService.initialize()
        }
        refreshHighlightedStrategyDetail()
    }

    Connections {
        target: strategyService
        function onStrategyRuntimeRuleEvaluated(evaluationData) {
            rememberRuntimeRuleEvaluation(evaluationData)
        }
    }

    ColumnLayout {
        id: runtimeRuleColumn
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "运行时规则观察窗"
                font.pixelSize: 20
                font.bold: true
                color: "#F8FAFC"
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "清空记录"
                enabled: runtimeRuleEvaluationFeed.length > 0
                onClicked: {
                    latestRuntimeRuleEvaluation = ({})
                    runtimeRuleEvaluationFeed = []
                }
            }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: highlightStrategyId
                ? ("当前重点观察已绑定策略 “" + (highlightStrategyName || highlightStrategyId) + "” 的运行时评估。这里只消费 StrategyService 的 shadow 评估信号，不会触发自动下单。")
                : "这里直接消费 StrategyService.strategyRuntimeRuleEvaluated，可用来检查市场环境、范围、信号、执行四道门禁的最新结果。"
            font.pixelSize: 13
            color: "#94A3B8"
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 980 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14

            Rectangle {
                Layout.fillWidth: true
                radius: 16
                color: "#111827"
                border.color: "#334155"
                border.width: 1
                implicitHeight: configuredDefaultsColumn.implicitHeight + 24

                ColumnLayout {
                    id: configuredDefaultsColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Text {
                        text: "配置中的默认覆盖"
                        font.pixelSize: 15
                        font.bold: true
                        color: "#F8FAFC"
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: hasConfiguredDefaults()
                        wrapMode: Text.WordWrap
                        text: "这些值来自当前交易连接配置里的 runtimeRuleDefaults，用来补齐策略未显式声明的运行时规则字段。"
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }

                    Text {
                        visible: !hasConfiguredDefaults()
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "当前交易配置里还没有 runtimeRuleDefaults。若策略自身未写完整规则，运行时就不会有额外默认覆盖。"
                        font.pixelSize: 12
                        color: "#64748B"
                    }

                    GridLayout {
                        visible: hasConfiguredDefaults()
                        Layout.fillWidth: true
                        columns: root.width > 1280 ? 2 : 1
                        columnSpacing: 12
                        rowSpacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0F172A"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: configuredRuleProfileColumn.implicitHeight + 18

                            ColumnLayout {
                                id: configuredRuleProfileColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6

                                Text { text: "ruleProfile"; font.pixelSize: 13; font.bold: true; color: "#E2E8F0" }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ruleProfileSummary((configuredRuntimeRuleDefaults || ({})).ruleProfile || ({})); font.pixelSize: 12; color: "#BAE6FD" }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0F172A"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: configuredExecutionColumn.implicitHeight + 18

                            ColumnLayout {
                                id: configuredExecutionColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6

                                Text { text: "executionPolicy"; font.pixelSize: 13; font.bold: true; color: "#E2E8F0" }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: executionPolicySummary((configuredRuntimeRuleDefaults || ({})).executionPolicy || ({})); font.pixelSize: 12; color: "#BAE6FD" }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0F172A"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: configuredBacktestColumn.implicitHeight + 18

                            ColumnLayout {
                                id: configuredBacktestColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6

                                Text { text: "backtestAssumptions"; font.pixelSize: 13; font.bold: true; color: "#E2E8F0" }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: backtestAssumptionsSummary((configuredRuntimeRuleDefaults || ({})).backtestAssumptions || ({})); font.pixelSize: 12; color: "#BAE6FD" }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0F172A"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: configuredScopeColumn.implicitHeight + 18

                            ColumnLayout {
                                id: configuredScopeColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6

                                Text { text: "strategyScopeContext"; font.pixelSize: 13; font.bold: true; color: "#E2E8F0" }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: strategyScopeContextSummary((configuredRuntimeRuleDefaults || ({})).strategyScopeContext || ({})); font.pixelSize: 12; color: "#BAE6FD" }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 16
                color: hasRuntimeRuleEvaluation ? "#111C34" : "#111827"
                border.color: hasRuntimeRuleEvaluation ? runtimeDecisionTone(latestRuntimeRuleDecision) : "#334155"
                border.width: 1
                implicitHeight: effectiveDefaultsColumn.implicitHeight + 24

                ColumnLayout {
                    id: effectiveDefaultsColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Text {
                        text: "最近一次实际生效快照"
                        font.pixelSize: 15
                        font.bold: true
                        color: "#F8FAFC"
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: hasRuntimeRuleEvaluation
                            ? "这里显示最近一次 strategyRuntimeRuleEvaluated 里真正参与评估的四类快照，适合核对默认覆盖是否已被吃进去。"
                            : "还没有收到运行时评估，因此暂时无法确认哪些默认值已经实际生效。"
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }

                    Text {
                        visible: hasRuntimeRuleEvaluation
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "标签说明：绿色=策略原值，蓝色=默认覆盖，黄色=运行时派生，橙色=运行时调整，红色=默认未生效。未生效原因会继续细分成策略原值覆盖、快照字段缺失、作用域门禁未接住、运行时值不一致。"
                        font.pixelSize: 12
                        color: "#93C5FD"
                    }

                    Rectangle {
                        visible: hasRuntimeRuleEvaluation && unresolvedConfiguredDefaultRows().length > 0
                        Layout.fillWidth: true
                        radius: 12
                        color: "#2A0F16"
                        border.color: "#EF4444"
                        border.width: 1
                        implicitHeight: unresolvedDefaultsColumn.implicitHeight + 18

                        ColumnLayout {
                            id: unresolvedDefaultsColumn
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 6

                            Text {
                                text: "默认已配置但未生效"
                                font.pixelSize: 13
                                font.bold: true
                                color: "#FECACA"
                            }

                            Repeater {
                                model: unresolvedConfiguredDefaultRows()

                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Rectangle {
                                        radius: 10
                                        color: modelData.issueColor
                                        implicitWidth: unresolvedIssueLabel.implicitWidth + 14
                                        implicitHeight: unresolvedIssueLabel.implicitHeight + 6

                                        Text {
                                            id: unresolvedIssueLabel
                                            anchors.centerIn: parent
                                            text: modelData.issueCategory
                                            font.pixelSize: 11
                                            font.bold: true
                                            color: "#0F172A"
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.sectionLabel + " · " + modelData.fieldLabel
                                            + " · 默认 " + modelData.configuredValueText
                                            + " · 实际 " + modelData.effectiveValueText
                                            + " · " + modelData.issueText
                                        font.pixelSize: 12
                                        color: "#FCA5A5"
                                    }
                                }
                            }
                        }
                    }

                    GridLayout {
                        visible: hasRuntimeRuleEvaluation
                        Layout.fillWidth: true
                        columns: root.width > 1280 ? 2 : 1
                        columnSpacing: 12
                        rowSpacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0F172A"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: effectiveRuleProfileColumn.implicitHeight + 18

                            ColumnLayout {
                                id: effectiveRuleProfileColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6

                                Text { text: "ruleProfile"; font.pixelSize: 13; font.bold: true; color: "#E2E8F0" }

                                Repeater {
                                    model: sectionFieldRows("ruleProfile")

                                    ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 3

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Text { text: modelData.label; font.pixelSize: 12; color: "#CBD5E1" }
                                            Item { Layout.fillWidth: true }
                                            Text { text: modelData.valueText; font.pixelSize: 12; font.bold: true; color: "#F8FAFC" }
                                            Rectangle {
                                                radius: 10
                                                color: modelData.sourceColor
                                                implicitWidth: sourceLabel.implicitWidth + 14
                                                implicitHeight: sourceLabel.implicitHeight + 6

                                                Text {
                                                    id: sourceLabel
                                                    anchors.centerIn: parent
                                                    text: modelData.sourceLabel
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                    color: "#0F172A"
                                                }
                                            }
                                        }

                                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.sourceDetail; font.pixelSize: 11; color: "#94A3B8" }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0F172A"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: effectiveExecutionColumn.implicitHeight + 18

                            ColumnLayout {
                                id: effectiveExecutionColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6

                                Text { text: "executionPolicy"; font.pixelSize: 13; font.bold: true; color: "#E2E8F0" }

                                Repeater {
                                    model: sectionFieldRows("executionPolicy")

                                    ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 3

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Text { text: modelData.label; font.pixelSize: 12; color: "#CBD5E1" }
                                            Item { Layout.fillWidth: true }
                                            Text { text: modelData.valueText; font.pixelSize: 12; font.bold: true; color: "#F8FAFC" }
                                            Rectangle {
                                                radius: 10
                                                color: modelData.sourceColor
                                                implicitWidth: executionSourceLabel.implicitWidth + 14
                                                implicitHeight: executionSourceLabel.implicitHeight + 6

                                                Text {
                                                    id: executionSourceLabel
                                                    anchors.centerIn: parent
                                                    text: modelData.sourceLabel
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                    color: "#0F172A"
                                                }
                                            }
                                        }

                                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.sourceDetail; font.pixelSize: 11; color: "#94A3B8" }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0F172A"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: effectiveBacktestColumn.implicitHeight + 18

                            ColumnLayout {
                                id: effectiveBacktestColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6

                                Text { text: "backtestAssumptions"; font.pixelSize: 13; font.bold: true; color: "#E2E8F0" }

                                Repeater {
                                    model: sectionFieldRows("backtestAssumptions")

                                    ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 3

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Text { text: modelData.label; font.pixelSize: 12; color: "#CBD5E1" }
                                            Item { Layout.fillWidth: true }
                                            Text { text: modelData.valueText; font.pixelSize: 12; font.bold: true; color: "#F8FAFC" }
                                            Rectangle {
                                                radius: 10
                                                color: modelData.sourceColor
                                                implicitWidth: backtestSourceLabel.implicitWidth + 14
                                                implicitHeight: backtestSourceLabel.implicitHeight + 6

                                                Text {
                                                    id: backtestSourceLabel
                                                    anchors.centerIn: parent
                                                    text: modelData.sourceLabel
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                    color: "#0F172A"
                                                }
                                            }
                                        }

                                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.sourceDetail; font.pixelSize: 11; color: "#94A3B8" }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0F172A"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: effectiveScopeColumn.implicitHeight + 18

                            ColumnLayout {
                                id: effectiveScopeColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6

                                Text { text: "strategyScopeContext"; font.pixelSize: 13; font.bold: true; color: "#E2E8F0" }

                                Repeater {
                                    model: sectionFieldRows("strategyScopeContext")

                                    ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 3

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Text { text: modelData.label; font.pixelSize: 12; color: "#CBD5E1" }
                                            Item { Layout.fillWidth: true }
                                            Text { text: modelData.valueText; font.pixelSize: 12; font.bold: true; color: "#F8FAFC" }
                                            Rectangle {
                                                radius: 10
                                                color: modelData.sourceColor
                                                implicitWidth: scopeSourceLabel.implicitWidth + 14
                                                implicitHeight: scopeSourceLabel.implicitHeight + 6

                                                Text {
                                                    id: scopeSourceLabel
                                                    anchors.centerIn: parent
                                                    text: modelData.sourceLabel
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                    color: "#0F172A"
                                                }
                                            }
                                        }

                                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.sourceDetail; font.pixelSize: 11; color: "#94A3B8" }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 16
            color: hasRuntimeRuleEvaluation ? "#111C34" : "#0B1220"
            border.color: hasRuntimeRuleEvaluation ? runtimeDecisionTone(latestRuntimeRuleDecision) : "#334155"
            border.width: 1
            implicitHeight: latestRuleColumn.implicitHeight + 24

            ColumnLayout {
                id: latestRuleColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: runtimeDecisionTone(latestRuntimeRuleDecision)
                    }

                    Text {
                        text: hasRuntimeRuleEvaluation
                            ? (runtimeDecisionLabel(latestRuntimeRuleDecision) + " · " + String(latestRuntimeRuleEvaluation.observedAt || ""))
                            : "等待第一条运行时规则评估"
                        font.pixelSize: 15
                        font.bold: true
                        color: "#F8FAFC"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        visible: hasRuntimeRuleEvaluation
                        text: String(latestRuntimeRuleEvaluation.strategyName || latestRuntimeRuleEvaluation.strategyId || "")
                        font.pixelSize: 12
                        color: "#BAE6FD"
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: hasRuntimeRuleEvaluation
                        ? ("标的 " + String(latestRuntimeRuleEvaluation.symbol || "--")
                           + "，动作 " + String(latestRuntimeRuleEvaluation.candidateAction || "--")
                           + "，原因 " + String(latestRuntimeRuleEvaluation.reason || "--"))
                        : "绑定策略并收到 trading.market.tick / trading.market.bar 后，这里会显示最新的规则评估结果。"
                    font.pixelSize: 13
                    color: "#E2E8F0"
                }

                Text {
                    Layout.fillWidth: true
                    visible: hasRuntimeRuleEvaluation
                    wrapMode: Text.WordWrap
                    text: runtimeGateSummary(latestRuntimeRuleEvaluation)
                    font.pixelSize: 12
                    color: "#93C5FD"
                }

                Text {
                    Layout.fillWidth: true
                    visible: hasRuntimeRuleEvaluation && runtimeSessionSummary(latestRuntimeRuleEvaluation).length > 0
                    wrapMode: Text.WordWrap
                    text: runtimeSessionSummary(latestRuntimeRuleEvaluation)
                    font.pixelSize: 12
                    color: "#94A3B8"
                }

                Text {
                    Layout.fillWidth: true
                    visible: hasRuntimeRuleEvaluation && autoExecutionSummary(latestRuntimeRuleEvaluation).length > 0
                    wrapMode: Text.WordWrap
                    text: autoExecutionSummary(latestRuntimeRuleEvaluation)
                    font.pixelSize: 12
                    color: autoExecutionStatusTone(latestRuntimeRuleEvaluation.autoExecutionStatus)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "最近记录"
                font.pixelSize: 14
                font.bold: true
                color: "#E2E8F0"
            }

            Repeater {
                model: runtimeRuleEvaluationFeed

                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    radius: 14
                    color: "#111827"
                    border.color: runtimeDecisionTone(modelData.decision)
                    border.width: 1
                    implicitHeight: feedEntryColumn.implicitHeight + 20

                    ColumnLayout {
                        id: feedEntryColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: runtimeDecisionLabel(modelData.decision)
                                font.pixelSize: 13
                                font.bold: true
                                color: "#F8FAFC"
                            }

                            Text {
                                text: String(modelData.observedAt || "")
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: String(modelData.symbol || "--")
                                font.pixelSize: 12
                                color: "#BAE6FD"
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: String(modelData.strategyName || modelData.strategyId || "未命名策略")
                                 + " · " + String(modelData.reason || "--")
                                 + " · " + String(modelData.candidateAction || "--")
                            font.pixelSize: 12
                            color: "#E2E8F0"
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: runtimeGateSummary(modelData)
                            font.pixelSize: 11
                            color: "#94A3B8"
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: autoExecutionSummary(modelData).length > 0
                            wrapMode: Text.WordWrap
                            text: autoExecutionSummary(modelData)
                            font.pixelSize: 11
                            color: autoExecutionStatusTone(modelData.autoExecutionStatus)
                        }
                    }
                }
            }

            Text {
                visible: runtimeRuleEvaluationFeed.length === 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "还没有收到运行时规则评估。若要触发，可先绑定策略并确保有真实或模拟的 trading.market.tick / trading.market.bar 进入 EventBus。"
                font.pixelSize: 12
                color: "#64748B"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 16
            color: "#111827"
            border.color: "#334155"
            border.width: 1
            implicitHeight: exportColumn.implicitHeight + 24

            ColumnLayout {
                id: exportColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "规则来源导出"
                        font.pixelSize: 15
                        font.bold: true
                        color: "#F8FAFC"
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "复制当前快照"
                        enabled: hasRuntimeRuleEvaluation
                        onClicked: {
                            runtimeRuleExportArea.selectAll()
                            runtimeRuleExportArea.copy()
                            runtimeRuleExportArea.deselect()
                            exportFeedbackMessage = "规则来源快照已复制，可直接粘贴到实盘核对记录"
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: hasRuntimeRuleEvaluation
                        ? "导出文本会带上字段来源和默认未生效告警，适合实盘前留档或发给开发排查。"
                        : "收到第一条运行时规则评估后，这里会自动生成可复制的规则来源快照。"
                    font.pixelSize: 12
                    color: "#94A3B8"
                }

                Text {
                    visible: exportFeedbackMessage.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: exportFeedbackMessage
                    font.pixelSize: 12
                    color: "#86EFAC"
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    radius: 12
                    color: "#0B1220"
                    border.color: "#1E293B"
                    border.width: 1

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 8

                        TextArea {
                            id: runtimeRuleExportArea
                            readOnly: true
                            selectByMouse: true
                            text: hasRuntimeRuleEvaluation ? exportSnapshotText() : "等待运行时规则评估后生成导出文本..."
                            wrapMode: TextEdit.NoWrap
                            color: "#CBD5E1"
                            selectionColor: "#1D4ED8"
                            selectedTextColor: "#F8FAFC"
                            font.pixelSize: 11
                            background: null
                        }
                    }
                }
            }
        }
    }
}
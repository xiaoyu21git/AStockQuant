import AStock.Bridge 1.0 as Bridge
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0 as ConsoleUiComponents
import "../../components/FactorWorkbench/Creation/components" as PluginComponents
import "../../utils/RiskBacktestMetaLoader.js" as RiskBacktestMeta
import "../../utils/NormalizeUtils.js" as NormalizeUtils
import "../../utils/ColorLabelMaps.js" as ColorLabelMaps
import "../../utils/DataAccessPatterns.js" as DataAccess
import "../../utils/PureUtils.js" as PureUtils
import "../../utils/DomainConstants.js" as DomainConstants


Item {
    id: riskConfigPage

    readonly property bool compactLayout: width < 1180
    readonly property bool narrowLayout: width < 900
    readonly property int pagePadding: narrowLayout ? 12 : 24
    readonly property int sectionSpacing: narrowLayout ? 18 : 24
    readonly property bool extraWideLayout: width >= 1500
    readonly property bool forceFourStatCards: width >= 1180
    readonly property bool forceTwoColumnSections: width >= 1180
    readonly property int controlValueWidth: narrowLayout ? 64 : 84
    readonly property int compactValueBoxWidth: narrowLayout ? 82 : 92
    readonly property int stepperButtonSize: 28
    readonly property int cardInnerPadding: narrowLayout ? 18 : 24
    readonly property int sectionHeaderHeight: 28
    readonly property int sectionIntroHeight: narrowLayout ? 34 : 36
    readonly property int cardRadius: 20
    readonly property int subPanelRadius: 16
    readonly property int compactGap: 16
    readonly property int subgroupGap: 10

    readonly property color pageBg: "#0F172A"
    readonly property color pageBgTop: "#111827"
    readonly property color pageText: "#E2E8F0"
    readonly property color secondaryText: "#94A3B8"
    readonly property color subtleText: "#64748B"
    readonly property color cardBg: "#1E293B"
    readonly property color elevatedCardBg: "#111827"
    readonly property color cardBorder: "#334155"
    readonly property color cardBorderSoft: "#273449"
    readonly property color cardShadow: "#30000000"
    readonly property color tabHover: "#1E293B"
    readonly property color tabTrack: "#0F172A"
    readonly property color primaryBlue: "#3B82F6"
    readonly property color primaryBlueHover: "#2563EB"
    readonly property color primaryBlueSoft: "#172554"
    readonly property color successGreen: "#10B981"
    readonly property color successSoft: "#0F2F22"
    readonly property color warningOrange: "#F97316"
    readonly property color warningSoft: "#3A2A10"
    readonly property color dangerRed: "#EF4444"
    readonly property color dangerSoft: "#3B1215"
    readonly property color progressBg: "#334155"
    readonly property color shadowColor: "#12000000"
    readonly property color headerStripBg: "#131F33"
    readonly property color insetPanelBg: "#132238"
    readonly property color insetPanelBorder: "#26486E"

    property var dynamicParamConfigs: []
    property var dynamicParamGroups: []
    property var dynamicParamValues: ({})
    property var pendingPersistedValues: ({})
    property var strategySnapshots: []
    property var activeRiskStrategy: ({})
    property var activeBacktestRecord: ({})
    property var localActionHistory: []
    property string focusedStrategyId: ""
    property var externalRiskContext: ({})
    property bool parametersLoaded: false
    readonly property var riskConfigService: Bridge.RiskConfigService
    readonly property var riskMonitorService: Bridge.RiskControlBridge
    readonly property var strategyService: null
    readonly property int loadedParamCount: dynamicParamConfigs.length
    readonly property real varUsagePercent: riskMonitorService ? riskMonitorService.varUsagePercent : 68
    readonly property real currentDrawdownPercent: riskMonitorService ? riskMonitorService.currentDrawdownPercent : -4.2
    readonly property real currentTotalExposurePercent: riskMonitorService ? riskMonitorService.currentTotalExposurePercent : riskSummary.maxTotalExposure
    readonly property real currentVarBudgetAmount: riskMonitorService ? riskMonitorService.varBudgetAmount : 0
    readonly property real currentEstimatedVarAmount: riskMonitorService ? riskMonitorService.estimatedVarAmount : 0
    property real varWarningPercent: 80
    property real orderSizeLimit: 100
    property real turnoverLimit: 5000
    property real slippageLimit: 0.2
    property real level1Breaker: 2
    property real level2Breaker: 5
    property real level3Breaker: 8
    property bool autoStopEnabled: true

    property var riskSummary: ({
        stopLossPercent: 10.0,
        takeProfitPercent: 20.0,
        maxDrawdownLimit: 12.0,
        maxPositionPercent: 15.0,
        maxTotalExposure: 67.0,
        maxIndustryExposure: 30.0,
        maxThemeExposure: 25.0,
        maxDailyLoss: -5.0,
        maxCorrelation: 70.0
    })

    property var monitorStats: []
    property var positionRisks: []
    property var alertItems: []
    property var historyItems: []
    property string positionRiskSource: "allocation"
    property string positionRiskSourceLabel: ""
    property bool riskServicesWarmupQueued: false

    function ensureRiskServicesReady() {
        if (riskConfigService && typeof riskConfigService.initialize === "function") {
            riskConfigService.initialize()
        }
        if (riskMonitorService && typeof riskMonitorService.initializeAsync === "function") {
            riskMonitorService.initializeAsync()
        } else if (riskMonitorService && typeof riskMonitorService.initialize === "function") {
            riskMonitorService.initialize()
        }
        if (strategyService && typeof strategyService.initializeAsync === "function") {
            strategyService.initializeAsync()
        } else if (strategyService && typeof strategyService.initialize === "function") {
            strategyService.initialize()
        }
    }

    function scheduleRiskServicesWarmup() {
        if (!visible || riskServicesWarmupQueued) {
            return
        }

        riskServicesWarmupQueued = true
        Qt.callLater(function() {
            riskServicesWarmupQueued = false
            if (!visible) {
                return
            }
            ensureRiskServicesReady()
            refreshRiskOverviewData()
        })
    }

    Component.onCompleted: {
        pendingPersistedValues = loadPersistedConfiguration()
        applyPersistedAuxiliaryConfiguration(pendingPersistedValues)
        if (visible) {
            scheduleRiskServicesWarmup()
        }
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }
        scheduleRiskServicesWarmup()
    }

    PluginComponents.ParamComponents {
        id: paramComponents

        Component.onCompleted: {
            if (typeof paramComponents.registerAllComponents === "function") {
                paramComponents.registerAllComponents()
            }
            riskConfigPage.initDynamicParams()
            paramLoadWatchdog.start()
        }
    }

    Timer {
        id: paramLoadWatchdog
        interval: 1500
        repeat: false
        onTriggered: {
            if (!riskConfigPage.parametersLoaded) {
                riskConfigPage.generateFallbackParamConfigs()
            }
        }
    }

    Connections {
        target: strategyService
        ignoreUnknownSignals: true

        function onStrategiesLoaded() {
            refreshRiskOverviewData()
        }

        function onDataChanged() {
            refreshRiskOverviewData()
        }

        function onInitializedChanged() {
            refreshRiskOverviewData()
        }
    }

    Connections {
        target: riskMonitorService
        ignoreUnknownSignals: true

        function onCurrentDrawdownPercentChanged() {
            alertItems = buildAlertItems(activeRiskStrategy, activeBacktestRecord, positionRisks)
        }

        function onVarUsagePercentChanged() {
            alertItems = buildAlertItems(activeRiskStrategy, activeBacktestRecord, positionRisks)
        }

        function onCurrentTotalExposurePercentChanged() {
            alertItems = buildAlertItems(activeRiskStrategy, activeBacktestRecord, positionRisks)
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: pageBgTop }
            GradientStop { position: 0.35; color: "#172033" }
            GradientStop { position: 1.0; color: pageBg }
        }
    }

    ScrollView {
            id: riskScrollView
            anchors.fill: parent
            anchors.margins: pagePadding
            clip: true
            contentWidth: availableWidth
            background: Rectangle {
                color: "transparent"
            }
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: Math.max(0, riskScrollView.availableWidth)
                spacing: sectionSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "风险控制模块"
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        color: pageText
                    }

                    Text {
                        text: "配置规则 → 实时监控 → 自动执行 → 复盘分析"
                        font.pixelSize: 14
                        color: secondaryText
                    }

                    Text {
                        visible: focusedStrategyId.length > 0
                        text: activeRiskStrategy && Object.keys(activeRiskStrategy).length > 0
                            ? ("当前焦点组合: " + resolveStrategyName(activeRiskStrategy) + " · 来自组合策略上下文")
                            : "当前焦点组合: 等待同步策略上下文"
                        font.pixelSize: 12
                        color: primaryBlue
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: forceFourStatCards ? 4 : (width >= 900 ? 2 : 1)
                    rowSpacing: compactGap
                    columnSpacing: compactGap

                    Repeater {
                        model: 4

                        delegate: Rectangle {
                            readonly property var statCardData: [
                                { label: "当前组合净值", value: "1.284", note: "今日 +0.32%", tone: pageText },
                                { label: "当前回撤", value: currentDrawdownPercent.toFixed(1) + "%", note: drawdownStatusText(), tone: drawdownToneColor() },
                                { label: "风险预算使用率", value: Math.round(varUsagePercent) + "%", note: riskBudgetUsageNote(), tone: varUsageToneColor() },
                                { label: "当前总仓位", value: currentTotalExposurePercent.toFixed(1) + "%", note: exposureUsageNote(), tone: pageText }
                            ][index] || ({ label: "", value: "", note: "", tone: pageText })
                            Layout.fillWidth: true
                            Layout.preferredHeight: 138
                            radius: subPanelRadius
                            color: cardBg
                            border.color: cardBorder
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: cardInnerPadding
                                spacing: 10

                                Text {
                                    text: statCardData.label
                                    font.pixelSize: 13
                                    color: secondaryText
                                }

                                Text {
                                    text: statCardData.value
                                    font.pixelSize: 32
                                    font.weight: Font.Bold
                                    font.family: "Consolas"
                                    color: statCardData.tone
                                }

                                Text {
                                    text: statCardData.note
                                    font.pixelSize: 12
                                    color: subtleText
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }

                RiskConfigCard {
                    id: riskConfigCardComponent
                    dynamicParamConfigs: riskConfigPage.dynamicParamConfigs
                    dynamicParamGroups: riskConfigPage.dynamicParamGroups
                    dynamicParamValues: riskConfigPage.dynamicParamValues
                    paramComponents: paramComponents
                    parametersLoaded: riskConfigPage.parametersLoaded
                    loadedParamCount: riskConfigPage.loadedParamCount
                    autoStopEnabled: riskConfigPage.autoStopEnabled
                    forceTwoColumnSections: riskConfigPage.forceTwoColumnSections
                    onSaveRequested: saveRiskConfiguration()
                    onApplyRequested: applyRiskConfiguration()
                    onResetRequested: resetRiskDefaults()
                    onAutoStopToggled: function(checked) { riskConfigPage.autoStopEnabled = checked }
                    onParamsChanged: function(newValues) {
                        riskConfigPage.dynamicParamValues = newValues
                        riskConfigPage.updateRiskSummary(newValues)
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: forceTwoColumnSections ? 2 : 1
                    rowSpacing: 24
                    columnSpacing: 24

                    PositionRiskPanel {
                        id: positionRiskPanel
                        positionRisks: riskConfigPage.positionRisks
                        activeRiskStrategy: riskConfigPage.activeRiskStrategy
                        maxPositionPercent: getConfigValue("maxPositionPercent", 15)
                    }

                    AlertPanel {
                        id: alertPanel
                        alertItems: riskConfigPage.alertItems
                        matchImplicitHeight: forceTwoColumnSections ? positionRiskPanel.implicitHeight : 0
                    }
                }
                RiskActionPanel {
                    id: riskActionPanel
                    historyItems: riskConfigPage.historyItems
                    onActionTriggered: function(action) { appendHistory("【操作】" + action + " 已执行") }
                }
            }
        }

    function resolveControlValue(control) {
        if (control.target === "dynamic") {
            return getConfigValue(control.key, control.fallback)
        }
        return riskConfigPage[control.key]
    }

    function applyControlValue(control, rawValue) {
        var numericValue = Number(rawValue)
        if (isNaN(numericValue)) {
            return
        }

        if (control.target === "dynamic") {
            setConfigValue(control.key, control.negativeStorage ? -numericValue : numericValue)
            return
        }

        riskConfigPage[control.key] = numericValue
    }

    function stepControlValue(control, direction) {
        var stepSize = Number(control.step !== undefined ? control.step : 1)
        if (isNaN(stepSize) || stepSize <= 0) {
            stepSize = 1
        }

        var nextValue = resolveControlValue(control) + stepSize * direction
        var minValue = Number(control.min)
        var maxValue = Number(control.max)
        if (!isNaN(minValue)) {
            nextValue = Math.max(minValue, nextValue)
        }
        if (!isNaN(maxValue)) {
            nextValue = Math.min(maxValue, nextValue)
        }

        var decimals = control.decimals !== undefined ? control.decimals : 0
        nextValue = Number(nextValue.toFixed(decimals))
        applyControlValue(control, nextValue)
    }

    function formatControlValue(control) {
        var numericValue = resolveControlValue(control)
        var decimals = control.decimals !== undefined ? control.decimals : 0
        var prefix = control.negativeDisplay ? "-" : ""
        return prefix + Number(numericValue).toFixed(decimals) + (control.suffix || "")
    }

    function drawdownStatusText() {
        var remaining = getConfigValue("maxDrawdownLimit", 12) - Math.abs(currentDrawdownPercent)
        if (remaining <= 0) {
            return "⚠ 已达止损线"
        }
        return "距止损线 " + remaining.toFixed(1) + "%"
    }

    function drawdownToneColor() {
        return Math.abs(currentDrawdownPercent) >= getConfigValue("maxDrawdownLimit", 12) ? dangerRed : warningOrange
    }

    function varUsageToneColor() {
        return varUsagePercent >= varWarningPercent ? dangerRed : pageText
    }

    function resetRiskDefaults() {
        varWarningPercent = 80
        orderSizeLimit = 100
        turnoverLimit = 5000
        slippageLimit = 0.2
        level1Breaker = 2
        level2Breaker = 5
        level3Breaker = 8
        autoStopEnabled = true

        generateFallbackParamConfigs()
        appendHistory("【操作】风控规则已恢复默认配置")
    }

    function appendHistory(message) {
        var timeString = Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
        var newHistory = [{ time: timeString, content: message }]
        for (var index = 0; index < localActionHistory.length && index < 7; ++index) {
            newHistory.push(localActionHistory[index])
        }
        localActionHistory = newHistory
        historyItems = buildHistoryItems(activeRiskStrategy)
    }

    function initDynamicParams() {
        generateDynamicParamConfigs()
    }

    function generateDynamicParamConfigs() {
        RiskBacktestMeta.loadMetaFile("qrc:/config/views/risk_backtest_params.json", function(meta) {
            if (meta) {
                dynamicParamConfigs = []
                var riskParamConfigs = RiskBacktestMeta.getParameterConfigs("risk", "qrc:/config/views/risk_backtest_params.json")

                riskParamConfigs.forEach(function(paramConfig) {
                    var config = {
                        id: paramConfig.id,
                        type: paramConfig.type,
                        label: paramConfig.label,
                        description: paramConfig.description,
                        default: paramConfig.default,
                        category: paramConfig.category,
                        group: paramConfig.group || paramConfig.category || "风险管理"
                    }

                    switch (paramConfig.type) {
                        case "slider":
                            config.min = paramConfig.min
                            config.max = paramConfig.max
                            config.step = paramConfig.step || 0.01
                            config.unit = paramConfig.unit || ""
                            config.decimals = paramConfig.decimals !== undefined
                                ? paramConfig.decimals
                                : ((config.step && config.step < 1) ? 4 : 0)
                            break
                        case "select":
                            config.type = "select"
                            config.options = paramConfig.options || []
                            config.multiple = paramConfig.multiple || false
                            break
                        case "toggle":
                            config.type = "toggle"
                            config.trueLabel = paramConfig.trueLabel || "是"
                            config.falseLabel = paramConfig.falseLabel || "否"
                            break
                    }

                    if (paramConfig.visibleWhen) {
                        config.visibleWhen = paramConfig.visibleWhen
                    }

                    dynamicParamConfigs.push(config)
                })

                dynamicParamConfigs = orderDynamicParamConfigs(dynamicParamConfigs)
                dynamicParamGroups = buildDynamicParamGroups(dynamicParamConfigs)

                if (riskConfigCardComponent.dynamicParamGenerator) {
                    riskConfigCardComponent.dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, dynamicParamGroups)
                }

                initDynamicValues()

                parametersLoaded = true
                restorePersistedConfiguration()
                updateRiskSummary(dynamicParamValues)
            } else {
                generateFallbackParamConfigs()
            }
        })
    }

    function generateFallbackParamConfigs() {
        dynamicParamConfigs = [
            { id: "stopLossPercent", type: "slider", label: "止损比例", description: "单个头寸的最大亏损比例", min: 1, max: 50, step: 0.5, default: 10, unit: "%", category: "risk", group: "基础风险控制" },
            { id: "takeProfitPercent", type: "slider", label: "止盈比例", description: "单个头寸的目标盈利比例", min: 5, max: 200, step: 1, default: 20, unit: "%", category: "risk", group: "基础风险控制" },
            { id: "maxDrawdownLimit", type: "slider", label: "最大回撤限制", description: "策略总体账户的最大允许回撤比例", min: 5, max: 50, step: 1, default: 12, unit: "%", category: "risk", group: "组合层风险" },
            { id: "maxPositionPercent", type: "slider", label: "单票集中度上限", description: "单一个股最大持仓比例", min: 5, max: 25, step: 1, default: 15, unit: "%", category: "position", group: "持仓层风险" },
            { id: "maxIndustryExposure", type: "slider", label: "单行业集中度上限", description: "同一行业总持仓比例限制", min: 15, max: 50, step: 5, default: 30, unit: "%", category: "industry", group: "持仓层风险" },
            { id: "maxDailyLoss", type: "slider", label: "单日最大亏损", description: "日内净值最大亏损限制", min: -15, max: -2, step: 1, default: -5, unit: "%", category: "account", group: "熔断机制" },
            { id: "maxTotalExposure", type: "slider", label: "最大总仓位", description: "所有持仓总市值占资金比例", min: 10, max: 100, step: 1, default: 67, unit: "%", category: "position", group: "组合层风险" },
            { id: "maxCorrelation", type: "slider", label: "最大持仓相关性", description: "持仓股票间最大允许相关性", min: 0, max: 100, step: 1, default: 70, unit: "%", category: "other", group: "其他配置" }
        ]

        dynamicParamConfigs = orderDynamicParamConfigs(dynamicParamConfigs)
        dynamicParamGroups = buildDynamicParamGroups(dynamicParamConfigs)

        if (riskConfigCardComponent.dynamicParamGenerator) {
            riskConfigCardComponent.dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, dynamicParamGroups)
        }

        initDynamicValues()
        parametersLoaded = true
        restorePersistedConfiguration()
    }

    function initDynamicValues() {
        var values = {}
        dynamicParamConfigs.forEach(function(config) {
            if (config.default !== undefined) {
                values[config.id] = config.default
            }
        })
        dynamicParamValues = values

        if (riskConfigCardComponent.dynamicParamGenerator) {
            riskConfigCardComponent.dynamicParamGenerator.setValues(values)
        }

        updateRiskSummary(values)
        restorePersistedConfiguration()
    }

    function cloneObject(source) {
        return PureUtils.cloneObject(source);
    }

    function auxiliaryRiskConfiguration() {
        return {
            varWarningPercent: varWarningPercent,
            orderSizeLimit: orderSizeLimit,
            turnoverLimit: turnoverLimit,
            slippageLimit: slippageLimit,
            level1Breaker: level1Breaker,
            level2Breaker: level2Breaker,
            level3Breaker: level3Breaker,
            autoStopEnabled: autoStopEnabled
        }
    }

    function normalizePositiveIntOrDefault(value, fallback) {
        return NormalizeUtils.normalizePositiveIntOrDefault(value, fallback);
    }

    function buildPersistedConfiguration() {
        var merged = cloneObject(dynamicParamValues)
        var auxiliaryValues = auxiliaryRiskConfiguration()
        for (var key in auxiliaryValues) {
            if (Object.prototype.hasOwnProperty.call(auxiliaryValues, key)) {
                merged[key] = auxiliaryValues[key]
            }
        }
        return merged
    }

    function applyPersistedAuxiliaryConfiguration(configuration) {
        if (!configuration || typeof configuration !== "object") {
            return
        }

        if (configuration.varWarningPercent !== undefined) {
            varWarningPercent = numberOrDefault(configuration.varWarningPercent, varWarningPercent)
        }
        if (configuration.orderSizeLimit !== undefined) {
            orderSizeLimit = numberOrDefault(configuration.orderSizeLimit, orderSizeLimit)
        }
        if (configuration.turnoverLimit !== undefined) {
            turnoverLimit = numberOrDefault(configuration.turnoverLimit, turnoverLimit)
        }
        if (configuration.slippageLimit !== undefined) {
            slippageLimit = numberOrDefault(configuration.slippageLimit, slippageLimit)
        }
        if (configuration.level1Breaker !== undefined) {
            level1Breaker = numberOrDefault(configuration.level1Breaker, level1Breaker)
        }
        if (configuration.level2Breaker !== undefined) {
            level2Breaker = numberOrDefault(configuration.level2Breaker, level2Breaker)
        }
        if (configuration.level3Breaker !== undefined) {
            level3Breaker = numberOrDefault(configuration.level3Breaker, level3Breaker)
        }
        if (configuration.autoStopEnabled !== undefined) {
            autoStopEnabled = Boolean(configuration.autoStopEnabled)
        }
    }

    function configurationHasValues(values) {
        return PureUtils.configurationHasValues(values);
    }

    function loadPersistedConfiguration() {
        if (!riskConfigService) {
            return {}
        }

        var savedConfig = {}
        if (typeof riskConfigService.loadCurrentConfiguration === "function") {
            savedConfig = riskConfigService.loadCurrentConfiguration()
        }
        if (!configurationHasValues(savedConfig) && typeof riskConfigService.loadAppliedConfiguration === "function") {
            savedConfig = riskConfigService.loadAppliedConfiguration()
        }
        return savedConfig || {}
    }

    function restorePersistedConfiguration() {
        if (!parametersLoaded || dynamicParamConfigs.length === 0) {
            return
        }

        if (!configurationHasValues(pendingPersistedValues)) {
            pendingPersistedValues = loadPersistedConfiguration()
        }
        if (!configurationHasValues(pendingPersistedValues)) {
            return
        }

        var restoredValues = cloneObject(dynamicParamValues)
        dynamicParamConfigs.forEach(function(config) {
            if (Object.prototype.hasOwnProperty.call(pendingPersistedValues, config.id)) {
                restoredValues[config.id] = pendingPersistedValues[config.id]
            }
        })

        dynamicParamValues = restoredValues
        if (riskConfigCardComponent.dynamicParamGenerator) {
            riskConfigCardComponent.dynamicParamGenerator.setValues(restoredValues)
        }
        applyPersistedAuxiliaryConfiguration(pendingPersistedValues)
        updateRiskSummary(restoredValues)
        pendingPersistedValues = ({})
    }

    function updateRiskSummary(values) {
        riskSummary = {
            stopLossPercent: normalizePercentValue(values.stopLossPercent, 10.0),
            takeProfitPercent: normalizePercentValue(values.takeProfitPercent, 20.0),
            maxDrawdownLimit: normalizePercentValue(values.maxDrawdownLimit, 12.0),
            maxPositionPercent: normalizePercentValue(values.maxPositionPercent, 15.0),
            maxTotalExposure: normalizePercentValue(values.maxTotalExposure, 67.0),
            maxIndustryExposure: normalizePercentValue(values.maxIndustryExposure, 30.0),
            maxThemeExposure: normalizePercentValue(values.maxThemeExposure, 25.0),
            maxDailyLoss: normalizeSignedPercentValue(values.maxDailyLoss, -5.0),
            maxCorrelation: normalizePercentValue(values.maxCorrelation, 70.0)
        }
    }

    function normalizePercentValue(value, fallback) {
        return NormalizeUtils.normalizePercentValue(value, fallback);
    }

    function normalizeSignedPercentValue(value, fallback) {
        return NormalizeUtils.normalizeSignedPercentValue(value, fallback);
    }

    function saveRiskConfiguration() {
        if (!riskConfigService || typeof riskConfigService.saveConfiguration !== "function") {
            return
        }
        var savedConfiguration = buildPersistedConfiguration()
        if (riskConfigService.saveConfiguration(savedConfiguration)) {
            pendingPersistedValues = cloneObject(savedConfiguration)
            appendHistory("【配置】风控规则已保存")
        }
    }

    function applyRiskConfiguration() {
        if (!riskConfigService || typeof riskConfigService.applyConfiguration !== "function") {
            return
        }
        var appliedConfiguration = buildPersistedConfiguration()
        if (riskConfigService.applyConfiguration(appliedConfiguration)) {
            pendingPersistedValues = cloneObject(appliedConfiguration)
            appendHistory("【配置】风控规则已应用到全局默认值")
        }
    }

    function getRiskLevelColor() {
        var riskScore = calculateRiskScore()
        if (riskScore < 3) {
            return successGreen
        }
        if (riskScore < 7) {
            return warningOrange
        }
        return dangerRed
    }

    function getRiskLevelText() {
        var riskScore = calculateRiskScore()
        if (riskScore < 3) {
            return "低风险"
        }
        if (riskScore < 7) {
            return "中风险"
        }
        return "高风险"
    }

    function calculateRiskScore() {
        var score = 0
        if (riskSummary.stopLossPercent < 5) score += 2
        else if (riskSummary.stopLossPercent < 10) score += 1
        if (riskSummary.takeProfitPercent > 30) score += 2
        else if (riskSummary.takeProfitPercent > 20) score += 1
        if (riskSummary.maxPositionPercent > 20) score += 2
        else if (riskSummary.maxPositionPercent > 15) score += 1
        if (riskSummary.maxTotalExposure > 90) score += 2
        else if (riskSummary.maxTotalExposure > 75) score += 1
        if (riskSummary.maxDailyLoss < -8) score += 2
        else if (riskSummary.maxDailyLoss < -5) score += 1
        return Math.min(score, 10)
    }

    // ═══════════════════════════════════════════════════════════════════
    // 辅助函数 — Phase 31c 重构时遗漏, 从 cb323dd 恢复
    // ═══════════════════════════════════════════════════════════════════

    function riskBudgetUsageNote() {
        if (currentVarBudgetAmount <= 0) {
            return "等待实时账户与风控预算"
        }
        return "预算 ¥" + Math.round(currentVarBudgetAmount).toLocaleString() + " · 估算占用 ¥" + Math.round(currentEstimatedVarAmount).toLocaleString()
    }

    function exposureUsageNote() {
        var maxExposure = getConfigValue("maxTotalExposure", 67)
        if (maxExposure <= 0) {
            return "未配置总仓位预算"
        }
        return "距上限剩余 " + Math.max(0, maxExposure - currentTotalExposurePercent).toFixed(1) + "%"
    }

    function numberOrDefault(value, fallback) {
        return PureUtils.numberOrDefault(value, fallback);
    }

    function parseTimestamp(value) {
        return PureUtils.parseTimestamp(value);
    }

    function getStrategyParameters(strategy) {
        return DataAccess.getStrategyParameters(strategy);
    }

    function getStrategyPerformance(strategy) {
        return DataAccess.getStrategyPerformance(strategy);
    }

    function getLatestBacktest(strategy) {
        return DataAccess.getLatestBacktest(strategy);
    }

    function getStrategyAdvancedOptions(strategy) {
        return DomainConstants.getStrategyAdvancedOptions(strategy);
    }

    function getBacktestHistory(strategy) {
        return DataAccess.getBacktestHistory(strategy);
    }

    function resolveStrategyName(strategy) {
        return DataAccess.resolveStrategyNameFromBacktest(strategy);
    }

    function resolveStrategyId(strategy) {
        return DataAccess.resolveStrategyId(strategy);
    }

    function normalizePercentFromRuntime(value) {
        return NormalizeUtils.normalizePercentFromRuntime(value);
    }

    function firstDefinedValue(source, keys) {
        return PureUtils.firstDefinedValue(source, keys);
    }

    function resolveStrategyConfigAliases(key) {
        return DomainConstants.resolveStrategyConfigAliases(key);
    }

    function hasBacktestRecord(strategy) {
        return DataAccess.hasBacktestRecord(strategy);
    }

    function isPortfolioStrategy(strategy) {
        var StrategyCreation5 = DomainConstants.StrategyCreation5;
        var storedTypeIndex = Number(strategy && strategy.strategyTypeIndex)
        return Number.isFinite(storedTypeIndex)
            && Math.floor(storedTypeIndex) === StrategyCreation5
    }

    function resolveExternalPortfolioStrategy() {
        var strategy = externalRiskContext && externalRiskContext.strategy
            ? externalRiskContext.strategy
            : ({})
        return isPortfolioStrategy(strategy) ? strategy : ({})
    }

    function resolveActiveBacktest(strategy) {
        if (externalRiskContext
                && externalRiskContext.latestBacktest
                && Object.keys(externalRiskContext.latestBacktest).length > 0
                && (!focusedStrategyId || String(externalRiskContext.strategyId || "") === String(resolveStrategyId(strategy) || focusedStrategyId))) {
            return externalRiskContext.latestBacktest
        }
        return getLatestBacktest(strategy)
    }

    function resolveFocusedStrategyConfigValue(key) {
        var strategy = activeRiskStrategy && Object.keys(activeRiskStrategy).length > 0
            ? activeRiskStrategy
            : resolveExternalPortfolioStrategy()
        if (!strategy || Object.keys(strategy).length === 0) {
            return undefined
        }

        var parameters = getStrategyParameters(strategy)
        var advancedOptions = getStrategyAdvancedOptions(strategy)
        var optimizationConfig = advancedOptions.optimization_config || ({})
        var latestBacktest = resolveActiveBacktest(strategy)
        var runtimeParameters = latestBacktest.runtimeParameters || ({})
        var runtimeConfig = parameters.backtest_runtime || strategy.backtest_runtime || ({})
        var aliases = resolveStrategyConfigAliases(key)
        var sources = [runtimeParameters, optimizationConfig, runtimeConfig, parameters, strategy]

        for (var index = 0; index < sources.length; ++index) {
            var value = firstDefinedValue(sources[index], aliases)
            if (value !== undefined) {
                return value
            }
        }

        return undefined
    }

    function getConfigValue(key, fallback) {
        var rawValue = resolveFocusedStrategyConfigValue(key)
        if (rawValue === undefined || rawValue === null || rawValue === "") {
            rawValue = dynamicParamValues[key]
        }
        if (rawValue === undefined || rawValue === null || rawValue === "") {
            return fallback
        }

        var numericValue = Number(rawValue)
        if (isNaN(numericValue)) {
            return fallback
        }

        return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
    }

    function preferredRiskParamGroups() {
        return DomainConstants.preferredRiskParamGroups();
    }

    function buildDynamicParamGroups(configs) {
        var configIdMap = ({})
        ;(configs || []).forEach(function(config) {
            if (config && config.id) {
                configIdMap[config.id] = true
            }
        })

        var groups = []
        preferredRiskParamGroups().forEach(function(group) {
            var resolvedParams = (group.params || []).filter(function(paramId) {
                return !!configIdMap[paramId]
            })

            if (resolvedParams.length === 0) {
                return
            }

            groups.push({
                id: group.id,
                name: group.name,
                description: group.description,
                minColumnWidth: group.minColumnWidth,
                maxColumns: group.maxColumns,
                params: resolvedParams
            })
        })

        return groups
    }

    function orderDynamicParamConfigs(configs) {
        var configMap = ({})
        var ordered = []
        var appended = ({})

        ;(configs || []).forEach(function(config) {
            if (config && config.id) {
                configMap[config.id] = config
            }
        })

        preferredRiskParamGroups().forEach(function(group) {
            ;(group.params || []).forEach(function(paramId) {
                if (!configMap[paramId] || appended[paramId]) {
                    return
                }

                appended[paramId] = true
                ordered.push(configMap[paramId])
            })
        })

        ;(configs || []).forEach(function(config) {
            if (!config || !config.id || appended[config.id]) {
                return
            }

            appended[config.id] = true
            ordered.push(config)
        })

        return ordered
    }

}

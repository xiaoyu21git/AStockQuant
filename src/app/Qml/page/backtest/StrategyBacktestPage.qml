// StrategyBacktestPage.qml
// 策略回测页面 - 动态参数版本
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as ConsoleUiComponents
import AStock.Bridge 1.0 as Bridge
import "../../components" as SharedComponents
import "../../utils/BacktestPerformanceAdapter.js" as BacktestPerformanceAdapter
import "../../utils/BacktestResultAdapter.js" as BacktestResultAdapter
import "../../utils/RiskBacktestMetaLoader.js" as RiskBacktestMeta
import "../../utils/StrategyStructureAdapter.js" as StructureAdapter

/**
 * 策略回测页面组件
 * 提供交易策略历史表现回测功能
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property var factorService: null
    property var strategyBacktestController: null
    property var strategyService: Bridge.StrategyService
    property var riskConfigService: Bridge.RiskConfigService
    property var riskBacktestMetaLoader: RiskBacktestMeta // qmllint disable unqualified
    property var structureAdapter: StructureAdapter // qmllint disable unqualified
    property string selectedStrategyId: ""
    property string selectedStrategyName: ""
    property var selectedStrategyData: ({})
    property var pendingBacktestConfig: ({})
    property var strategyOptions: []
    property bool syncingStrategySelection: false
    property bool syncingDataSourceSelection: false
    property string selectedStartDate: Qt.formatDate(new Date(new Date().getFullYear() - 3, new Date().getMonth(), new Date().getDate()), "yyyy-MM-dd")
    property string selectedEndDate: Qt.formatDate(new Date(), "yyyy-MM-dd")
    property var dataSourceOptions: [
        { value: "raw", label: "原始日线库" },
        { value: "cleaned", label: "清洗后日线" },
        { value: "cache", label: "最新缓存集" }
    ]
    property var universeOptions: [
        { value: "market", label: "全市场" },
        { value: "index", label: "指数成分股" }
    ]
    property var indexPoolOptions: [
        { value: "000300.SH", label: "沪深300" },
        { value: "000905.SH", label: "中证500" },
        { value: "000852.SH", label: "中证1000" }
    ]
    property var strategyUniverseModeOptions: [
        { value: "auto", label: "自动选择" },
        { value: "configured", label: "已保存回测池" },
        { value: "linked", label: "关联自选池" },
        { value: "merged", label: "合并去重" }
    ]
    property string selectedUniverseType: "market"
    property string selectedIndexSymbol: "000300.SH"
    property string selectedStrategyUniverseMode: "auto"
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    property string backtestUniverseSummary: ""
    
    // 回测结果
    property var backtestResult: ({})
    property var pendingStrategyPerformancePayload: ({})
    property string pendingStrategyCoverageSummary: ""
    property string pendingStrategyCoverageDecision: ""
    property var pendingStrategyPreviousBacktest: ({})

    Bridge.StrategyBacktestController {
        id: internalStrategyBacktestController

        onIsRunningChanged: function(isRunning) {
            root.isBacktesting = isRunning
        }

        onProgressChanged: function(progress) {
            root.backtestProgress = progress
        }

        onStatusChanged: function(status) {
            root.backtestStatus = status
        }

        onBacktestStarted: function(strategyId) {
            root.selectedStrategyId = strategyId
            root.isBacktesting = true
            root.backtestProgress = 0
            root.backtestStatus = root.backtestUniverseSummary.length > 0
                ? ("回测启动中... · 标的来源: " + root.backtestUniverseSummary)
                : "回测启动中..."
        }

        onBacktestCompleted: function(result) {
            root.isBacktesting = false
            root.backtestProgress = 100
            root.backtestStatus = root.backtestUniverseSummary.length > 0
                ? ("回测完成 · 标的来源: " + root.backtestUniverseSummary)
                : "回测完成"
            root.handleBacktestCompleted(result)
        }

        onBacktestFailed: function(error) {
            root.isBacktesting = false
            root.backtestStatus = error
        }

        onBacktestCancelled: {
            root.isBacktesting = false
            root.backtestProgress = 0
            root.backtestStatus = "已取消"
        }
    }
    
    // ============ 动态参数配置 ============
    
    // 动态参数生成器
    property var dynamicParamConfigs: []
    property var dynamicParamValues: ({})
    property bool parametersLoaded: false
    property alias parameterPanel: parameterPanel
    property alias strategyComboBox: parameterPanel.strategyComboBox
    property alias universeComboBox: parameterPanel.universeComboBox
    property alias indexPoolComboBox: parameterPanel.indexPoolComboBox
    property alias dataSourceComboBox: parameterPanel.dataSourceComboBox
    property alias startDatePicker: parameterPanel.startDatePicker
    property alias endDatePicker: parameterPanel.endDatePicker
    property alias dynamicParamGenerator: parameterPanel.dynamicParamGenerator
    property alias resultPanel: resultPanel
    
    // 插件化组件注册表
    ConsoleUiComponents.ParamComponents {
        id: paramComponents
        Component.onCompleted: {
            console.log("策略回测参数组件初始化完成")
            // 注册所有组件
            if (typeof paramComponents.registerAllComponents === 'function') {
                paramComponents.registerAllComponents()
            }
            // 初始化动态参数
            root.initDynamicParams()
        }
    }

    Timer {
        id: paramLoadWatchdog
        interval: 1500
        repeat: false
        onTriggered: {
            if (!root.parametersLoaded) {
                root.generateFallbackParamConfigs()
            }
        }
    }
    
    // ============ UI ============
    
    // 主布局
    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 24
        spacing: 0
        
        // 标题区域
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            spacing: 4
            
            Text {
                text: "📈 策略回测工作区（交易策略验证）"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            Text {
                text: "测试交易策略的盈利能力，评估策略的风险收益特征"
                font.pixelSize: 12
                color: "#94A3B8"
                wrapMode: Text.WordWrap
            }
            
            // 分隔线
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#334155"
                Layout.topMargin: 8
            }
        }
        
        // 滚动区域（主要内容）
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            // 隐藏滚动条样式
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            // 滚动区域内容
            ColumnLayout {
                id: contentLayout
                width: scrollView.width - 20  // 为滚动条留出空间
                spacing: 12
                
                // 策略配置面板（动态参数版本）
                ConsoleUiComponents.BacktestParameterPanel {
                    id: parameterPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: 520
                    strategyOptions: root.strategyOptions
                    universeOptions: root.universeOptions
                    indexPoolOptions: root.indexPoolOptions
                    dataSourceOptions: root.dataSourceOptions
                    selectedStrategyName: root.selectedStrategyName
                    selectedStrategyId: root.selectedStrategyId
                    selectedUniverseType: root.selectedUniverseType
                    selectedIndexSymbol: root.selectedIndexSymbol
                    selectedStartDate: root.selectedStartDate
                    selectedEndDate: root.selectedEndDate
                    dynamicParamConfigs: root.dynamicParamConfigs
                    dynamicParamValues: root.dynamicParamValues
                    parametersLoaded: root.parametersLoaded
                    syncingStrategySelection: root.syncingStrategySelection
                    syncingDataSourceSelection: root.syncingDataSourceSelection
                    paramRegistry: paramComponents
                    onStrategyOptionSelected: function(index, option) {
                        applyStrategySelection(option)
                    }
                    onUniverseOptionSelected: function(index, option) {
                        root.selectedUniverseType = option.value
                    }
                    onIndexOptionSelected: function(index, option) {
                        root.selectedIndexSymbol = option.value
                    }
                    onDataSourceOptionSelected: function(index, option) {
                        mergeDynamicParamValues({ dataSourceMode: option.value })
                        if (strategyBacktestController) {
                            strategyBacktestController.dataSourceMode = option.value
                        }
                    }
                    onStartDateSelected: function(dateText) {
                        root.selectedStartDate = dateText
                    }
                    onEndDateSelected: function(dateText) {
                        root.selectedEndDate = dateText
                    }
                    onDynamicParamsChanged: function(newValues) {
                        root.dynamicParamValues = newValues
                        syncDataSourceSelectionDisplay()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 156
                    radius: 12
                    color: "#162033"
                    border.width: 1
                    border.color: "#2B3A55"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "股票池覆盖与对比"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: "#F8FAFC"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: root.currentStrategyLatestBacktestRecord() && Object.keys(root.currentStrategyLatestBacktestRecord()).length > 0
                                    ? ("上一轮基线: " + (root.currentStrategyLatestBacktestRecord().recordedAt || "最近一次回测"))
                                    : "上一轮基线: 暂无"
                                font.pixelSize: 12
                                color: "#93C5FD"
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: [
                                    { title: "上一轮股票池", count: root.resolveBacktestRecordSymbolPool(root.currentStrategyLatestBacktestRecord()).length, accent: "#38BDF8" },
                                    { title: "本次候选池", count: root.resolveBacktestUniverseState().symbols.length, accent: "#34D399" },
                                    { title: "交集", count: root.intersectSymbolCollections(root.resolveBacktestRecordSymbolPool(root.currentStrategyLatestBacktestRecord()), root.resolveBacktestUniverseState().symbols).length, accent: "#F59E0B" }
                                ]

                                delegate: Rectangle {
                                    id: coverageCard
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    radius: 10
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: modelData.accent

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 3

                                        Text {
                                            text: coverageCard.modelData.title
                                            font.pixelSize: 11
                                            color: "#94A3B8"
                                            horizontalAlignment: Text.AlignHCenter
                                            width: parent.width
                                        }

                                        Text {
                                            text: coverageCard.modelData.count + " 只"
                                            font.pixelSize: 15
                                            font.weight: Font.DemiBold
                                            color: coverageCard.modelData.accent
                                            horizontalAlignment: Text.AlignHCenter
                                            width: parent.width
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.fillWidth: true
                                text: root.selectedUniverseType === "index"
                                    ? "指数模式下本次会按指数成分股执行；回测完成后仍会与上一轮基线比较，明显更优自动覆盖，接近时再确认。"
                                    : (root.buildStrategyLatestPoolComparisonText() + " 回测完成后，结果明显时自动覆盖上一轮基线，结果接近时再交给你选择是否覆盖。")
                                font.pixelSize: 11
                                color: "#94A3B8"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
                
                // 回测控制面板
                Rectangle {
                    id: controlPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: 84
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8
                        
                        // 回测控制
                        RowLayout {
                            spacing: 10
                            
                            // 回测按钮
                            Rectangle {
                                id: backtestButton
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 36
                                radius: 8
                                color: root.isBacktesting ? "#334155" : "#3B82F6"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: root.isBacktesting ? "⏸️" : "▶️"
                                        font.pixelSize: 13
                                        color: root.isBacktesting ? "#94A3B8" : "white"
                                    }
                                    
                                    Text {
                                        text: root.isBacktesting ? "回测中..." : "开始回测"
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        color: root.isBacktesting ? "#94A3B8" : "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: !root.isBacktesting
                                    onClicked: root.startBacktest()
                                }
                            }
                            
                            // 进度条
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 8
                                radius: 4
                                color: "#334155"
                                visible: root.isBacktesting
                                
                                Rectangle {
                                    width: parent.width * (root.backtestProgress / 100)
                                    height: parent.height
                                    radius: 4
                                    color: "#3B82F6"
                                }
                            }
                            
                            // 进度文本
                            Text {
                                text: root.isBacktesting ? root.backtestProgress + "%" : ""
                                font.pixelSize: 12
                                color: "#94A3B8"
                                visible: root.isBacktesting
                            }
                            
                            // 状态文本
                            Text {
                                text: root.backtestStatus
                                font.pixelSize: 12
                                color: root.isBacktesting ? "#F59E0B" : "#94A3B8"
                            }
                            
                            Item { Layout.fillWidth: true }
                        }
                        
                        // 提示信息
                        Text {
                            text: "点击开始回测按钮，系统将使用历史数据验证策略表现"
                            font.pixelSize: 9
                            color: "#64748B"
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
                
                ConsoleUiComponents.BacktestResultPanel {
                    id: resultPanel
                    backtestResult: root.backtestResult
                    isBacktesting: root.isBacktesting
                    strategyDisplayName: root.strategyComboBox.currentText || "未命名策略"
                    onOptimizationRequested: root.showOptimization()
                    onExportRequested: root.exportResults()
                }
            }
        }
    }
    
    // ============ 动态参数方法 ============
    
    // 初始化动态参数配置
    function initDynamicParams() {
        console.log("初始化策略回测动态参数配置")

        root.parametersLoaded = false
        paramLoadWatchdog.restart()
        
        // 生成动态参数配置
        root.generateDynamicParamConfigs()
    }

    function ensureDynamicParamsReady() {
        if (!root.parametersLoaded || root.dynamicParamConfigs.length === 0) {
            console.log("策略回测参数未就绪，重新初始化")
            root.generateDynamicParamConfigs()
            return
        }

        if (root.dynamicParamGenerator && root.dynamicParamGenerator.configsList.length === 0) {
            console.log("策略回测参数生成器为空，重新装载配置")
            root.dynamicParamGenerator.reloadConfigs(root.dynamicParamConfigs, [])
            root.dynamicParamGenerator.setValues(root.dynamicParamValues || {})
        }
    }

    function hasObjectData(value) {
        return value && Object.keys(value).length > 0
    }

    function normalizePositionSizingMethod(value) {
        if (value === undefined || value === null || value === "") {
            return value
        }

        if (typeof value === "number") {
            var numericMap = {
                1: "fixed",
                2: "kelly",
                3: "equalWeight",
                4: "riskParity"
            }
            return numericMap[value] || "fixed"
        }

        var textValue = String(value)
        var normalizedText = textValue.trim().toLowerCase()
        var textMap = {
            "1": "fixed",
            "2": "kelly",
            "3": "equalWeight",
            "4": "riskParity",
            "fixed": "fixed",
            "kelly": "kelly",
            "equalweight": "equalWeight",
            "riskparity": "riskParity"
        }
        return textMap[normalizedText] || textValue
    }

    function normalizeBenchmarkValue(value) {
        if (value === undefined || value === null || value === "") {
            return value
        }

        var textValue = String(value).trim()
        var benchmarkMap = {
            "沪深300": "000300.SH",
            "上证指数": "000001.SH",
            "深证成指": "399001.SZ",
            "中证500": "000905.SH",
            "000300.SH": "000300.SH",
            "000001.SH": "000001.SH",
            "399001.SZ": "399001.SZ",
            "000905.SH": "000905.SH"
        }

        return benchmarkMap[textValue] || textValue
    }

    function normalizePercentInput(value) {
        if (value === undefined || value === null || value === "") {
            return value
        }

        var numeric = Number(value)
        if (isNaN(numeric)) {
            return value
        }

        return numeric > 0 && numeric <= 1 ? numeric * 100 : numeric
    }

    function normalizeInitialCapitalInput(value) {
        if (value === undefined || value === null || value === "") {
            return value
        }

        var numeric = Number(value)
        if (isNaN(numeric)) {
            return value
        }

        if (numeric > 0 && numeric < 10000) {
            return numeric * 10000
        }

        return numeric
    }

    function mapBacktestYearsToPeriod(years) {
        var yearText = String(years || "3")
        if (yearText === "1") return "1year"
        if (yearText === "5") return "5year"
        if (yearText === "10") return "full"
        return "3year"
    }

    function firstDefinedValue(source, keys) {
        for (var index = 0; index < keys.length; ++index) {
            var key = keys[index]
            if (source[key] !== undefined && source[key] !== null && source[key] !== "") {
                return source[key]
            }
        }
        return undefined
    }

    function normalizeBacktestSessionConfig(config) {
        var normalized = ({})
        if (!config) {
            return normalized
        }

        var resolvedConfig = root.structureAdapter.resolveBacktestSessionView(config)

        if (resolvedConfig.startDate !== undefined && resolvedConfig.startDate !== null) normalized.startDate = String(resolvedConfig.startDate)
        if (resolvedConfig.endDate !== undefined && resolvedConfig.endDate !== null) normalized.endDate = String(resolvedConfig.endDate)
        if (resolvedConfig.initialCapital !== undefined && resolvedConfig.initialCapital !== null) normalized.initialCapital = normalizeInitialCapitalInput(resolvedConfig.initialCapital)
        if (resolvedConfig.dataSourceMode !== undefined && resolvedConfig.dataSourceMode !== null) normalized.dataSourceMode = String(resolvedConfig.dataSourceMode)
        if (resolvedConfig.backtestPeriod !== undefined && resolvedConfig.backtestPeriod !== null) {
            normalized.backtestPeriod = String(resolvedConfig.backtestPeriod)
        } else if (resolvedConfig.backtestYears !== undefined && resolvedConfig.backtestYears !== null) {
            normalized.backtestPeriod = mapBacktestYearsToPeriod(resolvedConfig.backtestYears)
        }
        if (resolvedConfig.benchmark !== undefined) normalized.benchmark = normalizeBenchmarkValue(resolvedConfig.benchmark)
        var commissionRate = firstDefinedValue(resolvedConfig, ["commissionRate", "transactionCost"])
        if (commissionRate !== undefined) normalized.commissionRate = Number(commissionRate)
        var slippageRate = firstDefinedValue(resolvedConfig, ["slippageRate", "slippageCost", "slippageLimit"])
        if (slippageRate !== undefined) normalized.slippageRate = Number(slippageRate)
        if (resolvedConfig.varWarningPercent !== undefined) normalized.varWarningPercent = Number(resolvedConfig.varWarningPercent)
        if (resolvedConfig.orderSizeLimit !== undefined) normalized.orderSizeLimit = Number(resolvedConfig.orderSizeLimit)
        if (resolvedConfig.turnoverLimit !== undefined) normalized.turnoverLimit = Number(resolvedConfig.turnoverLimit)
        if (resolvedConfig.slippageLimit !== undefined) normalized.slippageLimit = Number(resolvedConfig.slippageLimit)
        if (resolvedConfig.level1Breaker !== undefined) normalized.level1Breaker = Number(resolvedConfig.level1Breaker)
        if (resolvedConfig.level2Breaker !== undefined) normalized.level2Breaker = Number(resolvedConfig.level2Breaker)
        if (resolvedConfig.level3Breaker !== undefined) normalized.level3Breaker = Number(resolvedConfig.level3Breaker)
        if (resolvedConfig.autoStopEnabled !== undefined) normalized.autoStopEnabled = !!resolvedConfig.autoStopEnabled
        if (resolvedConfig.maxDrawdownLimit !== undefined) normalized.maxDrawdownLimit = normalizePercentInput(resolvedConfig.maxDrawdownLimit)
        if (resolvedConfig.positionSizingMethod !== undefined) normalized.positionSizingMethod = normalizePositionSizingMethod(resolvedConfig.positionSizingMethod)
        if (resolvedConfig.maxPositionPercent !== undefined) normalized.maxPositionPercent = normalizePercentInput(resolvedConfig.maxPositionPercent)
        if (resolvedConfig.stopLossPercent !== undefined) normalized.stopLossPercent = normalizePercentInput(resolvedConfig.stopLossPercent)
        if (resolvedConfig.takeProfitPercent !== undefined) normalized.takeProfitPercent = normalizePercentInput(resolvedConfig.takeProfitPercent)
        if (resolvedConfig.rebalanceDays !== undefined) normalized.rebalanceDays = Number(resolvedConfig.rebalanceDays)
        if (resolvedConfig.enableAdvancedOptions !== undefined) normalized.enableAdvancedOptions = !!resolvedConfig.enableAdvancedOptions
        if (resolvedConfig.enableWalkForward !== undefined) normalized.enableWalkForward = !!resolvedConfig.enableWalkForward
        if (resolvedConfig.enableMonteCarlo !== undefined) normalized.enableMonteCarlo = !!resolvedConfig.enableMonteCarlo
        if (resolvedConfig.monteCarloSamples !== undefined) normalized.monteCarloSamples = Number(resolvedConfig.monteCarloSamples)
        if (resolvedConfig.enableOutOfSample !== undefined) normalized.enableOutOfSample = !!resolvedConfig.enableOutOfSample
        if (resolvedConfig.outOfSampleRatio !== undefined) normalized.outOfSampleRatio = Number(resolvedConfig.outOfSampleRatio)

        return normalized
    }

    function loadAppliedRiskBacktestDefaults() {
        if (!riskConfigService || typeof riskConfigService.loadAppliedConfiguration !== "function") {
            return ({})
        }

        return normalizeBacktestSessionConfig(riskConfigService.loadAppliedConfiguration())
    }

    function buildBaseDynamicParamValues() {
        var values = {}
        dynamicParamConfigs.forEach(function(config) {
            if (config.default !== undefined) {
                values[config.id] = config.default
            }
        })

        var appliedRiskDefaults = loadAppliedRiskBacktestDefaults()
        for (var key in appliedRiskDefaults) {
            values[key] = appliedRiskDefaults[key]
        }

        return values
    }

    function mergeDynamicParamValues(overrides) {
        if (!hasObjectData(overrides)) {
            return
        }

        var nextValues = JSON.parse(JSON.stringify(dynamicParamValues || {}))
        for (var key in overrides) {
            nextValues[key] = overrides[key]
        }

        dynamicParamValues = nextValues
        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValues(nextValues)
        }

        syncDataSourceSelectionDisplay()
    }

    function buildSupplementalBacktestConfigs() {
        return [
            {
                id: "benchmark",
                type: "select",
                label: "基准指数",
                description: "本次回测用于对比的基准指数",
                options: [
                    { value: "000300.SH", label: "沪深300" },
                    { value: "000001.SH", label: "上证指数" },
                    { value: "399001.SZ", label: "深证成指" },
                    { value: "000905.SH", label: "中证500" }
                ],
                default: "000300.SH",
                category: "benchmark",
                group: "回测设置"
            },
            {
                id: "enableAdvancedOptions",
                type: "toggle",
                label: "启用高级选项",
                description: "是否启用滚动优化、蒙特卡洛、样本外测试等高级能力",
                default: false,
                category: "advanced",
                group: "高级选项"
            },
            {
                id: "enableWalkForward",
                type: "toggle",
                label: "滚动窗口优化",
                description: "是否启用滚动窗口优化",
                default: false,
                category: "advanced",
                group: "高级选项",
                visibleWhen: "enableAdvancedOptions == true"
            },
            {
                id: "enableMonteCarlo",
                type: "toggle",
                label: "蒙特卡洛模拟",
                description: "是否启用蒙特卡洛模拟",
                default: false,
                category: "advanced",
                group: "高级选项",
                visibleWhen: "enableAdvancedOptions == true"
            },
            {
                id: "monteCarloSamples",
                type: "slider",
                label: "蒙特卡洛样本数",
                description: "蒙特卡洛模拟抽样次数",
                min: 100,
                max: 10000,
                step: 100,
                default: 1000,
                unit: "次",
                category: "advanced",
                group: "高级选项",
                visibleWhen: "enableMonteCarlo == true"
            },
            {
                id: "enableOutOfSample",
                type: "toggle",
                label: "样本外测试",
                description: "是否启用样本外测试",
                default: false,
                category: "advanced",
                group: "高级选项",
                visibleWhen: "enableAdvancedOptions == true"
            },
            {
                id: "outOfSampleRatio",
                type: "slider",
                label: "样本外比例",
                description: "样本外测试占总样本的比例",
                min: 0.1,
                max: 0.5,
                step: 0.05,
                default: 0.3,
                decimals: 2,
                unit: "",
                category: "advanced",
                group: "高级选项",
                visibleWhen: "enableOutOfSample == true"
            }
        ]
    }

    function mergeSupplementalBacktestConfigs(configs) {
        var merged = (configs || []).slice()
        var existingIds = ({})
        merged.forEach(function(config) {
            existingIds[config.id] = true
        })

        buildSupplementalBacktestConfigs().forEach(function(config) {
            if (!existingIds[config.id]) {
                merged.push(config)
            }
        })

        return merged
    }

    function sanitizeDynamicParamConfigs(configs) {
        var sanitized = []
        var configMap = ({})
        ;(configs || []).forEach(function(config) {
            if (!config || !config.id) {
                return
            }
            configMap[config.id] = config
        })

        var canonicalAliasMap = {
            backtestYears: "backtestPeriod",
            positionPercent: "maxPositionPercent",
            maxPositionPercent: "positionSize",
            transactionCost: "commissionRate",
            slippageCost: "slippageRate",
            rebalancingPeriod: "rebalanceDays",
            rebalanceDays: "rebalanceDays"
        }
        var excludedIds = {
            backtestPeriod: true,
            backtestYears: true,
            maxPositionPercent: true,
            positionPercent: true,
            rebalanceDays: true,
            rebalancingPeriod: true
        }

        ;(configs || []).forEach(function(config) {
            if (!config || !config.id) {
                return
            }

            if (excludedIds[config.id]) {
                return
            }

            var canonicalId = canonicalAliasMap[config.id]
            if (canonicalId && configMap[canonicalId]) {
                return
            }

            if (!sanitized.some(function(item) { return item.id === config.id })) {
                sanitized.push(config)
            }
        })

        return sanitized
    }

    function applyBacktestSessionConfig(config) {
        pendingBacktestConfig = JSON.parse(JSON.stringify(config || {}))
        if (pendingBacktestConfig.startDate) {
            selectedStartDate = String(pendingBacktestConfig.startDate)
        }
        if (pendingBacktestConfig.endDate) {
            selectedEndDate = String(pendingBacktestConfig.endDate)
        }
        mergeDynamicParamValues(normalizeBacktestSessionConfig(pendingBacktestConfig))
    }

    function appendConfiguredSymbolCollection(target, seenSymbols, rawCollection) {
        var appendSymbol = function(rawSymbol) {
            var symbol = String(rawSymbol || "").trim()
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

    function resolveConfiguredSymbolPool() {
        return resolveConfiguredSymbolPoolState().symbols
    }

    function buildConfiguredUniverseResolution(symbols, sourceKey, sourceLabel) {
        return {
            symbols: symbols.slice(),
            sourceKey: sourceKey,
            sourceLabel: sourceLabel,
            count: symbols.length
        }
    }

    function intersectSymbolCollections(primarySymbols, compareSymbols) {
        var compareSet = ({})
        var intersection = []
        for (var index = 0; index < compareSymbols.length; ++index) {
            compareSet[String(compareSymbols[index] || "")] = true
        }

        for (var primaryIndex = 0; primaryIndex < primarySymbols.length; ++primaryIndex) {
            var symbol = String(primarySymbols[primaryIndex] || "")
            if (symbol && compareSet[symbol] && intersection.indexOf(symbol) === -1) {
                intersection.push(symbol)
            }
        }

        return intersection
    }

    function subtractSymbolCollections(primarySymbols, compareSymbols) {
        var compareSet = ({})
        var difference = []
        for (var index = 0; index < compareSymbols.length; ++index) {
            compareSet[String(compareSymbols[index] || "")] = true
        }

        for (var primaryIndex = 0; primaryIndex < primarySymbols.length; ++primaryIndex) {
            var symbol = String(primarySymbols[primaryIndex] || "")
            if (symbol && !compareSet[symbol] && difference.indexOf(symbol) === -1) {
                difference.push(symbol)
            }
        }

        return difference
    }

    function resolveConfiguredSymbolPoolState() {
        var appendFromConfig = function(config, sourceKey, sourceLabel) {
            if (!config) {
                return null
            }

            var symbols = sourceKey === "selectedStrategy"
                ? root.structureAdapter.resolvePersistedStrategySymbolPool(config)
                : root.structureAdapter.resolveSymbolPool(config)

            if (symbols.length > 0) {
                return buildConfiguredUniverseResolution(symbols, sourceKey, sourceLabel)
            }

            return null
        }

        var pendingState = appendFromConfig(pendingBacktestConfig, "pendingBacktestConfig", "待启动回测配置")
        if (pendingState) {
            return pendingState
        }

        var strategyState = appendFromConfig(selectedStrategyData, "selectedStrategy", "已保存策略")
        if (strategyState) {
            return strategyState
        }

        return buildConfiguredUniverseResolution([], "unresolved", "未命中")
    }

    function resolveLinkedStockPoolState() {
        var resolvedSymbols = []
        var seenSymbols = ({})

        var appendFromConfig = function(config, sourceKey, sourceLabel) {
            if (!config) {
                return null
            }

            var parameters = config.parameters || ({})
            var beforeCount = resolvedSymbols.length
            appendConfiguredSymbolCollection(resolvedSymbols, seenSymbols, config.linked_stock_pool_symbols)
            appendConfiguredSymbolCollection(resolvedSymbols, seenSymbols, config.linkedStockPoolSymbols)
            appendConfiguredSymbolCollection(resolvedSymbols, seenSymbols, parameters.linked_stock_pool_symbols)
            appendConfiguredSymbolCollection(resolvedSymbols, seenSymbols, parameters.linkedStockPoolSymbols)

            if (resolvedSymbols.length > beforeCount) {
                var poolName = String(
                    parameters.linked_stock_pool_name
                    || parameters.linkedStockPoolName
                    || config.linked_stock_pool_name
                    || config.linkedStockPoolName
                    || "关联自选池")
                return buildConfiguredUniverseResolution(
                    resolvedSymbols,
                    sourceKey,
                    sourceLabel + "（" + poolName + "）")
            }

            return null
        }

        var pendingState = appendFromConfig(pendingBacktestConfig, "pendingLinkedStockPool", "待启动回测配置")
        if (pendingState) {
            return pendingState
        }

        var strategyState = appendFromConfig(selectedStrategyData, "selectedStrategyLinkedStockPool", "关联自选池")
        if (strategyState) {
            return strategyState
        }

        return buildConfiguredUniverseResolution([], "unresolvedLinkedStockPool", "未绑定关联自选池")
    }

    function resolveMergedStrategyUniverseState() {
        var configuredState = resolveConfiguredSymbolPoolState()
        var linkedState = resolveLinkedStockPoolState()
        var mergedSymbols = []
        var seenSymbols = ({})

        appendConfiguredSymbolCollection(mergedSymbols, seenSymbols, configuredState.symbols)
        appendConfiguredSymbolCollection(mergedSymbols, seenSymbols, linkedState.symbols)
        return buildConfiguredUniverseResolution(mergedSymbols, "mergedStrategyUniverse", "回测池 + 关联自选池")
    }

    function resolveBacktestUniverseCandidates() {
        return {
            configuredState: resolveConfiguredSymbolPoolState(),
            linkedState: resolveLinkedStockPoolState(),
            mergedState: resolveMergedStrategyUniverseState()
        }
    }

    function buildStrategyUniverseComparisonText() {
        var candidates = resolveBacktestUniverseCandidates()
        var configuredSymbols = candidates.configuredState.symbols
        var linkedSymbols = candidates.linkedState.symbols

        if (configuredSymbols.length === 0 && linkedSymbols.length === 0) {
            return "当前策略没有已保存回测池，也没有关联自选池，将回退到当前数据源可用股票。"
        }

        if (configuredSymbols.length === 0) {
            return "当前仅存在关联自选池，可直接用它覆盖本次回测股票池。"
        }

        if (linkedSymbols.length === 0) {
            return "当前仅存在已保存回测池，可直接按历史回测股票池执行。"
        }

        var intersection = intersectSymbolCollections(configuredSymbols, linkedSymbols)
        var configuredOnly = subtractSymbolCollections(configuredSymbols, linkedSymbols)
        var linkedOnly = subtractSymbolCollections(linkedSymbols, configuredSymbols)
        return "已保存回测池 " + configuredSymbols.length
            + " 只，关联自选池 " + linkedSymbols.length
            + " 只，交集 " + intersection.length
            + " 只，回测池独有 " + configuredOnly.length
            + " 只，自选池独有 " + linkedOnly.length + " 只。"
    }

    function resolveBacktestUniverseState() {
        if (selectedUniverseType === "index") {
            return {
                symbols: [],
                sourceKey: "indexUniverse",
                sourceLabel: "指数成分股（" + getIndexPoolLabel(selectedIndexSymbol) + "）",
                count: 0
            }
        }

        var candidates = resolveBacktestUniverseCandidates()
        if (selectedStrategyUniverseMode === "configured") {
            return candidates.configuredState.count > 0
                ? candidates.configuredState
                : buildConfiguredUniverseResolution([], "selectedStrategy", "已保存回测池为空")
        }

        if (selectedStrategyUniverseMode === "linked") {
            return candidates.linkedState.count > 0
                ? candidates.linkedState
                : buildConfiguredUniverseResolution([], "selectedLinkedStockPool", "关联自选池为空")
        }

        if (selectedStrategyUniverseMode === "merged") {
            return candidates.mergedState.count > 0
                ? candidates.mergedState
                : buildConfiguredUniverseResolution([], "selectedMergedUniverse", "合并股票池为空")
        }

        if (candidates.configuredState.count > 0) {
            return candidates.configuredState
        }

        if (candidates.linkedState.count > 0) {
            return candidates.linkedState
        }

        var availableSymbols = strategyBacktestController ? (strategyBacktestController.getAvailableSymbols("") || []) : []
        return buildConfiguredUniverseResolution(availableSymbols, "availableSymbols", "当前数据源可用股票")
    }

    function buildBacktestUniverseSummary(universeState) {
        if (!universeState) {
            return ""
        }

        if (universeState.sourceKey === "indexUniverse") {
            return universeState.sourceLabel
        }

        return universeState.sourceLabel + "（" + universeState.count + " 只）"
    }

    function syncDataSourceSelectionDisplay() {
        if (!dataSourceComboBox) {
            return
        }

        var currentMode = String(dynamicParamValues.dataSourceMode || "raw")
        syncingDataSourceSelection = true
        var matchIndex = 0
        for (var index = 0; index < dataSourceOptions.length; ++index) {
            if (dataSourceOptions[index].value === currentMode) {
                matchIndex = index
                break
            }
        }
        dataSourceComboBox.currentIndex = matchIndex
        syncingDataSourceSelection = false
    }

    function applyStrategySelection(option) {
        if (!option) {
            return
        }

        selectedStrategyId = option.strategyId || ""
        selectedStrategyName = option.strategyName || ""

        if (strategyBacktestController) {
            strategyBacktestController.selectedStrategyId = selectedStrategyId
        }

        selectedStrategyData = loadSelectedStrategyData()
        applyStrategyDefaults(selectedStrategyData)

        if (hasObjectData(pendingBacktestConfig)) {
            mergeDynamicParamValues(normalizeBacktestSessionConfig(pendingBacktestConfig))
        }
    }

    function upsertStrategyOption(strategyId, strategyName) {
        var normalizedId = String(strategyId || "")
        if (!normalizedId) {
            return null
        }

        var normalizedName = String(strategyName || normalizedId || "未命名策略")
        var nextOptions = (strategyOptions || []).slice()
        var matchIndex = -1
        for (var index = 0; index < nextOptions.length; ++index) {
            if (String(nextOptions[index].strategyId || "") === normalizedId) {
                matchIndex = index
                break
            }
        }

        var option = {
            strategyId: normalizedId,
            strategyName: normalizedName,
            displayText: normalizedName
        }

        if (matchIndex >= 0) {
            nextOptions[matchIndex] = option
        } else {
            nextOptions.unshift(option)
        }

        strategyOptions = nextOptions
        return option
    }

    function refreshStrategyOptions() {
        var nextOptions = []
        if (strategyService && strategyService.getAllStrategies && strategyService.isInitialized) {
            var strategies = strategyService.getAllStrategies() || []
            for (var index = 0; index < strategies.length; ++index) {
                var strategy = strategies[index] || ({})
                var strategyId = String(strategy.strategy_id || strategy.strategyId || "")
                var strategyName = String(strategy.strategy_name || strategy.strategyName || strategy.name || strategyId || "未命名策略")
                nextOptions.push({
                    strategyId: strategyId,
                    strategyName: strategyName,
                    displayText: strategyName
                })
            }
        }

        strategyOptions = nextOptions
        syncStrategySelectionDisplay()

        if (strategyOptions.length > 0) {
            var selectedOption = null
            for (var optionIndex = 0; optionIndex < strategyOptions.length; ++optionIndex) {
                if (strategyOptions[optionIndex].strategyId === selectedStrategyId) {
                    selectedOption = strategyOptions[optionIndex]
                    break
                }
            }

            if (!selectedOption) {
                selectedOption = strategyOptions[0]
            }

            applyStrategySelection(selectedOption)
            syncStrategySelectionDisplay()
        }
    }

    function syncStrategySelectionDisplay() {
        if (!strategyComboBox) {
            return
        }

        syncingStrategySelection = true
        var matchIndex = -1
        for (var index = 0; index < strategyOptions.length; ++index) {
            var option = strategyOptions[index]
            if ((selectedStrategyId && option.strategyId === selectedStrategyId) ||
                (!selectedStrategyId && selectedStrategyName && option.strategyName === selectedStrategyName)) {
                matchIndex = index
                break
            }
        }
        strategyComboBox.currentIndex = matchIndex
        syncingStrategySelection = false
    }
    
    // 生成动态参数配置（从JSON文件动态加载）
    function generateDynamicParamConfigs() {
        console.log("开始动态加载策略回测参数配置")
        paramLoadWatchdog.restart()
        
        // 从配置文件加载
        root.riskBacktestMetaLoader.loadMetaFile("qrc:/config/views/risk_backtest_params.json", function(meta) {
            if (meta) {
                console.log("成功加载策略回测参数配置")
                
                // 清空现有配置
                root.dynamicParamConfigs = []
                
                // 只加载回测相关的参数
                var backtestParamConfigs = root.riskBacktestMetaLoader.getParameterConfigs("all")
                
                // 转换为动态参数生成器所需的格式
                backtestParamConfigs.forEach(function(paramConfig) {
                    var config = {
                        id: paramConfig.id,
                        type: paramConfig.type,
                        label: paramConfig.label,
                        description: paramConfig.description,
                        default: paramConfig.default,
                        category: paramConfig.category,
                        group: paramConfig.category || "回测配置"
                    }
                    
                    // 根据类型添加特定属性
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
                    
                    // 处理可见性条件
                    if (paramConfig.visibleWhen) {
                        config.visibleWhen = paramConfig.visibleWhen
                    }
                    
                    root.dynamicParamConfigs.push(config)
                })
                
                root.dynamicParamConfigs = root.sanitizeDynamicParamConfigs(root.mergeSupplementalBacktestConfigs(root.dynamicParamConfigs))
                console.log("策略回测动态参数配置加载完成，数量:", root.dynamicParamConfigs.length)

                // 设置动态参数生成器的配置
                if (root.dynamicParamGenerator) {
                    root.dynamicParamGenerator.reloadConfigs(root.dynamicParamConfigs, [])
                } else {
                    root.initDynamicValues()
                }

                root.initDynamicValues()
                
                root.parametersLoaded = true
                paramLoadWatchdog.stop()
            } else {
                console.error("加载策略回测参数配置失败，使用默认配置")
                paramLoadWatchdog.stop()
                root.generateFallbackParamConfigs()
            }
        })
    }
    
    // 后备参数配置（当动态加载失败时使用）
    function generateFallbackParamConfigs() {
        root.dynamicParamConfigs = []
        
        // 基础回测参数
        root.dynamicParamConfigs.push({
            id: "backtestPeriod",
            type: "select",
            label: "回测周期",
            description: "选择回测的时间周期",
            options: [
                { value: "1year", label: "最近1年" },
                { value: "3year", label: "最近3年" },
                { value: "5year", label: "最近5年" },
                { value: "full", label: "全周期" }
            ],
            default: "3year",
            category: "period",
            group: "时间周期配置"
        })
        
        root.dynamicParamConfigs.push({
            id: "initialCapital",
            type: "slider",
            label: "初始资金",
            description: "回测的初始资金金额（万元）",
            min: 10,
            max: 1000,
            step: 10,
            default: 100,
            unit: "万元",
            category: "capital",
            group: "资金管理"
        })
        
        root.dynamicParamConfigs.push({
            id: "commissionRate",
            type: "slider",
            label: "交易佣金",
            description: "每笔交易的佣金费率",
            min: 0.0001,
            max: 0.005,
            step: 0.0001,
            default: 0.001,
            decimals: 4,
            unit: "%",
            category: "cost",
            group: "交易成本"
        })
        
        root.dynamicParamConfigs.push({
            id: "slippageRate",
            type: "slider",
            label: "滑点率",
            description: "交易执行时的价格滑点率",
            min: 0,
            max: 0.01,
            step: 0.0001,
            default: 0.002,
            decimals: 4,
            unit: "%",
            category: "cost",
            group: "交易成本"
        })
        
        root.dynamicParamConfigs.push({
            id: "maxPositions",
            type: "slider",
            label: "最大持仓数",
            description: "同时持有的最大股票数量",
            min: 1,
            max: 50,
            step: 1,
            default: 10,
            unit: "只",
            category: "position",
            group: "仓位管理"
        })
        
        root.dynamicParamConfigs.push({
            id: "stopLossPercent",
            type: "slider",
            label: "止损比例",
            description: "单个头寸的最大亏损比例",
            min: 1,
            max: 50,
            step: 0.5,
            default: 10,
            decimals: 1,
            unit: "%",
            category: "risk",
            group: "风险控制"
        })
        
        root.dynamicParamConfigs.push({
            id: "takeProfitPercent",
            type: "slider",
            label: "止盈比例",
            description: "单个头寸的目标盈利比例",
            min: 5,
            max: 200,
            step: 1,
            default: 20,
            decimals: 0,
            unit: "%",
            category: "risk",
            group: "风险控制"
        })
        
        root.dynamicParamConfigs.push({
            id: "enableShortSelling",
            type: "toggle",
            label: "允许卖空",
            description: "是否允许卖空操作",
            default: false,
            category: "advanced",
            group: "高级选项"
        })
        
        root.dynamicParamConfigs = root.sanitizeDynamicParamConfigs(root.mergeSupplementalBacktestConfigs(root.dynamicParamConfigs))
        console.log("使用后备策略回测参数配置，数量:", root.dynamicParamConfigs.length)

        if (root.dynamicParamGenerator) {
            root.dynamicParamGenerator.reloadConfigs(root.dynamicParamConfigs, [])
        }
        root.initDynamicValues()
        root.parametersLoaded = true
        paramLoadWatchdog.stop()
    }
    
    // 初始化动态参数值
    function initDynamicValues() {
        var values = root.buildBaseDynamicParamValues()
        root.dynamicParamValues = values
        
        // 更新动态参数生成器的值
        if (root.dynamicParamGenerator) {
            root.dynamicParamGenerator.setValues(values)
        }

        if (root.hasObjectData(root.selectedStrategyData)) {
            root.applyStrategyDefaults(root.selectedStrategyData)
        }

        if (root.hasObjectData(root.pendingBacktestConfig)) {
            root.mergeDynamicParamValues(root.normalizeBacktestSessionConfig(root.pendingBacktestConfig))
        }
        
        console.log("初始化策略回测动态参数值完成:", values)
    }
    
    // ============ 内部函数 ============
    function setSelectedStrategy(strategyId, strategyName, backtestConfig) {
        root.selectedStrategyId = strategyId || ""
        root.selectedStrategyName = strategyName || ""
        root.pendingBacktestConfig = JSON.parse(JSON.stringify(backtestConfig || {}))

        root.refreshStrategyOptions()

        if (root.strategyBacktestController) {
            root.strategyBacktestController.selectedStrategyId = root.selectedStrategyId
        }

        root.selectedStrategyData = root.loadSelectedStrategyData()
        root.applyStrategyDefaults(root.selectedStrategyData)
        if (root.hasObjectData(root.pendingBacktestConfig)) {
            root.applyBacktestSessionConfig(root.pendingBacktestConfig)
            root.applyPortfolioBacktestContext(root.pendingBacktestConfig)
        }
        root.syncStrategySelectionDisplay()
    }

    function isPortfolioBacktestConfig(config) {
        if (!config) {
            return false
        }

        var source = String(config.source || "").toLowerCase()
        var strategyType = String(config.strategy_type || config.selectedStrategyType || "").toUpperCase()
        var subType = String(config.sub_type || config.selectedStrategySubtype || "").toLowerCase()
        return source === "portfolio_builder" || strategyType === "PORTFOLIO" || subType === "portfolio_builder"
    }

    function extractPortfolioAllocations(config) {
        return root.structureAdapter.resolvePortfolioAllocations(config, root.selectedStrategyData)
    }

    function applyPortfolioBacktestContext(config) {
        if (!root.isPortfolioBacktestConfig(config)) {
            return
        }

        var allocations = root.extractPortfolioAllocations(config)
        var scopeContext = root.structureAdapter.resolveStrategyScopeContext(config)
        var contextOverrides = {
            portfolioSource: String(scopeContext.portfolio_source || config.source || "portfolio_builder"),
            portfolioName: String(scopeContext.portfolio_name || config.portfolio_name || root.selectedStrategyName || ""),
            portfolioFactorCount: allocations.length,
            portfolioAllocationsJson: JSON.stringify(allocations),
            selectedStrategyType: String(scopeContext.selectedStrategyType || "PORTFOLIO"),
            selectedStrategySubtype: String(scopeContext.selectedStrategySubtype || "portfolio_builder")
        }

        root.mergeDynamicParamValues(contextOverrides)
        root.backtestStatus = allocations.length > 0
            ? "已载入组合策略，上下文包含 " + allocations.length + " 个因子"
            : "已载入组合策略，等待补充组合分配"
    }

    function loadSelectedStrategyData() {
        if (!strategyService || !selectedStrategyId || !strategyService.getStrategyById) {
            return ({})
        }

        var strategy = strategyService.getStrategyById(selectedStrategyId)
        return strategy || ({})
    }

    function buildRuntimeBacktestValues(sourceValues) {
        var runtimeValues = JSON.parse(JSON.stringify(sourceValues || dynamicParamValues || {}))
        var normalizedRuntimeValues = normalizeBacktestSessionConfig(runtimeValues)

        for (var normalizedKey in normalizedRuntimeValues) {
            runtimeValues[normalizedKey] = normalizedRuntimeValues[normalizedKey]
        }

        delete runtimeValues.backtestYears
        delete runtimeValues.backtestPeriod
        delete runtimeValues.positionPercent
        delete runtimeValues.maxPositionPercent
        delete runtimeValues.positionSize
        delete runtimeValues.stopLoss
        delete runtimeValues.takeProfit
        delete runtimeValues.rebalanceDays
        delete runtimeValues.rebalancingPeriod
        delete runtimeValues.transactionCost
        delete runtimeValues.slippageCost
        runtimeValues.startDate = selectedStartDate
        runtimeValues.endDate = selectedEndDate
        return runtimeValues
    }

    function extractLegacyBacktestParameterValues(parameters) {
        return {
            commissionRate: firstDefinedValue(parameters, ["commissionRate", "commission", "transactionCost", "transaction_cost"]),
            maxPositionPercent: firstDefinedValue(parameters, ["maxPositionPercent", "positionSize", "position_size", "positionPercent"]),
            stopLossPercent: firstDefinedValue(parameters, ["stopLossPercent", "stopLoss", "stop_loss"]),
            takeProfitPercent: firstDefinedValue(parameters, ["takeProfitPercent", "takeProfit", "take_profit"]),
            rebalanceDays: firstDefinedValue(parameters, ["rebalanceDays", "rebalancingPeriod", "rebalance_days"]),
            slippageRate: firstDefinedValue(parameters, ["slippageRate", "slippage", "slippageCost", "slippageLimit"]),
            varWarningPercent: firstDefinedValue(parameters, ["varWarningPercent"]),
            orderSizeLimit: firstDefinedValue(parameters, ["orderSizeLimit"]),
            turnoverLimit: firstDefinedValue(parameters, ["turnoverLimit"]),
            slippageLimit: firstDefinedValue(parameters, ["slippageLimit"]),
            level1Breaker: firstDefinedValue(parameters, ["level1Breaker"]),
            level2Breaker: firstDefinedValue(parameters, ["level2Breaker"]),
            level3Breaker: firstDefinedValue(parameters, ["level3Breaker"]),
            autoStopEnabled: firstDefinedValue(parameters, ["autoStopEnabled"])
        }
    }

    function buildLegacyBacktestRuntime(parameters, backtestSettings) {
        var legacyValues = extractLegacyBacktestParameterValues(parameters)
        return normalizeBacktestSessionConfig({
            startDate: backtestSettings.start_date,
            endDate: backtestSettings.end_date,
            backtestYears: backtestSettings.years,
            benchmark: backtestSettings.benchmark,
            commissionRate: legacyValues.commissionRate !== undefined ? legacyValues.commissionRate : backtestSettings.transaction_cost,
            maxDrawdownLimit: backtestSettings.max_drawdown_limit,
            positionSizingMethod: backtestSettings.position_sizing_method,
            maxPositionPercent: legacyValues.maxPositionPercent !== undefined ? legacyValues.maxPositionPercent : backtestSettings.max_position_percent,
            stopLossPercent: legacyValues.stopLossPercent !== undefined ? legacyValues.stopLossPercent : backtestSettings.stop_loss_percent,
            takeProfitPercent: legacyValues.takeProfitPercent !== undefined ? legacyValues.takeProfitPercent : backtestSettings.take_profit_percent,
            rebalanceDays: legacyValues.rebalanceDays,
            initialCapital: parameters.initialCapital,
            slippageRate: legacyValues.slippageRate,
            varWarningPercent: legacyValues.varWarningPercent,
            orderSizeLimit: legacyValues.orderSizeLimit,
            turnoverLimit: legacyValues.turnoverLimit,
            slippageLimit: legacyValues.slippageLimit,
            level1Breaker: legacyValues.level1Breaker,
            level2Breaker: legacyValues.level2Breaker,
            level3Breaker: legacyValues.level3Breaker,
            autoStopEnabled: legacyValues.autoStopEnabled,
            dataSourceMode: parameters.dataSourceMode
        })
    }

    function extractPersistedBacktestRuntime(strategy) {
        var parameters = strategy && strategy.parameters ? strategy.parameters : ({})
        var backtestSettings = parameters.backtest_settings || (strategy ? strategy.backtest_settings || ({}) : ({}))
        var structuredRuntime = normalizeBacktestSessionConfig(root.structureAdapter.resolveBacktestSessionView(strategy))
        var legacyRuntime = buildLegacyBacktestRuntime(parameters, backtestSettings)

        var resolvedRuntime = ({})
        for (var legacyKey in legacyRuntime) {
            resolvedRuntime[legacyKey] = legacyRuntime[legacyKey]
        }
        for (var structuredKey in structuredRuntime) {
            resolvedRuntime[structuredKey] = structuredRuntime[structuredKey]
        }
        return resolvedRuntime
    }

    function applyStrategyDefaults(strategy) {
        var nextValues = buildBaseDynamicParamValues()

        var persistedRuntime = extractPersistedBacktestRuntime(strategy)
        for (var key in persistedRuntime) {
            nextValues[key] = persistedRuntime[key]
        }

        if (persistedRuntime.startDate) {
            selectedStartDate = persistedRuntime.startDate
        }
        if (persistedRuntime.endDate) {
            selectedEndDate = persistedRuntime.endDate
        }

        dynamicParamValues = nextValues
        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValues(nextValues)
        }
        syncDataSourceSelectionDisplay()
    }

    function resolveDateRange() {
        function isValidDateString(value) {
            return /^\d{4}-\d{2}-\d{2}$/.test(String(value || ""))
        }

        var startDate = String(selectedStartDate || "")
        var endDate = String(selectedEndDate || "")

        return {
            startDate: isValidDateString(startDate) ? startDate : "",
            endDate: isValidDateString(endDate) ? endDate : ""
        }
    }

    function buildBacktestParams() {
        var strategy = root.selectedStrategyData && Object.keys(root.selectedStrategyData).length > 0
            ? root.selectedStrategyData
            : root.loadSelectedStrategyData()
        var parameters = strategy && strategy.parameters ? JSON.parse(JSON.stringify(strategy.parameters)) : ({})
        var runtimeValues = root.buildRuntimeBacktestValues(root.dynamicParamValues)
        var portfolioAllocations = root.extractPortfolioAllocations(root.pendingBacktestConfig)
        var isPortfolioContext = root.isPortfolioBacktestConfig(root.pendingBacktestConfig)
        var universeState = root.resolveBacktestUniverseState()
        var symbolPool = universeState.symbols.slice()

        runtimeValues.universeType = root.selectedUniverseType
        runtimeValues.universeId = root.selectedUniverseType === "index" ? root.selectedIndexSymbol : ""
        runtimeValues.indexSymbol = root.selectedUniverseType === "index" ? root.selectedIndexSymbol : ""

        delete parameters.commissionRate
        delete parameters.commission
        delete parameters.slippageRate
        delete parameters.slippage
        delete parameters.initialCapital
        delete parameters.dataSourceMode
        delete parameters.symbol_pool
        delete parameters.symbolPool

        parameters.backtest_runtime = runtimeValues
        parameters.backtest_universe_source = universeState.sourceKey || ""
        parameters.backtest_universe_label = universeState.sourceLabel || ""
        parameters.selectedStrategyId = root.selectedStrategyId
        parameters.selectedStrategyName = root.selectedStrategyName || root.strategyComboBox.currentText
        parameters.selectedStrategySubtype = String(strategy.sub_type || strategy.subType || parameters.strategy_subtype || "")
        parameters.selectedStrategyType = String(strategy.strategy_type || strategy.strategyType || "")
        if (root.selectedUniverseType !== "index" && symbolPool.length > 0) {
            parameters.symbol_pool = symbolPool
            parameters.symbolPool = symbolPool
        }

        if (isPortfolioContext) {
            parameters.portfolio_source = String(root.pendingBacktestConfig.source || "portfolio_builder")
            parameters.portfolio_name = String(root.pendingBacktestConfig.portfolio_name || root.selectedStrategyName || root.strategyComboBox.currentText || "")
            parameters.portfolio_factor_count = portfolioAllocations.length
            parameters.portfolio_allocations_json = JSON.stringify(portfolioAllocations)
            parameters.selectedStrategySubtype = "portfolio_builder"
            parameters.selectedStrategyType = "PORTFOLIO"
        }

        return parameters
    }

    function startBacktest() {
        if (!root.strategyBacktestController) {
            root.backtestStatus = "回测控制器未初始化"
            return
        }

        var effectiveStrategyId = root.selectedStrategyId || root.strategyComboBox.currentText
        var effectiveStrategyName = root.selectedStrategyName || root.strategyComboBox.currentText
        var dateRange = root.resolveDateRange()
        var universeState = root.resolveBacktestUniverseState()
        var symbols = universeState.symbols.slice()
        if (root.selectedUniverseType === "index") {
            if (!root.selectedIndexSymbol) {
                root.backtestStatus = "请选择指数"
                return
            }
        }

        if (!effectiveStrategyId) {
            root.backtestStatus = "请先选择策略"
            return
        }
        if (!dateRange.startDate || !dateRange.endDate) {
            root.backtestStatus = "请选择有效的开始日期和结束日期"
            return
        }
        if (dateRange.startDate > dateRange.endDate) {
            root.backtestStatus = "开始日期不能晚于结束日期"
            return
        }
        if (root.selectedUniverseType !== "index" && (!symbols || symbols.length === 0)) {
            root.backtestStatus = "当前数据源下没有可用股票"
            return
        }

        root.backtestUniverseSummary = root.buildBacktestUniverseSummary(universeState)
        if (root.backtestUniverseSummary.length > 0) {
            root.backtestStatus = "准备启动回测 · 标的来源: " + root.backtestUniverseSummary
        }

        root.selectedStrategyId = effectiveStrategyId
        root.selectedStrategyName = effectiveStrategyName
        root.selectedStrategyData = root.loadSelectedStrategyData()

        root.strategyBacktestController.initialCapital = Number(root.dynamicParamValues.initialCapital || 1000000)
        root.strategyBacktestController.startDate = dateRange.startDate
        root.strategyBacktestController.endDate = dateRange.endDate
        root.strategyBacktestController.dataSourceMode = root.dynamicParamValues.dataSourceMode || "raw"
        root.strategyBacktestController.selectedSymbols = symbols
        root.strategyBacktestController.startStrategyBacktest(
            effectiveStrategyId,
            root.buildBacktestParams(),
            symbols,
            dateRange.startDate,
            dateRange.endDate
        )
    }

    function showOptimization() {
        console.log("打开参数优化")
    }

    function exportResults() {
        console.log("导出策略回测结果", JSON.stringify(backtestResult || {}))
    }

    function getUniverseLabel(universeType) {
        for (var i = 0; i < universeOptions.length; ++i) {
            if (universeOptions[i].value === universeType) {
                return universeOptions[i].label
            }
        }
        return universeType || "全市场"
    }

    function getIndexPoolLabel(indexSymbol) {
        for (var i = 0; i < indexPoolOptions.length; ++i) {
            if (indexPoolOptions[i].value === indexSymbol) {
                return indexPoolOptions[i].label
            }
        }
        return indexSymbol || ""
    }

    function currentStrategyLatestBacktestRecord() {
        var strategy = root.selectedStrategyData && Object.keys(root.selectedStrategyData).length > 0
            ? root.selectedStrategyData
            : root.loadSelectedStrategyData()
        var performance = strategy.performance_metrics || strategy.performanceMetrics || ({})
        return performance.latestBacktest || performance.latest_backtest || ({})
    }

    function resolveBacktestRecordSymbolPool(record) {
        return root.structureAdapter.resolveBacktestRecordSymbolPool(record)
    }

    function normalizeStrategyMetricValue(value) {
        var number = Number(value)
        return isNaN(number) ? 0 : number
    }

    function compareStrategyBacktestCoverage(previousBacktest, currentBacktest) {
        if (!previousBacktest || Object.keys(previousBacktest).length === 0) {
            return {
                action: "replace",
                title: "首次记录",
                summary: "当前没有上一轮有效回测基线，本次结果将直接作为新的股票池基线。"
            }
        }

        var previousSummary = previousBacktest.summary || ({})
        var currentSummary = currentBacktest.summary || ({})
        var betterSignals = 0
        var worseSignals = 0
        var detailParts = []

        var returnsDiff = normalizeStrategyMetricValue(currentSummary.returns) - normalizeStrategyMetricValue(previousSummary.returns)
        var sharpeDiff = normalizeStrategyMetricValue(currentSummary.sharpeRatio) - normalizeStrategyMetricValue(previousSummary.sharpeRatio)
        var drawdownDiff = normalizeStrategyMetricValue(currentSummary.maxDrawdown) - normalizeStrategyMetricValue(previousSummary.maxDrawdown)
        var winRateDiff = normalizeStrategyMetricValue(currentSummary.winRate) - normalizeStrategyMetricValue(previousSummary.winRate)

        if (returnsDiff >= 3) {
            betterSignals++
            detailParts.push("总收益 +" + returnsDiff.toFixed(2) + "%")
        } else if (returnsDiff <= -3) {
            worseSignals++
            detailParts.push("总收益 " + returnsDiff.toFixed(2) + "%")
        }

        if (sharpeDiff >= 0.2) {
            betterSignals++
            detailParts.push("夏普 +" + sharpeDiff.toFixed(2))
        } else if (sharpeDiff <= -0.2) {
            worseSignals++
            detailParts.push("夏普 " + sharpeDiff.toFixed(2))
        }

        if (drawdownDiff <= -2) {
            betterSignals++
            detailParts.push("回撤改善 " + Math.abs(drawdownDiff).toFixed(2) + "%")
        } else if (drawdownDiff >= 2) {
            worseSignals++
            detailParts.push("回撤扩大 " + drawdownDiff.toFixed(2) + "%")
        }

        if (winRateDiff >= 3) {
            betterSignals++
            detailParts.push("胜率 +" + winRateDiff.toFixed(2) + "%")
        } else if (winRateDiff <= -3) {
            worseSignals++
            detailParts.push("胜率 " + winRateDiff.toFixed(2) + "%")
        }

        var previousPool = resolveBacktestRecordSymbolPool(previousBacktest)
        var currentPool = resolveBacktestRecordSymbolPool(currentBacktest)
        var overlapPool = intersectSymbolCollections(previousPool, currentPool)
        var summaryPrefix = "上次股票池 " + previousPool.length + " 只，本次股票池 " + currentPool.length + " 只，重合 " + overlapPool.length + " 只。"
        var detailSummary = detailParts.length > 0 ? ("关键差异: " + detailParts.join("，") + "。") : "两次关键指标接近。"

        if (betterSignals >= 2 && worseSignals === 0) {
            return {
                action: "replace",
                title: "结果明显更优",
                summary: summaryPrefix + detailSummary + " 已自动用本次结果覆盖上一轮股票池基线。"
            }
        }

        if (worseSignals >= 2 && betterSignals === 0) {
            return {
                action: "keep",
                title: "上一轮结果更稳健",
                summary: summaryPrefix + detailSummary + " 已保留上一轮股票池基线，仅追加本次回测历史。"
            }
        }

        return {
            action: "ask",
            title: "结果接近",
            summary: summaryPrefix + detailSummary + " 两次结果接近，请选择是否用本次股票池覆盖上一轮基线。"
        }
    }

    function buildStrategyLatestPoolComparisonText() {
        var previousBacktest = root.currentStrategyLatestBacktestRecord()
        var previousPool = root.resolveBacktestRecordSymbolPool(previousBacktest)
        var currentPool = root.resolveBacktestUniverseState().symbols.slice()
        var overlapPool = root.intersectSymbolCollections(previousPool, currentPool)

        if (previousPool.length === 0) {
            return "当前没有上一轮策略回测基线，本次完成后会直接建立新的股票池基线。"
        }

        return "上一轮股票池 " + previousPool.length
            + " 只，本次候选池 " + currentPool.length
            + " 只，重合 " + overlapPool.length
            + " 只，上轮独有 " + subtractSymbolCollections(previousPool, currentPool).length
            + " 只，本轮新增 " + subtractSymbolCollections(currentPool, previousPool).length + " 只。"
    }

    function commitStrategyBacktestPerformance(replaceLatestBacktest) {
        if (!selectedStrategyId || !strategyService || !strategyService.updateStrategyPerformance) {
            return false
        }

        var payload = JSON.parse(JSON.stringify(pendingStrategyPerformancePayload || ({})))
        payload.replaceLatestBacktest = replaceLatestBacktest
        var ok = strategyService.updateStrategyPerformance(selectedStrategyId, payload)
        if (!ok) {
            console.warn("回测结果回写策略失败:", selectedStrategyId)
            return false
        }

        selectedStrategyData = loadSelectedStrategyData()
        console.log("回测结果已同步到策略:", selectedStrategyId, JSON.stringify(payload))
        return true
    }

    function handleBacktestCompleted(result) {
        updateResults(result)
        syncBacktestPerformanceToStrategy()
    }

    function syncBacktestPerformanceToStrategy() {
        if (!selectedStrategyId || !hasBacktestResult() || !strategyService || !strategyService.updateStrategyPerformance) {
            return
        }

        var dateRange = resolveDateRange()
        var universeState = resolveBacktestUniverseState()
        var appliedSymbolPool = universeState.symbols.slice()
        var performancePayload = BacktestPerformanceAdapter.buildStrategyPerformancePayload(backtestResult, {
            selectedStrategyId: selectedStrategyId,
            selectedStrategyName: selectedStrategyName,
            selectedUniverseType: selectedUniverseType,
            universeLabel: getUniverseLabel(selectedUniverseType),
            selectedIndexSymbol: selectedIndexSymbol,
            indexLabel: getIndexPoolLabel(selectedIndexSymbol),
            dataSourceMode: dynamicParamValues.dataSourceMode || "raw",
            startDate: dateRange.startDate,
            endDate: dateRange.endDate,
            runtimeParameters: buildRuntimeBacktestValues(dynamicParamValues),
            appliedSymbolPool: appliedSymbolPool,
            universeSourceKey: universeState.sourceKey || "",
            universeSourceLabel: universeState.sourceLabel || ""
        })

        pendingStrategyPerformancePayload = performancePayload
        pendingStrategyPreviousBacktest = currentStrategyLatestBacktestRecord()

        var coverageDecision = compareStrategyBacktestCoverage(
            pendingStrategyPreviousBacktest,
            performancePayload.backtestHistoryEntry || ({}))
        pendingStrategyCoverageDecision = coverageDecision.action || "replace"
        pendingStrategyCoverageSummary = coverageDecision.summary || ""

        if (coverageDecision.action === "ask") {
            strategyCoverageDecisionDialog.open()
            return
        }

        var replaceLatestBacktest = coverageDecision.action === "replace"
        if (commitStrategyBacktestPerformance(replaceLatestBacktest)) {
            backtestStatus = coverageDecision.summary || backtestStatus
        }
    }

    function hasBacktestResult() {
        return backtestResult && Object.keys(backtestResult).length > 0
    }

    // 更新结果
    function updateResults(result) {
        backtestResult = BacktestResultAdapter.normalizeBacktestResult(result)
    }

    Dialog {
        id: strategyCoverageDecisionDialog
        modal: true
        width: 520
        title: root.pendingStrategyCoverageDecision === "ask" ? "股票池覆盖确认" : "回测结果处理"

        standardButtons: Dialog.Yes | Dialog.No
        visible: false

        onAccepted: {
            if (root.commitStrategyBacktestPerformance(true)) {
                root.backtestStatus = root.pendingStrategyCoverageSummary.length > 0
                    ? root.pendingStrategyCoverageSummary
                    : "已使用本次回测股票池覆盖上一轮基线"
            }
        }

        onRejected: {
            if (root.commitStrategyBacktestPerformance(false)) {
                root.backtestStatus = root.pendingStrategyCoverageSummary.length > 0
                    ? root.pendingStrategyCoverageSummary
                    : "已保留上一轮股票池基线，仅记录本次回测历史"
            }
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: root.pendingStrategyCoverageSummary
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: "#E2E8F0"
            }

            Text {
                Layout.fillWidth: true
                text: "选择“是”将把本次回测结果设置为新的有效股票池基线；选择“否”则只保留历史记录，不覆盖当前基线。"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#94A3B8"
            }
        }

        background: Rectangle {
            radius: 14
            color: "#0F172A"
            border.width: 1
            border.color: "#334155"
        }
    }

    Component.onCompleted: {
        if (!root.strategyBacktestController) {
            root.strategyBacktestController = internalStrategyBacktestController
        }
        if (root.riskConfigService && typeof root.riskConfigService.initialize === "function") {
            root.riskConfigService.initialize()
        }
        root.refreshStrategyOptions()
        root.ensureDynamicParamsReady()
        root.syncDataSourceSelectionDisplay()
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }

        root.refreshStrategyOptions()
        root.ensureDynamicParamsReady()
        if (root.hasObjectData(root.selectedStrategyData)) {
            root.applyStrategyDefaults(root.selectedStrategyData)
        }
        root.syncDataSourceSelectionDisplay()
    }

    Connections {
        target: root.strategyService
        function onInitializedChanged() {
            root.refreshStrategyOptions()
        }
        function onStrategyCreated(strategyId, strategyData) {
            var option = root.upsertStrategyOption(
                strategyId,
                strategyData ? (strategyData.strategy_name || strategyData.strategyName || strategyData.name || "") : ""
            )
            if (!option) {
                root.refreshStrategyOptions()
                return
            }

            root.applyStrategySelection(option)
            root.syncStrategySelectionDisplay()
        }
        function onDataChanged() {
            root.refreshStrategyOptions()
        }
        function onStrategiesLoaded(strategies) {
            root.refreshStrategyOptions()
        }
    }
}

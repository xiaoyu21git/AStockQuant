// StrategyBacktestPage.qml
// 策略回测页面 - 动态参数版本
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtCharts 2.15
import AStock.Bridge 1.0 as Bridge
import "../../components" as SharedComponents
import "../../components/Backtest" as BacktestComponents
import "../../components/FactorWorkbench/Creation/components" as PluginComponents
import "../../utils/BacktestPerformanceAdapter.js" as BacktestPerformanceAdapter
import "../../utils/BacktestResultAdapter.js" as BacktestResultAdapter
import "../../utils/RiskBacktestMetaLoader.js" as RiskBacktestMeta

/**
 * 策略回测页面组件
 * 提供交易策略历史表现回测功能
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property Bridge.FactorService factorService: null
    property Bridge.StrategyBacktestController strategyBacktestController: null
    property var strategyService: Bridge.StrategyService
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
    property string selectedUniverseType: "market"
    property string selectedIndexSymbol: "000300.SH"
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    
    // 回测结果
    property var backtestResult: ({})

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
            root.backtestStatus = "回测启动中..."
        }

        onBacktestCompleted: function(result) {
            root.isBacktesting = false
            root.backtestProgress = 100
            root.backtestStatus = "回测完成"
            root.updateResults(result)
            root.syncBacktestPerformanceToStrategy()
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
    PluginComponents.ParamComponents {
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
                BacktestComponents.BacktestParameterPanel {
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
                                color: isBacktesting ? "#334155" : "#3B82F6"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: isBacktesting ? "⏸️" : "▶️"
                                        font.pixelSize: 13
                                        color: isBacktesting ? "#94A3B8" : "white"
                                    }
                                    
                                    Text {
                                        text: isBacktesting ? "回测中..." : "开始回测"
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        color: isBacktesting ? "#94A3B8" : "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: !isBacktesting
                                    onClicked: startBacktest()
                                }
                            }
                            
                            // 进度条
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 8
                                radius: 4
                                color: "#334155"
                                visible: isBacktesting
                                
                                Rectangle {
                                    width: parent.width * (backtestProgress / 100)
                                    height: parent.height
                                    radius: 4
                                    color: "#3B82F6"
                                }
                            }
                            
                            // 进度文本
                            Text {
                                text: isBacktesting ? backtestProgress + "%" : ""
                                font.pixelSize: 12
                                color: "#94A3B8"
                                visible: isBacktesting
                            }
                            
                            // 状态文本
                            Text {
                                text: backtestStatus
                                font.pixelSize: 12
                                color: isBacktesting ? "#F59E0B" : "#94A3B8"
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
                
                BacktestComponents.BacktestResultPanel {
                    id: resultPanel
                    backtestResult: root.backtestResult
                    isBacktesting: root.isBacktesting
                    strategyDisplayName: strategyComboBox.currentText || "未命名策略"
                    onOptimizationRequested: showOptimization()
                    onExportRequested: exportResults()
                }
            }
        }
    }
    
    // ============ 动态参数方法 ============
    
    // 初始化动态参数配置
    function initDynamicParams() {
        console.log("初始化策略回测动态参数配置")

        parametersLoaded = false
        paramLoadWatchdog.restart()
        
        // 生成动态参数配置
        generateDynamicParamConfigs()
    }

    function ensureDynamicParamsReady() {
        if (!parametersLoaded || dynamicParamConfigs.length === 0) {
            console.log("策略回测参数未就绪，重新初始化")
            generateDynamicParamConfigs()
            return
        }

        if (dynamicParamGenerator && dynamicParamGenerator.configsList.length === 0) {
            console.log("策略回测参数生成器为空，重新装载配置")
            dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, [])
            dynamicParamGenerator.setValues(dynamicParamValues || {})
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

    function normalizeBacktestSessionConfig(config) {
        var normalized = ({})
        if (!config) {
            return normalized
        }

        if (config.startDate !== undefined && config.startDate !== null) normalized.startDate = String(config.startDate)
        if (config.endDate !== undefined && config.endDate !== null) normalized.endDate = String(config.endDate)
        if (config.initialCapital !== undefined && config.initialCapital !== null) normalized.initialCapital = normalizeInitialCapitalInput(config.initialCapital)
        if (config.dataSourceMode !== undefined && config.dataSourceMode !== null) normalized.dataSourceMode = String(config.dataSourceMode)
        if (config.backtestPeriod !== undefined && config.backtestPeriod !== null) {
            normalized.backtestPeriod = String(config.backtestPeriod)
        }
        if (config.backtestYears !== undefined && config.backtestYears !== null) {
            normalized.backtestPeriod = mapBacktestYearsToPeriod(config.backtestYears)
        }
        if (config.benchmark !== undefined) normalized.benchmark = normalizeBenchmarkValue(config.benchmark)
        if (config.transactionCost !== undefined) normalized.commissionRate = Number(config.transactionCost)
        if (config.commission !== undefined) normalized.commissionRate = Number(config.commission)
        if (config.commissionRate !== undefined) normalized.commissionRate = Number(config.commissionRate)
        if (config.slippageCost !== undefined) normalized.slippageRate = Number(config.slippageCost)
        if (config.slippage !== undefined) normalized.slippageRate = Number(config.slippage)
        if (config.slippageRate !== undefined) normalized.slippageRate = Number(config.slippageRate)
        if (config.maxDrawdownLimit !== undefined) normalized.maxDrawdownLimit = normalizePercentInput(config.maxDrawdownLimit)
        if (config.positionSizingMethod !== undefined) normalized.positionSizingMethod = normalizePositionSizingMethod(config.positionSizingMethod)
        if (config.maxPositionPercent !== undefined) {
            normalized.maxPositionPercent = normalizePercentInput(config.maxPositionPercent)
        }
        if (config.position_size !== undefined && normalized.maxPositionPercent === undefined) {
            normalized.maxPositionPercent = Number(config.position_size) <= 1
                ? Number(config.position_size) * 100
                : Number(config.position_size)
        }
        if (config.positionSize !== undefined && normalized.maxPositionPercent === undefined) {
            normalized.maxPositionPercent = Number(config.positionSize) <= 1
                ? Number(config.positionSize) * 100
                : Number(config.positionSize)
        }
        if (config.positionPercent !== undefined && normalized.maxPositionPercent === undefined) {
            normalized.maxPositionPercent = Number(config.positionPercent) <= 1
                ? Number(config.positionPercent) * 100
                : Number(config.positionPercent)
        }
        if (config.stop_loss !== undefined && normalized.stopLossPercent === undefined) {
            normalized.stopLossPercent = normalizePercentInput(config.stop_loss)
        }
        if (config.stopLoss !== undefined && normalized.stopLossPercent === undefined) {
            normalized.stopLossPercent = normalizePercentInput(config.stopLoss)
        }
        if (config.stopLossPercent !== undefined) normalized.stopLossPercent = normalizePercentInput(config.stopLossPercent)
        if (config.take_profit !== undefined && normalized.takeProfitPercent === undefined) {
            normalized.takeProfitPercent = normalizePercentInput(config.take_profit)
        }
        if (config.takeProfit !== undefined && normalized.takeProfitPercent === undefined) {
            normalized.takeProfitPercent = normalizePercentInput(config.takeProfit)
        }
        if (config.takeProfitPercent !== undefined) normalized.takeProfitPercent = normalizePercentInput(config.takeProfitPercent)
        if (config.rebalance_days !== undefined) normalized.rebalanceDays = Number(config.rebalance_days)
        if (config.rebalanceDays !== undefined) normalized.rebalanceDays = Number(config.rebalanceDays)
        if (config.rebalancingPeriod !== undefined && normalized.rebalanceDays === undefined) {
            normalized.rebalanceDays = Number(config.rebalancingPeriod)
        }
        if (config.enableAdvancedOptions !== undefined) normalized.enableAdvancedOptions = !!config.enableAdvancedOptions
        if (config.enableWalkForward !== undefined) normalized.enableWalkForward = !!config.enableWalkForward
        if (config.enableMonteCarlo !== undefined) normalized.enableMonteCarlo = !!config.enableMonteCarlo
        if (config.monteCarloSamples !== undefined) normalized.monteCarloSamples = Number(config.monteCarloSamples)
        if (config.enableOutOfSample !== undefined) normalized.enableOutOfSample = !!config.enableOutOfSample
        if (config.outOfSampleRatio !== undefined) normalized.outOfSampleRatio = Number(config.outOfSampleRatio)

        return normalized
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
        RiskBacktestMeta.loadMetaFile("qrc:/config/views/risk_backtest_params.json", function(meta) {
            if (meta) {
                console.log("成功加载策略回测参数配置")
                
                // 清空现有配置
                dynamicParamConfigs = []
                
                // 只加载回测相关的参数
                var backtestParamConfigs = RiskBacktestMeta.getParameterConfigs("all")
                
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
                    
                    dynamicParamConfigs.push(config)
                })
                
                dynamicParamConfigs = sanitizeDynamicParamConfigs(mergeSupplementalBacktestConfigs(dynamicParamConfigs))
                console.log("策略回测动态参数配置加载完成，数量:", dynamicParamConfigs.length)

                // 设置动态参数生成器的配置
                if (dynamicParamGenerator) {
                    dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, [])
                } else {
                    initDynamicValues()
                }

                initDynamicValues()
                
                parametersLoaded = true
                paramLoadWatchdog.stop()
            } else {
                console.error("加载策略回测参数配置失败，使用默认配置")
                paramLoadWatchdog.stop()
                generateFallbackParamConfigs()
            }
        })
    }
    
    // 后备参数配置（当动态加载失败时使用）
    function generateFallbackParamConfigs() {
        dynamicParamConfigs = []
        
        // 基础回测参数
        dynamicParamConfigs.push({
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
        
        dynamicParamConfigs.push({
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
        
        dynamicParamConfigs.push({
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
        
        dynamicParamConfigs.push({
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
        
        dynamicParamConfigs.push({
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
        
        dynamicParamConfigs.push({
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
        
        dynamicParamConfigs.push({
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
        
        dynamicParamConfigs.push({
            id: "enableShortSelling",
            type: "toggle",
            label: "允许卖空",
            description: "是否允许卖空操作",
            default: false,
            category: "advanced",
            group: "高级选项"
        })
        
        dynamicParamConfigs = sanitizeDynamicParamConfigs(mergeSupplementalBacktestConfigs(dynamicParamConfigs))
        console.log("使用后备策略回测参数配置，数量:", dynamicParamConfigs.length)

        if (dynamicParamGenerator) {
            dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, [])
        }
        initDynamicValues()
        parametersLoaded = true
        paramLoadWatchdog.stop()
    }
    
    // 初始化动态参数值
    function initDynamicValues() {
        var values = {}
        dynamicParamConfigs.forEach(function(config) {
            if (config.default !== undefined) {
                values[config.id] = config.default
            }
        })
        dynamicParamValues = values
        
        // 更新动态参数生成器的值
        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValues(values)
        }

        if (hasObjectData(selectedStrategyData)) {
            applyStrategyDefaults(selectedStrategyData)
        }

        if (hasObjectData(pendingBacktestConfig)) {
            mergeDynamicParamValues(normalizeBacktestSessionConfig(pendingBacktestConfig))
        }
        
        console.log("初始化策略回测动态参数值完成:", values)
    }
    
    // ============ 内部函数 ============
    function setSelectedStrategy(strategyId, strategyName, backtestConfig) {
        selectedStrategyId = strategyId || ""
        selectedStrategyName = strategyName || ""
        pendingBacktestConfig = JSON.parse(JSON.stringify(backtestConfig || {}))

        refreshStrategyOptions()

        if (strategyBacktestController) {
            strategyBacktestController.selectedStrategyId = selectedStrategyId
        }

        selectedStrategyData = loadSelectedStrategyData()
        applyStrategyDefaults(selectedStrategyData)
        if (hasObjectData(pendingBacktestConfig)) {
            applyBacktestSessionConfig(pendingBacktestConfig)
            applyPortfolioBacktestContext(pendingBacktestConfig)
        }
        syncStrategySelectionDisplay()
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
        if (!config) {
            return []
        }

        if (config.factor_allocations && config.factor_allocations.length !== undefined) {
            return config.factor_allocations
        }

        if (config.allocations && config.allocations.length !== undefined) {
            return config.allocations
        }

        var strategyAllocations = selectedStrategyData
            && selectedStrategyData.parameters
            && selectedStrategyData.parameters.allocations
            && selectedStrategyData.parameters.allocations.length !== undefined
            ? selectedStrategyData.parameters.allocations
            : []
        return strategyAllocations
    }

    function applyPortfolioBacktestContext(config) {
        if (!isPortfolioBacktestConfig(config)) {
            return
        }

        var allocations = extractPortfolioAllocations(config)
        var contextOverrides = {
            portfolioSource: String(config.source || "portfolio_builder"),
            portfolioName: String(config.portfolio_name || selectedStrategyName || ""),
            portfolioFactorCount: allocations.length,
            portfolioAllocationsJson: JSON.stringify(allocations),
            selectedStrategyType: "PORTFOLIO",
            selectedStrategySubtype: "portfolio_builder"
        }

        mergeDynamicParamValues(contextOverrides)
        backtestStatus = allocations.length > 0
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

    function extractPersistedBacktestRuntime(strategy) {
        var parameters = strategy && strategy.parameters ? strategy.parameters : ({})
        var backtestSettings = parameters.backtest_settings || strategy.backtest_settings || ({})
        var runtimeBacktest = parameters.backtest_runtime || strategy.backtest_runtime || ({})
        var normalizedRuntime = normalizeBacktestSessionConfig(runtimeBacktest)
        var legacyRuntime = normalizeBacktestSessionConfig({
            startDate: backtestSettings.start_date,
            endDate: backtestSettings.end_date,
            backtestYears: backtestSettings.years,
            benchmark: backtestSettings.benchmark,
            transactionCost: backtestSettings.transaction_cost,
            maxDrawdownLimit: backtestSettings.max_drawdown_limit,
            positionSizingMethod: backtestSettings.position_sizing_method,
            maxPositionPercent: backtestSettings.max_position_percent,
            position_size: parameters.position_size,
            stopLossPercent: backtestSettings.stop_loss_percent,
            takeProfitPercent: backtestSettings.take_profit_percent,
            rebalance_days: parameters.rebalance_days,
            rebalanceDays: parameters.rebalanceDays,
            rebalancingPeriod: parameters.rebalancingPeriod,
            positionSize: parameters.positionSize,
            stopLoss: parameters.stopLoss,
            takeProfit: parameters.takeProfit,
            initialCapital: parameters.initialCapital,
            commissionRate: parameters.commissionRate,
            commission: parameters.commission,
            slippageRate: parameters.slippageRate,
            slippage: parameters.slippage,
            dataSourceMode: parameters.dataSourceMode
        })

        var resolvedRuntime = ({})
        for (var legacyKey in legacyRuntime) {
            resolvedRuntime[legacyKey] = legacyRuntime[legacyKey]
        }
        for (var runtimeKey in normalizedRuntime) {
            resolvedRuntime[runtimeKey] = normalizedRuntime[runtimeKey]
        }
        return resolvedRuntime
    }

    function applyStrategyDefaults(strategy) {
        var nextValues = JSON.parse(JSON.stringify(dynamicParamValues || {}))

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
        var strategy = selectedStrategyData && Object.keys(selectedStrategyData).length > 0
            ? selectedStrategyData
            : loadSelectedStrategyData()
        var parameters = strategy && strategy.parameters ? JSON.parse(JSON.stringify(strategy.parameters)) : ({})
        var runtimeValues = buildRuntimeBacktestValues(dynamicParamValues)
        var portfolioAllocations = extractPortfolioAllocations(pendingBacktestConfig)
        var isPortfolioContext = isPortfolioBacktestConfig(pendingBacktestConfig)

        runtimeValues.universeType = selectedUniverseType
        runtimeValues.universeId = selectedUniverseType === "index" ? selectedIndexSymbol : ""
        runtimeValues.indexSymbol = selectedUniverseType === "index" ? selectedIndexSymbol : ""

        delete parameters.commissionRate
        delete parameters.commission
        delete parameters.slippageRate
        delete parameters.slippage
        delete parameters.initialCapital
        delete parameters.dataSourceMode

        parameters.backtest_runtime = runtimeValues
        parameters.selectedStrategyId = selectedStrategyId
        parameters.selectedStrategyName = selectedStrategyName || strategyComboBox.currentText
        parameters.selectedStrategySubtype = String(strategy.sub_type || strategy.subType || parameters.strategy_subtype || "")
        parameters.selectedStrategyType = String(strategy.strategy_type || strategy.strategyType || "")

        if (isPortfolioContext) {
            parameters.portfolio_source = String(pendingBacktestConfig.source || "portfolio_builder")
            parameters.portfolio_name = String(pendingBacktestConfig.portfolio_name || selectedStrategyName || strategyComboBox.currentText || "")
            parameters.portfolio_factor_count = portfolioAllocations.length
            parameters.portfolio_allocations_json = JSON.stringify(portfolioAllocations)
            parameters.selectedStrategySubtype = "portfolio_builder"
            parameters.selectedStrategyType = "PORTFOLIO"
        }

        return parameters
    }

    function startBacktest() {
        if (!strategyBacktestController) {
            backtestStatus = "回测控制器未初始化"
            return
        }

        var effectiveStrategyId = selectedStrategyId || strategyComboBox.currentText
        var effectiveStrategyName = selectedStrategyName || strategyComboBox.currentText
        var dateRange = resolveDateRange()
        var symbols = []
        if (selectedUniverseType === "index") {
            if (!selectedIndexSymbol) {
                backtestStatus = "请选择指数"
                return
            }
        } else {
            symbols = strategyBacktestController.getAvailableSymbols("")
        }

        if (!effectiveStrategyId) {
            backtestStatus = "请先选择策略"
            return
        }
        if (!dateRange.startDate || !dateRange.endDate) {
            backtestStatus = "请选择有效的开始日期和结束日期"
            return
        }
        if (dateRange.startDate > dateRange.endDate) {
            backtestStatus = "开始日期不能晚于结束日期"
            return
        }
        if (selectedUniverseType !== "index" && (!symbols || symbols.length === 0)) {
            backtestStatus = "当前数据源下没有可用股票"
            return
        }

        selectedStrategyId = effectiveStrategyId
        selectedStrategyName = effectiveStrategyName
        selectedStrategyData = loadSelectedStrategyData()

        strategyBacktestController.initialCapital = Number(dynamicParamValues.initialCapital || 1000000)
        strategyBacktestController.startDate = dateRange.startDate
        strategyBacktestController.endDate = dateRange.endDate
        strategyBacktestController.dataSourceMode = dynamicParamValues.dataSourceMode || "raw"
        strategyBacktestController.selectedSymbols = symbols
        strategyBacktestController.startStrategyBacktest(
            effectiveStrategyId,
            buildBacktestParams(),
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

    function syncBacktestPerformanceToStrategy() {
        if (!selectedStrategyId || !hasBacktestResult() || !strategyService || !strategyService.updateStrategyPerformance) {
            return
        }

        var dateRange = resolveDateRange()
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
            runtimeParameters: buildRuntimeBacktestValues(dynamicParamValues)
        })
        var ok = strategyService.updateStrategyPerformance(selectedStrategyId, performancePayload)
        if (!ok) {
            console.warn("回测结果回写策略失败:", selectedStrategyId)
            return
        }

        console.log("回测结果已同步到策略:", selectedStrategyId, JSON.stringify(performancePayload))
    }

    function hasBacktestResult() {
        return backtestResult && Object.keys(backtestResult).length > 0
    }

    // 更新结果
    function updateResults(result) {
        backtestResult = BacktestResultAdapter.normalizeBacktestResult(result)
    }

    Component.onCompleted: {
        if (!strategyBacktestController) {
            strategyBacktestController = internalStrategyBacktestController
        }
        refreshStrategyOptions()
        ensureDynamicParamsReady()
        syncDataSourceSelectionDisplay()
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }

        refreshStrategyOptions()
        ensureDynamicParamsReady()
        syncDataSourceSelectionDisplay()
    }

    Connections {
        target: strategyService
        function onInitializedChanged() {
            refreshStrategyOptions()
        }
        function onStrategyCreated(strategyId, strategyData) {
            var option = upsertStrategyOption(
                strategyId,
                strategyData ? (strategyData.strategy_name || strategyData.strategyName || strategyData.name || "") : ""
            )
            if (!option) {
                refreshStrategyOptions()
                return
            }

            applyStrategySelection(option)
            syncStrategySelectionDisplay()
        }
        function onDataChanged() {
            refreshStrategyOptions()
        }
        function onStrategiesLoaded(strategies) {
            refreshStrategyOptions()
        }
    }
}

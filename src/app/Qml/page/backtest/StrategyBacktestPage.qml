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
    property var uiLifecycleCoordinator: Bridge.UiLifecycleCoordinator
    property var riskBacktestMetaLoader: RiskBacktestMeta // qmllint disable unqualified
    property var structureAdapter: StructureAdapter // qmllint disable unqualified
    readonly property string sharedBacktestMetaPath: "qrc:/config/views/risk_backtest_params.json"
    readonly property string pageOnlyBacktestMetaPath: "qrc:/config/views/strategy_backtest_page_params.json"
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
    property var availableIndustryOptions: []
    readonly property var dataSourceManagedFilterKeys: ["excludeSt", "minListingDays"]
    property var dataSourceManagedFilterOverrides: ({})
    property string expectedProgrammaticDynamicParamPayload: ""
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    property string backtestStageLabel: "等待开始"
    property bool backtestCollectingData: false
    property string backtestUniverseSummary: ""
    readonly property string backtestActionHint: root.isBacktesting
        ? (root.backtestCollectingData
            ? "回测已开始，正在收集股票池与历史数据；开始回测按钮已禁用，请勿重复点击。"
            : "回测已开始，正在执行策略与汇总结果；开始回测按钮已禁用，请勿重复点击。")
        : "点击开始回测按钮，系统将使用历史数据验证策略表现"
    
    // 回测结果
    property var backtestResult: ({})
    property var pendingStrategyPerformancePayload: ({})
    property string pendingStrategyCoverageSummary: ""
    property string pendingStrategyCoverageDecision: ""
    property var pendingStrategyPreviousBacktest: ({})
    property bool pageServicesReady: false

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

        onCurrentStageLabelChanged: function(currentStageLabel) {
            root.backtestStageLabel = currentStageLabel || "等待开始"
        }

        onCollectingDataChanged: function(collectingData) {
            root.backtestCollectingData = collectingData
        }

        onBacktestStarted: function(strategyId) {
            root.selectedStrategyId = strategyId
            root.isBacktesting = true
            root.backtestProgress = 0
            root.backtestStageLabel = internalStrategyBacktestController.currentStageLabel || "准备提交回测"
            root.backtestCollectingData = internalStrategyBacktestController.collectingData
            root.backtestStatus = root.backtestUniverseSummary.length > 0
                ? ("回测启动中... · 标的来源: " + root.backtestUniverseSummary)
                : "回测启动中..."
        }

        onBacktestCompleted: function(result) {
            root.isBacktesting = false
            root.backtestProgress = 100
            root.backtestCollectingData = false
            root.backtestStageLabel = internalStrategyBacktestController.currentStageLabel || "回测完成"
            root.backtestStatus = root.backtestUniverseSummary.length > 0
                ? ("回测完成 · 标的来源: " + root.backtestUniverseSummary)
                : "回测完成"
            root.handleBacktestCompleted(result)
        }

        onBacktestFailed: function(error) {
            root.isBacktesting = false
            root.backtestCollectingData = false
            root.backtestStageLabel = internalStrategyBacktestController.currentStageLabel || "回测失败"
            root.backtestStatus = error
        }

        onBacktestCancelled: {
            root.isBacktesting = false
            root.backtestProgress = 0
            root.backtestCollectingData = false
            root.backtestStageLabel = "已取消"
            root.backtestStatus = "已取消"
        }
    }
    
    // ============ 动态参数配置 ============
    
    // 动态参数生成器
    property var dynamicParamConfigs: []
    property var dynamicParamGroups: []
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
                width: scrollView.availableWidth > 0 ? scrollView.availableWidth : Math.max(0, scrollView.width - 20)
                spacing: 12
                
                // 策略配置面板（动态参数版本）
                ConsoleUiComponents.BacktestParameterPanel {
                    id: parameterPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
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
                    dynamicParamGroups: root.dynamicParamGroups
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
                        mergeDynamicParamValues({ dataSourceMode: option.value }, { applyDataSourceDefaults: true })
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
                        var resolvedValues = JSON.parse(JSON.stringify(newValues || {}))
                        var payload = JSON.stringify(resolvedValues)
                        if (root.expectedProgrammaticDynamicParamPayload.length > 0
                                && root.expectedProgrammaticDynamicParamPayload === payload) {
                            root.expectedProgrammaticDynamicParamPayload = ""
                            root.dynamicParamValues = resolvedValues
                            syncDataSourceSelectionDisplay()
                            return
                        }

                        root.recordDataSourceManagedFilterOverrides(root.dynamicParamValues, resolvedValues)
                        root.dynamicParamValues = resolvedValues
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

                Rectangle {
                    Layout.fillWidth: true
                    radius: 12
                    color: "#162033"
                    border.width: 1
                    border.color: "#2B3A55"
                    implicitHeight: factorOverlaySummaryColumn.implicitHeight + 24

                    ColumnLayout {
                        id: factorOverlaySummaryColumn
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "因子排序层摘要"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: "#F8FAFC"
                            }

                            Rectangle {
                                radius: 9
                                color: resolveStrategyFactorOverlay().enabled ? "#082f49" : "#1f2937"
                                border.width: 1
                                border.color: resolveStrategyFactorOverlay().enabled ? "#0ea5e9" : "#475569"
                                implicitWidth: factorOverlayStrategyChipText.implicitWidth + 14
                                implicitHeight: 22

                                Text {
                                    id: factorOverlayStrategyChipText
                                    anchors.centerIn: parent
                                    text: resolveStrategyFactorOverlay().enabled ? "策略已定义" : "策略未定义"
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                    color: resolveStrategyFactorOverlay().enabled ? "#7dd3fc" : "#cbd5e1"
                                }
                            }

                            Rectangle {
                                radius: 9
                                color: resolveBacktestResultFactorOverlay().enabled ? "#0f3d2e" : "#1f2937"
                                border.width: 1
                                border.color: resolveBacktestResultFactorOverlay().enabled ? "#10b981" : "#475569"
                                implicitWidth: factorOverlayRuntimeChipText.implicitWidth + 14
                                implicitHeight: 22

                                Text {
                                    id: factorOverlayRuntimeChipText
                                    anchors.centerIn: parent
                                    text: resolveBacktestResultFactorOverlay().enabled ? "执行快照已启用" : "执行快照未启用"
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                    color: resolveBacktestResultFactorOverlay().enabled ? "#6ee7b7" : "#cbd5e1"
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "这里分开显示策略定义与本次执行快照，便于确认因子排序层是否真正进入回测，而不是只停留在保存参数里。"
                            font.pixelSize: 11
                            color: "#94A3B8"
                            wrapMode: Text.WordWrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 860 ? 2 : 1
                            columnSpacing: 12
                            rowSpacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 10
                                color: "#0F172A"
                                border.width: 1
                                border.color: resolveStrategyFactorOverlay().enabled ? "#0ea5e9" : "#334155"
                                implicitHeight: strategyFactorOverlayColumn.implicitHeight + 18

                                ColumnLayout {
                                    id: strategyFactorOverlayColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8

                                    Text {
                                        text: "已保存策略定义"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: "#E2E8F0"
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: factorOverlayTitleText(resolveStrategyFactorOverlay(), "当前策略未配置因子排序层")
                                        font.pixelSize: 12
                                        color: resolveStrategyFactorOverlay().enabled ? "#7dd3fc" : "#94A3B8"
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: factorOverlayDetailText(resolveStrategyFactorOverlay())
                                        font.pixelSize: 11
                                        color: "#CBD5E1"
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        visible: resolveStrategyFactorOverlay().enabled
                                        text: "总权重: " + Number(factorOverlayWeightTotal(resolveStrategyFactorOverlay()).toFixed(4)) + "%"
                                        font.pixelSize: 11
                                        color: "#94A3B8"
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 10
                                color: "#0F172A"
                                border.width: 1
                                border.color: resolveBacktestResultFactorOverlay().enabled ? "#10b981" : "#334155"
                                implicitHeight: runtimeFactorOverlayColumn.implicitHeight + 18

                                ColumnLayout {
                                    id: runtimeFactorOverlayColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8

                                    Text {
                                        text: "本次执行快照"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: "#E2E8F0"
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: factorOverlayTitleText(resolveBacktestResultFactorOverlay(), "当前还没有回测结果快照")
                                        font.pixelSize: 12
                                        color: resolveBacktestResultFactorOverlay().enabled ? "#6ee7b7" : "#94A3B8"
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: factorOverlayDetailText(resolveBacktestResultFactorOverlay())
                                        font.pixelSize: 11
                                        color: "#CBD5E1"
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        visible: resolveBacktestResultFactorOverlay().enabled
                                        text: "总权重: " + Number(factorOverlayWeightTotal(resolveBacktestResultFactorOverlay()).toFixed(4)) + "%"
                                        font.pixelSize: 11
                                        color: "#94A3B8"
                                    }
                                }
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
                                Layout.preferredWidth: 144
                                Layout.preferredHeight: 36
                                radius: 8
                                color: root.isBacktesting
                                    ? (root.backtestCollectingData ? "#92400E" : "#334155")
                                    : "#3B82F6"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: root.isBacktesting
                                            ? (root.backtestCollectingData ? "⏳" : "⏸️")
                                            : "▶️"
                                        font.pixelSize: 13
                                        color: root.isBacktesting ? "#94A3B8" : "white"
                                    }
                                    
                                    Text {
                                        text: root.isBacktesting
                                            ? (root.backtestCollectingData ? "收集数据中" : "回测执行中")
                                            : "开始回测"
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        color: root.isBacktesting ? "#94A3B8" : "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
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
                            Rectangle {
                                radius: 999
                                color: root.isBacktesting
                                    ? (root.backtestCollectingData ? "#7C2D12" : "#1D4ED8")
                                    : "#334155"
                                border.width: 1
                                border.color: root.isBacktesting
                                    ? (root.backtestCollectingData ? "#F59E0B" : "#60A5FA")
                                    : "#475569"
                                visible: root.isBacktesting || root.backtestStageLabel !== "等待开始"
                                Layout.preferredHeight: 24
                                Layout.preferredWidth: stageBadgeLabel.implicitWidth + 20

                                Text {
                                    id: stageBadgeLabel
                                    anchors.centerIn: parent
                                    text: root.backtestStageLabel
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    color: root.isBacktesting ? "#F8FAFC" : "#CBD5E1"
                                }
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
                            text: root.backtestActionHint
                            font.pixelSize: 9
                            color: root.isBacktesting
                                ? (root.backtestCollectingData ? "#F59E0B" : "#93C5FD")
                                : "#64748B"
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
            root.dynamicParamGenerator.reloadConfigs(root.dynamicParamConfigs, root.dynamicParamGroups)
            root.dynamicParamGenerator.setValues(root.dynamicParamValues || {})
        }
    }

    function ensureBacktestServicesReady() {
        if (root.uiLifecycleCoordinator && typeof root.uiLifecycleCoordinator.activateStrategyBacktestPage === "function") {
            root.uiLifecycleCoordinator.activateStrategyBacktestPage()
        }
    }

    function ensurePageReady() {
        if (pageServicesReady || !visible) {
            return
        }

        pageServicesReady = true
        root.ensureBacktestServicesReady()
        root.refreshStrategyOptions()
        root.ensureDynamicParamsReady()
        root.refreshIndustryFilterOptions()
        if (root.hasObjectData(root.selectedStrategyData)) {
            root.applyStrategyDefaults(root.selectedStrategyData)
        }
        root.syncDataSourceSelectionDisplay()
    }

    function hasObjectData(value) {
        return value && Object.keys(value).length > 0
    }

    function parseJsonArrayValue(rawValue) {
        if (Array.isArray(rawValue)) {
            return rawValue.slice()
        }

        var textValue = String(rawValue || "").trim()
        if (!textValue) {
            return []
        }

        try {
            var parsed = JSON.parse(textValue)
            return Array.isArray(parsed) ? parsed : []
        } catch (error) {
            return []
        }
    }

    function resolveStrategyFactorOverlay() {
        return root.structureAdapter.resolveFactorOverlay(root.selectedStrategyData || {}) || ({})
    }

    function resolveBacktestResultFactorOverlay() {
        if (!root.backtestResult || Object.keys(root.backtestResult).length === 0) {
            return ({})
        }

        return {
            enabled: !!root.backtestResult.factorOverlayEnabled,
            targetPositionCount: Number(root.backtestResult.factorOverlayTargetPositionCount || 0),
            minimumCompositeScore: Number(root.backtestResult.factorOverlayMinimumCompositeScore || 0),
            allocations: parseJsonArrayValue(root.backtestResult.factorOverlayAllocationsJson),
            factorIds: String(root.backtestResult.factorOverlayFactorIds || "")
        }
    }

    function factorOverlayAllocations(overlay) {
        return Array.isArray(overlay && overlay.allocations) ? overlay.allocations : []
    }

    function factorOverlayWeightTotal(overlay) {
        return factorOverlayAllocations(overlay).reduce(function(total, item) {
            return total + Number(item.weight_percent || item.weightPercent || item.weight || 0)
        }, 0)
    }

    function factorOverlayTitleText(overlay, emptyText) {
        if (!overlay || !overlay.enabled) {
            return emptyText
        }

        return "已启用因子排序层"
    }

    function factorOverlayDetailText(overlay) {
        if (!overlay || !overlay.enabled) {
            return "规则模板直接决定能否入场，不额外执行因子排序。"
        }

        return "目标持仓数 " + Number(overlay.targetPositionCount || 0)
            + "，最低综合分 " + Number(overlay.minimumCompositeScore || 0)
            + "，因子数 " + factorOverlayAllocations(overlay).length
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

    function normalizeTradingCostInput(value) {
        if (value === undefined || value === null || value === "") {
            return value
        }

        var numeric = Number(value)
        if (isNaN(numeric)) {
            return value
        }

        // 兼容历史错误值：若旧数据把 0.2 存成“0.2%”，这里还原成比例 0.002。
        if (numeric > 0.01) {
            return numeric / 100
        }

        return numeric
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

    function normalizeBooleanInput(value) {
        if (value === undefined || value === null || value === "") {
            return value
        }

        if (typeof value === "boolean") {
            return value
        }

        if (typeof value === "number") {
            return value !== 0
        }

        var normalizedText = String(value).trim().toLowerCase()
        if (normalizedText === "true" || normalizedText === "1" || normalizedText === "yes" || normalizedText === "on") {
            return true
        }
        if (normalizedText === "false" || normalizedText === "0" || normalizedText === "no" || normalizedText === "off") {
            return false
        }

        return !!value
    }

    function normalizeDelimitedTextInput(value) {
        if (value === undefined || value === null || value === "") {
            return ""
        }

        if (Array.isArray(value)) {
            var parts = value.map(function(item) {
                return String(item || "").trim()
            }).filter(function(item) {
                return item.length > 0
            })
            return parts.join(", ")
        }

        return String(value).trim()
    }

    function normalizeSelectOptionList(rawOptions, selectedValues) {
        var normalizedOptions = []
        var seenValues = ({})

        function appendOption(value, label) {
            var normalizedValue = String(value || "").trim()
            if (!normalizedValue || seenValues[normalizedValue]) {
                return
            }

            seenValues[normalizedValue] = true
            normalizedOptions.push({
                value: normalizedValue,
                label: String(label || normalizedValue).trim() || normalizedValue
            })
        }

        ;(rawOptions || []).forEach(function(option) {
            if (option === undefined || option === null) {
                return
            }

            if (typeof option === "object") {
                appendOption(option.value !== undefined ? option.value : option.label, option.label)
                return
            }

            appendOption(option, option)
        })

        ;(selectedValues || []).forEach(function(value) {
            appendOption(value, value)
        })

        normalizedOptions.sort(function(left, right) {
            return String(left.label || left.value).localeCompare(String(right.label || right.value), "zh-CN")
        })

        return normalizedOptions
    }

    function normalizeOptionListValue(value) {
        if (value === undefined || value === null || value === "") {
            return []
        }

        if (Array.isArray(value)) {
            return value.map(function(item) {
                return String(item || "").trim()
            }).filter(function(item) {
                return item.length > 0
            })
        }

        var rawText = String(value).trim()
        if (!rawText) {
            return []
        }

        if (rawText.charAt(0) === "[") {
            try {
                var parsed = JSON.parse(rawText)
                if (Array.isArray(parsed)) {
                    return parsed.map(function(item) {
                        return String(item || "").trim()
                    }).filter(function(item) {
                        return item.length > 0
                    })
                }
            } catch (error) {
            }
        }

        return rawText.split(/[,;\s，；]+/).map(function(item) {
            return String(item || "").trim()
        }).filter(function(item) {
            return item.length > 0
        })
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

    function buildDataSourceManagedFilterDefaults(dataSourceMode) {
        var normalizedMode = String(dataSourceMode || "raw").trim().toLowerCase()
        if (normalizedMode === "cleaned") {
            return {
                excludeSt: true,
                minListingDays: 60
            }
        }

        return {
            excludeSt: false,
            minListingDays: 0
        }
    }

    function buildDataSourceManagedFilterOverrideState(sourceValues) {
        var nextState = ({})
        root.dataSourceManagedFilterKeys.forEach(function(key) {
            nextState[key] = !!(sourceValues && sourceValues[key] !== undefined)
        })
        return nextState
    }

    function mergeDataSourceManagedFilterOverrideState(baseState, sourceValues) {
        var nextState = JSON.parse(JSON.stringify(baseState || {}))
        root.dataSourceManagedFilterKeys.forEach(function(key) {
            if (nextState[key] === undefined) {
                nextState[key] = false
            }
            if (sourceValues && sourceValues[key] !== undefined) {
                nextState[key] = true
            }
        })
        return nextState
    }

    function managedFilterValuesEqual(key, leftValue, rightValue) {
        if (key === "excludeSt") {
            return normalizeBooleanInput(leftValue) === normalizeBooleanInput(rightValue)
        }

        return Number(leftValue || 0) === Number(rightValue || 0)
    }

    function recordDataSourceManagedFilterOverrides(previousValues, nextValues) {
        var resolvedPreviousValues = previousValues || ({})
        var resolvedNextValues = nextValues || ({})
        var nextState = JSON.parse(JSON.stringify(root.dataSourceManagedFilterOverrides || {}))
        var changed = false

        root.dataSourceManagedFilterKeys.forEach(function(key) {
            if (!root.managedFilterValuesEqual(key, resolvedPreviousValues[key], resolvedNextValues[key])) {
                nextState[key] = true
                changed = true
            } else if (nextState[key] === undefined) {
                nextState[key] = false
                changed = true
            }
        })

        if (changed) {
            root.dataSourceManagedFilterOverrides = nextState
        }
    }

    function applyDataSourceManagedFilterDefaults(sourceValues, dataSourceMode) {
        var nextValues = JSON.parse(JSON.stringify(sourceValues || {}))
        var defaults = root.buildDataSourceManagedFilterDefaults(
            dataSourceMode !== undefined ? dataSourceMode : nextValues.dataSourceMode)
        var overrideState = root.dataSourceManagedFilterOverrides || ({})

        root.dataSourceManagedFilterKeys.forEach(function(key) {
            if (!overrideState[key] && defaults[key] !== undefined) {
                nextValues[key] = defaults[key]
            }
        })

        return nextValues
    }

    function commitDynamicParamValues(nextValues) {
        var resolvedValues = JSON.parse(JSON.stringify(nextValues || {}))
        dynamicParamValues = resolvedValues
        if (dynamicParamGenerator) {
            expectedProgrammaticDynamicParamPayload = JSON.stringify(resolvedValues)
            dynamicParamGenerator.setValues(resolvedValues)
        } else {
            expectedProgrammaticDynamicParamPayload = ""
        }

        syncDataSourceSelectionDisplay()
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
        var topN = firstDefinedValue(resolvedConfig, ["top_n", "topN", "maxPositions", "targetPositionCount"])
        if (topN !== undefined) normalized.top_n = Number(topN)
        var minCompositeScore = firstDefinedValue(resolvedConfig, ["minCompositeScore", "scoreThreshold", "minScore"])
        if (minCompositeScore !== undefined) normalized.minCompositeScore = Number(minCompositeScore)
        var commissionRate = firstDefinedValue(resolvedConfig, ["commissionRate", "transactionCost"])
        if (commissionRate !== undefined) normalized.commissionRate = normalizeTradingCostInput(commissionRate)
        var slippageRate = firstDefinedValue(resolvedConfig, ["slippageRate", "slippageCost"])
        if (slippageRate !== undefined) normalized.slippageRate = normalizeTradingCostInput(slippageRate)
        if (resolvedConfig.varWarningPercent !== undefined) normalized.varWarningPercent = Number(resolvedConfig.varWarningPercent)
        if (resolvedConfig.orderSizeLimit !== undefined) normalized.orderSizeLimit = Number(resolvedConfig.orderSizeLimit)
        if (resolvedConfig.turnoverLimit !== undefined) normalized.turnoverLimit = Number(resolvedConfig.turnoverLimit)
        if (resolvedConfig.slippageLimit !== undefined) normalized.slippageLimit = Number(resolvedConfig.slippageLimit)
        if (resolvedConfig.level1Breaker !== undefined) normalized.level1Breaker = Number(resolvedConfig.level1Breaker)
        if (resolvedConfig.level2Breaker !== undefined) normalized.level2Breaker = Number(resolvedConfig.level2Breaker)
        if (resolvedConfig.level3Breaker !== undefined) normalized.level3Breaker = Number(resolvedConfig.level3Breaker)
        if (resolvedConfig.enableAdvancedOptions !== undefined) normalized.enableAdvancedOptions = !!resolvedConfig.enableAdvancedOptions
        if (resolvedConfig.enableWalkForward !== undefined) normalized.enableWalkForward = !!resolvedConfig.enableWalkForward
        if (resolvedConfig.enableMonteCarlo !== undefined) normalized.enableMonteCarlo = !!resolvedConfig.enableMonteCarlo
        if (resolvedConfig.monteCarloSamples !== undefined) normalized.monteCarloSamples = Number(resolvedConfig.monteCarloSamples)
        if (resolvedConfig.enableOutOfSample !== undefined) normalized.enableOutOfSample = !!resolvedConfig.enableOutOfSample
        if (resolvedConfig.outOfSampleRatio !== undefined) normalized.outOfSampleRatio = Number(resolvedConfig.outOfSampleRatio)
        var marketFilters = firstDefinedValue(resolvedConfig, ["marketFilters", "market_filters"])
        if (marketFilters !== undefined) normalized.marketFilters = normalizeOptionListValue(marketFilters)
        var sectorFilters = firstDefinedValue(resolvedConfig, ["sectorFilters", "sector_filters", "industryFilters", "industry_filters"])
        if (sectorFilters !== undefined) normalized.sectorFilters = normalizeOptionListValue(sectorFilters)
        var excludeSt = firstDefinedValue(resolvedConfig, ["excludeSt", "exclude_st", "stFilter", "st_filter", "stFilterEnabled"])
        if (excludeSt !== undefined) normalized.excludeSt = normalizeBooleanInput(excludeSt)
        var minListingDays = firstDefinedValue(resolvedConfig, ["minListingDays", "minTradeDays", "minListedDays", "listingDays"])
        if (minListingDays !== undefined) normalized.minListingDays = Number(minListingDays)
        var minTurnoverRate = firstDefinedValue(resolvedConfig, ["minTurnoverRate", "minTurnover", "minLiquidity", "liquidityThreshold"])
        if (minTurnoverRate !== undefined) normalized.minTurnoverRate = Number(minTurnoverRate)

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

    function mergeDynamicParamValues(overrides, options) {
        if (!hasObjectData(overrides)) {
            return
        }

        var resolvedOptions = options || ({})
        var nextValues = JSON.parse(JSON.stringify(dynamicParamValues || {}))
        for (var key in overrides) {
            nextValues[key] = overrides[key]
        }

        if (resolvedOptions.overrideState) {
            dataSourceManagedFilterOverrides = JSON.parse(JSON.stringify(resolvedOptions.overrideState))
        }

        if (resolvedOptions.applyDataSourceDefaults) {
            nextValues = applyDataSourceManagedFilterDefaults(nextValues, nextValues.dataSourceMode)
        }

        commitDynamicParamValues(nextValues)
    }

    function updateDynamicParamConfig(paramId, mutateConfig) {
        if (!mutateConfig || !dynamicParamConfigs || dynamicParamConfigs.length === 0) {
            return
        }

        var changed = false
        var nextConfigs = dynamicParamConfigs.map(function(config) {
            if (!config || config.id !== paramId) {
                return config
            }

            var clonedConfig = JSON.parse(JSON.stringify(config))
            mutateConfig(clonedConfig)
            if (JSON.stringify(clonedConfig) !== JSON.stringify(config)) {
                changed = true
            }
            return clonedConfig
        })

        if (!changed) {
            return
        }

        var preservedValues = JSON.parse(JSON.stringify(dynamicParamValues || {}))
        dynamicParamConfigs = orderDynamicParamConfigs(nextConfigs)
        dynamicParamGroups = buildDynamicParamGroups(dynamicParamConfigs)
        if (dynamicParamGenerator) {
            dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, dynamicParamGroups)
            dynamicParamGenerator.setValues(preservedValues)
        }
        dynamicParamValues = preservedValues
    }

    function syncIndustryFilterParamConfig() {
        updateDynamicParamConfig("sectorFilters", function(config) {
            config.type = "multiselect"
            config.multiple = true
            config.options = root.normalizeSelectOptionList(
                root.availableIndustryOptions,
                root.normalizeOptionListValue(root.dynamicParamValues.sectorFilters))
            config.default = Array.isArray(config.default) ? config.default : []
            delete config.multiline
            delete config.placeholder
        })
    }

    function refreshIndustryFilterOptions() {
        if (!root.strategyBacktestController || typeof root.strategyBacktestController.getAvailableIndustries !== "function") {
            return
        }

        var rawIndustries = root.strategyBacktestController.getAvailableIndustries() || []
        root.availableIndustryOptions = root.normalizeSelectOptionList(
            rawIndustries,
            root.normalizeOptionListValue(root.dynamicParamValues.sectorFilters))
        root.syncIndustryFilterParamConfig()
    }

    function appendMetaParamConfigs(targetConfigs, paramConfigs) {
        ;(paramConfigs || []).forEach(function(paramConfig) {
            if (!paramConfig || !paramConfig.id) {
                return
            }

            var config = {
                id: paramConfig.id,
                type: paramConfig.type,
                label: paramConfig.label,
                description: paramConfig.description,
                default: paramConfig.default,
                category: paramConfig.category,
                group: paramConfig.group || paramConfig.category || "回测配置"
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
                    config.type = paramConfig.multiple ? "multiselect" : "select"
                    config.options = paramConfig.options || []
                    config.multiple = paramConfig.multiple || false
                    break
                case "multiselect":
                    config.type = "multiselect"
                    config.options = paramConfig.options || []
                    config.multiple = true
                    break
                case "toggle":
                    config.type = "toggle"
                    config.trueLabel = paramConfig.trueLabel || "是"
                    config.falseLabel = paramConfig.falseLabel || "否"
                    break
                case "input":
                    config.type = "input"
                    config.multiline = paramConfig.multiline || false
                    config.placeholder = paramConfig.placeholder || ""
                    config.maxLength = paramConfig.maxLength
                    break
            }

            if (paramConfig.visibleWhen) {
                config.visibleWhen = paramConfig.visibleWhen
            }

            targetConfigs.push(config)
        })
    }

    function preferredBacktestParamGroups() {
        return [
            {
                id: "coreBacktest",
                name: "核心回测",
                description: "这里只保留运行期参数，例如区间、资金、基准、选股数量与交易成本；策略自带的止损、止盈、仓位和调仓定义不再重复配置。",
                minColumnWidth: 760,
                maxColumns: 2,
                params: ["backtestPeriod", "initialCapital", "benchmark", "top_n", "commissionRate", "slippageRate"]
            },
            {
                id: "stockPoolFilters",
                name: "股票池筛选",
                description: "在已选股票池基础上，再按市场、行业、ST、上市天数、流动性和综合分阈值继续收口。",
                minColumnWidth: 760,
                maxColumns: 2,
                params: ["marketFilters", "sectorFilters", "excludeSt", "minListingDays", "minTurnoverRate", "minCompositeScore"]
            },
            {
                id: "advancedOptions",
                name: "高级选项",
                description: "包含组合风控阈值、成交限制和高级回测实验能力。",
                minColumnWidth: 760,
                maxColumns: 1,
                params: [
                    "varWarningPercent",
                    "orderSizeLimit",
                    "turnoverLimit",
                    "slippageLimit",
                    "level1Breaker",
                    "level2Breaker",
                    "level3Breaker",
                    "enableAdvancedOptions",
                    "enableWalkForward",
                    "enableMonteCarlo",
                    "monteCarloSamples",
                    "enableOutOfSample",
                    "outOfSampleRatio"
                ]
            }
        ]
    }

    function buildDynamicParamGroups(configs) {
        var configIdMap = ({})
        ;(configs || []).forEach(function(config) {
            if (config && config.id) {
                configIdMap[config.id] = true
            }
        })

        var groups = []
        preferredBacktestParamGroups().forEach(function(group) {
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

        preferredBacktestParamGroups().forEach(function(group) {
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
            rebalancingPeriod: "rebalanceDays"
        }
        var excludedIds = {
            backtestPeriod: true,
            backtestYears: true,
            positionPercent: true,
            rebalancingPeriod: true,
            rebalanceDays: true,
            stopLossPercent: true,
            takeProfitPercent: true,
            maxDrawdownLimit: true,
            autoStopEnabled: true,
            positionSizingMethod: true,
            maxTotalExposure: true,
            maxPositionPercent: true
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

        return root.orderDynamicParamConfigs(sanitized)
    }

    function applyBacktestSessionConfig(config) {
        pendingBacktestConfig = JSON.parse(JSON.stringify(config || {}))
        if (pendingBacktestConfig.startDate) {
            selectedStartDate = String(pendingBacktestConfig.startDate)
        }
        if (pendingBacktestConfig.endDate) {
            selectedEndDate = String(pendingBacktestConfig.endDate)
        }
        var normalizedConfig = normalizeBacktestSessionConfig(pendingBacktestConfig)
        dataSourceManagedFilterOverrides = mergeDataSourceManagedFilterOverrideState(
            dataSourceManagedFilterOverrides,
            normalizedConfig)
        mergeDynamicParamValues(normalizedConfig, { applyDataSourceDefaults: true })
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

    function buildMissingUniverseResolution(sourceKey, sourceLabel) {
        return buildConfiguredUniverseResolution([], sourceKey, sourceLabel)
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
                ? root.structureAdapter.resolvePersistedBacktestSymbolPool(config)
                : root.structureAdapter.resolvePersistedBacktestSymbolPool(config)

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
            return "当前策略没有已保存回测池，也没有关联自选池；当前实现已禁止默认回退到全市场，请先明确配置回测股票池。"
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
                : buildMissingUniverseResolution("selectedStrategy", "已保存回测池为空")
        }

        if (selectedStrategyUniverseMode === "linked") {
            return candidates.linkedState.count > 0
                ? candidates.linkedState
                : buildMissingUniverseResolution("selectedLinkedStockPool", "关联自选池为空")
        }

        if (selectedStrategyUniverseMode === "merged") {
            return candidates.mergedState.count > 0
                ? candidates.mergedState
                : buildMissingUniverseResolution("selectedMergedUniverse", "合并股票池为空")
        }

        if (candidates.configuredState.count > 0) {
            return candidates.configuredState
        }

        if (candidates.linkedState.count > 0) {
            return candidates.linkedState
        }

        return buildMissingUniverseResolution("missingStrategyUniverse", "未配置已保存回测池或关联自选池")
    }

    function buildBacktestUniverseSummary(universeState) {
        if (!universeState) {
            return ""
        }

        if (universeState.sourceKey === "indexUniverse") {
            return universeState.sourceLabel
        }

        if (universeState.count <= 0) {
            return universeState.sourceLabel || "未配置回测股票池"
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
        root.riskBacktestMetaLoader.loadMetaFile(root.sharedBacktestMetaPath, function(sharedMeta) {
            if (!sharedMeta) {
                console.error("加载共享回测参数配置失败，使用默认配置")
                paramLoadWatchdog.stop()
                root.generateFallbackParamConfigs()
                return
            }

            root.riskBacktestMetaLoader.loadMetaFile(root.pageOnlyBacktestMetaPath, function(pageOnlyMeta) {
                if (!pageOnlyMeta) {
                    console.error("加载页面专用回测参数配置失败，使用默认配置")
                    paramLoadWatchdog.stop()
                    root.generateFallbackParamConfigs()
                    return
                }

                console.log("成功加载策略回测参数配置")
                root.dynamicParamConfigs = []

                root.appendMetaParamConfigs(
                    root.dynamicParamConfigs,
                    root.riskBacktestMetaLoader.getParameterConfigs("all", root.sharedBacktestMetaPath))
                root.appendMetaParamConfigs(
                    root.dynamicParamConfigs,
                    root.riskBacktestMetaLoader.getParameterConfigs("all", root.pageOnlyBacktestMetaPath))

                root.dynamicParamConfigs = root.sanitizeDynamicParamConfigs(root.dynamicParamConfigs)
                root.dynamicParamGroups = root.buildDynamicParamGroups(root.dynamicParamConfigs)
                root.syncIndustryFilterParamConfig()
                console.log("策略回测动态参数配置加载完成，数量:", root.dynamicParamConfigs.length)

                if (root.dynamicParamGenerator) {
                    root.dynamicParamGenerator.reloadConfigs(root.dynamicParamConfigs, root.dynamicParamGroups)
                } else {
                    root.initDynamicValues()
                }

                root.initDynamicValues()
                root.parametersLoaded = true
                paramLoadWatchdog.stop()
            })
        })
    }
    
    // 后备参数配置（当动态加载失败时使用）
    function generateFallbackParamConfigs() {
        root.dynamicParamConfigs = []
        root.appendMetaParamConfigs(
            root.dynamicParamConfigs,
            root.riskBacktestMetaLoader.getParameterConfigs("all", root.sharedBacktestMetaPath))
        root.appendMetaParamConfigs(
            root.dynamicParamConfigs,
            root.riskBacktestMetaLoader.getParameterConfigs("all", root.pageOnlyBacktestMetaPath))
        root.dynamicParamConfigs = root.sanitizeDynamicParamConfigs(root.dynamicParamConfigs)
        root.dynamicParamGroups = root.buildDynamicParamGroups(root.dynamicParamConfigs)
        root.syncIndustryFilterParamConfig()
        console.log("使用后备策略回测参数配置，数量:", root.dynamicParamConfigs.length)

        if (root.dynamicParamGenerator) {
            root.dynamicParamGenerator.reloadConfigs(root.dynamicParamConfigs, root.dynamicParamGroups)
        }
        root.initDynamicValues()
        root.parametersLoaded = true
        paramLoadWatchdog.stop()
    }
    
    // 初始化动态参数值
    function initDynamicValues() {
        var values = root.buildBaseDynamicParamValues()
        root.dataSourceManagedFilterOverrides = root.buildDataSourceManagedFilterOverrideState(root.loadAppliedRiskBacktestDefaults())
        values = root.applyDataSourceManagedFilterDefaults(values, values.dataSourceMode)
        root.commitDynamicParamValues(values)

        if (root.hasObjectData(root.selectedStrategyData)) {
            root.applyStrategyDefaults(root.selectedStrategyData)
        }

        if (root.hasObjectData(root.pendingBacktestConfig)) {
            root.mergeDynamicParamValues(root.normalizeBacktestSessionConfig(root.pendingBacktestConfig))
        }

        root.refreshIndustryFilterOptions()
        
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
        delete runtimeValues.positionSize
        delete runtimeValues.maxPositionPercent
        delete runtimeValues.maxTotalExposure
        delete runtimeValues.stopLoss
        delete runtimeValues.stopLossPercent
        delete runtimeValues.autoStopEnabled
        delete runtimeValues.takeProfit
        delete runtimeValues.takeProfitPercent
        delete runtimeValues.maxDrawdownLimit
        delete runtimeValues.rebalancingPeriod
        delete runtimeValues.rebalanceDays
        delete runtimeValues.positionSizingMethod
        delete runtimeValues.transactionCost
        delete runtimeValues.slippageCost
        runtimeValues.startDate = selectedStartDate
        runtimeValues.endDate = selectedEndDate
        return runtimeValues
    }

    function extractLegacyBacktestParameterValues(parameters) {
        return {
            commissionRate: firstDefinedValue(parameters, ["commissionRate", "commission", "transactionCost", "transaction_cost"]),
            topN: firstDefinedValue(parameters, ["top_n", "topN", "maxPositions", "targetPositionCount"]),
            minCompositeScore: firstDefinedValue(parameters, ["minCompositeScore", "scoreThreshold", "minScore"]),
            slippageRate: firstDefinedValue(parameters, ["slippageRate", "slippage", "slippageCost"]),
            marketFilters: firstDefinedValue(parameters, ["marketFilters", "market_filters"]),
            sectorFilters: firstDefinedValue(parameters, ["sectorFilters", "sector_filters", "industryFilters", "industry_filters"]),
            excludeSt: firstDefinedValue(parameters, ["excludeSt", "exclude_st", "stFilter", "st_filter"]),
            minListingDays: firstDefinedValue(parameters, ["minListingDays", "minTradeDays", "minListedDays", "listingDays"]),
            minTurnoverRate: firstDefinedValue(parameters, ["minTurnoverRate", "minTurnover", "minLiquidity", "liquidityThreshold"]),
            varWarningPercent: firstDefinedValue(parameters, ["varWarningPercent"]),
            orderSizeLimit: firstDefinedValue(parameters, ["orderSizeLimit"]),
            turnoverLimit: firstDefinedValue(parameters, ["turnoverLimit"]),
            slippageLimit: firstDefinedValue(parameters, ["slippageLimit"]),
            level1Breaker: firstDefinedValue(parameters, ["level1Breaker"]),
            level2Breaker: firstDefinedValue(parameters, ["level2Breaker"]),
            level3Breaker: firstDefinedValue(parameters, ["level3Breaker"])
        }
    }

    function buildLegacyBacktestRuntime(parameters, backtestSettings) {
        var legacyValues = extractLegacyBacktestParameterValues(parameters)
        return normalizeBacktestSessionConfig({
            startDate: backtestSettings.start_date,
            endDate: backtestSettings.end_date,
            backtestYears: backtestSettings.years,
            benchmark: backtestSettings.benchmark,
            top_n: legacyValues.topN,
            minCompositeScore: legacyValues.minCompositeScore,
            commissionRate: legacyValues.commissionRate !== undefined ? legacyValues.commissionRate : backtestSettings.transaction_cost,
            marketFilters: legacyValues.marketFilters,
            sectorFilters: legacyValues.sectorFilters,
            excludeSt: legacyValues.excludeSt,
            minListingDays: legacyValues.minListingDays,
            minTurnoverRate: legacyValues.minTurnoverRate,
            initialCapital: parameters.initialCapital,
            slippageRate: legacyValues.slippageRate,
            varWarningPercent: legacyValues.varWarningPercent,
            orderSizeLimit: legacyValues.orderSizeLimit,
            turnoverLimit: legacyValues.turnoverLimit,
            slippageLimit: legacyValues.slippageLimit,
            level1Breaker: legacyValues.level1Breaker,
            level2Breaker: legacyValues.level2Breaker,
            level3Breaker: legacyValues.level3Breaker,
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
        var overrideState = buildDataSourceManagedFilterOverrideState(loadAppliedRiskBacktestDefaults())

        var persistedRuntime = extractPersistedBacktestRuntime(strategy)
        for (var key in persistedRuntime) {
            nextValues[key] = persistedRuntime[key]
        }
        overrideState = mergeDataSourceManagedFilterOverrideState(overrideState, persistedRuntime)
        dataSourceManagedFilterOverrides = overrideState
        nextValues = applyDataSourceManagedFilterDefaults(nextValues, nextValues.dataSourceMode)

        if (persistedRuntime.startDate) {
            selectedStartDate = persistedRuntime.startDate
        }
        if (persistedRuntime.endDate) {
            selectedEndDate = persistedRuntime.endDate
        }

        commitDynamicParamValues(nextValues)
        refreshIndustryFilterOptions()
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
            root.backtestStatus = "当前未解析到明确回测股票池；请先配置已保存回测池、关联自选池或切换到指数成分股模式"
            return
        }

        root.prepareForNextBacktest()

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

    function clearPendingStrategyBacktestState() {
        pendingStrategyPerformancePayload = ({})
        pendingStrategyCoverageSummary = ""
        pendingStrategyCoverageDecision = ""
        pendingStrategyPreviousBacktest = ({})
    }

    function prepareForNextBacktest() {
        if (strategyCoverageDecisionDialog.visible) {
            strategyCoverageDecisionDialog.close()
        }

        clearPendingStrategyBacktestState()
        backtestResult = ({})
        backtestProgress = 0
        backtestCollectingData = false
        backtestStageLabel = "准备提交回测"

        if (strategyBacktestController && typeof strategyBacktestController.prepareForNextRun === "function") {
            strategyBacktestController.prepareForNextRun(true)
        }
    }

    function zeroTradeBlockedBacktestSummary() {
        if (!backtestResult || typeof backtestResult !== "object") {
            return ""
        }

        var totalTrades = Number(backtestResult.totalTrades || 0)
        var ruleSummary = backtestResult.ruleTemplateSummary || ({})
        var entryBlockCount = Number(ruleSummary.entryBlockCount || backtestResult.ruleTemplateEntryBlockCount || 0)
        if (totalTrades !== 0 || entryBlockCount <= 0) {
            return ""
        }

        var recentEvents = Array.isArray(ruleSummary.recentEvents) ? ruleSummary.recentEvents : []
        var latestEvent = recentEvents.length > 0 ? recentEvents[recentEvents.length - 1] : null
        var latestMessage = latestEvent && latestEvent.message ? String(latestEvent.message) : ""
        var latestGroupTitle = latestEvent && latestEvent.groupTitle ? String(latestEvent.groupTitle) : ""

        var message = "本次回测已完成，但规则共阻止了 " + entryBlockCount + " 次入场，未产生任何成交。"
        if (latestGroupTitle) {
            message += " 最近阻断来自“" + latestGroupTitle + "”。"
        }
        if (latestMessage) {
            message += " 原因：" + latestMessage
        }
        return message
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

        var zeroTradeBlockedSummary = zeroTradeBlockedBacktestSummary()
        if (zeroTradeBlockedSummary.length > 0) {
            pendingStrategyCoverageDecision = "keep"
            pendingStrategyCoverageSummary = zeroTradeBlockedSummary + " 已仅追加本次回测历史，不覆盖上一轮基线。"
            if (commitStrategyBacktestPerformance(false)) {
                backtestStatus = pendingStrategyCoverageSummary
                clearPendingStrategyBacktestState()
            }
            return
        }

        if (coverageDecision.action === "ask") {
            strategyCoverageDecisionDialog.open()
            return
        }

        var replaceLatestBacktest = coverageDecision.action === "replace"
        if (commitStrategyBacktestPerformance(replaceLatestBacktest)) {
            backtestStatus = coverageDecision.summary || backtestStatus
            clearPendingStrategyBacktestState()
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
                root.clearPendingStrategyBacktestState()
            }
        }

        onRejected: {
            if (root.commitStrategyBacktestPerformance(false)) {
                root.backtestStatus = root.pendingStrategyCoverageSummary.length > 0
                    ? root.pendingStrategyCoverageSummary
                    : "已保留上一轮股票池基线，仅记录本次回测历史"
                root.clearPendingStrategyBacktestState()
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
        if (visible) {
            backtestPageActivationTimer.start()
        }
    }

    onVisibleChanged: {
        if (visible && !pageServicesReady) {
            backtestPageActivationTimer.start()
        }
    }

    Timer {
        id: backtestPageActivationTimer
        interval: 0
        repeat: false
        onTriggered: root.ensurePageReady()
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

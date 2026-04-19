// PortfolioBuilderPage.qml
// 增强版组合构建页面，实现拖拽式量化因子组合构建
// 遵循即时反馈、智能辅助、极简路径三大原则
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs 
import ConsoleUi 1.0 as ConsoleUiComponents
import AStock.Bridge 1.0 as Bridge
import "../components/Factor" as FactorComponents
import "../components/Navigation" as Navigation
import "../components/TopNavigation" as TopNavigation
import "../components/Base" as BaseComponents
import "../components/Risk" as RiskComponents
import "../utils/StartupGateFormatter.js" as StartupGateFormatter

/**
 * 增强版组合构建页面 - 拖拽式量化因子组合构建器
 * 三栏式设计：左侧因子池、中间组合构建器、右侧风险监控
 * 底部通知栏，支持实时预览和智能辅助
 */
Item {
    id: root

    signal requestBacktest(string strategyId, string strategyName, var backtestConfig)
    signal requestNavigation(string menuCode, string menuTitle, var navigationPayload)
    
    // ============ 页面属性 ============
    
    property string currentPortfolioId: "momentum_portfolio"
    property string portfolioName: "动量组合"
    property string lastRestoredPortfolioId: ""
    property bool hasPendingPortfolioEdits: false
    property real totalWeight: 100.0
    property var currentPortfolio: []
    property var currentStrategyDetail: ({})
    readonly property var factorService: Bridge.FactorService
    readonly property var portfolioAnalysisService: Bridge.PortfolioAnalysisService
    readonly property var riskConfigService: Bridge.RiskConfigService
    readonly property var strategyService: Bridge.StrategyService
    readonly property var tradingConnectionConfigService: Bridge.TradingConnectionConfigService
    readonly property var riskMonitorService: Bridge.RiskMonitorService
    readonly property var marketDataService: Bridge.MarketDataService
    property var factorViewModel: factorService ? factorService.getViewModel() : null
    property int initialPortfolioSize: 4
    readonly property var emptySectorExposure: ({
        "银行": 0.0,
        "消费": 0.0,
        "医药": 0.0,
        "科技": 0.0
    })
    readonly property var emptyStyleExposure: ({
        "市值": 0.0,
        "动量": 0.0,
        "价值": 0.0,
        "波动率": 0.0
    })
    property var portfolioState: ({
        metrics: {
            annualReturn: 0.0,
            sharpeRatio: 0.0,
            maxDrawdown: 0.0
        },
        exposures: {
            sector: {
                "银行": 0.0,
                "消费": 0.0,
                "医药": 0.0,
                "科技": 0.0
            },
            style: {
                "市值": 0.0,
                "动量": 0.0,
                "价值": 0.0,
                "波动率": 0.0
            }
        },
        snapshot: {
            status: "idle",
            positions: [],
            diagnostics: {}
        },
        backtest: {},
        systemStatus: {},
        notifications: [
            { type: "info", text: "等待加载真实因子数据", time: "当前", action: "刷新" }
        ],
        insights: {
            suggestion: "等待组合加载完成后生成建议"
        },
        lastUpdated: ""
    })
    readonly property var portfolioMetrics: portfolioState && portfolioState.metrics
        ? portfolioState.metrics
        : ({ annualReturn: 0.0, sharpeRatio: 0.0, maxDrawdown: 0.0 })
    readonly property var portfolioExposures: portfolioState && portfolioState.exposures
        ? portfolioState.exposures
        : ({ sector: emptySectorExposure, style: emptyStyleExposure })
    readonly property var sectorExposure: portfolioExposures && portfolioExposures.sector
        ? portfolioExposures.sector
        : emptySectorExposure
    readonly property var styleExposure: portfolioExposures && portfolioExposures.style
        ? portfolioExposures.style
        : emptyStyleExposure
    readonly property var portfolioSnapshot: portfolioState && portfolioState.snapshot
        ? portfolioState.snapshot
        : ({ status: "idle", positions: [], diagnostics: {} })
    readonly property var latestBacktestRecord: portfolioState && portfolioState.backtest
        ? portfolioState.backtest
        : ({})
    readonly property var systemStatus: portfolioState && portfolioState.systemStatus
        ? portfolioState.systemStatus
        : ({})
    readonly property var notifications: portfolioState && portfolioState.notifications
        ? portfolioState.notifications
        : []
    readonly property var visibleNotifications: filterVisibleNotifications(notifications)
    readonly property string portfolioSuggestion: portfolioState
        && portfolioState.insights
        && portfolioState.insights.suggestion
        ? portfolioState.insights.suggestion
        : "等待组合加载完成后生成建议"
    readonly property bool usingBacktestMetrics: String(portfolioMetrics.source || "") === "latestBacktest"
    readonly property string metricPanelTitle: usingBacktestMetrics ? "真实回测绩效" : "估算绩效"
    readonly property string metricPanelSubtitle: usingBacktestMetrics
        ? ("来源: 最近回测" + (portfolioMetrics.recordedAt ? " · " + portfolioMetrics.recordedAt : ""))
        : "来源: 当前组合估算"
    readonly property string riskPanelSubtitle: portfolioSnapshot.status === "success"
        ? ("快照日 " + String(portfolioSnapshot.snapshotDate || portfolioSnapshot.recordedAt || "当前"))
        : (usingBacktestMetrics && latestBacktestRecord.recordedAt
            ? ("参考回测 " + latestBacktestRecord.recordedAt)
            : "当前组合候选快照")
    readonly property string exposurePanelSubtitle: portfolioSnapshot.status === "success"
        ? "行业/市值暴露基于快照持仓，其他风格维度保留组合估算"
        : "当前暴露仍基于组合结构估算"
    readonly property var snapshotPositions: portfolioSnapshot && portfolioSnapshot.positions
        ? portfolioSnapshot.positions
        : []
    readonly property var snapshotDiagnostics: portfolioSnapshot && portfolioSnapshot.diagnostics
        ? portfolioSnapshot.diagnostics
        : ({})
    readonly property string snapshotPanelSubtitle: portfolioSnapshot.status === "success"
        ? ("候选 " + snapshotPositions.length + " 只 · 因子快照 " + Number(snapshotDiagnostics.factorSnapshotCount || 0) + " 份")
        : (portfolioSnapshot.error ? String(portfolioSnapshot.error) : "等待组合快照生成后展示候选持仓")
    property var executionPlanState: ({
        success: false,
        message: "尚未生成调仓计划预览",
        orders: [],
        batches: [],
        skippedOrders: [],
        summary: {},
        generatedAt: ""
    })
    readonly property var executionOrders: executionPlanState && executionPlanState.orders
        ? executionPlanState.orders
        : []
    readonly property var executionBatches: executionPlanState && executionPlanState.batches
        ? executionPlanState.batches
        : []
    readonly property var skippedExecutionOrders: executionPlanState && executionPlanState.skippedOrders
        ? executionPlanState.skippedOrders
        : []
    readonly property var executionPlanSummary: executionPlanState && executionPlanState.summary
        ? executionPlanState.summary
        : ({})
    property string executionPlanFingerprint: ""
    readonly property string currentExecutionPlanFingerprint: buildExecutionPlanFingerprint()
    readonly property bool hasExecutionPlanPreview: (executionOrders.length > 0)
        || (skippedExecutionOrders.length > 0)
        || String(executionPlanState.generatedAt || "").length > 0
    readonly property bool executionPlanStale: hasExecutionPlanPreview
        && String(executionPlanFingerprint || "").length > 0
        && executionPlanFingerprint !== currentExecutionPlanFingerprint
    readonly property string executionPlanSubtitle: !hasExecutionPlanPreview
        ? "按当前组合和账户持仓生成只读调仓计划"
        : (executionPlanStale
            ? "当前预览基于旧的组合状态，请重新生成"
            : (executionPlanState.generatedAt
                ? ("生成时间 " + String(executionPlanState.generatedAt))
                : String(executionPlanState.message || "调仓计划已生成")))
    readonly property int portfolioFactorColumnWidth: 220
    readonly property int portfolioWeightColumnWidth: 190
    readonly property int portfolioCorrelationColumnWidth: 110
    readonly property int portfolioActionColumnWidth: 72
    readonly property int snapshotSymbolColumnWidth: 120
    readonly property int snapshotWeightColumnWidth: 76
    readonly property int snapshotScoreColumnWidth: 56
    readonly property int snapshotStatusColumnWidth: 76
    readonly property int snapshotPriceColumnWidth: 70
    readonly property int executionActionColumnWidth: 72
    readonly property int executionSymbolColumnWidth: 120
    readonly property int executionQuantityColumnWidth: 86
    readonly property int executionWeightColumnWidth: 126
    readonly property int executionPriceColumnWidth: 78
    property string snapshotFilter: "all"
    property string snapshotSortKey: "score_desc"
    property int selectedSnapshotIndex: -1
    readonly property var filteredSnapshotPositions: sortSnapshotPositions(filterSnapshotPositions(snapshotPositions, snapshotFilter), snapshotSortKey)
    readonly property var activeSnapshotPosition: selectedSnapshotIndex >= 0 && selectedSnapshotIndex < filteredSnapshotPositions.length
        ? filteredSnapshotPositions[selectedSnapshotIndex]
        : (filteredSnapshotPositions.length > 0 ? filteredSnapshotPositions[0] : ({}))
    readonly property var activeSnapshotInstrument: activeSnapshotPosition
        && activeSnapshotPosition.symbol
        && marketDataService
        && marketDataService.resolveInstrument
        ? (marketDataService.resolveInstrument(String(activeSnapshotPosition.symbol)) || ({}))
        : ({})
    readonly property bool hasActiveSnapshotPosition: activeSnapshotPosition && Object.keys(activeSnapshotPosition).length > 0
    property bool infoPanelExpanded: false
    property bool snapshotDetailExpanded: false
    property bool leftPanelExpanded: true
    property bool notificationsMuted: false
    property var dismissedNotificationTexts: []
    readonly property int infoPanelWidth: infoPanelExpanded ? 264 : 0
    readonly property int snapshotDetailPanelWidth: snapshotDetailExpanded && hasActiveSnapshotPosition ? 256 : 0
    readonly property int leftPanelExpandedWidth: 248
    readonly property int leftPanelCollapsedWidth: 72
    
    // 快捷面板配置
    property var quickPanelConfig: {
        "常用因子": { expanded: true, items: 4 },
        "行业配置": { expanded: false, items: 4 },
        "风格暴露": { expanded: false, items: 4 }
    }
    
    // ============ 数据模型 ============
    
    // 可用因子池
    ListModel {
        id: factorPoolModel
    }
    
    // 当前组合
    ListModel {
        id: portfolioModel
    }
    
    // 常用因子
    ListModel {
        id: commonFactorsModel
    }

    Connections {
        target: factorService

        function onFactorsLoaded(factors) {
            root.syncFactorModels(factors || [])
        }

        function onFactorAdded() {
            root.refreshFactorSources()
        }

        function onFactorUpdated() {
            root.refreshFactorSources()
        }

        function onFactorDeleted() {
            root.refreshFactorSources()
        }
    }

    Connections {
        target: strategyService

        function onStrategiesLoaded(strategies) {
            if (!root.currentPortfolioId) {
                return
            }

            var items = strategies || []
            for (var index = 0; index < items.length; ++index) {
                var strategy = items[index] || ({})
                var strategyId = String(strategy.strategy_id || strategy.strategyId || strategy.id || "").trim()
                if (strategyId !== String(root.currentPortfolioId || "").trim()) {
                    continue
                }

                if (!root.shouldRestoreCurrentPortfolio()) {
                    return
                }

                root.currentStrategyDetail = strategy
                root.loadSavedPortfolio(strategy)
                return
            }
        }

        function onInitializedChanged() {
            var strategyServiceReady = false
            if (strategyService) {
                if (typeof strategyService.isInitialized === "function") {
                    strategyServiceReady = !!strategyService.isInitialized()
                } else if (strategyService.isInitialized !== undefined) {
                    strategyServiceReady = !!strategyService.isInitialized
                }
            }

            if (!strategyService || !strategyServiceReady) {
                return
            }

            if (root.shouldRestoreCurrentPortfolio()) {
                root.loadSavedPortfolio()
            }
        }

        function onStrategyUpdated(strategyId, strategyData) {
            if (String(strategyId || "") !== String(root.currentPortfolioId || "")) {
                return
            }

            root.currentStrategyDetail = strategyData || root.loadCurrentStrategyDetail()
            if (root.shouldRestoreCurrentPortfolio()) {
                root.loadSavedPortfolio(root.currentStrategyDetail)
            }
            root.updateSimulation([root.createNotification("success", "组合策略已更新，已同步最新状态", "查看")])
        }

        function onDataChanged() {
            if (!root.currentPortfolioId) {
                return
            }

            var detail = root.loadCurrentStrategyDetail()
            if (!detail || Object.keys(detail).length === 0) {
                return
            }

            root.currentStrategyDetail = detail
            if (root.shouldRestoreCurrentPortfolio()) {
                root.loadSavedPortfolio(detail)
            }
            var latest = root.getLatestBacktest(detail)
            if (latest && Object.keys(latest).length > 0) {
                root.updateSimulation([root.createNotification("success", "已同步最近一次回测结果", "查看")])
            } else {
                root.updateSimulation()
            }
        }
    }

    Connections {
        target: tradingConnectionConfigService

        function onCurrentConfigurationChanged() {
            root.syncBoundPortfolioContextIfNeeded(false)
        }
    }
    
    // 行业配置
    ListModel {
        id: sectorModel
        ListElement { sector: "银行"; weight: 0.4; color: "#3B82F6" }
        ListElement { sector: "消费"; weight: 0.3; color: "#10B981" }
        ListElement { sector: "医药"; weight: 0.2; color: "#8B5CF6" }
        ListElement { sector: "科技"; weight: 0.4; color: "#F59E0B" }
    }
    
    // 风格暴露
    ListModel {
        id: styleModel
        ListElement { style: "市值"; value: 1.2; target: 1.0; color: "#3B82F6" }
        ListElement { style: "动量"; value: 0.8; target: 0.8; color: "#10B981" }
        ListElement { style: "价值"; value: 0.5; target: 0.6; color: "#8B5CF6" }
        ListElement { style: "波动率"; value: 1.1; target: 1.0; color: "#F59E0B" }
    }
    
    // ============ 三栏式主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 主内容区域（三栏）
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            
            // === 左侧因子池面板 ===
            Rectangle {
                id: leftPanel
                Layout.preferredWidth: root.leftPanelExpanded ? root.leftPanelExpandedWidth : root.leftPanelCollapsedWidth
                Layout.fillHeight: true
                color: "#0F172A"
                border.width: 1
                border.color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    
                    // 面板标题
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        color: "#1E293B"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            Text {
                                text: root.leftPanelExpanded ? "收起" : "展开"
                                font.pixelSize: 12
                                color: "#60A5FA"

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.leftPanelExpanded = !root.leftPanelExpanded
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                visible: root.leftPanelExpanded
                            }

                            Text {
                                visible: root.leftPanelExpanded
                                text: "可用因子池"
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }

                            Item {
                                Layout.fillWidth: true
                                visible: root.leftPanelExpanded
                            }

                            Text {
                                text: root.leftPanelExpanded ? "搜索" : "搜"
                                font.pixelSize: 12
                                color: "#94A3B8"

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: searchFactors()
                                }
                            }
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        visible: root.leftPanelExpanded
                        
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                           // padding: 16
                            
                            // 常用因子
                            QuickPanelSection {
                                title: "常用因子"
                                expanded: quickPanelConfig["常用因子"].expanded
                                itemCount: quickPanelConfig["常用因子"].items
                                model: commonFactorsModel
                                delegate: QuickPanelItem {
                                    text: itemData ? String(itemData.displayName || "") : ""
                                    subText: itemData ? (String(itemData.frequency || 0) + " 次使用") : "0 次使用"
                                    icon: "因"
                                    draggable: true
                                    dragData: {
                                        "factorId": itemData ? String(itemData.factorId || "") : "",
                                        "displayName": itemData ? String(itemData.displayName || "") : "",
                                        "type": "factor"
                                    }
                                    onClicked: addFactorToPortfolio(itemData ? String(itemData.factorId || "") : "")
                                }
                                onToggleExpanded: quickPanelConfig["常用因子"].expanded = expanded
                            }
                            
                            // 行业配置
                            QuickPanelSection {
                                title: "行业配置"
                                expanded: quickPanelConfig["行业配置"].expanded
                                itemCount: quickPanelConfig["行业配置"].items
                                model: sectorModel
                                delegate: QuickPanelItem {
                                    text: itemData ? String(itemData.sector || "") : ""
                                    subText: itemData ? ((Number(itemData.weight || 0) * 100).toFixed(0) + "%") : "0%"
                                    icon: "行"
                                    onClicked: adjustSectorWeight(itemData ? String(itemData.sector || "") : "")
                                }
                                onToggleExpanded: quickPanelConfig["行业配置"].expanded = expanded
                            }
                            
                            // 风格暴露
                            QuickPanelSection {
                                title: "风格暴露"
                                expanded: quickPanelConfig["风格暴露"].expanded
                                itemCount: quickPanelConfig["风格暴露"].items
                                model: styleModel
                                delegate: QuickPanelItem {
                                    text: itemData ? String(itemData.style || "") : ""
                                    subText: itemData
                                        ? (Number(itemData.value || 0).toFixed(1) + " / " + Number(itemData.target || 0).toFixed(1))
                                        : "0.0 / 0.0"
                                    icon: "风"
                                    onClicked: adjustStyleExposure(itemData ? String(itemData.style || "") : "")
                                }
                                onToggleExpanded: quickPanelConfig["风格暴露"].expanded = expanded
                            }
                            
                            // 拖拽提示
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 60
                                radius: 8
                                color: "#1E293B"
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "拖拽添加"
                                        font.pixelSize: 12
                                        color: "#94A3B8"
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "拖拽因子到中间区域"
                                        font.pixelSize: 10
                                        color: "#64748B"
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        visible: !root.leftPanelExpanded
                        spacing: 10
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 14
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop

                        Rectangle {
                            width: 44
                            height: 44
                            radius: 10
                            color: "#1E293B"
                            border.width: 1
                            border.color: "#334155"

                            Text {
                                anchors.centerIn: parent
                                text: "常"
                                font.pixelSize: 14
                                color: "#F8FAFC"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.leftPanelExpanded = true
                                    quickPanelConfig["常用因子"].expanded = true
                                }
                            }
                        }

                        Rectangle {
                            width: 44
                            height: 44
                            radius: 10
                            color: "#1E293B"
                            border.width: 1
                            border.color: "#334155"

                            Text {
                                anchors.centerIn: parent
                                text: "行"
                                font.pixelSize: 14
                                color: "#F8FAFC"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.leftPanelExpanded = true
                                    quickPanelConfig["行业配置"].expanded = true
                                }
                            }
                        }

                        Rectangle {
                            width: 44
                            height: 44
                            radius: 10
                            color: "#1E293B"
                            border.width: 1
                            border.color: "#334155"

                            Text {
                                anchors.centerIn: parent
                                text: "风"
                                font.pixelSize: 14
                                color: "#F8FAFC"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.leftPanelExpanded = true
                                    quickPanelConfig["风格暴露"].expanded = true
                                }
                            }
                        }

                        Text {
                            width: 44
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: factorPoolModel.count + " 项"
                            font.pixelSize: 10
                            color: "#64748B"
                        }
                    }
                }
            }
            
            // === 中间组合构建器 ===
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    color: "#1E293B"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16

                        Text {
                            text: "组合构建: " + portfolioName
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }

                        Item { Layout.fillWidth: true }

                        Row {
                            spacing: 8

                            ConsoleUiComponents.ActionButton {
                                label: "运行回测"
                                tone: "primary"
                                onClicked: runBacktest()
                            }

                            ConsoleUiComponents.ActionButton {
                                label: "风险概览"
                                tone: "secondary"
                                onClicked: openRiskManagementPage()
                            }

                            ConsoleUiComponents.ActionButton {
                                label: "预览调仓"
                                tone: executionPlanStale ? "warning" : "secondary"
                                buttonWidth: 96
                                buttonEnabled: portfolioModel.count > 0
                                onClicked: previewExecutionPlan()
                            }

                            ConsoleUiComponents.ActionButton {
                                label: "刷新快照"
                                tone: "neutral"
                                buttonWidth: 96
                                onClicked: riskCheck()
                            }

                            ConsoleUiComponents.ActionButton {
                                label: "保存组合"
                                tone: "success"
                                buttonWidth: 96
                                onClicked: savePortfolio()
                            }

                            ConsoleUiComponents.ActionButton {
                                label: root.infoPanelExpanded ? "收起侧栏" : "系统侧栏"
                                tone: root.infoPanelExpanded ? "neutral" : "muted"
                                buttonWidth: 96
                                labelSize: 13
                                onClicked: root.infoPanelExpanded = !root.infoPanelExpanded
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0F172A"

                    ScrollView {
                        id: compositionScrollView
                        anchors.fill: parent
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        Item {
                            width: compositionScrollView.availableWidth > 0 ? compositionScrollView.availableWidth : compositionScrollView.width
                            implicitHeight: contentColumn.implicitHeight + 48

                            ColumnLayout {
                                id: contentColumn
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 24
                                spacing: 24

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Text {
                                        text: "当前组合 (总权重: " + totalWeight.toFixed(1) + "%)"
                                        font.pixelSize: 18
                                        font.weight: Font.DemiBold
                                        color: "#F1F5F9"
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: "重置权重"
                                        font.pixelSize: 12
                                        color: "#3B82F6"

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: resetWeights()
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 200
                                    radius: 12
                                    color: "#1E293B"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 0

                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 32
                                            spacing: 16

                                            Text {
                                                text: "因子"
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.portfolioFactorColumnWidth
                                            }

                                            Text {
                                                text: "权重"
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.portfolioWeightColumnWidth
                                            }

                                            Text {
                                                text: "相关性"
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.portfolioCorrelationColumnWidth
                                            }

                                            Text {
                                                text: "操作"
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.portfolioActionColumnWidth
                                            }
                                        }

                                        ListView {
                                            id: portfolioListView
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            model: portfolioModel
                                            clip: true
                                            spacing: 4

                                            delegate: PortfolioItem {
                                                width: portfolioListView.width
                                                height: 56

                                                factorId: model.factorId
                                                displayName: model.displayName
                                                weight: model.weight
                                                correlation: model.correlation
                                                factorColor: model.color

                                                onWeightEdited: function(factorId, newWeight) {
                                                    updateFactorWeight(model.factorId, newWeight)
                                                }
                                                onRemoveRequested: function(factorId) {
                                                    removeFactorFromPortfolio(factorId)
                                                }
                                            }

                                            ScrollBar.vertical: ScrollBar {
                                                policy: ScrollBar.AlwaysOn
                                                width: 8
                                            }
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 120
                                    spacing: 16

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        radius: 12
                                        color: "#1E293B"

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 16

                                            Text {
                                                text: "⚠️ 风险监控"
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: "#F1F5F9"
                                            }

                                            Text {
                                                text: riskPanelSubtitle
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                            }

                                            Column {
                                                spacing: 4

                                                Text {
                                                    text: "行业暴露:"
                                                    font.pixelSize: 12
                                                    color: "#94A3B8"
                                                }

                                                Text {
                                                    text: exposurePanelSubtitle
                                                    font.pixelSize: 10
                                                    color: "#64748B"
                                                }

                                                Row {
                                                    spacing: 4

                                                    Repeater {
                                                        model: sectorModel

                                                        delegate: Rectangle {
                                                            width: 20
                                                            height: 20
                                                            radius: 4
                                                            color: model.color
                                                            opacity: model.weight

                                                            Text {
                                                                anchors.centerIn: parent
                                                                text: "⬤"
                                                                font.pixelSize: 8
                                                                color: "white"
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            Column {
                                                spacing: 4

                                                Text {
                                                    text: "风格暴露:"
                                                    font.pixelSize: 12
                                                    color: "#94A3B8"
                                                }

                                                Text {
                                                    text: "市值 " + styleExposure["市值"].toFixed(1)
                                                        + "  |  动量 " + styleExposure["动量"].toFixed(1)
                                                        + "  |  价值 " + styleExposure["价值"].toFixed(1)
                                                        + "  |  波动率 " + styleExposure["波动率"].toFixed(1)
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                    width: parent.width
                                                    wrapMode: Text.WordWrap
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        radius: 12
                                        color: "#1E293B"

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 16

                                            Text {
                                                text: metricPanelTitle
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: "#F1F5F9"
                                            }

                                            Text {
                                                text: metricPanelSubtitle
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                            }

                                            GridLayout {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                columns: 2
                                                columnSpacing: 8
                                                rowSpacing: 8

                                                PerformanceMetric {
                                                    label: "年化收益"
                                                    value: portfolioMetrics.annualReturn
                                                    format: "%.1f"
                                                    unit: "%"
                                                    metricColor: portfolioMetrics.annualReturn > 15 ? "#10B981" : "#EF4444"
                                                }

                                                PerformanceMetric {
                                                    label: "夏普比率"
                                                    value: portfolioMetrics.sharpeRatio
                                                    format: "%.2f"
                                                    metricColor: portfolioMetrics.sharpeRatio > 1.5 ? "#10B981" : "#EF4444"
                                                }

                                                PerformanceMetric {
                                                    label: "最大回撤"
                                                    value: portfolioMetrics.maxDrawdown
                                                    format: "%.1f"
                                                    unit: "%"
                                                    metricColor: portfolioMetrics.maxDrawdown < 10 ? "#10B981" : "#EF4444"
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 272
                                    radius: 12
                                    color: "#1E293B"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 10

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 12

                                            ColumnLayout {
                                                spacing: 2

                                                Text {
                                                    text: "🧾 候选持仓快照"
                                                    font.pixelSize: 14
                                                    font.weight: Font.DemiBold
                                                    color: "#F1F5F9"
                                                }

                                                Text {
                                                    text: snapshotPanelSubtitle
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                }
                                            }

                                            Item { Layout.fillWidth: true }

                                            Rectangle {
                                                Layout.preferredWidth: 92
                                                Layout.preferredHeight: 28
                                                radius: 14
                                                color: "#0F172A"
                                                border.width: 1
                                                border.color: "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "目标仓位 " + Number(snapshotDiagnostics.targetWeightPercent || 0).toFixed(1) + "%"
                                                    font.pixelSize: 10
                                                    color: "#CBD5E1"
                                                }
                                            }

                                            Rectangle {
                                                Layout.preferredWidth: 88
                                                Layout.preferredHeight: 28
                                                radius: 14
                                                color: "#0F172A"
                                                border.width: 1
                                                border.color: "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "覆盖因子 " + Number(snapshotDiagnostics.allocationCount || 0)
                                                    font.pixelSize: 10
                                                    color: "#CBD5E1"
                                                }
                                            }

                                            Rectangle {
                                                Layout.preferredWidth: 84
                                                Layout.preferredHeight: 28
                                                radius: 14
                                                color: root.snapshotDetailExpanded ? "#1D4ED8" : "#0F172A"
                                                border.width: 1
                                                border.color: root.snapshotDetailExpanded ? "#60A5FA" : "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: root.snapshotDetailExpanded ? "收起详情" : "查看详情"
                                                    font.pixelSize: 10
                                                    color: "#CBD5E1"
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    enabled: root.hasActiveSnapshotPosition
                                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                    onClicked: root.snapshotDetailExpanded = !root.snapshotDetailExpanded
                                                }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 1
                                            color: "#334155"
                                        }

                                        Row {
                                            spacing: 8

                                            Repeater {
                                                model: [
                                                    { key: "all", label: "全部", count: root.countSnapshotPositions("all") },
                                                    { key: "warning", label: "预警", count: root.countSnapshotPositions("warning") },
                                                    { key: "danger", label: "风险", count: root.countSnapshotPositions("danger") },
                                                    { key: "normal", label: "正常", count: root.countSnapshotPositions("normal") }
                                                ]

                                                delegate: Rectangle {
                                                    readonly property bool selected: root.snapshotFilter === modelData.key
                                                    width: 64
                                                    height: 26
                                                    radius: 13
                                                    color: selected ? "#1D4ED8" : "#0F172A"
                                                    border.width: 1
                                                    border.color: selected ? "#60A5FA" : "#334155"

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData.label + " " + modelData.count
                                                        font.pixelSize: 10
                                                        color: selected ? "white" : "#CBD5E1"
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            root.snapshotFilter = modelData.key
                                                            root.selectedSnapshotIndex = -1
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        Row {
                                            spacing: 8

                                            Repeater {
                                                model: [
                                                    { key: "score_desc", label: "评分优先" },
                                                    { key: "weight_desc", label: "权重优先" },
                                                    { key: "status_desc", label: "风险优先" },
                                                    { key: "symbol_asc", label: "代码排序" }
                                                ]

                                                delegate: Rectangle {
                                                    readonly property bool selected: root.snapshotSortKey === modelData.key
                                                    width: 72
                                                    height: 24
                                                    radius: 12
                                                    color: selected ? "#0F766E" : "#0F172A"
                                                    border.width: 1
                                                    border.color: selected ? "#5EEAD4" : "#334155"

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData.label
                                                        font.pixelSize: 10
                                                        color: selected ? "white" : "#CBD5E1"
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            root.snapshotSortKey = modelData.key
                                                            root.selectedSnapshotIndex = -1
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 12

                                            Text {
                                                text: "标的"
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.snapshotSymbolColumnWidth
                                            }

                                            Text {
                                                text: "目标权重"
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.snapshotWeightColumnWidth
                                            }

                                            Text {
                                                text: "评分"
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.snapshotScoreColumnWidth
                                            }

                                            Text {
                                                text: "状态"
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.snapshotStatusColumnWidth
                                            }

                                            Text {
                                                text: "建议"
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: "最新价"
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"
                                                Layout.preferredWidth: root.snapshotPriceColumnWidth
                                            }
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true

                                            Text {
                                                anchors.centerIn: parent
                                                visible: filteredSnapshotPositions.length === 0
                                                text: portfolioSnapshot.status === "error"
                                                    ? (portfolioSnapshot.error || "快照生成失败")
                                                    : "当前筛选条件下没有候选持仓"
                                                font.pixelSize: 12
                                                color: "#64748B"
                                            }

                                            ListView {
                                                id: snapshotListView
                                                anchors.fill: parent
                                                anchors.rightMargin: root.snapshotDetailPanelWidth > 0 ? (root.snapshotDetailPanelWidth + 12) : 0
                                                visible: filteredSnapshotPositions.length > 0
                                                clip: true
                                                spacing: 6
                                                model: filteredSnapshotPositions

                                                ScrollBar.vertical: ScrollBar {
                                                    policy: ScrollBar.AsNeeded
                                                    width: 8
                                                }

                                                delegate: Rectangle {
                                                    readonly property var positionData: modelData || ({})
                                                    readonly property bool isActive: activeSnapshotPosition
                                                        && String(activeSnapshotPosition.symbol || activeSnapshotPosition.name || "")
                                                            === String(positionData.symbol || positionData.name || "")
                                                    width: snapshotListView.width
                                                    height: 38
                                                    radius: 8
                                                    color: isActive ? "#1E3A5F" : (index % 2 === 0 ? "#0F172A" : "#162033")
                                                    border.width: isActive ? 1 : 0
                                                    border.color: isActive ? "#60A5FA" : "transparent"

                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 12
                                                        anchors.rightMargin: 12
                                                        spacing: 12

                                                        Text {
                                                            text: String(positionData.symbol || positionData.name || "")
                                                            font.pixelSize: 12
                                                            color: "#F8FAFC"
                                                            Layout.preferredWidth: root.snapshotSymbolColumnWidth
                                                            elide: Text.ElideRight
                                                        }

                                                        Text {
                                                            text: String(positionData.ratio || "--")
                                                            font.pixelSize: 12
                                                            color: "#CBD5E1"
                                                            Layout.preferredWidth: root.snapshotWeightColumnWidth
                                                        }

                                                        Text {
                                                            text: Number(positionData.score || 0).toFixed(2)
                                                            font.pixelSize: 12
                                                            color: "#CBD5E1"
                                                            Layout.preferredWidth: root.snapshotScoreColumnWidth
                                                        }

                                                        Rectangle {
                                                            Layout.preferredWidth: root.snapshotStatusColumnWidth
                                                            Layout.preferredHeight: 22
                                                            radius: 11
                                                            color: positionData.badgeType === "danger"
                                                                ? "#7F1D1D"
                                                                : (positionData.badgeType === "warning" ? "#78350F" : "#14532D")

                                                            Text {
                                                                anchors.centerIn: parent
                                                                text: String(positionData.badgeText || "正常")
                                                                font.pixelSize: 10
                                                                color: "white"
                                                            }
                                                        }

                                                        Text {
                                                            text: String(positionData.recommendation || "继续观察")
                                                            font.pixelSize: 11
                                                            color: "#CBD5E1"
                                                            Layout.fillWidth: true
                                                            elide: Text.ElideRight
                                                        }

                                                        Text {
                                                            text: Number(positionData.lastPrice || 0) > 0
                                                                ? Number(positionData.lastPrice).toFixed(2)
                                                                : "--"
                                                            font.pixelSize: 12
                                                            color: "#CBD5E1"
                                                            Layout.preferredWidth: root.snapshotPriceColumnWidth
                                                            horizontalAlignment: Text.AlignRight
                                                        }
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            root.selectedSnapshotIndex = index
                                                            root.snapshotDetailExpanded = true
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                anchors.top: parent.top
                                                anchors.bottom: parent.bottom
                                                anchors.right: parent.right
                                                width: root.snapshotDetailPanelWidth
                                                radius: 10
                                                color: "#0F172A"
                                                border.width: 1
                                                border.color: "#334155"
                                                clip: true
                                                visible: width > 0

                                                Behavior on width {
                                                    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                                                }

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 12
                                                    spacing: 8

                                                    RowLayout {
                                                        Layout.fillWidth: true

                                                        Text {
                                                            text: root.hasActiveSnapshotPosition
                                                                ? String(activeSnapshotInstrument.name || activeSnapshotPosition.symbol || "候选持仓")
                                                                : "快照详情"
                                                            font.pixelSize: 13
                                                            font.weight: Font.DemiBold
                                                            color: "#F1F5F9"
                                                            Layout.fillWidth: true
                                                            elide: Text.ElideRight
                                                        }

                                                        ConsoleUiComponents.ActionChip {
                                                            label: "收起"
                                                            tone: "link"
                                                            onClicked: root.snapshotDetailExpanded = false
                                                        }
                                                    }

                                                    Text {
                                                        text: root.hasActiveSnapshotPosition && activeSnapshotPosition.symbol
                                                            ? String(activeSnapshotPosition.symbol)
                                                            : "选择左侧候选查看详情"
                                                        font.pixelSize: 11
                                                        color: "#94A3B8"
                                                    }

                                                    Rectangle {
                                                        Layout.fillWidth: true
                                                        Layout.preferredHeight: 1
                                                        color: "#334155"
                                                    }

                                                    Text {
                                                        text: "行业: " + String(activeSnapshotInstrument.industry || "未识别")
                                                        font.pixelSize: 11
                                                        color: "#CBD5E1"
                                                    }

                                                    Text {
                                                        text: "目标权重: " + String(activeSnapshotPosition.ratio || "--")
                                                        font.pixelSize: 11
                                                        color: "#CBD5E1"
                                                    }

                                                    Text {
                                                        text: "覆盖因子: " + Number(activeSnapshotPosition.factorCoverage || 0)
                                                        font.pixelSize: 11
                                                        color: "#CBD5E1"
                                                    }

                                                    Text {
                                                        text: "评分: " + Number(activeSnapshotPosition.score || 0).toFixed(2)
                                                        font.pixelSize: 11
                                                        color: "#CBD5E1"
                                                    }

                                                    Text {
                                                        text: "状态: " + String(activeSnapshotPosition.statusText || activeSnapshotPosition.badgeText || "--")
                                                        font.pixelSize: 11
                                                        color: "#CBD5E1"
                                                    }

                                                    Text {
                                                        text: "建议: " + String(activeSnapshotPosition.recommendation || "继续观察")
                                                        font.pixelSize: 11
                                                        color: "#CBD5E1"
                                                        wrapMode: Text.WordWrap
                                                        Layout.fillWidth: true
                                                    }

                                                    Text {
                                                        text: "总市值: " + formatCompactNumber(activeSnapshotInstrument.marketCap || activeSnapshotInstrument.market_cap)
                                                        font.pixelSize: 11
                                                        color: "#CBD5E1"
                                                    }

                                                    Text {
                                                        text: "流通市值: " + formatCompactNumber(activeSnapshotInstrument.circulatingMarketCap || activeSnapshotInstrument.circulating_market_cap)
                                                        font.pixelSize: 11
                                                        color: "#CBD5E1"
                                                    }

                                                    Text {
                                                        text: "快照日: " + String(activeSnapshotPosition.snapshotDate || portfolioSnapshot.snapshotDate || "--")
                                                        font.pixelSize: 11
                                                        color: "#94A3B8"
                                                    }

                                                    Row {
                                                        spacing: 8

                                                        ConsoleUiComponents.ActionButton {
                                                            label: "风险概览"
                                                            tone: "secondary"
                                                            buttonWidth: 92
                                                            buttonHeight: 28
                                                            labelSize: 11
                                                            onClicked: root.openRiskManagementPage()
                                                        }

                                                        ConsoleUiComponents.ActionButton {
                                                            label: "运行回测"
                                                            tone: portfolioModel.count > 0 ? "success" : "neutral"
                                                            buttonWidth: 92
                                                            buttonHeight: 28
                                                            labelSize: 11
                                                            buttonEnabled: portfolioModel.count > 0
                                                            onClicked: root.runBacktest()
                                                        }
                                                    }

                                                    Item {
                                                        Layout.fillHeight: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: executionOrders.length > 0
                                        ? (skippedExecutionOrders.length > 0 ? 276 : 248)
                                        : 172
                                    radius: 12
                                    color: "#1E293B"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 10

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 12

                                            ColumnLayout {
                                                spacing: 2

                                                Text {
                                                    text: "📋 调仓计划预览"
                                                    font.pixelSize: 14
                                                    font.weight: Font.DemiBold
                                                    color: "#F1F5F9"
                                                }

                                                Text {
                                                    text: executionPlanSubtitle
                                                    font.pixelSize: 11
                                                    color: executionPlanStale ? "#F59E0B" : "#94A3B8"
                                                }
                                            }

                                            Item { Layout.fillWidth: true }

                                            Rectangle {
                                                Layout.preferredWidth: 84
                                                Layout.preferredHeight: 28
                                                radius: 14
                                                color: executionPlanStale ? "#7C2D12" : "#0F172A"
                                                border.width: 1
                                                border.color: executionPlanStale ? "#FB923C" : "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: executionPlanStale ? "已过期" : (hasExecutionPlanPreview ? "已生成" : "未生成")
                                                    font.pixelSize: 10
                                                    color: "#E2E8F0"
                                                }
                                            }

                                            Rectangle {
                                                Layout.preferredWidth: 86
                                                Layout.preferredHeight: 28
                                                radius: 14
                                                color: "#0F172A"
                                                border.width: 1
                                                border.color: "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "委托 " + Number(executionPlanSummary.orderCount || executionOrders.length || 0)
                                                    font.pixelSize: 10
                                                    color: "#CBD5E1"
                                                }
                                            }

                                            Rectangle {
                                                Layout.preferredWidth: 86
                                                Layout.preferredHeight: 28
                                                radius: 14
                                                color: "#0F172A"
                                                border.width: 1
                                                border.color: "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "批次 " + Number(executionPlanSummary.batchCount || executionBatches.length || 0)
                                                    font.pixelSize: 10
                                                    color: "#CBD5E1"
                                                }
                                            }

                                            Rectangle {
                                                Layout.preferredWidth: 96
                                                Layout.preferredHeight: 28
                                                radius: 14
                                                color: "#0F172A"
                                                border.width: 1
                                                border.color: "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "买入 " + formatCompactNumber(executionPlanSummary.estimatedBuyNotional || 0)
                                                    font.pixelSize: 10
                                                    color: "#CBD5E1"
                                                }
                                            }

                                            Rectangle {
                                                Layout.preferredWidth: 96
                                                Layout.preferredHeight: 28
                                                radius: 14
                                                color: "#0F172A"
                                                border.width: 1
                                                border.color: "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "卖出 " + formatCompactNumber(executionPlanSummary.estimatedSellNotional || 0)
                                                    font.pixelSize: 10
                                                    color: "#CBD5E1"
                                                }
                                            }

                                            ConsoleUiComponents.ActionButton {
                                                label: hasExecutionPlanPreview ? "重新生成" : "生成计划"
                                                tone: executionPlanStale ? "warning" : "secondary"
                                                buttonWidth: 92
                                                buttonHeight: 28
                                                labelSize: 11
                                                buttonEnabled: portfolioModel.count > 0
                                                onClicked: previewExecutionPlan()
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 1
                                            color: "#334155"
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true

                                            Text {
                                                anchors.centerIn: parent
                                                visible: executionOrders.length === 0
                                                width: parent.width - 24
                                                horizontalAlignment: Text.AlignHCenter
                                                wrapMode: Text.WordWrap
                                                text: String(executionPlanState.message || "尚未生成调仓计划预览")
                                                font.pixelSize: 12
                                                color: executionPlanState.success ? "#64748B" : "#CBD5E1"
                                            }

                                            ColumnLayout {
                                                anchors.fill: parent
                                                spacing: 8
                                                visible: executionOrders.length > 0

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 12

                                                    Text {
                                                        text: "动作"
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        color: "#94A3B8"
                                                        Layout.preferredWidth: root.executionActionColumnWidth
                                                    }

                                                    Text {
                                                        text: "标的"
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        color: "#94A3B8"
                                                        Layout.preferredWidth: root.executionSymbolColumnWidth
                                                    }

                                                    Text {
                                                        text: "股数"
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        color: "#94A3B8"
                                                        Layout.preferredWidth: root.executionQuantityColumnWidth
                                                    }

                                                    Text {
                                                        text: "当前→目标"
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        color: "#94A3B8"
                                                        Layout.preferredWidth: root.executionWeightColumnWidth
                                                    }

                                                    Text {
                                                        text: "参考价"
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        color: "#94A3B8"
                                                        Layout.preferredWidth: root.executionPriceColumnWidth
                                                    }

                                                    Text {
                                                        text: "说明"
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        color: "#94A3B8"
                                                        Layout.fillWidth: true
                                                    }
                                                }

                                                ListView {
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                                    clip: true
                                                    spacing: 6
                                                    model: executionOrders

                                                    ScrollBar.vertical: ScrollBar {
                                                        policy: ScrollBar.AsNeeded
                                                        width: 8
                                                    }

                                                    delegate: Rectangle {
                                                        readonly property var orderData: modelData || ({})
                                                        width: ListView.view.width
                                                        height: 38
                                                        radius: 8
                                                        color: index % 2 === 0 ? "#0F172A" : "#162033"

                                                        RowLayout {
                                                            anchors.fill: parent
                                                            anchors.leftMargin: 12
                                                            anchors.rightMargin: 12
                                                            spacing: 12

                                                            Rectangle {
                                                                Layout.preferredWidth: root.executionActionColumnWidth
                                                                Layout.preferredHeight: 22
                                                                radius: 11
                                                                color: String(orderData.side || "") === "SELL" ? "#7F1D1D" : "#14532D"

                                                                Text {
                                                                    anchors.centerIn: parent
                                                                    text: String(orderData.side || "--")
                                                                    font.pixelSize: 10
                                                                    color: "white"
                                                                }
                                                            }

                                                            Text {
                                                                text: String(orderData.symbol || "--")
                                                                font.pixelSize: 12
                                                                color: "#F8FAFC"
                                                                Layout.preferredWidth: root.executionSymbolColumnWidth
                                                                elide: Text.ElideRight
                                                            }

                                                            Text {
                                                                text: formatIntegerValue(orderData.quantity)
                                                                font.pixelSize: 12
                                                                color: "#CBD5E1"
                                                                Layout.preferredWidth: root.executionQuantityColumnWidth
                                                            }

                                                            Text {
                                                                text: formatPercentValue(orderData.currentWeightPercent) + " → " + formatPercentValue(orderData.targetWeightPercent)
                                                                font.pixelSize: 12
                                                                color: "#CBD5E1"
                                                                Layout.preferredWidth: root.executionWeightColumnWidth
                                                            }

                                                            Text {
                                                                text: Number(orderData.price || 0) > 0 ? Number(orderData.price).toFixed(2) : "--"
                                                                font.pixelSize: 12
                                                                color: "#CBD5E1"
                                                                Layout.preferredWidth: root.executionPriceColumnWidth
                                                                horizontalAlignment: Text.AlignRight
                                                            }

                                                            Text {
                                                                text: String(orderData.reason || "")
                                                                font.pixelSize: 11
                                                                color: "#CBD5E1"
                                                                Layout.fillWidth: true
                                                                elide: Text.ElideRight
                                                            }
                                                        }
                                                    }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    visible: skippedExecutionOrders.length > 0
                                                    text: buildSkippedExecutionSummary(skippedExecutionOrders)
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                    wrapMode: Text.WordWrap
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 80
                                    radius: 12
                                    color: Qt.rgba(0.231, 0.510, 0.965, 0.1)
                                    border.color: "#3B82F6"
                                    border.width: 2

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 8

                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "⬇️ 拖放区域"
                                            font.pixelSize: 14
                                            color: "#3B82F6"
                                        }

                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "将左侧因子拖拽到此处添加到组合"
                                            font.pixelSize: 12
                                            color: "#94A3B8"
                                        }
                                    }

                                    DropArea {
                                        anchors.fill: parent

                                        function resolveDroppedText(dropEvent) {
                                            if (!dropEvent) {
                                                return ""
                                            }
                                            if (typeof dropEvent.getDataAsString === "function") {
                                                var plainText = String(dropEvent.getDataAsString("text/plain") || "")
                                                if (plainText.length > 0) {
                                                    return plainText
                                                }
                                                var textValue = String(dropEvent.getDataAsString("text") || "")
                                                if (textValue.length > 0) {
                                                    return textValue
                                                }
                                            }
                                            if (dropEvent.hasText && dropEvent.text) {
                                                return String(dropEvent.text)
                                            }
                                            return ""
                                        }

                                        onEntered: {
                                            parent.color = Qt.rgba(0.231, 0.510, 0.965, 0.2)
                                        }

                                        onExited: {
                                            parent.color = Qt.rgba(0.231, 0.510, 0.965, 0.1)
                                        }

                                        onDropped: {
                                            var payload = resolveDroppedText(drop)
                                            console.log("拖拽数据:", payload)
                                            if (payload.length > 0 && payload.indexOf("factorId") >= 0) {
                                                try {
                                                    var data = JSON.parse(payload)
                                                    if (data.type === "factor") {
                                                        addFactorToPortfolio(data.factorId)
                                                    }
                                                } catch(e) {
                                                    console.log("拖拽数据解析错误:", e)
                                                }
                                            }
                                            parent.color = Qt.rgba(0.231, 0.510, 0.965, 0.1)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
        }
        
        // === 底部通知栏 ===
        Rectangle {
            id: notificationBar
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#1E293B"
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 16
                
                Text {
                    text: "组合通知"
                    font.pixelSize: 14
                    color: "#94A3B8"
                }
                
                // 通知消息
                Row {
                    spacing: 16
                    
                    Repeater {
                        model: root.visibleNotifications.length
                        
                        delegate: Row {
                            readonly property var notificationData: root.visibleNotifications[index] || ({})
                            spacing: 6
                            
                            Text {
                                  text: notificationData.type === "warning" ? "预警" :
                                      notificationData.type === "success" ? "完成" : "提示"
                                  font.pixelSize: 12
                                  font.weight: Font.Medium
                                color: notificationData.type === "warning" ? "#F59E0B" :
                                       notificationData.type === "success" ? "#10B981" : "#3B82F6"
                            }
                            
                            Text {
                                text: notificationData.text || ""
                                font.pixelSize: 14
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: notificationData.time || ""
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            // 操作按钮
                            ConsoleUiComponents.ActionChip {
                                label: notificationData.action || "查看"
                                tone: "link"
                                onClicked: handleNotificationAction(index)
                            }
                        }
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // 通知控制
                Row {
                    spacing: 8

                    Text {
                        visible: root.notificationsMuted
                        text: "通知已静音"
                        font.pixelSize: 12
                        color: "#94A3B8"
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    ConsoleUiComponents.ActionChip {
                        label: root.notificationsMuted ? "取消静音" : "静音"
                        tone: "muted"
                        onClicked: muteNotifications()
                    }
                    
                    ConsoleUiComponents.ActionChip {
                        label: "清空"
                        tone: "muted"
                        chipEnabled: root.visibleNotifications.length > 0
                        onClicked: clearNotifications()
                    }
                }
            }
        }
    }

    Rectangle {
        id: infoSidePanel
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: notificationBar.height
        anchors.right: parent.right
        width: root.infoPanelWidth
        color: "#0F172A"
        border.width: width > 0 ? 1 : 0
        border.color: "#1E293B"
        clip: true
        z: 10
        visible: width > 0

        Behavior on width {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                color: "#1E293B"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12

                    Text {
                        text: "系统状态"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }

                    Item { Layout.fillWidth: true }

                    ConsoleUiComponents.ActionChip {
                        label: "收起"
                        tone: "link"
                        onClicked: root.infoPanelExpanded = false
                    }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: infoSidePanel.width
                    spacing: 16

                    Repeater {
                        model: Object.keys(systemStatus)

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            radius: 8
                            color: "#1E293B"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 4

                                RowLayout {
                                    spacing: 8

                                    Text {
                                        text: systemStatus[modelData].status
                                        font.pixelSize: 16
                                        color: systemStatus[modelData].color
                                    }

                                    Text {
                                        text: modelData
                                        font.pixelSize: 14
                                        color: "#94A3B8"
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: systemStatus[modelData].value
                                        font.pixelSize: 18
                                        font.weight: Font.DemiBold
                                        color: "#F1F5F9"
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 4
                                    radius: 2
                                    color: "#334155"
                                    visible: modelData === "模拟耗时"

                                    Rectangle {
                                        width: parent.width * 0.3
                                        height: parent.height
                                        radius: 2
                                        color: systemStatus[modelData].color
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 100
                        radius: 8
                        color: "#1E293B"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12

                            Text {
                                text: "智能建议"
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }

                            Text {
                                text: portfolioSuggestion
                                font.pixelSize: 12
                                color: "#10B981"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 8

                        ConsoleUiComponents.ActionButton {
                            label: "一键优化"
                            tone: "primary"
                            buttonWidth: parent.width
                            onClicked: autoOptimize()
                        }

                        ConsoleUiComponents.ActionButton {
                            label: "刷新风险"
                            tone: "neutral"
                            buttonWidth: parent.width
                            onClicked: riskCheck()
                        }
                    }
                }
            }
        }
    }
    
    // ============ 自定义组件 ============

    // 快捷面板部分（复用FactorLibraryPageEnhanced中的组件）
    component QuickPanelSection: Column {
        id: quickPanelSection
        property string title: ""
        property bool expanded: true
        property int itemCount: 5
        property var model: null
        property Component delegate: null
        
        signal toggleExpanded(bool expanded)

        function resolvedItemCount() {
            if (!quickPanelSection.model) {
                return 0
            }
            if (quickPanelSection.model.count !== undefined) {
                return Math.min(quickPanelSection.itemCount, Number(quickPanelSection.model.count || 0))
            }
            if (quickPanelSection.model.length !== undefined) {
                return Math.min(quickPanelSection.itemCount, Number(quickPanelSection.model.length || 0))
            }
            return 0
        }

        function getItemData(itemIndex) {
            if (!quickPanelSection.model || itemIndex < 0) {
                return ({})
            }
            if (typeof quickPanelSection.model.get === "function") {
                return quickPanelSection.model.get(itemIndex) || ({})
            }
            if (quickPanelSection.model.length !== undefined && itemIndex < quickPanelSection.model.length) {
                return quickPanelSection.model[itemIndex] || ({})
            }
            return ({})
        }
        
        spacing: 8
        width: parent.width
        
        // 标题栏
        Rectangle {
            width: parent.width
            height: 32
            radius: 6
            color: "#1E293B"
            
            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: expanded ? "▼" : "▶"
                    font.pixelSize: 12
                    color: "#94A3B8"
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            expanded = !expanded
                            toggleExpanded(expanded)
                        }
                    }
                }
                
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: title
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Item { width: parent.width - 100 }
                
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: model ? model.count : 0
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
            }
        }
        
        // 内容区域
        Column {
            width: parent.width
            spacing: 4
            visible: expanded
            
            Repeater {
                model: quickPanelSection.resolvedItemCount()
                
                Loader {
                    id: sectionItemLoader
                    property var itemData: quickPanelSection.getItemData(index)
                    width: parent.width
                    visible: true
                    active: true
                    height: 32
                    sourceComponent: quickPanelSection.delegate

                    onLoaded: {
                        if (item && item.hasOwnProperty("itemData")) {
                            item.itemData = sectionItemLoader.itemData || ({})
                        }
                    }
                }
            }
        }
    }
    
    // 快捷面板项（支持拖拽）
    component QuickPanelItem: Rectangle {
        id: quickPanelItem
        property var itemData: ({})
        property string text: ""
        property string subText: ""
        property string icon: ""
        property bool draggable: false
        property var dragData: null
        signal clicked()
        
        width: parent.width
        height: 32
        radius: 6
        color: "#1E293B"
        
        Row {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8
            
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: icon
                font.pixelSize: 14
                color: "#94A3B8"
            }
            
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                
                Text {
                    text: quickPanelItem.text
                    font.pixelSize: 14
                    color: "#F1F5F9"
                    elide: Text.ElideRight
                    width: parent.width
                }
                
                Text {
                    text: quickPanelItem.subText
                    font.pixelSize: 11
                    color: "#94A3B8"
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
        }
        
        MouseArea {
            id: quickPanelMouseArea
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: quickPanelItem.clicked()
            
            drag.target: quickPanelItem.draggable ? dragItem : null
            drag.smoothed: true
            drag.threshold: 10
            
            onPressed: {
                if (quickPanelItem.draggable && quickPanelItem.dragData) {
                    dragItem.text = quickPanelItem.text
                    dragItem.dragData = JSON.stringify(quickPanelItem.dragData)
                }
            }
            
            onReleased: {
                if (quickPanelItem.draggable) {
                    dragItem.x = 0
                    dragItem.y = 0
                }
            }

            onCanceled: {
                if (quickPanelItem.draggable) {
                    dragItem.x = 0
                    dragItem.y = 0
                }
            }
        }
        
        // 拖拽视觉反馈
        Rectangle {
            id: dragItem
            width: 120
            height: 40
            radius: 8
            color: "#3B82F6"
            visible: quickPanelMouseArea.drag.active
            z: 1000
            
            property string text: ""
            property string dragData: ""
            
            Text {
                anchors.centerIn: parent
                text: dragItem.text
                font.pixelSize: 12
                color: "white"
            }
            
            Drag.active: quickPanelMouseArea.drag.active
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2
            
            Drag.dragType: Drag.Automatic
            Drag.mimeData: {
                "text/plain": dragItem.dragData,
                "text": dragItem.dragData
            }
            
            Drag.onDragStarted: {
                console.log("开始拖拽:", dragItem.text)
            }
            
            Drag.onDragFinished: {
                console.log("拖拽完成")
                x = 0
                y = 0
            }
        }
    }
    
    // 组合项组件
    component PortfolioItem: Rectangle {
        property string factorId: ""
        property string displayName: ""
        property real weight: 0.0
        property real correlation: 0.0
        property color factorColor: "#3B82F6"

        signal weightEdited(string factorId, real newWeight)
        signal removeRequested(string factorId)

        radius: 8
        color: "#1E293B"
        implicitHeight: 56
        
        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12
            
            // 因子名称
            RowLayout {
                spacing: 8
                Layout.preferredWidth: root.portfolioFactorColumnWidth
                Layout.minimumWidth: root.portfolioFactorColumnWidth
                Layout.maximumWidth: root.portfolioFactorColumnWidth
                
                Rectangle {
                    width: 24
                    height: 24
                    radius: 4
                    color: factorColor
                    
                    Text {
                        anchors.centerIn: parent
                        text: "F"
                        font.pixelSize: 10
                        color: "white"
                    }
                }
                
                Text {
                    text: displayName
                    font.pixelSize: 14
                    color: "#F1F5F9"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
            
            // 权重调节
            RowLayout {
                spacing: 8
                Layout.preferredWidth: root.portfolioWeightColumnWidth
                Layout.minimumWidth: root.portfolioWeightColumnWidth
                Layout.maximumWidth: root.portfolioWeightColumnWidth
                
                Text {
                    text: weight.toFixed(1) + "%"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                    font.family: "Consolas"
                    Layout.preferredWidth: 56
                    verticalAlignment: Text.AlignVCenter
                }
                
                // 权重滑块
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: weight
                    stepSize: 0.5

                    onMoved: {
                        if (Math.abs(weight - value) > 0.1) {
                            weightEdited(factorId, value)
                        }
                    }
                }
            }
            
            // 相关性指示器
            Rectangle {
                Layout.preferredWidth: root.portfolioCorrelationColumnWidth
                Layout.minimumWidth: root.portfolioCorrelationColumnWidth
                Layout.maximumWidth: root.portfolioCorrelationColumnWidth
                Layout.preferredHeight: 24
                radius: 10
                color: "#334155"
                
                Rectangle {
                    width: Math.max(34, parent.width * Math.abs(correlation))
                    height: parent.height
                    radius: parent.radius
                    color: correlation > 0.5 ? "#EF4444" : 
                           correlation > 0.3 ? "#F59E0B" : "#10B981"
                    
                    Text {
                        anchors.centerIn: parent
                        text: correlation.toFixed(2)
                        font.pixelSize: 10
                        font.family: "Consolas"
                        color: "white"
                    }
                }
            }
            
            // 操作按钮
            Rectangle {
                Layout.preferredWidth: root.portfolioActionColumnWidth
                Layout.minimumWidth: root.portfolioActionColumnWidth
                Layout.maximumWidth: root.portfolioActionColumnWidth
                Layout.preferredHeight: 28
                radius: 14
                color: "#3B1215"
                border.width: 1
                border.color: "#7F1D1D"

                Text {
                    anchors.centerIn: parent
                    text: "移除"
                    font.pixelSize: 11
                    color: "#FCA5A5"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: removeRequested(factorId)
                }
            }
        }
    }
    
    // 性能指标组件
    component PerformanceMetric: Rectangle {
        property string label: ""
        property real value: 0
        property string format: "%.2f"
        property string unit: ""
        property color metricColor: "#3B82F6"
        
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        radius: 8
        color: "#334155"
        
        Column {
            anchors.centerIn: parent
            spacing: 2
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: label
                font.pixelSize: 10
                color: "#94A3B8"
            }
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    var formatted = format.replace("%d", Math.round(value)).replace("%.1f", value.toFixed(1)).replace("%.2f", value.toFixed(2))
                    return formatted + (unit ? " " + unit : "")
                }
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: parent.parent.metricColor
            }
        }
    }
    
    // ============ 核心业务函数 ============

    function currentTimeLabel() {
        return Qt.formatTime(new Date(), "HH:mm")
    }

    function createNotification(type, text, action) {
        return {
            type: type,
            text: text,
            time: currentTimeLabel(),
            action: action || "查看"
        }
    }

    function clampValue(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function formatCompactNumber(value) {
        var numericValue = Number(value)
        if (isNaN(numericValue) || numericValue <= 0) {
            return "--"
        }
        if (numericValue >= 100000000) {
            return (numericValue / 100000000).toFixed(2) + " 亿"
        }
        if (numericValue >= 10000) {
            return (numericValue / 10000).toFixed(1) + " 万"
        }
        return numericValue.toFixed(0)
    }

    function formatIntegerValue(value) {
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return "--"
        }
        return String(Math.round(numericValue))
    }

    function formatPercentValue(value) {
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return "--"
        }
        return numericValue.toFixed(1) + "%"
    }

    function buildExecutionPlanFingerprint() {
        var entries = []
        for (var index = 0; index < portfolioModel.count; index++) {
            var entry = portfolioModel.get(index) || ({})
            entries.push(String(entry.factorId || entry.displayName || "") + ":" + Number(entry.weight || 0).toFixed(4))
        }

        entries.sort()
        return JSON.stringify({
            strategyId: String(currentPortfolioId || ""),
            portfolioName: String(portfolioName || ""),
            totalWeight: Number(totalWeight || 0).toFixed(4),
            snapshotDate: String(portfolioSnapshot.snapshotDate || portfolioSnapshot.recordedAt || ""),
            entries: entries
        })
    }

    function buildSkippedExecutionSummary(items) {
        var source = items || []
        if (source.length === 0) {
            return ""
        }

        var previewItems = []
        for (var i = 0; i < Math.min(3, source.length); i++) {
            var item = source[i] || ({})
            previewItems.push(String(item.symbol || "--") + " · " + String(item.reason || "已跳过"))
        }

        var summary = "跳过 " + source.length + " 项: " + previewItems.join("；")
        if (source.length > 3) {
            summary += "；其余请重新生成后查看"
        }
        return summary
    }

    function matchesSnapshotFilter(position, filterKey) {
        var badgeType = String((position || {}).badgeType || "normal")
        if (filterKey === "danger") {
            return badgeType === "danger"
        }
        if (filterKey === "warning") {
            return badgeType === "warning"
        }
        if (filterKey === "normal") {
            return badgeType !== "danger" && badgeType !== "warning"
        }
        return true
    }

    function filterSnapshotPositions(positions, filterKey) {
        var source = positions || []
        var filtered = []
        for (var i = 0; i < source.length; i++) {
            var position = source[i] || ({})
            if (matchesSnapshotFilter(position, filterKey)) {
                filtered.push(position)
            }
        }
        return filtered
    }

    function snapshotStatusRank(position) {
        var badgeType = String((position || {}).badgeType || "normal")
        if (badgeType === "danger") {
            return 3
        }
        if (badgeType === "warning") {
            return 2
        }
        return 1
    }

    function compareSnapshotPositions(left, right, sortKey) {
        var leftItem = left || ({})
        var rightItem = right || ({})

        if (sortKey === "weight_desc") {
            return resolvePositionWeightRatio(rightItem) - resolvePositionWeightRatio(leftItem)
        }
        if (sortKey === "status_desc") {
            var rankDiff = snapshotStatusRank(rightItem) - snapshotStatusRank(leftItem)
            if (rankDiff !== 0) {
                return rankDiff
            }
            return numberOrDefault(rightItem.score, 0) - numberOrDefault(leftItem.score, 0)
        }
        if (sortKey === "symbol_asc") {
            return String(leftItem.symbol || leftItem.name || "").localeCompare(String(rightItem.symbol || rightItem.name || ""))
        }

        return numberOrDefault(rightItem.score, 0) - numberOrDefault(leftItem.score, 0)
    }

    function sortSnapshotPositions(positions, sortKey) {
        var copied = []
        var source = positions || []
        for (var i = 0; i < source.length; i++) {
            copied.push(source[i])
        }

        copied.sort(function(left, right) {
            return compareSnapshotPositions(left, right, sortKey)
        })

        return copied
    }

    function countSnapshotPositions(filterKey) {
        return filterSnapshotPositions(snapshotPositions, filterKey).length
    }

    function firstDefinedValue(source, keys) {
        if (!source) {
            return undefined
        }

        for (var index = 0; index < keys.length; ++index) {
            var key = keys[index]
            if (source[key] !== undefined && source[key] !== null && source[key] !== "") {
                return source[key]
            }
        }

        return undefined
    }

    function normalizePercentInput(value, fallback) {
        var numericValue = Number(value)
        if (isNaN(numericValue) || numericValue <= 0) {
            return fallback
        }
        return numericValue <= 1 ? numericValue * 100 : numericValue
    }

    function loadPortfolioOptimizationConfig() {
        var strategyParameters = getStrategyParameters(currentStrategyDetail)
        var advancedOptions = currentStrategyDetail && currentStrategyDetail.advanced_options
            ? currentStrategyDetail.advanced_options
            : (currentStrategyDetail && currentStrategyDetail.advancedOptions ? currentStrategyDetail.advancedOptions : ({}))
        var explicitOptimizationConfig = advancedOptions.optimization_config
            ? advancedOptions.optimization_config
            : (advancedOptions.optimizationConfig ? advancedOptions.optimizationConfig : ({}))
        var appliedRiskConfig = riskConfigService && riskConfigService.appliedConfiguration
            ? riskConfigService.appliedConfiguration
            : (riskConfigService && riskConfigService.loadAppliedConfiguration ? riskConfigService.loadAppliedConfiguration() : ({}))
        var currentRiskConfig = riskConfigService && riskConfigService.currentConfiguration
            ? riskConfigService.currentConfiguration
            : (riskConfigService && riskConfigService.loadCurrentConfiguration ? riskConfigService.loadCurrentConfiguration() : ({}))
        var effectiveRiskConfig = hasObjectData(appliedRiskConfig) ? appliedRiskConfig : currentRiskConfig

        var factorCount = Math.max(1, portfolioModel.count)
        var defaultMaxWeightPercent = clampValue((100 / factorCount) * 1.8, 22, 38)
        var defaultMinWeightPercent = factorCount <= 4 ? 10 : (factorCount <= 8 ? 6 : 3)

        var positionSizingMethod = String(
            firstDefinedValue(explicitOptimizationConfig, ["positionSizingMethod", "position_sizing_method"]) !== undefined
                ? firstDefinedValue(explicitOptimizationConfig, ["positionSizingMethod", "position_sizing_method"])
                : (firstDefinedValue(effectiveRiskConfig, ["positionSizingMethod", "position_sizing_method"]) !== undefined
                    ? firstDefinedValue(effectiveRiskConfig, ["positionSizingMethod", "position_sizing_method"])
                : (firstDefinedValue(strategyParameters, ["positionSizingMethod", "position_sizing_method"]) || "fixed")
                )
        ).trim()
        if (!positionSizingMethod) {
            positionSizingMethod = "fixed"
        }

        var maxPositionPercentValue = firstDefinedValue(explicitOptimizationConfig, ["maxPositionPercent", "maxSinglePositionRatio", "positionPercent", "position_size", "positionSize"])
        if (maxPositionPercentValue === undefined) {
            maxPositionPercentValue = firstDefinedValue(effectiveRiskConfig, ["maxPositionPercent", "maxSinglePositionRatio", "positionPercent", "position_size", "positionSize"])
        }
        if (maxPositionPercentValue === undefined) {
            maxPositionPercentValue = firstDefinedValue(strategyParameters, ["maxPositionPercent", "maxSinglePositionRatio", "positionPercent", "position_size", "positionSize"])
        }

        var maxTotalExposureValue = firstDefinedValue(explicitOptimizationConfig, ["maxTotalExposure", "maxPositionRatio"])
        if (maxTotalExposureValue === undefined) {
            maxTotalExposureValue = firstDefinedValue(effectiveRiskConfig, ["maxTotalExposure", "maxPositionRatio"])
        }
        if (maxTotalExposureValue === undefined) {
            maxTotalExposureValue = firstDefinedValue(strategyParameters, ["maxTotalExposure", "maxPositionRatio"])
        }

        var rebalanceDaysValue = firstDefinedValue(explicitOptimizationConfig, ["rebalanceDays", "rebalance_days", "rebalancingPeriod", "rebalanceFrequency"])
        if (rebalanceDaysValue === undefined) {
            rebalanceDaysValue = firstDefinedValue(effectiveRiskConfig, ["rebalanceDays", "rebalance_days", "rebalancingPeriod", "rebalanceFrequency"])
        }
        if (rebalanceDaysValue === undefined) {
            rebalanceDaysValue = firstDefinedValue(strategyParameters, ["rebalanceDays", "rebalance_days", "rebalancingPeriod", "rebalanceFrequency"])
        }

        var minWeightPercentValue = firstDefinedValue(explicitOptimizationConfig, ["minWeightPercent", "min_weight_percent"])
        if (minWeightPercentValue === undefined) {
            minWeightPercentValue = firstDefinedValue(effectiveRiskConfig, ["minWeightPercent", "min_weight_percent"])
        }
        if (minWeightPercentValue === undefined) {
            minWeightPercentValue = firstDefinedValue(strategyParameters, ["minWeightPercent", "min_weight_percent"])
        }

        var maxWeightPercentValue = firstDefinedValue(explicitOptimizationConfig, ["maxWeightPercent", "max_weight_percent"])
        if (maxWeightPercentValue === undefined) {
            maxWeightPercentValue = firstDefinedValue(effectiveRiskConfig, ["maxWeightPercent", "max_weight_percent"])
        }
        if (maxWeightPercentValue === undefined) {
            maxWeightPercentValue = firstDefinedValue(strategyParameters, ["maxWeightPercent", "max_weight_percent"])
        }

        var maxPositionsValue = firstDefinedValue(explicitOptimizationConfig, ["maxPositions", "top_n", "topN"])
        if (maxPositionsValue === undefined) {
            maxPositionsValue = firstDefinedValue(strategyParameters, ["maxPositions", "top_n", "topN"])
        }

        var maxPositionPercent = normalizePercentInput(maxPositionPercentValue, defaultMaxWeightPercent)
        var minWeightPercent = normalizePercentInput(minWeightPercentValue, defaultMinWeightPercent)
        var maxWeightPercent = normalizePercentInput(maxWeightPercentValue, maxPositionPercent)
        var maxTotalExposure = normalizePercentInput(maxTotalExposureValue, 100)
        var rebalanceDays = Number(rebalanceDaysValue)
        var maxPositions = Number(maxPositionsValue)
        if (isNaN(rebalanceDays) || rebalanceDays <= 0) {
            rebalanceDays = 20
        }
        if (isNaN(maxPositions) || maxPositions <= 0) {
            maxPositions = factorCount
        }

        return {
            positionSizingMethod: positionSizingMethod,
            maxPositionPercent: maxPositionPercent,
            maxSinglePositionRatio: maxPositionPercent,
            maxWeightPercent: maxWeightPercent,
            minWeightPercent: minWeightPercent,
            maxTotalExposure: maxTotalExposure,
            rebalanceDays: rebalanceDays,
            rebalanceFrequency: rebalanceDays,
            top_n: maxPositions,
            maxPositions: maxPositions
        }
    }

    function buildRiskNavigationPayload() {
        var strategyDetail = currentStrategyDetail && Object.keys(currentStrategyDetail).length > 0
            ? currentStrategyDetail
            : buildPortfolioStrategyData(portfolioMetrics, portfolioExposures)
        var payloadStrategy = {}

        for (var key in strategyDetail) {
            payloadStrategy[key] = strategyDetail[key]
        }

        if (!payloadStrategy.strategy_id && currentPortfolioId) {
            payloadStrategy.strategy_id = currentPortfolioId
        }
        if (!payloadStrategy.strategy_name && portfolioName) {
            payloadStrategy.strategy_name = portfolioName
        }
        if (!payloadStrategy.strategy_type) {
            payloadStrategy.strategy_type = "PORTFOLIO"
        }

        return {
            source: "portfolio_builder",
            strategyId: currentPortfolioId,
            strategyName: portfolioName,
            strategy: payloadStrategy,
            snapshot: portfolioSnapshot,
            latestBacktest: latestBacktestRecord,
            recordedAt: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
        }
    }

    function openRiskManagementPage() {
        requestNavigation("risk_management", "风险管理", buildRiskNavigationPayload())
        updateSimulation([createNotification("info", "已切换到风险管理页，可继续查看组合快照风险", "查看")])
    }

    function mergeNotifications(primaryNotifications, secondaryNotifications) {
        var merged = []
        var seenTexts = {}

        function appendItems(items) {
            if (!items || items.length === undefined) {
                return
            }

            for (var i = 0; i < items.length; i++) {
                var item = items[i] || {}
                var notificationText = String(item.text || "")
                if (!notificationText || seenTexts[notificationText]) {
                    continue
                }
                seenTexts[notificationText] = true
                merged.push(item)
                if (merged.length >= 3) {
                    return
                }
            }
        }

        appendItems(primaryNotifications)
        appendItems(secondaryNotifications)
        return merged
    }

    function filterVisibleNotifications(items) {
        if (!items || items.length === undefined || notificationsMuted) {
            return []
        }

        var dismissed = {}
        for (var i = 0; i < dismissedNotificationTexts.length; i++) {
            var dismissedText = String(dismissedNotificationTexts[i] || "")
            if (!dismissedText) {
                continue
            }
            dismissed[dismissedText] = true
        }

        var visibleItems = []
        for (var index = 0; index < items.length; index++) {
            var item = items[index] || ({})
            var notificationText = String(item.text || "")
            if (!notificationText || dismissed[notificationText]) {
                continue
            }
            visibleItems.push(item)
        }

        return visibleItems
    }

    function rememberDismissedNotifications(items) {
        if (!items || items.length === undefined) {
            return
        }

        var nextDismissed = []
        var seenTexts = {}

        for (var i = 0; i < dismissedNotificationTexts.length; i++) {
            var existingText = String(dismissedNotificationTexts[i] || "")
            if (!existingText || seenTexts[existingText]) {
                continue
            }
            seenTexts[existingText] = true
            nextDismissed.push(existingText)
        }

        for (var index = 0; index < items.length; index++) {
            var notificationText = String((items[index] || {}).text || "")
            if (!notificationText || seenTexts[notificationText]) {
                continue
            }
            seenTexts[notificationText] = true
            nextDismissed.push(notificationText)
        }

        dismissedNotificationTexts = nextDismissed
    }

    function clearDismissedNotifications() {
        if (dismissedNotificationTexts.length > 0) {
            dismissedNotificationTexts = []
        }
    }

    function collectPortfolioEntries() {
        var entries = []
        for (var i = 0; i < portfolioModel.count; i++) {
            entries.push(portfolioModel.get(i))
        }
        return entries
    }

    function numberOrDefault(value, fallback) {
        var numericValue = Number(value)
        return isNaN(numericValue) ? fallback : numericValue
    }

    function normalizePercentMetric(value) {
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return 0
        }
        return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
    }

    function hasObjectData(value) {
        return value && typeof value === "object" && Object.keys(value).length > 0
    }

    function getStrategyParameters(strategy) {
        if (!strategy) {
            return ({})
        }
        return strategy.parameters || strategy.strategy_params || strategy.strategyParams || ({})
    }

    function getStrategyPerformance(strategy) {
        if (!strategy) {
            return ({})
        }
        return strategy.performance_metrics || strategy.performanceMetrics || ({})
    }

    function getLatestBacktest(strategy) {
        var performance = getStrategyPerformance(strategy)
        return performance.latestBacktest || performance.latest_backtest || ({})
    }

    function isDefaultPortfolioDraft() {
        var normalizedPortfolioId = String(currentPortfolioId || "").trim()
        return !normalizedPortfolioId || normalizedPortfolioId === "momentum_portfolio"
    }

    function shouldRestoreCurrentPortfolio() {
        var normalizedPortfolioId = String(currentPortfolioId || "").trim()
        if (!normalizedPortfolioId) {
            return false
        }

        var restoredPortfolioId = String(lastRestoredPortfolioId || "").trim()
        if (hasPendingPortfolioEdits && restoredPortfolioId === normalizedPortfolioId) {
            return false
        }

        if (restoredPortfolioId !== normalizedPortfolioId) {
            return true
        }

        if (portfolioModel.count === 0) {
            return true
        }

        return !hasPositivePortfolioWeights()
    }

    function loadPortfolioContextById(strategyId, strategyName) {
        var normalizedStrategyId = String(strategyId || "").trim()
        if (!normalizedStrategyId || !strategyService || !strategyService.getStrategyById) {
            return false
        }

        currentPortfolioId = normalizedStrategyId
        lastRestoredPortfolioId = ""
        hasPendingPortfolioEdits = false
        if (String(strategyName || "").trim()) {
            portfolioName = String(strategyName || "").trim()
        }

        var detail = loadCurrentStrategyDetail()
        if (!detail || Object.keys(detail).length === 0) {
            return false
        }

        currentStrategyDetail = detail
        return loadSavedPortfolio()
    }

    function applyExternalContext(context) {
        var payload = context || ({})
        var strategy = payload.strategy || ({})
        var strategyId = String(
            payload.strategyId !== undefined ? payload.strategyId
                : (payload.strategy_id !== undefined ? payload.strategy_id
                    : (strategy.strategy_id !== undefined ? strategy.strategy_id : strategy.id))
        || "").trim()
        var strategyName = String(
            payload.strategyName !== undefined ? payload.strategyName
                : (payload.strategy_name !== undefined ? payload.strategy_name
                    : (strategy.strategy_name !== undefined ? strategy.strategy_name : strategy.name))
        || "").trim()

        if (!strategyId) {
            return false
        }

        return loadPortfolioContextById(strategyId, strategyName)
    }

    function syncBoundPortfolioContextIfNeeded(forceReload) {
        if (!tradingConnectionConfigService) {
            return false
        }

        var configuration = tradingConnectionConfigService.currentConfiguration || ({})
        var boundStrategyId = String(configuration.boundStrategyId || "").trim()
        var boundStrategyName = String(configuration.boundStrategyName || "").trim()
        if (!boundStrategyId) {
            return false
        }

        var currentStrategyId = String(currentPortfolioId || "").trim()
        var hasCurrentDetail = currentStrategyDetail && Object.keys(currentStrategyDetail).length > 0
        var shouldAdopt = forceReload
            || isDefaultPortfolioDraft()
            || !hasCurrentDetail
            || currentStrategyId !== boundStrategyId
        if (!shouldAdopt) {
            return false
        }

        return loadPortfolioContextById(boundStrategyId, boundStrategyName)
    }

    function normalizeSymbolValue(symbol) {
        return String(symbol || "").trim().toUpperCase()
    }

    function appendSymbolCollection(targetSymbols, seenSymbols, rawCollection) {
        var addSymbol = function(symbol) {
            var normalized = normalizeSymbolValue(symbol)
            if (!normalized || seenSymbols[normalized]) {
                return
            }

            seenSymbols[normalized] = true
            targetSymbols.push(normalized)
        }

        if (Array.isArray(rawCollection)) {
            for (var index = 0; index < rawCollection.length; ++index) {
                addSymbol(rawCollection[index])
            }
            return
        }

        if (rawCollection !== undefined && rawCollection !== null) {
            var rawText = String(rawCollection).trim()
            if (!rawText) {
                return
            }

            if (rawText.charAt(0) === "[") {
                try {
                    var parsed = JSON.parse(rawText)
                    if (Array.isArray(parsed)) {
                        for (var parsedIndex = 0; parsedIndex < parsed.length; ++parsedIndex) {
                            addSymbol(parsed[parsedIndex])
                        }
                        return
                    }
                } catch (error) {
                }
            }

            rawText.split(/[,;\s，；]+/).forEach(addSymbol)
        }
    }

    function resolveExistingStrategySymbolPool() {
        var resolvedSymbols = []
        var seenSymbols = ({})
        var strategyParameters = getStrategyParameters(currentStrategyDetail)
        var existingPool = currentStrategyDetail && currentStrategyDetail.symbol_pool !== undefined
            ? currentStrategyDetail.symbol_pool
            : (currentStrategyDetail && currentStrategyDetail.symbolPool !== undefined
                ? currentStrategyDetail.symbolPool
                : (strategyParameters.symbol_pool !== undefined
                    ? strategyParameters.symbol_pool
                    : strategyParameters.symbolPool))

        appendSymbolCollection(resolvedSymbols, seenSymbols, existingPool)
        return resolvedSymbols
    }

    function resolveLatestBacktestSymbolPool() {
        var resolvedSymbols = []
        var seenSymbols = ({})
        var latestBacktest = latestBacktestRecord && Object.keys(latestBacktestRecord).length > 0
            ? latestBacktestRecord
            : (getLatestBacktest(currentStrategyDetail) || ({}))
        var runtimeParameters = latestBacktest.runtimeParameters
            ? latestBacktest.runtimeParameters
            : (latestBacktest.runtime_parameters ? latestBacktest.runtime_parameters : ({}))

        appendSymbolCollection(resolvedSymbols, seenSymbols, latestBacktest.symbol_pool)
        appendSymbolCollection(resolvedSymbols, seenSymbols, latestBacktest.symbolPool)
        appendSymbolCollection(resolvedSymbols, seenSymbols, runtimeParameters.symbol_pool)
        appendSymbolCollection(resolvedSymbols, seenSymbols, runtimeParameters.symbolPool)
        appendSymbolCollection(resolvedSymbols, seenSymbols, latestBacktest.symbols)
        appendSymbolCollection(resolvedSymbols, seenSymbols, latestBacktest.selectedSymbols)
        appendSymbolCollection(resolvedSymbols, seenSymbols, runtimeParameters.symbols)
        appendSymbolCollection(resolvedSymbols, seenSymbols, runtimeParameters.selectedSymbols)

        return resolvedSymbols
    }

    function resolveRuntimeConfiguredSymbolPool() {
        var resolvedSymbols = []
        var seenSymbols = ({})
        var currentConfiguration = tradingConnectionConfigService && tradingConnectionConfigService.currentConfiguration
            ? tradingConnectionConfigService.currentConfiguration
            : (tradingConnectionConfigService && tradingConnectionConfigService.loadCurrentConfiguration
                ? tradingConnectionConfigService.loadCurrentConfiguration()
                : ({}))

        appendSymbolCollection(resolvedSymbols, seenSymbols, currentConfiguration.symbol_pool)
        appendSymbolCollection(resolvedSymbols, seenSymbols, currentConfiguration.symbolPool)
        appendSymbolCollection(resolvedSymbols, seenSymbols, currentConfiguration.symbols)
        appendSymbolCollection(resolvedSymbols, seenSymbols, currentConfiguration.selectedSymbols)

        return resolvedSymbols
    }

    function buildSymbolPoolResolution(symbols, sourceKey, sourceLabel) {
        return {
            symbols: symbols || [],
            sourceKey: String(sourceKey || "none"),
            sourceLabel: String(sourceLabel || "未命中"),
            count: symbols && symbols.length ? symbols.length : 0
        }
    }

    function resolvePortfolioSymbolPoolState() {
        var resolvedSymbols = []
        var seenSymbols = ({})

        var appendPositions = function(positions) {
            var source = positions || []
            for (var index = 0; index < source.length; ++index) {
                var position = source[index] || ({})
                appendSymbolCollection(resolvedSymbols, seenSymbols, [position.symbol || position.name])
            }
        }

        appendPositions(snapshotPositions || [])
        if (resolvedSymbols.length > 0) {
            return buildSymbolPoolResolution(resolvedSymbols, "snapshotPositions", "当前快照持仓")
        }

        if (portfolioSnapshot && portfolioSnapshot.positions) {
            appendPositions(portfolioSnapshot.positions)
        }
        if (resolvedSymbols.length > 0) {
            return buildSymbolPoolResolution(resolvedSymbols, "portfolioSnapshot", "组合快照")
        }

        if (portfolioState && portfolioState.snapshot && portfolioState.snapshot.positions) {
            appendPositions(portfolioState.snapshot.positions)
        }
        if (resolvedSymbols.length > 0) {
            return buildSymbolPoolResolution(resolvedSymbols, "portfolioStateSnapshot", "缓存快照")
        }

        appendSymbolCollection(resolvedSymbols, seenSymbols, resolveExistingStrategySymbolPool())
        if (resolvedSymbols.length > 0) {
            return buildSymbolPoolResolution(resolvedSymbols, "existingStrategy", "已保存策略")
        }

        appendSymbolCollection(resolvedSymbols, seenSymbols, resolveLatestBacktestSymbolPool())
        if (resolvedSymbols.length > 0) {
            return buildSymbolPoolResolution(resolvedSymbols, "latestBacktest", "最近回测或运行时参数")
        }

        appendSymbolCollection(resolvedSymbols, seenSymbols, resolveRuntimeConfiguredSymbolPool())
        if (resolvedSymbols.length > 0) {
            return buildSymbolPoolResolution(resolvedSymbols, "runtimeConfiguration", "当前交易配置")
        }

        return buildSymbolPoolResolution([], "none", "未命中")
    }

    function resolvePortfolioSymbolPool() {
        return resolvePortfolioSymbolPoolState().symbols
    }

    function refreshPortfolioStateBeforeSave() {
        if (portfolioModel.count === 0) {
            return
        }

        ensurePositivePortfolioWeights()

        if (snapshotPositions && snapshotPositions.length > 0) {
            return
        }

        rebuildPortfolioState()

        if (snapshotPositions && snapshotPositions.length > 0) {
            return
        }

        applyDirectSnapshotResult(buildDirectPortfolioSnapshot())
    }

    function canPersistPortfolioSymbolPool() {
        var resolvedPoolState = resolvePortfolioSymbolPoolState()
        if (resolvedPoolState.count > 0) {
            return true
        }

        return resolveExistingStrategySymbolPool().length > 0
    }

    function buildSymbolPoolPersistenceWarning() {
        var snapshotError = portfolioSnapshot && portfolioSnapshot.error
            ? String(portfolioSnapshot.error || "").trim()
            : ""
        var allocationSummary = buildAllocationDebugSummary()
        var resolvedPoolState = resolvePortfolioSymbolPoolState()
        var sourceSummary = "股票池来源: " + resolvedPoolState.sourceLabel + "（" + resolvedPoolState.count + " 只）"
        if (snapshotError) {
            return "当前组合还没有生成候选持仓，暂时无法写入标的池；当前快照结果: " + snapshotError + "；" + sourceSummary + "；" + allocationSummary
        }

        return "当前组合还没有生成候选持仓，暂时无法写入标的池；请先刷新组合快照后再保存；" + sourceSummary + "；" + allocationSummary
    }

    function buildAllocationDebugSummary() {
        var factorAllocations = buildPortfolioFactorAllocations()
        var usableCount = 0
        var missingFactorIdCount = 0
        var zeroWeightCount = 0
        var samples = []

        for (var i = 0; i < factorAllocations.length; i++) {
            var allocation = factorAllocations[i] || ({})
            var factorId = String(allocation.factor_id || "").trim()
            var weight = Number(allocation.weight || 0)

            if (!factorId) {
                missingFactorIdCount += 1
            }
            if (!(weight > 0)) {
                zeroWeightCount += 1
            }
            if (factorId && weight > 0) {
                usableCount += 1
            }
            if (samples.length < 3) {
                samples.push((factorId || "<empty>") + "@" + weight)
            }
        }

        return "因子=" + factorAllocations.length
            + "，可用=" + usableCount
            + "，空ID=" + missingFactorIdCount
            + "，零权重=" + zeroWeightCount
            + "，样本=" + samples.join(",")
    }

    function buildPortfolioFactorAllocations() {
        var factorAllocations = []
        for (var i = 0; i < portfolioModel.count; i++) {
            var factor = portfolioModel.get(i)
            var resolvedFactorId = String(factor.factorId || "").trim()
            if (!resolvedFactorId) {
                var resolvedFactor = findFactorInPoolByText(factor.displayName)
                resolvedFactorId = resolvedFactor && resolvedFactor.factorId ? String(resolvedFactor.factorId) : ""
                if (resolvedFactorId) {
                    portfolioModel.setProperty(i, "factorId", resolvedFactorId)
                }
            }
            factorAllocations.push({
                factor_id: resolvedFactorId,
                display_name: factor.displayName,
                weight: factor.weight,
                correlation: factor.correlation,
                ic_value: Number(factor.icValue || 0),
                ir_value: Number(factor.irValue || 0),
                turnover_rate: Number(factor.turnoverRate || 0),
                category: factor.category || "综合类"
            })
        }
        return factorAllocations
    }

    function buildPortfolioSnapshotRequestData(metricsOverride, exposuresOverride) {
        var factorAllocations = buildPortfolioFactorAllocations()
        var metrics = metricsOverride || portfolioMetrics
        var exposures = exposuresOverride || portfolioExposures
        var optimizationConfig = loadPortfolioOptimizationConfig()
        var symbolPool = resolvePortfolioSymbolPool()
        var parameters = {
            portfolio_name: portfolioName,
            total_weight: totalWeight,
            factor_count: portfolioModel.count,
            allocations: factorAllocations,
            factor_allocations: factorAllocations,
            portfolio_allocations_json: JSON.stringify(factorAllocations),
            symbol_pool: symbolPool,
            positionSizingMethod: optimizationConfig.positionSizingMethod,
            position_sizing_method: optimizationConfig.positionSizingMethod,
            maxPositionPercent: optimizationConfig.maxPositionPercent,
            maxSinglePositionRatio: optimizationConfig.maxSinglePositionRatio,
            positionPercent: optimizationConfig.maxPositionPercent,
            position_size: optimizationConfig.maxPositionPercent,
            positionSize: optimizationConfig.maxPositionPercent,
            maxWeightPercent: optimizationConfig.maxWeightPercent,
            max_weight_percent: optimizationConfig.maxWeightPercent,
            minWeightPercent: optimizationConfig.minWeightPercent,
            min_weight_percent: optimizationConfig.minWeightPercent,
            maxTotalExposure: optimizationConfig.maxTotalExposure,
            maxPositionRatio: optimizationConfig.maxTotalExposure,
            rebalanceDays: optimizationConfig.rebalanceDays,
            rebalance_days: optimizationConfig.rebalanceDays,
            rebalanceFrequency: optimizationConfig.rebalanceFrequency,
            top_n: optimizationConfig.top_n,
            maxPositions: optimizationConfig.maxPositions,
            backtest_runtime: {
                positionSizingMethod: optimizationConfig.positionSizingMethod,
                maxPositionPercent: optimizationConfig.maxPositionPercent,
                maxTotalExposure: optimizationConfig.maxTotalExposure,
                rebalanceDays: optimizationConfig.rebalanceDays,
                minWeightPercent: optimizationConfig.minWeightPercent,
                maxWeightPercent: optimizationConfig.maxWeightPercent,
                top_n: optimizationConfig.top_n,
                maxPositions: optimizationConfig.maxPositions
            },
            estimated_metrics: {
                annual_return: metrics.annualReturn,
                sharpe_ratio: metrics.sharpeRatio,
                max_drawdown: metrics.maxDrawdown
            },
            exposures: {
                sector: exposures.sector,
                style: exposures.style
            }
        }

        if (symbolPool.length > 0) {
            parameters.symbol_pool = symbolPool
        }

        var strategyData = {
            strategy_name: portfolioName,
            strategy_type: "PORTFOLIO",
            description: "组合构建页保存的多因子组合策略",
            asset_type: "stock",
            time_frame: "daily",
            risk_level: metrics.maxDrawdown > 20 ? "high" : (metrics.maxDrawdown > 12 ? "medium" : "low"),
            optimization_method: "portfolio_builder",
            advanced_options: {
                source: "PortfolioBuilderPage",
                saved_at: new Date().toISOString(),
                optimization_config: optimizationConfig
            },
            parameters: parameters,
            factor_allocations: factorAllocations,
            sub_type: "portfolio_builder",
            status: "DRAFT",
            version: "1.0",
            language: "Python",
            author: "PortfolioBuilder",
            tags: ["组合策略", "多因子", "PortfolioBuilder"]
        }

        if (symbolPool.length > 0) {
            strategyData.symbol_pool = symbolPool
        }

        return strategyData
    }

    function applyDirectSnapshotResult(snapshotResult) {
        if (!snapshotResult || typeof snapshotResult !== "object") {
            return false
        }

        portfolioState = {
            metrics: portfolioMetrics,
            exposures: portfolioExposures,
            snapshot: snapshotResult,
            backtest: latestBacktestRecord,
            systemStatus: systemStatus,
            notifications: notifications,
            insights: {
                suggestion: portfolioSuggestion
            },
            lastUpdated: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
        }

        return String(snapshotResult.status || "") === "success"
            && snapshotResult.positions
            && snapshotResult.positions.length !== undefined
            && snapshotResult.positions.length > 0
    }

    function buildDirectPortfolioSnapshot() {
        if (!riskMonitorService || typeof riskMonitorService.buildPortfolioSnapshot !== "function") {
            return ({})
        }

        return riskMonitorService.buildPortfolioSnapshot(
            buildPortfolioSnapshotRequestData(portfolioMetrics, portfolioExposures),
            getLatestBacktest(currentStrategyDetail) || ({})) || ({})
    }

    function resolvePositionWeightRatio(position) {
        var ratioPercent = Number(position.ratioValue || 0)
        if (ratioPercent > 0) {
            return ratioPercent / 100.0
        }

        var ratioText = String(position.ratio || "").replace("%", "")
        var numericRatio = Number(ratioText)
        return isNaN(numericRatio) ? 0 : numericRatio / 100.0
    }

    function syncExposureModels(exposures) {
        var sectorColors = {
            "银行": "#3B82F6",
            "消费": "#10B981",
            "医药": "#8B5CF6",
            "科技": "#F59E0B"
        }
        var styleColors = {
            "市值": "#3B82F6",
            "动量": "#10B981",
            "价值": "#8B5CF6",
            "波动率": "#F59E0B"
        }
        var styleTargets = {
            "市值": 1.0,
            "动量": 0.8,
            "价值": 0.6,
            "波动率": 1.0
        }

        sectorModel.clear()
        styleModel.clear()

        var sectorData = exposures && exposures.sector ? exposures.sector : emptySectorExposure
        var styleData = exposures && exposures.style ? exposures.style : emptyStyleExposure

        for (var sectorName in sectorData) {
            sectorModel.append({
                sector: sectorName,
                weight: Number(sectorData[sectorName] || 0),
                color: sectorColors[sectorName] || "#94A3B8"
            })
        }

        for (var styleName in styleData) {
            styleModel.append({
                style: styleName,
                value: Number(styleData[styleName] || 0),
                target: Number(styleTargets[styleName] || 1.0),
                color: styleColors[styleName] || "#94A3B8"
            })
        }
    }

    function loadCurrentStrategyDetail() {
        if (!strategyService || !strategyService.getStrategyById || !currentPortfolioId) {
            return ({})
        }

        return strategyService.getStrategyById(currentPortfolioId) || ({})
    }

    function parseAllocationList(rawAllocations) {
        if (!rawAllocations) {
            return []
        }

        if (typeof rawAllocations === "string") {
            var jsonText = rawAllocations.trim()
            if (!jsonText) {
                return []
            }
            try {
                var parsed = JSON.parse(jsonText)
                return parsed && parsed.length !== undefined ? parsed : []
            } catch (error) {
                console.warn("组合分配解析失败:", error)
                return []
            }
        }

        return rawAllocations.length !== undefined ? rawAllocations : []
    }

    function findFactorInPool(factorId) {
        for (var i = 0; i < factorPoolModel.count; i++) {
            var factor = factorPoolModel.get(i)
            if (factor.factorId === factorId) {
                return factor
            }
        }
        return null
    }

    function normalizeFactorLookupText(value) {
        return String(value || "").trim().toLowerCase()
    }

    function dedupePortfolioModel() {
        var seenKeys = ({})
        var removedCount = 0

        for (var index = portfolioModel.count - 1; index >= 0; --index) {
            var item = portfolioModel.get(index) || ({})
            var factorIdKey = normalizeFactorLookupText(item.factorId)
            var displayNameKey = normalizeFactorLookupText(item.displayName)
            var uniqueKey = factorIdKey || displayNameKey

            if (!uniqueKey) {
                continue
            }

            if (seenKeys[uniqueKey]) {
                portfolioModel.remove(index)
                removedCount += 1
                continue
            }

            seenKeys[uniqueKey] = true
        }

        return removedCount
    }

    function findFactorInPoolByText(text) {
        var normalizedText = normalizeFactorLookupText(text)
        if (!normalizedText) {
            return null
        }

        for (var i = 0; i < factorPoolModel.count; i++) {
            var factor = factorPoolModel.get(i)
            if (normalizeFactorLookupText(factor.factorId) === normalizedText
                    || normalizeFactorLookupText(factor.displayName) === normalizedText
                    || normalizeFactorLookupText(factor.factorName) === normalizedText
                    || normalizeFactorLookupText(factor.name) === normalizedText) {
                return factor
            }
        }

        if (!factorService || typeof factorService.getAllFactors !== "function") {
            return null
        }

        var allFactors = factorService.getAllFactors() || []
        for (var index = 0; index < allFactors.length; ++index) {
            var candidate = normalizeFactorRecord(allFactors[index], index)
            if (normalizeFactorLookupText(candidate.factorId) === normalizedText
                    || normalizeFactorLookupText(candidate.displayName) === normalizedText) {
                return candidate
            }
        }

        return null
    }

    function resolveAllocationFactorId(allocation, displayName) {
        var candidateId = String(
            allocation.factor_id || allocation.factorId || allocation.instance_id || allocation.instanceId
            || allocation.id || allocation.factor_name || allocation.factorName || "")
        if (candidateId.trim()) {
            return candidateId.trim()
        }

        var matchedFactor = findFactorInPoolByText(displayName)
        return matchedFactor && matchedFactor.factorId ? String(matchedFactor.factorId) : ""
    }

    function resolveAllocationWeight(allocation) {
        var rawWeight = allocation.weight
        if (rawWeight === undefined || rawWeight === null || rawWeight === "") {
            rawWeight = allocation.ratio
        }
        if (rawWeight === undefined || rawWeight === null || rawWeight === "") {
            rawWeight = allocation.allocation
        }
        if (rawWeight === undefined || rawWeight === null || rawWeight === "") {
            rawWeight = allocation.value
        }

        var normalizedWeightText = String(rawWeight || "").trim()
        if (normalizedWeightText.indexOf("%") >= 0) {
            normalizedWeightText = normalizedWeightText.replace(/%/g, "")
        }
        normalizedWeightText = normalizedWeightText.replace(/，/g, ",")

        var numericWeight = normalizedWeightText ? Number(normalizedWeightText) : Number(rawWeight || 0)
        if (isNaN(numericWeight)) {
            return 0
        }

        return Math.abs(numericWeight) <= 1 ? numericWeight * 100 : numericWeight
    }

    function normalizeAllocationRecord(rawAllocation) {
        var allocation = rawAllocation || ({})
        var displayName = String(
            allocation.display_name || allocation.displayName || allocation.factor_name || allocation.factorName
            || allocation.name || allocation.id || "")
        var factorId = resolveAllocationFactorId(allocation, displayName)
        var poolFactor = findFactorInPool(factorId)
        return {
            factorId: factorId,
            displayName: displayName || String(poolFactor ? poolFactor.displayName : factorId),
            weight: resolveAllocationWeight(allocation),
            correlation: Number(allocation.correlation || (poolFactor ? poolFactor.correlation : 0)),
            icValue: Number(allocation.ic_value || allocation.icValue || (poolFactor ? poolFactor.icValue : 0)),
            irValue: Number(allocation.ir_value || allocation.irValue || (poolFactor ? poolFactor.irValue : 0)),
            turnoverRate: Number(allocation.turnover_rate || allocation.turnoverRate || (poolFactor ? poolFactor.turnoverRate : 0)),
            category: String(allocation.category || (poolFactor ? poolFactor.category : "综合类")),
            color: getFactorColor(String(allocation.category || (poolFactor ? poolFactor.category : "综合类")))
        }
    }

    function hasPositivePortfolioWeights() {
        for (var i = 0; i < portfolioModel.count; i++) {
            if (Number(portfolioModel.get(i).weight || 0) > 0) {
                return true
            }
        }
        return false
    }

    function ensurePositivePortfolioWeights() {
        if (portfolioModel.count === 0 || hasPositivePortfolioWeights()) {
            return false
        }

        var equalWeight = 100.0 / portfolioModel.count
        for (var i = 0; i < portfolioModel.count; i++) {
            portfolioModel.setProperty(i, "weight", equalWeight)
        }

        updateTotalWeight()
        return true
    }

    function loadSavedPortfolio(strategyOverride) {
        if (!currentPortfolioId) {
            lastRestoredPortfolioId = ""
            hasPendingPortfolioEdits = false
            return false
        }

        var strategy = strategyOverride || ({})
        if ((!strategy || Object.keys(strategy).length === 0) && strategyService && strategyService.getStrategyById) {
            strategy = strategyService.getStrategyById(currentPortfolioId) || ({})
        }
        if (!strategy || Object.keys(strategy).length === 0) {
            lastRestoredPortfolioId = ""
            hasPendingPortfolioEdits = false
            return false
        }

        currentStrategyDetail = strategy

        var parameters = strategy.parameters || ({})
        portfolioName = String(strategy.strategy_name || parameters.portfolio_name || portfolioName)
        currentPortfolioId = String(strategy.strategy_id || strategy.id || currentPortfolioId)

        var allocations = parseAllocationList(
            parameters.portfolio_allocations_json
            || parameters.factor_allocations
            || parameters.allocations
            || strategy.factor_allocations)

        if (allocations.length === 0) {
            lastRestoredPortfolioId = ""
            hasPendingPortfolioEdits = false
            return false
        }

        portfolioModel.clear()
        for (var i = 0; i < allocations.length; i++) {
            portfolioModel.append(normalizeAllocationRecord(allocations[i]))
        }

        dedupePortfolioModel()
        lastRestoredPortfolioId = String(currentPortfolioId || "").trim()
        hasPendingPortfolioEdits = false

        var restoredDefaultWeights = ensurePositivePortfolioWeights()
        updateTotalWeight()
        updateSimulation([createNotification(
            restoredDefaultWeights ? "warning" : "success",
            restoredDefaultWeights
                ? ("已恢复已保存组合: " + portfolioName + "；历史权重缺失，已按等权重回填")
                : ("已恢复已保存组合: " + portfolioName),
            "查看")])
        return true
    }

    function rebuildPortfolioStateLocally(notificationOverrides) {
        var latestBacktest = getLatestBacktest(currentStrategyDetail)
        var summary = latestBacktest.summary || ({})
        var metrics = hasObjectData(latestBacktest)
            ? {
                annualReturn: normalizePercentMetric(
                    summary.annualReturn !== undefined ? summary.annualReturn
                        : (summary.annualizedReturn !== undefined ? summary.annualizedReturn : summary.returns)),
                sharpeRatio: numberOrDefault(summary.sharpeRatio, 0),
                maxDrawdown: Math.abs(normalizePercentMetric(summary.maxDrawdown)),
                source: "latestBacktest",
                recordedAt: String(latestBacktest.recordedAt || "")
            }
            : ({
                annualReturn: 0.0,
                sharpeRatio: 0.0,
                maxDrawdown: 0.0,
                source: "fallback",
                recordedAt: ""
            })
        var exposures = {
            sector: emptySectorExposure,
            style: emptyStyleExposure
        }
        var snapshot = {
            status: portfolioModel.count > 0 ? "unavailable" : "idle",
            positions: [],
            diagnostics: {},
            error: portfolioModel.count > 0 ? "组合分析服务不可用，当前仅保留基础展示" : "当前组合为空"
        }
        var connectedServices = 0
        if (factorService) connectedServices += 1
        if (strategyService) connectedServices += 1
        if (riskMonitorService) connectedServices += 1
        var automaticNotifications = []
        if (portfolioModel.count === 0) {
            automaticNotifications.push(createNotification("info", "等待添加组合因子", "添加因子"))
        } else {
            automaticNotifications.push(createNotification("warning", "组合分析服务当前不可用，页面仅展示已保存权重与最近回测结果", "检查"))
            if (Math.abs(totalWeight - 100.0) > 0.5) {
                automaticNotifications.push(createNotification("warning", "总权重为 " + totalWeight.toFixed(1) + "% ，建议重置到 100%", "重置"))
            }
            if (hasObjectData(latestBacktest)) {
                automaticNotifications.push(createNotification(
                    "success",
                    "最近回测收益 " + metrics.annualReturn.toFixed(1) + "% ，夏普 " + numberOrDefault(summary.sharpeRatio, 0).toFixed(2),
                    "查看"))
            }
        }
        var nextNotifications = notificationOverrides && notificationOverrides.length !== undefined
            ? mergeNotifications(notificationOverrides, automaticNotifications)
            : automaticNotifications

        syncExposureModels(exposures)
        currentPortfolio = collectPortfolioEntries()
        portfolioState = {
            metrics: metrics,
            exposures: exposures,
            snapshot: snapshot,
            backtest: latestBacktest,
            systemStatus: {
                "因子池": {
                    status: factorPoolModel.count > 0 ? "📚" : "📭",
                    value: factorPoolModel.count + " 个",
                    color: factorPoolModel.count > 0 ? "#3B82F6" : "#94A3B8"
                },
                "当前组合": {
                    status: portfolioModel.count > 0 ? "🧩" : "➕",
                    value: portfolioModel.count + " 个",
                    color: portfolioModel.count > 0 ? "#10B981" : "#94A3B8"
                },
                "最近回测": {
                    status: hasObjectData(latestBacktest) ? "🟢" : "🟡",
                    value: hasObjectData(latestBacktest) ? (metrics.annualReturn.toFixed(1) + "%") : "待回测",
                    color: hasObjectData(latestBacktest) ? "#10B981" : "#F59E0B"
                },
                "风险快照": {
                    status: portfolioModel.count > 0 ? "🔴" : "🟡",
                    value: portfolioModel.count > 0 ? "服务未连" : "待生成",
                    color: portfolioModel.count > 0 ? "#EF4444" : "#F59E0B"
                },
                "数据源": {
                    status: connectedServices === 4 ? "🟢" : "🟡",
                    value: connectedServices + "/4 已连接",
                    color: connectedServices >= 3 ? "#10B981" : "#F59E0B"
                }
            },
            notifications: nextNotifications,
            insights: {
                suggestion: portfolioModel.count === 0
                    ? "先从左侧添加真实因子，组合分析恢复后会自动刷新。"
                    : "当前页面已切到轻量兜底模式，建议检查 PortfolioAnalysisService 状态后再继续优化或风险分析。"
            },
            lastUpdated: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
        }
    }

    function rebuildPortfolioState(notificationOverrides) {
        if (!portfolioAnalysisService || !portfolioAnalysisService.analyzePortfolioState) {
            rebuildPortfolioStateLocally(notificationOverrides)
            return
        }

        var latestBacktest = getLatestBacktest(currentStrategyDetail)
        var analyzedState = portfolioAnalysisService.analyzePortfolioState(
            buildPortfolioStrategyData(),
            latestBacktest || ({})) || ({})

        if (!analyzedState.metrics || !analyzedState.exposures || !analyzedState.snapshot) {
            rebuildPortfolioStateLocally(notificationOverrides)
            return
        }

        var nextExposures = analyzedState.exposures || ({ sector: emptySectorExposure, style: emptyStyleExposure })
        var automaticNotifications = analyzedState.notifications && analyzedState.notifications.length !== undefined
            ? analyzedState.notifications
            : []
        var nextNotifications = notificationOverrides && notificationOverrides.length !== undefined
            ? mergeNotifications(notificationOverrides, automaticNotifications)
            : automaticNotifications

        syncExposureModels(nextExposures)
        currentPortfolio = collectPortfolioEntries()
        portfolioState = {
            metrics: analyzedState.metrics || ({ annualReturn: 0.0, sharpeRatio: 0.0, maxDrawdown: 0.0 }),
            exposures: nextExposures,
            snapshot: analyzedState.snapshot || ({ status: "idle", positions: [], diagnostics: {} }),
            backtest: analyzedState.backtest || latestBacktest || ({}),
            systemStatus: analyzedState.systemStatus || ({}),
            notifications: nextNotifications,
            insights: analyzedState.insights || ({ suggestion: "等待组合加载完成后生成建议" }),
            lastUpdated: analyzedState.lastUpdated || Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
        }
    }

    function resolveFactorFromService(factorId) {
        if (!factorService || typeof factorService.getFactorById !== "function" || !factorId) {
            return null
        }

        var rawFactor = factorService.getFactorById(String(factorId)) || ({})
        if (!rawFactor || Object.keys(rawFactor).length === 0) {
            return null
        }

        return normalizeFactorRecord(rawFactor, factorPoolModel.count)
    }
    
    // 添加因子到组合
    function addFactorToPortfolio(factorId) {
        console.log("添加因子到组合:", factorId)
        
        // 检查是否已存在
        for (var i = 0; i < portfolioModel.count; i++) {
            if (portfolioModel.get(i).factorId === factorId) {
                console.log("因子已在组合中")
                updateSimulation([createNotification("info", "因子已在当前组合中", "查看")])
                return
            }
        }
        
        // 从因子池获取数据
        var factorData = null
        for (var j = 0; j < factorPoolModel.count; j++) {
            if (factorPoolModel.get(j).factorId === factorId) {
                factorData = factorPoolModel.get(j)
                break
            }
        }

        if (!factorData) {
            factorData = resolveFactorFromService(factorId)
            if (factorData) {
                factorPoolModel.append(factorData)
            }
        }
        
        if (factorData) {
            // 添加到组合
            portfolioModel.append({
                factorId: factorId,
                displayName: factorData.displayName,
                weight: 20.0,  // 默认权重
                correlation: factorData.correlation,
                icValue: factorData.icValue,
                irValue: factorData.irValue,
                turnoverRate: factorData.turnoverRate,
                category: factorData.category,
                color: getFactorColor(factorData.category)
            })
            hasPendingPortfolioEdits = true
            
            // 重新计算权重
            rebalanceWeights()
            updateSimulation([createNotification("success", "已添加因子: " + factorData.displayName, "查看")])
            return
        }

        updateSimulation([createNotification("warning", "未找到因子数据，无法加入组合: " + String(factorId), "刷新")])
    }
    
    // 移除因子
    function removeFactorFromPortfolio(factorId) {
        console.log("移除因子:", factorId)
        
        for (var i = 0; i < portfolioModel.count; i++) {
            if (portfolioModel.get(i).factorId === factorId) {
                portfolioModel.remove(i)
                hasPendingPortfolioEdits = true
                break
            }
        }
        
        // 重新计算权重
        rebalanceWeights()
        updateSimulation([createNotification("info", "已移除组合因子", "查看")])
    }
    
    // 更新因子权重
    function updateFactorWeight(factorId, newWeight) {
        console.log("更新因子权重:", factorId, newWeight)

        if (factorId === undefined || factorId === null || newWeight === undefined || newWeight === null || isNaN(Number(newWeight))) {
            console.warn("忽略非法权重更新:", factorId, newWeight)
            return
        }
        
        for (var i = 0; i < portfolioModel.count; i++) {
            if (portfolioModel.get(i).factorId === factorId) {
                portfolioModel.setProperty(i, "weight", Number(newWeight))
                hasPendingPortfolioEdits = true
                break
            }
        }
        
        // 更新总权重
        updateTotalWeight()
        updateSimulation()
    }
    
    // 更新总权重
    function updateTotalWeight() {
        var sum = 0
        for (var i = 0; i < portfolioModel.count; i++) {
            sum += portfolioModel.get(i).weight
        }
        totalWeight = sum
    }
    
    // 重新平衡权重
    function rebalanceWeights() {
        if (portfolioModel.count === 0) return
        
        var equalWeight = 100.0 / portfolioModel.count
        for (var i = 0; i < portfolioModel.count; i++) {
            portfolioModel.setProperty(i, "weight", equalWeight)
        }
        
        updateTotalWeight()
        updateSimulation()
    }
    
    // 重置权重
    function resetWeights() {
        console.log("重置权重")
        hasPendingPortfolioEdits = true
        rebalanceWeights()
    }
    
    // 更新组合状态
    function updateSimulation(notificationOverrides) {
        console.log("更新组合状态")
        rebuildPortfolioState(notificationOverrides)
    }
    
    // 获取因子颜色
    function getFactorColor(category) {
        switch (category) {
            case "动量类": return "#3B82F6"
            case "价值类": return "#F59E0B"
            case "质量类": return "#10B981"
            case "情绪类": return "#8B5CF6"
            case "流动性类": return "#06B6D4"
            default: return "#94A3B8"
        }
    }
    
    // 搜索因子
    function searchFactors() {
        console.log("刷新真实因子池")
        refreshFactorSources([createNotification("info", "已刷新真实因子池", "查看")])
    }

    function refreshFactorSources(notificationOverrides) {
        if (!factorService || !factorService.getAllFactors) {
            updateSimulation([createNotification("warning", "FactorService 未连接，无法加载真实因子", "检查")])
            return
        }

        var factors = factorService.getAllFactors() || []
        syncFactorModels(factors, notificationOverrides)
    }

    function syncFactorModels(factors, notificationOverrides) {
        factorPoolModel.clear()
        commonFactorsModel.clear()

        var normalizedFactors = []
        for (var i = 0; i < factors.length; i++) {
            var normalized = normalizeFactorRecord(factors[i], i)
            if (normalized.factorId) {
                normalizedFactors.push(normalized)
                factorPoolModel.append(normalized)
            }
        }

        normalizedFactors.sort(function(left, right) {
            return factorScore(right) - factorScore(left)
        })

        for (var j = 0; j < Math.min(6, normalizedFactors.length); j++) {
            commonFactorsModel.append({
                factorId: normalizedFactors[j].factorId,
                displayName: normalizedFactors[j].displayName,
                frequency: Math.max(1, Math.round(factorScore(normalizedFactors[j]) * 10))
            })
        }

        if (portfolioModel.count === 0 && isDefaultPortfolioDraft()) {
            lastRestoredPortfolioId = ""
            for (var k = 0; k < Math.min(initialPortfolioSize, normalizedFactors.length); k++) {
                portfolioModel.append({
                    factorId: normalizedFactors[k].factorId,
                    displayName: normalizedFactors[k].displayName,
                    weight: 0,
                    correlation: normalizedFactors[k].correlation,
                    icValue: normalizedFactors[k].icValue,
                    irValue: normalizedFactors[k].irValue,
                    turnoverRate: normalizedFactors[k].turnoverRate,
                    category: normalizedFactors[k].category,
                    color: getFactorColor(normalizedFactors[k].category)
                })
            }
            rebalanceWeights()
        } else {
            refreshPortfolioMeta()
        }

        updateTotalWeight()
        updateSimulation(notificationOverrides && notificationOverrides.length !== undefined
            ? notificationOverrides
            : [createNotification(
                normalizedFactors.length > 0 ? "success" : "warning",
                normalizedFactors.length > 0
                    ? "已加载 " + normalizedFactors.length + " 个真实因子"
                    : "未读取到真实因子数据",
                normalizedFactors.length > 0 ? "查看" : "刷新")])
    }

    function refreshPortfolioMeta() {
        for (var i = 0; i < portfolioModel.count; i++) {
            var portfolioFactorId = portfolioModel.get(i).factorId
            var portfolioDisplayName = String(portfolioModel.get(i).displayName || "")
            if (!String(portfolioFactorId || "").trim()) {
                var matchedFactor = findFactorInPoolByText(portfolioDisplayName)
                if (matchedFactor && matchedFactor.factorId) {
                    portfolioFactorId = matchedFactor.factorId
                    portfolioModel.setProperty(i, "factorId", matchedFactor.factorId)
                }
            }
            for (var j = 0; j < factorPoolModel.count; j++) {
                var candidate = factorPoolModel.get(j)
                if (candidate.factorId === portfolioFactorId) {
                    portfolioModel.setProperty(i, "displayName", candidate.displayName)
                    portfolioModel.setProperty(i, "correlation", candidate.correlation)
                    portfolioModel.setProperty(i, "icValue", candidate.icValue)
                    portfolioModel.setProperty(i, "irValue", candidate.irValue)
                    portfolioModel.setProperty(i, "turnoverRate", candidate.turnoverRate)
                    portfolioModel.setProperty(i, "category", candidate.category)
                    portfolioModel.setProperty(i, "color", getFactorColor(candidate.category))
                    break
                }
            }
        }
    }

    function normalizeFactorRecord(rawFactor, index) {
        var factor = rawFactor || {}
        var factorId = String(factor.factorId || "")
        var displayName = String(factor.displayName || factor.factorName || factor.name || factorId)
        var category = resolveFactorCategory(factor)
        var icValue = Number(factor.icValue || 0)
        var irValue = Number(factor.irValue || 0)
        var turnoverRate = Number(factor.turnoverRate || 0)

        return {
            factorId: factorId,
            displayName: displayName,
            category: category,
            icValue: icValue,
            irValue: irValue,
            turnoverRate: turnoverRate,
            correlation: estimateFactorCorrelation(factor, index)
        }
    }

    function resolveFactorCategory(factor) {
        var categoryText = String(factor.majorCategory || factor.subCategory || factor.category || "")
        var lowered = categoryText.toLowerCase()
        if (lowered.indexOf("动量") >= 0 || lowered.indexOf("momentum") >= 0) return "动量类"
        if (lowered.indexOf("价值") >= 0 || lowered.indexOf("value") >= 0) return "价值类"
        if (lowered.indexOf("质量") >= 0 || lowered.indexOf("quality") >= 0) return "质量类"
        if (lowered.indexOf("情绪") >= 0 || lowered.indexOf("sentiment") >= 0) return "情绪类"
        if (lowered.indexOf("流动") >= 0 || lowered.indexOf("liquidity") >= 0) return "流动性类"
        return "综合类"
    }

    function estimateFactorCorrelation(factor, index) {
        var groupReturns = factor.groupReturns || []
        if (groupReturns.length >= 2) {
            var spread = Math.abs(Number(groupReturns[0] || 0) - Number(groupReturns[groupReturns.length - 1] || 0))
            return Math.max(0, Math.min(0.95, 0.6 - spread * 0.1))
        }

        var turnoverRate = Number(factor.turnoverRate || 0)
        return Math.max(0.05, Math.min(0.95, 0.15 + (turnoverRate % 40) / 100 + (index % 5) * 0.03))
    }

    function factorScore(factor) {
        return Math.abs(Number(factor.icValue || 0)) * 100
            + Math.abs(Number(factor.irValue || 0)) * 10
            + Math.max(0, 30 - Number(factor.turnoverRate || 0)) * 0.2
    }

    function applyAllocationResult(result) {
        if (!result || !result.success || !result.allocations || result.allocations.length === undefined) {
            return false
        }

        var updatedWeights = {}
        for (var allocationIndex = 0; allocationIndex < result.allocations.length; allocationIndex++) {
            var allocation = result.allocations[allocationIndex] || ({})
            var factorId = String(allocation.factor_id || allocation.factorId || "")
            if (!factorId) {
                continue
            }
            updatedWeights[factorId] = Number(allocation.weight || 0)
        }

        var applied = false
        for (var modelIndex = 0; modelIndex < portfolioModel.count; modelIndex++) {
            var modelFactorId = String(portfolioModel.get(modelIndex).factorId || "")
            if (updatedWeights[modelFactorId] === undefined) {
                continue
            }
            portfolioModel.setProperty(modelIndex, "weight", updatedWeights[modelFactorId])
            applied = true
        }

        if (applied) {
            updateTotalWeight()
        }
        return applied
    }
    
    // 调整行业权重
    function adjustSectorWeight(sector) {
        console.log("调整行业权重:", sector)

        if (!sector) {
            updateSimulation([createNotification("warning", "缺少行业信息，无法调整行业配置", "检查")])
            return
        }

        if (!portfolioAnalysisService || !portfolioAnalysisService.adjustPortfolioExposure) {
            updateSimulation([createNotification("warning", "组合分析服务不可用，无法调整行业配置", "检查")])
            return
        }

        var result = portfolioAnalysisService.adjustPortfolioExposure(
            buildPortfolioStrategyData(portfolioMetrics, portfolioExposures),
            "sector",
            String(sector),
            loadPortfolioOptimizationConfig()) || ({})

        if (applyAllocationResult(result)) {
            updateSimulation([createNotification("success", result.message || ("已调整 " + String(sector) + " 行业配置"), "查看")])
            return
        }

        updateSimulation([createNotification("warning", result.message || ("无法调整 " + String(sector) + " 行业配置"), "检查")])
    }
    
    // 调整风格暴露
    function adjustStyleExposure(style) {
        console.log("调整风格暴露:", style)

        if (!style) {
            updateSimulation([createNotification("warning", "缺少风格信息，无法调整风格暴露", "检查")])
            return
        }

        if (!portfolioAnalysisService || !portfolioAnalysisService.adjustPortfolioExposure) {
            updateSimulation([createNotification("warning", "组合分析服务不可用，无法调整风格暴露", "检查")])
            return
        }

        var result = portfolioAnalysisService.adjustPortfolioExposure(
            buildPortfolioStrategyData(portfolioMetrics, portfolioExposures),
            "style",
            String(style),
            loadPortfolioOptimizationConfig()) || ({})

        if (applyAllocationResult(result)) {
            updateSimulation([createNotification("success", result.message || ("已调整 " + String(style) + " 风格暴露"), "查看")])
            return
        }

        updateSimulation([createNotification("warning", result.message || ("无法调整 " + String(style) + " 风格暴露"), "检查")])
    }
    
    // 运行回测
    function runBacktest() {
        console.log("运行组合回测")

        if (portfolioModel.count === 0) {
            updateSimulation([createNotification("warning", "当前组合为空，无法发起回测", "添加因子")])
            return
        }

        if (!strategyService) {
            updateSimulation([createNotification("warning", "StrategyService 未初始化，无法发起回测", "检查")])
            return
        }

        if (!savePortfolio()) {
            updateSimulation([createNotification("warning", "组合保存失败，无法继续回测", "重试")])
            return
        }

        var strategyData = buildPortfolioStrategyData(portfolioMetrics, portfolioExposures)
        var backtestConfig = {
            source: "portfolio_builder",
            strategy_type: "PORTFOLIO",
            sub_type: "portfolio_builder",
            portfolio_name: portfolioName,
            symbol_pool: strategyData.symbol_pool || strategyData.parameters.symbol_pool || [],
            factor_allocations: strategyData.parameters.allocations,
            backtest_runtime: strategyData.parameters.backtest_runtime || ({}),
            advanced_options: strategyData.advanced_options || ({}),
            estimated_metrics: {
                annual_return: portfolioMetrics.annualReturn,
                sharpe_ratio: portfolioMetrics.sharpeRatio,
                max_drawdown: portfolioMetrics.maxDrawdown
            }
        }

        var runtimeBacktest = backtestConfig.backtest_runtime || ({})
        backtestConfig.positionSizingMethod = runtimeBacktest.positionSizingMethod
        backtestConfig.maxPositionPercent = runtimeBacktest.maxPositionPercent
        backtestConfig.maxTotalExposure = runtimeBacktest.maxTotalExposure
        backtestConfig.rebalanceDays = runtimeBacktest.rebalanceDays
        backtestConfig.minWeightPercent = runtimeBacktest.minWeightPercent
        backtestConfig.maxWeightPercent = runtimeBacktest.maxWeightPercent
        backtestConfig.top_n = runtimeBacktest.top_n
        backtestConfig.maxPositions = runtimeBacktest.maxPositions

        updateSimulation([createNotification("info", "组合已推送到回测页", "查看")])
        requestBacktest(currentPortfolioId, portfolioName, backtestConfig)
    }
    
    // 保存组合
    function savePortfolio() {
        console.log("保存组合")

        if (!strategyService) {
            updateSimulation([createNotification("warning", "StrategyService 未初始化，无法保存组合", "检查")])
            return false
        }

        if (portfolioModel.count === 0) {
            updateSimulation([createNotification("warning", "当前组合为空，无法保存", "添加因子")])
            return false
        }

        refreshPortfolioStateBeforeSave()
        var hasPersistableSymbolPool = canPersistPortfolioSymbolPool()
        var resolvedPoolState = resolvePortfolioSymbolPoolState()

        var portfolioStrategyData = buildPortfolioStrategyData(portfolioMetrics, portfolioExposures)
        var success = false
        var savedStrategyId = currentPortfolioId
        var shouldUpdateExisting = !isDefaultPortfolioDraft()
            && String(currentPortfolioId || "").trim()
            && strategyService.updateStrategy

        if (shouldUpdateExisting) {
            success = strategyService.updateStrategy(currentPortfolioId, portfolioStrategyData)
            if (!success) {
                updateSimulation([createNotification("warning", "当前组合未能更新，已阻止自动新建重复策略", "检查")])
                return false
            }
        } else if (strategyService.createStrategy) {
            savedStrategyId = strategyService.createStrategy(portfolioStrategyData)
            success = !!savedStrategyId
        }

        if (success) {
            if (savedStrategyId) {
                currentPortfolioId = savedStrategyId
            }

            currentStrategyDetail = loadCurrentStrategyDetail()
            hasPendingPortfolioEdits = false
            lastRestoredPortfolioId = String(currentPortfolioId || "").trim()

            var bindingNotifications = []
            if (tradingConnectionConfigService && tradingConnectionConfigService.bindStrategyConfiguration) {
                var bindingResult = tradingConnectionConfigService.bindStrategyConfiguration(
                    String(savedStrategyId || currentPortfolioId || ""),
                    String(portfolioName || ""),
                    true,
                    false) || ({})

                if (bindingResult.success) {
                    var bindingNotificationText = bindingResult.readyForTrading
                        ? (bindingResult.message || "已同步交易绑定到当前组合策略")
                        : StartupGateFormatter.blockedActionMessage(
                            bindingResult.startupGate || ({}),
                            bindingResult.message || "已同步交易绑定，但当前组合策略未通过 StartupGate")
                    bindingNotifications.push(createNotification(
                        bindingResult.readyForTrading ? "success" : "warning",
                        bindingNotificationText,
                        bindingResult.readyForTrading ? "交易" : "检查"))
                } else {
                    var bindingFailureText = StartupGateFormatter.blockedActionMessage(
                        bindingResult.startupGate || ({}),
                        bindingResult.message || "组合已保存，但交易绑定更新失败")
                    bindingNotifications.push(createNotification(
                        "warning",
                        bindingFailureText,
                        "检查"))
                }
            }

            bindingNotifications.unshift(createNotification("success", "组合已保存为策略: " + portfolioName, "查看"))
            bindingNotifications.push(createNotification(
                resolvedPoolState.count > 0 ? "info" : "warning",
                "本次股票池来源: " + resolvedPoolState.sourceLabel + "（" + resolvedPoolState.count + " 只）",
                resolvedPoolState.count > 0 ? "查看" : "检查"))
            if (!hasPersistableSymbolPool) {
                bindingNotifications.push(createNotification(
                    "warning",
                    "组合已保存，但本次未固化股票池；当前来源未命中，建议先刷新组合快照后再保存一次。",
                    "检查"))
            }
            updateSimulation(bindingNotifications)
            return true
        } else {
            updateSimulation([createNotification("warning", "组合保存失败，请检查数据库与策略服务状态", "重试")])
            return false
        }
    }
    
    // 自动优化
    function autoOptimize() {
        console.log("一键优化组合")
        if (portfolioModel.count === 0) {
            updateSimulation([createNotification("warning", "当前组合为空，无法优化", "添加因子")])
            return
        }

        var optimizationConfig = loadPortfolioOptimizationConfig()

        if (portfolioAnalysisService && portfolioAnalysisService.optimizePortfolioAllocations) {
            var optimizeResult = portfolioAnalysisService.optimizePortfolioAllocations(
                buildPortfolioStrategyData(portfolioMetrics, portfolioExposures),
                optimizationConfig || ({}) ) || ({})

            if (applyAllocationResult(optimizeResult)) {
                updateSimulation([createNotification("success", optimizeResult.message || "已完成组合优化", "查看")])
                return
            }
        }

        rebalanceWeights()
        updateSimulation([createNotification("warning", "组合优化服务不可用，已回退为等权重分配", "检查")])
    }
    
    // 风险检查
    function riskCheck() {
        console.log("风险检查")

        if (!portfolioAnalysisService || !portfolioAnalysisService.checkPortfolioRisk) {
            updateSimulation([createNotification("warning", "组合分析服务不可用，已刷新当前组合状态", "检查")])
            return
        }

        var riskResult = portfolioAnalysisService.checkPortfolioRisk(
            buildPortfolioStrategyData(portfolioMetrics, portfolioExposures),
            getLatestBacktest(currentStrategyDetail) || ({})) || ({})

        var snapshot = riskResult.snapshot || ({})
        var primaryMessage = snapshot && String(snapshot.status || "") === "error" && String(snapshot.error || "").trim()
            ? String(snapshot.error || "").trim()
            : (riskResult.message || "组合风险检查已完成")

        var primaryNotifications = [createNotification(
            riskResult.success ? "success" : "warning",
            primaryMessage,
            "查看")]
        var detailNotifications = riskResult.notifications && riskResult.notifications.length !== undefined
            ? mergeNotifications(primaryNotifications, riskResult.notifications)
            : primaryNotifications

        updateSimulation(detailNotifications)
    }

    function previewExecutionPlan() {
        console.log("预览组合调仓计划")

        if (portfolioModel.count === 0) {
            executionPlanState = {
                success: false,
                message: "当前组合为空，无法生成调仓计划",
                orders: [],
                batches: [],
                skippedOrders: [],
                summary: {},
                generatedAt: ""
            }
            executionPlanFingerprint = ""
            updateSimulation([createNotification("warning", "当前组合为空，无法生成调仓计划", "添加因子")])
            return
        }

        if (!portfolioAnalysisService || !portfolioAnalysisService.buildPortfolioExecutionPlan) {
            executionPlanState = {
                success: false,
                message: "组合分析服务不可用，无法生成调仓计划",
                orders: [],
                batches: [],
                skippedOrders: [],
                summary: {},
                generatedAt: ""
            }
            executionPlanFingerprint = ""
            updateSimulation([createNotification("warning", "组合分析服务不可用，无法生成调仓计划", "检查")])
            return
        }

        var result = portfolioAnalysisService.buildPortfolioExecutionPlan(
            buildPortfolioStrategyData(portfolioMetrics, portfolioExposures),
            getLatestBacktest(currentStrategyDetail) || ({})) || ({})

        executionPlanState = {
            success: !!result.success,
            message: String(result.message || (result.success ? "调仓计划已生成" : "调仓计划生成失败")),
            orders: result.orders && result.orders.length !== undefined ? result.orders : [],
            batches: result.batches && result.batches.length !== undefined ? result.batches : [],
            skippedOrders: result.skippedOrders && result.skippedOrders.length !== undefined ? result.skippedOrders : [],
            summary: result.summary || ({}),
            generatedAt: String(result.generatedAt || Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss"))
        }
        executionPlanFingerprint = currentExecutionPlanFingerprint

        var orderCount = executionPlanState.orders.length
        var batchCount = executionPlanState.batches.length
        var notificationMessage = result.success
            ? (orderCount > 0
                ? ("已生成调仓计划，共 " + orderCount + " 笔委托 / " + batchCount + " 个执行批次")
                : (executionPlanState.message || "当前无需调仓"))
            : (executionPlanState.message || "调仓计划生成失败")
        updateSimulation([createNotification(result.success ? "success" : "warning", notificationMessage, "查看")])
    }

    function buildPortfolioStrategyData(metricsOverride, exposuresOverride) {
        var factorAllocations = buildPortfolioFactorAllocations()
        var metrics = metricsOverride || portfolioMetrics
        var exposures = exposuresOverride || portfolioExposures
        var optimizationConfig = loadPortfolioOptimizationConfig()
        var symbolPool = resolvePortfolioSymbolPool()
        var parameters = {
            portfolio_name: portfolioName,
            total_weight: totalWeight,
            factor_count: portfolioModel.count,
            allocations: factorAllocations,
            factor_allocations: factorAllocations,
            portfolio_allocations_json: "",
            positionSizingMethod: optimizationConfig.positionSizingMethod,
            position_sizing_method: optimizationConfig.positionSizingMethod,
            maxPositionPercent: optimizationConfig.maxPositionPercent,
            maxSinglePositionRatio: optimizationConfig.maxSinglePositionRatio,
            positionPercent: optimizationConfig.maxPositionPercent,
            position_size: optimizationConfig.maxPositionPercent,
            positionSize: optimizationConfig.maxPositionPercent,
            maxWeightPercent: optimizationConfig.maxWeightPercent,
            max_weight_percent: optimizationConfig.maxWeightPercent,
            minWeightPercent: optimizationConfig.minWeightPercent,
            min_weight_percent: optimizationConfig.minWeightPercent,
            maxTotalExposure: optimizationConfig.maxTotalExposure,
            maxPositionRatio: optimizationConfig.maxTotalExposure,
            rebalanceDays: optimizationConfig.rebalanceDays,
            rebalance_days: optimizationConfig.rebalanceDays,
            rebalanceFrequency: optimizationConfig.rebalanceFrequency,
            top_n: optimizationConfig.top_n,
            maxPositions: optimizationConfig.maxPositions,
            backtest_runtime: {
                positionSizingMethod: optimizationConfig.positionSizingMethod,
                maxPositionPercent: optimizationConfig.maxPositionPercent,
                maxTotalExposure: optimizationConfig.maxTotalExposure,
                rebalanceDays: optimizationConfig.rebalanceDays,
                minWeightPercent: optimizationConfig.minWeightPercent,
                maxWeightPercent: optimizationConfig.maxWeightPercent,
                top_n: optimizationConfig.top_n,
                maxPositions: optimizationConfig.maxPositions
            },
            estimated_metrics: {
                annual_return: metrics.annualReturn,
                sharpe_ratio: metrics.sharpeRatio,
                max_drawdown: metrics.maxDrawdown
            },
            exposures: {
                sector: exposures.sector,
                style: exposures.style
            }
        }

        parameters.portfolio_allocations_json = JSON.stringify(factorAllocations)
        if (symbolPool.length > 0) {
            parameters.symbol_pool = symbolPool
        }

        var strategyData = {
            strategy_name: portfolioName,
            strategy_type: "PORTFOLIO",
            description: "组合构建页保存的多因子组合策略",
            asset_type: "stock",
            time_frame: "daily",
            allocations: factorAllocations,
            factor_allocations: factorAllocations,
            portfolio_allocations_json: JSON.stringify(factorAllocations),
            risk_level: metrics.maxDrawdown > 20 ? "high" : (metrics.maxDrawdown > 12 ? "medium" : "low"),
            optimization_method: "portfolio_builder",
            advanced_options: {
                source: "PortfolioBuilderPage",
                saved_at: new Date().toISOString(),
                optimization_config: optimizationConfig
            },
            parameters: parameters,
            sub_type: "portfolio_builder",
            status: "DRAFT",
            version: "1.0",
            language: "Python",
            author: "PortfolioBuilder",
            tags: ["组合策略", "多因子", "PortfolioBuilder"]
        }

        if (symbolPool.length > 0) {
            strategyData.symbol_pool = symbolPool
        }

        return strategyData
    }
    
    // 处理通知操作
    function handleNotificationAction(index) {
        var notificationData = visibleNotifications[index] || ({})
        var action = String(notificationData.action || "查看").trim()
        var notificationText = String(notificationData.text || "")

        console.log("处理通知:", index, notificationData)

        if (!notificationText) {
            infoPanelExpanded = true
            return
        }

        rememberDismissedNotifications([notificationData])

        if (action === "刷新") {
            clearDismissedNotifications()
            leftPanelExpanded = true
            refreshFactorSources([createNotification("success", "已刷新真实因子池", "查看")])
            return
        }

        if (action === "添加因子") {
            clearDismissedNotifications()
            leftPanelExpanded = true
            infoPanelExpanded = false
            snapshotDetailExpanded = false
            updateSimulation([createNotification("info", "已展开因子池，可继续添加组合因子", "查看")])
            return
        }

        if (action === "重置") {
            clearDismissedNotifications()
            resetWeights()
            return
        }

        if (action === "保存") {
            clearDismissedNotifications()
            savePortfolio()
            return
        }

        if (action === "交易") {
            clearDismissedNotifications()
            if ((!currentPortfolioId || isDefaultPortfolioDraft()) && !savePortfolio()) {
                return
            }

            requestNavigation("trade_execution", "交易执行", {
                source: "portfolio_builder",
                strategyId: currentPortfolioId,
                strategyName: portfolioName,
                strategy: currentStrategyDetail && Object.keys(currentStrategyDetail).length > 0
                    ? currentStrategyDetail
                    : buildPortfolioStrategyData(portfolioMetrics, portfolioExposures)
            })
            return
        }

        if (action === "检查") {
            clearDismissedNotifications()
            infoPanelExpanded = true
            if (portfolioAnalysisService && portfolioAnalysisService.checkPortfolioRisk && portfolioModel.count > 0) {
                riskCheck()
            } else {
                updateSimulation([createNotification("info", "已展开组合诊断面板", "查看")])
            }
            return
        }

        infoPanelExpanded = true
        if (filteredSnapshotPositions.length > 0) {
            selectedSnapshotIndex = 0
            snapshotDetailExpanded = true
        }

        if (notificationText.indexOf("风险") >= 0 || notificationText.indexOf("快照") >= 0) {
            openRiskManagementPage()
            return
        }

        updateSimulation([createNotification("info", "已展开组合详情面板", "查看")])
    }
    
    // 静音通知
    function muteNotifications() {
        notificationsMuted = !notificationsMuted
        if (!notificationsMuted) {
            clearDismissedNotifications()
        }
    }
    
    // 清理通知
    function clearNotifications() {
        rememberDismissedNotifications(visibleNotifications)
    }
    
    // ============ 初始化 ============

    function ensurePortfolioBuilderServicesReady() {
        if (strategyService && strategyService.initialize) {
            strategyService.initialize()
        }
        if (factorService && factorService.initialize) {
            factorService.initialize()
        }
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }

        ensurePortfolioBuilderServicesReady()
        console.log("组合构建页面显示，准备同步绑定策略上下文", currentPortfolioId, lastRestoredPortfolioId)
        if (!syncBoundPortfolioContextIfNeeded(false) && shouldRestoreCurrentPortfolio()) {
            loadSavedPortfolio()
        }
    }
    
    Component.onCompleted: {
        console.log("组合构建页面初始化完成")
        console.log("当前组合:", portfolioName)
        console.log("因子数量:", portfolioModel.count)

        if (visible) {
            ensurePortfolioBuilderServicesReady()
        }

        if (!syncBoundPortfolioContextIfNeeded(true)) {
            loadSavedPortfolio()
        }
        refreshFactorSources()
        updateTotalWeight()
        updateSimulation()
    }
}
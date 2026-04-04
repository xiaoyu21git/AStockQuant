// pages/StrategyLibraryPage.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtCharts 2.15
import AStock.Bridge 1.0  // 导入C++桥接模块
import "../../components/Strategy" as StrategyComponents
import "../../components/Base" as BaseComponents
import "../../components" as Components
import "../../utils/StrategyDataAdapter.js" as StrategyAdapter

Rectangle {
    id: strategyLibraryPage
    color: "#0F172A"  // primaryBg
    
    // 属性
    property int selectedStrategyIndex: 0
    property string selectedStrategyId: ""
    property bool showFilter: false
    property bool showSorter: false
    property int runningStrategyIndex: 0
    property bool serviceSignalsBound: false
    property bool deleteInProgress: false
    
    // 信号
    signal createNewStrategy()
    signal strategySelected(string strategyName)
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color primaryBg: "#0F172A"
    readonly property color secondaryBg: "#1E293B"
    readonly property color tertiaryBg: "#334155"
    readonly property color accentBlue: "#3B82F6"
    readonly property color borderColor: "#475569"
    readonly property color warningAmber: "#F59E0B"
    readonly property color successGreen: "#10B981"
    readonly property color riseRed: "#EF4444"
    readonly property color fallGreen: "#10B981"
    
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeXLarge: 24
    
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    readonly property real spacingXLarge: 24
    
    readonly property real borderRadiusMedium: 8
    readonly property real borderRadiusXLarge: 16
    
    // C++服务引用
    property var strategyService: StrategyService
    property var strategyViewModel: null
    
    // 初始化策略服务 - 确保数据自动加载
    function initializeStrategyViewModel() {
        // 获取StrategyService单例
        strategyService = StrategyService
        if (strategyService) {
            // 获取视图模型 - 先获取，以便绑定到UI
            strategyViewModel = strategyService.getViewModel()

            if (!serviceSignalsBound) {
                serviceSignalsBound = true

                strategyService.initializedChanged.connect(function() {
                    if (strategyViewModel && strategyViewModel.count === 0) {
                        selectedStrategyIndex = -1
                    }
                })

                strategyService.cacheLoadedChanged.connect(function() {
                })

                strategyService.strategiesLoaded.connect(function(strategies) {
                    console.log("策略加载完成信号，数量:", strategies.length)
                })

                strategyService.dataChanged.connect(function() {
                    console.log("策略数据已变更，ViewModel会自动更新")
                    syncSelectedStrategy()
                })

                strategyService.strategyCreated.connect(function(strategyId, strategyData) {
                    console.log("新策略创建成功，ID:", strategyId, "名称:", strategyData.strategy_name)
                })
            }
            
            console.log("策略服务初始化完成，视图模型已绑定")
        } else {
            console.error("无法获取StrategyService实例")
        }
    }
    
    // 包装器函数：获取策略数量
    function getStrategyCount() {
        if (strategyViewModel) {
            return strategyViewModel.count
        }
        return 0
    }
    
    // 包装器函数：获取策略数据
    function getStrategyData(index) {
        if (strategyViewModel && index >= 0 && index < strategyViewModel.count) {
            return strategyViewModel.getRow(index)
        }
        return null
    }
    
    // 包装器函数：获取运行策略数量
    function getRunningStrategyCount() {
        var count = 0
        if (strategyViewModel) {
            for (var i = 0; i < strategyViewModel.count; i++) {
                var strategy = strategyViewModel.getRow(i)
                if (strategy && (strategy.status === "running" || strategy.status === "ACTIVE")) {
                    count++
                }
            }
        }
        return count
    }
    
    // 包装器函数：获取指定索引的运行策略
    function getRunningStrategy(runningIndex) {
        var runningCount = 0
        if (strategyViewModel) {
            for (var i = 0; i < strategyViewModel.count; i++) {
                var strategy = strategyViewModel.getRow(i)
                if (strategy && (strategy.status === "running" || strategy.status === "ACTIVE")) {
                    if (runningCount === runningIndex) {
                        return strategy
                    }
                    runningCount++
                }
            }
        }
        return null
    }

    function requestDeleteStrategy(strategyId, strategyName) {
        if (!strategyId || deleteInProgress) {
            console.warn("删除策略失败：缺少策略ID")
            return
        }

        deleteConfirmDialog.strategyId = strategyId
        deleteConfirmDialog.strategyName = strategyName || "未命名策略"
        deleteConfirmDialog.open()
    }

    function getSelectedStrategySummary() {
        if (strategyViewModel && selectedStrategyId) {
            for (var index = 0; index < strategyViewModel.count; ++index) {
                var row = strategyViewModel.getRow(index)
                var rowId = row ? (row.strategyId || row.id || "") : ""
                if (rowId === selectedStrategyId) {
                    return row
                }
            }
        }

        if (selectedStrategyIndex >= 0 && strategyViewModel && strategyViewModel.count > selectedStrategyIndex) {
            return strategyViewModel.getRow(selectedStrategyIndex)
        }
        return null
    }

    function selectStrategyAt(index) {
        if (!strategyViewModel || index < 0 || index >= strategyViewModel.count) {
            selectedStrategyIndex = -1
            selectedStrategyId = ""
            return
        }

        var selectedRow = strategyViewModel.getRow(index)
        selectedStrategyIndex = index
        selectedStrategyId = selectedRow ? (selectedRow.strategyId || selectedRow.id || "") : ""
        strategySelected(selectedRow ? (selectedRow.strategyName || selectedRow.name || "") : "")
    }

    function syncSelectedStrategy() {
        if (!strategyViewModel || strategyViewModel.count === 0) {
            selectedStrategyIndex = -1
            selectedStrategyId = ""
            return
        }

        if (selectedStrategyId) {
            for (var index = 0; index < strategyViewModel.count; ++index) {
                var row = strategyViewModel.getRow(index)
                var rowId = row ? (row.strategyId || row.id || "") : ""
                if (rowId === selectedStrategyId) {
                    selectedStrategyIndex = index
                    return
                }
            }
        }

        if (selectedStrategyIndex >= 0 && selectedStrategyIndex < strategyViewModel.count) {
            var currentRow = strategyViewModel.getRow(selectedStrategyIndex)
            selectedStrategyId = currentRow ? (currentRow.strategyId || currentRow.id || "") : ""
            return
        }

        selectStrategyAt(0)
    }

    function getSelectedStrategyDetail(strategyId) {
        if (!strategyService || !strategyId || !strategyService.getStrategyById) {
            return ({})
        }

        return strategyService.getStrategyById(strategyId) || ({})
    }

    function resolveStrategyForEdit(strategyCandidate) {
        var candidate = strategyCandidate || ({})
        var strategyId = ""

        if (typeof candidate === "string") {
            strategyId = candidate
        } else {
            strategyId = candidate.strategy_id || candidate.strategyId || candidate.id || ""
        }

        if (strategyId && strategyService && strategyService.getStrategyById) {
            var detail = strategyService.getStrategyById(strategyId) || ({})
            if (detail && Object.keys(detail).length > 0) {
                return detail
            }
        }

        return typeof candidate === "object" ? candidate : ({})
    }

    function openStrategyCreation(strategyDetail) {
        var resolvedStrategy = resolveStrategyForEdit(strategyDetail)
        strategyCreationLoader.pendingStrategyData = resolvedStrategy || ({})
        strategyCreationLoader.active = true

        if (strategyCreationLoader.item) {
            if (resolvedStrategy && Object.keys(resolvedStrategy).length > 0 && strategyCreationLoader.item.loadStrategyForEdit) {
                strategyCreationLoader.item.loadStrategyForEdit(resolvedStrategy)
            } else if (strategyCreationLoader.item.resetForm) {
                strategyCreationLoader.item.resetForm()
            }
        }
    }

    function getStrategyPerformanceMetrics(strategyDetail) {
        if (!strategyDetail) {
            return ({})
        }

        return strategyDetail.performance_metrics || strategyDetail.performanceMetrics || ({})
    }

    function getLatestBacktestRecord(strategyDetail) {
        var performance = getStrategyPerformanceMetrics(strategyDetail)
        return performance.latestBacktest || performance.latest_backtest || ({})
    }

    function getBacktestHistory(strategyDetail) {
        var performance = getStrategyPerformanceMetrics(strategyDetail)
        return performance.backtestHistory || performance.backtest_history || []
    }

    function formatBacktestPercentValue(value, decimals) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return number.toFixed(decimals === undefined ? 2 : decimals) + "%"
    }

    function formatBacktestNumberValue(value, decimals) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return number.toFixed(decimals === undefined ? 2 : decimals)
    }

    function formatBacktestIntegerValue(value) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return Math.round(number).toString()
    }

    function buildLatestBacktestItems(strategyDetail) {
        var latest = getLatestBacktestRecord(strategyDetail)
        var summary = latest.summary || ({})
        if (!latest || Object.keys(latest).length === 0) {
            return []
        }

        return [
            { label: "回测时间", value: latest.recordedAt || "--" },
            { label: "股票池", value: latest.universeLabel || latest.universeType || "--" },
            { label: "指数", value: latest.indexLabel || latest.indexSymbol || "--" },
            { label: "数据源", value: latest.dataSourceMode || "--" },
            { label: "区间", value: (latest.startDate || "--") + " ~ " + (latest.endDate || "--") },
            { label: "总收益", value: formatBacktestPercentValue(summary.returns, 2) },
            { label: "最大回撤", value: formatBacktestPercentValue(summary.maxDrawdown, 2) },
            { label: "夏普比率", value: formatBacktestNumberValue(summary.sharpeRatio, 2) },
            { label: "胜率", value: formatBacktestPercentValue(summary.winRate, 2) },
            { label: "交易次数", value: formatBacktestIntegerValue(summary.tradesCount) },
            { label: "运行天数", value: formatBacktestIntegerValue(summary.runningDays) },
            { label: "净值点数", value: formatBacktestIntegerValue(latest.equityPointCount) }
        ]
    }

    function calculateMetricAxisBounds(history, metricKey, fallbackMin, fallbackMax) {
        if (!history || history.length === 0) {
            return { min: fallbackMin, max: fallbackMax }
        }

        var minValue = 0
        var maxValue = 0
        var initialized = false
        for (var index = 0; index < history.length; ++index) {
            var summary = history[index] && history[index].summary ? history[index].summary : ({})
            var currentValue = Number(summary[metricKey])
            if (isNaN(currentValue)) {
                continue
            }

            if (!initialized) {
                minValue = currentValue
                maxValue = currentValue
                initialized = true
            } else {
                minValue = Math.min(minValue, currentValue)
                maxValue = Math.max(maxValue, currentValue)
            }
        }

        if (!initialized) {
            return { min: fallbackMin, max: fallbackMax }
        }

        if (minValue === maxValue) {
            var singlePadding = Math.max(Math.abs(minValue) * 0.08, metricKey === "sharpeRatio" ? 0.2 : 1)
            return { min: minValue - singlePadding, max: maxValue + singlePadding }
        }

        var padding = (maxValue - minValue) * 0.1
        return { min: minValue - padding, max: maxValue + padding }
    }

    function updateHistoryMetricSeries(series, history, metricKey) {
        if (!series) {
            return
        }

        series.clear()
        if (!history) {
            return
        }

        for (var index = 0; index < history.length; ++index) {
            var summary = history[index] && history[index].summary ? history[index].summary : ({})
            var currentValue = Number(summary[metricKey])
            if (!isNaN(currentValue)) {
                series.append(index, currentValue)
            }
        }
    }
    
    // 数据模型（完全使用数据库数据，移除模拟数据）
    ListModel {
        id: strategyModel
        // 不再使用硬编码数据，完全依赖数据库
    }
    
    // 定时器 - 用于自动滚动
    Timer {
        id: autoScrollTimer
        interval: 3000  // 3秒切换一次
        running: true
        repeat: true
        onTriggered: {
            var runningCount = 0;
            
            // 只使用数据库数据
            if (strategyViewModel && strategyViewModel.count > 0) {
                for (var i = 0; i < strategyViewModel.count; i++) {
                    var strategy = strategyViewModel.getRow(i);
                    if (strategy && (strategy.status === "running" || strategy.status === "ACTIVE")) {
                        runningCount++;
                    }
                }
            }
            
            if (runningCount > 1) {
                runningStrategyIndex++;
                if (runningStrategyIndex >= runningCount) {
                    runningStrategyIndex = 0;
                }
            }
        }
    }
    
    // 主布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 头部区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            color: secondaryBg
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: spacingXLarge
                
                // 标题
                ColumnLayout {
                    spacing: spacingMedium
                    
                    Text {
                        text: "量化策略库"
                        font.pixelSize: fontSizeXLarge
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }
                    
                    Text {
                        text: "管理您的量化交易策略，监控实时运行状态"
                        font.pixelSize: fontSizeNormal
                        color: textTertiary
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // 操作按钮组
                Row {
                    spacing: spacingLarge
                    
                    // 筛选按钮
                    StrategyComponents.StrategyFilterButton {
                        onClicked: showFilter = !showFilter
                    }
                    
                    // 排序按钮
                    StrategyComponents.StrategySortButton {
                        onClicked: showSorter = !showSorter
                    }
                    
                    // 新建策略按钮
                    StrategyComponents.CreateStrategyButton {
                        onClicked: {
                            strategyLibraryPage.openStrategyCreation({});
                        }
                    }
                }
            }
        }
        
        // 主要内容区域
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff  // 隐藏垂直滚动条
            
            ColumnLayout {
                width: scrollView.width - 10
                spacing: spacingXLarge
                
                // 列表头部信息
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    
                    Text {
                        text: "显示 " + (strategyViewModel ? strategyViewModel.count : strategyModel.count) + " 个策略 (数据库: " + (strategyViewModel ? strategyViewModel.count : 0) + ")"
                        font.pixelSize: fontSizeNormal
                        color: textSecondary
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 视图切换按钮
                    StrategyComponents.ViewModeToggle {
                        currentMode: "grid"
                        onModeChanged: {
                            // 视图模式切换逻辑
                        }
                    }
                }
                
                // 策略列表 - 卡片形式
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 420
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    radius: borderRadiusXLarge
                    color: secondaryBg
                    border.color: borderColor
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: spacingMedium
                        
                        Text {
                            text: "策略列表"
                            font.pixelSize: fontSizeLarge
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }
                        
                            // 策略卡片网格 - 使用统一的StrategyCard组件
                            GridView {
                                id: strategyGridView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                
                                // 模型绑定到StrategyViewModel
                                model: strategyViewModel
                                
                                // 2列布局，使用更大的卡片高度
                                cellWidth: (width - 30) / 2
                                cellHeight: 280  // 统一卡片高度+间距
                                
                                // 隐藏滚动条
                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AlwaysOff
                                }
                                
                                delegate: Components.StrategyCard {
                                    width: strategyGridView.cellWidth - 12
                                    height: strategyGridView.cellHeight - 20
                                    
                                    // 策略基本属性
                                    strategyId: model.strategyId || model.id || ""
                                    strategyName: model.strategyName || model.name || "未命名策略"
                                    displayName: model.strategyName || model.name || "未命名策略"
                                    strategyType: model.strategyType || "趋势策略"
                                    description: model.description || "暂无描述"
                                    status: model.status || "STOPPED"
                                    
                                    // 性能指标
                                    returns: parseFloat(model.returns) || 0.0
                                    sharpeRatio: parseFloat(model.sharpeRatio) || 0.0
                                    maxDrawdown: parseFloat(model.maxDrawdown) || 0.0
                                    winRate: parseFloat(model.winRate) || 0.0
                                    
                                    // 实时状态
                                    runningDays: model.runningDays || 0
                                    tradesCount: model.tradesCount || 0
                                    dailyPnL: parseFloat(model.dailyPnL) || 0
                                    position: parseFloat(model.position) || 0
                                    
                                    // 布局设置
                                    selected: strategyLibraryPage.selectedStrategyId !== ""
                                        ? strategyLibraryPage.selectedStrategyId === (model.strategyId || model.id || "")
                                        : strategyLibraryPage.selectedStrategyIndex === index
                                    showMiniChart: true
                                    showParameterPanel: false  // 列表视图不显示参数面板
                                    cardWidth: strategyGridView.cellWidth - 12
                                    cardHeight: 260
                                    
                                    // 确保颜色正确
                                    Component.onCompleted: {
                                        // 如果数据适配器可用，使用统一的颜色映射
                                        if (typeof StrategyAdapter !== 'undefined') {
                                            categoryColor = StrategyAdapter.getStrategyTypeColor(strategyType)
                                        }
                                    }
                                    
                                    enableCardClick: true

                                    onClicked: {
                                        strategyLibraryPage.selectStrategyAt(index)
                                    }

                                    onEntitySelected: function(entityId) {
                                        strategyLibraryPage.selectStrategyAt(index)
                                    }
                                    
                                    onStartClicked: {
                                        console.log("启动策略:", model.strategyId || model.id)
                                        if (strategyService && (model.strategyId || model.id)) {
                                            strategyService.activateStrategy(model.strategyId || model.id)
                                        }
                                    }
                                    
                                    onStopClicked: {
                                        console.log("停止策略:", model.strategyId || model.id)
                                        if (strategyService && (model.strategyId || model.id)) {
                                            strategyService.deactivateStrategy(model.strategyId || model.id)
                                        }
                                    }
                                    
                                    onOptimizeClicked: {
                                        console.log("优化策略:", model.strategyId || model.id)
                                        // TODO: 实现策略优化功能
                                    }

                                    onDeleteClicked: {
                                        strategyLibraryPage.requestDeleteStrategy(
                                            model.strategyId || model.id || "",
                                            model.strategyName || model.name || "未命名策略"
                                        )
                                    }
                                }
                            }
                    }
                }
                
                // 策略详细区域 - 使用统一的StrategyCard（集成策略控制功能）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1120
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    radius: borderRadiusXLarge
                    color: secondaryBg
                    border.color: borderColor
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: spacingMedium
                        
                        Text {
                            text: "策略详情与控制"
                            font.pixelSize: fontSizeLarge
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }
                        
                        // 当前选择的策略卡片
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 320
                            radius: borderRadiusMedium
                            color: Qt.rgba(59/255, 130/255, 246/255, 0.05)
                            border.color: Qt.rgba(59/255, 130/255, 246/255, 0.3)
                            border.width: 1
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8
                                
                                // 空状态
                                Text {
                                    text: "请从上方策略列表中选择一个策略"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                                    visible: selectedStrategyIndex < 0
                                }
                                
                        // 已选择策略的详细信息 - 使用统一的StrategyCard
                        Components.StrategyCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: selectedStrategyIndex >= 0 && strategyViewModel && strategyViewModel.count > selectedStrategyIndex
                            
                            property var selectedStrategy: {
                                if (selectedStrategyIndex >= 0 && strategyViewModel && strategyViewModel.count > selectedStrategyIndex) {
                                    return strategyViewModel.getRow(selectedStrategyIndex);
                                }
                                return null;
                            }
                            
                            // 策略基本属性
                            strategyId: selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id || "") : ""
                            strategyName: selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : ""
                            displayName: selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : ""
                            strategyType: selectedStrategy ? (selectedStrategy.strategyType || "趋势策略") : "趋势策略"
                            description: selectedStrategy ? (selectedStrategy.description || "暂无描述") : "暂无描述"
                            status: selectedStrategy ? (selectedStrategy.status || "STOPPED") : "STOPPED"
                            
                            // 性能指标
                            returns: selectedStrategy ? parseFloat(selectedStrategy.returns) || 0.0 : 0.0
                            sharpeRatio: selectedStrategy ? parseFloat(selectedStrategy.sharpeRatio) || 0.0 : 0.0
                            maxDrawdown: selectedStrategy ? parseFloat(selectedStrategy.maxDrawdown) || 0.0 : 0.0
                            winRate: selectedStrategy ? parseFloat(selectedStrategy.winRate) || 0.0 : 0.0
                            
                            // 实时状态
                            runningDays: selectedStrategy ? (selectedStrategy.runningDays || 0) : 0
                            tradesCount: selectedStrategy ? (selectedStrategy.tradesCount || 0) : 0
                            dailyPnL: selectedStrategy ? parseFloat(selectedStrategy.dailyPnL) || 0 : 0
                            position: selectedStrategy ? parseFloat(selectedStrategy.position) || 0 : 0
                            
                            // 布局设置 - 详细视图显示更多信息
                            selected: true
                            showMiniChart: true
                            showParameterPanel: true  // 详细视图显示参数面板
                            cardWidth: parent.width - 32  // 减去边距
                            cardHeight: parent.height - 32
                            
                            // 确保颜色正确
                            Component.onCompleted: {
                                // 如果数据适配器可用，使用统一的颜色映射
                                if (typeof StrategyAdapter !== 'undefined' && selectedStrategy) {
                                    var strategyType = selectedStrategy.strategyType || "趋势策略"
                                    categoryColor = StrategyAdapter.getStrategyTypeColor(strategyType)
                                }
                            }
                            
                            // 信号连接
                            onStartClicked: {
                                console.log("启动策略:", selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id) : "")
                                if (strategyService && selectedStrategy && (selectedStrategy.strategyId || selectedStrategy.id)) {
                                    strategyService.activateStrategy(selectedStrategy.strategyId || selectedStrategy.id)
                                }
                            }
                            
                            onStopClicked: {
                                console.log("停止策略:", selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id) : "")
                                if (strategyService && selectedStrategy && (selectedStrategy.strategyId || selectedStrategy.id)) {
                                    strategyService.deactivateStrategy(selectedStrategy.strategyId || selectedStrategy.id)
                                }
                            }
                            
                            onOptimizeClicked: {
                                console.log("优化策略:", selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id) : "")
                                // TODO: 实现策略优化功能
                                optimizeStrategy()
                            }

                            onDeleteClicked: {
                                strategyLibraryPage.requestDeleteStrategy(
                                    selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id || "") : "",
                                    selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : "未命名策略"
                                )
                            }
                        }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 150
                            visible: selectedStrategyIndex >= 0
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1

                            ColumnLayout {
                                id: latestBacktestSection
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10

                                property var selectedStrategySummary: strategyLibraryPage.getSelectedStrategySummary()
                                property string selectedStrategyId: selectedStrategySummary ? (selectedStrategySummary.strategyId || selectedStrategySummary.id || "") : ""
                                property var selectedStrategyDetail: strategyLibraryPage.getSelectedStrategyDetail(selectedStrategyId)
                                property var latestBacktestItems: strategyLibraryPage.buildLatestBacktestItems(selectedStrategyDetail)

                                Text {
                                    text: "最近一次回测"
                                    font.pixelSize: fontSizeNormal + 1
                                    font.weight: Font.DemiBold
                                    color: textPrimary
                                }

                                Text {
                                    visible: latestBacktestSection.latestBacktestItems.length === 0
                                    text: "当前策略还没有可展示的回测记录。"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                GridLayout {
                                    visible: latestBacktestSection.latestBacktestItems.length > 0
                                    Layout.fillWidth: true
                                    columns: 4
                                    columnSpacing: 10
                                    rowSpacing: 8

                                    Repeater {
                                        model: latestBacktestSection.latestBacktestItems

                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 44
                                            radius: 8
                                            color: "#0B1220"

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 2

                                                Text {
                                                    text: modelData.label
                                                    font.pixelSize: 11
                                                    color: textTertiary
                                                }

                                                Text {
                                                    text: modelData.value
                                                    font.pixelSize: 13
                                                    font.weight: Font.Medium
                                                    color: textPrimary
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 64
                            visible: selectedStrategyIndex >= 0
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1

                            RowLayout {
                                id: currentStrategyRow
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 12

                                property var selectedStrategySummary: strategyLibraryPage.getSelectedStrategySummary()
                                property string selectedStrategyId: selectedStrategySummary ? (selectedStrategySummary.strategyId || selectedStrategySummary.id || "") : ""
                                property var selectedStrategyDetail: strategyLibraryPage.getSelectedStrategyDetail(selectedStrategyId)

                                Text {
                                    text: "当前策略"
                                    font.pixelSize: fontSizeNormal
                                    font.weight: Font.DemiBold
                                    color: textPrimary
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: currentStrategyRow.selectedStrategyDetail && Object.keys(currentStrategyRow.selectedStrategyDetail).length > 0
                                        ? (currentStrategyRow.selectedStrategyDetail.strategy_name || currentStrategyRow.selectedStrategyDetail.strategyName || "未命名策略")
                                        : "未选择策略"
                                    font.pixelSize: fontSizeNormal
                                    color: textSecondary
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    Layout.preferredWidth: 96
                                    Layout.preferredHeight: 34
                                    radius: 6
                                    color: "#2563eb"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "编辑策略"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "white"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            var editStrategyId = currentStrategyRow.selectedStrategyDetail
                                                ? (currentStrategyRow.selectedStrategyDetail.strategy_id || currentStrategyRow.selectedStrategyDetail.strategyId || currentStrategyRow.selectedStrategyDetail.id || "")
                                                : ""
                                            if (editStrategyId) {
                                                strategyLibraryPage.openStrategyCreation(currentStrategyRow.selectedStrategySummary)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: selectedStrategyIndex >= 0
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1

                            ColumnLayout {
                                id: backtestHistorySection
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10

                                property var selectedStrategySummary: strategyLibraryPage.getSelectedStrategySummary()
                                property string selectedStrategyId: selectedStrategySummary ? (selectedStrategySummary.strategyId || selectedStrategySummary.id || "") : ""
                                property var selectedStrategyDetail: strategyLibraryPage.getSelectedStrategyDetail(selectedStrategyId)
                                property var backtestHistory: strategyLibraryPage.getBacktestHistory(selectedStrategyDetail)

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: "回测历史"
                                        font.pixelSize: fontSizeNormal + 1
                                        font.weight: Font.DemiBold
                                        color: textPrimary
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: backtestHistorySection.backtestHistory.length > 0 ? ("最近 " + backtestHistorySection.backtestHistory.length + " 条") : "暂无历史"
                                        font.pixelSize: fontSizeNormal - 1
                                        color: textSecondary
                                    }
                                }

                                Text {
                                    visible: backtestHistorySection.backtestHistory.length === 0
                                    text: "这里会保留不同股票池、不同日期区间的回测摘要，便于横向比较。"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    visible: backtestHistorySection.backtestHistory.length > 0
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 360
                                    radius: 10
                                    color: "#0B1220"
                                    border.color: "#1E293B"
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 10

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Text {
                                                text: "历史回测对比图"
                                                font.pixelSize: fontSizeNormal
                                                font.weight: Font.DemiBold
                                                color: textPrimary
                                            }

                                            Item { Layout.fillWidth: true }

                                            Text {
                                                text: "横轴按回测记录时间顺序排列"
                                                font.pixelSize: 11
                                                color: textTertiary
                                            }
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            columns: 3
                                            columnSpacing: 12
                                            rowSpacing: 12

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                radius: 8
                                                color: "#111827"

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 10
                                                    spacing: 8

                                                    Text {
                                                        text: "收益对比(%)"
                                                        font.pixelSize: 12
                                                        font.weight: Font.Medium
                                                        color: textPrimary
                                                    }

                                                    ChartView {
                                                        id: historyReturnsChart
                                                        Layout.fillWidth: true
                                                        Layout.fillHeight: true
                                                        antialiasing: true
                                                        legend.visible: false
                                                        backgroundColor: "transparent"
                                                        plotAreaColor: "transparent"

                                                        ValueAxis {
                                                            id: historyReturnsAxisX
                                                            min: 0
                                                            max: Math.max(1, historyReturnsSeries.count > 0 ? historyReturnsSeries.count - 1 : 1)
                                                            tickCount: Math.min(6, Math.max(2, historyReturnsSeries.count > 1 ? 6 : 2))
                                                            labelsColor: textTertiary
                                                            gridLineColor: "#1E293B"
                                                            lineVisible: false
                                                        }

                                                        ValueAxis {
                                                            id: historyReturnsAxisY
                                                            min: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "returns", -5, 5).min
                                                            max: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "returns", -5, 5).max
                                                            tickCount: 5
                                                            labelsColor: textSecondary
                                                            gridLineColor: "#1E293B"
                                                            labelFormat: "%.1f"
                                                        }

                                                        LineSeries {
                                                            id: historyReturnsSeries
                                                            axisX: historyReturnsAxisX
                                                            axisY: historyReturnsAxisY
                                                            color: riseRed
                                                            width: 2

                                                            Component.onCompleted: strategyLibraryPage.updateHistoryMetricSeries(historyReturnsSeries, backtestHistorySection.backtestHistory, "returns")
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                radius: 8
                                                color: "#111827"

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 10
                                                    spacing: 8

                                                    Text {
                                                        text: "回撤对比(%)"
                                                        font.pixelSize: 12
                                                        font.weight: Font.Medium
                                                        color: textPrimary
                                                    }

                                                    ChartView {
                                                        id: historyDrawdownChart
                                                        Layout.fillWidth: true
                                                        Layout.fillHeight: true
                                                        antialiasing: true
                                                        legend.visible: false
                                                        backgroundColor: "transparent"
                                                        plotAreaColor: "transparent"

                                                        ValueAxis {
                                                            id: historyDrawdownAxisX
                                                            min: 0
                                                            max: Math.max(1, historyDrawdownSeries.count > 0 ? historyDrawdownSeries.count - 1 : 1)
                                                            tickCount: Math.min(6, Math.max(2, historyDrawdownSeries.count > 1 ? 6 : 2))
                                                            labelsColor: textTertiary
                                                            gridLineColor: "#1E293B"
                                                            lineVisible: false
                                                        }

                                                        ValueAxis {
                                                            id: historyDrawdownAxisY
                                                            min: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "maxDrawdown", 0, 10).min
                                                            max: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "maxDrawdown", 0, 10).max
                                                            tickCount: 5
                                                            labelsColor: textSecondary
                                                            gridLineColor: "#1E293B"
                                                            labelFormat: "%.1f"
                                                        }

                                                        LineSeries {
                                                            id: historyDrawdownSeries
                                                            axisX: historyDrawdownAxisX
                                                            axisY: historyDrawdownAxisY
                                                            color: "#F59E0B"
                                                            width: 2

                                                            Component.onCompleted: strategyLibraryPage.updateHistoryMetricSeries(historyDrawdownSeries, backtestHistorySection.backtestHistory, "maxDrawdown")
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                radius: 8
                                                color: "#111827"

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 10
                                                    spacing: 8

                                                    Text {
                                                        text: "夏普对比"
                                                        font.pixelSize: 12
                                                        font.weight: Font.Medium
                                                        color: textPrimary
                                                    }

                                                    ChartView {
                                                        id: historySharpeChart
                                                        Layout.fillWidth: true
                                                        Layout.fillHeight: true
                                                        antialiasing: true
                                                        legend.visible: false
                                                        backgroundColor: "transparent"
                                                        plotAreaColor: "transparent"

                                                        ValueAxis {
                                                            id: historySharpeAxisX
                                                            min: 0
                                                            max: Math.max(1, historySharpeSeries.count > 0 ? historySharpeSeries.count - 1 : 1)
                                                            tickCount: Math.min(6, Math.max(2, historySharpeSeries.count > 1 ? 6 : 2))
                                                            labelsColor: textTertiary
                                                            gridLineColor: "#1E293B"
                                                            lineVisible: false
                                                        }

                                                        ValueAxis {
                                                            id: historySharpeAxisY
                                                            min: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "sharpeRatio", -1, 1).min
                                                            max: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "sharpeRatio", -1, 1).max
                                                            tickCount: 5
                                                            labelsColor: textSecondary
                                                            gridLineColor: "#1E293B"
                                                            labelFormat: "%.2f"
                                                        }

                                                        LineSeries {
                                                            id: historySharpeSeries
                                                            axisX: historySharpeAxisX
                                                            axisY: historySharpeAxisY
                                                            color: "#38BDF8"
                                                            width: 2

                                                            Component.onCompleted: strategyLibraryPage.updateHistoryMetricSeries(historySharpeSeries, backtestHistorySection.backtestHistory, "sharpeRatio")
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                ScrollView {
                                    visible: backtestHistorySection.backtestHistory.length > 0
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                                    Column {
                                        width: parent.width
                                        spacing: 10

                                        Repeater {
                                            model: backtestHistorySection.backtestHistory

                                            delegate: Rectangle {
                                                width: parent.width
                                                radius: 10
                                                color: "#0B1220"
                                                border.color: "#1E293B"
                                                border.width: 1
                                                implicitHeight: historyContent.implicitHeight + 24

                                                ColumnLayout {
                                                    id: historyContent
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.margins: 12
                                                    spacing: 8

                                                    property var summary: modelData.summary || ({})

                                                    RowLayout {
                                                        Layout.fillWidth: true

                                                        Text {
                                                            text: (modelData.recordedAt || "--") + "  ·  " + (modelData.universeLabel || modelData.universeType || "未知股票池")
                                                            font.pixelSize: 13
                                                            font.weight: Font.Medium
                                                            color: textPrimary
                                                        }

                                                        Item { Layout.fillWidth: true }

                                                        Text {
                                                            text: strategyLibraryPage.formatBacktestPercentValue(historyContent.summary.returns, 2)
                                                            font.pixelSize: 13
                                                            font.weight: Font.DemiBold
                                                            color: Number(historyContent.summary.returns) >= 0 ? riseRed : fallGreen
                                                        }
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: (modelData.startDate || "--") + " ~ " + (modelData.endDate || "--")
                                                            + "    数据源: " + (modelData.dataSourceMode || "--")
                                                            + (modelData.indexLabel || modelData.indexSymbol ? ("    指数: " + (modelData.indexLabel || modelData.indexSymbol)) : "")
                                                        font.pixelSize: 12
                                                        color: textSecondary
                                                        wrapMode: Text.WordWrap
                                                    }

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 14

                                                        Text {
                                                            text: "最大回撤: " + strategyLibraryPage.formatBacktestPercentValue(historyContent.summary.maxDrawdown, 2)
                                                            font.pixelSize: 12
                                                            color: textSecondary
                                                        }

                                                        Text {
                                                            text: "夏普: " + strategyLibraryPage.formatBacktestNumberValue(historyContent.summary.sharpeRatio, 2)
                                                            font.pixelSize: 12
                                                            color: textSecondary
                                                        }

                                                        Text {
                                                            text: "胜率: " + strategyLibraryPage.formatBacktestPercentValue(historyContent.summary.winRate, 2)
                                                            font.pixelSize: 12
                                                            color: textSecondary
                                                        }

                                                        Text {
                                                            text: "交易: " + strategyLibraryPage.formatBacktestIntegerValue(historyContent.summary.tradesCount)
                                                            font.pixelSize: 12
                                                            color: textSecondary
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                onBacktestHistoryChanged: {
                                    strategyLibraryPage.updateHistoryMetricSeries(historyReturnsSeries, backtestHistorySection.backtestHistory, "returns")
                                    strategyLibraryPage.updateHistoryMetricSeries(historyDrawdownSeries, backtestHistorySection.backtestHistory, "maxDrawdown")
                                    strategyLibraryPage.updateHistoryMetricSeries(historySharpeSeries, backtestHistorySection.backtestHistory, "sharpeRatio")
                                }
                            }
                        }
                        
                        // 提示信息
                        Text {
                            text: "提示：统一量化卡片组件已集成到策略库页面"
                            font.pixelSize: fontSizeNormal - 1
                            color: textTertiary
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                
                // 策略图表
                StrategyComponents.StrategyChart {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    Layout.bottomMargin: spacingXLarge
                }
            }
        }
    }
    
    // 新建策略对话框
    StrategyComponents.CreateStrategyDialog {
        id: createDialog
        anchors.centerIn: parent
        visible: isOpen
        
        onStrategyCreated: function(strategyData) {
            console.log("创建策略:", strategyData);
            // 添加到策略模型
            strategyModel.append({
                name: strategyData.name,
                description: strategyData.description,
                status: strategyData.status,
                returns: strategyData.returns,
                maxDrawdown: strategyData.maxDrawdown,
                sharpeRatio: strategyData.sharpeRatio,
                winRate: strategyData.winRate,
                tags: strategyData.tags,
                runningDays: 0,
                tradesCount: 0,
                position: 0,
                dailyPnL: 0
            });
        }
        
        onClosed: {
            // 关闭对话框
        }
    }
    
    // 筛选弹窗
    StrategyComponents.StrategyFilter {
        id: filterComponent
        anchors.centerIn: parent
        visible: showFilter
        
        onFilterApplied: function(filterData) {
            console.log("应用筛选:", filterData);
            showFilter = false;
        }
        
        onFilterReset: function() {
            console.log("重置筛选");
        }
        
        onFilterClosed: function() {
            showFilter = false;
        }
    }
    
    // 排序弹窗
    StrategyComponents.StrategySorter {
        id: sorterComponent
        anchors.centerIn: parent
        visible: showSorter
        
        onSortApplied: function(sortType) {
            console.log("应用排序:", sortType);
            showSorter = false;
        }
        
        onSortClosed: function() {
            showSorter = false;
        }
    }

    Dialog {
        id: deleteConfirmDialog
        anchors.centerIn: parent
        modal: true
        width: 420
        property string strategyId: ""
        property string strategyName: ""

        background: Rectangle {
            radius: borderRadiusMedium
            color: secondaryBg
            border.color: borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: spacingLarge

            Text {
                text: "删除策略"
                font.pixelSize: fontSizeLarge
                font.weight: Font.DemiBold
                color: textPrimary
            }

            Text {
                text: "确认删除策略“" + deleteConfirmDialog.strategyName + "”？此操作不可撤销。"
                color: textSecondary
                font.pixelSize: fontSizeNormal
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    onClicked: deleteConfirmDialog.close()
                }

                Button {
                    text: "确认删除"
                    enabled: !deleteInProgress
                    onClicked: {
                        if (deleteInProgress) {
                            return
                        }

                        deleteInProgress = true
                        if (strategyService && deleteConfirmDialog.strategyId) {
                            var deletedStrategyId = deleteConfirmDialog.strategyId
                            var ok = strategyService.deleteStrategy(deletedStrategyId)
                            if (ok) {
                                if (selectedStrategyIndex >= 0 && strategyViewModel && selectedStrategyIndex >= strategyViewModel.count - 1) {
                                    selectedStrategyIndex = Math.max(0, strategyViewModel.count - 2)
                                }
                                console.log("策略删除成功:", deletedStrategyId)
                            } else {
                                console.error("策略删除失败:", deletedStrategyId)
                            }
                        }
                        deleteInProgress = false
                        deleteConfirmDialog.close()
                    }
                }
            }
        }
    }
    
    // 遮罩层
    Rectangle {
        anchors.fill: parent
        color: "#00000060"
        visible: showFilter || showSorter || createDialog.isOpen || deleteConfirmDialog.visible
        
        MouseArea {
            anchors.fill: parent
            onClicked: {
                showFilter = false;
                showSorter = false;
                createDialog.closeDialog();
                if (deleteConfirmDialog.visible) {
                    deleteConfirmDialog.close()
                }
            }
        }
    }
    
    // 新建策略页面加载器 - 使用专业版
    Loader {
        id: strategyCreationLoader
        anchors.fill: parent
        active: false
        source: "StrategyCreationPagePro.qml"
        property var pendingStrategyData: ({})
        
        onLoaded: {
            if (item) {
                if (pendingStrategyData && Object.keys(pendingStrategyData).length > 0 && typeof item.loadStrategyForEdit !== "undefined") {
                    item.loadStrategyForEdit(pendingStrategyData)
                } else if (typeof item.resetForm !== "undefined") {
                    item.resetForm()
                }

                // 连接返回信号
                if (typeof item.backClicked !== "undefined") {
                    item.backClicked.connect(function() {
                        console.log("收到创建页面返回信号，关闭创建页面")
                        strategyCreationLoader.pendingStrategyData = ({})
                        strategyCreationLoader.active = false;
                        // 确保返回到策略库页面
                        strategyLibraryPage.forceActiveFocus();
                        // 注意：不需要手动调用syncWithDatabase，因为StrategyService.createStrategy()
                        // 已经发送了dataChanged信号，这个信号会被我们的监听器处理
                        console.log("创建页面已关闭，数据更新将由dataChanged信号处理")
                    });
                }
                
                // 连接策略创建信号（兼容旧版本）
                if (typeof item.strategyCreated !== "undefined") {
                    item.strategyCreated.connect(function(strategyData) {
                        console.log("创建策略:", strategyData);
                        // 添加到策略模型
                        strategyModel.append({
                            name: strategyData.name,
                            description: strategyData.description,
                            status: strategyData.status,
                            returns: strategyData.returns,
                            maxDrawdown: strategyData.maxDrawdown,
                            sharpeRatio: strategyData.sharpeRatio,
                            winRate: strategyData.winRate,
                            tags: strategyData.tags,
                            runningDays: 0,
                            tradesCount: 0,
                            position: 0,
                            dailyPnL: 0
                        });
                        
                        // 关闭创建页面
                        strategyCreationLoader.pendingStrategyData = ({})
                        strategyCreationLoader.active = false;
                    });
                }
                
                // 连接回测请求信号
                if (typeof item.requestBacktest !== "undefined") {
                    item.requestBacktest.connect(function(strategyId, strategyName, backtestConfig) {
                        console.log("接收到回测请求，策略ID:", strategyId, "策略名称:", strategyName);
                        // 关闭创建页面
                        strategyCreationLoader.pendingStrategyData = ({})
                        strategyCreationLoader.active = false;
                        // 通知主窗口切换到回测页面
                        if (typeof window !== "undefined" && window.handleStrategyBacktestRequest) {
                            window.handleStrategyBacktestRequest(strategyId, strategyName, backtestConfig);
                        }
                    });
                }
                
                // 专业版使用resetForm完成后的返回
                if (typeof item.resetForm !== "undefined") {
                    // 监听resetForm完成事件（如果有）
                }
            }
        }
    }
    
    // 工具函数
    function updateStrategyParameter(index, value) {
        console.log("更新参数:", index, value);
    }
    
    function resetStrategyParameters() {
        console.log("重置参数");
    }
    
    function optimizeStrategy() {
        console.log("优化策略");
    }
    
    // 初始化
    Component.onCompleted: {
        console.log("策略库页面初始化完成")
        // 初始化策略视图模型，连接到数据库
        initializeStrategyViewModel()
        
        // 注意：不再使用硬编码数据作为后备，完全依赖数据库数据
        // 策略数据将通过dataChanged信号自动更新
    }
}
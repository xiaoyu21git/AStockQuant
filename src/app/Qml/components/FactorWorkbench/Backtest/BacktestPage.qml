// BacktestPage.qml
// 因子回测页面 - 模块化组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import "../../Backtest" as BacktestComponents

/**
 * 因子回测页面组件
 * 提供因子历史表现回测功能
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property Bridge.FactorService factorService: null  // 修复：属性名以小写字母开头
    property Bridge.CleanedDataController cleanedDataController: null
    property string selectedFactorId: ""
    
    // 回测控制器
    Bridge.FactorBacktestController {
        id: factorBacktestController
        onBacktestStarted: function(factorId) {
            console.log("回测开始:", factorId)
            isBacktesting = true
            backtestStatus = "正在回测"
        }
        onBacktestProgress: function(progress, status) {
            backtestProgress = progress
            backtestStatus = status
        }
        onBacktestCompleted: function(result) {
            console.log("回测完成:", result)
            isBacktesting = false
            backtestProgress = 100
            backtestStatus = "回测完成"
            
            // 更新结果
            updateResults(result)
            showToast("✅ 因子回测完成")
        }
        onBacktestFailed: function(error) {
            console.error("回测失败:", error)
            isBacktesting = false
            backtestStatus = "回测失败"
            showToast("❌ 回测失败: " + error)
        }
        onBacktestCancelled: function() {
            console.log("回测已取消")
            isBacktesting = false
            backtestStatus = "已取消"
            showToast("⏸️ 回测已取消")
        }
    }
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    
    // 回测结果
    property var backtestResult: ({})
    property var groupResults: []
    property var icirResult: ({})
    property var summaryStats: ({})
    
    // 分组配置
    property var groupConfig: ({})
    
    // ============ UI ============
    
    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16
            
            // 标题
            Text {
                text: "🧪 因子回测工作区"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            // 回测控制面板
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 12
                color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
                    // 回测配置
                    RowLayout {
                        spacing: 16
                        
                        // 回测周期
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "回测周期"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            ComboBox {
                                id: periodComboBox
                                Layout.preferredWidth: 120
                                model: ["最近1年", "最近3年", "最近5年", "全周期"]
                                currentIndex: 0
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: parent.displayText
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        // 基准指数
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "基准指数"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            ComboBox {
                                id: benchmarkComboBox
                                Layout.preferredWidth: 120
                                model: ["沪深300", "中证500", "中证1000", "创业板指"]
                                currentIndex: 0
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: parent.displayText
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        // 分组数量
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "分组数量"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            ComboBox {
                                id: groupComboBox
                                Layout.preferredWidth: 80
                                model: ["5组", "10组", "20组"]
                                currentIndex: 1
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: parent.displayText
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        // 数据集选择
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "数据集"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            ComboBox {
                                id: datasetComboBox
                                Layout.preferredWidth: 160
                                model: ListModel {
                                    id: datasetModel
                                    ListElement { id: -1; name: "默认数据源"; description: "使用系统默认数据源" }
                                }
                                textRole: "name"
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: parent.displayText
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                }
                                
                                onCurrentIndexChanged: {
                                    console.log("数据集选择变更:", currentText)
                                    if (currentIndex >= 0) {
                                        var selected = datasetModel.get(currentIndex)
                                        console.log("选择的数据集:", selected.id, selected.name)
                                    }
                                }
                                
                                Component.onCompleted: {
                                    loadDataSets()
                                }
                            }
                        }
                        
                        // 缓存选择
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "缓存选择"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            ComboBox {
                                id: cacheComboBox
                                Layout.preferredWidth: 140
                                model: ["自动选择", "使用缓存", "重新计算"]
                                currentIndex: 0
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: parent.displayText
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                }
                                
                                onCurrentIndexChanged: {
                                    console.log("缓存选择变更:", currentText)
                                }
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 高级配置按钮
                        Rectangle {
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 32
                            radius: 6
                            color: "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "⚙️"
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: "高级配置"
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: showAdvancedConfig()
                            }
                        }
                    }
                    
                    // 回测控制
                    RowLayout {
                        spacing: 12
                        
                        // 回测按钮
                        Rectangle {
                            id: backtestButton
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 40
                            radius: 8
                            color: isBacktesting ? "#334155" : "#3B82F6"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 8
                                
                                Text {
                                    text: isBacktesting ? "⏸️" : "▶️"
                                    font.pixelSize: 14
                                    color: isBacktesting ? "#94A3B8" : "white"
                                }
                                
                                Text {
                                    text: isBacktesting ? "回测中..." : "开始回测"
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    color: isBacktesting ? "#94A3B8" : "white"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: !isBacktesting && selectedFactorId
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
                }
            }
            
            // 分组配置面板（默认折叠）
            Rectangle {
                id: groupConfigPanelContainer
                Layout.fillWidth: true
                Layout.preferredHeight: groupConfigPanel.visible ? 400 : 0
                radius: 12
                color: "#1E293B"
                visible: false
                clip: true
                
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 300; easing.type: Easing.InOutQuad }
                }
                
                GroupConfigPanel {
                    id: groupConfigPanel
                    anchors.fill: parent
                    anchors.margins: 16
                    
                    onConfigChanged: function(config) {
                        console.log("分组配置变更:", config)
                        groupConfig = config
                    }
                }
            }
            
            // 回测结果区域
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
                    // 结果标题
                    RowLayout {
                        spacing: 8
                        
                        Text {
                            text: "📊 回测结果"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 结果状态
                        Text {
                            text: selectedFactorId ? "因子: " + getFactorName(selectedFactorId) : "请选择因子进行回测"
                            font.pixelSize: 12
                            color: selectedFactorId ? "#3B82F6" : "#94A3B8"
                        }
                    }
                    
                    // 结果网格
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 3
                        columnSpacing: 16
                        rowSpacing: 16
                        
                        // 年化收益
                        BacktestComponents.BacktestMetricCard {
                            title: "执行年化"
                            value: metricPercentText(summaryStats.executionAnnualReturn, 2)
                            description: "Execution Annual Return"
                            trend: metricTrend(summaryStats.executionAnnualReturn)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }
                        
                        // 夏普比率
                        BacktestComponents.BacktestMetricCard {
                            title: "夏普比率"
                            value: metricNumberText(summaryStats.sharpeRatio, 2)
                            description: "Sharpe Ratio"
                            trend: metricTrend(summaryStats.sharpeRatio)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }
                        
                        // 最大回撤
                        BacktestComponents.BacktestMetricCard {
                            title: "最大回撤"
                            value: metricPercentText(summaryStats.maxDrawdown, 2)
                            description: "Max Drawdown"
                            trend: "down"
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }
                        
                        // 胜率
                        BacktestComponents.BacktestMetricCard {
                            title: "胜率"
                            value: metricPercentText(summaryStats.winRate, 1)
                            description: "Win Rate"
                            trend: metricTrend(summaryStats.winRate - 0.5)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }
                        
                        // IC值
                        BacktestComponents.BacktestMetricCard {
                            title: "IC值"
                            value: metricNumberText(icirResult.icValue, 3)
                            description: "Information Coefficient"
                            trend: metricTrend(icirResult.icValue)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }
                        
                        // IR值
                        BacktestComponents.BacktestMetricCard {
                            title: "IR值"
                            value: metricNumberText(icirResult.irValue, 2)
                            description: "Information Ratio"
                            trend: metricTrend(icirResult.irValue)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }

                        // 基准年化
                        BacktestComponents.BacktestMetricCard {
                            title: "基准年化"
                            value: metricPercentText(summaryStats.benchmarkAnnualReturn, 2)
                            description: "Benchmark Return"
                            trend: metricTrend(summaryStats.benchmarkAnnualReturn)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }

                        // 超额年化
                        BacktestComponents.BacktestMetricCard {
                            title: "超额年化"
                            value: metricPercentText(summaryStats.excessAnnualReturn, 2)
                            description: "Excess Return"
                            trend: metricTrend(summaryStats.excessAnnualReturn)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }

                        // 跟踪误差
                        BacktestComponents.BacktestMetricCard {
                            title: "跟踪误差"
                            value: metricPercentText(summaryStats.trackingError, 2)
                            description: "Tracking Error"
                            trend: "neutral"
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }

                        // 信息比率
                        BacktestComponents.BacktestMetricCard {
                            title: "信息比率"
                            value: metricNumberText(summaryStats.informationRatio, 2)
                            description: "Information Ratio"
                            trend: metricTrend(summaryStats.informationRatio)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }

                        // Alpha
                        BacktestComponents.BacktestMetricCard {
                            title: "Alpha"
                            value: metricPercentText(summaryStats.alpha, 2)
                            description: "CAPM Alpha"
                            trend: metricTrend(summaryStats.alpha)
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }

                        // Beta
                        BacktestComponents.BacktestMetricCard {
                            title: "Beta"
                            value: metricNumberText(summaryStats.beta, 2)
                            description: "Benchmark Beta"
                            trend: "neutral"
                            cardHeight: 100
                            titleSize: 14
                            valueSize: 20
                            descriptionSize: 10
                            upColor: "#EF4444"
                            downColor: "#10B981"
                        }
                    }
                    
                    // 详细结果按钮
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        
                        // 查看详细结果
                        Rectangle {
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 32
                            radius: 6
                            color: "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "📈"
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: "详细结果"
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: showDetailedResults()
                            }
                        }
                        
                        // 对比分析
                        Rectangle {
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 32
                            radius: 6
                            color: "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "⇄"
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: "对比"
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: showComparison()
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 导出结果
                        Rectangle {
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 32
                            radius: 6
                            color: "#3B82F6"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "📤"
                                    font.pixelSize: 12
                                    color: "white"
                                }
                                
                                Text {
                                    text: "导出"
                                    font.pixelSize: 12
                                    color: "white"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: exportResults()
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    
    // 开始回测
    function startBacktest() {
        if (!selectedFactorId) {
            showToast("请先选择要回测的因子")
            return
        }
        
        // 获取回测配置
        var config = getBacktestConfig()
        
        console.log("开始因子回测:", {
            factorId: selectedFactorId,
            config: config
        })
        
        // 初始化回测控制器
        if (!factorBacktestController.initialize()) {
            showToast("❌ 回测控制器初始化失败")
            return
        }
        
        // 运行异步回测
        factorBacktestController.runFactorBacktestAsync(selectedFactorId, config)
    }
    
    // 获取回测配置
    function getBacktestConfig() {
        var config = factorBacktestController.getDefaultConfig()
        
        // 获取数据库中实际可用的日期范围
        var dataDateRange = getAvailableDataDateRange()
        var dataEndDate = dataDateRange.endDate
        var dataStartDate = dataDateRange.startDate
        
        console.log("数据库可用日期范围:", dataStartDate, "至", dataEndDate)
        
        // 设置回测周期 - 基于数据库实际数据的结束日期
        var period = periodComboBox.currentText
        var endDate = new Date(dataEndDate)
        var startDate = new Date(dataEndDate)
        
        switch(period) {
            case "最近1年":
                startDate.setFullYear(endDate.getFullYear() - 1)
                break
            case "最近3年":
                startDate.setFullYear(endDate.getFullYear() - 3)
                break
            case "最近5年":
                startDate.setFullYear(endDate.getFullYear() - 5)
                break
            case "全周期":
                startDate = new Date(dataStartDate) // 使用数据库的最早日期
                break
        }
        
        // 确保开始日期不早于数据库中的最早日期
        var dbStartDate = new Date(dataStartDate)
        if (startDate < dbStartDate) {
            startDate = dbStartDate
        }
        
        config.startDate = formatDate(startDate)
        config.endDate = formatDate(endDate)
        
        console.log("回测配置日期:", config.startDate, "至", config.endDate)
        
        // 设置分组数量
        var groups = groupComboBox.currentText
        config.numGroups = parseInt(groups)
        
        // 设置分组方法
        config.groupingMethod = "quantile" // 默认使用分位数分组
        
        // 设置回测策略
        config.strategy = "equal_weight" // 默认使用等权重策略
        
        // 设置初始资金
        config.initialCapital = 1000000
        
        // 设置交易成本
        config.transactionCost = 0.001
        
        // 设置滑点
        config.slippage = 0.001
        
        // 设置最大线程数
        config.maxThreads = 4
        
        // 根据缓存选择设置缓存配置
        var cacheOption = cacheComboBox.currentText
        if (cacheOption === "使用缓存") {
            config.enableCache = true
            config.cacheTTL = 3600
            console.log("缓存模式: 强制使用缓存")
        } else if (cacheOption === "重新计算") {
            config.enableCache = false  // 禁用缓存
            console.log("缓存模式: 强制重新计算")
        } else {
            // 自动选择
            config.enableCache = true
            config.cacheTTL = 3600
            console.log("缓存模式: 自动选择")
        }
        
        // 设置数据集配置
        if (datasetComboBox.currentIndex >= 0) {
            var selectedDataSet = datasetModel.get(datasetComboBox.currentIndex)
            config.dataSetId = selectedDataSet.id
            config.dataSetName = selectedDataSet.name
            console.log("数据集配置:", selectedDataSet.id, selectedDataSet.name)
        }
        
        return config
    }
    
    // 格式化日期为YYYY-MM-DD
    function formatDate(date) {
        var year = date.getFullYear()
        var month = (date.getMonth() + 1).toString().padStart(2, '0')
        var day = date.getDate().toString().padStart(2, '0')
        return year + "-" + month + "-" + day
    }
    
    // 更新结果
    function updateResults(result) {
        backtestResult = result
        
        // 提取分组结果
        if (result.groups && Array.isArray(result.groups)) {
            groupResults = result.groups
        }
        
        // 提取ICIR结果
        if (result.icirResult) {
            icirResult = result.icirResult
        }
        
        // 提取汇总统计
        if (result.summary) {
            summaryStats = result.summary
        }
        
        // 更新UI显示
        updateResultCards()
    }

    function hasFactorContext() {
        return String(selectedFactorId || "").length > 0
    }

    function hasNumericMetricValue(value) {
        if (value === undefined || value === null) {
            return false
        }
        var numericValue = Number(value)
        return isFinite(numericValue)
    }

    function metricNumberText(value, digits) {
        if (!hasFactorContext()) {
            return "N/A"
        }
        if (!hasNumericMetricValue(value)) {
            return Number(0).toFixed(digits)
        }
        return Number(value).toFixed(digits)
    }

    function metricPercentText(value, digits) {
        if (!hasFactorContext()) {
            return "N/A"
        }
        if (!hasNumericMetricValue(value)) {
            return (Number(0) * 100).toFixed(digits) + "%"
        }
        return (Number(value) * 100).toFixed(digits) + "%"
    }

    function metricTrend(value) {
        if (!hasFactorContext() || !hasNumericMetricValue(value)) {
            return "neutral"
        }
        var numericValue = Number(value)
        if (numericValue > 0) {
            return "up"
        }
        if (numericValue < 0) {
            return "down"
        }
        return "neutral"
    }
    
    // 更新结果卡片
    function updateResultCards() {
        // 这里需要更新结果卡片组件的值
        // 由于QML组件是静态的，我们需要重新加载或使用绑定
        // 暂时使用控制台输出
        console.log("更新结果卡片:", {
            summaryStats: summaryStats,
            icirResult: icirResult
        })
    }
    
    // 获取因子名称
    function getFactorName(factorId) {
        // 如果没有因子服务，返回空字符串
        if (!factorService) return ""
        
        // 通过因子服务获取因子信息
        var factor = factorService.getFactorById(factorId)
        if (factor && typeof factor === 'object' && Object.keys(factor).length > 0) {
            return factor.displayName || factor.factorName || ""
        }
        return ""
    }
    
    // 显示详细结果
    function showDetailedResults() {
        console.log("显示详细回测结果")
        if (!selectedFactorId) {
            showToast("请先完成回测")
            return
        }
        // TODO: 实现详细结果功能
        showToast("详细结果功能开发中")
    }
    
    // 显示对比分析
    function showComparison() {
        console.log("显示对比分析")
        if (!selectedFactorId) {
            showToast("请先完成回测")
            return
        }
        // TODO: 实现对比分析功能
        showToast("对比分析功能开发中")
    }
    
    // 导出结果
    function exportResults() {
        console.log("导出回测结果")
        if (!selectedFactorId) {
            showToast("请先完成回测")
            return
        }
        // TODO: 实现导出功能
        showToast("导出结果功能开发中")
    }
    
    // 显示高级配置
    function showAdvancedConfig() {
        groupConfigPanelContainer.visible = !groupConfigPanelContainer.visible
        console.log("高级配置面板:", groupConfigPanelContainer.visible ? "显示" : "隐藏")
    }
    
    // 加载数据集列表
    function loadDataSets() {
        console.log("加载数据集列表")
        
        // 清空现有数据
        datasetModel.clear()
        
        // 从回测控制器获取数据集列表
        var dataSets = factorBacktestController.getAvailableDataSets()
        
        if (dataSets && dataSets.length > 0) {
            console.log("获取到数据集数量:", dataSets.length)
            
            // 添加到模型
            for (var i = 0; i < dataSets.length; i++) {
                var dataSet = dataSets[i]
                datasetModel.append({
                    id: dataSet.id || -1,
                    name: dataSet.name || "未知数据集",
                    description: dataSet.description || "",
                    stockCount: dataSet.stockCount || 0,
                    startDate: dataSet.startDate || "",
                    endDate: dataSet.endDate || ""
                })
            }
            
            // 默认选择第一个
            datasetComboBox.currentIndex = 0
            console.log("数据集列表加载完成")
        } else {
            console.warn("未获取到数据集列表")
            // 添加默认选项
            datasetModel.append({
                id: -1,
                name: "默认数据源",
                description: "使用系统默认数据源",
                stockCount: 0,
                startDate: "",
                endDate: ""
            })
        }
    }
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
        // TODO: 实现toast提示组件
    }
    
    // ============ 数据日期范围获取 ============
    
    // 获取数据库中实际可用的数据日期范围
    function getAvailableDataDateRange() {
        // 首先尝试从cleanedDataController获取
        if (cleanedDataController) {
            var dateRange = cleanedDataController.getDataDateRange()
            if (dateRange && dateRange.startDate && dateRange.endDate) {
                console.log("从CleanedDataController获取日期范围:", dateRange.startDate, "至", dateRange.endDate)
                return dateRange
            }
        }
        
        // 如果没有cleanedDataController或无法获取，使用默认的2024年数据范围
        // 这是因为数据库中的示例数据通常是2024年的
        console.log("使用默认数据日期范围: 2024-01-01 至 2024-12-31")
      
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("因子回测页面初始化完成")
        console.log("因子服务:", factorService)
        console.log("当前选择因子:", selectedFactorId)
        
        // 加载数据集列表
        loadDataSets()
    }
}
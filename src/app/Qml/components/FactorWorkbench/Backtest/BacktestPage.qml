// BacktestPage.qml
// 因子回测页面 - 模块化组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

/**
 * 因子回测页面组件
 * 提供因子历史表现回测功能
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property Bridge.GlobalDataService globalDataService: null
    property Bridge.FactorService factorService: null  // 修复：属性名以小写字母开头
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
                        
                        Item { Layout.fillWidth: true }
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
                        ResultCard {
                            title: "年化收益"
                            value: "12.5%"
                            description: "Annual Return"
                            trend: "up"
                        }
                        
                        // 夏普比率
                        ResultCard {
                            title: "夏普比率"
                            value: "1.85"
                            description: "Sharpe Ratio"
                            trend: "up"
                        }
                        
                        // 最大回撤
                        ResultCard {
                            title: "最大回撤"
                            value: "-18.2%"
                            description: "Max Drawdown"
                            trend: "down"
                        }
                        
                        // 胜率
                        ResultCard {
                            title: "胜率"
                            value: "62.3%"
                            description: "Win Rate"
                            trend: "up"
                        }
                        
                        // 盈亏比
                        ResultCard {
                            title: "盈亏比"
                            value: "1.45"
                            description: "Profit/Loss Ratio"
                            trend: "neutral"
                        }
                        
                        // 信息比率
                        ResultCard {
                            title: "信息比率"
                            value: "0.85"
                            description: "Information Ratio"
                            trend: "up"
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
    
    // ============ 组件定义 ============
    
    // 结果卡片组件
    component ResultCard: Item {
        property string title: ""
        property string value: ""
        property string description: ""
        property string trend: "neutral"  // up, down, neutral
        
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#0F172A"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4
                
                Text {
                    text: title
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }
                
                Row {
                    spacing: 6
                    
                    Text {
                        text: value
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: getValueColor()
                    }
                    
                    // 趋势指示器
                    Text {
                        visible: trend !== "neutral"
                        text: trend === "up" ? "↑" : "↓"
                        font.pixelSize: 14
                        color: trend === "up" ? "#10B981" : "#EF4444"
                    }
                }
                
                Text {
                    text: description
                    font.pixelSize: 10
                    color: "#94A3B8"
                }
            }
        }
        
        // 根据数值获取颜色
        function getValueColor() {
            if (trend === "up") return "#10B981"
            if (trend === "down") return "#EF4444"
            return "#F1F5F9"
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
        
        // 设置回测周期
        var period = periodComboBox.currentText
        var today = new Date()
        var startDate = new Date()
        
        switch(period) {
            case "最近1年":
                startDate.setFullYear(today.getFullYear() - 1)
                break
            case "最近3年":
                startDate.setFullYear(today.getFullYear() - 3)
                break
            case "最近5年":
                startDate.setFullYear(today.getFullYear() - 5)
                break
            case "全周期":
                startDate.setFullYear(2010, 0, 1) // 从2010年开始
                break
        }
        
        config.startDate = formatDate(startDate)
        config.endDate = formatDate(today)
        
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
        
        // 启用缓存
        config.enableCache = true
        config.cacheTTL = 3600
        
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
        if (factor && !factor.isEmpty()) {
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
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
        // TODO: 实现toast提示组件
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("因子回测页面初始化完成")
        console.log("全局数据服务:", globalDataService)
        console.log("因子服务:", factorService)
        console.log("当前选择因子:", selectedFactorId)
    }
}
// StrategyBacktestPage.qml
// 策略回测页面 - 模块化组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

/**
 * 策略回测页面组件
 * 提供交易策略历史表现回测功能
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property Bridge.GlobalDataService globalDataService: null
    property Bridge.StrategyService strategyService: null
    property string selectedStrategyId: ""
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    
    // 回测结果
    property var backtestResult: ({})
    property var performanceStats: ({})
    property var riskMetrics: ({})
    
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
                text: "📈 策略回测工作区（交易策略验证）"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            // 说明
            Text {
                text: "测试交易策略的盈利能力，评估策略的风险收益特征"
                font.pixelSize: 12
                color: "#94A3B8"
                wrapMode: Text.WordWrap
            }
            
            // 策略配置面板
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                radius: 12
                color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
                    Text {
                        text: "⚙️ 策略配置"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                    
                    // 策略选择
                    RowLayout {
                        spacing: 16
                        
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "选择策略"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            ComboBox {
                                id: strategyComboBox
                                Layout.preferredWidth: 200
                                model: ["均线交叉策略", "动量策略", "反转策略", "多因子策略"]
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
                        
                        Item { Layout.fillWidth: true }
                    }
                    
                    // 回测参数
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
                        
                        // 初始资金
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "初始资金"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            TextField {
                                id: initialCapitalField
                                Layout.preferredWidth: 120
                                text: "1000000"
                                placeholderText: "输入初始资金"
                                
                                background: Rectangle {
                                    implicitWidth: 200
                                    implicitHeight: 32
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                color: "#F1F5F9"
                                font.pixelSize: 12
                                selectByMouse: true
                                
                                validator: DoubleValidator {
                                    bottom: 10000
                                    top: 1000000000
                                    decimals: 0
                                }
                            }
                        }
                        
                        // 交易成本
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "交易成本"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            TextField {
                                id: commissionField
                                Layout.preferredWidth: 80
                                text: "0.001"
                                placeholderText: "佣金率"
                                
                                background: Rectangle {
                                    implicitWidth: 200
                                    implicitHeight: 32
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                color: "#F1F5F9"
                                font.pixelSize: 12
                                selectByMouse: true
                                
                                validator: DoubleValidator {
                                    bottom: 0
                                    top: 0.1
                                    decimals: 4
                                }
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                    }
                }
            }
            
            // 回测控制面板
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                radius: 12
                color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
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
                            text: "📊 策略回测结果"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 结果状态
                        Text {
                            text: selectedStrategyId ? "策略: " + getStrategyName(selectedStrategyId) : "请配置策略进行回测"
                            font.pixelSize: 12
                            color: selectedStrategyId ? "#3B82F6" : "#94A3B8"
                        }
                    }
                    
                    // 结果网格
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 3
                        columnSpacing: 16
                        rowSpacing: 16
                        
                        // 总收益
                        ResultCard {
                            title: "总收益"
                            value: "25.8%"
                            description: "Total Return"
                            trend: "up"
                        }
                        
                        // 年化收益
                        ResultCard {
                            title: "年化收益"
                            value: "15.2%"
                            description: "Annual Return"
                            trend: "up"
                        }
                        
                        // 夏普比率
                        ResultCard {
                            title: "夏普比率"
                            value: "1.42"
                            description: "Sharpe Ratio"
                            trend: "up"
                        }
                        
                        // 最大回撤
                        ResultCard {
                            title: "最大回撤"
                            value: "-12.5%"
                            description: "Max Drawdown"
                            trend: "down"
                        }
                        
                        // 胜率
                        ResultCard {
                            title: "胜率"
                            value: "58.3%"
                            description: "Win Rate"
                            trend: "up"
                        }
                        
                        // 盈亏比
                        ResultCard {
                            title: "盈亏比"
                            value: "1.68"
                            description: "Profit/Loss Ratio"
                            trend: "up"
                        }
                    }
                    
                    // 控制按钮
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
                        
                        // 参数优化
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
                                    text: "优化"
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: showOptimization()
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
        if (!strategyComboBox.currentText) {
            showToast("请先选择要回测的策略")
            return
        }
        
        // 获取回测配置
        var config = getBacktestConfig()
        
        console.log("开始策略回测:", {
            strategy: strategyComboBox.currentText,
            config: config
        })
        
        // 模拟回测过程
        simulateBacktest()
    }
    
    // 获取回测配置
    function getBacktestConfig() {
        var config = {
            strategy: strategyComboBox.currentText,
            period: periodComboBox.currentText,
            initialCapital: parseFloat(initialCapitalField.text) || 1000000,
            commissionRate: parseFloat(commissionField.text) || 0.001,
            slippage: 0.001,
            startDate: "2020-01-01",
            endDate: "2025-01-01"
        }
        
        return config
    }
    
    // 模拟回测过程
    function simulateBacktest() {
        isBacktesting = true
        backtestStatus = "正在回测"
        backtestProgress = 0
        
        // 模拟进度更新
        var interval = setInterval(function() {
            if (backtestProgress < 100) {
                backtestProgress += 10
                backtestStatus = "正在回测 (" + backtestProgress + "%)"
            } else {
                clearInterval(interval)
                isBacktesting = false
                backtestStatus = "回测完成"
                
                // 更新结果
                updateResults(generateMockResult())
                showToast("✅ 策略回测完成")
            }
        }, 200)
    }
    
    // 生成模拟结果
    function generateMockResult() {
        return {
            totalReturn: 0.258,
            annualReturn: 0.152,
            sharpeRatio: 1.42,
            maxDrawdown: -0.125,
            winRate: 0.583,
            profitLossRatio: 1.68,
            totalTrades: 156,
            winningTrades: 91,
            losingTrades: 65,
            averageWin: 0.032,
            averageLoss: -0.019
        }
    }
    
    // 更新结果
    function updateResults(result) {
        backtestResult = result
        
        // 提取绩效统计
        if (result) {
            performanceStats = {
                totalReturn: result.totalReturn,
                annualReturn: result.annualReturn,
                sharpeRatio: result.sharpeRatio
            }
            
            riskMetrics = {
                maxDrawdown: result.maxDrawdown,
                winRate: result.winRate,
                profitLossRatio: result.profitLossRatio
            }
        }
        
        // 更新UI显示
        updateResultCards()
    }
    
    // 更新结果卡片
    function updateResultCards() {
        console.log("更新策略回测结果卡片:", {
            performanceStats: performanceStats,
            riskMetrics: riskMetrics
        })
    }
    
    // 获取策略名称
    function getStrategyName(strategyId) {
        // 如果没有策略服务，返回空字符串
        if (!strategyService) return strategyComboBox.currentText || ""
        return strategyComboBox.currentText || ""
    }
    
    // 显示详细结果
    function showDetailedResults() {
        console.log("显示详细策略回测结果")
        if (!strategyComboBox.currentText) {
            showToast("请先完成回测")
            return
        }
        showToast("详细结果功能开发中")
    }
    
    // 显示参数优化
    function showOptimization() {
        console.log("显示参数优化")
        if (!strategyComboBox.currentText) {
            showToast("请先选择策略")
            return
        }
        showToast("参数优化功能开发中")
    }
    
    // 导出结果
    function exportResults() {
        console.log("导出策略回测结果")
        if (!strategyComboBox.currentText) {
            showToast("请先完成回测")
            return
        }
        showToast("导出结果功能开发中")
    }
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
        // TODO: 实现toast提示组件
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("策略回测页面初始化完成")
        console.log("全局数据服务:", globalDataService)
        console.log("策略服务:", strategyService)
    }
}

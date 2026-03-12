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
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    
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
        
        console.log("开始因子回测:", {
            factorId: selectedFactorId,
            period: periodComboBox.currentText,
            benchmark: benchmarkComboBox.currentText,
            groups: groupComboBox.currentText
        })
        
        // 模拟回测过程
        isBacktesting = true
        backtestProgress = 0
        backtestStatus = "初始化..."
        
        // 模拟进度更新
        var timer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 100; running: true; repeat: true }', root)
        timer.triggered.connect(function() {
            if (backtestProgress < 100) {
                backtestProgress += 2
                
                // 更新状态文本
                if (backtestProgress < 30) {
                    backtestStatus = "数据加载..."
                } else if (backtestProgress < 60) {
                    backtestStatus = "计算因子值..."
                } else if (backtestProgress < 90) {
                    backtestStatus = "计算绩效指标..."
                } else {
                    backtestStatus = "生成报告..."
                }
            } else {
                // 回测完成
                isBacktesting = false
                backtestStatus = "回测完成"
                timer.stop()
                timer.destroy()
                
                showToast("✅ 因子回测完成")
            }
        })
    }
    
    // 获取因子名称
    function getFactorName(factorId) {
        if (!factorDataModel) return ""
        
        for (var i = 0; i < factorDataModel.rowCount(); i++) {
            var factor = factorDataModel.get(i)
            if (factor.factorId === factorId) {
                return factor.displayName || factor.factorName
            }
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
        console.log("因子数据模型:", factorDataModel)
        console.log("当前选择因子:", selectedFactorId)
    }
}
// BacktestResultView.qml
// 回测结果展示组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtCharts 2.15

/**
 * 回测结果展示组件
 * 显示因子回测的详细结果和图表
 */
Item {
    id: root
    
    // ============ 属性 ============
    
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
                text: "📊 回测结果详情"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            // 结果概览卡片
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 12
                color: "#1E293B"
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16
                    
                    // 年化收益
                    StatCard {
                        title: "年化收益"
                        value: summaryStats.topGroupReturn ? (summaryStats.topGroupReturn * 100).toFixed(2) + "%" : "N/A"
                        description: "Top Group Return"
                        trend: summaryStats.topGroupReturn > 0 ? "up" : "down"
                    }
                    
                    // 夏普比率
                    StatCard {
                        title: "夏普比率"
                        value: summaryStats.sharpeRatio ? summaryStats.sharpeRatio.toFixed(2) : "N/A"
                        description: "Sharpe Ratio"
                        trend: summaryStats.sharpeRatio > 1 ? "up" : "neutral"
                    }
                    
                    // 最大回撤
                    StatCard {
                        title: "最大回撤"
                        value: summaryStats.maxDrawdown ? (summaryStats.maxDrawdown * 100).toFixed(2) + "%" : "N/A"
                        description: "Max Drawdown"
                        trend: "down"
                    }
                    
                    // 胜率
                    StatCard {
                        title: "胜率"
                        value: summaryStats.winRate ? (summaryStats.winRate * 100).toFixed(1) + "%" : "N/A"
                        description: "Win Rate"
                        trend: summaryStats.winRate > 0.5 ? "up" : "down"
                    }
                    
                    Item { Layout.fillWidth: true }
                }
            }
            
            // ICIR指标卡片
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                radius: 12
                color: "#1E293B"
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16
                    
                    // IC值
                    StatCard {
                        title: "IC值"
                        value: icirResult.icValue ? icirResult.icValue.toFixed(3) : "N/A"
                        description: "Information Coefficient"
                        trend: icirResult.icValue > 0 ? "up" : "down"
                    }
                    
                    // IR值
                    StatCard {
                        title: "IR值"
                        value: icirResult.irValue ? icirResult.irValue.toFixed(2) : "N/A"
                        description: "Information Ratio"
                        trend: icirResult.irValue > 0.5 ? "up" : "neutral"
                    }
                    
                    // IC T统计量
                    StatCard {
                        title: "IC T统计"
                        value: icirResult.icTStat ? icirResult.icTStat.toFixed(2) : "N/A"
                        description: "IC T-Statistic"
                        trend: Math.abs(icirResult.icTStat) > 1.96 ? "up" : "neutral"
                    }
                    
                    // IC正率
                    StatCard {
                        title: "IC正率"
                        value: icirResult.icPositiveRate ? (icirResult.icPositiveRate * 100).toFixed(1) + "%" : "N/A"
                        description: "IC Positive Rate"
                        trend: icirResult.icPositiveRate > 0.5 ? "up" : "down"
                    }
                    
                    Item { Layout.fillWidth: true }
                }
            }
            
            // 分组结果表格
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
                    Text {
                        text: "📈 分组绩效表现"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                    
                    // 分组表格
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: groupResults
                        clip: true
                        
                        header: Row {
                            width: parent.width
                            height: 40
                            spacing: 0
                            
                            // 表头
                            TableHeaderCell { text: "组别"; width: 80 }
                            TableHeaderCell { text: "股票数"; width: 80 }
                            TableHeaderCell { text: "收益"; width: 100 }
                            TableHeaderCell { text: "波动率"; width: 100 }
                            TableHeaderCell { text: "夏普比率"; width: 100 }
                            TableHeaderCell { text: "最大回撤"; width: 100 }
                        }
                        
                        delegate: Row {
                            width: parent.width
                            height: 40
                            spacing: 0
                            
                            // 组别
                            TableCell {
                                width: 80
                                text: modelData.groupName || ("组" + (index + 1))
                                color: index === 0 ? "#10B981" : index === groupResults.length - 1 ? "#EF4444" : "#F1F5F9"
                            }
                            
                            // 股票数
                            TableCell {
                                width: 80
                                text: modelData.stockCount || "N/A"
                            }
                            
                            // 收益
                            TableCell {
                                width: 100
                                text: modelData.return ? (modelData.return * 100).toFixed(2) + "%" : "N/A"
                                color: modelData.return > 0 ? "#10B981" : "#EF4444"
                            }
                            
                            // 波动率
                            TableCell {
                                width: 100
                                text: modelData.volatility ? (modelData.volatility * 100).toFixed(2) + "%" : "N/A"
                            }
                            
                            // 夏普比率
                            TableCell {
                                width: 100
                                text: modelData.sharpeRatio ? modelData.sharpeRatio.toFixed(2) : "N/A"
                                color: modelData.sharpeRatio > 1 ? "#10B981" : modelData.sharpeRatio < 0 ? "#EF4444" : "#F1F5F9"
                            }
                            
                            // 最大回撤
                            TableCell {
                                width: 100
                                text: modelData.maxDrawdown ? (modelData.maxDrawdown * 100).toFixed(2) + "%" : "N/A"
                                color: modelData.maxDrawdown < -0.2 ? "#EF4444" : "#F1F5F9"
                            }
                        }
                    }
                }
            }
            
            // 分组结果图表
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 400
                radius: 12
                color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
                    Text {
                        text: "📊 分组绩效可视化"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                    
                    // 分组结果图表组件
                    GroupResultChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        groupResults: root.groupResults
                    }
                }
            }
            
            // 图表区域（保留原有图表）
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                radius: 12
                color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
                    Text {
                        text: "📈 分组收益曲线"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                    
                    // 图表占位符
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: "#0F172A"
                        
                        Text {
                            anchors.centerIn: parent
                            text: "📊 收益曲线图表\n（需要集成QtCharts）"
                            font.pixelSize: 14
                            color: "#94A3B8"
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }
    }
    
    // ============ 组件定义 ============
    
    // 统计卡片组件
    component StatCard: Item {
        property string title: ""
        property string value: ""
        property string description: ""
        property string trend: "neutral"  // up, down, neutral
        
        Layout.preferredWidth: 120
        Layout.preferredHeight: 80
        
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#0F172A"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                
                Text {
                    text: title
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
                
                Row {
                    spacing: 6
                    
                    Text {
                        text: value
                        font.pixelSize: 18
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
    
    // 表格表头单元格
    component TableHeaderCell: Rectangle {
        property string text: ""
        
        width: 100
        height: 40
        color: "#0F172A"
        border.width: 1
        border.color: "#334155"
        
        Text {
            anchors.centerIn: parent
            text: parent.text
            font.pixelSize: 12
            font.weight: Font.Medium
            color: "#F1F5F9"
        }
    }
    
    // 表格单元格
    component TableCell: Rectangle {
        property string text: ""
        property color textColor: "#F1F5F9"
        
        width: 100
        height: 40
        color: "#0F172A"
        border.width: 1
        border.color: "#334155"
        
        Text {
            anchors.centerIn: parent
            text: parent.text
            font.pixelSize: 12
            color: parent.textColor
        }
    }
    
    // ============ 内部函数 ============
    
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
        
        console.log("更新回测结果:", {
            groupCount: groupResults.length,
            icirResult: icirResult,
            summaryStats: summaryStats
        })
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("回测结果视图初始化完成")
    }
}
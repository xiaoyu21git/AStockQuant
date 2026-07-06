// BacktestResultView.qml
// 回测结果展示组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtCharts 2.15
import "../../Backtest" as BacktestComponents

/**
 * 回测结果展示组件
 * 显示因子回测的详细结果和图表
 */
Item {
    id: root
    
    // ============ 属�?============
    
    property var backtestResult: ({})
    property var metricSections: ({})

    function executionMetrics() {
        return metricSections && metricSections.execution ? metricSections.execution : ({})
    }

    function icMetrics() {
        return metricSections && metricSections.ic ? metricSections.ic : ({})
    }

    function groupMetrics() {
        return metricSections && metricSections.groups && Array.isArray(metricSections.groups) ? metricSections.groups : []
    }
    
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
                    BacktestComponents.BacktestMetricCard {
                        title: "执行年化"
                        value: root.metricPercentText(executionMetrics().annualReturn, 2)
                        description: "Execution Annual Return"
                        trend: root.metricTrend(executionMetrics().annualReturn)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true
                        Layout.minimumWidth: 100
                    }
                    
                    // 夏普比率
                    BacktestComponents.BacktestMetricCard {
                        title: "夏普比率"
                        value: root.metricNumberText(executionMetrics().sharpeRatio, 2)
                        description: "Sharpe Ratio"
                        trend: root.metricTrend(executionMetrics().sharpeRatio)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }
                    
                    // 最大回撤
                    BacktestComponents.BacktestMetricCard {
                        title: "最大回撤"
                        value: root.metricPercentText(executionMetrics().maxDrawdown, 2)
                        description: "Max Drawdown"
                        trend: "down"
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }
                    
                    // 胜率
                    BacktestComponents.BacktestMetricCard {
                        title: "胜率"
                        value: root.metricPercentText(executionMetrics().winRate, 1)
                        description: "Win Rate"
                        trend: root.metricTrend(executionMetrics().winRate - 0.5)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }
                }
            }

            // 基准对比指标卡片
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 12
                color: "#1E293B"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    BacktestComponents.BacktestMetricCard {
                        title: "基准年化"
                        value: root.metricPercentText(executionMetrics().benchmarkAnnualReturn, 2)
                        description: "Benchmark Return"
                        trend: root.metricTrend(executionMetrics().benchmarkAnnualReturn)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }

                    BacktestComponents.BacktestMetricCard {
                        title: "超额年化"
                        value: root.metricPercentText(executionMetrics().excessAnnualReturn, 2)
                        description: "Excess Return"
                        trend: root.metricTrend(executionMetrics().excessAnnualReturn)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }

                    BacktestComponents.BacktestMetricCard {
                        title: "信息比率"
                        value: root.metricNumberText(executionMetrics().informationRatio, 2)
                        description: "Information Ratio"
                        trend: root.metricTrend(executionMetrics().informationRatio)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }

                    BacktestComponents.BacktestMetricCard {
                        title: "跟踪误差"
                        value: root.metricPercentText(executionMetrics().trackingError, 2)
                        description: "Tracking Error"
                        trend: "neutral"
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }

                    BacktestComponents.BacktestMetricCard {
                        title: "Alpha"
                        value: root.metricPercentText(executionMetrics().alpha, 2)
                        description: "CAPM Alpha"
                        trend: root.metricTrend(executionMetrics().alpha)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }

                    BacktestComponents.BacktestMetricCard {
                        title: "Beta"
                        value: root.metricNumberText(executionMetrics().beta, 2)
                        description: "Benchmark Beta"
                        trend: "neutral"
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }
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
                    
                    // IC指标
                    BacktestComponents.BacktestMetricCard {
                        title: "IC"
                        value: root.metricNumberText(icMetrics().value, 3)
                        description: "Information Coefficient"
                        trend: root.metricTrend(icMetrics().value)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }
                    
                    // IR指标
                    BacktestComponents.BacktestMetricCard {
                        title: "IR"
                        value: root.metricNumberText(icMetrics().ir, 2)
                        description: "Information Ratio"
                        trend: root.metricTrend(icMetrics().ir)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }
                    
                    // IC标准差
                    BacktestComponents.BacktestMetricCard {
                        title: "IC标准差"
                        value: root.metricNumberText(icMetrics().std, 3)
                        description: "IC Std Dev"
                        trend: "neutral"
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }
                    
                    // IC正率
                    BacktestComponents.BacktestMetricCard {
                        title: "IC正率"
                        value: root.metricPercentText(icMetrics().positiveRate, 1)
                        description: "IC Positive Rate"
                        trend: root.metricTrend(icMetrics().positiveRate - 0.5)
                        upColor: "#EF4444"
                        downColor: "#10B981"
                        cardHeight: 80
                        Layout.fillWidth: true; Layout.minimumWidth: 80
                    }
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
                        model: groupMetrics()
                        clip: true
                        
                        header: Row {
                            width: parent.width
                            height: 40
                            spacing: 0
                            
                            // 表头
                            TableHeaderCell { text: "组别"; width: 80 }
                            TableHeaderCell { text: "股票数量"; width: 80 }
                            TableHeaderCell { text: "收益"; width: 100 }
                            TableHeaderCell { text: "年化收益"; width: 100 }
                            TableHeaderCell { text: "最小因子值"; width: 100 }
                            TableHeaderCell { text: "最大因子值"; width: 100 }
                        }
                        
                        delegate: Row {
                            width: parent.width
                            height: 40
                            spacing: 0
                            
                            // 组别
                            TableCell {
                                width: 80
                                text: "组 " + (modelData.groupIndex || (index + 1))
                                color: index === 0 ? "#EF4444" : index === groupMetrics().length - 1 ? "#10B981" : "#F1F5F9"
                            }
                            
                            // 股票数量
                            TableCell {
                                width: 80
                                text: root.metricIntegerText(modelData.stockCount)
                            }
                            
                            // 收益
                            TableCell {
                                width: 100
                                text: root.metricPercentText(modelData.annualizedReturn, 2)
                                color: root.metricColor(modelData.annualizedReturn)
                            }
                            
                            // 波动�?
                            TableCell {
                                width: 100
                                text: root.metricPercentText(modelData.annualizedReturn, 2)
                            }
                            
                            // 夏普比率
                            TableCell {
                                width: 100
                                text: root.metricNumberText(modelData.minFactorValue, 2)
                                color: "#F1F5F9"
                            }
                            
                            // 最大回�?
                            TableCell {
                                width: 100
                                text: root.metricNumberText(modelData.maxFactorValue, 2)
                                color: "#F1F5F9"
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
                        groupResults: root.groupMetrics()
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
                    
                    // 图表占位�?
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
    
    // 表格表头单元�?
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
    
    // 表格单元�?
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

    function hasFactorContext() {
        if (backtestResult && String(backtestResult.factorId || "").length > 0) {
            return true
        }
        var config = backtestResult && backtestResult.config ? backtestResult.config : ({})
        return String(config.factorId || "").length > 0
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

    function metricIntegerText(value) {
        if (!hasFactorContext()) {
            return "N/A"
        }
        if (!hasNumericMetricValue(value)) {
            return "0"
        }
        return String(Math.round(Number(value)))
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

    function metricColor(value) {
        var trend = metricTrend(value)
        if (trend === "up") {
            return "#EF4444"
        }
        if (trend === "down") {
            return "#10B981"
        }
        return "#F1F5F9"
    }
    
    // ============ 内部函数 ============
    
    // 更新结果
    function updateResults(result) {
        backtestResult = result
        metricSections = result && result.metrics ? result.metrics : ({})
        
        console.log("更新回测结果:", {
            groupCount: groupMetrics().length,
            metricSections: metricSections
        })
    }
    
    // ============ 初始�?============
    
    Component.onCompleted: {
        console.log("回测结果视图初始化完成")
    }
}

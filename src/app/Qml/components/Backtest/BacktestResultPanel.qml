import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtCharts 2.15

Rectangle {
    id: root

    property var backtestResult: ({})
    property var chartDateLabels: []
    property real portfolioAxisMin: 0
    property real portfolioAxisMax: 1
    property real drawdownAxisMin: -1
    property real drawdownAxisMax: 0
    property bool portfolioTooltipVisible: false
    property string portfolioTooltipText: ""
    property real portfolioTooltipX: 0
    property real portfolioTooltipY: 0
    property bool drawdownTooltipVisible: false
    property string drawdownTooltipText: ""
    property real drawdownTooltipX: 0
    property real drawdownTooltipY: 0
    property string strategyDisplayName: ""
    property bool isBacktesting: false

    property alias netValueSeries: netValueSeries
    property alias portfolioBoundarySeries: portfolioBoundarySeries
    property alias portfolioPeakSeries: portfolioPeakSeries
    property alias drawdownSeries: drawdownSeries
    property alias drawdownBoundarySeries: drawdownBoundarySeries
    property alias drawdownTroughSeries: drawdownTroughSeries
    property alias portfolioChartView: portfolioChartView
    property alias drawdownChartView: drawdownChartView
    property alias detailedResultsDialog: detailedResultsDialog

    signal optimizationRequested()
    signal exportRequested()

    Layout.fillWidth: true
    implicitHeight: resultContent.implicitHeight + 32
    Layout.preferredHeight: implicitHeight
    radius: 12
    color: "#1E293B"

    onBacktestResultChanged: refreshTimeSeriesCharts()

    ColumnLayout {
        id: resultContent
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            spacing: 8

            Text {
                text: "📊 策略回测结果"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.resultStatusText()
                font.pixelSize: 12
                color: root.resultStatusColor()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 184
            columns: 3
            columnSpacing: 12
            rowSpacing: 12

            BacktestMetricCard {
                title: "总收益"
                value: root.formatPercent(root.backtestResult.totalReturn, 1)
                description: "Total Return"
                trend: root.backtestResult.totalReturn > 0 ? "up" : (root.backtestResult.totalReturn < 0 ? "down" : "neutral")
            }

            BacktestMetricCard {
                title: "年化收益"
                value: root.formatPercent(root.backtestResult.annualReturn, 1)
                description: "Annual Return"
                trend: root.backtestResult.annualReturn > 0 ? "up" : (root.backtestResult.annualReturn < 0 ? "down" : "neutral")
            }

            BacktestMetricCard {
                title: "夏普比率"
                value: root.formatNumber(root.backtestResult.sharpeRatio, 2)
                description: "Sharpe Ratio"
                trend: root.backtestResult.sharpeRatio > 0 ? "up" : (root.backtestResult.sharpeRatio < 0 ? "down" : "neutral")
            }

            BacktestMetricCard {
                title: "最大回撤"
                value: root.formatPercent(root.backtestResult.maxDrawdown, 1)
                description: "Max Drawdown"
                trend: "down"
            }

            BacktestMetricCard {
                title: "胜率"
                value: root.formatPercent(root.backtestResult.winRate, 1)
                description: "Win Rate"
                trend: root.backtestResult.winRate > 0.5 ? "up" : (root.backtestResult.winRate < 0.5 ? "down" : "neutral")
            }

            BacktestMetricCard {
                title: "盈亏比"
                value: root.formatNumber(root.backtestResult.profitLossRatio, 2)
                description: "Profit/Loss Ratio"
                trend: root.backtestResult.profitLossRatio > 1 ? "up" : (root.backtestResult.profitLossRatio < 1 ? "down" : "neutral")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.hasPortfolioContext() ? 92 : 0
            radius: 8
            color: "#0F172A"
            visible: root.hasPortfolioContext()

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    text: "🧩 组合回测上下文"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8

                    BacktestStatItem {
                        label: "组合名称"
                        value: root.backtestResult.portfolioName || root.strategyDisplayName || "--"
                    }

                    BacktestStatItem {
                        label: "因子数量"
                        value: root.backtestResult.portfolioFactorCount || "--"
                    }

                    BacktestStatItem {
                        label: "来源"
                        value: root.backtestResult.portfolioSource || "--"
                    }

                    BacktestStatItem {
                        label: "因子列表"
                        value: root.backtestResult.portfolioFactorIds || "--"
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.hasExecutionRiskContext() ? 92 : 0
            radius: 8
            color: "#0F172A"
            visible: root.hasExecutionRiskContext()

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    text: "🛡️ 本次执行参数"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8

                    BacktestStatItem {
                        label: "止损"
                        value: root.formatPercent(root.backtestResult.executionStopLossRate, 1)
                    }

                    BacktestStatItem {
                        label: "止盈"
                        value: root.formatPercent(root.backtestResult.executionTakeProfitRate, 1)
                    }

                    BacktestStatItem {
                        label: "最大回撤限制"
                        value: root.formatPercent(root.backtestResult.executionMaxDrawdownLimit, 1)
                    }

                    BacktestStatItem {
                        label: "调仓周期"
                        value: root.formatInteger(root.backtestResult.executionRebalanceFrequency)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.hasRuleTemplateSummary()
                ? (132
                    + Math.min(root.ruleTemplateGroupDecisions().length, 4) * 26
                    + (root.ruleTemplateGroupDecisions().length > 0 ? 38 : 0)
                    + Math.min(root.ruleTemplateRecentEvents().length, 3) * 24)
                : 0
            radius: 8
            color: "#0F172A"
            visible: root.hasRuleTemplateSummary()

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "📏 规则模板命中"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: (root.backtestResult.ruleTemplateSummary || {}).statusText || ""
                        font.pixelSize: 11
                        color: "#94A3B8"
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8

                    BacktestStatItem {
                        label: "规则文件"
                        value: root.backtestResult.ruleTemplateFileName || "--"
                    }

                    BacktestStatItem {
                        label: "命名空间"
                        value: (root.backtestResult.ruleTemplateSummary || {}).templateNamespace || "--"
                    }

                    BacktestStatItem {
                        label: "所属规则组"
                        value: root.ruleTemplateGroupText(root.backtestResult.ruleTemplateSummary || ({}))
                    }

                    BacktestStatItem {
                        label: "组合语义"
                        value: root.ruleTemplateGroupLogicText(root.backtestResult.ruleTemplateSummary || ({}))
                    }

                    BacktestStatItem {
                        label: "开仓阻断"
                        value: root.formatInteger(root.backtestResult.ruleTemplateEntryBlockCount)
                    }

                    BacktestStatItem {
                        label: "规则退出"
                        value: root.formatInteger(root.backtestResult.ruleTemplateForcedExitCount)
                    }
                }

                Repeater {
                    model: root.ruleTemplateGroupDecisions().slice(0, 4)

                    delegate: Text {
                        Layout.fillWidth: true
                        text: root.ruleTemplateGroupDecisionText(modelData)
                        font.pixelSize: 11
                        color: "#FDE68A"
                        wrapMode: Text.WordWrap
                        visible: text.length > 0
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.ruleTemplateGroupDecisions().length > 0
                    text: "最近命中事件"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: "#94A3B8"
                }

                Repeater {
                    model: root.ruleTemplateRecentEvents().slice(-3)

                    delegate: Text {
                        Layout.fillWidth: true
                        text: root.ruleTemplateEventText(modelData)
                        font.pixelSize: 11
                        color: "#CBD5E1"
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 104
            radius: 8
            color: "#0F172A"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    text: "📈 交易统计"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: 3
                    columnSpacing: 16
                    rowSpacing: 8

                    BacktestStatItem {
                        label: "总交易次数"
                        value: root.backtestResult.totalTrades || 0
                    }

                    BacktestStatItem {
                        label: "盈利交易"
                        value: root.backtestResult.winningTrades || 0
                        valueColor: "#EF4444"
                    }

                    BacktestStatItem {
                        label: "亏损交易"
                        value: root.backtestResult.losingTrades || 0
                        valueColor: "#10B981"
                    }

                    BacktestStatItem {
                        label: "平均盈利"
                        value: root.formatPercent(root.backtestResult.averageWin, 2)
                        valueColor: "#EF4444"
                    }

                    BacktestStatItem {
                        label: "平均亏损"
                        value: root.formatPercent(root.backtestResult.averageLoss, 2)
                        valueColor: "#10B981"
                    }

                    BacktestStatItem {
                        label: "交易天数"
                        value: root.backtestResult.tradingDays || 0
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 228
            radius: 8
            color: "#0F172A"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "📉 净值曲线"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.chartDateLabels.length > 0
                            ? (root.chartDateLabels[0] + " ~ " + root.chartDateLabels[root.chartDateLabels.length - 1])
                            : "等待回测结果"
                        font.pixelSize: 11
                        color: "#94A3B8"
                    }
                }

                ChartView {
                    id: portfolioChartView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    antialiasing: true
                    legend.visible: false
                    backgroundColor: "transparent"
                    plotAreaColor: "transparent"

                    ValueAxis {
                        id: portfolioXAxis
                        min: 0
                        max: Math.max(1, netValueSeries.count > 0 ? netValueSeries.count - 1 : 1)
                        tickCount: Math.min(6, Math.max(2, netValueSeries.count > 1 ? 6 : 2))
                        labelsColor: "#64748B"
                        gridLineColor: "#1E293B"
                        lineVisible: false
                    }

                    ValueAxis {
                        id: portfolioYAxis
                        min: root.portfolioAxisMin
                        max: root.portfolioAxisMax
                        tickCount: 5
                        labelsColor: "#94A3B8"
                        gridLineColor: "#1E293B"
                        labelFormat: "%.0f"
                    }

                    LineSeries {
                        id: netValueSeries
                        name: "净值"
                        color: "#38BDF8"
                        width: 2
                        axisX: portfolioXAxis
                        axisY: portfolioYAxis
                    }

                    ScatterSeries {
                        id: portfolioBoundarySeries
                        axisX: portfolioXAxis
                        axisY: portfolioYAxis
                        color: "#E2E8F0"
                        borderColor: "#0F172A"
                        markerSize: 9
                    }

                    ScatterSeries {
                        id: portfolioPeakSeries
                        axisX: portfolioXAxis
                        axisY: portfolioYAxis
                        color: "#10B981"
                        borderColor: "#0F172A"
                        markerSize: 10
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onExited: root.portfolioTooltipVisible = false
                        onPositionChanged: function(mouse) {
                            root.updateChartTooltip(portfolioChartView, netValueSeries, mouse.x, mouse.y, false)
                        }
                    }

                    Rectangle {
                        visible: root.portfolioTooltipVisible
                        x: Math.min(Math.max(0, root.portfolioTooltipX), portfolioChartView.width - width)
                        y: Math.min(Math.max(0, root.portfolioTooltipY), portfolioChartView.height - height)
                        width: portfolioTooltipLabel.implicitWidth + 16
                        height: portfolioTooltipLabel.implicitHeight + 10
                        radius: 6
                        color: "#020617"
                        border.width: 1
                        border.color: "#334155"
                        opacity: 0.96

                        Text {
                            id: portfolioTooltipLabel
                            anchors.centerIn: parent
                            text: root.portfolioTooltipText
                            font.pixelSize: 11
                            color: "#F8FAFC"
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: root.chartDateLabels.length > 0 ? root.chartDateLabels[0] : "--"
                        font.pixelSize: 11
                        color: "#64748B"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.chartDateLabels.length > 2 ? root.chartDateLabels[Math.floor((root.chartDateLabels.length - 1) / 2)] : ""
                        font.pixelSize: 11
                        color: "#64748B"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: root.chartDateLabels.length > 0 ? root.chartDateLabels[root.chartDateLabels.length - 1] : "--"
                        font.pixelSize: 11
                        color: "#64748B"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: "● 起点/终点"
                        font.pixelSize: 11
                        color: "#E2E8F0"
                    }

                    Text {
                        text: "● 峰值"
                        font.pixelSize: 11
                        color: "#10B981"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "悬浮查看当日净值"
                        font.pixelSize: 11
                        color: "#64748B"
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 196
            radius: 8
            color: "#0F172A"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "📊 回撤曲线"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: netValueSeries.count > 0 ? ("数据点: " + netValueSeries.count) : "等待回测结果"
                        font.pixelSize: 11
                        color: "#94A3B8"
                    }
                }

                ChartView {
                    id: drawdownChartView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    antialiasing: true
                    legend.visible: false
                    backgroundColor: "transparent"
                    plotAreaColor: "transparent"

                    ValueAxis {
                        id: drawdownXAxis
                        min: 0
                        max: Math.max(1, drawdownSeries.count > 0 ? drawdownSeries.count - 1 : 1)
                        tickCount: Math.min(6, Math.max(2, drawdownSeries.count > 1 ? 6 : 2))
                        labelsColor: "#64748B"
                        gridLineColor: "#1E293B"
                        lineVisible: false
                    }

                    ValueAxis {
                        id: drawdownYAxis
                        min: root.drawdownAxisMin
                        max: root.drawdownAxisMax
                        tickCount: 5
                        labelsColor: "#94A3B8"
                        gridLineColor: "#1E293B"
                        labelFormat: "%.2f"
                    }

                    LineSeries {
                        id: drawdownSeries
                        name: "回撤"
                        color: "#F59E0B"
                        width: 2
                        axisX: drawdownXAxis
                        axisY: drawdownYAxis
                    }

                    ScatterSeries {
                        id: drawdownBoundarySeries
                        axisX: drawdownXAxis
                        axisY: drawdownYAxis
                        color: "#E2E8F0"
                        borderColor: "#0F172A"
                        markerSize: 9
                    }

                    ScatterSeries {
                        id: drawdownTroughSeries
                        axisX: drawdownXAxis
                        axisY: drawdownYAxis
                        color: "#EF4444"
                        borderColor: "#0F172A"
                        markerSize: 10
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onExited: root.drawdownTooltipVisible = false
                        onPositionChanged: function(mouse) {
                            root.updateChartTooltip(drawdownChartView, drawdownSeries, mouse.x, mouse.y, true)
                        }
                    }

                    Rectangle {
                        visible: root.drawdownTooltipVisible
                        x: Math.min(Math.max(0, root.drawdownTooltipX), drawdownChartView.width - width)
                        y: Math.min(Math.max(0, root.drawdownTooltipY), drawdownChartView.height - height)
                        width: drawdownTooltipLabel.implicitWidth + 16
                        height: drawdownTooltipLabel.implicitHeight + 10
                        radius: 6
                        color: "#020617"
                        border.width: 1
                        border.color: "#334155"
                        opacity: 0.96

                        Text {
                            id: drawdownTooltipLabel
                            anchors.centerIn: parent
                            text: root.drawdownTooltipText
                            font.pixelSize: 11
                            color: "#F8FAFC"
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: "● 起点/终点"
                        font.pixelSize: 11
                        color: "#E2E8F0"
                    }

                    Text {
                        text: "● 最大回撤"
                        font.pixelSize: 11
                        color: "#EF4444"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "悬浮查看当日回撤"
                        font.pixelSize: 11
                        color: "#64748B"
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 120
                Layout.fillHeight: true
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
                    enabled: root.hasBacktestResult()
                    onClicked: {
                        if (root.hasBacktestResult()) {
                            detailedResultsDialog.open()
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 100
                Layout.fillHeight: true
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
                    onClicked: root.optimizationRequested()
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 100
                Layout.fillHeight: true
                radius: 6
                color: "#3B82F6"
                enabled: root.backtestResult && Object.keys(root.backtestResult).length > 0
                opacity: enabled ? 1.0 : 0.5

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
                    enabled: parent.enabled
                    onClicked: root.exportRequested()
                }
            }
        }
    }

    Dialog {
        id: detailedResultsDialog
        modal: true
        focus: true
        width: Math.min(root.width * 0.78, 980)
        height: Math.min(root.height * 0.82, 760)
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0

        background: Rectangle {
            radius: 12
            color: "#0F172A"
            border.width: 1
            border.color: "#334155"
        }

        header: Rectangle {
            height: 56
            color: "#111827"
            radius: 12

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20

                Text {
                    text: "策略回测详细结果"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#F8FAFC"
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: root.strategyDisplayName || "未命名策略"
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
            }
        }

        contentItem: ScrollView {
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                width: detailedResultsDialog.availableWidth
                spacing: 16
                padding: 20

                Repeater {
                    model: root.buildDetailedResultSections()

                    delegate: Rectangle {
                        width: parent.width
                        radius: 10
                        color: "#111827"
                        border.width: 1
                        border.color: "#1F2937"
                        implicitHeight: sectionColumn.implicitHeight + 24

                        property var sectionData: modelData

                        Column {
                            id: sectionColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            spacing: 10

                            Text {
                                text: sectionData.title
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: "#F8FAFC"
                            }

                            GridLayout {
                                width: parent.width
                                columns: 2
                                columnSpacing: 12
                                rowSpacing: 8

                                Repeater {
                                    model: sectionData.items

                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 52
                                        radius: 8
                                        color: "#0B1220"

                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 4

                                            Text {
                                                text: modelData.label
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                            }

                                            Text {
                                                text: modelData.value
                                                font.pixelSize: 14
                                                font.weight: Font.Medium
                                                color: "#E2E8F0"
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function hasBacktestResult() {
        return root.backtestResult && Object.keys(root.backtestResult).length > 0
    }

    function hasValue(value) {
        return value !== undefined && value !== null && value !== ""
    }

    function hasPortfolioContext() {
        return hasValue(root.backtestResult.portfolioSource)
            || Number(root.backtestResult.portfolioFactorCount || 0) > 0
            || hasValue(root.backtestResult.portfolioFactorIds)
    }

    function hasExecutionRiskContext() {
        return hasValue(root.backtestResult.executionStopLossRate)
            || hasValue(root.backtestResult.executionTakeProfitRate)
            || hasValue(root.backtestResult.executionMaxDrawdownLimit)
            || hasValue(root.backtestResult.executionRebalanceFrequency)
    }

    function hasRuleTemplateSummary() {
        var summary = root.backtestResult.ruleTemplateSummary || {}
        return !!summary.hasTemplate || Number(summary.triggeredCount || 0) > 0
    }

    function ruleTemplateGroupDecisions() {
        var summary = root.backtestResult.ruleTemplateSummary || {}
        return summary.latestGroupDecisions instanceof Array ? summary.latestGroupDecisions : []
    }

    function ruleTemplateRecentEvents() {
        var summary = root.backtestResult.ruleTemplateSummary || {}
        return summary.recentEvents instanceof Array ? summary.recentEvents : []
    }

    function resultStatusText() {
        if (root.isBacktesting) {
            return "回测中..."
        }
        if (hasBacktestResult()) {
            return "策略: " + (root.strategyDisplayName || "未命名策略")
        }
        return "请配置策略进行回测"
    }

    function resultStatusColor() {
        if (root.isBacktesting) {
            return "#F59E0B"
        }
        if (hasBacktestResult()) {
            return "#3B82F6"
        }
        return "#94A3B8"
    }

    function formatPercent(value, decimals) {
        if (!hasValue(value) || isNaN(Number(value))) {
            return "--"
        }

        return (Number(value) * 100).toFixed(decimals === undefined ? 2 : decimals) + "%"
    }

    function formatNumber(value, decimals) {
        if (!hasValue(value) || isNaN(Number(value))) {
            return "--"
        }

        return Number(value).toFixed(decimals === undefined ? 2 : decimals)
    }

    function formatInteger(value) {
        if (!hasValue(value) || isNaN(Number(value))) {
            return "--"
        }

        return Math.round(Number(value)).toString()
    }

    function ruleTemplateEventText(event) {
        if (!event) {
            return ""
        }

        var fragments = []
        if (hasValue(event.timestamp)) {
            fragments.push(String(event.timestamp))
        }
        if (hasValue(event.symbol)) {
            fragments.push(String(event.symbol))
        }
        var groupText = ruleTemplateGroupText(event)
        if (groupText !== "--") {
            fragments.push("规则组 " + groupText)
        }

        var actionText = event.eventType === "forced_exit" ? "触发退出" : "阻断开仓"
        if (hasValue(event.ruleId)) {
            actionText += " · " + String(event.ruleId)
        }
        if (hasValue(event.reasonCode)) {
            actionText += " · " + String(event.reasonCode)
        } else if (hasValue(event.message)) {
            actionText += " · " + String(event.message)
        }
        fragments.push(actionText)
        return fragments.join("  ")
    }

    function ruleTemplateGroupText(source) {
        var payload = source || {}
        var title = String(payload.groupTitle || payload.group_title || "").trim()
        var role = String(payload.groupRole || payload.group_role || "").trim()
        if (title.length > 0 && role.length > 0) {
            return title + " / " + role
        }
        if (title.length > 0) {
            return title
        }
        if (role.length > 0) {
            return role
        }
        var groupId = String(payload.groupId || payload.group_id || "").trim()
        return groupId.length > 0 ? groupId : "--"
    }

    function ruleTemplateGroupLogicText(source) {
        var payload = source || {}
        var operator = String(payload.groupOperator || payload.group_operator || "").trim().toLowerCase()
        if (operator === "all") {
            return "组内全部满足"
        }
        if (operator === "any") {
            return "组内任一满足"
        }
        if (operator === "at_least") {
            var threshold = Number(payload.matchThreshold || payload.groupMatchThreshold || 0)
            return threshold > 0 ? ("组内至少命中 " + threshold + " 条") : "组内至少命中"
        }
        if (operator === "score_sum") {
            return "组内累计评分"
        }
        if (operator === "first_match") {
            return "按首个命中裁决"
        }
        return operator.length > 0 ? operator : "--"
    }

    function ruleTemplateGroupDecisionStatusText(decision) {
        var payload = decision || {}
        var disposition = String(payload.disposition || "").trim().toLowerCase()
        var outcome = String(payload.outcome || "").trim().toLowerCase()
        if (disposition === "skipped") {
            return "本轮跳过"
        }
        if (outcome === "matched") {
            return "纳入并命中"
        }
        if (outcome === "incomplete") {
            return "纳入但未齐"
        }
        return "纳入未命中"
    }

    function ruleTemplateGroupDecisionReasonText(decision) {
        var skipReason = String((decision || {}).skipReason || "").trim().toLowerCase()
        if (skipReason === "role_filtered") {
            return "role 与当前动作不匹配"
        }
        if (skipReason === "stage_filtered") {
            return "阶段与当前动作不匹配"
        }
        if (skipReason === "group_incomplete") {
            return "all 组未全部满足"
        }
        if (skipReason === "group_threshold_unmet") {
            var threshold = Number((decision || {}).matchThreshold || 0)
            return threshold > 0 ? ("at_least 组未达到 " + threshold + " 条") : "at_least 组未达阈值"
        }
        return skipReason.length > 0 ? skipReason : ""
    }

    function ruleTemplateGroupDecisionText(decision) {
        var payload = decision || {}
        var fragments = []
        if (hasValue(payload.stage)) {
            fragments.push("阶段 " + String(payload.stage))
        }
        var groupText = ruleTemplateGroupText(payload)
        if (groupText !== "--") {
            fragments.push("规则组 " + groupText)
        }
        var logicText = ruleTemplateGroupLogicText(payload)
        if (logicText !== "--") {
            fragments.push(logicText)
        }
        fragments.push(ruleTemplateGroupDecisionStatusText(payload))

        var applicableCount = Number(payload.applicableCount || 0)
        var memberCount = Number(payload.memberCount || 0)
        var matchedCount = Number(payload.matchedCount || 0)
        var filteredCount = Number(payload.filteredCount || 0)
        if (memberCount > 0) {
            fragments.push("纳入 " + applicableCount + "/" + memberCount)
        }
        if (matchedCount > 0) {
            fragments.push("命中 " + matchedCount)
        }
        if (filteredCount > 0) {
            fragments.push("过滤 " + filteredCount)
        }
        var threshold = Number(payload.matchThreshold || 0)
        if (threshold > 0) {
            fragments.push("阈值 " + threshold)
        }
        if (hasValue(payload.matchedRuleId)) {
            fragments.push("命中规则 " + String(payload.matchedRuleId))
        }
        if (hasValue(payload.matchedReasonCode)) {
            fragments.push("原因码 " + String(payload.matchedReasonCode))
        }
        if (payload.aggregatedScore !== undefined && payload.aggregatedScore !== null) {
            fragments.push("累计分 " + Number(payload.aggregatedScore).toFixed(2))
        }
        if (hasValue(payload.selectedBy)) {
            fragments.push("选取方式 " + String(payload.selectedBy))
        }
        var reasonText = ruleTemplateGroupDecisionReasonText(payload)
        if (reasonText.length > 0) {
            fragments.push(reasonText)
        }
        return fragments.join("  ")
    }

    function buildDetailedResultSections() {
        if (!hasBacktestResult()) {
            return []
        }

        var result = root.backtestResult || {}
        var performance = result.performance || {}
        var trades = result.trades || {}
        var risk = result.risk || {}
        var timeSeries = result.timeSeries || {}
        var dates = timeSeries.dates || []
        var portfolioValues = timeSeries.portfolioValues || []

        var sections = [
            {
                title: "组合上下文",
                items: [
                    { label: "组合名称", value: result.portfolioName || root.strategyDisplayName || "--" },
                    { label: "组合来源", value: result.portfolioSource || "--" },
                    { label: "因子数量", value: formatInteger(result.portfolioFactorCount) },
                    { label: "执行子类型", value: result.portfolioStrategySubtype || "--" },
                    { label: "因子列表", value: result.portfolioFactorIds || "--" }
                ]
            },
            {
                title: "执行参数",
                items: [
                    { label: "止损", value: formatPercent(result.executionStopLossRate, 2) },
                    { label: "止盈", value: formatPercent(result.executionTakeProfitRate, 2) },
                    { label: "最大回撤限制", value: formatPercent(result.executionMaxDrawdownLimit, 2) },
                    { label: "调仓周期", value: formatInteger(result.executionRebalanceFrequency) },
                    { label: "最大总仓位", value: formatPercent(result.executionMaxPositionRatio, 2) },
                    { label: "单标的仓位上限", value: formatPercent(result.executionMaxSinglePositionRatio, 2) }
                ]
            },
            {
                title: "绩效指标",
                items: [
                    { label: "总收益", value: formatPercent(performance.totalReturn, 2) },
                    { label: "年化收益", value: formatPercent(performance.annualizedReturn, 2) },
                    { label: "波动率", value: formatPercent(performance.volatility, 2) },
                    { label: "夏普比率", value: formatNumber(performance.sharpeRatio, 2) },
                    { label: "索提诺比率", value: formatNumber(performance.sortinoRatio, 2) },
                    { label: "卡尔玛比率", value: formatNumber(performance.calmarRatio, 2) },
                    { label: "最大回撤", value: formatPercent(Math.abs(Number(performance.maxDrawdown || 0)), 2) },
                    { label: "胜率", value: formatPercent(performance.winRate, 2) },
                    { label: "盈亏比", value: formatNumber(performance.profitFactor, 2) },
                    { label: "平均盈利", value: formatPercent(performance.averageWin, 2) },
                    { label: "平均亏损", value: formatPercent(performance.averageLoss, 2) },
                    { label: "Alpha", value: formatNumber(performance.alpha, 4) },
                    { label: "Beta", value: formatNumber(performance.beta, 4) },
                    { label: "信息比率", value: formatNumber(performance.informationRatio, 2) },
                    { label: "跟踪误差", value: formatNumber(performance.trackingError, 4) }
                ]
            },
            {
                title: "交易统计",
                items: [
                    { label: "总交易次数", value: formatInteger(trades.totalTrades) },
                    { label: "盈利交易", value: formatInteger(trades.winningTrades) },
                    { label: "亏损交易", value: formatInteger(trades.losingTrades) },
                    { label: "总盈利", value: formatNumber(trades.totalProfit, 2) },
                    { label: "总亏损", value: formatNumber(trades.totalLoss, 2) },
                    { label: "最大单笔盈利", value: formatNumber(trades.largestWin, 2) },
                    { label: "最大单笔亏损", value: formatNumber(trades.largestLoss, 2) },
                    { label: "平均持仓天数", value: formatNumber(trades.averageHoldingPeriod, 2) }
                ]
            },
            {
                title: "风险指标",
                items: [
                    { label: "VaR 95%", value: formatNumber(risk.var95, 4) },
                    { label: "CVaR 95%", value: formatNumber(risk.cvar95, 4) },
                    { label: "下行偏差", value: formatNumber(risk.downsideDeviation, 4) },
                    { label: "上行偏差", value: formatNumber(risk.upsideDeviation, 4) },
                    { label: "偏度", value: formatNumber(risk.skewness, 4) },
                    { label: "峰度", value: formatNumber(risk.kurtosis, 4) }
                ]
            },
            {
                title: "时间序列",
                items: [
                    { label: "数据点数量", value: formatInteger(dates.length) },
                    { label: "开始日期", value: dates.length > 0 ? dates[0] : "--" },
                    { label: "结束日期", value: dates.length > 0 ? dates[dates.length - 1] : "--" },
                    { label: "净值点数量", value: formatInteger(portfolioValues.length) },
                    { label: "起始组合价值", value: portfolioValues.length > 0 ? formatNumber(portfolioValues[0], 2) : "--" },
                    { label: "期末组合价值", value: portfolioValues.length > 0 ? formatNumber(portfolioValues[portfolioValues.length - 1], 2) : "--" }
                ]
            }
        ]

        if (hasRuleTemplateSummary()) {
            sections.splice(2, 0, {
                title: "规则模板",
                items: [
                    { label: "规则文件", value: result.ruleTemplateFileName || "--" },
                    { label: "命名空间", value: (result.ruleTemplateSummary || {}).templateNamespace || "--" },
                    { label: "所属规则组", value: ruleTemplateGroupText(result.ruleTemplateSummary || {}) },
                    { label: "组合语义", value: ruleTemplateGroupLogicText(result.ruleTemplateSummary || {}) },
                    { label: "最近裁决", value: ruleTemplateGroupDecisions().length > 0 ? ruleTemplateGroupDecisionText(ruleTemplateGroupDecisions()[0]) : "--" },
                    { label: "触发次数", value: formatInteger(result.ruleTemplateTriggeredCount) },
                    { label: "开仓阻断", value: formatInteger(result.ruleTemplateEntryBlockCount) },
                    { label: "规则退出", value: formatInteger(result.ruleTemplateForcedExitCount) },
                    { label: "最近命中", value: ruleTemplateEventText(result.ruleTemplateLatestEvent) || "--" }
                ]
            })
        }

        return sections
    }

    function calculateAxisBounds(values, fallbackMin, fallbackMax) {
        if (!values || values.length === 0) {
            return { min: fallbackMin, max: fallbackMax }
        }

        var minValue = Number(values[0])
        var maxValue = Number(values[0])
        for (var index = 1; index < values.length; ++index) {
            var currentValue = Number(values[index])
            if (isNaN(currentValue)) {
                continue
            }
            minValue = Math.min(minValue, currentValue)
            maxValue = Math.max(maxValue, currentValue)
        }

        if (minValue === maxValue) {
            var singlePadding = Math.max(Math.abs(minValue) * 0.05, 1)
            return {
                min: minValue - singlePadding,
                max: maxValue + singlePadding
            }
        }

        var padding = (maxValue - minValue) * 0.08
        return {
            min: minValue - padding,
            max: maxValue + padding
        }
    }

    function updateLineSeries(series, values) {
        if (!series) {
            return
        }

        series.clear()
        for (var index = 0; index < values.length; ++index) {
            var numericValue = Number(values[index])
            if (!isNaN(numericValue)) {
                series.append(index, numericValue)
            }
        }
    }

    function updateScatterSeries(series, points) {
        if (!series) {
            return
        }

        series.clear()
        for (var index = 0; index < points.length; ++index) {
            var point = points[index]
            if (!point) {
                continue
            }

            var xValue = Number(point.x)
            var yValue = Number(point.y)
            if (!isNaN(xValue) && !isNaN(yValue)) {
                series.append(xValue, yValue)
            }
        }
    }

    function findExtremePoint(values, preferMax) {
        if (!values || values.length === 0) {
            return null
        }

        var bestIndex = -1
        var bestValue = 0
        for (var index = 0; index < values.length; ++index) {
            var currentValue = Number(values[index])
            if (isNaN(currentValue)) {
                continue
            }
            if (bestIndex < 0 || (preferMax ? currentValue > bestValue : currentValue < bestValue)) {
                bestIndex = index
                bestValue = currentValue
            }
        }

        if (bestIndex < 0) {
            return null
        }

        return { x: bestIndex, y: bestValue }
    }

    function buildBoundaryPoints(values) {
        if (!values || values.length === 0) {
            return []
        }

        var points = []
        var firstValue = Number(values[0])
        if (!isNaN(firstValue)) {
            points.push({ x: 0, y: firstValue })
        }

        var lastIndex = values.length - 1
        var lastValue = Number(values[lastIndex])
        if (lastIndex > 0 && !isNaN(lastValue)) {
            points.push({ x: lastIndex, y: lastValue })
        }
        return points
    }

    function updateChartTooltip(chartView, series, mouseX, mouseY, usePercentValue) {
        if (!chartView || !series || series.count <= 0) {
            if (usePercentValue) {
                drawdownTooltipVisible = false
            } else {
                portfolioTooltipVisible = false
            }
            return
        }

        var point = chartView.mapToValue(Qt.point(mouseX, mouseY), series)
        var nearestIndex = Math.max(0, Math.min(series.count - 1, Math.round(point.x)))
        var dateLabel = chartDateLabels.length > nearestIndex ? chartDateLabels[nearestIndex] : ("点 " + (nearestIndex + 1))
        var valueLabel = usePercentValue
            ? formatPercent(series.at(nearestIndex).y, 2)
            : formatNumber(series.at(nearestIndex).y, 2)

        if (usePercentValue) {
            drawdownTooltipText = dateLabel + "\n回撤: " + valueLabel
            drawdownTooltipVisible = true
            drawdownTooltipX = mouseX + 12
            drawdownTooltipY = mouseY - 42
        } else {
            portfolioTooltipText = dateLabel + "\n净值: " + valueLabel
            portfolioTooltipVisible = true
            portfolioTooltipX = mouseX + 12
            portfolioTooltipY = mouseY - 42
        }
    }

    function refreshTimeSeriesCharts() {
        var timeSeries = root.backtestResult && root.backtestResult.timeSeries ? root.backtestResult.timeSeries : ({})
        var dates = timeSeries.dates || []
        var portfolioValues = timeSeries.portfolioValues || []
        var drawdowns = timeSeries.drawdowns || []

        chartDateLabels = dates

        var portfolioBounds = calculateAxisBounds(portfolioValues, 0, 1)
        portfolioAxisMin = portfolioBounds.min
        portfolioAxisMax = portfolioBounds.max

        var drawdownBounds = calculateAxisBounds(drawdowns, -1, 0)
        drawdownAxisMin = Math.min(drawdownBounds.min, -0.001)
        drawdownAxisMax = Math.max(drawdownBounds.max, 0)

        updateLineSeries(netValueSeries, portfolioValues)
        updateLineSeries(drawdownSeries, drawdowns)
        updateScatterSeries(portfolioBoundarySeries, buildBoundaryPoints(portfolioValues))
        updateScatterSeries(drawdownBoundarySeries, buildBoundaryPoints(drawdowns))

        var portfolioPeakPoint = findExtremePoint(portfolioValues, true)
        updateScatterSeries(portfolioPeakSeries, portfolioPeakPoint ? [portfolioPeakPoint] : [])

        var drawdownTroughPoint = findExtremePoint(drawdowns, false)
        updateScatterSeries(drawdownTroughSeries, drawdownTroughPoint ? [drawdownTroughPoint] : [])

        portfolioTooltipVisible = false
        drawdownTooltipVisible = false
    }
}
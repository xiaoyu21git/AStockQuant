// BacktestResultView.qml
// 回测结果展示组件 — 最终结果视图
// 上家: FactorBacktestPage (显式绑定 metricSections/displayedResult/isBacktesting/currentGroup)
// 吸收 GroupResultPanel (结果选择器 + 分组卡片列表), 保留指标卡/基准卡/IC卡/分组对比图
// ⚠️ QML 绑定陷阱: 函数调用内部读取不被依赖跟踪 → 数据访问一律用直接属性链
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtCharts 2.15
import "../../Backtest" as BacktestComponents

Rectangle {
    id: root
    Layout.fillWidth: true
    radius: 12
    color: "#0F172A"

    // ============ 属性 (由 FactorBacktestPage 显式绑定) ============
    // displayedResult = 当前选中的单条结果; 命名避开 backtestResult,
    // 以便结果选择器经作用域链解析到页面根的原始结果 (含 .results 列表)
    property var displayedResult: ({})
    property var metricSections: ({})
    property bool isBacktesting: false
    property int currentGroup: 0

    // ── 直接属性链数据视图 (绑定可跟踪, 结果切换时自动刷新) ──
    property var execMetrics: metricSections && metricSections.execution ? metricSections.execution : ({})
    property var icInfo: metricSections && metricSections.ic ? metricSections.ic : ({})
    property var groupList: metricSections && metricSections.groups && Array.isArray(metricSections.groups)
                            ? metricSections.groups : []
    property var rawRetSeries: metricSections && metricSections.factorQuality
                               && metricSections.factorQuality.rawReturns
                               && Array.isArray(metricSections.factorQuality.rawReturns)
                               ? metricSections.factorQuality.rawReturns : []
    property var resultChoices: {
        var touch = metricSections  // 结果切换时页面重设 resultMetrics → 触发重算
        return displayedBacktestResults()
    }
    // 因子归因有效性 (组合模式产出; 单因子模式显示多空收益小卡)
    property bool attributionValid: {
        var fa = metricSections && metricSections.factorAttribution
        return fa ? (fa.isValid === true) : false
    }
    // 指标上下文: 有因子结果或指标数据时显示数值, 否则 "N/A"
    property bool hasContext: {
        if (displayedResult && (String(displayedResult.factorId || "").length > 0
                                || String(displayedResult.factorName || "").length > 0)) return true
        return metricSections && Object.keys(metricSections).length > 0
    }

    // ============ 格式化工具 (纯函数, 入参直接属性链) ============
    function hasNumericMetricValue(value) {
        if (value === undefined || value === null) return false
        return isFinite(Number(value))
    }
    function metricNumberText(ctx, value, digits) {
        if (!ctx) return "N/A"
        if (!hasNumericMetricValue(value)) return Number(0).toFixed(digits)
        return Number(value).toFixed(digits)
    }
    function metricPercentText(ctx, value, digits) {
        if (!ctx) return "N/A"
        if (!hasNumericMetricValue(value)) return (Number(0) * 100).toFixed(digits) + "%"
        return (Number(value) * 100).toFixed(digits) + "%"
    }
    function metricIntegerText(ctx, value) {
        if (!ctx) return "N/A"
        if (!hasNumericMetricValue(value)) return "0"
        return String(Math.round(Number(value)))
    }
    function metricTrend(ctx, value) {
        if (!ctx || !hasNumericMetricValue(value)) return "neutral"
        var n = Number(value)
        if (n > 0) return "up"
        if (n < 0) return "down"
        return "neutral"
    }
    function pnlColor(v) {
        var n = Number(v)
        return isNaN(n) ? "#94A3B8" : (n >= 0 ? "#EF4444" : "#10B981")
    }

    // ============ UI ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // 标题行 + 结果选择器 (仅多结果时可见)
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "📊 回测结果详情"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: resultSelector
                Layout.preferredWidth: 220
                visible: resultChoices.length > 1
                model: resultChoices
                currentIndex: selectedBacktestResultIndex
                delegate: ItemDelegate {
                    width: resultSelector.width
                    text: displayedBacktestResultName(modelData)
                }
                contentItem: Text {
                    text: resultSelector.currentIndex >= 0 && resultSelector.currentIndex < resultChoices.length
                        ? displayedBacktestResultName(resultChoices[resultSelector.currentIndex])
                        : "选择回测结果"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    radius: 8
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                }
                onActivated: function(index) {
                    selectedBacktestResultIndex = index
                    // backtestResult 经作用域链解析到页面根的原始结果 (含 .results 列表)
                    applyDisplayedBacktestResult(backtestResult)
                }
            }
            Text {
                text: groupList.length > 0 ? "共 " + groupList.length + " 个分组" : "等待回测结果"
                font.pixelSize: 12
                color: "#94A3B8"
            }
        }

        // 执行指标卡: 年化 / 夏普 / 最大回撤 / 胜率
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
                    title: "执行年化"
                    value: root.metricPercentText(root.hasContext, root.execMetrics.annualReturn, 2)
                    description: "Execution Annual Return"
                    trend: root.metricTrend(root.hasContext, root.execMetrics.annualReturn)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 100
                }
                BacktestComponents.BacktestMetricCard {
                    title: "夏普比率"
                    value: root.metricNumberText(root.hasContext, root.execMetrics.sharpeRatio, 2)
                    description: "Sharpe Ratio"
                    trend: root.metricTrend(root.hasContext, root.execMetrics.sharpeRatio)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "最大回撤"
                    value: root.metricPercentText(root.hasContext, root.execMetrics.maxDrawdown, 2)
                    description: "Max Drawdown"
                    trend: "down"
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "胜率"
                    value: root.metricPercentText(root.hasContext, root.execMetrics.winRate, 1)
                    description: "Win Rate"
                    trend: root.metricTrend(root.hasContext, Number(root.execMetrics.winRate) - 0.5)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
            }
        }

        // 基准对比卡: 基准年化 / 超额年化 / 信息比率 / 跟踪误差 / Alpha / Beta
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
                    value: root.metricPercentText(root.hasContext, root.execMetrics.benchmarkAnnualReturn, 2)
                    description: "Benchmark Return"
                    trend: root.metricTrend(root.hasContext, root.execMetrics.benchmarkAnnualReturn)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "超额年化"
                    value: root.metricPercentText(root.hasContext, root.execMetrics.excessAnnualReturn, 2)
                    description: "Excess Return"
                    trend: root.metricTrend(root.hasContext, root.execMetrics.excessAnnualReturn)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "信息比率"
                    value: root.metricNumberText(root.hasContext, root.execMetrics.informationRatio, 2)
                    description: "Information Ratio"
                    trend: root.metricTrend(root.hasContext, root.execMetrics.informationRatio)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "跟踪误差"
                    value: root.metricPercentText(root.hasContext, root.execMetrics.trackingError, 2)
                    description: "Tracking Error"
                    trend: "neutral"
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "Alpha"
                    value: root.metricPercentText(root.hasContext, root.execMetrics.alpha, 2)
                    description: "CAPM Alpha"
                    trend: root.metricTrend(root.hasContext, root.execMetrics.alpha)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "Beta"
                    value: root.metricNumberText(root.hasContext, root.execMetrics.beta, 2)
                    description: "Benchmark Beta"
                    trend: "neutral"
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
            }
        }

        // IC 卡: IC / IR / IC标准差 / IC正率
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            radius: 12
            color: "#1E293B"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                BacktestComponents.BacktestMetricCard {
                    title: "IC"
                    value: root.metricNumberText(root.hasContext, root.icInfo.value, 3)
                    description: "Information Coefficient"
                    trend: root.metricTrend(root.hasContext, root.icInfo.value)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "IR"
                    value: root.metricNumberText(root.hasContext, root.icInfo.ir, 2)
                    description: "Information Ratio"
                    trend: root.metricTrend(root.hasContext, root.icInfo.ir)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "IC标准差"
                    value: root.metricNumberText(root.hasContext, root.icInfo.std, 3)
                    description: "IC Std Dev"
                    trend: "neutral"
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
                BacktestComponents.BacktestMetricCard {
                    title: "IC正率"
                    value: root.metricPercentText(root.hasContext, root.icInfo.positiveRate, 1)
                    description: "IC Positive Rate"
                    trend: root.metricTrend(root.hasContext, Number(root.icInfo.positiveRate) - 0.5)
                    upColor: "#EF4444"; downColor: "#10B981"
                    cardHeight: 80
                    Layout.fillWidth: true; Layout.minimumWidth: 80
                }
            }
        }

        // 分组卡片列表 (吸收 GroupResultPanel, 含"正在计算分组..."空态 + currentGroup 高亮)
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
                    text: "📊 分组内容"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }
                ListView {
                    id: groupListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 220
                    model: groupList
                    clip: true
                    spacing: 8
                    delegate: groupCard
                }
                Text {
                    anchors.centerIn: parent
                    text: isBacktesting ? "正在计算分组..." : "请开始回测查看分组内容"
                    font.pixelSize: 14
                    color: "#94A3B8"
                    visible: groupList.length === 0
                }
            }
        }

        // 分组绩效对比图 (GroupResultChart 保留)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 380
            radius: 12
            color: "#1E293B"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                GroupResultChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    groupResults: groupList
                }
            }
        }

        // 因子归因 (组合模式) / 单因子多空收益小卡 (单因子模式)
        FactorAttributionPanel {
            Layout.fillWidth: true
            visible: attributionValid
            report: attributionValid
                ? (metricSections.factorAttribution || null)
                : null
        }
        SingleFactorReturnCard {
            Layout.fillWidth: true
            visible: !attributionValid && rawRetSeries.length > 0
        }
    }

    // ============ 内联组件 ============
    // 分组卡片 (GroupResultPanel 委托原样吸收)
    component groupCard: Rectangle {
        width: ListView.view.width
        height: 60
        radius: 8
        color: "#1E293B"
        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 16
                color: "#0F172A"
                Text {
                    anchors.centerIn: parent
                    text: modelData.groupIndex || (index + 1)
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    color: "#F1F5F9"
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "第 " + (modelData.groupIndex || (index + 1)) + " 组"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: "#F1F5F9"
                }
                RowLayout {
                    spacing: 16
                    Text {
                        text: "股票: " + (isFinite(Number(modelData.stockCount)) ? Number(modelData.stockCount).toFixed(0) : "0")
                        font.pixelSize: 11
                        color: "#94A3B8"
                    }
                    Text {
                        text: "因子值: " + (isFinite(Number(modelData.minFactorValue)) ? Number(modelData.minFactorValue).toFixed(2) : "0.00")
                              + " - " + (isFinite(Number(modelData.maxFactorValue)) ? Number(modelData.maxFactorValue).toFixed(2) : "0.00")
                        font.pixelSize: 11
                        color: "#94A3B8"
                    }
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 2
                Text {
                    text: (isFinite(Number(modelData.returnRate)) ? (Number(modelData.returnRate) * 100).toFixed(2) + "%" : "0.00%")
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    color: pnlColor(modelData.returnRate)
                }
                Text {
                    text: "收益"
                    font.pixelSize: 10
                    color: "#94A3B8"
                }
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#3B82F620"
            border.width: 2
            border.color: "#3B82F6"
            visible: isBacktesting && currentGroup === (index + 1)
        }
    }

    // 单因子模式: 因子多空收益曲线小卡 (数据 = rawLongShortReturns, 纯 UI 绘制)
    component SingleFactorReturnCard: Rectangle {
        radius: 8
        color: "#1E293B"
        property var rawData: metricSections && metricSections.factorQuality
                             && metricSections.factorQuality.rawReturns
                             && Array.isArray(metricSections.factorQuality.rawReturns)
                             ? metricSections.factorQuality.rawReturns : []
        property double cardHeight: 240
        Layout.preferredHeight: cardHeight

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6
            Text {
                text: "因子多空收益（单因子模式·无成本口径）"
                font.pixelSize: 12
                font.weight: Font.Bold
                color: "#F1F5F9"
            }
            Text {
                text: "每期多空分组收益与算术累计曲线；组合模式将显示因子归因区块"
                font.pixelSize: 9
                color: "#64748B"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ChartView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                antialiasing: true
                legend.visible: true
                legend.font.pixelSize: 8
                legend.labelColor: "#94A3B8"
                backgroundColor: "transparent"
                plotAreaColor: "transparent"
                ValueAxis { id: sfX; min: 0; max: 1; labelsColor: "#64748B"; gridLineColor: "#1E293B"; labelFormat: "%.0f" }
                ValueAxis { id: sfY; labelsColor: "#94A3B8"; gridLineColor: "#1E293B"; labelFormat: "%.2f" }
                LineSeries { id: sfRaw; name: "每期多空收益"; axisX: sfX; axisY: sfY; color: "#38BDF8"; width: 1.5 }
                LineSeries { id: sfCum; name: "算术累计"; axisX: sfX; axisY: sfY; color: "#EF4444"; width: 2 }
            }
        }

        onRawDataChanged: refreshSingleFactor()
        Component.onCompleted: refreshSingleFactor()

        function refreshSingleFactor() {
            sfRaw.clear(); sfCum.clear()
            var cum = 0
            for (var i = 0; i < rawData.length; i++) {
                var r = Number(rawData[i])
                sfRaw.append(i, r)
                cum += isNaN(r) ? 0 : r
                sfCum.append(i, cum)
            }
            sfX.max = Math.max(1, rawData.length - 1)
        }
    }

    Component.onCompleted: {
        console.log("回测结果视图初始化完成")
    }
}

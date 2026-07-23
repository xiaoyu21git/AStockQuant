import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15
import AStock.Bridge 1.0

Item {
    id: root
    anchors.fill: parent

    property string selectedStrategyId: ""
    property string selectedStrategyName: ""
    property var selectedResult: null
    property int selectedRow: -1
    signal resultSelected(var result)
    function refreshPerformance() { perfModel.refresh() }
    property var equityDates: []
    property string statsMode: "monthly"
    property int eqZoomRange: 0

    function rebuildStatsChart() {
        statsChart.removeAllSeries()
        if (!selectedResult || !selectedResult.timeSeries) return
        var ts = selectedResult.timeSeries
        var pv = ts.portfolioValues || []; var dates = ts.dates || []
        if (pv.length < 2 || dates.length < 2) return
        var dailyReturns = []
        for (var i = 1; i < pv.length; i++)
            if (pv[i-1] > 0) dailyReturns.push({ d: dates[i], r: (pv[i]/pv[i-1]-1)*100 })
        var labels = [], values = []
        if (statsMode === "daily") {
            for (var j = 0; j < Math.min(dailyReturns.length, 60); j++)
                { labels.push(String(dates[j+1]||"").substring(0,8)); values.push(dailyReturns[j].r) }
        } else if (statsMode === "monthly") {
            var months = {}
            for (var m = 0; m < dailyReturns.length; m++) {
                var md = String(dailyReturns[m].d||"").substring(0,6)
                if (!months[md]) months[md] = { sum: 0, cnt: 0 }
                months[md].sum += dailyReturns[m].r; months[md].cnt++
            }
            for (var mk in months) { labels.push(mk); values.push(months[mk].sum) }
        } else if (statsMode === "yearly") {
            var years = {}
            for (var y = 0; y < dailyReturns.length; y++) {
                var yd = String(dailyReturns[y].d||"").substring(0,4)
                if (!years[yd]) years[yd] = { sum: 0, cnt: 0 }
                years[yd].sum += dailyReturns[y].r; years[yd].cnt++
            }
            for (var yk in years) { labels.push(yk); values.push(years[yk].sum) }
        }
        var bar = statsChart.createSeries(ChartView.SeriesTypeBar, "", statsX, statsY)
        if (bar) {
            bar.append("收益", values)
            if (bar.barSets && bar.barSets.length > 0) bar.barSets[0].color = "#DC2626"
        }
        statsX.categories = labels
    }

    onSelectedResultChanged: {
        equityLine.clear(); bmLine.clear(); equityDates = []
        if (!selectedResult || !selectedResult.timeSeries) return
        var ts = selectedResult.timeSeries
        var pv = ts.portfolioValues || []; var bv = ts.benchmarkValues || []
        var dates = ts.dates || []; equityDates = dates
        var base = pv.length > 0 && pv[0] > 0 ? pv[0] : 1
        var bmBase = bv.length > 0 && bv[0] > 0 ? bv[0] : 1
        for (var i = 0; i < pv.length; i++) equityLine.append(i, pv[i] / base)
        for (var j = 0; j < bv.length; j++) bmLine.append(j, bv[j] / bmBase)
        eqAxisX.min = 0; eqAxisX.max = Math.max(pv.length-1, 0)
        eqAxisY.min = 0.5; eqAxisY.max = 2.5
        rebuildStatsChart()
    }

    Component.onCompleted: {
        strategyCombo.model = StrategyBridge.listModel
        StrategyBridge.strategiesChanged.connect(function() {
            if (root.selectedStrategyId) perfModel.refresh()
        })
    }

    onSelectedStrategyIdChanged: {
        selectedRow = -1; selectedResult = null
        autoSelectTimer.start()
        scatterSeries.clear()
    }

    Timer { id: autoSelectTimer; interval: 200; onTriggered: {
        if (perfModel.count > 0 && selectedRow < 0) {
            selectedRow = 0; selectedResult = perfModel.loadResultDetail(0)
        }
    }}

    StrategyPerformanceModel {
        id: perfModel
        strategyId: root.selectedStrategyId
        onErrorOccurred: function(msg) { console.warn("绩效查询:", msg) }
        onCountChanged: { rebuildScatterChart() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // ── 左侧: 列表 ──
        ColumnLayout {
            Layout.preferredWidth: parent.width * 0.55
            Layout.fillHeight: true
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Text { text: "策略绩效"; font.pixelSize: 18; font.weight: Font.Black; color: "#F1F5F9" }
                Item { Layout.fillWidth: true }
                ComboBox {
                    id: strategyCombo; Layout.preferredWidth: 200
                    textRole: "name"; currentIndex: -1
                    displayText: currentIndex >= 0 ? currentText : "选择策略"
                    onActivated: {
                        var row = StrategyBridge.listModel.getRow(currentIndex)
                        if (row) { root.selectedStrategyId = row.strategyId || ""; root.selectedStrategyName = row.name || "" }
                    }
                }
                Rectangle {
                    width: 60; height: 28; radius: 6; color: "#3B82F6"
                    Text { anchors.centerIn: parent; text: "刷新"; font.pixelSize: 11; color: "white" }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: perfModel.refresh() }
                }
            }

            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                radius: 12; color: "#1E293B"; border.width: 1; border.color: "#334155"; clip: true
                ListView {
                    id: historyList; anchors.fill: parent; anchors.margins: 8
                    model: perfModel; spacing: 4
                    header: Row {
                        width: historyList.width
                        Text { width: 105; text: "回测时间"; font.pixelSize: 10; color: "#64748B" }
                        Text { width: 100; text: "数据区间"; font.pixelSize: 10; color: "#64748B" }
                        Text { width: 55; text: "年化"; font.pixelSize: 10; color: "#64748B" }
                        Text { width: 45; text: "夏普"; font.pixelSize: 10; color: "#64748B" }
                        Text { width: 50; text: "回撤"; font.pixelSize: 10; color: "#64748B" }
                        Text { width: 45; text: "胜率"; font.pixelSize: 10; color: "#64748B" }
                        Text { width: 50; text: "盈亏比"; font.pixelSize: 10; color: "#64748B" }
                        Text { width: 55; text: "因子"; font.pixelSize: 10; color: "#64748B" }
                        Text { width: 40; text: "持仓"; font.pixelSize: 10; color: "#64748B" }
                    }
                    delegate: Rectangle {
                        width: historyList.width; height: 30; radius: 6
                        color: root.selectedRow === index ? "#1E3A5F" : "transparent"
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: { root.selectedRow = index; root.selectedResult = perfModel.loadResultDetail(index); root.resultSelected(root.selectedResult) }
                        }
                        Row { anchors.verticalCenter: parent.verticalCenter
                            Text { width: 105; text: (runAt||"").substring(0,16); font.pixelSize: 11; color: "#CBD5E1" }
                            Text { width: 100; text: dateRange(parameters); font.pixelSize: 10; color: "#64748B" }
                            Text { width: 55; text: (annualizedReturn*100).toFixed(2)+"%"; font.pixelSize: 11; color: annualizedReturn>=0?"#EF4444":"#22C55E" }
                            Text { width: 45; text: sharpeRatio.toFixed(2); font.pixelSize: 11; color: "#F1F5F9" }
                            Text { width: 50; text: (maxDrawdown*100).toFixed(1)+"%"; font.pixelSize: 11; color: "#22C55E" }
                            Text { width: 45; text: (winRate*100).toFixed(1)+"%"; font.pixelSize: 11; color: "#F1F5F9" }
                            Text { width: 50; text: profitFactor.toFixed(2); font.pixelSize: 11; color: "#F1F5F9" }
                            Text { width: 55; text: perfModel.rowFactorNames(index); font.pixelSize: 9; color: "#93c5fd"; elide: Text.ElideRight }
                            Text { width: 40; text: parameters?(parameters.maxPositions||0):"-"; font.pixelSize: 10; color: "#CBD5E1" }
                        }
                    }
                }
            }
        }

        // ── 右侧: 图表 ──
        ColumnLayout {
            Layout.preferredWidth: parent.width * 0.45
            Layout.fillHeight: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: selectedResult ? 120 : 0
                radius: 12; color: "#1E293B"; border.width: 1; border.color: "#334155"
                visible: selectedResult !== null
                RowLayout {
                    id: detailRow; anchors.fill: parent; anchors.margins: 12; spacing: 14
                    Component.onCompleted: {
                        if (!selectedResult) return
                        var idx = perfModel.index(selectedRow, 0)
                        detailCard("夏普", perfModel.data(idx, StrategyPerformanceModel.SharpeRatioRole).toFixed(3), true)
                        detailCard("年化", (perfModel.data(idx, StrategyPerformanceModel.AnnualizedReturnRole)*100).toFixed(2)+"%", false)
                        detailCard("回撤", (perfModel.data(idx, StrategyPerformanceModel.MaxDrawdownRole)*100).toFixed(1)+"%", false)
                        detailCard("胜率", (perfModel.data(idx, StrategyPerformanceModel.WinRateRole)*100).toFixed(1)+"%", false)
                        detailCard("Sortino", perfModel.data(idx, StrategyPerformanceModel.SortinoRatioRole).toFixed(3), false)
                        detailCard("Calmar", perfModel.data(idx, StrategyPerformanceModel.CalmarRatioRole).toFixed(3), false)
                        detailCard("盈亏比", perfModel.data(idx, StrategyPerformanceModel.ProfitFactorRole).toFixed(2), false)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                radius: 12; color: "#1E293B"; border.width: 1; border.color: "#334155"
                ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 4
                    RowLayout {
                        Text { text: "净值走势"; font.pixelSize: 12; font.weight: Font.DemiBold; color: "#F1F5F9" }
                        Item { Layout.fillWidth: true }
                        Repeater {
                            model: [{l:"全部",r:0},{l:"1年",r:252},{l:"6月",r:126},{l:"3月",r:63}]
                            Rectangle {
                                width: lbl.implicitWidth+14; height: 22; radius: 5
                                color: eqZoomRange===modelData.r?"#3B82F6":"#334155"
                                Text { id: lbl; anchors.centerIn: parent; text: modelData.l; font.pixelSize: 10; color: "white" }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: { eqZoomRange=modelData.r; if(modelData.r===0)equityChart.zoomReset() }
                                }
                            }
                        }
                    }
                    ChartView {
                        id: equityChart; Layout.fillWidth: true; Layout.fillHeight: true
                        antialiasing: true; legend.visible: false
                        backgroundColor: "#F1F5F9"; plotAreaColor: "#F1F5F9"
                        ValueAxis { id: eqAxisX; labelsVisible: false; gridVisible: false }
                        ValueAxis { id: eqAxisY; labelsVisible: true; gridVisible: true; labelsColor: "#94A3B8"; gridLineColor: "#E2E8F0"; labelFormat: "%.2f" }
                        LineSeries { id: equityLine; color: "#DC2626"; width: 3 }
                        LineSeries { id: bmLine; color: "#0EA5E9"; width: 2; style: Qt.DashLine }
                        Text { anchors.centerIn: parent; text: selectedResult ? "" : "选择回测记录"; font.pixelSize: 14; color: "#94A3B8"; visible: !selectedResult }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                radius: 12; color: "#1E293B"; border.width: 1; border.color: "#334155"
                ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 4
                    RowLayout {
                        Text { text: "收益统计"; font.pixelSize: 12; font.weight: Font.DemiBold; color: "#F1F5F9" }
                        Item { Layout.fillWidth: true }
                        Repeater {
                            model: [{l:"按日",m:"daily"},{l:"按月",m:"monthly"},{l:"按年",m:"yearly"}]
                            Rectangle {
                                width: slbl.implicitWidth+14; height: 22; radius: 5
                                color: statsMode===modelData.m?"#3B82F6":"#334155"
                                Text { id: slbl; anchors.centerIn: parent; text: modelData.l; font.pixelSize: 10; color: "white" }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: { statsMode=modelData.m; rebuildStatsChart() }
                                }
                            }
                        }
                    }
                    ChartView {
                        id: statsChart; Layout.fillWidth: true; Layout.fillHeight: true
                        antialiasing: true; legend.visible: false
                        backgroundColor: "#F1F5F9"; plotAreaColor: "#F1F5F9"
                        BarCategoryAxis { id: statsX; labelsColor: "#64748B"; gridVisible: false; labelsFont.pixelSize: 9 }
                        ValueAxis { id: statsY; labelsVisible: false; gridVisible: false }
                    }
                }
            }
        }
    }

    // ── 回测收益散点图 ──
    Rectangle {
        Layout.fillWidth: true; Layout.fillHeight: true
        radius: 12; color: "#1E293B"; border.width: 1; border.color: "#334155"
        ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 4
            Text { text: "回测收益分布"; font.pixelSize: 12; font.weight: Font.DemiBold; color: "#F1F5F9" }
            ChartView {
                id: scatterChart; Layout.fillWidth: true; Layout.fillHeight: true
                antialiasing: true; legend.visible: false
                backgroundColor: "#1E293B"; plotAreaColor: "#1E293B"
                ValueAxis { id: scX; min: -1; labelsColor: "#64748B"; gridLineColor: "#334155"; labelFormat: "%.0f" }
                ValueAxis { id: scY; labelsColor: "#94A3B8"; gridLineColor: "#334155"; labelFormat: "%.1f" }
                ScatterSeries { id: scatterSeries; markerSize: 12; color: "#3B82F6"; borderColor: "#60A5FA" }
            }
        }
    }

    function rebuildScatterChart() {
        scatterSeries.clear()
        var records = []
        for (var i = 0; i < perfModel.count; i++) {
            var r = perfModel.loadResultDetail(i) || ({})
            var tr = Number(r.totalReturn || 0)
            var fn = perfModel.rowFactorNames(i)
            if (isFinite(tr)) records.push({ idx: i, y: tr * 100, label: fn || ("#" + (i+1)) })
        }
        if (records.length === 0) return
        var mx = -Infinity, mn = Infinity
        for (var j = 0; j < records.length; j++) {
            if (records[j].y > mx) mx = records[j].y; if (records[j].y < mn) mn = records[j].y
        }
        scX.min = -1; scX.max = records.length
        var pd = (mx - mn) * 0.1 || 5
        scY.min = Math.min(0, mn - pd); scY.max = mx + pd
        for (var k = 0; k < records.length; k++)
            scatterSeries.append(records[k].idx, records[k].y, records[k].label)
        for (var m = 0; m < scatterSeries.count; m++) {
            var p = scatterSeries.at(m)
            if (p.y >= 0) { p.color = "#EF4444"; p.borderColor = "#FCA5A5" }
            else { p.color = "#22C55E"; p.borderColor = "#86EFAC" }
        }
    }

    Component {
        id: detailCardComp
        Rectangle {
            property string cardLabel: ""; property string cardValue: ""; property bool cardEmphasize: false
            width: 90; height: 70; radius: 8
            color: cardEmphasize ? "#172235" : "#121A2B"
            border.width: 1; border.color: cardEmphasize ? "#FB923C" : "#243247"
            Column { anchors.centerIn: parent; spacing: 4
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: cardValue; font.pixelSize: 18; font.weight: Font.Black
                    color: cardEmphasize ? "#F97316" : "#F1F5F9" }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: cardLabel; font.pixelSize: 10; color: "#94A3B8" }
            }
        }
    }
    function dateRange(p) {
        if (!p) return "-"
        var s = (p.dataStartDate||"").toString().substring(0,10)
        var e = (p.dataEndDate||"").toString().substring(0,10)
        if (s.length < 4) return "-"
        return s + "~" + e
    }
    function detailCard(l, v, e) { return detailCardComp.createObject(detailRow, { cardLabel: l, cardValue: v, cardEmphasize: e }) }
}

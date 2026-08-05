import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: root

    readonly property color bg: "#0B1220"
    readonly property color panelBg: "#121A2B"
    readonly property color panelBorder: "#243247"
    readonly property color textPri: "#E5EEF8"
    readonly property color textSec: "#91A4BC"
    readonly property color accent: "#F97316"
    readonly property color green: "#22C55E"
    readonly property color red: "#EF4444"

    property var selectedRunId: ""
    property var runList: []
    property var dailyData: []
    property var icData: []
    property var periodData: []

    function refresh() { Bridge.BacktestAnalyticsService.refreshRunList() }

    Component.onCompleted: {
        runList = Bridge.BacktestAnalyticsService.runList || []
        if (runList.length > 0) selectRun(runList[0].runId)
    }

    Connections {
        target: Bridge.BacktestAnalyticsService
        function onRunListChanged() {
            runList = Bridge.BacktestAnalyticsService.runList || []
            if (runList.length > 0 && !selectedRunId) selectRun(runList[0].runId)
        }
    }

    function selectRun(runId) {
        selectedRunId = runId
        Bridge.BacktestAnalyticsService.loadRunDetail(runId)
        dailyData = Bridge.BacktestAnalyticsService.loadDailyReturns(runId)
        icData = Bridge.BacktestAnalyticsService.loadIcSeries(runId)
        periodData = Bridge.BacktestAnalyticsService.loadPeriods(runId)
    }

    function quickStat(data, field, decimals) {
        if (!data || data.length === 0) return "--"
        var sum = 0, cnt = 0
        for (var i = 0; i < data.length; i++) {
            var v = Number(data[i][field] || 0)
            if (isFinite(v)) { sum += v; cnt++ }
        }
        return cnt > 0 ? (sum / cnt).toFixed(decimals || 2) : "--"
    }

    function cumReturn(data, field) {
        var cum = 1.0
        for (var i = 0; i < data.length; i++)
            cum *= (1.0 + Number(data[i][field] || 0))
        return cum
    }

    function totalTurnover(data) {
        if (!data || data.length === 0) return "--"
        var sum = 0, cnt = 0
        for (var i = 0; i < data.length; i++) {
            var t = Number(data[i].longTurnover || 0) + Number(data[i].shortTurnover || 0)
            if (isFinite(t)) { sum += t; cnt++ }
        }
        return cnt > 0 ? ((sum / cnt * 50).toFixed(1) + "%") : "--"
    }

    Rectangle { anchors.fill: parent; color: bg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── 标题栏 ──
        RowLayout {
            Layout.fillWidth: true
            Text { text: "📈 因子绩效分析"; font.pixelSize: 22; font.weight: Font.Black; color: textPri }
            Item { Layout.fillWidth: true }
            Button {
                text: "刷新列表"
                onClicked: root.refresh()
                background: Rectangle { radius: 8; color: accent; opacity: 0.9 }
                contentItem: Text { text: "刷新列表"; color: "white"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ═══ 左侧：回测列表 ═══
            Rectangle {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                radius: 12; color: panelBg; border.color: panelBorder; border.width: 1
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    Text { text: "回测记录"; font.pixelSize: 14; font.weight: Font.Bold; color: textPri }

                    ListView {
                        id: runListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: runList
                        clip: true
                        delegate: Rectangle {
                            width: runListView.width
                            height: 72
                            radius: 8
                            color: modelData.runId === selectedRunId ? "#1E293B" : "transparent"
                            border.color: modelData.runId === selectedRunId ? accent : "transparent"
                            border.width: modelData.runId === selectedRunId ? 1 : 0

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.selectRun(modelData.runId)
                            }

                            Column {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 3
                                Text {
                                    text: String(modelData.factorId || "").substring(0, 24)
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: textPri
                                    elide: Text.ElideRight; width: parent.width
                                }
                                Row {
                                    spacing: 6
                                    Text { text: "Sharpe " + Number(modelData.sharpe || 0).toFixed(2); font.pixelSize: 10; color: Number(modelData.sharpe || 0) >= 0 ? green : red }
                                    Text { text: "IC " + Number(modelData.icMean || 0).toFixed(3); font.pixelSize: 10; color: textSec }
                                    Text { text: "⇅ " + Number(modelData.turnover || 0).toFixed(2); font.pixelSize: 10; color: textSec }
                                }
                                Text {
                                    text: String(modelData.createdAt || "").substring(0, 19) + "  |  " + Number(modelData.dailyCount || 0) + "d"
                                    font.pixelSize: 9; color: textSec
                                }
                            }
                        }
                    }
                }
            }

            // ═══ 右侧：详情 ═══
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                // ── 指标卡片行 ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: [
                            {label: "累计净值", value: dailyData.length > 0 ? cumReturn(dailyData, "costReturn").toFixed(3) : "--", color: green},
                            {label: "Sharpe", value: quickStat(dailyData, "costReturn", 2), sub: "日均 " + quickStat(dailyData, "costReturn", 4)},
                            {label: "IC 均值", value: quickStat(icData, "ic", 3), sub: "ICIR " + quickStat(icData, "icMA20", 3)},
                            {label: "日均换手", value: totalTurnover(periodData), sub: periodData.length + " 期"}
                        ]
                        delegate: Rectangle {
                            Layout.preferredWidth: 140
                            height: 80
                            radius: 12; color: panelBg; border.color: panelBorder; border.width: 1
                            Column {
                                anchors.centerIn: parent
                                spacing: 4
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; font.pixelSize: 10; color: textSec }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.value; font.pixelSize: 20; font.weight: Font.Black; color: textPri }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.sub || ""; font.pixelSize: 9; color: textSec }
                            }
                        }
                    }
                }

                // ── 双图表行：IC走势 + 收益曲线 ──
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 220
                    spacing: 10

                    // IC 走势图
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 12; color: panelBg; border.color: panelBorder; border.width: 1
                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 4
                            Text { text: "Rank IC 走势"; font.pixelSize: 12; font.weight: Font.Bold; color: textPri }
                            Canvas {
                                id: icCanvas
                                anchors.fill: parent; anchors.topMargin: 24
                                onPaint: {
                                    var ctx = getContext("2d"); ctx.clearRect(0, 0, width, height)
                                    if (icData.length === 0) return
                                    var lpad = 44, rpad = 14, tpad = 8, bpad = 30
                                    ctx.strokeStyle = Qt.rgba(0.55,0.62,0.73,0.18); ctx.lineWidth = 0.5
                                    ctx.beginPath(); ctx.moveTo(lpad, height/2); ctx.lineTo(width-rpad, height/2); ctx.stroke()
                                    ctx.strokeStyle = accent; ctx.lineWidth = 1.5; ctx.beginPath()
                                    for (var i = 0; i < icData.length; i++) {
                                        var x = lpad + (width-lpad-rpad)*i/Math.max(1,icData.length-1)
                                        var y = height/2 - (Number(icData[i].ic||0))*height*2
                                        y = Math.max(tpad, Math.min(height-bpad, y))
                                        if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y)
                                    }
                                    ctx.stroke()
                                    // 0 线
                                    ctx.fillStyle = textSec; ctx.font = "9px sans-serif"
                                    ctx.fillText("0", 2, height/2 + 4)
                                }
                                Connections { target: root; function onIcDataChanged() { icCanvas.requestPaint() } }
                                Component.onCompleted: requestPaint()
                            }
                        }
                    }

                    // 收益曲线
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 12; color: panelBg; border.color: panelBorder; border.width: 1
                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 4
                            Text { text: "累计净值曲线"; font.pixelSize: 12; font.weight: Font.Bold; color: textPri }
                            Canvas {
                                id: returnCanvas
                                anchors.fill: parent; anchors.topMargin: 24
                                onPaint: {
                                    var ctx = getContext("2d"); ctx.clearRect(0, 0, width, height)
                                    if (dailyData.length === 0) return
                                    var lpad = 44, rpad = 14, tpad = 8, bpad = 30
                                    // build cum series
                                    var rawCum = [], costCum = [], cum = 1.0, cumC = 1.0
                                    for (var i = 0; i < dailyData.length; i++) {
                                        cum  *= (1.0 + Number(dailyData[i].rawReturn  || 0)); rawCum.push(cum)
                                        cumC *= (1.0 + Number(dailyData[i].costReturn || 0)); costCum.push(cumC)
                                    }
                                    var all = rawCum.concat(costCum)
                                    var vmin = Math.min(0.5, Math.min.apply(null, all)); var vmax = Math.max(1.5, Math.max.apply(null, all))
                                    // grid
                                    ctx.strokeStyle = Qt.rgba(0.55,0.62,0.73,0.18); ctx.lineWidth = 0.5
                                    ctx.beginPath(); var by = lpad + (width-lpad-rpad)*(1.0-vmin)/(vmax-vmin)
                                    by = height - bpad - (1.0-vmin)/(vmax-vmin)*(height-tpad-bpad)
                                    ctx.moveTo(lpad, by); ctx.lineTo(width-rpad, by); ctx.stroke()
                                    // raw
                                    ctx.strokeStyle = red; ctx.lineWidth = 1.2; ctx.beginPath()
                                    for (var j = 0; j < rawCum.length; j++) {
                                        var xx = lpad + (width-lpad-rpad)*j/Math.max(1,rawCum.length-1)
                                        var yy = height - bpad - (rawCum[j]-vmin)/(vmax-vmin)*(height-tpad-bpad)
                                        if (j===0) ctx.moveTo(xx,yy); else ctx.lineTo(xx,yy)
                                    }
                                    ctx.stroke()
                                    // cost
                                    ctx.strokeStyle = "#3B82F6"; ctx.lineWidth = 1.2
                                    ctx.setLineDash([4,2]); ctx.beginPath()
                                    for (var k = 0; k < costCum.length; k++) {
                                        var xk = lpad + (width-lpad-rpad)*k/Math.max(1,costCum.length-1)
                                        var yk = height - bpad - (costCum[k]-vmin)/(vmax-vmin)*(height-tpad-bpad)
                                        if (k===0) ctx.moveTo(xk,yk); else ctx.lineTo(xk,yk)
                                    }
                                    ctx.stroke(); ctx.setLineDash([])
                                    // legend
                                    ctx.fillStyle = red; ctx.fillRect(4, 4, 10, 10)
                                    ctx.fillStyle = textSec; ctx.font="9px sans-serif"; ctx.fillText("raw", 18, 13)
                                    ctx.fillStyle = "#3B82F6"; ctx.fillRect(50, 4, 10, 10)
                                    ctx.fillText("cost-adj", 64, 13)
                                }
                                Connections { target: root; function onDailyDataChanged() { returnCanvas.requestPaint() } }
                                Component.onCompleted: requestPaint()
                            }
                        }
                    }
                }

                // ── 分组收益 + 调仓统计 ──
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 160
                    spacing: 10

                    // 换手率走势
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        radius: 12; color: panelBg; border.color: panelBorder; border.width: 1
                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 4
                            Text { text: "换手率 (" + periodData.length + " 期)"; font.pixelSize: 12; font.weight: Font.Bold; color: textPri }
                            ScrollView {
                                anchors.fill: parent; anchors.topMargin: 20
                                Column {
                                    width: parent.width
                                    Repeater {
                                        model: Math.min(periodData.length, 200)
                                        delegate: Rectangle {
                                            width: parent.width; height: 18
                                            Row {
                                                anchors.fill: parent; spacing: 8
                                                Text { width: 80; text: String(periodData[index].date || "").substring(0,10); font.pixelSize: 9; color: textSec }
                                                Rectangle {
                                                    width: Math.max(4, (parent.width-120) * Number(periodData[index].longTurnover || 0))
                                                    height: 12; radius: 6; color: green; anchors.verticalCenter: parent.verticalCenter
                                                }
                                                Rectangle {
                                                    width: Math.max(4, (parent.width-120) * Number(periodData[index].shortTurnover || 0))
                                                    height: 12; radius: 6; color: red; anchors.verticalCenter: parent.verticalCenter
                                                }
                                                Text { text: ((Number(periodData[index].longTurnover||0)+Number(periodData[index].shortTurnover||0))*50).toFixed(0)+"%"; font.pixelSize: 9; color: textPri }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 调仓快照
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        radius: 12; color: panelBg; border.color: panelBorder; border.width: 1
                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 4
                            Text { text: "最近调仓"; font.pixelSize: 12; font.weight: Font.Bold; color: textPri }
                            ScrollView {
                                anchors.fill: parent; anchors.topMargin: 20
                                Column {
                                    width: parent.width
                                    Repeater {
                                        model: Math.min(periodData.length, 200)
                                        property int idx: periodData.length - 1 - (200 - index - 1 >= periodData.length ? index : Math.max(0, periodData.length - 1 - index))
                                        delegate: Item {
                                            width: parent.width; height: 16
                                            property int pi: Math.min(periodData.length - 1, Math.max(0, periodData.length - 1 - index))
                                            Row {
                                                anchors.fill: parent; spacing: 6
                                                Text { width: 70; text: String((periodData[pi]||{}).date || "").substring(0,10); font.pixelSize: 9; color: textSec }
                                                Text { text: "L:"+((periodData[pi]||{}).longHeld||0)+"+"+((periodData[pi]||{}).longBought||0)+"-"+((periodData[pi]||{}).longSold||0); font.pixelSize: 9; color: green; width: 60 }
                                                Text { text: "S:"+((periodData[pi]||{}).shortHeld||0)+"-"+((periodData[pi]||{}).shortBought||0)+"+"+((periodData[pi]||{}).shortSold||0); font.pixelSize: 9; color: red; width: 60 }
                                                Text { text: "net " + Number((periodData[pi]||{}).strategyNetReturn || 0).toFixed(4); font.pixelSize: 9; color: Number((periodData[pi]||{}).strategyNetReturn || 0) >= 0 ? green : red }
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
}

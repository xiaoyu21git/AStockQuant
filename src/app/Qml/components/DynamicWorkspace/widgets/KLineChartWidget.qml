import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading" as Trading
import "../../../utils/TradingConstants.js" as Const

Item {
    id: root
    property var widgetConfig: ({})
    clip: true

    // symbol 完全由 C++ MarketDataBridge.primarySymbol 驱动
    // ensureWatchSymbol → C++ 更新 primarySymbol → 自动触发此绑定
    readonly property string chartSymbol: Bridge.MarketDataBridge
        && Bridge.MarketDataBridge.primarySymbol
        ? Bridge.MarketDataBridge.primarySymbol : "000001.SZ"

    property int chartMode: 0  // 0=K线, 1=分时

    onChartSymbolChanged: { tsCanvas.requestPaint() }
    onChartModeChanged: { if (chartMode === 1) tsCanvas.requestPaint() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 模式切换
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            spacing: 2

            ModeTab { label: "K线"; active: chartMode === 0; onClicked: chartMode = 0 }
            ModeTab { label: "分时"; active: chartMode === 1; onClicked: chartMode = 1 }
            Item { Layout.fillWidth: true }
        }

        // K线
        Trading.CompactChart {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: chartMode === 0
            marketDataService: Bridge.MarketDataBridge
            symbol: chartSymbol
            clip: true
        }

        // 分时图
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: chartMode === 1
            color: "#0b1220"
            border.color: "#1f2e45"
            border.width: 1
            clip: true

            Canvas {
                id: tsCanvas
                anchors.fill: parent
                anchors.margins: 4

                property var _intraday: Bridge.MarketDataBridge
                    && Bridge.MarketDataBridge.intraday
                    ? Bridge.MarketDataBridge.intraday : []

                Connections {
                    target: Bridge.MarketDataBridge
                    function onIntradayChanged() {
                        tsCanvas._intraday = Bridge.MarketDataBridge.intraday || []
                        tsCanvas.requestPaint()
                    }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    var w = width, h = height
                    if (w <= 0 || h <= 0) return
                    ctx.clearRect(0, 0, w, h)

                    var tsData = []
                    var raw = tsCanvas._intraday || []
                    var rawSym = chartSymbol.replace(/\.(SZ|SH|BJ)$/, "")
                    for (var i = 0; i < raw.length; i++) {
                        var bs = String(raw[i].symbol || "")
                        if (bs === chartSymbol || bs === rawSym || bs.indexOf(rawSym) >= 0)
                            tsData.push(raw[i])
                    }
                    if (tsData.length === 0) tsData = raw

                    if (tsData.length === 0) {
                        ctx.fillStyle = "#64748B"
                        ctx.font = "12px sans-serif"
                        ctx.textAlign = "center"
                        ctx.fillText("暂无分时数据", w/2, h/2)
                        return
                    }

                    var priceH = -Infinity, priceL = Infinity, volMax = 0
                    var prevClose = Number(tsData[0].close || tsData[0].price || 0)
                    for (var j = 0; j < tsData.length; j++) {
                        var p = Number(tsData[j].close || tsData[j].price || 0)
                        if (p > 0) { priceH = Math.max(priceH, p); priceL = Math.min(priceL, p) }
                        volMax = Math.max(volMax, Number(tsData[j].volume || 0))
                    }
                    if (priceL >= priceH) { priceH = priceL + 1; priceL -= 1 }
                    var priceRng = priceH - priceL

                    // 布局: 价格区70% + 量能区22% + 边距8%
                    var chartH = h * 0.70
                    var volH = h * 0.22
                    var topM = 8

                    function tpx(idx) { return idx / Math.max(1, tsData.length - 1) * w }
                    function tpy(pr) { return topM + chartH * (1 - (pr - priceL) / priceRng) }

                    // 背景横线 (基于价格)
                    var gs = priceRng > 10 ? (priceRng > 100 ? 5 : 1) : 0.5
                    if (gs >= 5) gs = Math.ceil(gs / 5) * 5
                    var gp = Math.floor(priceL / gs) * gs
                    for (var g = 0; g < 30; g++) {
                        var gpr = gp + g * gs
                        if (gpr > priceH) break
                        var gy = tpy(gpr)
                        if (gy < topM || gy > topM + chartH) continue
                        var isMj = Math.abs(gpr % (gs * 5)) < 0.001
                        ctx.strokeStyle = isMj ? "#1e3048" : "#111d2e"
                        ctx.lineWidth = isMj ? 0.7 : 0.35
                        ctx.setLineDash([])
                        ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke()
                    }

                    // 均价
                    var sumC = 0, cntC = 0
                    for (var k = 0; k < tsData.length; k++) {
                        var cp = Number(tsData[k].close || tsData[k].price || 0)
                        if (cp > 0) { sumC += cp; cntC++ }
                    }
                    var avgP = cntC > 0 ? sumC / cntC : (priceH + priceL) / 2

                    // 昨收虚线
                    if (prevClose > 0) {
                        ctx.strokeStyle = "#64748B"
                        ctx.lineWidth = 0.8
                        ctx.setLineDash([3, 5])
                        var pcy = tpy(prevClose)
                        if (pcy >= topM && pcy <= topM + chartH) {
                            ctx.beginPath(); ctx.moveTo(0, pcy); ctx.lineTo(w, pcy); ctx.stroke()
                        }
                        ctx.setLineDash([])
                    }

                    // 均价虚线
                    ctx.strokeStyle = "#f59e0b"
                    ctx.lineWidth = 0.8
                    ctx.setLineDash([4, 4])
                    var ay = tpy(avgP)
                    ctx.beginPath(); ctx.moveTo(0, ay); ctx.lineTo(w, ay); ctx.stroke()
                    ctx.setLineDash([])

                    // 价格线
                    ctx.strokeStyle = "#ffffff"
                    ctx.lineWidth = 1.3
                    ctx.beginPath()
                    var started = false
                    for (var m = 0; m < tsData.length; m++) {
                        var cp2 = Number(tsData[m].close || tsData[m].price || 0)
                        if (cp2 <= 0) continue
                        var sx = tpx(m), sy = tpy(cp2)
                        if (!started) { ctx.moveTo(sx, sy); started = true }
                        else ctx.lineTo(sx, sy)
                    }
                    ctx.stroke()

                    // 价格渐变填充
                    if (started) {
                        ctx.lineTo(tpx(tsData.length - 1), topM + chartH)
                        ctx.lineTo(0, topM + chartH)
                        ctx.closePath()
                        var grad = ctx.createLinearGradient(0, topM, 0, topM + chartH)
                        grad.addColorStop(0, "rgba(255,255,255,0.12)")
                        grad.addColorStop(1, "rgba(255,255,255,0.01)")
                        ctx.fillStyle = grad
                        ctx.fill()
                    }

                    // 量柱
                    var barW = Math.max(1, w / Math.max(1, tsData.length) * 0.7)
                    for (var n = 0; n < tsData.length; n++) {
                        var vol = Number(tsData[n].volume || 0)
                        var bh = volMax > 0 ? (vol / volMax) * volH : 0
                        var bx = tpx(n) - barW / 2
                        var by2 = h - 4 - bh
                        var up = n > 0 ? (Number(tsData[n].close || tsData[n].price || 0)
                            >= Number(tsData[n-1].close || tsData[n-1].price || 0)) : true
                        ctx.fillStyle = up ? "rgba(239,68,68,0.7)" : "rgba(16,185,129,0.7)"
                        ctx.fillRect(bx, by2, barW, bh)
                    }

                    // 价格标注
                    ctx.fillStyle = "#94a3b8"
                    ctx.font = "8px monospace"
                    ctx.textAlign = "right"
                    ctx.fillText(priceH.toFixed(2), w - 4, topM + 10)
                    ctx.fillText(priceL.toFixed(2), w - 4, topM + chartH - 2)
                    ctx.fillText(avgP.toFixed(2), w - 4, ay + 3)
                    if (prevClose > 0 && pcy >= topM && pcy <= topM + chartH)
                        ctx.fillText("昨收 " + prevClose.toFixed(2), w - 4, pcy - 2)

                    // 时间
                    ctx.textAlign = "left"
                    ctx.fillStyle = "#64748B"
                    ctx.font = "9px monospace"
                    if (tsData.length > 0)
                        ctx.fillText(String(tsData[0].time || tsData[0].date || "").slice(-8), 2, h - 2)
                    ctx.textAlign = "right"
                    if (tsData.length > 1)
                        ctx.fillText(String(tsData[tsData.length-1].time || tsData[tsData.length-1].date || "").slice(-8), w - 2, h - 2)
                }
            }
        }
    }

    component ModeTab: Rectangle {
        property string label: ""
        property bool active: false
        signal clicked()
        width: 34; height: 20; radius: 3
        color: active ? "#1a3050" : "transparent"
        border.color: active ? "#4f8cff" : "transparent"
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: label
            color: active ? "#e2e8f0" : "#64748B"
            font.pixelSize: 10
            font.weight: active ? Font.DemiBold : Font.Normal
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }
}

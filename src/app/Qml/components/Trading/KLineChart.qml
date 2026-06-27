import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/TradingConstants.js" as Const

// ── KLineChart — 独立 K 线图组件 ──
// Canvas 渲染：蜡烛图 + 成交量 + MA均线 + 十字光标
// 交互：拖拽平移 / 滚轮缩放 / 周期切换
Rectangle {
    id: root

    // ── 输入 ──
    property var candles: []            // [{open,high,low,close,volume,time}]
    property var signals: []            // [{type:"buy"/"sell"/"clear"/"add", price, quantity, time, reason}]
    property string period: "daily"     // daily/weekly/monthly
    property string symbol: ""

    // ── 视口状态 ──
    property int candleCount: candles ? candles.length : 0
    property int visibleStart: Math.max(0, candleCount - visibleCount)
    property int visibleCount: 60       // 可见K线数量 (缩放控制)
    property real candleWidth: chartCanvas.width / Math.max(1, visibleCount)
    property real barWidth: Math.max(1, candleWidth * 0.7)

    // ── 十字光标 ──
    property int hoverIndex: -1
    property real hoverPrice: 0
    property bool hoverVisible: false

    // ── 布局 ──
    readonly property real chartRatio: 0.72
    readonly property real volumeRatio: 0.18
    readonly property real axisRatio: 0.10
    readonly property real rightAxisWidth: 64

    radius: 12
    color: Const.tradingPanelBgAlt
    border.color: Const.tradingPanelBorderAlt
    border.width: 1

    // ═══════════════════════════════
    // 内部计算
    // ═══════════════════════════════
    function visibleCandles() {
        if (!candles || candles.length === 0) return []
        var start = Math.max(0, visibleStart)
        var end = Math.min(candleCount, visibleStart + visibleCount)
        return candles.slice(start, end)
    }

    function viewportPriceRange() {
        var vc = visibleCandles()
        if (vc.length === 0) return { min: 0, max: 100 }
        var high = -Infinity, low = Infinity
        for (var i = 0; i < vc.length; i++) {
            high = Math.max(high, Number(vc[i].high || 0))
            low = Math.min(low, Number(vc[i].low || 0))
        }
        var pad = (high - low) * 0.06
        return { min: low - pad, max: high + pad }
    }

    function viewportVolumeMax() {
        var vc = visibleCandles()
        var maxVol = 0
        for (var i = 0; i < vc.length; i++) {
            maxVol = Math.max(maxVol, Number(vc[i].volume || 0))
        }
        return maxVol
    }

    function priceToY(price, range, areaHeight) {
        if (range.max <= range.min) return areaHeight / 2
        return areaHeight * (1 - (price - range.min) / (range.max - range.min))
    }

    // MA 均线
    function calcMA(candlesArray, period) {
        var result = []
        for (var i = 0; i < candlesArray.length; i++) {
            if (i < period - 1) { result.push(NaN); continue }
            var sum = 0
            for (var j = i - period + 1; j <= i; j++) sum += Number(candlesArray[j].close || 0)
            result.push(sum / period)
        }
        return result
    }

    // ═══════════════════════════════
    // 缩放 / 平移
    // ═══════════════════════════════
    function zoomIn() {
        visibleCount = Math.max(8, visibleCount - 10)
        clampViewport()
        requestPaint()
    }

    function zoomOut() {
        visibleCount = Math.min(Math.max(60, candleCount), visibleCount + 10)
        clampViewport()
        requestPaint()
    }

    function panLeft() {
        visibleStart = Math.max(0, visibleStart - Math.floor(visibleCount / 4))
        clampViewport()
        requestPaint()
    }

    function panRight() {
        visibleStart = Math.min(candleCount - visibleCount, visibleStart + Math.floor(visibleCount / 4))
        clampViewport()
        requestPaint()
    }

    function scrollToEnd() {
        visibleStart = Math.max(0, candleCount - visibleCount)
        clampViewport()
        requestPaint()
    }

    function clampViewport() {
        visibleStart = Math.max(0, Math.min(candleCount - Math.max(1, visibleCount), visibleStart))
        visibleCount = Math.max(8, Math.min(candleCount, visibleCount))
    }

    function requestPaint() {
        chartCanvas.requestPaint()
        volumeCanvas.requestPaint()
    }

    // ── 数据变更 ──
    onCandlesChanged: {
        if (candles && candles.length > 0) {
            visibleCount = Math.min(60, candles.length)
            visibleStart = Math.max(0, candles.length - visibleCount)
        }
        requestPaint()
    }

    // ═══════════════════════════════
    // 布局
    // ═══════════════════════════════
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 2

        // ── 信号图例 ──
        RowLayout {
            visible: signals && signals.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            spacing: 12

            Text { text: "信号:"; color: Const.tradingLabelSecondary; font.pixelSize: 10 }

            Repeater {
                model: [
                    { type: "buy", color: "#ef4444", label: "买点 B" },
                    { type: "sell", color: "#10b981", label: "卖点 S" },
                    { type: "add", color: "#f59e0b", label: "加仓 +" },
                    { type: "clear", color: "#8b5cf6", label: "清仓 X" }
                ]
                RowLayout {
                    spacing: 3
                    Rectangle { width: 8; height: 8; radius: 4; color: modelData.color }
                    Text { text: modelData.label; color: modelData.color; font.pixelSize: 9 }
                }
            }

            Item { Layout.fillWidth: true }
            Text {
                text: "共 " + (signals ? signals.length : 0) + " 条"
                color: Const.tradingLabelSecondary
                font.pixelSize: 9
            }
        }

        // ── 工具栏 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 4

            Text {
                text: root.symbol || "K线图"
                color: Const.tradingTitleText
                font.pixelSize: 13
                font.weight: Font.Bold
                Layout.leftMargin: 4
            }

            Item { Layout.fillWidth: true }

            Repeater {
                model: [
                    { key: "daily", label: "日K" },
                    { key: "weekly", label: "周K" },
                    { key: "monthly", label: "月K" }
                ]
                Rectangle {
                    radius: 6
                    implicitWidth: labelText.implicitWidth + 16
                    implicitHeight: 24
                    color: root.period === modelData.key ? Const.tradingTabActiveBorder : Const.tradingTabInactiveBg
                    border.color: root.period === modelData.key ? Const.tradingTabActiveBorder : Const.tradingTabInactiveBorder
                    border.width: 1
                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: modelData.label
                        color: root.period === modelData.key ? Const.tradingTabActiveText : Const.tradingTabInactiveText
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.period = modelData.key
                    }
                }
            }

            Rectangle { radius: 6; implicitWidth: 24; implicitHeight: 24; color: Const.tradingButtonBg; border.color: Const.tradingInputActiveBorder; border.width: 1
                Text { anchors.centerIn: parent; text: "−"; color: Const.tradingLightBlue; font.pixelSize: 14; font.weight: Font.Bold }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.zoomOut() }
            }
            Rectangle { radius: 6; implicitWidth: 24; implicitHeight: 24; color: Const.tradingButtonBg; border.color: Const.tradingInputActiveBorder; border.width: 1
                Text { anchors.centerIn: parent; text: "+"; color: Const.tradingLightBlue; font.pixelSize: 14; font.weight: Font.Bold }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.zoomIn() }
            }
            Rectangle { radius: 6; implicitWidth: 36; implicitHeight: 24; color: Const.tradingButtonBg; border.color: Const.tradingInputActiveBorder; border.width: 1
                Text { anchors.centerIn: parent; text: "⇤"; color: Const.tradingLightBlue; font.pixelSize: 12 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.scrollToEnd() }
            }
        }

        // ── 主图 + 右轴 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // ── K线画布 ──
                Canvas {
                    id: chartCanvas
                    anchors.fill: parent
                    anchors.rightMargin: 0
                    renderStrategy: Canvas.Cooperative

                    onPaint: {
                        var ctx = getContext("2d")
                        var w = width, h = height
                        ctx.clearRect(0, 0, w, h)

                        var vc = visibleCandles()
                        if (vc.length === 0) {
                            ctx.fillStyle = Const.tradingEmptyText
                            ctx.font = "13px sans-serif"
                            ctx.textAlign = "center"
                            ctx.fillText("暂无K线数据", w / 2, h / 2)
                            return
                        }

                        var cw = w / Math.max(1, visibleCount)
                        var range = viewportPriceRange()
                        var bw = Math.max(1, cw * 0.7)

                        // ── 网格线 ──
                        var gridSteps = 5
                        ctx.strokeStyle = "#1a2a3a"
                        ctx.lineWidth = 0.5
                        for (var g = 1; g < gridSteps; g++) {
                            var gy = h * g / gridSteps
                            ctx.beginPath()
                            ctx.moveTo(0, gy)
                            ctx.lineTo(w, gy)
                            ctx.stroke()
                        }

                        // ── MA 均线 ──
                        var allCandles = candles || []
                        var ma5 = calcMA(allCandles, 5)
                        var ma10 = calcMA(allCandles, 10)
                        var ma20 = calcMA(allCandles, 20)
                        var maColors = ["#f59e0b", "#3b82f6", "#a855f7"]
                        var maData = [ma5, ma10, ma20]

                        for (var maIdx = 0; maIdx < maData.length; maIdx++) {
                            var ma = maData[maIdx]
                            ctx.strokeStyle = maColors[maIdx]
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            var firstPoint = true
                            for (var i = 0; i < vc.length; i++) {
                                var globalIdx = visibleStart + i
                                if (globalIdx >= ma.length) continue
                                var maVal = ma[globalIdx]
                                if (isNaN(maVal)) continue
                                var x = i * cw + cw / 2
                                var y = priceToY(maVal, range, h)
                                if (firstPoint) { ctx.moveTo(x, y); firstPoint = false }
                                else ctx.lineTo(x, y)
                            }
                            ctx.stroke()
                        }

                        // ── 蜡烛图 ──
                        for (var ci = 0; ci < vc.length; ci++) {
                            var c = vc[ci]
                            var open = Number(c.open || 0), close = Number(c.close || 0)
                            var high = Number(c.high || 0), low = Number(c.low || 0)
                            if (open <= 0) continue

                            var isUp = close >= open
                            var bodyColor = isUp ? "#ef4444" : "#10b981"
                            var borderColor = isUp ? "#dc2626" : "#059669"

                            var cx = ci * cw
                            var openY = priceToY(open, range, h)
                            var closeY = priceToY(close, range, h)
                            var highY = priceToY(high, range, h)
                            var lowY = priceToY(low, range, h)

                            // 影线
                            ctx.strokeStyle = borderColor
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(cx + cw / 2, highY)
                            ctx.lineTo(cx + cw / 2, lowY)
                            ctx.stroke()

                            // 实体
                            var bodyTop = Math.min(openY, closeY)
                            var bodyH = Math.max(1, Math.abs(closeY - openY))
                            ctx.fillStyle = bodyColor
                            ctx.fillRect(cx + (cw - bw) / 2, bodyTop, bw, bodyH)
                        }

                        // ── 交易信号标注 ──
                        if (signals && signals.length > 0) {
                            var sigList = signals
                            for (var si = 0; si < sigList.length; si++) {
                                var sig = sigList[si]
                                var sigTime = String(sig.time || "")
                                var sigIdx = -1
                                // 按时间匹配到可见K线索引
                                for (var svi = 0; svi < vc.length; svi++) {
                                    if (String(vc[svi].time || "") === sigTime) { sigIdx = svi; break }
                                }
                                if (sigIdx < 0) continue
                                var sigPrice = Number(sig.price || 0)
                                if (sigPrice <= 0) continue
                                var sigY = priceToY(sigPrice, range, h)
                                var sigX = sigIdx * cw + cw / 2
                                var sigType = String(sig.type || "").toLowerCase()
                                var qty = Number(sig.quantity || 0)

                                // 颜色和标记
                                var markColor, markLabel
                                if (sigType === "buy")       { markColor = "#ef4444"; markLabel = "B" }
                                else if (sigType === "sell") { markColor = "#10b981"; markLabel = "S" }
                                else if (sigType === "add")  { markColor = "#f59e0b"; markLabel = "+" }
                                else if (sigType === "clear"){ markColor = "#8b5cf6"; markLabel = "X" }
                                else continue

                                // 箭头三角形
                                var arrowSize = 8
                                ctx.fillStyle = markColor
                                ctx.beginPath()
                                if (sigType === "buy" || sigType === "add") {
                                    // 向上箭头 (买点/加仓在价格下方)
                                    ctx.moveTo(sigX, sigY + 4)
                                    ctx.lineTo(sigX - arrowSize, sigY + 4 + arrowSize)
                                    ctx.lineTo(sigX + arrowSize, sigY + 4 + arrowSize)
                                } else {
                                    // 向下箭头 (卖点/清仓在价格上方)
                                    ctx.moveTo(sigX, sigY - 4)
                                    ctx.lineTo(sigX - arrowSize, sigY - 4 - arrowSize)
                                    ctx.lineTo(sigX + arrowSize, sigY - 4 - arrowSize)
                                }
                                ctx.closePath()
                                ctx.fill()

                                // 信号圆点
                                ctx.beginPath()
                                ctx.arc(sigX, sigY, 4, 0, 2 * Math.PI)
                                ctx.fillStyle = markColor
                                ctx.fill()
                                ctx.strokeStyle = "#ffffff"
                                ctx.lineWidth = 1
                                ctx.stroke()

                                // 标签文字 (类型+数量)
                                var labelText = markLabel
                                if (qty > 0) {
                                    if (qty >= 10000) labelText += " " + (qty / 10000).toFixed(1) + "万"
                                    else labelText += " " + Math.round(qty)
                                }
                                ctx.fillStyle = markColor
                                ctx.font = "bold 10px monospace"
                                ctx.textAlign = sigType === "buy" || sigType === "add" ? "left" : "right"
                                var labelX = sigType === "buy" || sigType === "add" ? sigX + 6 : sigX - 6
                                var labelY = sigType === "buy" || sigType === "add" ? sigY + 18 : sigY - 14
                                ctx.fillText(labelText, labelX, labelY)

                                // 信号价格线（虚线延伸）
                                ctx.strokeStyle = markColor + "44"
                                ctx.lineWidth = 0.5
                                ctx.setLineDash([2, 6])
                                ctx.beginPath()
                                ctx.moveTo(sigX, sigY)
                                ctx.lineTo(sigX + (sigType === "buy" || sigType === "add" ? 30 : -30), sigY)
                                ctx.stroke()
                                ctx.setLineDash([])
                            }
                        }

                        // ── 十字光标 ──
                        if (hoverVisible && hoverIndex >= 0) {
                            var hx = hoverIndex * cw + cw / 2
                            ctx.strokeStyle = "#ffffff88"
                            ctx.lineWidth = 0.5
                            ctx.setLineDash([4, 4])
                            ctx.beginPath()
                            ctx.moveTo(hx, 0)
                            ctx.lineTo(hx, h)
                            ctx.stroke()
                            var hy = priceToY(hoverPrice, range, h)
                            ctx.beginPath()
                            ctx.moveTo(0, hy)
                            ctx.lineTo(w, hy)
                            ctx.stroke()
                            ctx.setLineDash([])
                        }

                        // ── 价格轴标注 ──
                        ctx.fillStyle = Const.tradingLabelSecondary
                        ctx.font = "10px monospace"
                        ctx.textAlign = "right"
                        for (var gy2 = 1; gy2 <= gridSteps; gy2++) {
                            var price = range.max - (range.max - range.min) * gy2 / gridSteps
                            ctx.fillText(price.toFixed(2), w - 4, h * gy2 / gridSteps + 3)
                        }
                    }
                }

                // ── 十字光标标签 ──
                Rectangle {
                    visible: root.hoverVisible && root.hoverIndex >= 0
                    x: Math.min(parent.width - 120, root.hoverIndex * (parent.width / Math.max(1, root.visibleCount)) + 10)
                    y: 4
                    radius: 6
                    color: "#881a1a2e"
                    border.color: Const.tradingLabelSecondary
                    border.width: 0.5
                    implicitWidth: 110
                    implicitHeight: 40
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            text: {
                                var vc2 = root.visibleCandles()
                                if (root.hoverIndex >= 0 && root.hoverIndex < vc2.length)
                                    return String(vc2[root.hoverIndex].time || "")
                                return ""
                            }
                            color: Const.tradingLabelSecondary
                            font.pixelSize: 10
                        }
                        Text {
                            text: "O:" + (function() {
                                var vc2 = root.visibleCandles()
                                if (root.hoverIndex >= 0 && root.hoverIndex < vc2.length)
                                    return Number(vc2[root.hoverIndex].open || 0).toFixed(2)
                                return "--"
                            })() + " H:" + (function() {
                                var vc2 = root.visibleCandles()
                                if (root.hoverIndex >= 0 && root.hoverIndex < vc2.length)
                                    return Number(vc2[root.hoverIndex].high || 0).toFixed(2)
                                return "--"
                            })()
                            color: Const.tradingTitleText
                            font.pixelSize: 10
                        }
                        Text {
                            text: "L:" + (function() {
                                var vc2 = root.visibleCandles()
                                if (root.hoverIndex >= 0 && root.hoverIndex < vc2.length)
                                    return Number(vc2[root.hoverIndex].low || 0).toFixed(2)
                                return "--"
                            })() + " C:" + (function() {
                                var vc2 = root.visibleCandles()
                                if (root.hoverIndex >= 0 && root.hoverIndex < vc2.length)
                                    return Number(vc2[root.hoverIndex].close || 0).toFixed(2)
                                return "--"
                            })()
                            color: Const.tradingTitleText
                            font.pixelSize: 10
                        }
                    }
                }

                // ── 成交量画布 ──
                Canvas {
                    id: volumeCanvas
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height * root.volumeRatio
                    renderStrategy: Canvas.Cooperative

                    onPaint: {
                        var ctx = getContext("2d")
                        var w = width, h = height
                        ctx.clearRect(0, 0, w, h)

                        var vc = visibleCandles()
                        if (vc.length === 0) return
                        var cw = w / Math.max(1, visibleCount)
                        var bw = Math.max(1, cw * 0.7)
                        var maxVol = viewportVolumeMax()
                        if (maxVol <= 0) return

                        ctx.strokeStyle = "#1a2a3a"
                        ctx.lineWidth = 0.5
                        ctx.beginPath()
                        ctx.moveTo(0, 0)
                        ctx.lineTo(w, 0)
                        ctx.stroke()

                        for (var vi = 0; vi < vc.length; vi++) {
                            var vol = Number(vc[vi].volume || 0)
                            var barH = h * vol / maxVol
                            var isUp = Number(vc[vi].close || 0) >= Number(vc[vi].open || 0)
                            ctx.fillStyle = isUp ? "#ef444488" : "#10b98188"
                            ctx.fillRect(vi * cw + (cw - bw) / 2, h - barH, bw, barH)
                        }

                        // 最大量标注
                        ctx.fillStyle = Const.tradingLabelSecondary
                        ctx.font = "9px monospace"
                        ctx.textAlign = "right"
                        ctx.fillText(formatVolume(maxVol), w - 2, 12)
                    }
                }

                // ── 时间轴 ──
                Canvas {
                    id: timeCanvas
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height * root.axisRatio
                    renderStrategy: Canvas.Cooperative

                    onPaint: {
                        var ctx = getContext("2d")
                        var w = width, h = height
                        ctx.clearRect(0, 0, w, h)

                        var vc = visibleCandles()
                        if (vc.length === 0) return
                        var cw = w / Math.max(1, visibleCount)
                        var step = Math.max(1, Math.floor(visibleCount / 7))

                        ctx.fillStyle = Const.tradingLabelSecondary
                        ctx.font = "9px monospace"
                        ctx.textAlign = "center"
                        for (var ti = 0; ti < vc.length; ti += step) {
                            var timeStr = String(vc[ti].time || "")
                            if (timeStr.length > 10) timeStr = timeStr.slice(5)  // 截短显示
                            ctx.fillText(timeStr, ti * cw + cw / 2, h - 4)
                        }
                    }
                }

                // ── 鼠标交互区 ──
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    property real lastMouseX: 0
                    property real dragStartX: 0
                    property int dragStartVisibleStart: 0

                    onWheel: function(wheel) {
                        if (wheel.angleDelta.y > 0) root.zoomIn()
                        else root.zoomOut()
                    }

                    onPressed: function(mouse) {
                        dragStartX = mouse.x
                        dragStartVisibleStart = root.visibleStart
                    }

                    onPositionChanged: function(mouse) {
                        if (pressed) {
                            var dx = mouse.x - dragStartX
                            var cw = width / Math.max(1, root.visibleCount)
                            var shift = Math.round(-dx / cw)
                            root.visibleStart = Math.max(0, Math.min(root.candleCount - root.visibleCount,
                                dragStartVisibleStart + shift))
                            root.requestPaint()
                        }

                        var cw = width / Math.max(1, root.visibleCount)
                        var idx = Math.floor(mouse.x / cw)
                        if (idx >= 0 && idx < root.visibleCount) {
                            root.hoverIndex = idx
                            var vc = root.visibleCandles()
                            if (idx < vc.length) {
                                root.hoverPrice = Number(vc[idx].close || 0)
                            }
                            root.hoverVisible = true
                        } else {
                            root.hoverVisible = false
                        }
                    }

                    onExited: { root.hoverVisible = false; root.requestPaint() }

                    // 双击回到最新
                    onDoubleClicked: root.scrollToEnd()
                }
            }

            // ── 右轴成交量标注 ──
            Item {
                Layout.preferredWidth: root.rightAxisWidth
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 0

                    // 主图区价格标注
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: parent.height * root.chartRatio
                    }
                    // 成交量标注
                    Text {
                        Layout.fillWidth: true
                        Layout.preferredHeight: parent.height * root.volumeRatio
                        text: "VOL"
                        color: Const.tradingLabelSecondary
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignTop
                        topPadding: 2
                    }
                }
            }
        }
    }

    // ── 工具函数 ──
    function formatVolume(vol) {
        if (vol >= 100000000) return (vol / 100000000).toFixed(1) + "亿"
        if (vol >= 10000) return (vol / 10000).toFixed(1) + "万"
        return vol.toFixed(0)
    }

    // ── 加载状态 ──
    Rectangle {
        anchors.centerIn: parent
        visible: !candles || candles.length === 0
        radius: 12
        color: Const.tradingPanelBgAlt
        border.color: Const.tradingPanelBorderAlt
        border.width: 1
        implicitWidth: 200
        implicitHeight: 60
        Text {
            anchors.centerIn: parent
            text: root.symbol ? (root.symbol + " — 加载K线数据中...") : "输入代码后显示K线图"
            color: Const.tradingEmptyText
            font.pixelSize: 12
        }
    }
}

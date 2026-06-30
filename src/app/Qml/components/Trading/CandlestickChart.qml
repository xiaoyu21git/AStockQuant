// CandlestickChart.qml — 自绘K线图 (分时线图 + 蜡烛图 + 成交量)
import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    color: "#0d1117"

    property string stockCode:  "000001"
    property string stockName:  ""
    property int    chartPeriod: 7   // Daily
    property var    candleModel: null
    property var    dataLoader:  null
    property double preClose:    0.0
    property double latestPrice: 0.0

    readonly property color upColor:    "#ef5350"
    readonly property color downColor:  "#26a69a"
    readonly property color frameBg:    "#0d1117"
    readonly property color panelBg:    "#161b22"
    readonly property color gridColor:  "#21262d"
    readonly property color borderColor:"#30363d"
    readonly property color textDim:    "#8b949e"
    readonly property color textBright: "#e6edf3"

    readonly property var periodLabels: ["分时","1分","5分","15分","30分","60分","120分","日线","周线","月线"]
    property double priceMin: 0; property double priceMax: 0; property double volMax: 0
    property double chartAreaRatio: 0.72  // 主图占比

    onChartPeriodChanged: {
        console.log("[CandlestickChart] period changed to " + chartPeriod)
        if (dataLoader) dataLoader.loadFromDB(stockCode, chartPeriod)
    }
    onStockCodeChanged: {
        console.log("[CandlestickChart] stockCode changed to " + stockCode)
        if (dataLoader) dataLoader.loadFromDB(stockCode, chartPeriod)
    }

    Connections {
        target: candleModel; enabled: candleModel !== null
        function onCountChanged()     {
            console.log("[CandlestickChart] countChanged count=" + (candleModel ? candleModel.count : 0))
            recalc(); canvas.requestPaint()
        }
        function onLastPriceChanged() {
            if (candleModel) root.latestPrice = candleModel.lastPrice
            console.log("[CandlestickChart] lastPriceChanged price=" + root.latestPrice)
            // tick 更新最后一根K线 → 需要重绘 (影线/实体可能变化)
            recalc(); canvas.requestPaint()
        }
        function onPreCloseChanged()  {
            if (candleModel) root.preClose = candleModel.preClose
        }
    }

    // 供外部 Widget 调用的安全重绘入口
    function requestRepaint() {
        canvas.requestPaint()
    }

    function recalc() {
        if (!candleModel || candleModel.count === 0) return
        priceMin = 1e18; priceMax = 0; volMax = 0
        for (var i = 0; i < candleModel.count; i++) {
            var h = candleModel.data(candleModel.index(i, 2), 0x0103)
            var l = candleModel.data(candleModel.index(i, 3), 0x0104)
            var v = candleModel.data(candleModel.index(i, 5), 0x0106) // VolumeRole
            if (h > priceMax) priceMax = h; if (l < priceMin) priceMin = l
            if (v > volMax) volMax = v
        }
        var pad = (priceMax - priceMin) * 0.08
        if (pad <= 0) pad = 0.5
        priceMin = Math.max(0, priceMin - pad); priceMax += pad
    }

    // ══════════════════════════════════════════════
    // 工具栏 (QML 原生按钮, 不占 Canvas 区域)
    // ══════════════════════════════════════════════
    Rectangle {
        id: toolbar; height: 30; color: panelBg
        anchors { top: parent.top; left: parent.left; right: parent.right }
        Row {
            anchors.centerIn: parent; spacing: 2
            Repeater {
                model: periodLabels
                Rectangle {
                    width: Math.max(28, root.width / 11); height: 24; radius: 4
                    color: chartPeriod === index ? "#1f6feb" : "transparent"
                    Text { anchors.centerIn: parent; text: modelData; color: chartPeriod === index ? "white" : textDim
                           font.pixelSize: 10 }
                    MouseArea { anchors.fill: parent; onClicked: chartPeriod = index }
                }
            }
        }
    }

    // ══════════════════════════════════════════════
    // 信息栏
    // ══════════════════════════════════════════════
    Rectangle {
        id: infoBar; height: 22; color: frameBg
        anchors { top: toolbar.bottom; left: parent.left; right: parent.right }
        Row {
            anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
            spacing: 10
            Text { text: stockName || stockCode; color: textBright; font.pixelSize: 11; font.bold: true }
            Text { text: latestPrice > 0 ? latestPrice.toFixed(2) : "--"; color: latestPrice >= preClose ? upColor : (latestPrice>0?downColor:textDim); font.pixelSize: 13; font.bold: true }
            Text { text: preClose>0&&latestPrice>0 ? ((latestPrice-preClose>=0?"+":"")+(latestPrice-preClose).toFixed(2)+" ("+((latestPrice-preClose)/preClose*100).toFixed(2)+"%)") : "--"; color: latestPrice>=preClose?upColor:downColor; font.pixelSize: 10 }
        }
    }

    // ══════════════════════════════════════════════
    // Canvas
    // ══════════════════════════════════════════════
    Canvas {
        id: canvas
        anchors { top: infoBar.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        onPaint: {
            var ctx = canvas.getContext("2d")
            var W = canvas.width, H = canvas.height
            ctx.clearRect(0, 0, W, H); ctx.fillStyle = frameBg; ctx.fillRect(0, 0, W, H)

            if (!candleModel || candleModel.count === 0) {
                ctx.fillStyle = textDim; ctx.font = "13px sans-serif"; ctx.textAlign = "center"
                ctx.fillText("暂无数据", W/2, H/2); return
            }

            var n = candleModel.count
            var rightMargin = 58, leftMargin = 4
            var plotW = W - leftMargin - rightMargin, plotX = leftMargin
            var priceRange = priceMax - priceMin; if (priceRange <= 0) priceRange = 1
            var candleW = Math.max(1, plotW / n * 0.65)
            var candleGap = plotW / n
            var chartTop = 2
            var mainH = (H - 2) * chartAreaRatio
            var divY  = chartTop + mainH
            var volBot = H - 2
            var volH   = volBot - divY - 1

            // 网格 + Y轴
            ctx.strokeStyle = gridColor; ctx.lineWidth = 0.5
            ctx.fillStyle = textDim; ctx.font = "9px sans-serif"; ctx.textAlign = "right"
            for (var g = 0; g < 5; g++) {
                var gy = chartTop + mainH * g / 4
                ctx.beginPath(); ctx.moveTo(plotX, gy); ctx.lineTo(W, gy); ctx.stroke()
                ctx.fillText((priceMax - priceRange * g / 4).toFixed(2), W-4, gy+3)
            }
            // 昨收线
            if (preClose > 0) {
                var preY = chartTop + mainH * (1 - (preClose - priceMin) / priceRange)
                ctx.strokeStyle = "#666688"; ctx.setLineDash([4,4])
                ctx.beginPath(); ctx.moveTo(plotX, preY); ctx.lineTo(W, preY); ctx.stroke(); ctx.setLineDash([])
            }

            var isTimeShare = (chartPeriod === 0)

            if (isTimeShare) {
                // 分时图: 收盘价连线 + 成交量柱
                ctx.strokeStyle = "#ffd700"; ctx.lineWidth = 1.5; ctx.beginPath()
                for (var j = 0; j < n; j++) {
                    var cc = candleModel.data(candleModel.index(j, 4), 0x0105)
                    var cy = chartTop + mainH * (1 - (cc - priceMin) / priceRange)
                    var lx = plotX + j * candleGap + candleGap/2
                    j === 0 ? ctx.moveTo(lx, cy) : ctx.lineTo(lx, cy)
                }
                ctx.stroke()
            } else {
                // 蜡烛图
                for (var i = 0; i < n; i++) {
                    var o=candleModel.data(candleModel.index(i,1),0x0102), hi=candleModel.data(candleModel.index(i,2),0x0103)
                    var lo=candleModel.data(candleModel.index(i,3),0x0104), c=candleModel.data(candleModel.index(i,4),0x0105)
                    var cx=plotX+i*candleGap+candleGap/2
                    var yO=chartTop+mainH*(1-(o-priceMin)/priceRange), yC=chartTop+mainH*(1-(c-priceMin)/priceRange)
                    var yH=chartTop+mainH*(1-(hi-priceMin)/priceRange), yL=chartTop+mainH*(1-(lo-priceMin)/priceRange)
                    var col=(c>=o)?upColor:downColor
                    ctx.strokeStyle=col; ctx.fillStyle=col; ctx.lineWidth=1
                    ctx.beginPath(); ctx.moveTo(cx,yH); ctx.lineTo(cx,yL); ctx.stroke()
                    var bh=Math.abs(yC-yO), by=Math.min(yO,yC)
                    ctx.fillRect(cx-candleW/2,by,candleW,Math.max(1,bh))
                }
            }

            // 分隔线
            ctx.strokeStyle = borderColor; ctx.lineWidth = 1
            ctx.beginPath(); ctx.moveTo(0, divY); ctx.lineTo(W, divY); ctx.stroke()

            // 成交量
            if (volMax > 0) {
                ctx.fillStyle = textDim; ctx.textAlign = "center"
                var step = Math.max(1, Math.floor(n/4))
                for (var k = 0; k < n; k += step) {
                    var ts = candleModel.data(candleModel.index(k,0),0x0101)
                    var ds = new Date(ts).toLocaleString(Qt.locale(), isTimeShare?"hh:mm":"MM-dd")
                    ctx.fillText(ds, plotX+k*candleGap+candleGap/2, volBot-2)
                }
                for (var m = 0; m < n; m++) {
                    var vo=candleModel.data(candleModel.index(m,1),0x0102), vc=candleModel.data(candleModel.index(m,4),0x0105)
                    var vv=candleModel.data(candleModel.index(m,5),0x0106)
                    var bh = (vv/volMax)*(volH-14), vx=plotX+m*candleGap+candleGap/2-candleW/2
                    ctx.fillStyle = (vc>=vo)?upColor:downColor
                    ctx.fillRect(vx, volBot-bh-12, candleW, Math.max(1,bh))
                }
            }

            // 十字光标
            if (crosshair.visible) {
                ctx.strokeStyle = "#666688"; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(crosshair.mx, 0); ctx.lineTo(crosshair.mx, H); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(0, crosshair.my); ctx.lineTo(W, crosshair.my); ctx.stroke()
            }
        }

        MouseArea {
            anchors.fill: parent; hoverEnabled: true
            onPositionChanged: function(m) {
                if (!candleModel || candleModel.count===0) return
                var n = candleModel.count, pw = canvas.width-62, cg = pw/n
                var idx = Math.floor((m.x-4)/cg)
                if (idx<0||idx>=n){crosshair.visible=false;tooltip.visible=false;return}
                crosshair.visible=true; crosshair.mx=m.x; crosshair.my=m.y
                var o=candleModel.data(candleModel.index(idx,1),0x0102), h=candleModel.data(candleModel.index(idx,2),0x0103)
                var l=candleModel.data(candleModel.index(idx,3),0x0104), c=candleModel.data(candleModel.index(idx,4),0x0105)
                var v=candleModel.data(candleModel.index(idx,5),0x0105), ts=candleModel.data(candleModel.index(idx,0),0x0101)
                tooltip.show(m.x+12,Math.max(0,m.y-70),ts,o,h,l,c,v)
            }
            onExited: { crosshair.visible=false; tooltip.visible=false }
        }
    }

    Item { id: crosshair; anchors.fill: canvas; visible: false; property real mx:0; property real my:0 }

    CandleTooltip { id: tooltip; z: 200 }
}

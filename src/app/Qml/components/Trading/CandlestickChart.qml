// CandlestickChart.qml — 同花顺风格K线图 v2
// 主图(K线+MA) + 成交量副图(柱+MAVOL) + MACD副图(DIF/DEA/柱)
// 指标从 CandleDataModel role 读取 (C++预计算), 十字光标通过 CrosshairManager 联动
import QtQuick 2.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

Rectangle {
    id: root
    color: "#1a1a2e"

    property string stockCode:  "000001"
    property string stockName:  ""
    property int    chartPeriod: 7   // Daily
    property var    candleModel: null
    property var    dataLoader:  null
    property double preClose:    0.0
    property double latestPrice: 0.0
    property double avgLinePrice: 0.0  // 分时VWAP均价 (从MarketDataBridge读取)

    readonly property color upColor:    "#ef5350"
    readonly property color downColor:  "#26a69a"
    readonly property color frameBg:    "#1a1a2e"
    readonly property color panelBg:    "#222244"
    readonly property color gridColor:  "#3a3a5a"
    readonly property color borderColor:"#444466"
    readonly property color textDim:    "#8888aa"
    readonly property color textBright: "#d0d0e0"

    readonly property var periodLabels: ["分时","1分","5分","15分","30分","60分","120分","日线","周线","月线"]

    // ── C++ Role 常量 (与 CandleDataModel::Role 对齐) ──
    // QML 属性名必须小写开头, 用 readonly property 暴露 role 数值
    readonly property int roMa5:    Qt.UserRole + 100
    readonly property int roMa10:   Qt.UserRole + 101
    readonly property int roMa20:   Qt.UserRole + 102
    readonly property int roMa60:   Qt.UserRole + 103
    readonly property int roMaVol5: Qt.UserRole + 108
    readonly property int roMaVol10:Qt.UserRole + 109
    readonly property int roMacdDif: Qt.UserRole + 110
    readonly property int roMacdDea: Qt.UserRole + 111
    readonly property int roMacdHist:Qt.UserRole + 112

    // ── 画布布局比例 ──
    property double mainRatio: 0.60
    property double volRatio:  0.17
    property double macdRatio: 0.23

    // ── 价格/成交量/MACD范围 ──
    property double priceMin: 0; property double priceMax: 0
    property double volMax: 0
    property double macdMin: 0; property double macdMax: 0
    property double macdAbsMax: 0

    onChartPeriodChanged: {
        console.log("[CandlestickChart] period=" + chartPeriod)
        if (dataLoader) dataLoader.loadFromDB(stockCode, chartPeriod)
    }
    onStockCodeChanged: {
        console.log("[CandlestickChart] stockCode=" + stockCode)
        if (dataLoader) dataLoader.loadFromDB(stockCode, chartPeriod)
    }

    Connections {
        target: candleModel; enabled: candleModel !== null
        function onCountChanged()     { recalc(); requestAllPaint() }
        function onLastPriceChanged() {
            if (candleModel) root.latestPrice = candleModel.lastPrice
            recalc(); requestAllPaint()
        }
        function onPreCloseChanged()  {
            if (candleModel) root.preClose = candleModel.preClose
        }
    }

    // ── 监听 CrosshairManager ──
    Connections {
        target: Bridge.CrosshairManager
        enabled: Bridge.CrosshairManager !== null && Bridge.CrosshairManager !== undefined
        function onSelectedIndexChanged() { requestAllPaint() }
    }

    function requestAllPaint() {
        mainCanvas.requestPaint()
        volCanvas.requestPaint()
        macdCanvas.requestPaint()
    }

    function recalc() {
        if (!candleModel || candleModel.count === 0) return
        var n = candleModel.count
        priceMin = 1e18; priceMax = 0; volMax = 0
        var macdHi = -1e18, macdLo = 1e18
        for (var i = 0; i < n; i++) {
            var h = candleModel.data(candleModel.index(i, 2), 0x0103) // HighRole
            var l = candleModel.data(candleModel.index(i, 3), 0x0104) // LowRole
            var v = candleModel.data(candleModel.index(i, 5), 0x0106) // VolumeRole
            if (h > priceMax) priceMax = h; if (l < priceMin) priceMin = l
            if (v > volMax) volMax = v
            // MACD range
            var dif = candleModel.data(candleModel.index(i, 0), roMacdDif)
            var dea = candleModel.data(candleModel.index(i, 0), roMacdDea)
            var hist = candleModel.data(candleModel.index(i, 0), roMacdHist)
            if (dif !== undefined && dif !== null) {
                if (dif > macdHi) macdHi = dif; if (dif < macdLo) macdLo = dif
            }
            if (dea !== undefined && dea !== null) {
                if (dea > macdHi) macdHi = dea; if (dea < macdLo) macdLo = dea
            }
            if (hist !== undefined && hist !== null) {
                if (hist > macdHi) macdHi = hist; if (hist < macdLo) macdLo = hist
            }
        }
        // 确保昨收/最新价在范围内
        if (preClose > 0) { if (preClose > priceMax) priceMax = preClose; if (preClose < priceMin) priceMin = preClose }
        if (latestPrice > 0) { if (latestPrice > priceMax) priceMax = latestPrice; if (latestPrice < priceMin) priceMin = latestPrice }
        var pad = (priceMax - priceMin) * 0.08
        if (pad <= 0) pad = 0.5
        priceMin = Math.max(0, priceMin - pad); priceMax += pad

        // MACD 范围
        if (macdHi === -1e18 && macdLo === 1e18) { macdHi = 0; macdLo = 0 }
        macdAbsMax = Math.max(Math.abs(macdHi), Math.abs(macdLo)) * 1.15
        if (macdAbsMax < 0.01) macdAbsMax = 0.1
        macdMax = macdAbsMax; macdMin = -macdAbsMax
    }

    // ══════════════════════════════════════════════
    // 工具栏
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
    // 画布区域
    // ══════════════════════════════════════════════
    Item {
        id: canvasArea
        anchors { top: infoBar.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }

        // ── 主图 Canvas (K线 + MA) ──
        Canvas {
            id: mainCanvas
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: parent.height * mainRatio
            onPaint: drawMainChart(getContext("2d"), width, height)
        }

        // ── 主图/成交量分隔线 ──
        Rectangle {
            anchors { top: mainCanvas.bottom; left: parent.left; right: parent.right }
            height: 1; color: borderColor
        }

        // ── 成交量 Canvas ──
        Canvas {
            id: volCanvas
            anchors { top: mainCanvas.bottom; topMargin: 1; left: parent.left; right: parent.right }
            height: parent.height * volRatio
            onPaint: drawVolumeChart(getContext("2d"), width, height)
        }

        // ── 成交量/MACD分隔线 ──
        Rectangle {
            anchors { top: volCanvas.bottom; left: parent.left; right: parent.right }
            height: 1; color: borderColor
        }

        // ── MACD Canvas ──
        Canvas {
            id: macdCanvas
            anchors { top: volCanvas.bottom; topMargin: 1; left: parent.left; right: parent.right }
            height: parent.height * macdRatio
            onPaint: drawMacdChart(getContext("2d"), width, height)
        }

        // ── 十字光标覆盖层 MouseArea ──
        MouseArea {
            z: 10; anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            cursorShape: Qt.CrossCursor

            onPositionChanged: function(m) {
                if (!candleModel || candleModel.count===0){ hideCrosshair(); return }
                var n = candleModel.count, lm=6, rm=60
                var plotW = parent.width - lm - rm
                if (plotW <= 0 || n <= 0) { hideCrosshair(); return }
                var cg = plotW / n
                var idx = Math.floor((m.x - lm) / cg)
                if (idx<0||idx>=n){ hideCrosshair(); return }
                updateCrosshairIndex(idx)
                // 更新 tooltip
                var o=candleModel.data(candleModel.index(idx,1),0x0102),h=candleModel.data(candleModel.index(idx,2),0x0103)
                var l=candleModel.data(candleModel.index(idx,3),0x0104),c=candleModel.data(candleModel.index(idx,4),0x0105)
                var v=candleModel.data(candleModel.index(idx,5),0x0106),ts=candleModel.data(candleModel.index(idx,0),0x0101)
                tooltip.show(m.x+12, Math.max(0,m.y-70), ts, o, h, l, c, v)
            }
            onExited: { hideCrosshair(); tooltip.visible = false }
        }
    }

    // ══════════════════════════════════════════════
    // 坐标映射 (被三个 paint 函数共用)
    // ══════════════════════════════════════════════
    property int leftMargin: 4
    property int rightMargin: 58

    function plotWidth() { return canvasArea.width - leftMargin - rightMargin }
    function candleW()   { var n=candleModel?candleModel.count:1; return Math.max(1, plotWidth()/n*0.65) }
    function candleGap() { var n=candleModel?candleModel.count:1; return plotWidth()/n }
    function xForIndex(i) { return leftMargin + i * candleGap() + candleGap()/2 }

    function tradingMinute(ts) {
        var d = new Date(ts), m = d.getHours()*60 + d.getMinutes()
        if (m < 570) return 0; if (m >= 900) return 240
        if (m < 690) return m - 570
        if (m < 780) return 120
        return 120 + (m - 780)
    }
    function timeX(ts) { return leftMargin + tradingMinute(ts) / 240 * plotWidth() }

    // ── 主图 Y 轴映射 ──
    function yPrice(p, chartTop, mainH) {
        var range = priceMax - priceMin; if (range <= 0) range = 1
        return chartTop + mainH * (1 - (p - priceMin) / range)
    }

    // ── 十字光标 ──
    property int crosshairIndex: -1
    property bool crosshairVisible: false

    function updateCrosshairIndex(idx) {
        crosshairIndex = idx; crosshairVisible = true
        if (Bridge.CrosshairManager) Bridge.CrosshairManager.setSelectedIndex(idx)
        requestAllPaint()
    }
    function hideCrosshair() {
        crosshairVisible = false
        if (Bridge.CrosshairManager) Bridge.CrosshairManager.hide()
        requestAllPaint()
    }

    // ══════════════════════════════════════════════
    // drawMainChart
    // ══════════════════════════════════════════════
    function drawMainChart(ctx, W, H) {
        ctx.clearRect(0,0,W,H); ctx.fillStyle=frameBg; ctx.fillRect(0,0,W,H)
        if (!candleModel || candleModel.count===0) {
            ctx.fillStyle=textDim; ctx.font="13px sans-serif"; ctx.textAlign="center"
            ctx.fillText("暂无数据",W/2,H/2); return
        }

        var n = candleModel.count, pw = plotWidth(), px = leftMargin
        var chartTop = 2, mainH = H - 4
        var isTS = (chartPeriod === 0)
        var range = priceMax - priceMin; if (range<=0) range=1

        if (isTS) {
            // ── 分时图网格 ──
            var timeGrid = [{min:0,l:"09:30",b:true},{min:30,l:"10:00",b:false},{min:60,l:"10:30",b:false},
                            {min:90,l:"11:00",b:false},{min:120,l:"11:30/13:00",b:true},
                            {min:150,l:"13:30",b:false},{min:180,l:"14:00",b:false},
                            {min:210,l:"14:30",b:false},{min:240,l:"15:00",b:true}]
            for (var ti=0; ti<timeGrid.length; ti++) {
                var tg=timeGrid[ti], tx=leftMargin+tg.min/240*pw
                ctx.strokeStyle=tg.b?"#888888":gridColor; ctx.lineWidth=tg.b?1.2:0.6
                ctx.beginPath(); ctx.moveTo(tx,chartTop); ctx.lineTo(tx,chartTop+mainH); ctx.stroke()
                ctx.fillStyle=textDim; ctx.font="9px sans-serif"; ctx.textAlign="center"
                ctx.fillText(tg.l, tx, H-2)
            }
            // 横线网格
            ctx.strokeStyle=gridColor; ctx.lineWidth=0.7
            ctx.fillStyle=textDim; ctx.font="9px sans-serif"; ctx.textAlign="right"
            for (var g=0;g<5;g++){var gy=chartTop+mainH*g/4;ctx.beginPath();ctx.moveTo(px,gy);ctx.lineTo(px+pw,gy);ctx.stroke()
                ctx.fillText((priceMax-range*g/4).toFixed(2),px+pw,gy+3)}
            // 数据点
            var pts=[]; for(var j=0;j<n;j++){var cc=candleModel.data(candleModel.index(j,4),0x0105)
                var ts=candleModel.data(candleModel.index(j,0),0x0101)
                var vol=candleModel.data(candleModel.index(j,5),0x0106)
                var cy=yPrice(cc,chartTop,mainH),lx=timeX(ts);pts.push({x:lx,y:cy,price:cc,vol:vol})}
            // 渐变填充
            if(pts.length>1){ctx.beginPath();ctx.moveTo(pts[0].x,pts[0].y)
                for(var p=1;p<pts.length;p++)ctx.lineTo(pts[p].x,pts[p].y)
                var lx=pts[pts.length-1].x,cb=chartTop+mainH;ctx.lineTo(lx,cb);ctx.lineTo(pts[0].x,cb);ctx.closePath()
                var grad=ctx.createLinearGradient(0,chartTop,0,cb)
                var above=latestPrice>=preClose
                grad.addColorStop(0,above?"rgba(239,68,68,0.28)":"rgba(16,185,129,0.28)")
                grad.addColorStop(1,"rgba(0,0,0,0.01)");ctx.fillStyle=grad;ctx.fill()}
            // 价格折线
            ctx.strokeStyle="#f0f0f0";ctx.lineWidth=1.5;ctx.setLineDash([])
            ctx.beginPath();for(var k=0;k<pts.length;k++)k===0?ctx.moveTo(pts[k].x,pts[k].y):ctx.lineTo(pts[k].x,pts[k].y);ctx.stroke()
            // 均价线 (VWAP — 从 MarketDataBridge 或 LiveData 读取)
            // 累计VWAP曲线
            if(avgLinePrice>0&&pts.length>0){ctx.strokeStyle="#f59e0b";ctx.lineWidth=1.2;ctx.setLineDash([])
                ctx.beginPath();var cumV=0,cumVV=0;for(var k=0;k<pts.length;k++){
                    var vol=pts[k].vol||0;if(vol>0){cumV+=pts[k].price*vol;cumVV+=vol}
                    var vwap=cumVV>0?cumV/cumVV:pts[k].price;var vy=yPrice(vwap,chartTop,mainH)
                    k===0?ctx.moveTo(pts[k].x,vy):ctx.lineTo(pts[k].x,vy)};ctx.stroke()
                var ly=yPrice(avgLinePrice,chartTop,mainH);ctx.fillStyle="#f59e0b";ctx.textAlign="left"
                ctx.fillText("均价 "+avgLinePrice.toFixed(2),px+4,ly-2)}
        } else {
            // ── K线模式 ──
            // 网格 + Y轴
            ctx.strokeStyle=gridColor;ctx.lineWidth=0.5
            ctx.fillStyle=textDim;ctx.font="9px sans-serif";ctx.textAlign="right"
            for(var g=0;g<5;g++){var gy=chartTop+mainH*g/4;ctx.beginPath();ctx.moveTo(px,gy);ctx.lineTo(W,gy);ctx.stroke()
                ctx.fillText((priceMax-range*g/4).toFixed(2),W-4,gy+3)}
            if(preClose>0){var py=yPrice(preClose,chartTop,mainH);ctx.strokeStyle="#666688";ctx.setLineDash([4,4])
                ctx.beginPath();ctx.moveTo(px,py);ctx.lineTo(W,py);ctx.stroke();ctx.setLineDash([])}

            var cw=candleW(), cg=candleGap()
            // 画蜡烛
            var prevH=null,prevL=null,prevC=null,prevCx=null
            for(var i=0;i<n;i++){
                var o=candleModel.data(candleModel.index(i,1),0x0102),hi=candleModel.data(candleModel.index(i,2),0x0103)
                var lo=candleModel.data(candleModel.index(i,3),0x0104),c=candleModel.data(candleModel.index(i,4),0x0105)
                var cx=xForIndex(i),yO=yPrice(o,chartTop,mainH),yC=yPrice(c,chartTop,mainH)
                var yH=yPrice(hi,chartTop,mainH),yL=yPrice(lo,chartTop,mainH)
                // 跳空缺口
                if(prevH!==null){
                    var gapUp=lo>prevH, gapDn=hi<prevL
                    if(gapUp||gapDn){
                        var gpTop=yPrice(gapUp?Math.min(lo,o):Math.max(hi,o),chartTop,mainH)
                        var gpBot=yPrice(gapUp?Math.max(prevH,prevC):Math.min(prevL,prevC),chartTop,mainH)
                        if(gpTop>gpBot){var t=gpTop;gpTop=gpBot;gpBot=t}
                        var gl=prevCx+cw*0.3,gr=cx-cw*0.3
                        if(gr>gl&&gpBot>gpTop){ctx.fillStyle=gapUp?upColor:downColor;ctx.globalAlpha=0.30
                            ctx.fillRect(gl,gpTop,gr-gl,gpBot-gpTop);ctx.globalAlpha=1.0}
                    }
                }
                var col=(c>=o)?upColor:downColor;ctx.strokeStyle=col;ctx.fillStyle=col;ctx.lineWidth=1
                ctx.beginPath();ctx.moveTo(cx,yH);ctx.lineTo(cx,yL);ctx.stroke()
                var bh=Math.abs(yC-yO),by=Math.min(yO,yC);ctx.fillRect(cx-cw/2,by,cw,Math.max(1,bh))
                prevH=hi;prevL=lo;prevC=c;prevCx=cx
            }

            // ── MA 线 (从 C++ role 读取) ──
            drawMaLine(ctx, 5,  "#f4f4f4", 0.8, roMa5,  chartTop, mainH)
            drawMaLine(ctx, 10, "#f59e0b", 0.8, roMa10, chartTop, mainH)
            drawMaLine(ctx, 20, "#c084fc", 0.8, roMa20, chartTop, mainH)
            drawMaLine(ctx, 60, "#22d3ee", 0.8, roMa60, chartTop, mainH)
        }

        // ── 十字光标竖直/水平线 ──
        drawCrosshairLines(ctx, W, H, chartTop, chartTop+mainH)
    }

    // ── MA 线绘制 (主图, 价格Y轴) ──
    function drawMaLine(ctx, period, color, lw, role, chartTop, mainH) {
        if(!candleModel||candleModel.count<period)return
        ctx.strokeStyle=color;ctx.lineWidth=lw;ctx.beginPath()
        var firstValid=true
        for(var i=period-1;i<candleModel.count;i++){
            var ma=candleModel.data(candleModel.index(i,0),role)
            if(ma===undefined||ma===null||ma<=0)continue
            var mx=xForIndex(i),my=yPrice(ma,chartTop,mainH)
            if(firstValid){ctx.moveTo(mx,my);firstValid=false}else ctx.lineTo(mx,my)
        }
        ctx.stroke()
    }

    // ── MAVOL 线绘制 (成交量副图, 成交量Y轴) ──
    function drawVolMaLine(ctx, period, color, lw, role, topY, botY, volH) {
        if(!candleModel||candleModel.count<period||volMax<=0)return
        ctx.strokeStyle=color;ctx.lineWidth=lw;ctx.beginPath()
        var firstValid=true
        for(var i=period-1;i<candleModel.count;i++){
            var ma=candleModel.data(candleModel.index(i,0),role)
            if(ma===undefined||ma===null||ma<=0)continue
            var mx=xForIndex(i),my=botY-(ma/volMax)*volH
            if(firstValid){ctx.moveTo(mx,my);firstValid=false}else ctx.lineTo(mx,my)
        }
        ctx.stroke()
    }

    // ══════════════════════════════════════════════
    // drawVolumeChart
    // ══════════════════════════════════════════════
    function drawVolumeChart(ctx, W, H) {
        ctx.clearRect(0,0,W,H);ctx.fillStyle=frameBg;ctx.fillRect(0,0,W,H)
        if(!candleModel||candleModel.count===0||volMax<=0){ctx.fillStyle=textDim;ctx.font="10px sans-serif";ctx.textAlign="center";ctx.fillText("VOL",W/2,H/2);return}
        var n=candleModel.count,pw=plotWidth(),isTS=(chartPeriod===0)
        var cw=candleW(),cg=candleGap(),top=2,bot=H-14
        var volH=bot-top

        for(var i=0;i<n;i++){
            var o=candleModel.data(candleModel.index(i,1),0x0102),c=candleModel.data(candleModel.index(i,4),0x0105)
            var v=candleModel.data(candleModel.index(i,5),0x0106)
            var bh=(v/volMax)*volH
            var vx=isTS?timeX(candleModel.data(candleModel.index(i,0),0x0101))-cw/2:xForIndex(i)-cw/2
            ctx.fillStyle=(c>=o)?upColor:downColor
            ctx.fillRect(vx,bot-bh,cw,Math.max(1,bh))
        }
        // MAVOL 线 (Y轴用成交量范围映射)
        if(!isTS) {
            drawVolMaLine(ctx, 5,  "#f4f4f4", 0.7, roMaVol5, top, bot, volH)
            drawVolMaLine(ctx, 10, "#f59e0b", 0.7, roMaVol10, top, bot, volH)
        }
        // X轴时间标签 (K线模式)
        if(!isTS){
            ctx.fillStyle=textDim;ctx.textAlign="center"
            var step=Math.max(1,Math.floor(n/4))
            for(var k=0;k<n;k+=step){
                var ts=candleModel.data(candleModel.index(k,0),0x0101)
                ctx.fillText(new Date(ts).toLocaleString(Qt.locale(),"MM-dd"),xForIndex(k),H-2)
            }
        }
        // 分隔线
        ctx.strokeStyle=borderColor;ctx.lineWidth=1
        ctx.beginPath();ctx.moveTo(0,top);ctx.lineTo(W,top);ctx.stroke()
    }

    // ══════════════════════════════════════════════
    // drawMacdChart
    // ══════════════════════════════════════════════
    function drawMacdChart(ctx, W, H) {
        ctx.clearRect(0,0,W,H);ctx.fillStyle=frameBg;ctx.fillRect(0,0,W,H)
        if(!candleModel||candleModel.count<26||chartPeriod===0){
            ctx.fillStyle=textDim;ctx.font="10px sans-serif";ctx.textAlign="center"
            ctx.fillText(chartPeriod===0?"分时图无MACD":"MACD(12,26,9)",W/2,H/2);return
        }
        var n=candleModel.count,top=2,bot=H-12,macdH=bot-top
        var rangeH=macdMax-macdMin;if(rangeH<=0)rangeH=1
        var zeroY=top+macdH*(1-(0-macdMin)/rangeH)
        var cw=candleW(),cg=candleGap()

        // 零轴
        ctx.strokeStyle="#404860";ctx.lineWidth=0.8
        ctx.beginPath();ctx.moveTo(leftMargin,zeroY);ctx.lineTo(W-rightMargin,zeroY);ctx.stroke()

        // 柱状图
        var lastHist=null
        for(var i=0;i<n;i++){
            var hist=candleModel.data(candleModel.index(i,0),roMacdHist)
            if(hist===undefined||hist===null)continue
            var hx=xForIndex(i)-cw/2
            var hy=top+macdH*(1-(hist-macdMin)/rangeH)
            var bh=Math.abs(hy-zeroY)
            if(hist>=0){
                var alpha=1.0
                if(lastHist!==null&&hist<lastHist)alpha=0.6
                ctx.fillStyle="rgba(239,83,80,"+alpha+")"
                ctx.fillRect(hx,hy,cw,Math.max(1,bh))
            } else {
                alpha=1.0;if(lastHist!==null&&Math.abs(hist)<Math.abs(lastHist))alpha=0.6
                ctx.fillStyle="rgba(38,166,154,"+alpha+")"
                ctx.fillRect(hx,zeroY,cw,Math.max(1,bh))
            }
            lastHist=hist
        }

        // DIF 线 (白)
        drawIndicatorLine(ctx, roMacdDif, "#f4f4f4", 0.8, top, macdH)
        // DEA 线 (黄)
        drawIndicatorLine(ctx, roMacdDea, "#f59e0b", 0.8, top, macdH)

        // Y轴标签
        ctx.fillStyle=textDim;ctx.font="9px sans-serif";ctx.textAlign="right"
        ctx.fillText(macdMax.toFixed(3),W-4,top+10)
        ctx.fillText("0",W-4,zeroY+3)
        ctx.fillText(macdMin.toFixed(3),W-4,bot)
    }

    function drawIndicatorLine(ctx, role, color, lw, chartTop, chartH) {
        if(!candleModel)return
        ctx.strokeStyle=color;ctx.lineWidth=lw;ctx.beginPath()
        var first=true,range=macdMax-macdMin;if(range<=0)range=1
        for(var i=0;i<candleModel.count;i++){
            var v=candleModel.data(candleModel.index(i,0),role)
            if(v===undefined||v===null)continue
            var mx=xForIndex(i),my=chartTop+chartH*(1-(v-macdMin)/range)
            if(first){ctx.moveTo(mx,my);first=false}else ctx.lineTo(mx,my)
        }
        ctx.stroke()
    }

    // ── 十字光标垂直线 ──
    function drawCrosshairLines(ctx, W, H, topY, botY) {
        if(!crosshairVisible||crosshairIndex<0||!candleModel)return
        var cx=xForIndex(crosshairIndex)
        ctx.strokeStyle="#888899";ctx.lineWidth=0.8;ctx.setLineDash([3,3])
        ctx.beginPath();ctx.moveTo(cx,topY);ctx.lineTo(cx,H);ctx.stroke()
        ctx.setLineDash([])
    }

    // ══════════════════════════════════════════════
    // 十字光标 Tooltip
    // ══════════════════════════════════════════════
    CandleTooltip { id: tooltip; z: 200 }
}

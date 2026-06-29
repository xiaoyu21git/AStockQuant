import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/TradingConstants.js" as Const

// ── CompactChart — 内嵌 K 线图（gmsdk history_bars）──
Rectangle {
    id: root

    property var marketDataService: null
    property string symbol: ""
    property var displayPositions: []
    property var signals: []

    property var klineCache: ({})
    property var currentCandles: []

    radius: 24
    color: Const.tradingPanelBgAlt
    border.color: Const.tradingPanelBorderAlt
    border.width: 1
    clip: true

    function normalizeSymbol(raw) {
        var s = String(raw || "").trim().toUpperCase()
        if (s.length === 0) return s
        if (s.indexOf(".") >= 0) return s
        if (s.indexOf("6") === 0 || s.indexOf("5") === 0 || s.indexOf("9") === 0) return s + ".SH"
        if (s.indexOf("0") === 0 || s.indexOf("3") === 0 || s.indexOf("2") === 0) return s + ".SZ"
        if (s.indexOf("8") === 0 || s.indexOf("4") === 0) return s + ".BJ"
        return s
    }

    Component.onCompleted: { if (root.symbol && root.marketDataService) loadKline() }

    function loadKline() {
        var sym = normalizeSymbol(root.symbol)
        if (!sym || !root.marketDataService) return
        if (klineCache[sym]) { currentCandles = klineCache[sym]; return }
        if (typeof marketDataService.loadBars !== "function") return
        var today = new Date()
        var y = today.getFullYear(), m = String(today.getMonth()+1).padStart(2,"0"), d = String(today.getDate()).padStart(2,"0")
        marketDataService.loadBars([sym], (y-1)+"-"+m+"-"+d, y+"-"+m+"-"+d)
    }

    function convertToCandles(raw) {
        if (!raw || raw.length === 0) return []
        var result = []
        for (var i = 0; i < raw.length; i++) {
            var r = raw[i] || {}
            result.push({
                open: Number(r.open || 0), high: Number(r.high || 0),
                low: Number(r.low || 0), close: Number(r.close || 0),
                volume: Number(r.volume || 0), time: String(r.time || r.date || "")
            })
        }
        result.sort(function(a,b) { return a.time < b.time ? -1 : a.time > b.time ? 1 : 0 })
        return result
    }

    onSymbolChanged: {
        if (root.symbol) {
            klineCache = {}  // 清缓存强制重新加载, 避免 updateTodayBar 的 1 根 bar 被缓存
            loadKline()
        }
    }

    Connections {
        target: root.marketDataService
        enabled: !!root.marketDataService
        function onBarsChanged() {
            var sym = normalizeSymbol(root.symbol)
            var candles = convertToCandles(root.marketDataService.bars || [])
            if (candles.length > 0) { klineCache[sym] = candles; currentCandles = candles }
        }
    }

    KLineChart {
        anchors.fill: parent; anchors.margins: 6
        symbol: root.symbol; candles: root.currentCandles; signals: root.signals
    }
}

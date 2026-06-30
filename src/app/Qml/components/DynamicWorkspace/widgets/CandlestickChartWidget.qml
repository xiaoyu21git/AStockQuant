// CandlestickChartWidget.qml — K线图 Widget 包装器 v2
import QtQuick 2.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading"

CandlestickChart {
    id: root
    anchors.fill: parent
    candleModel: Bridge.CandleDataModel
    dataLoader: Bridge.StockDataLoader

    // ── symbol 由 C++ MarketDataBridge.primarySymbol 驱动 ──
    readonly property string exchangeSymbol: Bridge.MarketDataBridge
        && Bridge.MarketDataBridge.primarySymbol
        ? Bridge.MarketDataBridge.primarySymbol : "000001.SZ"

    readonly property string pureCode: {
        var sym = exchangeSymbol
        var dot = sym.indexOf(".")
        return dot > 0 ? sym.substring(0, dot) : sym
    }

    stockCode: pureCode

    // ── 分时 VWAP 均价线: 从 MarketDataBridge 的 snapshot 中取 avgLine ──
    readonly property var activeSnapshot: Bridge.MarketDataBridge
        && Bridge.MarketDataBridge.marketSnapshots
        ? (function() {
            var snaps = Bridge.MarketDataBridge.marketSnapshots
            for (var i = 0; i < snaps.length; i++) {
                var s = snaps[i]
                if (s && s.symbol && s.symbol.toUpperCase().indexOf(pureCode.toUpperCase()) >= 0) {
                    return s
                }
            }
            return null
          })()
        : null

    avgLinePrice: activeSnapshot && activeSnapshot.avgLine ? activeSnapshot.avgLine : 0.0

    // ── 分时图 tick 触发重绘 ──
    Connections {
        target: Bridge.StockDataLoader
        enabled: Bridge.StockDataLoader !== null
        function onTickReceived(symbol, price, volume) {
            if (root.chartPeriod === 0)
                root.requestAllPaint()
        }
    }

    Component.onCompleted: {
        console.log("[CandlestickChartWidget] created, stockCode=" + root.stockCode
                    + " period=" + root.chartPeriod + " modelCount="
                    + (root.candleModel ? root.candleModel.count : "null"))
        if (root.dataLoader && root.stockCode)
            root.dataLoader.loadFromDB(root.stockCode, root.chartPeriod)
    }
}

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
    avgLinePrice: {
        var snaps = Bridge.MarketDataBridge ? Bridge.MarketDataBridge.marketSnapshots : ({})
        var s = snaps[exchangeSymbol] || ({})
        return s.avgLine || 0.0
    }

    // ── 分时图 tick 触发重绘 ──
    Connections {
        target: Bridge.StockDataLoader
        enabled: Bridge.StockDataLoader !== null
        function onTickReceived(symbol, price, volume) {
            if (root.chartPeriod === 0)
                root.requestAllPaint()
        }
    }

    onStockCodeChanged: {
        if (root.dataLoader && root.stockCode)
            root.dataLoader.loadFromDB(root.stockCode, root.chartPeriod)
    }
    onChartPeriodChanged: {
        if (root.dataLoader && root.stockCode)
            root.dataLoader.loadFromDB(root.stockCode, root.chartPeriod)
    }

    Component.onCompleted: {
        if (root.dataLoader && root.stockCode)
            root.dataLoader.loadFromDB(root.stockCode, root.chartPeriod)
    }
}

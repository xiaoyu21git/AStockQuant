// CandlestickChartWidget.qml
import QtQuick 2.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading"

CandlestickChart {
    id: root
    anchors.fill: parent
    candleModel: Bridge.CandleDataModel
    dataLoader: Bridge.StockDataLoader

    // ════════════════════════════════════════════════════════════
    // symbol 由 C++ MarketDataBridge.primarySymbol 驱动
    // OrderFormWidget / ensureWatchSymbol → primarySymbol 变更
    // → stockCode 绑定自动更新 → onStockCodeChanged → loadFromDB
    // ════════════════════════════════════════════════════════════
    readonly property string exchangeSymbol: Bridge.MarketDataBridge
        && Bridge.MarketDataBridge.primarySymbol
        ? Bridge.MarketDataBridge.primarySymbol : "000001.SZ"

    // 纯数字代码提取: "000001.SZ" → "000001"
    readonly property string pureCode: {
        var sym = exchangeSymbol
        var dot = sym.indexOf(".")
        return dot > 0 ? sym.substring(0, dot) : sym
    }

    stockCode: pureCode

    Connections {
        target: Bridge.StockDataLoader
        enabled: Bridge.StockDataLoader !== null
        function onTickReceived(symbol, price, volume) {
            // 分时图需要每次 tick 都重绘 (价格线延伸);
            // K线模式下 onLastPriceChanged 已经触发了重绘
            if (root.chartPeriod === 0)
                root.requestRepaint()
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

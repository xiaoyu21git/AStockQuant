import QtQuick 2.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading" as Trading

Trading.CompactChart {
    id: chart
    property var widgetConfig: ({})
    symbol: widgetConfig.symbol || (Bridge.MarketDataBridge ? Bridge.MarketDataBridge.primarySymbol : "") || "000001.SZ"
    marketDataService: Bridge.MarketDataBridge
}

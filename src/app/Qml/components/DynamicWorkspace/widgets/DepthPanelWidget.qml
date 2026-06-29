import QtQuick 2.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading" as Trading

Trading.DepthMarketPanel {
    id: panel
    property var widgetConfig: ({})
    scaleFactor: Math.min(1.0, Math.max(0.4, height / 400))
    compactMode: true

    // symbol 由 C++ primarySymbol 驱动
    readonly property string currentSymbol: Bridge.MarketDataBridge
        && Bridge.MarketDataBridge.primarySymbol
        ? Bridge.MarketDataBridge.primarySymbol : ""

    activeSymbol: currentSymbol

    readonly property var bridge: Bridge.MarketDataBridge
    property var snap: ({})

    function refresh() {
        if (!bridge || !bridge.marketSnapshots) return
        var snaps = bridge.marketSnapshots
        snap = snaps[currentSymbol] || ({})
    }

    Component.onCompleted: refresh()

    Connections {
        target: bridge
        function onMarketSnapshotsChanged() { refresh() }
    }

    marketSnapshot: snap
    depthSnapshot: snap && snap.depthSnapshot ? snap.depthSnapshot
                   : ({ bids: [], asks: [], totalBid: 0, totalAsk: 0 })

    onCurrentSymbolChanged: refresh()
}

import QtQuick 2.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading" as Trading

Flickable {
    id: root
    property var widgetConfig: ({})
    implicitHeight: 450
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    contentWidth: ws.width
    contentHeight: ws.height
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    Trading.TradingChartWorkspace {
        id: ws
        width: root.width
        height: Math.max(root.height, implicitHeight)
        marketDataService: Bridge.MarketDataBridge
        displayPositions: Bridge.PositionAccountBridge.positions || []
        seedSymbol: root.widgetConfig.symbol || ""
        clip: true
    }
}

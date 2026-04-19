import QtQuick 2.15
import ConsoleUi 1.0

Item {
    id: chartArea

    property var marketData: []
    property var marketDataService: null
    property var displayPositions: []
    property string currentSymbol: ""
    property string currencySymbol: "¥"
    property bool autoWatchSymbols: true
    property int selectedPeriodIndex: 0
    readonly property var quoteData: currentQuote()
    readonly property real latestPrice: Number(quoteData.price !== undefined ? quoteData.price : (quoteData.close || 0))
    readonly property real dayChange: Number(quoteData.change || 0)

    function currentQuote() {
        for (var index = 0; index < marketData.length; ++index) {
            if ((marketData[index] || {}).symbol === currentSymbol) {
                return marketData[index]
            }
        }
        return marketData.length > 0 ? marketData[0] : ({ symbol: currentSymbol, name: "", price: 0, change: 0 })
    }

    TradingChartWorkspace {
        anchors.fill: chartArea
        marketDataService: chartArea.marketDataService
        displayPositions: chartArea.displayPositions
        seedSymbol: chartArea.currentSymbol
        autoWatchSymbols: chartArea.autoWatchSymbols
    }
}

import QtQuick 2.15
import QtCharts 2.15

Item {
    id: root

    property var candleSeries: []
    property var ma5Series: []
    property var ma10Series: []
    property var ma20Series: []
    property bool showMa5: true
    property bool showMa10: true
    property bool showMa20: false
    property real chartLow: 0
    property real chartHigh: 1

    Component {
        id: candlestickSetComponent

        CandlestickSet {}
    }

    function appendLinePoints(seriesObject, points) {
        if (!seriesObject) {
            return
        }

        seriesObject.clear()
        for (var index = 0; index < points.length; ++index) {
            var point = points[index] || ({})
            seriesObject.append(Number(point.index || 0), Number(point.value || 0))
        }
    }

    function refreshCandles() {
        candleQtSeries.clear()
        for (var index = 0; index < candleSeries.length; ++index) {
            var candle = candleSeries[index] || ({})
            var setObject = candlestickSetComponent.createObject(root, {
                timestamp: index,
                open: Number(candle.open || 0),
                high: Number(candle.high || 0),
                low: Number(candle.low || 0),
                close: Number(candle.close || 0)
            })
            candleQtSeries.append(setObject)
        }
    }

    function refreshMovingAverages() {
        appendLinePoints(ma5LineSeries, ma5Series || [])
        appendLinePoints(ma10LineSeries, ma10Series || [])
        appendLinePoints(ma20LineSeries, ma20Series || [])
    }

    function refreshAllSeries() {
        refreshCandles()
        refreshMovingAverages()
    }

    onCandleSeriesChanged: refreshAllSeries()
    onMa5SeriesChanged: refreshMovingAverages()
    onMa10SeriesChanged: refreshMovingAverages()
    onMa20SeriesChanged: refreshMovingAverages()
    Component.onCompleted: refreshAllSeries()

    ChartView {
        id: chartView
        anchors.fill: parent
        antialiasing: true
        legend.visible: false
        backgroundColor: "transparent"
        plotAreaColor: "transparent"
        margins.top: 0
        margins.bottom: 0
        margins.left: 0
        margins.right: 0

        ValueAxis {
            id: axisX
            min: 0
            max: Math.max(1, root.candleSeries.length - 1)
            tickCount: Math.min(6, Math.max(2, root.candleSeries.length > 1 ? 6 : 2))
            labelsColor: "transparent"
            gridLineColor: "#243042"
            lineVisible: false
            shadesVisible: false
        }

        ValueAxis {
            id: axisY
            min: root.chartLow
            max: Math.max(root.chartLow + 0.01, root.chartHigh)
            tickCount: 5
            labelsColor: "transparent"
            gridLineColor: "transparent"
            lineVisible: false
            shadesVisible: false
        }

        CandlestickSeries {
            id: candleQtSeries
            axisX: axisX
            axisY: axisY
            increasingColor: "#ef4444"
            decreasingColor: "#10b981"
            bodyOutlineVisible: true
            capsVisible: false
            bodyWidth: 0.92
        }

        LineSeries {
            id: ma5LineSeries
            axisX: axisX
            axisY: axisY
            visible: root.showMa5
            color: "#f59e0b"
            width: 1.1
        }

        LineSeries {
            id: ma10LineSeries
            axisX: axisX
            axisY: axisY
            visible: root.showMa10
            color: "#38bdf8"
            width: 1.1
        }

        LineSeries {
            id: ma20LineSeries
            axisX: axisX
            axisY: axisY
            visible: root.showMa20
            color: "#a78bfa"
            width: 1.1
        }
    }
}
import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/TradingConstants.js" as Const

Rectangle {
    id: root
    property var widgetConfig: ({})
    color: Const.tradingPanelBg
    radius: 10
    border.color: Const.tradingPanelBorder
    border.width: 1
    clip: true
    property real scaleFactor: Math.min(1.0, Math.max(0.4, height / 400))

    readonly property var bridge: Bridge.MarketDataBridge
    readonly property var sym: widgetConfig.symbol || (bridge ? bridge.primarySymbol : "") || ""
    property var snap: ({})
    property var bids: []
    property var asks: []

    function refresh() {
        var s = bridge && bridge.marketSnapshots ? bridge.marketSnapshots : ({})
        snap = s[sym] || (Object.keys(s).length > 0 ? s[Object.keys(s)[0]] : ({}))
        var d = snap.depthSnapshot || ({})
        bids = d.bids || []
        asks = d.asks || []
    }
    Component.onCompleted: refresh()
    Connections { target: bridge; function onMarketSnapshotsChanged() { refresh() } }
    onSymChanged: refresh()

    readonly property real maxVol: {
        var m = 0
        for (var i = 0; i < Math.min(bids.length,5); i++) m = Math.max(m, Number(bids[i].volume)||0)
        for (var j = 0; j < Math.min(asks.length,5); j++) m = Math.max(m, Number(asks[j].volume)||0)
        return m > 0 ? m : 1
    }
    function s(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    function fmtVol(v) { var n = Number(v)||0; return n>=1e8 ? (n/1e8).toFixed(1)+"亿" : n>=1e4 ? (n/1e4).toFixed(0)+"万" : String(n) }
    function barW(v) { return Math.max(2, (Number(v)||0) / maxVol * s(50)) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: s(6)
        spacing: s(2)

        Text { text: sym || "--"; color: "#F1F5F9"; font.pixelSize: s(12); font.weight: Font.Bold }
        RowLayout { Layout.fillWidth: true; spacing: s(6)
            Text { text: (snap.price||"--"); color: "#F1F5F9"; font.pixelSize: s(16); font.weight: Font.Bold }
            Text { text: (snap.changePercent||"--"); color: snap.isUp ? "#EF4444" : "#10B981"; font.pixelSize: s(11) }
        }

        // 卖盘 5档
        ColumnLayout { Layout.fillWidth: true; spacing: 1
            Repeater {
                model: Math.min(asks.length, 5)
                RowLayout { Layout.fillWidth: true; spacing: s(3)
                    Text { text: "卖"+(5-index); color: "#10B981"; font.pixelSize: s(9); Layout.preferredWidth: s(18) }
                    Rectangle { Layout.preferredWidth: barW(asks[index].volume); Layout.preferredHeight: s(10); radius: 2; color: "rgba(16,185,129,0.3)" }
                    Text { text: Number(asks[index].price).toFixed(2); color: "#10B981"; font.pixelSize: s(9); Layout.preferredWidth: s(44) }
                    Text { text: fmtVol(asks[index].volume); color: "#94A3B8"; font.pixelSize: s(9); Layout.fillWidth: true }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#334155" }

        // 买盘 5档
        ColumnLayout { Layout.fillWidth: true; spacing: 1
            Repeater {
                model: Math.min(bids.length, 5)
                RowLayout { Layout.fillWidth: true; spacing: s(3)
                    Text { text: "买"+(index+1); color: "#EF4444"; font.pixelSize: s(9); Layout.preferredWidth: s(18) }
                    Rectangle { Layout.preferredWidth: barW(bids[index].volume); Layout.preferredHeight: s(10); radius: 2; color: "rgba(239,68,68,0.3)" }
                    Text { text: Number(bids[index].price).toFixed(2); color: "#EF4444"; font.pixelSize: s(9); Layout.preferredWidth: s(44) }
                    Text { text: fmtVol(bids[index].volume); color: "#94A3B8"; font.pixelSize: s(9); Layout.fillWidth: true }
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}

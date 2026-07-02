// SectorHeatWidget.qml — 热门板块排名 (掘金SDK实时数据, 密度自适应)
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../Trading"
import "../../utils/TradingConstants.js" as Const
import "../../utils/TradingWidgetBase.js" as Base
import AStock.Bridge 1.0 as Bridge

Item {
    id: root

    property var widgetConfig: ({})
    readonly property int optimalHeight: 400
    readonly property real scaleFactor: Base.computeScaleFactor(height, optimalHeight)
    readonly property string densityMode: Base.computeDensityMode(height, optimalHeight, _densityCache)
    property string _densityCache: "normal"

    function _sf(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    readonly property int rowH: Math.max(18, Math.round((height - headerH) / Math.max(1, _model.length)))
    readonly property int headerH: _sf(28)
    readonly property int colNameW: width * 0.38
    readonly property int colChgW:  width * 0.19
    readonly property int colLeadW: width * 0.26
    readonly property int colLChgW: width * 0.17
    readonly property int fs: _sf(densityMode==="compact"?9:(densityMode==="expanded"?13:11))
    readonly property int fsSmall: _sf(densityMode==="compact"?8:10)

    readonly property color upC: Const.tradingBuyRed
    readonly property color downC: Const.depthLimitDownGreen

    readonly property var _model: {
        var raw = Bridge.MarketDataBridge ? Bridge.MarketDataBridge.sectorHeatData : []
        return raw && raw.length ? raw : []
    }

    Timer {
        id: refreshTimer; interval: 60000; running: true; repeat: true
        onTriggered: { if (Bridge.MarketDataBridge) Bridge.MarketDataBridge.fetchSectorHeat() }
    }
    Component.onCompleted: {
        if (Bridge.MarketDataBridge) Bridge.MarketDataBridge.fetchSectorHeat()
    }

    // header
    Rectangle {
        id: header; anchors { top: parent.top; left: parent.left; right: parent.right }
        height: headerH; color: "#1a1a2e"
        RowLayout {
            anchors.fill: parent; anchors.margins: 4; spacing: 2
            Text { text: "板块"; color: "#888"; font.pixelSize: fsSmall; Layout.preferredWidth: colNameW; horizontalAlignment: Text.AlignLeft; leftPadding: 4 }
            Text { text: "涨幅%"; color: "#888"; font.pixelSize: fsSmall; Layout.preferredWidth: colChgW; horizontalAlignment: Text.AlignHCenter }
            Text { text: "领涨股"; color: "#888"; font.pixelSize: fsSmall; Layout.preferredWidth: colLeadW; horizontalAlignment: Text.AlignHCenter }
            Text { text: "涨幅%"; color: "#888"; font.pixelSize: fsSmall; Layout.preferredWidth: colLChgW; horizontalAlignment: Text.AlignHCenter }
        }
    }

    // rows
    ListView {
        id: listView
        anchors { top: header.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        clip: true; model: _model
        delegate: Rectangle {
            width: listView.width; height: rowH
            color: index % 2 === 0 ? "#1a1a2e" : "#1f1f3a"
            RowLayout {
                anchors.fill: parent; anchors.margins: 2; spacing: 0
                Text { text: modelData.name || ""; color: "#d0d0e0"; font.pixelSize: fs; elide: Text.ElideRight; Layout.preferredWidth: colNameW; horizontalAlignment: Text.AlignLeft; leftPadding: 4 }
                Text {
                    text: { var c = modelData.chg||0; return (c>0?"+":"")+c.toFixed(2) }
                    color: (modelData.chg||0)>=0 ? upC : downC; font.pixelSize: fs; font.weight: Font.DemiBold
                    Layout.preferredWidth: colChgW; horizontalAlignment: Text.AlignHCenter
                }
                Text { text: modelData.lead || "--"; color: "#d0d0e0"; font.pixelSize: fs; elide: Text.ElideRight; Layout.preferredWidth: colLeadW; horizontalAlignment: Text.AlignHCenter }
                Text {
                    text: { var lc = modelData.leadChg||0; return lc!==0 ? ((lc>0?"+":"")+lc.toFixed(1)) : "--" }
                    color: (modelData.leadChg||0)>=0 ? upC : downC; font.pixelSize: fs
                    Layout.preferredWidth: colLChgW; horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    onDensityModeChanged: { _densityCache = densityMode }
    Connections {
        target: Bridge.MarketDataBridge
        enabled: Bridge.MarketDataBridge !== null
        function onSectorHeatDataChanged() { /* auto-update via _model binding */ }
    }
}

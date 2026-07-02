// SectorHeatWidget.qml — 热门板块 (掘金实时数据, 资金流入+信号分析)
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
    readonly property int optimalHeight: 420
    readonly property real scaleFactor: Base.computeScaleFactor(height, optimalHeight)
    readonly property string densityMode: Base.computeDensityMode(height, optimalHeight, _densityCache)
    property string _densityCache: "normal"

    function _sf(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    readonly property int rowH: Math.max(20, Math.round((height - headerH) / Math.max(1, _model.length)))
    readonly property int headerH: _sf(30)
    readonly property int fs: _sf(densityMode==="compact"?9:(densityMode==="expanded"?13:11))
    readonly property int fsSm: _sf(densityMode==="compact"?8:10)

    readonly property color upC: Const.tradingBuyRed
    readonly property color dnC: Const.depthLimitDownGreen
    readonly property color nc: "#888"

    readonly property var _model: {
        var raw = Bridge.MarketDataBridge ? Bridge.MarketDataBridge.sectorHeatData : []
        return raw && raw.length ? raw : []
    }

    function signalTag(s) {
        switch(s) { case 0: return "🟢 真机会"; case 1: return "🔴 诱多"; case 2: return "🟡 抄底"; default: return "⚪ 走弱" }
    }
    function signalColor(s) {
        switch(s) { case 0: return "#22c55e"; case 1: return "#ef4444"; case 2: return "#f59e0b"; default: return "#888" }
    }
    function fmtMoney(v) {
        var a = Math.abs(v)
        if (a >= 1e8) return (v/1e8).toFixed(2)+"亿"
        if (a >= 1e4) return (v/1e4).toFixed(2)+"万"
        return v.toFixed(0)
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
            anchors.fill: parent; anchors.margins: 3; spacing: 1
            Text { text: "板块"; color: "#888"; font.pixelSize: fsSm; Layout.preferredWidth: width*0.16; horizontalAlignment: Text.AlignLeft; leftPadding: 4 }
            Text { text: "涨幅%"; color: "#888"; font.pixelSize: fsSm; Layout.preferredWidth: width*0.11; horizontalAlignment: Text.AlignHCenter }
            Text { text: "主力净流入"; color: "#888"; font.pixelSize: fsSm; Layout.preferredWidth: width*0.18; horizontalAlignment: Text.AlignHCenter }
            Text { text: "净流入率"; color: "#888"; font.pixelSize: fsSm; Layout.preferredWidth: width*0.11; horizontalAlignment: Text.AlignHCenter }
            Text { text: "领涨股"; color: "#888"; font.pixelSize: fsSm; Layout.preferredWidth: width*0.18; horizontalAlignment: Text.AlignHCenter }
            Text { text: "信号"; color: "#888"; font.pixelSize: fsSm; Layout.preferredWidth: width*0.18; horizontalAlignment: Text.AlignHCenter }
            Text { text: "涨幅"; color: "#888"; font.pixelSize: fsSm; Layout.preferredWidth: width*0.08; horizontalAlignment: Text.AlignHCenter }
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
            Rectangle { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                width: 3; height: parent.height*0.6; radius: 1;
                color: signalColor(modelData.signal||3)
            }
            RowLayout {
                anchors.fill: parent; anchors.margins: 2; spacing: 0
                // 板块名
                Text { text: modelData.name||""; color: "#d0d0e0"; font.pixelSize: fs; elide: Text.ElideRight
                       Layout.preferredWidth: parent.width*0.16; horizontalAlignment: Text.AlignLeft; leftPadding: 6 }
                // 涨幅
                Text { text: { var c=modelData.chg||0; return (c>0?"+":"")+c.toFixed(2) }
                       color: c>=0?upC:dnC; font.pixelSize: fs; font.weight: Font.DemiBold
                       Layout.preferredWidth: parent.width*0.11; horizontalAlignment: Text.AlignHCenter }
                // 主力净流入
                Text { text: { var n=modelData.netIn||0; return (n>0?"+":"")+fmtMoney(n) }
                       color: (modelData.netIn||0)>=0?upC:dnC; font.pixelSize: fs
                       Layout.preferredWidth: parent.width*0.18; horizontalAlignment: Text.AlignHCenter }
                // 净流入率
                Text { text: { var r=modelData.netInRate||0; return (r>0?"+":"")+r.toFixed(1)+"%" }
                       color: (modelData.netInRate||0)>=0?upC:dnC; font.pixelSize: fs
                       Layout.preferredWidth: parent.width*0.11; horizontalAlignment: Text.AlignHCenter }
                // 领涨股
                Text { text: modelData.lead||"--"; color: "#d0d0e0"; font.pixelSize: fs; elide: Text.ElideRight
                       Layout.preferredWidth: parent.width*0.18; horizontalAlignment: Text.AlignHCenter }
                // 信号
                Text { text: signalTag(modelData.signal); color: signalColor(modelData.signal)
                       font.pixelSize: fs; font.weight: Font.DemiBold
                       Layout.preferredWidth: parent.width*0.18; horizontalAlignment: Text.AlignHCenter }
                // 领涨股涨幅
                Text { text: { var lc=modelData.leadChg||0; return lc!==0?((lc>0?"+":"")+lc.toFixed(1)):"--" }
                       color: (modelData.leadChg||0)>=0?upC:dnC; font.pixelSize: fs
                       Layout.preferredWidth: parent.width*0.08; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }

    onDensityModeChanged: { _densityCache = densityMode }
    Connections {
        target: Bridge.MarketDataBridge
        enabled: Bridge.MarketDataBridge !== null
        function onSectorHeatDataChanged() {}
    }
}

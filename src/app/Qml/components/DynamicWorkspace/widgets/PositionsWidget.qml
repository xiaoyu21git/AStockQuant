import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/TradingConstants.js" as Const

Rectangle {
    id: root
    property var widgetConfig: ({})
    color: Const.tradingPanelBg
    radius: 10; border.color: Const.tradingPanelBorder; border.width: 1
    clip: true
    property real scaleFactor: Math.min(1.0, Math.max(0.4, height / 250))

    readonly property var bridge: Bridge.PositionAccountBridge
    property var positions: bridge && bridge.positions ? bridge.positions : []

    Connections { target: bridge; function onPositionsChanged() { positions = bridge.positions || [] } }

    function s(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    function fmtVol(v) { var n=Number(v)||0; return n>=1e8?(n/1e8).toFixed(1)+"亿":n>=1e4?(n/1e4).toFixed(0)+"万":String(n) }
    function sideLabel(sd) { return String(sd||"")==="SHORT"?"融券":String(sd||"")==="LONG"?"融资":"现" }
    function sideColor(sd) { return String(sd||"")==="SHORT"?"#10B981":"#EF4444" }
    function pnlColor(v) { return Number(v||0)>=0?"#EF4444":"#10B981" }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: s(6); spacing: s(2)
        Text { text: "持仓概览"; color: "#F1F5F9"; font.pixelSize: s(12); font.weight: Font.Bold }

        // 表头
        RowLayout { Layout.fillWidth: true; spacing: s(2)
            Text { text: "代码"; color: "#64748B"; font.pixelSize: s(8); Layout.preferredWidth: s(46) }
            Text { text: "数量"; color: "#64748B"; font.pixelSize: s(8); Layout.preferredWidth: s(36) }
            Text { text: "现价"; color: "#64748B"; font.pixelSize: s(8); Layout.preferredWidth: s(38) }
            Text { text: "市值"; color: "#64748B"; font.pixelSize: s(8); Layout.preferredWidth: s(44) }
            Text { text: "盈亏"; color: "#64748B"; font.pixelSize: s(8); Layout.fillWidth: true }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#334155" }

        ListView {
            id: lv; Layout.fillWidth: true; Layout.fillHeight: true; model: positions; clip: true
            delegate: RowLayout {
                width: lv.width; spacing: s(2)
                Text { text: String(modelData.symbol||"").replace(/(\d{6}).*/,"$1")||"--"; color: "#F1F5F9"; font.pixelSize: s(9); Layout.preferredWidth: s(46); elide: Text.ElideRight }
                Text { text: String(Number(modelData.quantity||0)); color: "#F1F5F9"; font.pixelSize: s(9); Layout.preferredWidth: s(36) }
                Text { text: Number(modelData.lastPrice||0).toFixed(2); color: "#F1F5F9"; font.pixelSize: s(9); Layout.preferredWidth: s(38) }
                Text { text: fmtVol(modelData.marketValue||0); color: "#94A3B8"; font.pixelSize: s(9); Layout.preferredWidth: s(44) }
                Text { text: (Number(modelData.unrealizedPnl||0)>=0?"+":"")+Number(modelData.unrealizedPnl||0).toFixed(2);
                       color: pnlColor(modelData.unrealizedPnl); font.pixelSize: s(9); Layout.fillWidth: true }
            }
        }
    }
}

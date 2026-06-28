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
    property real scaleFactor: Math.min(1.0, Math.max(0.4, height / 300))

    readonly property var tradeBridge: Bridge.TradeExecutionBridge
    property var orders: tradeBridge && tradeBridge.recentOrders ? tradeBridge.recentOrders : []

    Connections {
        target: tradeBridge
        function onRecentOrdersChanged() { orders = tradeBridge.recentOrders || [] }
    }

    function s(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    function statusText(st) {
        var t = String(st||"")
        return t === "SUBMITTED" ? "已报" : t === "PARTIAL_FILLED" ? "部成"
             : t === "FILLED" ? "已成" : t === "CANCELLED" ? "已撤"
             : t === "REJECTED" ? "拒单" : t === "PENDING" ? "待报" : t
    }
    function statusColor(st) {
        return String(st||"") === "FILLED" ? "#10B981" : String(st||"") === "REJECTED" ? "#EF4444" : "#F1F5F9"
    }
    function sideColor(sd) { return String(sd||"") === "SELL" ? "#10B981" : "#EF4444" }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: s(6)
        spacing: s(2)

        Text { text: "交易列表"; color: "#F1F5F9"; font.pixelSize: s(12); font.weight: Font.Bold }

        // 表头
        RowLayout { Layout.fillWidth: true; spacing: s(2)
            Text { text: "代码"; color: "#64748B"; font.pixelSize: s(9); Layout.preferredWidth: s(52) }
            Text { text: "方向"; color: "#64748B"; font.pixelSize: s(9); Layout.preferredWidth: s(24) }
            Text { text: "价格"; color: "#64748B"; font.pixelSize: s(9); Layout.preferredWidth: s(40) }
            Text { text: "数量"; color: "#64748B"; font.pixelSize: s(9); Layout.preferredWidth: s(36) }
            Text { text: "状态"; color: "#64748B"; font.pixelSize: s(9); Layout.fillWidth: true }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#334155" }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: orders
            clip: true
            delegate: RowLayout {
                width: listView.width
                spacing: s(2)
                Text { text: String(modelData.symbol||"").replace(/(\d{6}).*/, "$1") || "--"; color: "#F1F5F9"; font.pixelSize: s(9); Layout.preferredWidth: s(52); elide: Text.ElideRight }
                Text { text: String(modelData.side||"") === "SELL" ? "卖" : "买"; color: sideColor(modelData.side); font.pixelSize: s(9); Layout.preferredWidth: s(24) }
                Text { text: Number(modelData.price||0).toFixed(2); color: "#F1F5F9"; font.pixelSize: s(9); Layout.preferredWidth: s(40) }
                Text { text: String(Number(modelData.quantity||0)); color: "#94A3B8"; font.pixelSize: s(9); Layout.preferredWidth: s(36) }
                Text { text: statusText(modelData.rawStatus||modelData.status); color: statusColor(modelData.rawStatus||modelData.status); font.pixelSize: s(9); Layout.fillWidth: true }
            }
        }
    }
}

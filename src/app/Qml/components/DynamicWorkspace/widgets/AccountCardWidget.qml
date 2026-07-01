import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../page/dashboard" as Dash

// ============================================================================
// AccountCardWidget — 账户概览 (资金管理页面 StatusSummaryCard 风格)
// 数据源: PositionAccountBridge + TradeExecutionBridge
// ============================================================================

Item {
    id: root
    property var widgetConfig: ({})
    clip: true

    property var accountSnapshot: Bridge.PositionAccountBridge
        && Bridge.PositionAccountBridge.accountSnapshot
        ? Bridge.PositionAccountBridge.accountSnapshot : ({})
    property var positions: Bridge.PositionAccountBridge
        && Bridge.PositionAccountBridge.positions
        ? Bridge.PositionAccountBridge.positions : []
    property var orders: Bridge.TradeExecutionBridge
        && Bridge.TradeExecutionBridge.recentOrders
        ? Bridge.TradeExecutionBridge.recentOrders : []

    Connections {
        target: Bridge.PositionAccountBridge
        function onAccountSnapshotChanged() { accountSnapshot = Bridge.PositionAccountBridge.accountSnapshot || ({}) }
        function onPositionsChanged() { positions = Bridge.PositionAccountBridge.positions || [] }
    }
    Connections {
        target: Bridge.TradeExecutionBridge
        function onRecentOrdersChanged() { orders = Bridge.TradeExecutionBridge.recentOrders || [] }
    }

    readonly property color neutralAccent: "#3b82f6"
    readonly property color riseColor: "#ef4444"
    readonly property color fallColor: "#10b981"

    function cny(v) { var n = Number(v||0); return "¥ " + n.toLocaleString(Qt.locale(), 'f', 2) }
    function signedCny(v) { var n = Number(v||0); return (n>=0?"+":"") + "¥ " + Math.abs(n).toLocaleString(Qt.locale(), 'f', 2) }
    function pnlColor(v) { return Number(v||0) >= 0 ? riseColor : fallColor }

    readonly property var cards: [
        {
            title: "资产总额", value: cny(accountSnapshot.totalAsset || 0),
            detail: "可用 " + cny(accountSnapshot.availableCash || 0),
            iconSource: "qrc:/resources/icons/chart-line.svg",
            accentColor: neutralAccent, accentBackground: "#3b82f620", accentBorder: "#3b82f655",
            indicatorText: "资", valueColor: "#f1f5f9", detailColor: "#94a3b8"
        },
        {
            title: "持仓市值", value: cny(accountSnapshot.marketValue || 0),
            detail: "持仓占比 " + Number((accountSnapshot.totalAsset||0)>0 ? (accountSnapshot.marketValue||0)/accountSnapshot.totalAsset*100 : 0).toFixed(1) + "%",
            iconSource: "qrc:/resources/icons/100.svg",
            accentColor: "#0ea5a4", accentBackground: "#14b8a620", accentBorder: "#14b8a655",
            indicatorText: "仓", valueColor: "#f1f5f9", detailColor: "#94a3b8"
        },
        {
            title: "已实现盈亏", value: signedCny(accountSnapshot.realizedPnl || 0),
            detail: "浮动 " + signedCny(accountSnapshot.unrealizedPnl || 0),
            iconSource: "qrc:/resources/icons/shield-alt.svg",
            accentColor: pnlColor(accountSnapshot.realizedPnl || 0),
            accentBackground: Number(accountSnapshot.realizedPnl||0)>=0 ? "#ef444420" : "#10b98120",
            accentBorder: Number(accountSnapshot.realizedPnl||0)>=0 ? "#ef444455" : "#10b98155",
            indicatorText: Number(accountSnapshot.realizedPnl||0)>=0 ? "+" : "-",
            valueColor: pnlColor(accountSnapshot.realizedPnl || 0),
            detailColor: pnlColor(accountSnapshot.unrealizedPnl || 0)
        },
        {
            title: "最新委托", value: orders.length > 0 ? String(orders[0].status || "--") : "--",
            detail: orders.length > 0 ? String((orders[0].symbol||"") + " / " + (orders[0].side||"")) : "暂无委托更新",
            iconSource: "qrc:/resources/icons/robot.svg",
            accentColor: orders.length > 0 ? neutralAccent : neutralAccent,
            accentBackground: "#3b82f620", accentBorder: "#3b82f655",
            indicatorText: "委",
            valueColor: orders.length > 0 ? "#f1f5f9" : "#f1f5f9",
            detailColor: orders.length > 0 ? "#cbd5e1" : "#94a3b8"
        }
    ]

    GridLayout {
        anchors.fill: parent
        anchors.margins: 4
        columns: width > 400 ? 2 : 1
        columnSpacing: 8
        rowSpacing: 8

        Repeater {
            model: root.cards

            delegate: Dash.StatusSummaryCard {
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                cardData: modelData
                fallbackAccentColor: "#3b82f6"
            }
        }
    }
}

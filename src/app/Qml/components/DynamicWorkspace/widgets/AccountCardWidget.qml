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

    readonly property int filledCount: orders.filter(function(o){return String(o.status||"").toUpperCase()==="FILLED"}).length
    readonly property int posCount: Array.isArray(positions) ? positions.length : 0
    readonly property double exposurePct: (accountSnapshot.totalAsset||0) > 0 ? (accountSnapshot.marketValue||0) / accountSnapshot.totalAsset * 100 : 0

    readonly property var cards: [
        {
            title: "资产总额", value: cny(accountSnapshot.totalAsset || 0),
            detail: "可用 " + cny(accountSnapshot.availableCash || 0),
            iconSource: "qrc:/resources/icons/chart-line.svg",
            accentColor: neutralAccent, accentBackground: "#3b82f620", accentBorder: "#3b82f655",
            indicatorText: "资", valueColor: "#f1f5f9", detailColor: "#94a3b8"
        },
        {
            title: "可用资金", value: cny(accountSnapshot.availableCash || 0),
            detail: "持仓 " + exposurePct.toFixed(0) + "%",
            iconSource: "qrc:/resources/icons/wallet.svg",
            accentColor: "#22c55e", accentBackground: "#22c55e20", accentBorder: "#22c55e55",
            indicatorText: "钱", valueColor: "#f1f5f9", detailColor: "#94a3b8"
        },
        {
            title: "持仓市值", value: cny(accountSnapshot.marketValue || 0),
            detail: posCount + " 只品种",
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
            title: "浮动盈亏", value: signedCny(accountSnapshot.unrealizedPnl || 0),
            detail: (accountSnapshot.totalAsset||0) > 0 ? (Number(accountSnapshot.unrealizedPnl||0)/accountSnapshot.totalAsset*100).toFixed(2) + "%" : "—",
            iconSource: "qrc:/resources/icons/fire.svg",
            accentColor: pnlColor(accountSnapshot.unrealizedPnl || 0),
            accentBackground: Number(accountSnapshot.unrealizedPnl||0)>=0 ? "#ef444420" : "#10b98120",
            accentBorder: Number(accountSnapshot.unrealizedPnl||0)>=0 ? "#ef444455" : "#10b98155",
            indicatorText: "浮", valueColor: pnlColor(accountSnapshot.unrealizedPnl || 0),
            detailColor: "#94a3b8"
        },
        {
            title: "最新委托", value: orders.length > 0 ? String(orders[0].status || "--") : "--",
            detail: orders.length > 0 ? String((orders[0].symbol||"") + " " + (orders[0].side||"")) : "暂无委托",
            iconSource: "qrc:/resources/icons/robot.svg",
            accentColor: neutralAccent, accentBackground: "#3b82f620", accentBorder: "#3b82f655",
            indicatorText: "委", valueColor: orders.length > 0 ? "#f1f5f9" : "#f1f5f9",
            detailColor: orders.length > 0 ? "#cbd5e1" : "#94a3b8"
        },
        {
            title: "今日成交", value: String(filledCount) + " 笔",
            detail: orders.length > 0 ? "共 " + orders.length + " 笔委托" : "暂无成交",
            iconSource: "qrc:/resources/icons/check.svg",
            accentColor: "#10b981", accentBackground: "#10b98120", accentBorder: "#10b98155",
            indicatorText: "成", valueColor: "#10b981",
            detailColor: "#94a3b8"
        },
        {
            title: "持仓品种", value: String(posCount) + " 只",
            detail: "市值 " + cny(accountSnapshot.marketValue || 0),
            iconSource: "qrc:/resources/icons/table.svg",
            accentColor: "#f59e0b", accentBackground: "#f59e0b20", accentBorder: "#f59e0b55",
            indicatorText: "持", valueColor: "#f1f5f9",
            detailColor: "#94a3b8"
        }
    ]

    readonly property int cardCols: Math.max(1, Math.floor(width / 200))

    GridLayout {
        anchors.fill: parent
        anchors.margins: 4
        columns: cardCols
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

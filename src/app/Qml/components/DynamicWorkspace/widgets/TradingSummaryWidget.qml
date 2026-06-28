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
    property real scaleFactor: Math.min(1.0, Math.max(0.4, height / 200))

    readonly property var posBridge: Bridge.PositionAccountBridge
    readonly property var tradeBridge: Bridge.TradeExecutionBridge
    readonly property var riskBridge: Bridge.RiskControlBridge
    readonly property var acct: posBridge && posBridge.accountSnapshot ? posBridge.accountSnapshot : ({})
    property var orders: tradeBridge && tradeBridge.recentOrders ? tradeBridge.recentOrders : []

    Connections { target: tradeBridge; function onRecentOrdersChanged() { orders = tradeBridge.recentOrders || [] } }

    function s(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    function fmt(v) { var n=Number(v)||0; return n>=1e8?(n/1e8).toFixed(1)+"亿":n>=1e4?(n/1e4).toFixed(0)+"万":n.toFixed(0) }

    // 订单统计
    readonly property int totalOrders: orders.length
    readonly property int filledOrders: orders.filter(function(o){ return String(o.rawStatus||o.status||"") === "FILLED" }).length
    readonly property int rejectedOrders: orders.filter(function(o){ return String(o.rawStatus||o.status||"") === "REJECTED" }).length
    readonly property int pendingOrders: totalOrders - filledOrders - rejectedOrders

    GridLayout {
        anchors.fill: parent; anchors.margins: s(6)
        columns: 2; columnSpacing: s(8); rowSpacing: s(4)

        Text { text: "交易概览"; color: "#F1F5F9"; font.pixelSize: s(12); font.weight: Font.Bold
               Layout.fillWidth: true; Layout.columnSpan: 2 }

        MC { l: "总资产"; v: fmt(acct.totalAsset||0) }
        MC { l: "可用资金"; v: fmt(acct.availableCash||0) }
        MC { l: "持仓市值"; v: fmt(acct.marketValue||0) }
        MC { l: "浮动盈亏"; v: fmt(acct.unrealizedPnl||0); vc: (Number(acct.unrealizedPnl)||0)>=0?"#EF4444":"#10B981" }
        MC { l: "总委托"; v: String(totalOrders) }
        MC { l: "已成"; v: String(filledOrders); vc: "#10B981" }
        MC { l: "待报/部成"; v: String(pendingOrders); vc: "#F59E0B" }
        MC { l: "拒单"; v: String(rejectedOrders); vc: "#EF4444" }
        MC { l: "风险敞口"; v: (riskBridge?Number(riskBridge.currentTotalExposurePercent||0).toFixed(1):"--")+"%" }
        MC { l: "VaR使用"; v: (riskBridge?Number(riskBridge.varUsagePercent||0).toFixed(1):"--")+"%" }
    }

    component MC: ColumnLayout {
        property string l: ""; property string v: ""; property color vc: "#F1F5F9"
        Layout.fillWidth: true; spacing: 1
        Text { text: l; color: "#64748B"; font.pixelSize: s(9) }
        Text { text: v; color: vc; font.pixelSize: s(14); font.weight: Font.Bold }
    }
}

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../page/dashboard" as Dash

// ============================================================================
// AccountCardWidget — 账户概览 + 持仓/委托tab切换
// ============================================================================

Item {
    id: root
    property var widgetConfig: ({})
    clip: true

    property var snap: Bridge.PositionAccountBridge && Bridge.PositionAccountBridge.accountSnapshot
        ? Bridge.PositionAccountBridge.accountSnapshot : ({})
    property var positions: Bridge.PositionAccountBridge && Bridge.PositionAccountBridge.positions
        ? Bridge.PositionAccountBridge.positions : []
    property var orders: Bridge.TradeExecutionBridge && Bridge.TradeExecutionBridge.recentOrders
        ? Bridge.TradeExecutionBridge.recentOrders : []

    Connections {
        target: Bridge.PositionAccountBridge
        function onAccountSnapshotChanged() { snap = Bridge.PositionAccountBridge.accountSnapshot || ({}) }
        function onPositionsChanged() { positions = Bridge.PositionAccountBridge.positions || [] }
    }
    Connections {
        target: Bridge.TradeExecutionBridge
        function onRecentOrdersChanged() { orders = Bridge.TradeExecutionBridge.recentOrders || [] }
    }

    readonly property color riseColor: "#ef4444"
    readonly property color fallColor: "#10b981"
    readonly property color neutralAccent: "#3b82f6"
    function cny(v) { var n = Number(v||0); return n.toLocaleString(Qt.locale(), 'f', 2) }
    function signedCny(v) { var n = Number(v||0); return (n>=0?"+":"-") + Math.abs(n).toLocaleString(Qt.locale(), 'f', 2) }
    function pnlColor(v) { return Number(v||0) >= 0 ? riseColor : fallColor }

    readonly property double totalAsset: Number(snap.totalAsset || 0)
    readonly property double marketValue: Number(snap.marketValue || 0)
    readonly property double totalPnl: Number(snap.realizedPnl||0) + Number(snap.unrealizedPnl||0)
    readonly property int posCount: Array.isArray(positions) ? positions.length : 0

    property int currentTab: 0

    function filterOrders(pred) { var r=[]; for(var i=0;i<orders.length;i++){if(pred(orders[i]))r.push(orders[i])} return r }
    readonly property var buyOrders: filterOrders(function(o){ return String(o.side||"").toUpperCase() === "BUY" })
    readonly property var sellOrders: filterOrders(function(o){ return String(o.side||"").toUpperCase() === "SELL" })
    readonly property var cancelOrders: filterOrders(function(o){ var s=String(o.status||"").toUpperCase(); return s==="CANCELLED"||s==="REJECTED" })

    readonly property var summaryData: [
        { t:"总资产", v:cny(totalAsset), d:"可用 "+cny(snap.availableCash||0), i:"qrc:/resources/icons/chart-line.svg", ac:neutralAccent, abg:"#3b82f620", ab:"#3b82f655", ind:"资", vc:"#f1f5f9", dc:"#94a3b8" },
        { t:"总盈亏", v:signedCny(totalPnl), d:"浮动 "+signedCny(snap.unrealizedPnl||0), i:"qrc:/resources/icons/fire.svg", ac:pnlColor(totalPnl), abg:pnlColor(totalPnl)==riseColor?"#ef444420":"#10b98120", ab:pnlColor(totalPnl)==riseColor?"#ef444455":"#10b98155", ind:totalPnl>=0?"+":"-", vc:pnlColor(totalPnl), dc:pnlColor(totalPnl) },
        { t:"总持仓", v:String(posCount)+" 只", d:"市值 "+cny(marketValue), i:"qrc:/resources/icons/table.svg", ac:"#f59e0b", abg:"#f59e0b20", ab:"#f59e0b55", ind:"持", vc:"#f1f5f9", dc:"#94a3b8" },
        { t:"总市值", v:cny(marketValue), d:"占比 "+(totalAsset>0?(marketValue/totalAsset*100).toFixed(0):"0")+"%", i:"qrc:/resources/icons/100.svg", ac:"#0ea5a4", abg:"#14b8a620", ab:"#14b8a655", ind:"市", vc:"#f1f5f9", dc:"#94a3b8" }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        // ── 总览卡片 ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: root.summaryData
                delegate: Dash.StatusSummaryCard {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 82
                    cardData: ({ title:modelData.t, value:modelData.v, detail:modelData.d, iconSource:modelData.i, accentColor:modelData.ac, accentBackground:modelData.abg, accentBorder:modelData.ab, indicatorText:modelData.ind, valueColor:modelData.vc, detailColor:modelData.dc })
                    fallbackAccentColor: "#3b82f6"
                }
            }
        }

        // ── 持仓比例标签 ──
        Flow {
            Layout.fillWidth: true
            spacing: 4
            visible: posCount > 0 && marketValue > 0

            Repeater {
                model: { var a=[]; for(var i=0;i<Math.min(positions.length,12);i++)a.push(positions[i]); return a }
                delegate: Rectangle {
                    required property var modelData
                    readonly property double pct: marketValue > 0 ? (Number(modelData.marketValue||0) / marketValue * 100) : 0
                    width: tagText.width + 12
                    height: 20
                    radius: 4
                    color: "#1e293b"
                    border.width: 1; border.color: pct > 10 ? "#3b82f6" : "#334155"
                    Text {
                        id: tagText; anchors.centerIn: parent
                        text: String(modelData.symbol||"").replace(".SZ","").replace(".SH","") + " " + pct.toFixed(1) + "%"
                        color: pct > 10 ? "#93c5fd" : "#94a3b8"; font.pixelSize: 10
                    }
                }
            }
        }

        // ── Tab 栏 ──
        RowLayout {
            Layout.fillWidth: true; spacing: 2
            Repeater {
                model: ["持仓","买入","卖出","撤单"]
                delegate: Rectangle {
                    required property var modelData; required property int index
                    Layout.fillWidth: true; height: 28; radius: 6
                    color: root.currentTab === index ? "#1e40af" : "#1e293b"
                    border.width: root.currentTab === index ? 1 : 0; border.color: "#3b82f6"
                    Text { anchors.centerIn: parent; text: modelData; color: root.currentTab === index ? "#f1f5f9" : "#94a3b8"; font.pixelSize: 11; font.weight: root.currentTab === index ? Font.DemiBold : Font.Normal }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.currentTab = index }
                }
            }
        }

        // ── 列表内容 (与卡片等高) ──
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 82
            radius: 8; color: "#0f172a"; border.width: 1; border.color: "#1e293b"

            // 持仓
            ListView {
                anchors.fill: parent; anchors.margins: 4
                visible: root.currentTab === 0; clip: true; spacing: 2
                model: { var a=[]; for(var i=0;i<Math.min(root.positions.length,20);i++)a.push(root.positions[i]); return a }
                delegate: Rectangle {
                    required property var modelData; width: parent?parent.width:100; height:24; color:"transparent"
                    readonly property double pnl: Number(modelData.unrealizedPnl||0); readonly property double mv: Number(modelData.marketValue||0)
                    RowLayout { anchors.fill:parent; spacing:4
                        Text { text:String(modelData.symbol||"").replace(".SZ","").replace(".SH",""); color:"#f1f5f9"; font.pixelSize:11; Layout.preferredWidth:50 }
                        Text { text:String(modelData.quantity||0); color:"#94a3b8"; font.pixelSize:10; Layout.preferredWidth:40 }
                        Text { text:cny(mv); color:"#cbd5e1"; font.pixelSize:10; Layout.fillWidth:true; elide:Text.ElideRight }
                        Text { text:signedCny(pnl); color:pnlColor(pnl); font.pixelSize:11; Layout.preferredWidth:70; horizontalAlignment:Text.AlignRight }
                    }
                }
            }

            // 买入
            ListView {
                anchors.fill: parent; anchors.margins: 4
                visible: root.currentTab === 1; clip: true; spacing: 2
                model: { var a=[]; for(var i=0;i<Math.min(root.buyOrders.length,20);i++)a.push(root.buyOrders[i]); return a }
                delegate: Rectangle {
                    required property var modelData; width:parent?parent.width:100; height:24; color:"transparent"
                    RowLayout { anchors.fill:parent; spacing:4
                        Text { text:String(modelData.symbol||""); color:"#ef4444"; font.pixelSize:11; Layout.preferredWidth:50 }
                        Text { text:cny(modelData.price||0); color:"#f1f5f9"; font.pixelSize:11; Layout.preferredWidth:60 }
                        Text { text:String(modelData.quantity||0); color:"#94a3b8"; font.pixelSize:10; Layout.fillWidth:true; elide:Text.ElideRight }
                        Text { text:String(modelData.status||""); color:"#f59e0b"; font.pixelSize:10; Layout.preferredWidth:50 }
                    }
                }
            }

            // 卖出
            ListView {
                anchors.fill: parent; anchors.margins: 4
                visible: root.currentTab === 2; clip: true; spacing: 2
                model: { var a=[]; for(var i=0;i<Math.min(root.sellOrders.length,20);i++)a.push(root.sellOrders[i]); return a }
                delegate: Rectangle {
                    required property var modelData; width:parent?parent.width:100; height:24; color:"transparent"
                    RowLayout { anchors.fill:parent; spacing:4
                        Text { text:String(modelData.symbol||""); color:"#10b981"; font.pixelSize:11; Layout.preferredWidth:50 }
                        Text { text:cny(modelData.price||0); color:"#f1f5f9"; font.pixelSize:11; Layout.preferredWidth:60 }
                        Text { text:String(modelData.quantity||0); color:"#94a3b8"; font.pixelSize:10; Layout.fillWidth:true; elide:Text.ElideRight }
                        Text { text:String(modelData.status||""); color:"#f59e0b"; font.pixelSize:10; Layout.preferredWidth:50 }
                    }
                }
            }

            // 撤单
            ListView {
                anchors.fill: parent; anchors.margins: 4
                visible: root.currentTab === 3; clip: true; spacing: 2
                model: { var a=[]; for(var i=0;i<Math.min(root.cancelOrders.length,20);i++)a.push(root.cancelOrders[i]); return a }
                delegate: Rectangle {
                    required property var modelData; width:parent?parent.width:100; height:24; color:"transparent"
                    RowLayout { anchors.fill:parent; spacing:4
                        Text { text:String(modelData.symbol||""); color:"#94a3b8"; font.pixelSize:11; Layout.preferredWidth:50 }
                        Text { text:String(modelData.side||""); color:"#64748b"; font.pixelSize:10; Layout.preferredWidth:30 }
                        Text { text:cny(modelData.price||0); color:"#64748b"; font.pixelSize:11; Layout.fillWidth:true }
                        Text { text:String(modelData.status||""); color:"#ef4444"; font.pixelSize:10; Layout.preferredWidth:50 }
                    }
                }
            }
        }
    }
}

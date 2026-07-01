import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../page/dashboard" as Dash

// ============================================================================
// AccountCardWidget — 账户概览 + 持仓/委托tab切换 (同花顺风格)
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

    // ── Tab ──
    property int currentTab: 0
    readonly property var tabs: ["持仓", "买入", "卖出", "撤单"]

    // ── 筛选数据 ──
    function filterOrders(pred) { var r=[]; for(var i=0;i<orders.length;i++){if(pred(orders[i]))r.push(orders[i])} return r }
    readonly property var buyOrders: filterOrders(function(o){ return String(o.side||"").toUpperCase() === "BUY" })
    readonly property var sellOrders: filterOrders(function(o){ return String(o.side||"").toUpperCase() === "SELL" })
    readonly property var cancelOrders: filterOrders(function(o){ var s=String(o.status||"").toUpperCase(); return s==="CANCELLED"||s==="REJECTED" })

    // ── 总览卡片 ──
    readonly property var summaryCards: [
        { title:"总资产", value:cny(totalAsset), detail:"可用 "+cny(snap.availableCash||0), icon:"qrc:/resources/icons/chart-line.svg", accent:neutralAccent, abg:"#3b82f620", ab:"#3b82f655", ind:"资", vc:"#f1f5f9", dc:"#94a3b8" },
        { title:"总盈亏", value:signedCny(totalPnl), detail:"已实现 "+signedCny(snap.realizedPnl||0)+" / 浮动 "+signedCny(snap.unrealizedPnl||0), icon:"qrc:/resources/icons/fire.svg", accent:pnlColor(totalPnl), abg:pnlColor(totalPnl)==riseColor?"#ef444420":"#10b98120", ab:pnlColor(totalPnl)==riseColor?"#ef444455":"#10b98155", ind:totalPnl>=0?"+":"-", vc:pnlColor(totalPnl), dc:pnlColor(totalPnl) },
        { title:"总持仓", value:String(posCount)+" 只", detail:"市值 "+cny(marketValue), icon:"qrc:/resources/icons/table.svg", accent:"#f59e0b", abg:"#f59e0b20", ab:"#f59e0b55", ind:"持", vc:"#f1f5f9", dc:"#94a3b8" },
        { title:"总市值", value:cny(marketValue), detail:"占比 "+(totalAsset>0?(marketValue/totalAsset*100).toFixed(0):"0")+"%", icon:"qrc:/resources/icons/100.svg", accent:"#0ea5a4", abg:"#14b8a620", ab:"#14b8a655", ind:"市", vc:"#f1f5f9", dc:"#94a3b8" }
    ]

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 6

            // ── 总览卡片行 ──
            GridLayout {
                Layout.fillWidth: true
                columns: Math.max(1, Math.floor(width / 180))
                columnSpacing: 6
                rowSpacing: 6

                Repeater {
                    model: root.summaryCards
                    delegate: Dash.StatusSummaryCard {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 90
                        cardData: ({
                            title: modelData.title,
                            value: modelData.value,
                            detail: modelData.detail,
                            iconSource: modelData.icon,
                            accentColor: modelData.accent,
                            accentBackground: modelData.abg,
                            accentBorder: modelData.ab,
                            indicatorText: modelData.ind,
                            valueColor: modelData.vc,
                            detailColor: modelData.dc
                        })
                        fallbackAccentColor: "#3b82f6"
                    }
                }
            }

            // ── 持仓比例标签 (同花顺风格) ──
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
                        border.width: 1
                        border.color: pct > 10 ? "#3b82f6" : "#334155"

                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: String(modelData.symbol||"").replace(".SZ","").replace(".SH","") + " " + pct.toFixed(1) + "%"
                            color: pct > 10 ? "#93c5fd" : "#94a3b8"
                            font.pixelSize: 10
                        }
                    }
                }
            }

            // ── Tab 栏 ──
            RowLayout {
                Layout.fillWidth: true
                spacing: 2

                Repeater {
                    model: root.tabs
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        height: 28
                        radius: 6
                        color: root.currentTab === index ? "#1e40af" : "#1e293b"
                        border.width: root.currentTab === index ? 1 : 0
                        border.color: "#3b82f6"

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: root.currentTab === index ? "#f1f5f9" : "#94a3b8"
                            font.pixelSize: 11
                            font.weight: root.currentTab === index ? Font.DemiBold : Font.Normal
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentTab = index
                        }
                    }
                }
            }

            // ── 列表内容区 ──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                radius: 8
                color: "#0f172a"
                border.width: 1
                border.color: "#1e293b"

                // 持仓列表
                ListView {
                    anchors.fill: parent
                    anchors.margins: 4
                    visible: root.currentTab === 0
                    model: { var a=[]; for(var i=0;i<Math.min(root.positions.length,20);i++)a.push(root.positions[i]); return a }
                    clip: true
                    spacing: 2

                    delegate: Rectangle {
                        required property var modelData
                        width: parent ? parent.width : 100
                        height: 24
                        color: "transparent"
                        readonly property double pnl: Number(modelData.unrealizedPnl||0)
                        readonly property double mv: Number(modelData.marketValue||0)

                        RowLayout {
                            anchors.fill: parent
                            spacing: 4
                            Text { text: String(modelData.symbol||"").replace(".SZ","").replace(".SH",""); color:"#f1f5f9"; font.pixelSize:11; Layout.preferredWidth:50 }
                            Text { text: String(modelData.quantity||0); color:"#94a3b8"; font.pixelSize:10; Layout.preferredWidth:40 }
                            Text { text: cny(mv); color:"#cbd5e1"; font.pixelSize:10; Layout.fillWidth:true; elide:Text.ElideRight }
                            Text { text: signedCny(pnl); color: pnlColor(pnl); font.pixelSize:11; Layout.preferredWidth:70; horizontalAlignment:Text.AlignRight }
                        }
                    }
                }

                // 买入列表
                ListView {
                    anchors.fill: parent
                    anchors.margins: 4
                    visible: root.currentTab === 1
                    model: { var a=[]; for(var i=0;i<Math.min(root.buyOrders.length,20);i++)a.push(root.buyOrders[i]); return a }
                    clip: true
                    spacing: 2

                    delegate: Rectangle {
                        required property var modelData
                        width: parent ? parent.width : 100
                        height: 24
                        color: "transparent"
                        RowLayout {
                            anchors.fill: parent
                            spacing: 4
                            Text { text: String(modelData.symbol||""); color:"#ef4444"; font.pixelSize:11; Layout.preferredWidth:50 }
                            Text { text: cny(modelData.price||0); color:"#f1f5f9"; font.pixelSize:11; Layout.preferredWidth:60 }
                            Text { text: String(modelData.quantity||0); color:"#94a3b8"; font.pixelSize:10; Layout.fillWidth:true; elide:Text.ElideRight }
                            Text { text: String(modelData.status||""); color:"#f59e0b"; font.pixelSize:10; Layout.preferredWidth:50 }
                        }
                    }
                }

                // 卖出列表
                ListView {
                    anchors.fill: parent
                    anchors.margins: 4
                    visible: root.currentTab === 2
                    model: { var a=[]; for(var i=0;i<Math.min(root.sellOrders.length,20);i++)a.push(root.sellOrders[i]); return a }
                    clip: true
                    spacing: 2

                    delegate: Rectangle {
                        required property var modelData
                        width: parent ? parent.width : 100
                        height: 24
                        color: "transparent"
                        RowLayout {
                            anchors.fill: parent
                            spacing: 4
                            Text { text: String(modelData.symbol||""); color:"#10b981"; font.pixelSize:11; Layout.preferredWidth:50 }
                            Text { text: cny(modelData.price||0); color:"#f1f5f9"; font.pixelSize:11; Layout.preferredWidth:60 }
                            Text { text: String(modelData.quantity||0); color:"#94a3b8"; font.pixelSize:10; Layout.fillWidth:true; elide:Text.ElideRight }
                            Text { text: String(modelData.status||""); color:"#f59e0b"; font.pixelSize:10; Layout.preferredWidth:50 }
                        }
                    }
                }

                // 撤单列表
                ListView {
                    anchors.fill: parent
                    anchors.margins: 4
                    visible: root.currentTab === 3
                    model: { var a=[]; for(var i=0;i<Math.min(root.cancelOrders.length,20);i++)a.push(root.cancelOrders[i]); return a }
                    clip: true
                    spacing: 2

                    delegate: Rectangle {
                        required property var modelData
                        width: parent ? parent.width : 100
                        height: 24
                        color: "transparent"
                        RowLayout {
                            anchors.fill: parent
                            spacing: 4
                            Text { text: String(modelData.symbol||""); color:"#94a3b8"; font.pixelSize:11; Layout.preferredWidth:50 }
                            Text { text: String(modelData.side||""); color:"#64748b"; font.pixelSize:10; Layout.preferredWidth:30 }
                            Text { text: cny(modelData.price||0); color:"#64748b"; font.pixelSize:11; Layout.fillWidth:true }
                            Text { text: String(modelData.status||""); color:"#ef4444"; font.pixelSize:10; Layout.preferredWidth:50 }
                        }
                    }
                }
            }
        }
    }
}

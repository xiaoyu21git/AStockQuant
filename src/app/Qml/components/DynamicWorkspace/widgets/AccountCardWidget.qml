import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: root
    property var widgetConfig: ({})
    clip: true

    // ── C++ 推送数据 ──
    // 直接绑定 — C++ NOTIFY 信号自动触发 QML 重求值，不能用在 Connections 里重新赋值(会断绑定)
    readonly property var snap: (Bridge.PositionAccountBridge && Bridge.PositionAccountBridge.accountSnapshot)
        ? Bridge.PositionAccountBridge.accountSnapshot : ({})
    readonly property var positions: (Bridge.PositionAccountBridge && Bridge.PositionAccountBridge.positions)
        ? Bridge.PositionAccountBridge.positions : []
    readonly property var orders: (Bridge.TradeExecutionBridge && Bridge.TradeExecutionBridge.recentOrders)
        ? Bridge.TradeExecutionBridge.recentOrders : []
    readonly property var mktSnap: (Bridge.MarketDataBridge && Bridge.MarketDataBridge.marketSnapshots)
        ? Bridge.MarketDataBridge.marketSnapshots : ({})

    // 合并行情数据
    function orderChg(order) {
        var sym=String(order.symbol||""); var snap=mktSnap[sym]||({})
        var cur=Number(snap.price||0); var cost=Number(order.price||0)
        if(cost<=0||cur<=0) return "—"
        var side=String(order.side||"").toUpperCase()
        return side==="BUY"?((cur-cost)/cost*100).toFixed(2)+"%" : ((cost-cur)/cost*100).toFixed(2)+"%"
    }
    function orderPnl(order) {
        var sym=String(order.symbol||""); var snap=mktSnap[sym]||({})
        var cur=Number(snap.price||0); var cost=Number(order.price||0); var q=Number(order.quantity||0)
        if(cost<=0||cur<=0||q<=0) return "—"
        var side=String(order.side||"").toUpperCase()
        return side==="BUY"? scny((cur-cost)*q) : scny((cost-cur)*q)
    }
    function chgColor(order) {
        var sym=String(order.symbol||""); var snap=mktSnap[sym]||({})
        var cur=Number(snap.price||0); var cost=Number(order.price||0)
        if(cost<=0||cur<=0) return "#94a3b8"
        return cur>=cost ? rise : fall
    }

    readonly property color rise: "#ef4444"; readonly property color fall: "#10b981"
    function cny(v) { var n=Number(v||0); return n.toLocaleString(Qt.locale(),'f',2) }
    function scny(v) { var n=Number(v||0); return (n>=0?"+":"")+Math.abs(n).toLocaleString(Qt.locale(),'f',2) }
    function pc(v) { return Number(v||0)>=0?rise:fall }

    readonly property double totalAsset: Number(snap.totalAsset||0)
    readonly property double marketValue: Number(snap.marketValue||0)
    readonly property double totalPnl: Number(snap.realizedPnl||0)+Number(snap.unrealizedPnl||0)

    function flt(pred) { var r=[]; for(var i=0;i<orders.length;i++){if(pred(orders[i]))r.push(orders[i])} return r }
    readonly property var buyOrders: flt(function(o){return String(o.side||"").toUpperCase()==="BUY"})
    readonly property var sellOrders: flt(function(o){return String(o.side||"").toUpperCase()==="SELL"})
    readonly property var cancelOrders: flt(function(o){var s=String(o.status||"").toUpperCase();return s==="CANCELLED"||s==="REJECTED"})

    property int currentTab: 0

    // ── 通用卡片组件 ──
    Component {
        id: statCard
        Rectangle {
            property string label: ""; property string value: ""; property string detail: ""
            property color accent: "#3b82f6"
            Layout.fillWidth: true; Layout.preferredHeight: 72; radius: 10
            color: "#121828"; border.width: 1; border.color: "#2d3748"
            Rectangle { width:parent.width; height:3; color:accent; radius:1.5 }
            ColumnLayout {
                anchors.fill:parent; anchors.margins:12; spacing:2
                Text { text:label; color:"#94a3b8"; font.pixelSize:11; Layout.alignment:Qt.AlignHCenter }
                Text { text:value; color:"#f1f5f9"; font.pixelSize:18; font.weight:Font.DemiBold; Layout.alignment:Qt.AlignHCenter }
                Text { text:detail; color:"#94a3b8"; font.pixelSize:10; Layout.alignment:Qt.AlignHCenter; elide:Text.ElideRight }
            }
        }
    }

    // ── 通用表头 ──
    Component {
        id: listHeader
        Rectangle {
            property var columns: []
            property var widths: []
            width:parent?parent.width:100; height:22; color:"#1e293b"; radius:4
            Row {
                anchors.fill:parent; anchors.margins:4; spacing:2
                Repeater {
                    model: columns
                    delegate: Text {
                        required property var modelData; required property int index
                        readonly property var w: widths.length>index ? widths[index] : 1
                        text:modelData; color:"#64748b"; font.pixelSize:10
                        width:parent.width*w/(widths.reduce(function(s,x){return s+x},0)||1)
                        horizontalAlignment:Text.AlignRight; elide:Text.ElideRight
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 4; spacing: 4

        // ── 4 总览卡片 (居中) ──
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Loader {
                Layout.fillWidth: true; Layout.preferredHeight: 72
                sourceComponent: statCard
                onLoaded: { item.label="总资产"; item.value=cny(totalAsset); item.detail="可用 "+cny(snap.availableCash||0); item.accent="#3b82f6" }
            }
            Loader {
                Layout.fillWidth: true; Layout.preferredHeight: 72
                sourceComponent: statCard
                onLoaded: { item.label="总盈亏"; item.value=scny(totalPnl); item.detail="浮动 "+scny(snap.unrealizedPnl||0); item.accent=pc(totalPnl) }
            }
            Loader {
                Layout.fillWidth: true; Layout.preferredHeight: 72
                sourceComponent: statCard
                onLoaded: { item.label="总持仓"; item.value=String(Array.isArray(positions)?positions.length:0)+" 只"; item.detail="市值 "+cny(marketValue); item.accent="#f59e0b" }
            }
            Loader {
                Layout.fillWidth: true; Layout.preferredHeight: 72
                sourceComponent: statCard
                onLoaded: { item.label="总市值"; item.value=cny(marketValue); item.detail="占比 "+(totalAsset>0?(marketValue/totalAsset*100).toFixed(0):"0")+"%"; item.accent="#0ea5a4" }
            }
        }

        // ── Tab 栏 (立体效果) ──
        RowLayout { Layout.fillWidth:true; spacing:1
            Repeater {
                model: ["持仓","买入","卖出","撤单"]
                delegate: Rectangle {
                    required property var modelData; required property int index; property bool sel:root.currentTab===index
                    Layout.fillWidth:true; height:28; radius:6
                    color:sel?"#1d4ed8":"#1e293b"
                    border.width:1; border.color:sel?"#3b82f6":"#334155"
                    Rectangle {
                        anchors.bottom:parent.bottom; width:parent.width; height:sel?3:0
                        gradient: Gradient { GradientStop{position:0;color:"#2563eb"} GradientStop{position:1;color:"#1d4ed8"} }
                        visible:sel; radius:2
                    }
                    Text { anchors.centerIn:parent; text:modelData; color:sel?"#f1f5f9":"#94a3b8"; font.pixelSize:11; font.weight:sel?Font.DemiBold:Font.Normal }
                    MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:root.currentTab=index }
                }
            }
        }

        // ── 列表区 ──
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 100
            radius: 8; color: "#0f172a"; border.width: 1; border.color: "#1e293b"

            // === 持仓列表 ===
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 4; spacing: 2
                visible: root.currentTab === 0

                Loader {
                    Layout.fillWidth: true; Layout.preferredHeight: 20
                    sourceComponent: listHeader
                    onLoaded: { item.columns=["市值","盈亏","持仓/可用","成本/现价"]; item.widths=[2,2,2,3] }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 2
                    model: { var a=[]; for(var i=0;i<Math.min(positions.length,20);i++)a.push(positions[i]); return a }
                    delegate: Rectangle {
                        required property var modelData; width:parent?parent.width:100; height:22; color:"transparent"
                        readonly property double pnl:Number(modelData.unrealizedPnl||0)
                        readonly property double mv:Number(modelData.marketValue||0)
                        readonly property double qty:Number(modelData.quantity||0)
                        readonly property double avail:Number(modelData.availableQuantity||qty)
                        readonly property double cost:Number(modelData.costBasis||modelData.avgPrice||0)
                        Row { anchors.fill:parent; spacing:2
                            Text { text:cny(mv); color:"#cbd5e1"; font.pixelSize:10; width:parent.width*2/9; horizontalAlignment:Text.AlignRight; elide:Text.ElideRight }
                            Text { text:scny(pnl); color:pc(pnl); font.pixelSize:10; width:parent.width*2/9; horizontalAlignment:Text.AlignRight; elide:Text.ElideRight }
                            Text { text:String(qty)+"/"+String(avail); color:"#94a3b8"; font.pixelSize:10; width:parent.width*2/9; horizontalAlignment:Text.AlignRight; elide:Text.ElideRight }
                            Text { text:cny(cost)+"/"+cny(Number(modelData.lastPrice||0)); color:"#94a3b8"; font.pixelSize:10; width:parent.width*3/9; horizontalAlignment:Text.AlignRight; elide:Text.ElideRight }
                        }
                    }
                }
            }

            // === 买入列表 ===
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 4; spacing: 2
                visible: root.currentTab === 1

                Loader {
                    Layout.fillWidth: true; Layout.preferredHeight: 20
                    sourceComponent: listHeader
                    onLoaded: { item.columns=["标的","涨幅","数量","盈亏","成本"]; item.widths=[2,1,1,1,1] }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 2
                    model: { var a=[]; for(var i=0;i<Math.min(buyOrders.length,20);i++)a.push(buyOrders[i]); return a }
                    delegate: Rectangle {
                        required property var modelData; width:parent?parent.width:100; height:22; color:"transparent"
                        readonly property double p:Number(modelData.price||0); readonly property double q:Number(modelData.quantity||0)
                        Row { anchors.fill:parent; spacing:2
                            Text { text:String(modelData.symbol||"").replace(".SZ","").replace(".SH",""); color:"#ef4444"; font.pixelSize:10; width:parent.width*2/6; elide:Text.ElideRight }
                            Text { text:"—"; color:"#94a3b8"; font.pixelSize:10; width:parent.width/6; horizontalAlignment:Text.AlignRight }
                            Text { text:String(q); color:"#cbd5e1"; font.pixelSize:10; width:parent.width/6; horizontalAlignment:Text.AlignRight }
                            Text { text:"—"; color:"#94a3b8"; font.pixelSize:10; width:parent.width/6; horizontalAlignment:Text.AlignRight }
                            Text { text:cny(p); color:"#94a3b8"; font.pixelSize:10; width:parent.width/6; horizontalAlignment:Text.AlignRight }
                        }
                    }
                }
            }

            // === 卖出列表 ===
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 4; spacing: 2
                visible: root.currentTab === 2

                Loader {
                    Layout.fillWidth: true; Layout.preferredHeight: 20
                    sourceComponent: listHeader
                    onLoaded: { item.columns=["标的","涨幅","数量","盈亏","成本"]; item.widths=[2,1,1,1,1] }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 2
                    model: { var a=[]; for(var i=0;i<Math.min(sellOrders.length,20);i++)a.push(sellOrders[i]); return a }
                    delegate: Rectangle {
                        required property var modelData; width:parent?parent.width:100; height:22; color:"transparent"
                        readonly property double p:Number(modelData.price||0); readonly property double q:Number(modelData.quantity||0)
                        Row { anchors.fill:parent; spacing:2
                            Text { text:String(modelData.symbol||"").replace(".SZ","").replace(".SH",""); color:"#10b981"; font.pixelSize:10; width:parent.width*2/6 }
                            Text { text:"—"; color:"#94a3b8"; font.pixelSize:10; width:parent.width/6; horizontalAlignment:Text.AlignRight }
                            Text { text:String(q); color:"#cbd5e1"; font.pixelSize:10; width:parent.width/6; horizontalAlignment:Text.AlignRight }
                            Text { text:"—"; color:"#94a3b8"; font.pixelSize:10; width:parent.width/6; horizontalAlignment:Text.AlignRight }
                            Text { text:cny(p); color:"#94a3b8"; font.pixelSize:10; width:parent.width/6; horizontalAlignment:Text.AlignRight }
                        }
                    }
                }
            }

            // === 撤单列表 ===
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 4; spacing: 2
                visible: root.currentTab === 3

                Loader {
                    Layout.fillWidth: true; Layout.preferredHeight: 20
                    sourceComponent: listHeader
                    onLoaded: { item.columns=["委托时间","委托价/均价","委托量/成交","状态"]; item.widths=[3,2,2,1] }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 2
                    model: { var a=[]; for(var i=0;i<Math.min(cancelOrders.length,20);i++)a.push(cancelOrders[i]); return a }
                    delegate: Rectangle {
                        required property var modelData; width:parent?parent.width:100; height:22; color:"transparent"
                        readonly property double p:Number(modelData.price||0); readonly property double q:Number(modelData.quantity||0)
                        Row { anchors.fill:parent; spacing:2
                            Text { text:String(modelData.updatedAt||modelData.createdAt||"—"); color:"#64748b"; font.pixelSize:10; width:parent.width*3/8; elide:Text.ElideRight }
                            Text { text:cny(p)+"/—"; color:"#64748b"; font.pixelSize:10; width:parent.width*2/8; horizontalAlignment:Text.AlignRight }
                            Text { text:String(q)+"/—"; color:"#64748b"; font.pixelSize:10; width:parent.width*2/8; horizontalAlignment:Text.AlignRight }
                            Text { text:String(modelData.status||"—"); color:"#ef4444"; font.pixelSize:10; width:parent.width/8; horizontalAlignment:Text.AlignRight }
                        }
                    }
                }
            }
        }
    }
}

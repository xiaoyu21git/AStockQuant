import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: root
    property var widgetConfig: ({})
    property real scaleFactor: Math.min(1.0, Math.max(0.4, height / 120))
    clip: true

    readonly property bool compact: root.height < 100
    property var acct: Bridge.PositionAccountBridge.accountSnapshot || ({})

    Connections {
        target: Bridge.PositionAccountBridge
        function onAccountSnapshotChanged() {
            root.acct = Bridge.PositionAccountBridge.accountSnapshot || ({})
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: root.compact ? 3 : 10
        columns: 2
        columnSpacing: root.compact ? 4 : 14
        rowSpacing: root.compact ? 1 : 8

        MT { l: "总资产";   v: fmt(acct.totalAsset) }
        MT { l: "持仓市值"; v: fmt(acct.marketValue) }
        MT { l: "可用资金"; v: fmt(acct.availableCash) }
        MT { l: "浮动盈亏"; v: fmt(acct.unrealizedPnl)
             vc: (acct.unrealizedPnl||0)>=0?"#10B981":"#EF4444" }
    }

    function fmt(v) {
        if (v === undefined || v === null) return "--"
        var n = Number(v)
        if (Math.abs(n)>=1e8) return (n/1e8).toFixed(2)+"亿"
        if (Math.abs(n)>=1e4) return (n/1e4).toFixed(2)+"万"
        return n.toFixed(2)
    }

    component MT: RowLayout {
        property string l: ""; property string v: ""; property color vc: "#F1F5F9"
        Layout.fillWidth: true; spacing: 4
        Text { text: l; color: "#94A3B8"
               font.pixelSize: Math.max(7, Math.round((root.compact ? 10 : 12) * root.scaleFactor))
               Layout.preferredWidth: Math.max(30, Math.round((root.compact ? 44 : 58) * root.scaleFactor)) }
        Text { text: parent.v; color: parent.vc
               font.pixelSize: Math.max(8, Math.round((root.compact ? 12 : 15) * root.scaleFactor))
               font.weight: Font.DemiBold
               Layout.fillWidth: true; elide: Text.ElideRight }
    }
}

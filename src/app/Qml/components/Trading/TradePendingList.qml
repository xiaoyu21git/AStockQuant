import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../Base/Constants.qml" as Const

// ── TradePendingList — 委托队列 ──
Rectangle {
    id: root

    property var orders: []            // 委托列表

    signal cancelRequested(string orderId)

    implicitHeight: listHeight()
    color: Const.Constants.tertiaryBg
    radius: 6

    function listHeight() {
        var count = root.orders ? root.orders.length : 0
        if (count === 0) return 44
        return Math.min(count * 36 + 24, 200)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        // ── 标题 ──
        Text {
            Layout.fillWidth: true
            text: {
                var c = root.orders ? root.orders.length : 0
                return "委托 (" + c + ")"
            }
            font.pixelSize: 12; font.weight: Font.Medium
            color: Const.Constants.textSecondary
        }

        // ── 列表 ──
        ListView {
            id: listView
            visible: root.orders && root.orders.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.orders || []
            clip: true
            spacing: 2

            delegate: TradePendingRow {
                width: listView.width
                orderData: modelData
                onCancelClicked: function(id) { root.cancelRequested(id) }
            }
        }

        // ── 空状态 ──
        Text {
            visible: !root.orders || root.orders.length === 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: "暂无委托"
            font.pixelSize: 11
            color: Const.Constants.textTertiary
            horizontalAlignment: Text.AlignHCenter
        }
    }
}

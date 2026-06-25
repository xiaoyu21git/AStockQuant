import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../Base/Constants.qml" as Const
import "TradeUtils.js" as Utils

// ── TradePendingRow — 单条委托行 ──
Rectangle {
    id: root

    property var orderData: null   // {symbol, side, price, quantity, status, brokerOrderId, time}

    signal cancelClicked(string orderId)

    implicitHeight: 32
    color: "transparent"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        spacing: 6

        // 方向标签
        Rectangle {
            width: 30; height: 20; radius: 3
            color: {
                var s = root.orderData ? root.orderData.side || "" : ""
                return s === "BUY" ? Const.Constants.lossRed : Const.Constants.profitGreen
            }
            Text {
                anchors.centerIn: parent
                text: {
                    var s = root.orderData ? root.orderData.side || "" : ""
                    return s === "BUY" ? "买" : s === "SELL" ? "卖" : s
                }
                font.pixelSize: 10; font.weight: Font.Bold; color: "white"
            }
        }

        // 代码
        Text {
            Layout.preferredWidth: 72
            text: root.orderData ? (root.orderData.symbol || "--") : "--"
            font.pixelSize: 12; font.family: "Inter, Noto Sans SC"
            color: Const.Constants.textPrimary; elide: Text.ElideRight
        }

        // 数量 @ 价格
        Text {
            Layout.fillWidth: true
            text: {
                if (!root.orderData) return "--"
                var qty = root.orderData.quantity || 0
                var price = root.orderData.price || 0
                return qty + "股" + (price > 0 ? " @ " + Utils.formatPrice(price) : "")
            }
            font.pixelSize: 11; font.family: "Inter, Noto Sans SC"
            color: Const.Constants.textSecondary; elide: Text.ElideRight
        }

        // 状态
        Text {
            Layout.preferredWidth: 36
            text: Utils.orderStatusLabel(root.orderData ? root.orderData.status || "" : "")
            font.pixelSize: 11; font.family: "Inter, Noto Sans SC"
            color: Const.Constants.textSecondary
            horizontalAlignment: Text.AlignHCenter
        }

        // 撤单按钮
        Rectangle {
            visible: {
                if (!root.orderData) return false
                var s = root.orderData.status || ""
                return s === "pending" || s === "submitted" || s === "partial"
            }
            width: 40; height: 22; radius: 3
            color: Const.Constants.lossRed + "22"
            border.color: Const.Constants.lossRed
            Text {
                anchors.centerIn: parent; text: "撤单"
                font.pixelSize: 10; color: Const.Constants.lossRedLight
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    var id = root.orderData ? (root.orderData.brokerOrderId || root.orderData.id || "") : ""
                    if (id !== "") root.cancelClicked(id)
                }
            }
        }
    }
}

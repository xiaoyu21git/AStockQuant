import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../Base/Constants.qml" as Const
import "TradeUtils.js" as Utils

// ── TradeFeePreview — 费用预估 ──
Rectangle {
    id: root

    property double price: 0.0
    property int quantity: 0
    property var feeRate: ({ commission: 0.0003, stampTax: 0.001, minCommission: 5.0 })
    property bool isSell: false

    implicitHeight: feeColumn.implicitHeight + 12
    color: "transparent"

    property var feeResult: Utils.computeFee(price, quantity, feeRate)

    ColumnLayout {
        id: feeColumn
        anchors.fill: parent
        anchors.margins: 6
        spacing: 2

        // ── 预计金额 ──
        RowLayout {
            visible: root.price > 0 && root.quantity > 0
            Layout.fillWidth: true
            Text {
                text: "预计" + (root.isSell ? "收入" : "支出")
                font.pixelSize: 12
                color: Const.Constants.textSecondary
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "¥" + Utils.formatAmount(
                    root.isSell ? root.feeResult.notional - root.feeResult.commission - root.feeResult.tax
                                : root.feeResult.total)
                font.pixelSize: 13
                font.weight: Font.Bold
                color: Const.Constants.textPrimary
            }
        }

        // ── 佣金 ──
        RowLayout {
            visible: root.price > 0 && root.quantity > 0
            Layout.fillWidth: true
            Text {
                text: "佣金"
                font.pixelSize: 11
                color: Const.Constants.textTertiary
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "¥" + Utils.formatPrice(root.feeResult.commission, 2)
                font.pixelSize: 11
                color: Const.Constants.textTertiary
            }
        }

        // ── 印花税（仅卖出）──
        RowLayout {
            visible: root.isSell && root.price > 0 && root.quantity > 0
            Layout.fillWidth: true
            Text {
                text: "印花税"
                font.pixelSize: 11
                color: Const.Constants.textTertiary
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "¥" + Utils.formatPrice(root.feeResult.tax, 2)
                font.pixelSize: 11
                color: Const.Constants.textTertiary
            }
        }

        // ── 空状态 ──
        Text {
            visible: root.price <= 0 || root.quantity <= 0
            Layout.fillWidth: true
            text: "输入价格和数量后显示费用预估"
            font.pixelSize: 11
            color: Const.Constants.textTertiary
            horizontalAlignment: Text.AlignHCenter
        }
    }
}

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../Base/Constants.qml" as Const
import "TradeUtils.js" as Utils

// ── TradeOrderSheet — 下单参数表单 ──
Rectangle {
    id: root

    // ── 双向绑定属性 ──
    property string priceType: "market"      // "market" | "limit"
    property double orderPrice: 0.0
    property int orderQuantity: 0
    property string orderSide: "buy"         // 当前买卖方向
    property string displayDensity: "full"

    // ── 外部注入 ──
    property var marketSnapshot: null
    property var accountSnapshot: null
    property var feeRate: ({ commission: 0.0003, stampTax: 0.001, minCommission: 5.0 })

    // ── 参考价格 ──
    property double refPrice: marketSnapshot ? (marketSnapshot.price || 0) : 0

    implicitHeight: densityHeight()
    color: Const.Constants.tertiaryBg
    radius: 6

    function densityHeight() {
        if (displayDensity === "compact") return 100
        return 180
    }

    // ── 价格步进 = 0.01 ──
    function adjustPrice(delta) {
        if (root.priceType !== "limit") {
            root.priceType = "limit"
            root.orderPrice = root.refPrice
        }
        root.orderPrice = Math.max(0.01, root.orderPrice + delta)
        root.orderPrice = parseFloat(root.orderPrice.toFixed(2))
    }

    // ── 快捷填单比例计算 ──
    function fillQuickShare(level) {
        var base = 0
        if (root.orderSide === "sell" && root.accountSnapshot) {
            base = root.accountSnapshot.availableShares || 0
        } else if (root.accountSnapshot) {
            var cash = root.accountSnapshot.availableCash || 0
            var price = root.priceType === "limit" && root.orderPrice > 0
                ? root.orderPrice : root.refPrice
            if (price > 0) base = Math.floor(cash / (price * 100)) * 100
        }
        var ratio = level === "quarter" ? 0.25 : level === "half" ? 0.5 : 1.0
        root.orderQuantity = Math.floor(base * ratio / 100) * 100
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: displayDensity === "compact" ? 4 : 8

        // ── 价格类型 + 价格输入 ──
        RowLayout {
            Layout.fillWidth: true

            // 价格类型切换
            Row {
                spacing: 4
                Rectangle {
                    width: 48; height: 28; radius: 4
                    color: root.priceType === "market" ? Const.Constants.accentBlue : Const.Constants.borderColor
                    Text {
                        anchors.centerIn: parent; text: "市价"
                        font.pixelSize: 12; color: "white"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.priceType = "market"
                    }
                }
                Rectangle {
                    width: 48; height: 28; radius: 4
                    color: root.priceType === "limit" ? Const.Constants.accentBlue : Const.Constants.borderColor
                    Text {
                        anchors.centerIn: parent; text: "限价"
                        font.pixelSize: 12; color: "white"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.priceType = "limit"
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // 参考价（市价时显示）
            Text {
                visible: root.priceType === "market" && root.refPrice > 0
                text: "参考 " + Utils.formatPrice(root.refPrice)
                font.pixelSize: 12
                color: Const.Constants.textSecondary
            }
        }

        // ── 价格输入行（限价时显示）──
        RowLayout {
            visible: root.priceType === "limit"
            Layout.fillWidth: true

            // [-]
            Rectangle {
                width: 28; height: 28; radius: 4
                color: Const.Constants.borderColor
                Text {
                    anchors.centerIn: parent; text: "-"
                    font.pixelSize: 16; color: Const.Constants.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.adjustPrice(-0.01)
                }
            }

            // 价格输入框
            TextField {
                id: priceInput
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                text: root.orderPrice > 0 ? root.orderPrice.toFixed(2) : ""
                placeholderText: Utils.formatPrice(root.refPrice)
                font.pixelSize: 14
                font.family: "Inter, Noto Sans SC"
                color: "#ff6b6b"
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator { bottom: 0.01; top: 999999.99; decimals: 2 }

                background: Rectangle {
                    radius: 4
                    color: Const.Constants.secondaryBg
                    border.color: priceInput.activeFocus ? Const.Constants.accentBlue : Const.Constants.borderColor
                }

                onEditingFinished: {
                    var v = parseFloat(text)
                    root.orderPrice = isNaN(v) ? 0 : v
                }
            }

            // [+]
            Rectangle {
                width: 28; height: 28; radius: 4
                color: Const.Constants.borderColor
                Text {
                    anchors.centerIn: parent; text: "+"
                    font.pixelSize: 16; color: Const.Constants.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.adjustPrice(0.01)
                }
            }
        }

        // ── 快捷数量按钮 ──
        RowLayout {
            Layout.fillWidth: true

            Repeater {
                model: ["quarter", "half", "full"]
                Rectangle {
                    width: 56; height: 26; radius: 4
                    color: Const.Constants.borderColor
                    Text {
                        anchors.centerIn: parent
                        text: Utils.quickShareLabel(modelData)
                        font.pixelSize: 11; color: Const.Constants.textSecondary
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.fillQuickShare(modelData)
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        // ── 数量输入 ──
        TextField {
            id: qtyInput
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            text: root.orderQuantity > 0 ? root.orderQuantity.toString() : ""
            placeholderText: "数量（100 的倍数）"
            font.pixelSize: 14
            font.family: "Inter, Noto Sans SC"
            color: "#ffd93d"
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 100; top: 999999900 }

            background: Rectangle {
                radius: 4
                color: Const.Constants.secondaryBg
                border.color: qtyInput.activeFocus ? Const.Constants.accentBlue : Const.Constants.borderColor
            }

            onEditingFinished: {
                var v = parseInt(text)
                if (!isNaN(v)) root.orderQuantity = Math.floor(v / 100) * 100
            }
        }

        // ── 费用预估 ──
        TradeFeePreview {
            Layout.fillWidth: true
            price: root.priceType === "limit" && root.orderPrice > 0
                ? root.orderPrice : root.refPrice
            quantity: root.orderQuantity
            feeRate: root.feeRate
            isSell: root.orderSide === "sell"
        }
    }
}

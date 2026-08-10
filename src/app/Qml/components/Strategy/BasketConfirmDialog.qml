import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

Rectangle {
    id: dialog

    // ── 尺寸与状态 ──
    implicitWidth: 720
    implicitHeight: 560
    radius: 20
    color: "#1E293B"
    border.color: "#334155"

    property bool isOpen: false
    property string strategyName: ""
    property string contextDesc: ""

    // ── 颜色常量 ──
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color accentBlue: "#3B82F6"
    readonly property color accentGreen: "#10B981"
    readonly property color dangerRed: "#EF4444"
    readonly property color tertiaryBg: "#334155"
    readonly property color borderLight: "#475569"

    // ── 订单数据 (每个 item: symbol, side, sideRaw, quantity, targetWeight, signalScore, traceId) ──
    property var orders: []
    property int orderCount: 0
    property var editedQtys: []  // 用户编辑后的数量数组

    // ═════════════════════════════════════════════════════
    // Bridge 连接: 监听篮子变化
    // ═════════════════════════════════════════════════════
    Connections {
        target: Bridge.StrategyBridge
        function onPendingBasketChanged() {
            if (Bridge.StrategyBridge.hasPendingBasket) {
                dialog.orders = Bridge.StrategyBridge.pendingBasketOrders
                dialog.strategyName = Bridge.StrategyBridge.pendingBasketStrategyName
                dialog.contextDesc = Bridge.StrategyBridge.pendingBasketContextDesc
                dialog.orderCount = dialog.orders.length
                // 初始化编辑数量数组 (用原始数量填充)
                dialog.editedQtys = []
                for (var i = 0; i < dialog.orderCount; i++) {
                    var qty = dialog.orders[i].quantity
                    dialog.editedQtys.push(qty)
                }
                dialog.isOpen = true
            } else {
                dialog.isOpen = false
            }
        }
    }

    // ── 可见性绑定 ──
    visible: isOpen

    // ═════════════════════════════════════════════════════
    // 布局
    // ═════════════════════════════════════════════════════
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 标题栏 ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 72
            color: tertiaryBg

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 16

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "📋 篮子订单确认"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }
                    Text {
                        text: strategyName + " · " + contextDesc + " · " + orderCount + " 笔订单"
                        font.pixelSize: 12
                        color: textSecondary
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // ── 表头 ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            color: "#263348"
            border.color: borderLight
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 16
                spacing: 0

                Text { text: "标的"; font.pixelSize: 12; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 140 }
                Text { text: "方向"; font.pixelSize: 12; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 60 }
                Text { text: "权重"; font.pixelSize: 12; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 70 }
                Text { text: "评分"; font.pixelSize: 12; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 70 }
                Item { Layout.fillWidth: true }
                Text { text: "数量(股)"; font.pixelSize: 12; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
            }
        }

        // ── 订单列表 ──
        ListView {
            id: orderList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: orders
            spacing: 0

            delegate: Rectangle {
                width: orderList.width
                implicitHeight: 52
                color: index % 2 === 0 ? "#1E293B" : "#22304A"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 16
                    spacing: 0

                    // 标的代码
                    Text {
                        text: modelData.symbol || ""
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: textPrimary
                        Layout.preferredWidth: 140
                    }

                    // 方向 (买入绿色 / 卖出红色)
                    Text {
                        text: modelData.side || ""
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: modelData.sideRaw === 0 ? accentGreen : dangerRed
                        Layout.preferredWidth: 60
                    }

                    // 权重
                    Text {
                        text: modelData.targetWeight !== undefined
                              ? (modelData.targetWeight * 100).toFixed(1) + "%"
                              : "—"
                        font.pixelSize: 13
                        color: textSecondary
                        Layout.preferredWidth: 70
                    }

                    // 评分
                    Rectangle {
                        Layout.preferredWidth: 60
                        implicitHeight: 20
                        radius: 4
                        color: {
                            var s = modelData.signalScore || 0
                            if (s >= 0.7) return Qt.rgba(16/255, 185/255, 129/255, 0.2)
                            if (s >= 0.4) return Qt.rgba(245/255, 158/255, 11/255, 0.2)
                            return Qt.rgba(239/255, 68/255, 68/255, 0.15)
                        }

                        Text {
                            anchors.centerIn: parent
                            text: modelData.signalScore !== undefined
                                  ? (modelData.signalScore * 100).toFixed(0)
                                  : "—"
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            color: {
                                var s = modelData.signalScore || 0
                                if (s >= 0.7) return accentGreen
                                if (s >= 0.4) return accentGreen  // "#F59E0B" if you want warning
                                return dangerRed
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // 数量编辑框 (SpinBox 风格)
                    Rectangle {
                        Layout.preferredWidth: 110
                        implicitHeight: 34
                        radius: 8
                        color: "#0F172A"
                        border.color: qtyInput.activeFocus ? accentBlue : borderLight

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 2
                            spacing: 0

                            // 减号
                            Rectangle {
                                implicitWidth: 28; implicitHeight: 28
                                radius: 6
                                color: qtyMinusMa.containsMouse ? accentBlue : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "−"; font.pixelSize: 16; color: textSecondary
                                }
                                MouseArea {
                                    id: qtyMinusMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var newVal = Math.max(100, dialog.editedQtys[index] - 100)
                                        dialog.editedQtys[index] = newVal
                                        dialog.editedQtys = dialog.editedQtys  // 触发更新
                                    }
                                }
                            }

                            // 数量输入
                            TextInput {
                                id: qtyInput
                                Layout.fillWidth: true
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                color: textPrimary
                                text: dialog.editedQtys[index] !== undefined
                                      ? dialog.editedQtys[index].toString()
                                      : modelData.quantity.toString()
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 100; top: 99999999 }

                                onEditingFinished: {
                                    var v = parseInt(text)
                                    if (!isNaN(v) && v >= 100) {
                                        dialog.editedQtys[index] = v
                                        dialog.editedQtys = dialog.editedQtys  // 触发更新
                                    } else {
                                        text = dialog.editedQtys[index].toString()
                                    }
                                }
                            }

                            // 加号
                            Rectangle {
                                implicitWidth: 28; implicitHeight: 28
                                radius: 6
                                color: qtyPlusMa.containsMouse ? accentBlue : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "+"; font.pixelSize: 16; color: textSecondary
                                }
                                MouseArea {
                                    id: qtyPlusMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var newVal = dialog.editedQtys[index] + 100
                                        dialog.editedQtys[index] = newVal
                                        dialog.editedQtys = dialog.editedQtys
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── 底部按钮栏 ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            color: tertiaryBg

            RowLayout {
                anchors.centerIn: parent
                spacing: 16

                // 拒绝按钮
                Rectangle {
                    implicitWidth: 140; implicitHeight: 40
                    radius: 10
                    color: rejectMa.containsMouse ? "#EF4444" : "transparent"
                    border.color: dangerRed
                    border.width: 1.5

                    Text {
                        anchors.centerIn: parent
                        text: "✕ 全部拒绝"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: rejectMa.containsMouse ? "#FFFFFF" : dangerRed
                    }

                    MouseArea {
                        id: rejectMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Bridge.StrategyBridge.rejectBasket()
                            dialog.isOpen = false
                        }
                    }
                }

                // 确认按钮
                Rectangle {
                    implicitWidth: 200; implicitHeight: 40
                    radius: 10
                    color: confirmMa.containsMouse ? "#059669" : accentGreen

                    Text {
                        anchors.centerIn: parent
                        text: "✓ 确认下单 (" + orderCount + " 笔)"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#FFFFFF"
                    }

                    MouseArea {
                        id: confirmMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // 构建编辑后的订单列表
                            var edited = []
                            for (var i = 0; i < dialog.orderCount; i++) {
                                var item = {}
                                for (var key in dialog.orders[i]) {
                                    item[key] = dialog.orders[i][key]
                                }
                                if (dialog.editedQtys[i] !== undefined) {
                                    item.quantity = dialog.editedQtys[i]
                                }
                                edited.push(item)
                            }
                            Bridge.StrategyBridge.confirmBasket(edited)
                            dialog.isOpen = false
                        }
                    }
                }
            }
        }
    }
}

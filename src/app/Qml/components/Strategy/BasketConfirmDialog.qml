import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

Rectangle {
    id: dialog

    // ── 尺寸与状态 ──
    implicitWidth: 800
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
    readonly property color warningAmber: "#F59E0B"
    readonly property color tertiaryBg: "#334155"
    readonly property color borderLight: "#475569"

    // ── 订单数据 (每个 item: stockName, symbol, side, sideRaw, quantity, price, orderType,
    //              targetWeight, signalScore, signalIntent, traceId) ──
    property var orders: []
    property int orderCount: 0
    property var editedQtys: []  // 用户编辑后的数量数组 (与 orders 同步排序/删除)

    // ── 汇总统计 ──
    property int buyCount: 0
    property int buyTotalQty: 0
    property int sellCount: 0
    property int sellTotalQty: 0

    // ── 资金统计 ──
    property var accountSnapshot: ({})
    property double buyAmount: 0       // 买入预估金额
    property double sellAmount: 0      // 卖出预估金额
    property double netAmount: 0       // 净占用 (买-卖)
    property double buyPct: 0          // 买入金额 / 可用资金 %
    property bool hasUnknownPrice: false  // 是否有无法估价的订单

    function calcAmounts() {
        var bAmt = 0, sAmt = 0, unknown = false
        for (var i = 0; i < orders.length; i++) {
            var qty = editedQtys[i] !== undefined ? editedQtys[i] : orders[i].quantity
            var effPrice = orders[i].price > 0 ? orders[i].price : (orders[i].latestPrice || 0)
            var amt = effPrice * qty
            if (orders[i].sideRaw === 0) {  // Buy
                bAmt += amt
                if (effPrice <= 0) unknown = true
            } else {                         // Sell
                sAmt += amt
                if (effPrice <= 0) unknown = true
            }
        }
        buyAmount = bAmt; sellAmount = sAmt; netAmount = bAmt - sAmt
        hasUnknownPrice = unknown
        var availCash = accountSnapshot.availableCash || 0
        buyPct = availCash > 0 ? (bAmt / availCash * 100) : 0
    }

    function updateSummary() {
        var bc = 0, bq = 0, sc = 0, sq = 0
        for (var i = 0; i < orders.length; i++) {
            var qty = editedQtys[i] !== undefined ? editedQtys[i] : orders[i].quantity
            if (orders[i].sideRaw === 0) {  // Buy=0
                bc++; bq += qty
            } else {                         // Sell=1
                sc++; sq += qty
            }
        }
        buyCount = bc; buyTotalQty = bq; sellCount = sc; sellTotalQty = sq
        calcAmounts()
    }

    // ── 排序: signalScore 降序, 同步 orders 和 editedQtys ──
    function sortOrdersByScore() {
        var combined = orders.map(function(o, i) {
            return { order: o, qty: editedQtys[i] }
        })
        combined.sort(function(a, b) {
            return (b.order.signalScore || 0) - (a.order.signalScore || 0)
        })
        orders = combined.map(function(c) { return c.order })
        editedQtys = combined.map(function(c) { return c.qty })
    }

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
                    dialog.editedQtys.push(dialog.orders[i].quantity)
                }
                // 按 signalScore 降序排列
                dialog.sortOrdersByScore()
                dialog.updateSummary()
                dialog.initAccountSnapshot()
                dialog.isOpen = true
            } else {
                dialog.isOpen = false
            }
        }
    }

    // ── 账户数据 (资金占用统计) ──
    Connections {
        target: Bridge.PositionAccountBridge
        function onAccountSnapshotChanged() {
            dialog.accountSnapshot = Bridge.PositionAccountBridge.accountSnapshot || ({})
            if (dialog.isOpen) dialog.calcAmounts()
        }
    }

    // 首显时主动读取一次账户快照
    function initAccountSnapshot() {
        var snap = Bridge.PositionAccountBridge.accountSnapshot
        if (snap && Object.keys(snap).length > 0) {
            dialog.accountSnapshot = snap
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
            implicitHeight: 68
            color: tertiaryBg

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
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

        // ── 交易汇总条 (双行: 股数 + 金额) ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 54
            color: "#263348"

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                spacing: 1

                // 第一行: 笔数 · 股数
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "📊"; font.pixelSize: 13 }
                        Text {
                            text: buyCount > 0
                                ? "买入 " + buyCount + " 笔 · " + buyTotalQty + " 股"
                                : "买入 —"
                            font.pixelSize: 13; font.weight: Font.Medium
                            color: buyCount > 0 ? accentGreen : textTertiary
                        }
                    }

                    Rectangle {
                        implicitWidth: 1; implicitHeight: 18
                        color: borderLight
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "📉"; font.pixelSize: 13 }
                        Text {
                            text: sellCount > 0
                                ? "卖出 " + sellCount + " 笔 · " + sellTotalQty + " 股"
                                : "卖出 —"
                            font.pixelSize: 13; font.weight: Font.Medium
                            color: sellCount > 0 ? dangerRed : textTertiary
                        }
                        Layout.leftMargin: 12
                    }
                }

                // 第二行: 金额 · 占比 · 净额
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: buyAmount > 0
                                ? "¥" + buyAmount.toLocaleString(Qt.locale(), "f", 0)
                                : (buyCount > 0 ? "¥—" : "")
                            font.pixelSize: 13; font.weight: Font.DemiBold
                            color: accentGreen
                        }
                        Text {
                            text: buyPct > 0 ? "(" + buyPct.toFixed(1) + "%)" : ""
                            font.pixelSize: 11
                            color: textSecondary
                        }
                    }

                    Rectangle {
                        implicitWidth: 1; implicitHeight: 18
                        color: borderLight
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: sellAmount > 0
                                ? "¥" + sellAmount.toLocaleString(Qt.locale(), "f", 0)
                                : (sellCount > 0 ? "¥—" : "")
                            font.pixelSize: 13; font.weight: Font.DemiBold
                            color: dangerRed
                        }
                        Text {
                            text: netAmount !== 0 && (buyAmount + sellAmount) > 0
                                ? (netAmount > 0 ? "占用 ¥" + netAmount.toLocaleString(Qt.locale(), "f", 0)
                                                : "回笼 ¥" + Math.abs(netAmount).toLocaleString(Qt.locale(), "f", 0))
                                : ""
                            font.pixelSize: 12
                            color: netAmount >= 0 ? warningAmber : accentGreen
                        }
                        Layout.leftMargin: 12
                    }
                }
            }
        }

        // ── 表头 ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            color: "#1A2744"
            border.color: borderLight
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 12
                spacing: 0

                Text { text: "名称"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 100 }
                Text { text: "代码"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 85 }
                Text { text: "方向"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 50 }
                Text { text: "权重"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 60 }
                Text { text: "评分"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 55 }
                Text { text: "价格"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 85 }
                Item { Layout.fillWidth: true }
                Text { text: "数量(股)"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                Item { Layout.preferredWidth: 40 }  // 删除按钮占位
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
                implicitHeight: 50
                color: index % 2 === 0 ? "#1E293B" : "#22304A"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 12
                    spacing: 0

                    // 股票名称
                    Text {
                        text: modelData.stockName || ""
                        font.pixelSize: 12
                        color: textPrimary
                        Layout.preferredWidth: 100
                        elide: Text.ElideRight
                    }

                    // 标的代码
                    Text {
                        text: modelData.symbol || ""
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: textPrimary
                        Layout.preferredWidth: 85
                    }

                    // 方向
                    Text {
                        text: modelData.side || ""
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: modelData.sideRaw === 0 ? accentGreen : dangerRed
                        Layout.preferredWidth: 50
                    }

                    // 权重
                    Text {
                        text: modelData.targetWeight !== undefined
                              ? (modelData.targetWeight * 100).toFixed(1) + "%"
                              : "—"
                        font.pixelSize: 12
                        color: textSecondary
                        Layout.preferredWidth: 60
                    }

                    // 评分
                    Rectangle {
                        Layout.preferredWidth: 48
                        implicitHeight: 18
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
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            color: {
                                var s = modelData.signalScore || 0
                                if (s >= 0.7) return accentGreen
                                return dangerRed
                            }
                        }
                    }

                    // 价格编辑 (点击切换显示/编辑)
                    Item {
                        Layout.preferredWidth: 80
                        implicitHeight: 30

                        // 显示层
                        Text {
                            id: priceText
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignRight
                            text: modelData.price > 0 ? modelData.price.toFixed(2) : "市价"
                            font.pixelSize: 12
                            font.weight: modelData.price > 0 ? Font.Normal : Font.Medium
                            color: modelData.price > 0 ? "#E5E7EB" : warningAmber
                            elide: Text.ElideRight

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    priceText.visible = false
                                    priceInput.visible = true
                                    priceInput.text = modelData.price > 0 ? modelData.price.toFixed(2) : ""
                                    priceInput.forceActiveFocus()
                                    Qt.callLater(function() {
                                        if (priceInput.visible) priceInput.selectAll()
                                    })
                                }
                            }
                        }

                        // 编辑层
                        TextInput {
                            id: priceInput
                            anchors.fill: parent
                            visible: false
                            verticalAlignment: TextInput.AlignVCenter
                            horizontalAlignment: TextInput.AlignRight
                            font.pixelSize: 12
                            color: "#E5E7EB"
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            validator: DoubleValidator { bottom: 0; decimals: 3; notation: DoubleValidator.StandardNotation }

                            onEditingFinished: {
                                var val = parseFloat(text)
                                if (isNaN(val) || val <= 0) {
                                    // 恢复市价
                                    dialog.orders[index].price = 0
                                    dialog.orders[index].orderType = 1  // Market
                                } else {
                                    dialog.orders[index].price = val
                                    dialog.orders[index].orderType = 0  // Limit
                                }
                                dialog.orders = dialog.orders  // 触发绑定刷新
                                visible = false
                                priceText.visible = true
                            }

                            // ESC 取消编辑
                            Keys.onEscapePressed: {
                                visible = false
                                priceText.visible = true
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // 数量编辑框 (SpinBox 风格)
                    Rectangle {
                        Layout.preferredWidth: 100
                        implicitHeight: 30
                        radius: 8
                        color: "#0F172A"
                        border.color: qtyInput.activeFocus ? accentBlue : borderLight

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0

                            // 减号
                            Rectangle {
                                implicitWidth: 24; implicitHeight: 24
                                radius: 6
                                color: qtyMinusMa.containsMouse ? accentBlue : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "−"; font.pixelSize: 14; color: textSecondary
                                }
                                MouseArea {
                                    id: qtyMinusMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var newVal = Math.max(100, dialog.editedQtys[index] - 100)
                                        dialog.editedQtys[index] = newVal
                                        dialog.editedQtys = dialog.editedQtys
                                        dialog.updateSummary()
                                    }
                                }
                            }

                            // 数量输入
                            TextInput {
                                id: qtyInput
                                Layout.fillWidth: true
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                font.pixelSize: 12
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
                                        dialog.editedQtys = dialog.editedQtys
                                        dialog.updateSummary()
                                    } else {
                                        text = dialog.editedQtys[index].toString()
                                    }
                                }
                            }

                            // 加号
                            Rectangle {
                                implicitWidth: 24; implicitHeight: 24
                                radius: 6
                                color: qtyPlusMa.containsMouse ? accentBlue : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "+"; font.pixelSize: 14; color: textSecondary
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
                                        dialog.updateSummary()
                                    }
                                }
                            }
                        }
                    }

                    // 删除按钮
                    Rectangle {
                        Layout.preferredWidth: 28; implicitWidth: 28
                        implicitHeight: 28
                        radius: 14
                        color: delMa.containsMouse ? dangerRed : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            font.pixelSize: 12
                            color: delMa.containsMouse ? "#FFFFFF" : textTertiary
                        }

                        MouseArea {
                            id: delMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                dialog.orders.splice(index, 1)
                                dialog.editedQtys.splice(index, 1)
                                dialog.orderCount = dialog.orders.length
                                dialog.orders = dialog.orders  // 触发 ListView 刷新
                                dialog.updateSummary()
                            }
                        }
                    }
                }
            }
        }

        // ── 底部按钮栏 ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 60
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
                            var edited = []
                            for (var i = 0; i < dialog.orderCount; i++) {
                                edited.push({
                                    orderIndex: dialog.orders[i].orderIndex,
                                    quantity: dialog.editedQtys[i] !== undefined ? dialog.editedQtys[i] : dialog.orders[i].quantity
                                })
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

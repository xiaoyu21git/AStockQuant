import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/TradingConstants.js" as Const

Item {
    id: formRoot

    required property var orders
    required property bool compactMode
    required property real scaleFactor
    required property var tradingFormHelper

    signal cancelRequested(var orderId)
    signal approveCheckpointRequested(var orderData, bool retry)
    signal resumeExecutionRequested(var orderData, bool retry)

    function _s(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    readonly property int _orderRowHeight: _s(compactMode ? 48 : 72)
    readonly property int _metaFont: _s(compactMode ? 9 : 12)
    readonly property int _spacing: compactMode ? 4 : 6
    readonly property int _margins: compactMode ? 6 : 8
    readonly property int _itemRadius: compactMode ? 12 : 14
    readonly property int _itemPadding: compactMode ? 8 : 12
    readonly property int _btnRadius: compactMode ? 12 : 14
    readonly property int _btnW1: compactMode ? 56 : 72
    readonly property int _btnW2: compactMode ? 88 : 110
    readonly property int _btnH: compactMode ? 24 : 30
    readonly property int _btnFont: compactMode ? 10 : 11
    readonly property int _detailFont: compactMode ? 9 : 10
    readonly property int _bodyFont: compactMode ? 10 : 11

    ListView {
        anchors.fill: parent
        anchors.margins: _margins
        clip: true
        spacing: _spacing
        model: formRoot.orders

        delegate: Rectangle {
            property var orderData: modelData
            readonly property var orderUi: formRoot.tradingFormHelper.buildOrderPresentation(orderData || ({}))
            width: ListView.view.width
            height: _orderRowHeight
            radius: _itemRadius
            color: Const.tradingOrderItemBg
            border.color: orderUi.normalizedStatus === "CANCELLED" ? Const.tradingOrderItemCancelledBorder : Const.tradingOrderItemBorder
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: _itemPadding
                spacing: compactMode ? 6 : 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: orderData.symbol + "  " + orderData.action + "  " + String(orderUi.headlineAmount || "")
                        color: Const.tradingAccentCyan
                        font.pixelSize: _metaFont
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        text: String(orderUi.priceSummary || "")
                            + "  ·  " + orderData.time
                            + "  ·  " + orderData.status
                            + (String(orderUi.filledSummary || "").length > 0 ? "  ·  " + String(orderUi.filledSummary || "") : "")
                        color: Const.tradingOrderText
                        font.pixelSize: _bodyFont
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: String(orderUi.auxiliarySummary || "").length > 0
                        text: String(orderUi.auxiliarySummary || "")
                        color: Const.tradingOrderDetailText
                        font.pixelSize: _detailFont
                        elide: Text.ElideMiddle
                    }
                }

                Rectangle {
                    visible: orderUi.canCancel === true
                    radius: _btnRadius
                    color: Const.tradingCancelBg
                    border.color: Const.tradingCancelText
                    border.width: 1
                    implicitWidth: _btnW1
                    implicitHeight: _btnH

                    Text {
                        anchors.centerIn: parent
                        text: "撤单"
                        color: Const.tradingCancelText
                        font.pixelSize: _btnFont
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: formRoot.cancelRequested(orderData.cancelOrderId || orderData.id)
                    }
                }

                Rectangle {
                    visible: orderUi.canApproveManualCheckpoint === true
                    radius: _btnRadius
                    color: Const.tradingCheckpointBg
                    border.color: Const.tradingCheckpointText
                    border.width: 1
                    implicitWidth: _btnW2
                    implicitHeight: _btnH

                    Text {
                        anchors.centerIn: parent
                        text: String(orderUi.checkpointActionLabel || "人工确认")
                        color: Const.tradingCheckpointText
                        font.pixelSize: _btnFont
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: formRoot.approveCheckpointRequested(orderData, orderUi.canRetryManualCheckpoint === true)
                    }
                }

                Rectangle {
                    visible: orderUi.canResumeExecutionPause === true
                    radius: _btnRadius
                    color: Const.tradingResumeBg
                    border.color: Const.tradingResumeText
                    border.width: 1
                    implicitWidth: _btnW2
                    implicitHeight: _btnH

                    Text {
                        anchors.centerIn: parent
                        text: String(orderUi.executionPauseActionLabel || "恢复执行")
                        color: Const.tradingResumeText
                        font.pixelSize: _btnFont
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: formRoot.resumeExecutionRequested(orderData, orderUi.canRetryExecutionPause === true)
                    }
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: formRoot.orders.length === 0
        text: "暂无委托订单"
        color: Const.tradingEmptyText
        font.pixelSize: _metaFont
    }
}

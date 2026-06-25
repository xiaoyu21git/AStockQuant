import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import "../Base/Constants.qml" as Const

// ── TradeQuickActions — 买入 / 卖出按钮（始终可见）──
Rectangle {
    id: root

    // ── 输入 ──
    property bool canSubmit: true
    property string displayDensity: "full"
    property string lastError: ""

    // ── 输出 ──
    signal buyRequested()
    signal sellRequested()

    implicitHeight: densityHeight()
    color: "transparent"

    function densityHeight() {
        if (displayDensity === "mini") return 32
        return 48
    }

    RowLayout {
        anchors.fill: parent
        spacing: 10

        // ── 买入按钮 ──
        Rectangle {
            id: buyBtn
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: root.canSubmit ? Const.Constants.lossRed : Const.Constants.borderColor

            Text {
                anchors.centerIn: parent
                text: "买入"
                font.pixelSize: displayDensity === "mini" ? 13 : 15
                font.family: "Inter, Noto Sans SC"
                font.weight: Font.Bold
                color: root.canSubmit ? "white" : Const.Constants.textTertiary
            }

            layer.enabled: root.canSubmit
            layer.effect: DropShadow {
                radius: 6
                samples: 12
                color: "#ef444466"
                horizontalOffset: 0
                verticalOffset: 2
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.canSubmit
                cursorShape: Qt.PointingHandCursor
                onClicked: root.buyRequested()
            }
        }

        // ── 卖出按钮 ──
        Rectangle {
            id: sellBtn
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: root.canSubmit ? Const.Constants.profitGreen : Const.Constants.borderColor

            Text {
                anchors.centerIn: parent
                text: "卖出"
                font.pixelSize: displayDensity === "mini" ? 13 : 15
                font.family: "Inter, Noto Sans SC"
                font.weight: Font.Bold
                color: root.canSubmit ? "white" : Const.Constants.textTertiary
            }

            layer.enabled: root.canSubmit
            layer.effect: DropShadow {
                radius: 6
                samples: 12
                color: "#10b98166"
                horizontalOffset: 0
                verticalOffset: 2
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.canSubmit
                cursorShape: Qt.PointingHandCursor
                onClicked: root.sellRequested()
            }
        }
    }

    // ── 错误提示 ──
    Text {
        anchors.top: parent.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.lastError !== ""
        text: root.lastError
        font.pixelSize: 11
        font.family: "Inter, Noto Sans SC"
        color: Const.Constants.lossRedLight
    }
}

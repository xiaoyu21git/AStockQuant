import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot

    // 数据流入
    required property var alertItems

    // 可选：两列模式下与持仓面板等高对齐
    property real matchImplicitHeight: 0

    // 主题常量
    readonly property color pageText: "#E2E8F0"
    readonly property color subtleText: "#64748B"
    readonly property color elevatedCardBg: "#111827"
    readonly property color cardBorder: "#334155"
    readonly property color cardBorderSoft: "#273449"
    readonly property color dangerRed: "#EF4444"
    readonly property color warningOrange: "#F97316"
    readonly property color primaryBlue: "#3B82F6"
    readonly property int cardRadius: 20
    readonly property int cardInnerPadding: 24

    // 事件流出
    signal alertDismissed(int index)

    function alertBackground(level) {
        if (level === "danger") return dangerRed
        if (level === "warning") return warningOrange
        return primaryBlue
    }

    function alertForeground(level) {
        if (level === "danger") return "#FFFFFF"
        if (level === "warning") return "#FFFFFF"
        return "#BFDBFE"
    }

    Layout.fillWidth: true
    radius: cardRadius
    color: elevatedCardBg
    border.color: cardBorder
    border.width: 1
    implicitHeight: Math.max(alertsCardColumn.implicitHeight + 48, matchImplicitHeight)

    ColumnLayout {
        id: alertsCardColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: cardInnerPadding
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "实时预警"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: pageText
            }

            Item { Layout.fillWidth: true }
        }

        Repeater {
            model: panelRoot.alertItems.length

            delegate: Rectangle {
                readonly property var alertItemData: panelRoot.alertItems[index] || ({})
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                color: "transparent"
                border.color: cardBorderSoft
                border.width: index === panelRoot.alertItems.length - 1 ? 0 : 1

                RowLayout {
                    anchors.fill: parent
                    spacing: 12

                    Rectangle {
                        width: 32
                        height: 32
                        radius: 10
                        color: alertBackground(alertItemData.level)

                        Text {
                            anchors.centerIn: parent
                            text: alertItemData.icon || ""
                            font.pixelSize: 15
                            color: alertForeground(alertItemData.level)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: alertItemData.title || ""
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: pageText
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: alertItemData.time || ""
                            font.pixelSize: 12
                            color: subtleText
                        }
                    }
                }
            }
        }

        Text {
            visible: panelRoot.alertItems.length === 0
            text: "当前没有可生成的真实预警项"
            font.pixelSize: 13
            color: subtleText
        }
    }
}

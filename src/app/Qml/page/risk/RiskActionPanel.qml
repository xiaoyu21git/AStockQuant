import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0 as ConsoleUiComponents

Rectangle {
    id: panelRoot

    // 数据流入
    required property var historyItems

    // 主题常量（与 RiskConfigurationPage 保持一致）
    readonly property color pageText: "#E2E8F0"
    readonly property color secondaryText: "#94A3B8"
    readonly property color subtleText: "#64748B"
    readonly property color cardBg: "#1E293B"
    readonly property color cardBorder: "#334155"
    readonly property color cardBorderSoft: "#273449"
    readonly property int cardRadius: 20
    readonly property int cardInnerPadding: 24
    readonly property int compactGap: 16

    // 事件流出
    signal actionTriggered(string action)

    Layout.fillWidth: true
    radius: cardRadius
    color: cardBg
    border.color: cardBorder
    border.width: 1
    implicitHeight: actionsCardColumn.implicitHeight + 48

    ColumnLayout {
        id: actionsCardColumn
        anchors.fill: parent
        anchors.margins: cardInnerPadding
        spacing: compactGap

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "风控操作"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: pageText
            }

            Item { Layout.fillWidth: true }
        }

        Flow {
            Layout.fillWidth: true
            spacing: compactGap

            Repeater {
                model: [
                    { text: "一键减仓30%", style: "outline", action: "减仓30%" },
                    { text: "全部平仓", style: "danger", action: "全部平仓" },
                    { text: "清除预警", style: "outline", action: "清除预警" },
                    { text: "导出风控报告", style: "primary", action: "导出风控报告" }
                ]

                delegate: ConsoleUiComponents.ActionButton {
                    readonly property var actionButtonData: modelData
                    label: actionButtonData.text
                    tone: actionButtonData.style === "primary"
                        ? "primary"
                        : (actionButtonData.style === "danger" ? "danger" : "muted")
                    buttonWidth: Math.max(132, actionButtonData.text.length * 13 + 28)
                    buttonHeight: 40
                    labelSize: 13
                    onClicked: panelRoot.actionTriggered(actionButtonData.action)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "最近操作记录"
                font.pixelSize: 13
                font.weight: Font.Medium
                color: secondaryText
            }

            Repeater {
                model: panelRoot.historyItems.length

                delegate: Rectangle {
                    readonly property var historyItemData: panelRoot.historyItems[index] || ({})
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: "transparent"
                    border.color: cardBorderSoft
                    border.width: index === panelRoot.historyItems.length - 1 ? 0 : 1

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 0
                        anchors.rightMargin: 0
                        text: (historyItemData.time || "") + " · " + (historyItemData.content || "")
                        font.pixelSize: 13
                        color: secondaryText
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                visible: panelRoot.historyItems.length === 0
                text: "暂无来自策略服务或页面操作的记录"
                font.pixelSize: 13
                color: subtleText
            }
        }
    }
}

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../components" as Components
import "../../components/Strategy" as StrategyComponents
import "../../components/Base" as BaseComponents

Rectangle {
    id: panelRoot
    // -- Interface --
    required property QtObject page
    visible: !panelRoot.page.showBacktestWorkbench && !panelRoot.page.showPerformance
    Layout.fillWidth: true
    Layout.preferredHeight: 124
    Layout.alignment: Qt.AlignHCenter
    radius: panelRoot.page.borderRadiusXLarge
    color: panelRoot.page.secondaryBg
    border.color: Qt.rgba(71 / 255, 85 / 255, 105 / 255, 0.22)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: panelRoot.page.borderRadiusMedium
                color: "#0B1220"
                border.width: 1
                border.color: "#334155"

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 10

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "检索"
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: "#CBD5E1"
                    }

                    TextInput {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 90
                        font.pixelSize: 15
                        color: panelRoot.page.textPrimary
                        text: panelRoot.page.strategyLibrarySearchText

                        onTextChanged: {
                            if (panelRoot.page.strategyLibrarySearchText !== text) {
                                panelRoot.page.strategyLibrarySearchText = text
                            }
                        }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "搜索策略名称、描述或标签..."
                            font: parent.font
                            color: panelRoot.page.textSecondary
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }

            StrategyComponents.StrategyFilterButton {
                active: panelRoot.page.showFilter
                onClicked: {
                    panelRoot.page.showSorter = false
                    panelRoot.page.showFilter = !panelRoot.page.showFilter
                }
            }

            StrategyComponents.StrategySortButton {
                active: panelRoot.page.showSorter
                onClicked: {
                    panelRoot.page.showFilter = false
                    panelRoot.page.showSorter = !panelRoot.page.showSorter
                }
            }

            Rectangle {
                Layout.preferredWidth: 120
                Layout.preferredHeight: 40
                radius: panelRoot.page.borderRadiusMedium
                color: "#0B1220"
                border.width: 1
                border.color: "#1D6B4F"

                Row {
                    anchors.centerIn: parent
                    spacing: 6

                    Text {
                        text: "+"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: "#A7F3D0"
                    }

                    Text {
                        text: "新建策略"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "#ECFDF5"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panelRoot.page.openStrategyCreation({})
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: panelRoot.page.strategyLibrarySearchText.trim().length > 0
                    ? ("显示 " + panelRoot.page.strategyVisibleModel.count + " / " + (panelRoot.page.strategyViewModel ? panelRoot.page.strategyViewModel.count : 0) + " 个策略")
                    : ("共 " + panelRoot.page.strategyVisibleModel.count + " 个策略")
                font.pixelSize: panelRoot.page.fontSizeNormal
                color: panelRoot.page.textSecondary
            }

            Item { Layout.fillWidth: true }

            StrategyComponents.ViewModeToggle {
                currentMode: "grid"
                onModeChanged: {
                }
            }
        }
    }
}

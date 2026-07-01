// StrategyTypeSelector.qml
// 策略类型选择组件 - 用于策略创建向导步骤1

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../../../utils/StrategyCreationUtils.js" as Utils

Rectangle {
    id: root

    // ============ 属性 ============

    property int selectedStrategyTypeIndex: 0
    readonly property int selectedStrategyBehaviorKind: Utils.StrategyCreationUtils.strategyBehaviorKindFromTypeIndex(selectedStrategyTypeIndex)
    readonly property int compactSelectorColumns: width >= 280 ? 2 : 1
    readonly property int strategyCardHeight: compactSelectorColumns > 1 ? 44 : 50

    ListModel {
        id: strategyTypeListModel
        ListElement { typeIndex: 0 }
        ListElement { typeIndex: 1 }
        ListElement { typeIndex: 2 }
        ListElement { typeIndex: 3 }
        ListElement { typeIndex: 4 }
        ListElement { typeIndex: 5 }
        ListElement { typeIndex: 6 }
        ListElement { typeIndex: 7 }
        ListElement { typeIndex: 8 }
        ListElement { typeIndex: 9 }
    }

    // 信号
    signal strategyTypeIndexChanged(int strategyTypeIndex)

    // ============ 主布局 ============

    color: "transparent"

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // 左侧：策略类型选择
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.25
            Layout.minimumWidth: 180
            spacing: 12

            Text {
                text: Utils.StrategyCreationUtils.tr('strategyCreation.selectStrategyType')
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#f1f5f9"
            }

            // 类型选择列表
            ScrollView {
                id: strategyScrollView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 200
                Layout.preferredHeight: root.compactSelectorColumns > 1 ? 236 : 360
                Layout.maximumHeight: root.compactSelectorColumns > 1 ? 260 : 420
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                GridLayout {
                    id: strategyListColumn
                    width: strategyScrollView.availableWidth
                    columns: root.compactSelectorColumns
                    columnSpacing: 8
                    rowSpacing: 6

                    Component {
                        id: strategyTypeCardDelegate

                        Rectangle {
                            property int strategyTypeIndex: model.typeIndex
                            property bool isSelected: root.selectedStrategyTypeIndex === model.typeIndex

                            Layout.fillWidth: true
                            Layout.preferredHeight: root.strategyCardHeight
                            radius: 6
                            color: isSelected ? "#1e40af" : "#1e293b"
                            border.width: isSelected ? 2 : 1
                            border.color: isSelected ? "#3b82f6" : "#475569"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                Rectangle {
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                    radius: 6
                                    color: isSelected ? "#3b82f6" : "#334155"
                                    border.width: 1
                                    border.color: isSelected ? "#60a5fa" : "#475569"

                                    Text {
                                        anchors.centerIn: parent
                                        text: Utils.StrategyCreationUtils.getStrategyIconFromIndex(strategyTypeIndex)
                                        font.pixelSize: 12
                                        color: isSelected ? "white" : "#cbd5e1"
                                    }
                                }

                                Text {
                                    text: Utils.StrategyCreationUtils.getStrategyTypeNameFromIndex(strategyTypeIndex)
                                    font.pixelSize: 12
                                    font.weight: isSelected ? Font.DemiBold : Font.Medium
                                    color: isSelected ? "white" : "#f1f5f9"
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    visible: isSelected
                                    Layout.preferredWidth: 10
                                    Layout.preferredHeight: 10
                                    radius: 5
                                    color: "#10b981"
                                    border.width: 2
                                    border.color: "white"
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleStrategyType(model.typeIndex)
                            }
                        }
                    }

                    Repeater {
                        model: strategyTypeListModel
                        delegate: strategyTypeCardDelegate
                    }
                }
            }

            // 策略类型描述
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                Layout.minimumHeight: 80
                radius: 8
                color: "#0f172a"
                border.width: 1
                border.color: "#334155"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Text {
                        id: strategyTypeDesc
                        text: Utils.StrategyCreationUtils.getStrategyTypeDescriptionFromIndex(root.selectedStrategyTypeIndex)
                        font.pixelSize: 12
                        color: "#94a3b8"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
        }
    }

    // ============ 功能函数 ============

    function reset() {
        root.selectedStrategyTypeIndex = Utils.StrategyCreationUtils.StrategyTypeIndex.TrendFollowing
    }

    function toggleStrategyType(strategyTypeIndex) {
        root.selectedStrategyTypeIndex = root.selectedStrategyTypeIndex === strategyTypeIndex ? -1 : strategyTypeIndex
        root.strategyTypeIndexChanged(root.selectedStrategyTypeIndex)
    }

    function setSelectedStrategyTypeIndex(strategyTypeIndex, emitSignal) {
        root.selectedStrategyTypeIndex = Utils.StrategyCreationUtils.normalizeStrategyTypeIndex(strategyTypeIndex)
        if (emitSignal === undefined || emitSignal) {
            root.strategyTypeIndexChanged(root.selectedStrategyTypeIndex)
        }
    }

    function isValid() {
        return root.selectedStrategyTypeIndex >= 0
    }
}

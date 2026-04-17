import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    property var stages: []
    property string selectedStageId: "signal"

    signal stageSelected(string stageId)

    function stageRuleCount(stageData) {
        var total = 0
        var groups = Array.isArray(stageData && stageData.groups) ? stageData.groups : []
        for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            total += Array.isArray(groups[groupIndex].rules) ? groups[groupIndex].rules.length : 0
        }
        return total
    }

    radius: 12
    implicitHeight: navigatorLayout.implicitHeight + 20
    color: "#0f172a"
    border.width: 1
    border.color: "#334155"

    ColumnLayout {
        id: navigatorLayout
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        Text {
            text: "阶段导航"
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: "#f8fafc"
        }

        ScrollView {
            id: navigatorScrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: navigatorScrollView.availableWidth
                spacing: 8

                Repeater {
                    model: Array.isArray(root.stages) ? root.stages : []

                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool isSelected: root.selectedStageId === modelData.stageId
                        Layout.fillWidth: true
                        radius: 10
                        color: isSelected ? "#172554" : "#111827"
                        border.width: 1
                        border.color: isSelected ? (modelData.accentColor || "#2563eb") : "#1f2937"
                        implicitHeight: 58

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Rectangle {
                                Layout.preferredWidth: 8
                                Layout.fillHeight: true
                                radius: 4
                                color: modelData.accentColor || "#475569"
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: modelData.title || modelData.stageId
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: isSelected ? "#dbeafe" : "#e2e8f0"
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "规则组 " + (Array.isArray(modelData.groups) ? modelData.groups.length : 0) + " · 已放入 " + root.stageRuleCount(modelData)
                                    font.pixelSize: 10
                                    color: "#94a3b8"
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.stageSelected(modelData.stageId)
                        }
                    }
                }
            }
        }
    }
}
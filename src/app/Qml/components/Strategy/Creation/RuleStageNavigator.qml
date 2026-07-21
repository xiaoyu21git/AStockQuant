import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    property var stages: []
    property string selectedStageId: "signal"

    signal stageSelected(string stageId)
    signal addStageRequested(string stageId)

    function stageRuleCount(stageData) {
        var total = 0
        var groups = Array.isArray(stageData && stageData.groups) ? stageData.groups : []
        for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            total += Array.isArray(groups[groupIndex].rules) ? groups[groupIndex].rules.length : 0
        }
        return total
    }

    readonly property var stageTypes: [
        { stageId: "market", title: "市场", accentColor: "#f59e0b" },
        { stageId: "eligibility", title: "入场过滤", accentColor: "#10b981" },
        { stageId: "signal", title: "入场信号", accentColor: "#3b82f6" },
        { stageId: "rebalance", title: "出场", accentColor: "#ef4444" },
        { stageId: "portfolio", title: "组合", accentColor: "#8b5cf6" }
    ]

    radius: 8; color: "#0f172a"; border.width: 1; border.color: "#334155"

    Popup {
        id: addStagePopup
        y: addBtn.y + addBtn.height + 4
        x: 0
        width: 160; height: Math.min(implicitHeight, 240)
        padding: 4
        background: Rectangle { radius: 8; color: "#1e293b"; border.width: 1; border.color: "#3b82f6" }

        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: root.stageTypes
            delegate: ItemDelegate {
                width: parent.width; height: 32
                highlighted: false
                background: Rectangle { color: "transparent" }
                contentItem: RowLayout {
                    spacing: 6
                    Rectangle {
                        Layout.preferredWidth: 6; Layout.fillHeight: true; radius: 3
                        color: modelData.accentColor
                    }
                    Text {
                        text: modelData.title; font.pixelSize: 12; color: "#e2e8f0"
                        Layout.fillWidth: true; verticalAlignment: Text.AlignVCenter
                    }
                }
                onClicked: {
                    root.addStageRequested(modelData.stageId)
                    addStagePopup.close()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 6; spacing: 4

        Text {
            text: "阶段"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#94a3b8"
        }

        ListView {
            id: stageList
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 4
            model: Array.isArray(root.stages) ? root.stages : []

            delegate: Rectangle {
                required property var modelData
                readonly property bool isSelected: root.selectedStageId === modelData.stageId
                width: ListView.view.width
                height: 36; radius: 6
                color: isSelected ? "#172554" : "#111827"
                border.width: 1
                border.color: isSelected ? (modelData.accentColor || "#2563eb") : "#1f2937"

                RowLayout {
                    anchors.fill: parent; anchors.margins: 6; spacing: 6
                    Rectangle {
                        Layout.preferredWidth: 4; Layout.fillHeight: true; radius: 2
                        color: modelData.accentColor || "#475569"
                    }
                    Text {
                        text: modelData.title || modelData.stageId
                        font.pixelSize: 11; font.weight: Font.Medium
                        color: isSelected ? "#dbeafe" : "#e2e8f0"
                        Layout.fillWidth: true; elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: root.stageRuleCount(modelData) > 0
                        radius: 8; implicitWidth: 22; implicitHeight: 16
                        color: "#1f2937"
                        Text {
                            anchors.centerIn: parent
                            text: root.stageRuleCount(modelData); font.pixelSize: 9; color: "#94a3b8"
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

        // Add button
        Rectangle {
            id: addBtn
            Layout.fillWidth: true; implicitHeight: 28; radius: 6
            color: "#111827"; border.width: 1; border.color: "#1f2937"
            RowLayout {
                anchors.centerIn: parent; spacing: 4
                Text { text: "+"; font.pixelSize: 14; color: "#60a5fa" }
                Text { text: "添加阶段"; font.pixelSize: 11; color: "#64748b" }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: addStagePopup.open()
            }
        }
    }
}
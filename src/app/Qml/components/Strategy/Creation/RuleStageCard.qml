import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var stageData: ({})
    property bool isSelected: false
    property string selectedGroupId: ""
    property var groupIssuesById: ({})

    signal stageSelected(string stageId)
    signal addRuleRequested(string stageId, string groupId)
    signal groupSelected(string stageId, string groupId)
    signal groupEdited(string stageId, string groupId, var patch)
    signal removeRuleRequested(string stageId, string groupId, string instanceId)
    signal moveRuleRequested(string stageId, string groupId, string instanceId, int direction)

    radius: 12
    color: isSelected ? "#0f172a" : "#111827"
    border.width: 1
    border.color: isSelected ? ((stageData && stageData.accentColor) || "#2563eb") : "#1f2937"
    implicitHeight: stageLayout.implicitHeight + 22

    ColumnLayout {
        id: stageLayout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            id: stageHeaderRow
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 10
                Layout.fillHeight: true
                radius: 5
                color: (stageData && stageData.accentColor) || "#475569"
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: (stageData && stageData.title) || (stageData && stageData.stageId) || "未命名阶段"
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: "#f8fafc"
                    wrapMode: Text.WordWrap
                }

                Text {
                    id: stageDescriptionText
                    Layout.fillWidth: true
                    text: (stageData && stageData.description) || ""
                    font.pixelSize: 12
                    color: "#94a3b8"
                    wrapMode: Text.WordWrap
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: Array.isArray(stageData && stageData.groups) ? stageData.groups : []

                delegate: RuleGroupCard {
                    Layout.fillWidth: true
                    stageId: root.stageData.stageId || ""
                    groupData: modelData
                    isSelected: root.selectedGroupId === (modelData.groupId || "")
                    issueLevel: ((root.groupIssuesById || ({}))[(String(root.stageData.stageId || "") + "::" + String(modelData.groupId || ""))] || ({})).level || ""
                    issueMessages: ((root.groupIssuesById || ({}))[(String(root.stageData.stageId || "") + "::" + String(modelData.groupId || ""))] || ({})).messages || []
                    onAddRuleRequested: function(stageId, groupId) {
                        root.addRuleRequested(stageId, groupId)
                    }
                    onGroupSelected: function(stageId, groupId) {
                        root.groupSelected(stageId, groupId)
                    }
                    onGroupEdited: function(stageId, groupId, patch) {
                        root.groupEdited(stageId, groupId, patch)
                    }
                    onRemoveRuleRequested: function(stageId, groupId, instanceId) {
                        root.removeRuleRequested(stageId, groupId, instanceId)
                    }
                    onMoveRuleRequested: function(stageId, groupId, instanceId, direction) {
                        root.moveRuleRequested(stageId, groupId, instanceId, direction)
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: stageHeaderRow.height + stageDescriptionText.height + stageLayout.spacing + 12
        acceptedButtons: Qt.LeftButton
        propagateComposedEvents: true
        onClicked: function(mouse) {
            root.stageSelected((root.stageData && root.stageData.stageId) || "")
        }
    }
}
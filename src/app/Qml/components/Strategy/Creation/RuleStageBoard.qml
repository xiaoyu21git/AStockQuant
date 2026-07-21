import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    property var stages: []
    property var groupIssuesById: ({})
    property string selectedStageId: "signal"
    property string selectedGroupId: ""

    signal stageSelected(string stageId)
    signal addRuleRequested(string stageId, string groupId)
    signal groupSelected(string stageId, string groupId)
    signal groupEdited(string stageId, string groupId, var patch)
    signal removeRuleRequested(string stageId, string groupId, string instanceId)
    signal moveRuleRequested(string stageId, string groupId, string instanceId, int direction)

    readonly property bool showAllStages: width >= 900

    readonly property var displayedStages: {
        var list = Array.isArray(stages) ? stages : []
        for (var i = 0; i < list.length; ++i)
            if (list[i].stageId === selectedStageId) return [list[i]]
        return list.length > 0 ? [list[0]] : []
    }

    radius: 12
    implicitHeight: boardLayout.implicitHeight + 24
    color: "#0f172a"
    border.width: 1
    border.color: "#334155"

    ColumnLayout {
        id: boardLayout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Text {
            text: "阶段编排器"
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: "#f8fafc"
        }

        Text {
            Layout.fillWidth: true
            text: "当前版本先接入阶段卡和规则组外壳，后续再把同阶段多规则组合接到保存和执行链路。"
            font.pixelSize: 12
            color: "#94a3b8"
            wrapMode: Text.WordWrap
        }

        ScrollView {
            id: boardScrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: boardScrollView.availableWidth
                spacing: 10

                Repeater {
                    model: root.displayedStages

                    delegate: RuleStageCard {
                        Layout.fillWidth: true
                        stageData: modelData
                        groupIssuesById: root.groupIssuesById
                        isSelected: root.selectedStageId === modelData.stageId
                        selectedGroupId: root.selectedStageId === modelData.stageId ? root.selectedGroupId : ""
                        onStageSelected: function(stageId) {
                            root.stageSelected(stageId)
                        }
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
    }
}
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQml 2.15

ColumnLayout {
    id: panelRoot

    // 暴露 RuleTemplatePickerDialog 的引用供外部使用
    property alias ruleTemplatePicker: ruleTemplatePicker

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    spacing: root.ruleComposerSpacing

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: root.ruleComposerMinHeight
        spacing: root.ruleComposerSpacing

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                // 标签切换栏
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: Array.isArray(root.ruleComposerStages) ? root.ruleComposerStages : []
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool isActive: root.selectedRuleComposerStageId === modelData.stageId
                            implicitWidth: tabRow.implicitWidth + 28; implicitHeight: 38; radius: 8
                            color: isActive ? "#1e3a5f" : "#111827"
                            border.width: 1; border.color: isActive ? (modelData.accentColor || "#2563eb") : "#1f2937"
                            RowLayout {
                                id: tabRow; anchors.centerIn: parent; spacing: 6
                                Rectangle { width: 8; height: 8; radius: 4; color: modelData.accentColor || "#475569" }
                                Text { text: modelData.title || modelData.stageId; font.pixelSize: 14; font.weight: isActive ? Font.DemiBold : Font.Normal; color: isActive ? "#dbeafe" : "#94a3b8" }
                                Text {
                                    visible: root.countStageRules(modelData) > 0
                                    text: root.countStageRules(modelData); font.pixelSize: 11; color: "#64748b"
                                }
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectRuleComposerStage(modelData.stageId)
                            }
                            Rectangle {
                                anchors.right: parent.right; anchors.top: parent.top
                                anchors.margins: 2
                                width: 16; height: 16; radius: 8
                                color: "#1f2937"
                                visible: isActive
                                Text { anchors.centerIn: parent; text: "×"; font.pixelSize: 11; color: "#f87171" }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.removeRuleComposerStage(modelData.stageId)
                                }
                            }
                        }
                    }

                    Rectangle {
                        implicitWidth: 38; implicitHeight: 38; radius: 8
                        color: "#1a2332"; border.width: 1; border.color: "#1f2937"
                        Text { anchors.centerIn: parent; text: "+"; font.pixelSize: 18; color: "#60a5fa" }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: addStageMenu.open()
                        }
                        Menu {
                            id: addStageMenu; y: parent.height + 2
                            Instantiator {
                                model: root.availableRuleStages
                                MenuItem {
                                    text: modelData.title; height: 28
                                    onTriggered: root.addRuleComposerStage(modelData.stageId)
                                }
                                onObjectAdded: (idx, obj) => addStageMenu.insertItem(idx, obj)
                                onObjectRemoved: (idx, obj) => addStageMenu.removeItem(obj)
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                // 规则看板
                RuleStageBoard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    stages: root.ruleComposerStages
                    groupIssuesById: root.ruleComposerValidation.groupIssues || ({})
                    selectedStageId: root.selectedRuleComposerStageId
                    selectedGroupId: root.selectedRuleComposerGroupId
                    onStageSelected: function(stageId) {
                        root.selectedRuleComposerStageId = stageId
                        root.ensureSelectedRuleComposerGroup()
                    }
                    onAddRuleRequested: function(stageId, groupId) {
                        root.selectedRuleComposerStageId = stageId
                        root.selectedRuleComposerGroupId = groupId
                        ruleTemplatePicker.stageId = stageId
                        ruleTemplatePicker.groupId = groupId
                        ruleTemplatePicker.open()
                    }
                    onGroupSelected: function(stageId, groupId) {
                        root.selectedRuleComposerStageId = stageId
                        root.selectedRuleComposerGroupId = groupId
                    }
                    onGroupEdited: function(stageId, groupId, patch) {
                        root.updateRuleComposerGroup(stageId, groupId, patch)
                    }
                    onRemoveRuleRequested: function(stageId, groupId, instanceId) {
                        root.removeRuleComposerInstance(stageId, groupId, instanceId)
                    }
                    onMoveRuleRequested: function(stageId, groupId, instanceId, direction) {
                        root.moveRuleComposerInstance(stageId, groupId, instanceId, direction)
                    }
                }
            }
        }

        RuleTemplateSuggestionPanel {
            visible: root.useRuleComposerColumns
            Layout.preferredWidth: root.ruleComposerSuggestionWidth
            Layout.minimumWidth: root.ruleComposerSuggestionMinWidth
            Layout.maximumWidth: root.ruleComposerSuggestionMaxWidth
            Layout.fillHeight: true
            panelTitle: "补充模板建议"
            hintMessage: "默认规则包和当前组快捷引入是主入口；这里仅用于补充非默认模板。先选阶段和规则组，再把模板加入当前规则组。"
            showInlinePhaseInputs: !root.useRuleComposerColumns
            phaseLockValue: root.currentSuggestionPhaseLock()
            selectedStrategyTypeIndex: root.selectedStrategyTypeIndex
            strategyProfile: root.strategyProfile
            selectedStageId: root.selectedRuleComposerStageId
            selectedStageTitle: (root.currentSelectedRuleComposerStage() && root.currentSelectedRuleComposerStage().title) || ""
            selectedGroupId: root.selectedRuleComposerGroupId
            selectedGroupTitle: (root.currentSelectedRuleComposerGroup() && root.currentSelectedRuleComposerGroup().title) || ""
            selectedGroupRole: (root.currentSelectedRuleComposerGroup() && root.currentSelectedRuleComposerGroup().role) || ""
            onApplySuggestionRequested: function(suggestion, applyMode) {
                root.bindRuleTemplateSuggestion(suggestion, applyMode)
                root.applyRuleTemplateSuggestionRequested({
                    suggestion: suggestion,
                    applyMode: applyMode
                })
            }
        }
    }

    RuleTemplateSuggestionPanel {
        visible: !root.useRuleComposerColumns
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        panelTitle: "补充模板建议"
        hintMessage: "默认规则包和当前组快捷引入是主入口；这里仅用于补充非默认模板。先选阶段和规则组，再把模板加入当前规则组。"
        showInlinePhaseInputs: true
        phaseLockValue: root.currentSuggestionPhaseLock()
        selectedStrategyTypeIndex: root.selectedStrategyTypeIndex
        strategyProfile: root.strategyProfile
        selectedStageId: root.selectedRuleComposerStageId
        selectedStageTitle: (root.currentSelectedRuleComposerStage() && root.currentSelectedRuleComposerStage().title) || ""
        selectedGroupId: root.selectedRuleComposerGroupId
        selectedGroupTitle: (root.currentSelectedRuleComposerGroup() && root.currentSelectedRuleComposerGroup().title) || ""
        selectedGroupRole: (root.currentSelectedRuleComposerGroup() && root.currentSelectedRuleComposerGroup().role) || ""
        onApplySuggestionRequested: function(suggestion, applyMode) {
            root.bindRuleTemplateSuggestion(suggestion, applyMode)
            root.applyRuleTemplateSuggestionRequested({
                suggestion: suggestion,
                applyMode: applyMode
            })
        }
    }

    // 规则模板浏览弹窗（原在父文件底部，现移入组件内部）
    RuleTemplatePickerDialog {
        id: ruleTemplatePicker
        selectedStrategyTypeIndex: root.selectedStrategyTypeIndex
        strategyProfile: root.strategyProfile

        onRuleAdded: function(templateId) {
            if (!templateId) return
            var all = ruleTemplateSuggestionService.suggestAllTemplates()
            var suggestion = null
            for (var i = 0; i < all.length; ++i) {
                if (String(all[i].templateId || all[i].template_id || "") === templateId) {
                    suggestion = all[i]
                    break
                }
            }
            if (suggestion) {
                root.upsertRuleComposerSuggestion(suggestion)
                root.syncDecoratedParameters()
            }
        }
    }
}

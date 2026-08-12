import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQml 2.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/PureUtils.js" as PureUtils

ColumnLayout {
    id: panelRoot

    // 暴露 RuleTemplatePickerDialog 的引用供外部使用
    property alias ruleTemplatePicker: ruleTemplatePicker

    // ── 数据流入（从父级 StrategyParamConfig 派生）──
    required property int ruleComposerSpacing
    required property int ruleComposerMinHeight
    required property var ruleComposerStages
    required property string selectedRuleComposerStageId
    required property string selectedRuleComposerGroupId
    required property var ruleComposerValidation
    required property bool useRuleComposerColumns
    required property int ruleComposerSuggestionWidth
    required property int ruleComposerSuggestionMinWidth
    required property int ruleComposerSuggestionMaxWidth
    required property int selectedStrategyType
    required property var strategyProfile
    required property var availableRuleStages
    required property var suggestionPhaseLock
    required property string selectedStageTitle
    required property string selectedGroupTitle
    required property string selectedGroupRole

    // ── 操作信号（发往父级）──
    signal stageSelectRequested(string stageId)
    signal stageAddRequested(string stageId)
    signal stageRemoveRequested(string stageId)
    signal stageAndGroupSelectRequested(string stageId, string groupId)
    signal groupEditRequested(string stageId, string groupId, var patch)
    signal ruleInstanceRemoveRequested(string stageId, string groupId, string instanceId)
    signal ruleInstanceMoveRequested(string stageId, string groupId, string instanceId, string direction)
    signal suggestionApplyRequested(var suggestion, string applyMode)
    signal suggestionUpsertRequested(var suggestion)

    // ── 本地工具函数 ──
    function countStageRules(stageData) {
        return PureUtils.countStageRules(stageData)
    }

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    spacing: ruleComposerSpacing

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: ruleComposerMinHeight
        spacing: ruleComposerSpacing

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
                        model: Array.isArray(ruleComposerStages) ? ruleComposerStages : []
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool isActive: selectedRuleComposerStageId === modelData.stageId
                            implicitWidth: tabRow.implicitWidth + 28; implicitHeight: 38; radius: 8
                            color: isActive ? "#1e3a5f" : "#111827"
                            border.width: 1; border.color: isActive ? (modelData.accentColor || "#2563eb") : "#1f2937"
                            RowLayout {
                                id: tabRow; anchors.centerIn: parent; spacing: 6
                                Rectangle { width: 8; height: 8; radius: 4; color: modelData.accentColor || "#475569" }
                                Text { text: modelData.title || modelData.stageId; font.pixelSize: 14; font.weight: isActive ? Font.DemiBold : Font.Normal; color: isActive ? "#dbeafe" : "#94a3b8" }
                                Text {
                                    visible: countStageRules(modelData) > 0
                                    text: countStageRules(modelData); font.pixelSize: 11; color: "#64748b"
                                }
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: stageSelectRequested(modelData.stageId)
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
                                    onClicked: stageRemoveRequested(modelData.stageId)
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
                                model: availableRuleStages
                                MenuItem {
                                    text: modelData.title; height: 28
                                    onTriggered: stageAddRequested(modelData.stageId)
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
                    stages: ruleComposerStages
                    groupIssuesById: ruleComposerValidation.groupIssues || ({})
                    selectedStageId: selectedRuleComposerStageId
                    selectedGroupId: selectedRuleComposerGroupId
                    onStageSelected: function(stageId) {
                        stageSelectRequested(stageId)
                    }
                    onAddRuleRequested: function(stageId, groupId) {
                        stageAndGroupSelectRequested(stageId, groupId)
                        ruleTemplatePicker.stageId = stageId
                        ruleTemplatePicker.groupId = groupId
                        ruleTemplatePicker.open()
                    }
                    onGroupSelected: function(stageId, groupId) {
                        stageAndGroupSelectRequested(stageId, groupId)
                    }
                    onGroupEdited: function(stageId, groupId, patch) {
                        groupEditRequested(stageId, groupId, patch)
                    }
                    onRemoveRuleRequested: function(stageId, groupId, instanceId) {
                        ruleInstanceRemoveRequested(stageId, groupId, instanceId)
                    }
                    onMoveRuleRequested: function(stageId, groupId, instanceId, direction) {
                        ruleInstanceMoveRequested(stageId, groupId, instanceId, direction)
                    }
                }
            }
        }

        RuleTemplateSuggestionPanel {
            visible: useRuleComposerColumns
            Layout.preferredWidth: ruleComposerSuggestionWidth
            Layout.minimumWidth: ruleComposerSuggestionMinWidth
            Layout.maximumWidth: ruleComposerSuggestionMaxWidth
            Layout.fillHeight: true
            panelTitle: "补充模板建议"
            hintMessage: "默认规则包和当前组快捷引入是主入口；这里仅用于补充非默认模板。先选阶段和规则组，再把模板加入当前规则组。"
            showInlinePhaseInputs: !useRuleComposerColumns
            phaseLockValue: suggestionPhaseLock
            selectedStrategyType: selectedStrategyType
            strategyProfile: strategyProfile
            selectedStageId: selectedRuleComposerStageId
            selectedStageTitle: selectedStageTitle
            selectedGroupId: selectedRuleComposerGroupId
            selectedGroupTitle: selectedGroupTitle
            selectedGroupRole: selectedGroupRole
            onApplySuggestionRequested: function(suggestion, applyMode) {
                suggestionApplyRequested(suggestion, applyMode)
            }
        }
    }

    RuleTemplateSuggestionPanel {
        visible: !useRuleComposerColumns
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        panelTitle: "补充模板建议"
        hintMessage: "默认规则包和当前组快捷引入是主入口；这里仅用于补充非默认模板。先选阶段和规则组，再把模板加入当前规则组。"
        showInlinePhaseInputs: true
        phaseLockValue: suggestionPhaseLock
        selectedStrategyType: selectedStrategyType
        strategyProfile: strategyProfile
        selectedStageId: selectedRuleComposerStageId
        selectedStageTitle: selectedStageTitle
        selectedGroupId: selectedRuleComposerGroupId
        selectedGroupTitle: selectedGroupTitle
        selectedGroupRole: selectedGroupRole
        onApplySuggestionRequested: function(suggestion, applyMode) {
            suggestionApplyRequested(suggestion, applyMode)
        }
    }

    // 规则模板浏览弹窗（原在父文件底部，现移入组件内部）
    RuleTemplatePickerDialog {
        id: ruleTemplatePicker
        strategyProfile: strategyProfile

        onRuleAdded: function(templateId) {
            if (!templateId) return
            var all = Bridge.RuleTemplateSuggestionService.suggestAllTemplates()
            var suggestion = null
            for (var i = 0; i < all.length; ++i) {
                if (String(all[i].templateId || all[i].template_id || "") === templateId) {
                    suggestion = all[i]
                    break
                }
            }
            if (suggestion) {
                suggestionUpsertRequested(suggestion)
            }
        }
    }
}

// StrategyParamConfig.qml
// 策略参数配置组件 - 用于策略创建向导步骤2

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/StrategyCreationUtils.js" as Utils
import "../../../utils/RuleTemplatePreviewUtils.js" as PreviewUtils
import "../../FactorWorkbench/Creation/components" as PluginComponents

Rectangle {
    id: root

    function clampWidth(minWidth, preferredWidth, maxWidth) {
        return Math.round(Math.max(minWidth, Math.min(maxWidth, preferredWidth)))
    }
    
    // ============ 属性 ============
    
    property int selectedStrategyTypeIndex: 0
    property Bridge.FactorService factorService: null
    property var strategyParameters: ({})
    property var boundRuleTemplateBindings: ({})
    property var boundRuleTemplateBindingEntries: []
    property bool suppressRuleComposerReset: false
    property bool parametersValid: false
    property bool enableAdvancedOptions: false
    property var strategyProfile: ({})
    property var ruleComposerStages: []
    property var ruleComposerValidation: ({ valid: true, errorCount: 0, warningCount: 0, errors: [], warnings: [], suggestions: [], groupIssues: ({}) })
    property string selectedRuleComposerStageId: "signal"
    property string selectedRuleComposerGroupId: ""
    readonly property var currentRuleComposerGroupQuickImportEntries: root.currentRuleComposerGroupQuickImportEntries()
    readonly property int selectedStrategyBehaviorKind: Utils.StrategyCreationUtils.strategyBehaviorKindFromTypeIndex(root.selectedStrategyTypeIndex)
    readonly property bool ruleComposerConfigValid: (root.ruleComposerValidation.errorCount || 0) === 0
    readonly property bool useNarrowRulePanels: width >= 1180
    readonly property bool useWideParamGrid: width >= 1200
    readonly property int parameterPaneMaxColumns: 3
    readonly property int parameterPaneMinColumnWidth: 320
    readonly property int factorOverlayCardMinWidth: 320
    readonly property int factorOverlayCardMaxWidth: 420
    readonly property real rulePanelWidth: useNarrowRulePanels ? Math.min(width * 0.76, 920) : width
    readonly property bool useRuleComposerColumns: width >= 1560
    readonly property int ruleComposerMinHeight: 560
    readonly property int ruleComposerSpacing: useRuleComposerColumns ? 12 : 8
    readonly property real ruleComposerWidthBudget: Math.max(0, width - 96)
    readonly property int ruleComposerProfileMinWidth: useRuleComposerColumns ? 152 : 132
    readonly property int ruleComposerProfileMaxWidth: useRuleComposerColumns ? 196 : 164
    readonly property int ruleComposerProfileWidth: clampWidth(
        ruleComposerProfileMinWidth,
        ruleComposerWidthBudget * (useRuleComposerColumns ? 0.125 : 0.15),
        ruleComposerProfileMaxWidth)
    readonly property int ruleComposerNavigatorMinWidth: useRuleComposerColumns ? 128 : 116
    readonly property int ruleComposerNavigatorMaxWidth: useRuleComposerColumns ? 160 : 142
    readonly property int ruleComposerNavigatorWidth: clampWidth(
        ruleComposerNavigatorMinWidth,
        ruleComposerWidthBudget * (useRuleComposerColumns ? 0.1 : 0.125),
        ruleComposerNavigatorMaxWidth)
    readonly property int ruleComposerSuggestionMinWidth: useRuleComposerColumns ? 300 : 240
    readonly property int ruleComposerSuggestionMaxWidth: useRuleComposerColumns ? 420 : 320
    readonly property int ruleComposerSuggestionWidth: clampWidth(
        ruleComposerSuggestionMinWidth,
        ruleComposerWidthBudget * (useRuleComposerColumns ? 0.26 : 0.27),
        ruleComposerSuggestionMaxWidth)
    property var factorOverlay: ({ enabled: false, targetPositionCount: 10, minimumCompositeScore: 0, combineMode: "rank_only", selectionScope: "rule_eligible", allocations: [] })
    property var factorSelectorDialog: null
    
    function factorOverlayCardWidth(containerWidth) {
        var widthBudget = Math.max(0, Number(containerWidth) || 0)
        if (widthBudget <= 0) {
            return factorOverlayCardMinWidth
        }
        if (widthBudget >= factorOverlayCardMinWidth * 2 + 12) {
            return clampWidth(
                factorOverlayCardMinWidth,
                (widthBudget - 12) / 2,
                factorOverlayCardMaxWidth)
        }
        return Math.min(widthBudget, factorOverlayCardMaxWidth)
    }

    // 信号
    signal parametersChanged(var newParameters)
    signal validationChanged(bool allValid, var errors)
    signal advancedOptionsChanged(bool enabled)
    signal applyRuleTemplateSuggestionRequested(var suggestion)
    
    // 监听外部enableAdvancedOptions变化
    onEnableAdvancedOptionsChanged: {
        if (advancedParamsSwitch && advancedParamsSwitch.checked !== root.enableAdvancedOptions) {
            advancedParamsSwitch.checked = root.enableAdvancedOptions
        }
    }
    
    // 插件化组件注册表
    PluginComponents.ParamComponents {
        id: paramComponents
    }
    
    // ============ 主布局 ============
    
    color: "transparent"
    
    ScrollView {
        id: paramConfigScrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        
        // 隐藏滚动条
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        
        ColumnLayout {
            width: paramConfigScrollView.availableWidth
            spacing: 12
            anchors.margins: 10
            
            // 参数配置标题
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                
                Text {
                    text: Utils.StrategyCreationUtils.tr('strategyCreation.step2Title')
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: "#f1f5f9"
                }
                
                Text {
                    text: Utils.StrategyCreationUtils.tr('strategyCreation.step2Description')
                    font.pixelSize: 13
                    color: "#94a3b8"
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: [
                            Utils.StrategyCreationUtils.tr('strategyCreation.commonParameters'),
                            Utils.StrategyCreationUtils.tr('strategyCreation.personalizedParameters')
                        ]

                        delegate: Rectangle {
                            radius: 10
                            color: "#172554"
                            border.width: 1
                            border.color: "#2563eb"
                            implicitHeight: 28
                            implicitWidth: tagLabel.implicitWidth + 18

                            Text {
                                id: tagLabel
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: "#dbeafe"
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }
            
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 14

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    radius: 10
                    color: "#0f172a"
                    border.width: 1
                    border.color: "#334155"
                    implicitHeight: parameterPanelLayout.implicitHeight + 24

                    ColumnLayout {
                        id: parameterPanelLayout
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterConfigPanel')
                            font.pixelSize: 16
                            font.weight: Font.Medium
                            color: "#f1f5f9"
                        }

                        PluginComponents.DynamicParamGenerator {
                            id: dynamicGenerator
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.max(260, implicitHeight)
                            minColumnWidth: root.parameterPaneMinColumnWidth
                            maxColumns: root.parameterPaneMaxColumns
                            showGroups: false

                            paramRegistry: paramComponents

                            onParamsChanged: function(newValues) {
                                var mergedValues = root.decorateParameters(newValues)
                                root.strategyParameters = mergedValues
                                root.parametersChanged(mergedValues)
                            }

                            onValidationChanged: function(allValid, errors) {
                                root.parametersValid = allValid
                                root.emitValidationState(errors)
                            }

                            Component.onCompleted: {
                                loadParamConfigs()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Text {
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.configuredParameters') + ": " +
                                      (dynamicGenerator ? dynamicGenerator.configsList.length : 0)
                                font.pixelSize: 12
                                color: "#94a3b8"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: root.parametersValid ?
                                      Utils.StrategyCreationUtils.tr('strategyCreation.parameterValidationPassed') :
                                      Utils.StrategyCreationUtils.tr('strategyCreation.parameterValidationRequired')
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: root.parametersValid ? "#10b981" : "#ef4444"
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: "#0b1220"
                    border.width: 1
                    border.color: "#1e3a8a"
                    implicitHeight: usageColumn.implicitHeight + 18

                    ColumnLayout {
                        id: usageColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5

                        Text {
                            text: "使用方式"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: "#bfdbfe"
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "1. 先在左侧点阶段。2. 在中间选规则组并按需修改标题、角色、组合方式。3. 去右侧输入术语，点“加入当前规则组”把规则放进去。"
                            font.pixelSize: 11
                            color: "#cbd5e1"
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "当前焦点: "
                                  + (((root.currentSelectedRuleComposerStage() && root.currentSelectedRuleComposerStage().title) || "未选择阶段"))
                                  + " / "
                                  + (((root.currentSelectedRuleComposerGroup() && root.currentSelectedRuleComposerGroup().title) || "未选择规则组"))
                                  + "。右侧建议会优先按当前规则组角色过滤。"
                            font.pixelSize: 11
                            color: "#7dd3fc"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: "#0b1220"
                    border.width: 1
                    border.color: root.factorOverlay.enabled ? "#0ea5e9" : "#334155"
                    implicitHeight: factorOverlayColumn.implicitHeight + 20

                    ColumnLayout {
                        id: factorOverlayColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Text {
                                text: "因子排序层"
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: "#f1f5f9"
                            }

                            Rectangle {
                                radius: 9
                                color: "#0f172a"
                                border.width: 1
                                border.color: "#334155"
                                implicitWidth: modeChipText.implicitWidth + 14
                                implicitHeight: 22

                                Text {
                                    id: modeChipText
                                    anchors.centerIn: parent
                                    text: "规则先筛选，因子后排序"
                                    font.pixelSize: 10
                                    color: "#93c5fd"
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                id: factorOverlaySwitch
                                checked: !!root.factorOverlay.enabled
                                onCheckedChanged: {
                                    root.factorOverlay.enabled = checked
                                    root.factorOverlay = root.normalizeFactorOverlay(root.factorOverlay)
                                    root.syncDecoratedParameters()
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.factorOverlay.enabled
                                  ? "规则模板只负责放行/否决，因子层只在同一交易日的合格候选之间做排序和持仓数量裁剪。"
                                  : "未启用因子排序层时，当前策略仅按规则模板和基础参数运行。"
                            font.pixelSize: 11
                            color: root.factorOverlay.enabled ? "#bae6fd" : "#94a3b8"
                            wrapMode: Text.WordWrap
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: root.factorOverlay.enabled

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Button {
                                    text: "选择因子"
                                    onClicked: root.openFactorSelector()
                                }

                                Button {
                                    text: "等权重"
                                    enabled: (root.factorOverlay.allocations || []).length > 0
                                    onClicked: root.rebalanceFactorOverlayWeights()
                                }

                                Button {
                                    text: "清空"
                                    enabled: (root.factorOverlay.allocations || []).length > 0
                                    onClicked: root.clearFactorOverlayAllocations()
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "已选 " + ((root.factorOverlay.allocations || []).length) + " 个因子"
                                    font.pixelSize: 11
                                    color: "#cbd5e1"
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 1
                                columnSpacing: 12
                                rowSpacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: "最低综合分"
                                        font.pixelSize: 11
                                        color: "#cbd5e1"
                                    }

                                    TextField {
                                        id: factorMinimumScoreField
                                        Layout.fillWidth: true
                                        text: String(root.factorOverlay.minimumCompositeScore || 0)
                                        placeholderText: "默认 0"
                                        onEditingFinished: {
                                            var parsed = Number(text)
                                            root.factorOverlay.minimumCompositeScore = isNaN(parsed) ? 0 : parsed
                                            text = String(root.factorOverlay.minimumCompositeScore)
                                            root.syncDecoratedParameters()
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                visible: (root.factorOverlay.allocations || []).length > 0

                                Text {
                                    Layout.fillWidth: true
                                    text: "已选因子"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: "#f8fafc"
                                }

                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: factorAllocationFlow.implicitHeight

                                    Flow {
                                        id: factorAllocationFlow
                                        width: parent.width
                                        spacing: 10

                                        Repeater {
                                            model: root.factorOverlay.allocations || []

                                            delegate: Rectangle {
                                                required property int index
                                                required property var modelData

                                                width: root.factorOverlayCardWidth(factorAllocationFlow.width)
                                                radius: 12
                                                color: "#0b1220"
                                                border.width: 1
                                                border.color: "#1f3b5b"
                                                implicitHeight: factorAllocationColumn.implicitHeight + 16

                                                ColumnLayout {
                                                    id: factorAllocationColumn
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    spacing: 6

                                                    RowLayout {
                                                        width: parent.width
                                                        spacing: 8

                                                        Rectangle {
                                                            radius: 9
                                                            color: "#0ea5e9"
                                                            border.width: 1
                                                            border.color: "#38bdf8"
                                                            implicitWidth: 42
                                                            implicitHeight: 18

                                                            Text {
                                                                anchors.centerIn: parent
                                                                text: "已选"
                                                                font.pixelSize: 9
                                                                color: "white"
                                                            }
                                                        }

                                                        Item { Layout.fillWidth: true }

                                                        Button {
                                                            text: "移除"
                                                            onClicked: root.removeFactorOverlayAllocation(index)
                                                        }
                                                    }

                                                    Text {
                                                        width: parent.width
                                                        text: modelData.display_name || modelData.factor_id || "未命名因子"
                                                        font.pixelSize: 12
                                                        font.weight: Font.Medium
                                                        color: "#e2e8f0"
                                                        wrapMode: Text.WordWrap
                                                    }

                                                    Text {
                                                        width: parent.width
                                                        text: modelData.factor_id || ""
                                                        font.pixelSize: 9
                                                        color: "#94a3b8"
                                                        wrapMode: Text.WrapAnywhere
                                                    }

                                                    RowLayout {
                                                        width: parent.width
                                                        spacing: 8

                                                        Rectangle {
                                                            Layout.preferredWidth: 96
                                                            implicitHeight: 28
                                                            radius: 8
                                                            color: "#111827"
                                                            border.width: 1
                                                            border.color: "#334155"

                                                            TextField {
                                                                anchors.fill: parent
                                                                anchors.leftMargin: 6
                                                                anchors.rightMargin: 6
                                                                text: String(modelData.weight_percent !== undefined ? modelData.weight_percent : 0)
                                                                placeholderText: "权重%"
                                                                horizontalAlignment: Text.AlignRight
                                                                verticalAlignment: Text.AlignVCenter
                                                                color: "#e2e8f0"
                                                                font.pixelSize: 10
                                                                background: null
                                                                onEditingFinished: root.updateFactorOverlayWeight(index, text)
                                                            }
                                                        }

                                                        Flow {
                                                            Layout.fillWidth: true
                                                            spacing: 4

                                                            Repeater {
                                                                model: [
                                                                    { label: "排序因子", fg: "#93c5fd", bg: "#0f172a" },
                                                                    { label: "权重 " + String(modelData.weight_percent !== undefined ? modelData.weight_percent : 0) + "%", fg: "#fde68a", bg: "#2a2110" }
                                                                ]

                                                                delegate: Rectangle {
                                                                    required property var modelData
                                                                    radius: 9
                                                                    color: modelData.bg
                                                                    border.width: 1
                                                                    border.color: Qt.darker(modelData.bg, 1.12)
                                                                    implicitWidth: chipText.implicitWidth + 10
                                                                    implicitHeight: chipText.implicitHeight + 6

                                                                    Text {
                                                                        id: chipText
                                                                        anchors.centerIn: parent
                                                                        text: modelData.label
                                                                        font.pixelSize: 8
                                                                        color: modelData.fg
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: root.factorOverlayErrors().length > 0
                                text: root.factorOverlayErrors().join("；")
                                font.pixelSize: 11
                                color: "#fca5a5"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                ColumnLayout {
                    id: ruleComposerRow
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: root.ruleComposerSpacing

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 10
                        color: "#0b1220"
                        border.width: 1
                        border.color: "#1d4ed8"
                        implicitHeight: defaultRulePackColumn.implicitHeight + 20

                        ColumnLayout {
                            id: defaultRulePackColumn
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    Text {
                                        text: "默认规则包入口"
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: "#dbeafe"
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "先加载当前策略的默认规则包，再按当前阶段和规则组做局部增删改。右侧建议栏现在只作为补充入口。"
                                        font.pixelSize: 11
                                        color: "#bfdbfe"
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                Button {
                                    text: "恢复整包默认项"
                                    onClicked: root.restoreDefaultRuleComposerPack()
                                }

                                Button {
                                    text: "恢复当前阶段"
                                    enabled: !!root.currentSelectedRuleComposerStage()
                                    onClicked: root.restoreSelectedRuleComposerStageDefaults()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Rectangle {
                                    radius: 8
                                    color: "#111827"
                                    border.width: 1
                                    border.color: "#334155"
                                    implicitWidth: currentStageChipText.implicitWidth + 16
                                    implicitHeight: currentStageChipText.implicitHeight + 10

                                    Text {
                                        id: currentStageChipText
                                        anchors.centerIn: parent
                                        text: ((root.currentSelectedRuleComposerStage() && root.currentSelectedRuleComposerStage().title) || "未选择阶段")
                                        font.pixelSize: 11
                                        color: "#e2e8f0"
                                    }
                                }

                                Rectangle {
                                    radius: 8
                                    color: "#111827"
                                    border.width: 1
                                    border.color: "#334155"
                                    implicitWidth: currentGroupChipText.implicitWidth + 16
                                    implicitHeight: currentGroupChipText.implicitHeight + 10

                                    Text {
                                        id: currentGroupChipText
                                        anchors.centerIn: parent
                                        text: ((root.currentSelectedRuleComposerGroup() && root.currentSelectedRuleComposerGroup().title) || "未选择规则组")
                                        font.pixelSize: 11
                                        color: "#e2e8f0"
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "当前阶段已放入 " + root.selectedRuleComposerStageRuleCount()
                                          + " 条规则，可快捷引入 " + root.currentRuleComposerGroupQuickImportEntries.length + " 个默认项。"
                                    font.pixelSize: 11
                                    color: "#93c5fd"
                                    wrapMode: Text.WordWrap
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: "当前组快捷引入"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: "#f8fafc"
                                }

                                Flow {
                                    width: parent.width
                                    spacing: 8
                                    visible: root.currentRuleComposerGroupQuickImportEntries.length > 0

                                    Repeater {
                                        model: root.currentRuleComposerGroupQuickImportEntries

                                        delegate: Button {
                                            required property var modelData
                                            text: modelData.termDisplayName || modelData.templateDisplayName || modelData.templateId || "未命名默认项"
                                            onClicked: root.applyDefaultRulePackEntryToCurrentGroup(modelData)
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: root.currentRuleComposerGroupQuickImportEntries.length === 0
                                    text: "当前组没有剩余的默认快捷项。可以先恢复当前阶段默认规则，或者再用右侧建议栏补充非默认模板。"
                                    font.pixelSize: 11
                                    color: "#94a3b8"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumHeight: root.ruleComposerMinHeight
                        Layout.preferredHeight: root.ruleComposerMinHeight
                        spacing: root.ruleComposerSpacing

                        StrategyProfilePanel {
                            Layout.preferredWidth: root.ruleComposerProfileWidth
                            Layout.minimumWidth: root.ruleComposerProfileMinWidth
                            Layout.maximumWidth: root.ruleComposerProfileMaxWidth
                            Layout.fillHeight: true
                            selectedStrategyTypeIndex: root.selectedStrategyTypeIndex
                            strategyProfile: root.strategyProfile
                            onProfileEdited: function(profile) {
                                root.strategyProfile = profile
                                root.rebuildRuleComposerState(false)
                                root.syncDecoratedParameters()
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 12

                            RuleComposerSummaryBar {
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                stages: root.ruleComposerStages
                                strategyProfile: root.strategyProfile
                                validationSummary: root.ruleComposerValidation
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumHeight: root.ruleComposerMinHeight - 108
                                Layout.preferredHeight: root.ruleComposerMinHeight - 108
                                spacing: root.ruleComposerSpacing

                                RuleStageNavigator {
                                    Layout.preferredWidth: root.ruleComposerNavigatorWidth
                                    Layout.minimumWidth: root.ruleComposerNavigatorMinWidth
                                    Layout.maximumWidth: root.ruleComposerNavigatorMaxWidth
                                    Layout.fillHeight: true
                                    stages: root.ruleComposerStages
                                    selectedStageId: root.selectedRuleComposerStageId
                                    onStageSelected: function(stageId) {
                                        root.selectedRuleComposerStageId = stageId
                                        root.ensureSelectedRuleComposerGroup()
                                    }
                                }

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
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.minimumHeight: root.enableAdvancedOptions ? 184 : 56
                    radius: 10
                    color: "#0f172a"
                    border.width: 1
                    border.color: "#334155"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10
                        
                        // 标题和切换
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            
                            Text {
                                text: "优化与脚本"
                                font.pixelSize: 16
                                font.weight: Font.Medium
                                color: "#f1f5f9"
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Switch {
                                id: advancedParamsSwitch
                                checked: root.enableAdvancedOptions
                                onCheckedChanged: {
                                    root.enableAdvancedOptions = checked
                                    root.advancedOptionsChanged(checked)
                                }
                                
                                indicator: Rectangle {
                                    implicitWidth: 36
                                    implicitHeight: 20
                                    radius: 10
                                    color: parent.checked ? "#3b82f6" : "#334155"
                                    border.width: 1
                                    border.color: parent.checked ? "#3b82f6" : "#475569"
                                    
                                    Rectangle {
                                        x: parent.checked ? parent.width - width - 2 : 2
                                        y: 2
                                        width: 16
                                        height: 16
                                        radius: 8
                                        color: "white"
                                        Behavior on x {
                                            NumberAnimation { duration: 200 }
                                        }
                                    }
                                }
                            }
                        }
                        
                        // 高级选项内容
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: root.enableAdvancedOptions
                            
                            GridLayout {
                                Layout.fillWidth: true
                                columns: root.useWideParamGrid ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 10
                                
                                // 参数优化范围
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    
                                    Text {
                                        text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterOptimizationRange')
                                        font.pixelSize: 12
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        id: parameterOptimizationRangeCombo
                                        Layout.fillWidth: true
                                        model: Utils.StrategyCreationUtils.tr('strategyCreation.parameterOptimizationRangeOptions')
                                        currentIndex: 1
                                        
                                        background: Rectangle {
                                            implicitHeight: 36
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: parent.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 12
                                            padding: 8
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                                
                                // 参数敏感性分析
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    
                                    Text {
                                        text: Utils.StrategyCreationUtils.tr('strategyCreation.sensitivityAnalysis')
                                        font.pixelSize: 12
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        id: sensitivityAnalysisCombo
                                        Layout.fillWidth: true
                                        model: Utils.StrategyCreationUtils.tr('strategyCreation.sensitivityAnalysisOptions')
                                        currentIndex: 1
                                        
                                        background: Rectangle {
                                            implicitHeight: 36
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: parent.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 12
                                            padding: 8
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                                
                                // 参数约束
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    
                                    Text {
                                        text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterConstraints')
                                        font.pixelSize: 12
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        id: parameterConstraintsCombo
                                        Layout.fillWidth: true
                                        model: Utils.StrategyCreationUtils.tr('strategyCreation.parameterConstraintOptions')
                                        currentIndex: 0
                                        
                                        background: Rectangle {
                                            implicitHeight: 36
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: parent.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 12
                                            padding: 8
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                                
                                // 参数初始化方式
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    
                                    Text {
                                        text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterInitializationMethod')
                                        font.pixelSize: 12
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        id: parameterInitializationMethodCombo
                                        Layout.fillWidth: true
                                        model: Utils.StrategyCreationUtils.tr('strategyCreation.parameterInitializationMethods')
                                        currentIndex: 0
                                        
                                        background: Rectangle {
                                            implicitHeight: 36
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: parent.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 12
                                            padding: 8
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                            }
                            
                            // 自定义参数脚本
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.customParameterScript')
                                    font.pixelSize: 12
                                    color: "#cbd5e1"
                                }
                                
                                TextArea {
                                    id: customParameterScriptTextArea
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 70
                                    placeholderText: Utils.StrategyCreationUtils.tr('strategyCreation.customParameterScriptPlaceholder')
                                    wrapMode: Text.WordWrap
                                    
                                    background: Rectangle {
                                        radius: 6
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#334155"
                                    }
                                    
                                    color: "#f1f5f9"
                                    font.pixelSize: 12
                                    padding: 10
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: !root.useNarrowRulePanels
                    Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
                    Layout.preferredWidth: root.rulePanelWidth
                    Layout.maximumWidth: root.rulePanelWidth
                    visible: root.previewRuleComposerStages().length > 0
                    radius: 10
                    color: "#0f172a"
                    border.width: 1
                    border.color: "#334155"
                    implicitHeight: selectedTemplateLayout.implicitHeight + 24

                    ColumnLayout {
                        id: selectedTemplateLayout
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Text {
                            text: "已绑定规则模板"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "#f1f5f9"
                        }

                        Text {
                            text: "这里直接按阶段、规则组、规则实例展示当前真实编排结果，和最终保存到策略里的结构保持一致。"
                            font.pixelSize: 11
                            color: "#93c5fd"
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Text {
                            text: "共 " + root.previewRuleComposerStages().length + " 个阶段 / "
                                  + root.previewRuleComposerGroupCount() + " 个规则组 / "
                                  + root.previewRuleComposerRuleCount() + " 条规则"
                            font.pixelSize: 11
                            color: "#cbd5e1"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: !root.hasAnyMarketRuleComposerRules()
                                      ? "当前市场环境阶段已清空，可手动添加规则或恢复系统默认组合。"
                                      : (root.hasCustomizedMarketRuleComposerStage()
                                      ? "恢复后只重置市场环境阶段，不影响标的准入、入场确认和退出规则。"
                                      : "当前市场环境阶段已经是系统默认组合，可直接继续补充其它阶段规则。")
                                font.pixelSize: 11
                                color: "#94a3b8"
                                wrapMode: Text.WordWrap
                            }

                            Button {
                                text: "清空市场规则"
                                enabled: root.hasAnyMarketRuleComposerRules()
                                onClicked: root.clearMarketRuleComposerStage()
                            }

                            Button {
                                text: "恢复默认市场规则"
                                enabled: root.hasCustomizedMarketRuleComposerStage()
                                onClicked: root.restoreDefaultMarketRuleComposerStage()
                            }
                        }

                        Repeater {
                            model: root.previewRuleComposerStages()

                            delegate: Rectangle {
                                id: stagePreviewCard
                                required property var modelData
                                Layout.fillWidth: true
                                radius: 8
                                color: "#111827"
                                border.width: 1
                                border.color: modelData.accentColor || "#334155"
                                implicitHeight: stageBindingColumn.implicitHeight + 16

                                ColumnLayout {
                                    id: stageBindingColumn
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Rectangle {
                                            radius: 10
                                            color: "#1e293b"
                                            border.width: 1
                                            border.color: "#475569"
                                            implicitWidth: phaseText.implicitWidth + 12
                                            implicitHeight: 22

                                            Text {
                                                id: phaseText
                                                anchors.centerIn: parent
                                                text: PreviewUtils.phaseDisplayName(modelData.stageId, "short")
                                                font.pixelSize: 11
                                                color: "#cbd5e1"
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: (modelData.title || modelData.stageId || "阶段")
                                                   + " · "
                                                   + root.populatedRuleComposerGroups(modelData).length + " 个规则组 / "
                                                   + root.previewRuleComposerRuleCountForStage(modelData) + " 条规则"
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            color: "#e2e8f0"
                                            wrapMode: Text.WordWrap
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: !!modelData.description
                                        text: modelData.description || ""
                                        font.pixelSize: 11
                                        color: "#cbd5e1"
                                        wrapMode: Text.WordWrap
                                    }

                                    Repeater {
                                        model: root.populatedRuleComposerGroups(modelData)

                                        delegate: Rectangle {
                                            id: groupPreviewCard
                                            required property var modelData
                                            property var stageData: stagePreviewCard.modelData
                                            Layout.fillWidth: true
                                            radius: 8
                                            color: "#0b1220"
                                            border.width: 1
                                            border.color: "#1f2937"
                                            implicitHeight: groupBindingColumn.implicitHeight + 14

                                            ColumnLayout {
                                                id: groupBindingColumn
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 8

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 8

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: (modelData.title || modelData.groupId || "规则组")
                                                              + " · " + root.roleDisplayName(modelData.role)
                                                              + " / " + root.operatorDisplayName(modelData.operator)
                                                              + " / " + ((Array.isArray(modelData.rules) ? modelData.rules.length : 0) + " 条规则")
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        color: "#f8fafc"
                                                        wrapMode: Text.WordWrap
                                                    }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    visible: !!modelData.description
                                                    text: modelData.description || ""
                                                    font.pixelSize: 11
                                                    color: "#94a3b8"
                                                    wrapMode: Text.WordWrap
                                                }

                                                Repeater {
                                                    model: Array.isArray(modelData.rules) ? modelData.rules : []

                                                    delegate: Rectangle {
                                                        id: rulePreviewCard
                                                        required property var modelData
                                                        property var stageData: groupPreviewCard.stageData
                                                        property var groupData: groupPreviewCard.modelData
                                                        property var bindingData: root.ruleComposerPreviewBinding(stageData, groupData, modelData)
                                                        property var insight: PreviewUtils.getTemplateInsight(bindingData)
                                                        property bool secondaryCollapsible: PreviewUtils.normalizePhaseKey(bindingData.phase) === "market"
                                                        property bool secondaryExpanded: !secondaryCollapsible
                                                        Layout.fillWidth: true
                                                        radius: 8
                                                        color: "#111827"
                                                        border.width: 1
                                                        border.color: "#334155"
                                                        implicitHeight: ruleBindingColumn.implicitHeight + 16

                                                        ColumnLayout {
                                                            id: ruleBindingColumn
                                                            anchors.fill: parent
                                                            anchors.margins: 8
                                                            spacing: 8

                                                            RowLayout {
                                                                Layout.fillWidth: true
                                                                spacing: 8

                                                                Rectangle {
                                                                    radius: 10
                                                                    color: "#1e293b"
                                                                    border.width: 1
                                                                    border.color: "#475569"
                                                                    implicitWidth: rulePhaseText.implicitWidth + 12
                                                                    implicitHeight: 22

                                                                    Text {
                                                                        id: rulePhaseText
                                                                        anchors.centerIn: parent
                                                                        text: PreviewUtils.phaseDisplayName(bindingData.phase, "short")
                                                                        font.pixelSize: 11
                                                                        color: "#cbd5e1"
                                                                    }
                                                                }

                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    text: bindingData.template_display_name || bindingData.template_id || "未命名模板"
                                                                    font.pixelSize: 12
                                                                    font.weight: Font.Medium
                                                                    color: "#e2e8f0"
                                                                    wrapMode: Text.WordWrap
                                                                }

                                                                Rectangle {
                                                                    visible: !!bindingData.default_injected
                                                                    radius: 9
                                                                    color: "#3f2d16"
                                                                    border.width: 1
                                                                    border.color: "#f59e0b"
                                                                    implicitWidth: presetBindingChipText.implicitWidth + 14
                                                                    implicitHeight: 22

                                                                    Text {
                                                                        id: presetBindingChipText
                                                                        anchors.centerIn: parent
                                                                        text: "系统预置"
                                                                        font.pixelSize: 10
                                                                        font.weight: Font.Medium
                                                                        color: "#fde68a"
                                                                    }
                                                                }

                                                                Button {
                                                                    text: "移除"
                                                                    onClicked: root.removeRuleComposerInstance(stageData.stageId, groupData.groupId, modelData.instanceId)
                                                                }
                                                            }

                                                            Text {
                                                                Layout.fillWidth: true
                                                                visible: !!(bindingData.summary || (insight && insight.summary))
                                                                text: bindingData.summary || (insight && insight.summary) || ""
                                                                font.pixelSize: 11
                                                                color: "#cbd5e1"
                                                                wrapMode: Text.WordWrap
                                                            }

                                                            Text {
                                                                Layout.fillWidth: true
                                                                visible: !!(bindingData.term_display_name || bindingData.term_id)
                                                                text: "绑定术语: " + (bindingData.term_display_name || bindingData.term_id || "")
                                                                font.pixelSize: 11
                                                                color: "#7dd3fc"
                                                                wrapMode: Text.WordWrap
                                                            }

                                                            RuleTemplateStructureView {
                                                                Layout.fillWidth: true
                                                                bindingData: rulePreviewCard.bindingData
                                                                compact: false
                                                                showTemplateHeader: false
                                                            }

                                                            Rectangle {
                                                                Layout.fillWidth: true
                                                                visible: insight !== null
                                                                radius: 8
                                                                color: "#0b1220"
                                                                border.width: 1
                                                                border.color: "#334155"
                                                                implicitHeight: boundMarketInsightColumn.implicitHeight + 18

                                                                ColumnLayout {
                                                                    id: boundMarketInsightColumn
                                                                    anchors.fill: parent
                                                                    anchors.margins: 10
                                                                    spacing: 8

                                                                    Text {
                                                                        Layout.fillWidth: true
                                                                        text: PreviewUtils.insightSectionTitle(bindingData.phase, true)
                                                                        font.pixelSize: 12
                                                                        font.weight: Font.DemiBold
                                                                        color: "#f8fafc"
                                                                    }

                                                                    Rectangle {
                                                                        Layout.fillWidth: true
                                                                        radius: 6
                                                                        color: "#1f2937"
                                                                        border.width: 1
                                                                        border.color: "#7c2d12"
                                                                        implicitHeight: boundFreezeColumn.implicitHeight + 16

                                                                        ColumnLayout {
                                                                            id: boundFreezeColumn
                                                                            anchors.fill: parent
                                                                            anchors.margins: 8
                                                                            spacing: 4

                                                                            Text {
                                                                                Layout.fillWidth: true
                                                                                text: PreviewUtils.insightPrimaryTitle(insight)
                                                                                font.pixelSize: 11
                                                                                font.weight: Font.Medium
                                                                                color: "#fdba74"
                                                                            }

                                                                            Repeater {
                                                                                model: PreviewUtils.insightPrimaryItems(insight)

                                                                                delegate: Text {
                                                                                    Layout.fillWidth: true
                                                                                    text: (index + 1) + ". " + modelData
                                                                                    font.pixelSize: 11
                                                                                    color: "#e5e7eb"
                                                                                    wrapMode: Text.WordWrap
                                                                                }
                                                                            }
                                                                        }
                                                                    }

                                                                    Rectangle {
                                                                        Layout.fillWidth: true
                                                                        radius: 6
                                                                        color: "#102a43"
                                                                        border.width: 1
                                                                        border.color: "#0369a1"
                                                                        implicitHeight: boundRepairColumn.implicitHeight + 16

                                                                        ColumnLayout {
                                                                            id: boundRepairColumn
                                                                            anchors.fill: parent
                                                                            anchors.margins: 8
                                                                            spacing: 6

                                                                            Item {
                                                                                Layout.fillWidth: true
                                                                                implicitHeight: boundRepairHeader.implicitHeight

                                                                                RowLayout {
                                                                                    id: boundRepairHeader
                                                                                    anchors.fill: parent
                                                                                    spacing: 8

                                                                                    Text {
                                                                                        Layout.fillWidth: true
                                                                                        text: PreviewUtils.insightSecondaryTitle(insight)
                                                                                        font.pixelSize: 11
                                                                                        font.weight: Font.Medium
                                                                                        color: "#7dd3fc"
                                                                                    }

                                                                                    Text {
                                                                                        visible: secondaryCollapsible
                                                                                        text: secondaryExpanded ? "收起" : "展开"
                                                                                        font.pixelSize: 10
                                                                                        font.weight: Font.Medium
                                                                                        color: "#bae6fd"
                                                                                    }
                                                                                }

                                                                                MouseArea {
                                                                                    anchors.fill: parent
                                                                                    enabled: secondaryCollapsible
                                                                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                                                    onClicked: secondaryExpanded = !secondaryExpanded
                                                                                }
                                                                            }

                                                                            ColumnLayout {
                                                                                Layout.fillWidth: true
                                                                                visible: !secondaryCollapsible || secondaryExpanded
                                                                                spacing: 4

                                                                                Repeater {
                                                                                    model: PreviewUtils.insightSecondaryItems(insight)

                                                                                    delegate: Text {
                                                                                        Layout.fillWidth: true
                                                                                        text: (index + 1) + ". " + modelData
                                                                                        font.pixelSize: 11
                                                                                        color: "#e0f2fe"
                                                                                        wrapMode: Text.WordWrap
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 功能函数 ============
    
    // 加载参数配置
    function loadParamConfigs() {
        var paramConfigs = Utils.StrategyCreationUtils.buildParamConfigs(root.selectedStrategyTypeIndex)
        if (dynamicGenerator) {
            dynamicGenerator.reloadConfigs(paramConfigs, [])
        }
    }

    function defaultFactorOverlay() {
        return {
            enabled: false,
            targetPositionCount: 10,
            minimumCompositeScore: 0,
            combineMode: "rank_only",
            selectionScope: "rule_eligible",
            allocations: []
        }
    }

    function importedFactorContextPayload(sourceParameters) {
        var source = sourceParameters && typeof sourceParameters === "object" ? sourceParameters : ({})
        var payload = ({})
        var factorImportContext = normalizeStructuredValue(source.factorImportContext) || ({})

        if (Object.keys(factorImportContext).length > 0) {
            payload.factorImportContext = factorImportContext
        }

        return payload
    }

    function mergeImportedFactorContext(targetParameters, sourceParameters) {
        var target = targetParameters && typeof targetParameters === "object" ? targetParameters : ({})
        var payload = importedFactorContextPayload(sourceParameters)

        if (payload.factorImportContext && Object.keys(payload.factorImportContext).length > 0) {
            target.factorImportContext = payload.factorImportContext
        }

        return target
    }

    function cloneValue(value) {
        return JSON.parse(JSON.stringify(value))
    }

    function firstOverlayValue(source, keys, fallbackValue) {
        var container = source && typeof source === "object" ? source : ({})
        var aliasKeys = Array.isArray(keys) ? keys : []
        for (var index = 0; index < aliasKeys.length; ++index) {
            var key = aliasKeys[index]
            if (container[key] !== undefined && container[key] !== null && String(container[key]).trim() !== "") {
                return container[key]
            }
        }
        return fallbackValue
    }

    function normalizedOverlayAllocation(rawAllocation) {
        var item = rawAllocation && typeof rawAllocation === "object" ? rawAllocation : ({})
        var factorId = String(firstOverlayValue(item, ["factor_id", "factorId"], "")).trim()
        if (!factorId) {
            return null
        }

        return {
            factor_id: factorId,
            display_name: String(firstOverlayValue(item, ["display_name", "displayName", "factor_name", "factorName", "name"], root.factorDisplayName(factorId) || factorId)),
            weight_percent: Number(firstOverlayValue(item, ["weight_percent", "weightPercent", "weight"], 0)) || 0
        }
    }

    function normalizeFactorOverlay(rawOverlay, sourceParameters) {
        var overlay = normalizeStructuredValue(rawOverlay)
        overlay = overlay && typeof overlay === "object" ? cloneValue(overlay) : defaultFactorOverlay()
        var normalized = defaultFactorOverlay()
        normalized.targetPositionCount = Math.max(1, Number(firstOverlayValue(overlay, ["targetPositionCount", "target_position_count"], normalized.targetPositionCount)) || normalized.targetPositionCount)
        normalized.minimumCompositeScore = Number(firstOverlayValue(overlay, ["minimumCompositeScore", "minimum_composite_score"], normalized.minimumCompositeScore)) || 0
        normalized.combineMode = String(firstOverlayValue(overlay, ["combineMode", "combine_mode"], normalized.combineMode))
        normalized.selectionScope = String(firstOverlayValue(overlay, ["selectionScope", "selection_scope"], normalized.selectionScope))

        var allocations = Array.isArray(overlay.allocations) ? overlay.allocations : []
        var seenFactorIds = ({})
        for (var index = 0; index < allocations.length; ++index) {
            var normalizedAllocation = normalizedOverlayAllocation(allocations[index])
            if (!normalizedAllocation || seenFactorIds[normalizedAllocation.factor_id]) {
                continue
            }
            seenFactorIds[normalizedAllocation.factor_id] = true
            normalized.allocations.push(normalizedAllocation)
        }

        if (overlay.enabled !== undefined && overlay.enabled !== null) {
            normalized.enabled = !!overlay.enabled
        } else {
            normalized.enabled = normalized.allocations.length > 0
        }

        return normalized
    }

    function factorDisplayName(factorId) {
        if (!factorService || typeof factorService.getFactorById !== "function" || !factorId) {
            return factorId
        }
        var factor = factorService.getFactorById(String(factorId)) || ({})
        return String(factor.displayName || factor.factorName || factor.name || factorId)
    }

    function factorOverlayErrors() {
        var errors = []
        if (!root.factorOverlay.enabled) {
            return errors
        }

        var allocations = root.factorOverlay.allocations || []
        if (allocations.length === 0) {
            errors.push("已启用因子排序层但还没有选择任何因子")
            return errors
        }

        var totalWeight = 0
        for (var index = 0; index < allocations.length; ++index) {
            var weight = Number(allocations[index].weight_percent)
            if (!isFinite(weight) || weight <= 0) {
                errors.push("存在非正数因子权重")
                break
            }
            totalWeight += weight
        }

        if (!(totalWeight > 0)) {
            errors.push("因子总权重必须大于 0")
        }

        return errors
    }

    function openFactorSelector() {
        if (!factorService) {
            return
        }

        if (factorService.initialize) {
            factorService.initialize()
        }

        if (factorSelectorDialog) {
            factorSelectorDialog.destroy()
            factorSelectorDialog = null
        }

        var component = Qt.createComponent("../../FactorWorkbench/Backtest/FactorSelectorDialog.qml")
        if (component.status !== Component.Ready) {
            console.warn("加载因子选择对话框失败:", component.errorString())
            return
        }

        factorSelectorDialog = component.createObject(root, {
            factorService: factorService,
            factorViewModel: factorService.getViewModel ? factorService.getViewModel() : null,
            requireSupportValidation: false,
            selectedFactorIds: (root.factorOverlay.allocations || []).map(function(item) { return item.factor_id || "" })
        })
        if (!factorSelectorDialog) {
            return
        }

        factorSelectorDialog.factorsSelected.connect(root.applySelectedFactors)
        factorSelectorDialog.dialogClosed.connect(function() {
            if (factorSelectorDialog) {
                factorSelectorDialog.destroy()
                factorSelectorDialog = null
            }
        })
        factorSelectorDialog.open()
    }

    function rebalanceFactorOverlayWeights() {
        var allocations = cloneValue(root.factorOverlay.allocations || [])
        if (allocations.length === 0) {
            return
        }
        var equalWeight = 100 / allocations.length
        for (var index = 0; index < allocations.length; ++index) {
            allocations[index].weight_percent = Number(equalWeight.toFixed(4))
        }
        root.factorOverlay.allocations = allocations
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        root.syncDecoratedParameters()
    }

    function applySelectedFactors(selectionPayload) {
        var factorIds = Array.isArray(selectionPayload)
            ? selectionPayload
            : (Array.isArray(selectionPayload && selectionPayload.factorIds) ? selectionPayload.factorIds : [])

        var existing = root.factorOverlay.allocations || []
        var existingById = ({})
        for (var index = 0; index < existing.length; ++index) {
            var existingId = String(existing[index].factor_id || "").trim()
            if (existingId) {
                existingById[existingId] = existing[index]
            }
        }

        var nextAllocations = []
        for (var factorIndex = 0; factorIndex < factorIds.length; ++factorIndex) {
            var factorId = String(factorIds[factorIndex] || "").trim()
            if (!factorId) {
                continue
            }
            var current = existingById[factorId]
            nextAllocations.push({
                factor_id: factorId,
                display_name: current ? String(current.display_name || root.factorDisplayName(factorId)) : root.factorDisplayName(factorId),
                weight_percent: current ? Number(current.weight_percent || 0) : 0
            })
        }

        root.factorOverlay.enabled = nextAllocations.length > 0
        root.factorOverlay.allocations = nextAllocations
        if (nextAllocations.some(function(item) { return !(Number(item.weight_percent) > 0) })) {
            var equalWeight = nextAllocations.length > 0 ? 100 / nextAllocations.length : 0
            for (var allocationIndex = 0; allocationIndex < nextAllocations.length; ++allocationIndex) {
                nextAllocations[allocationIndex].weight_percent = Number(equalWeight.toFixed(4))
            }
            root.factorOverlay.allocations = nextAllocations
        }
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        root.syncDecoratedParameters()
    }

    function updateFactorOverlayWeight(index, rawWeight) {
        var allocations = cloneValue(root.factorOverlay.allocations || [])
        if (index < 0 || index >= allocations.length) {
            return
        }
        var parsedWeight = Number(String(rawWeight || "").replace(/%/g, ""))
        allocations[index].weight_percent = isNaN(parsedWeight) ? 0 : parsedWeight
        root.factorOverlay.allocations = allocations
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        root.syncDecoratedParameters()
    }

    function removeFactorOverlayAllocation(index) {
        var allocations = cloneValue(root.factorOverlay.allocations || [])
        if (index < 0 || index >= allocations.length) {
            return
        }
        allocations.splice(index, 1)
        root.factorOverlay.allocations = allocations
        root.factorOverlay.enabled = allocations.length > 0
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        if (allocations.length > 0) {
            root.rebalanceFactorOverlayWeights()
            return
        }

        root.syncDecoratedParameters()
    }

    function clearFactorOverlayAllocations() {
        root.factorOverlay.allocations = []
        root.factorOverlay.enabled = false
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        root.syncDecoratedParameters()
    }

    function currentSuggestionPhaseLock() {
        var stage = currentSelectedRuleComposerStage()
        return normalizedRuleTemplatePhase((stage && stage.stageId) || root.selectedRuleComposerStageId || "")
    }

    function currentSelectedRuleComposerStage() {
        var selectedStageId = String(root.selectedRuleComposerStageId || "").trim()
        if (!selectedStageId) {
            return null
        }

        for (var index = 0; index < root.ruleComposerStages.length; ++index) {
            var stage = root.ruleComposerStages[index]
            if (String(stage && stage.stageId || "").trim() === selectedStageId) {
                return stage
            }
        }

        return null
    }

    function currentSelectedRuleComposerGroup() {
        var stage = currentSelectedRuleComposerStage()
        var groups = Array.isArray(stage && stage.groups) ? stage.groups : []
        var selectedGroupId = String(root.selectedRuleComposerGroupId || "").trim()

        if (selectedGroupId) {
            for (var index = 0; index < groups.length; ++index) {
                var group = groups[index]
                if (String(group && group.groupId || "").trim() === selectedGroupId) {
                    return group
                }
            }
        }

        return groups.length > 0 ? groups[0] : null
    }

    function firstPopulatedRuleComposerSelection(stages, preferredStageId, preferredGroupId) {
        var normalizedStages = Array.isArray(stages) ? stages : []
        var preferredStageKey = String(preferredStageId || "").trim().toLowerCase()
        var preferredGroupKey = String(preferredGroupId || "").trim()
        var fallbackStage = ""
        var fallbackGroup = ""

        function firstGroupId(stage) {
            var stageGroups = Array.isArray(stage && stage.groups) ? stage.groups : []
            for (var groupIndex = 0; groupIndex < stageGroups.length; ++groupIndex) {
                var candidateGroupId = String(stageGroups[groupIndex] && stageGroups[groupIndex].groupId || "").trim()
                if (candidateGroupId) {
                    return candidateGroupId
                }
            }
            return ""
        }

        var preferredStage = null
        for (var stageIndex = 0; stageIndex < normalizedStages.length; ++stageIndex) {
            var stage = normalizedStages[stageIndex]
            var stageId = String(stage && stage.stageId || "").trim()
            var normalizedStageId = stageId.toLowerCase()
            if (!fallbackStage && stageId) {
                fallbackStage = stageId
                fallbackGroup = firstGroupId(stage)
            }
            if (preferredStageKey && normalizedStageId === preferredStageKey) {
                preferredStage = stage
            }
        }

        if (preferredStage) {
            var preferredResolvedStageId = String(preferredStage.stageId || "").trim()
            var preferredResolvedGroupId = firstGroupId(preferredStage)
            var preferredGroups = Array.isArray(preferredStage.groups) ? preferredStage.groups : []
            if (preferredGroupKey) {
                for (var preferredGroupIndex = 0; preferredGroupIndex < preferredGroups.length; ++preferredGroupIndex) {
                    var preferredGroup = preferredGroups[preferredGroupIndex]
                    var candidatePreferredGroupId = String(preferredGroup && preferredGroup.groupId || "").trim()
                    if (candidatePreferredGroupId === preferredGroupKey) {
                        preferredResolvedGroupId = candidatePreferredGroupId
                        break
                    }
                }
            }

            return {
                stageId: preferredResolvedStageId,
                groupId: preferredResolvedGroupId
            }
        }

        return {
            stageId: fallbackStage || preferredStageId || "signal",
            groupId: fallbackGroup || preferredGroupId || ""
        }
    }

    function defaultRuleComposerStages() {
        var profile = root.strategyProfile
        if (!profile || Object.keys(profile).length === 0) {
            profile = Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        }
        return Utils.StrategyCreationUtils.buildDefaultRuleComposerSkeleton(profile, [])
    }

    function defaultRulePackEntries() {
        var profile = root.strategyProfile
        if (!profile || Object.keys(profile).length === 0) {
            profile = Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        }
        return Utils.StrategyCreationUtils.buildDefaultMarketRuleBindings(profile)
            .concat(Utils.StrategyCreationUtils.buildDefaultBaseRuleBindings(profile))
    }

    function selectedRuleComposerStageRuleCount() {
        var stage = currentSelectedRuleComposerStage()
        var groups = Array.isArray(stage && stage.groups) ? stage.groups : []
        var total = 0
        for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            var rules = Array.isArray(groups[groupIndex].rules) ? groups[groupIndex].rules : []
            total += rules.length
        }
        return total
    }

    function currentRuleComposerGroupQuickImportEntries() {
        var stage = currentSelectedRuleComposerStage()
        var group = currentSelectedRuleComposerGroup()
        if (!stage || !group) {
            return []
        }

        var stageId = String(stage.stageId || "").trim().toLowerCase()
        var groupId = String(group.groupId || "").trim().toLowerCase()
        var groupRole = String(group.role || "").trim().toLowerCase()
        var existingTemplates = ({})
        var groupRules = Array.isArray(group.rules) ? group.rules : []
        for (var ruleIndex = 0; ruleIndex < groupRules.length; ++ruleIndex) {
            var templateId = String(groupRules[ruleIndex].templateId || "").trim()
            if (templateId) {
                existingTemplates[templateId] = true
            }
        }

        var exactMatches = []
        var roleMatches = []
        var stageMatches = []
        var defaults = defaultRulePackEntries()
        for (var index = 0; index < defaults.length; ++index) {
            var entry = defaults[index] || ({})
            var entryStageId = String(entry.stageId || "").trim().toLowerCase()
            var entryGroupId = String(entry.groupId || "").trim().toLowerCase()
            var entryGroupRole = String(entry.groupRole || "").trim().toLowerCase()
            var entryTemplateId = String(entry.templateId || "").trim()
            if (!entryTemplateId || existingTemplates[entryTemplateId] || entryStageId !== stageId) {
                continue
            }
            if (entryGroupId === groupId) {
                exactMatches.push(entry)
            } else if (groupRole && entryGroupRole === groupRole) {
                roleMatches.push(entry)
            } else {
                stageMatches.push(entry)
            }
        }

        if (exactMatches.length > 0) {
            return exactMatches
        }
        if (roleMatches.length > 0) {
            return roleMatches
        }
        return stageMatches
    }

    function applyDefaultRulePackEntryToCurrentGroup(entry) {
        var currentStage = currentSelectedRuleComposerStage()
        var currentGroup = currentSelectedRuleComposerGroup()
        if (!entry || !currentStage || !currentGroup) {
            return
        }

        upsertRuleComposerSuggestion({
            template_id: entry.templateId || "",
            template_display_name: entry.templateDisplayName || entry.templateId || "",
            term_id: entry.termId || "",
            term_display_name: entry.termDisplayName || "",
            file_name: entry.fileName || "",
            phase: currentStage.stageId || "signal",
            category: entry.category || "",
            summary: entry.summary || "",
            is_ready: true
        })
        syncDecoratedParameters()
    }

    function restoreDefaultRuleComposerPack() {
        if (!root.strategyProfile || Object.keys(root.strategyProfile).length === 0) {
            root.strategyProfile = Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        }

        var defaultStages = defaultRuleComposerStages()
        var selection = firstPopulatedRuleComposerSelection(
            defaultStages,
            root.selectedRuleComposerStageId || "signal",
            root.selectedRuleComposerGroupId || "")

        root.ruleComposerStages = defaultStages
        root.selectedRuleComposerStageId = selection.stageId || "signal"
        root.selectedRuleComposerGroupId = selection.groupId || ""
        ensureSelectedRuleComposerGroup()
        syncRuleTemplateBindingPreviewState()
        syncDecoratedParameters()
    }

    function restoreSelectedRuleComposerStageDefaults() {
        var stageId = String(root.selectedRuleComposerStageId || "").trim().toLowerCase()
        var previousGroupId = String(root.selectedRuleComposerGroupId || "").trim()
        if (!stageId) {
            return
        }

        var defaults = defaultRuleComposerStages()
        var defaultStage = null
        for (var defaultIndex = 0; defaultIndex < defaults.length; ++defaultIndex) {
            if (String(defaults[defaultIndex].stageId || "").trim().toLowerCase() === stageId) {
                defaultStage = defaults[defaultIndex]
                break
            }
        }
        if (!defaultStage) {
            return
        }

        var nextStages = cloneRuleComposerStages()
        for (var stageIndex = 0; stageIndex < nextStages.length; ++stageIndex) {
            if (String(nextStages[stageIndex].stageId || "").trim().toLowerCase() !== stageId) {
                continue
            }
            nextStages[stageIndex] = defaultStage
            root.ruleComposerStages = nextStages
            root.selectedRuleComposerStageId = defaultStage.stageId || stageId
            root.selectedRuleComposerGroupId = previousGroupId
            ensureSelectedRuleComposerGroup()
            syncRuleTemplateBindingPreviewState()
            syncDecoratedParameters()
            return
        }
    }

    function previewRuleComposerStages() {
        return (root.ruleComposerStages || []).filter(function(stage) {
            return root.populatedRuleComposerGroups(stage).length > 0
        })
    }

    function previewRuleComposerGroupCount() {
        return previewRuleComposerStages().reduce(function(total, stage) {
            return total + root.populatedRuleComposerGroups(stage).length
        }, 0)
    }

    function previewRuleComposerRuleCount() {
        return previewRuleComposerStages().reduce(function(total, stage) {
            return total + root.previewRuleComposerRuleCountForStage(stage)
        }, 0)
    }

    function previewRuleComposerRuleCountForStage(stage) {
        return root.populatedRuleComposerGroups(stage).reduce(function(total, group) {
            return total + (Array.isArray(group.rules) ? group.rules.length : 0)
        }, 0)
    }

    function ruleComposerPreviewBinding(stageData, groupData, ruleData) {
        var binding = {
            phase: normalizedRuleTemplatePhase((ruleData && ruleData.phase) || (stageData && stageData.stageId) || "signal")
        }

        if (ruleData && ruleData.fileName) {
            binding.file_name = ruleData.fileName
        }
        if (ruleData && ruleData.filePath) {
            binding.file_path = ruleData.filePath
        }
        if (ruleData && ruleData.templateId) {
            binding.template_id = ruleData.templateId
        }
        if (ruleData && ruleData.templateName) {
            binding.template_display_name = ruleData.templateName
        }
        if (ruleData && ruleData.summary) {
            binding.summary = ruleData.summary
        }
        if (ruleData && ruleData.category) {
            binding.category = ruleData.category
        }
        if (ruleData && ruleData.termId) {
            binding.term_id = ruleData.termId
        }
        if (ruleData && ruleData.termName) {
            binding.term_display_name = ruleData.termName
        }
        if (ruleData && ruleData.defaultInjected) {
            binding.default_injected = true
        }
        if (groupData && groupData.groupId) {
            binding.group_id = groupData.groupId
        }
        if (groupData && groupData.title) {
            binding.group_title = groupData.title
        }
        if (groupData && groupData.role) {
            binding.group_role = groupData.role
        }
        if (groupData && groupData.operator) {
            binding.group_operator = groupData.operator
        }
        var groupMinMatchCount = Number(groupData && (groupData.groupMinMatchCount || groupData.matchThreshold || 0))
        if (groupMinMatchCount > 0) {
            binding.group_min_match_count = groupMinMatchCount
        }
        return binding
    }

    function rebuildRuleComposerState(resetProfile) {
        if (resetProfile || !root.strategyProfile || Object.keys(root.strategyProfile).length === 0) {
            root.strategyProfile = Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        }
        root.ruleComposerStages = Utils.StrategyCreationUtils.buildDefaultRuleComposerSkeleton(
            root.strategyProfile,
            root.boundRuleTemplateBindingList()
        )
        if (!root.selectedRuleComposerStageId) {
            root.selectedRuleComposerStageId = "signal"
        }
        var hasSelectedStage = false
        for (var index = 0; index < root.ruleComposerStages.length; ++index) {
            if (root.ruleComposerStages[index].stageId === root.selectedRuleComposerStageId) {
                hasSelectedStage = true
                break
            }
        }
        if (!hasSelectedStage && root.ruleComposerStages.length > 0) {
            root.selectedRuleComposerStageId = root.ruleComposerStages[0].stageId
        }
        ensureSelectedRuleComposerGroup()
        emitValidationState(currentParameterValidationErrors())
    }

    function currentMarketRuleComposerStage() {
        for (var index = 0; index < root.ruleComposerStages.length; ++index) {
            if ((root.ruleComposerStages[index].stageId || "") === "market") {
                return root.ruleComposerStages[index]
            }
        }
        return null
    }

    function defaultMarketRuleComposerStage() {
        var profile = root.strategyProfile
        if (!profile || Object.keys(profile).length === 0) {
            profile = Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        }
        var defaultStages = Utils.StrategyCreationUtils.buildDefaultRuleComposerSkeleton(profile, [])
        for (var index = 0; index < defaultStages.length; ++index) {
            if ((defaultStages[index].stageId || "") === "market") {
                return defaultStages[index]
            }
        }
        return null
    }

    function marketRuleComposerStageSignature(stage) {
        var groups = Array.isArray(stage && stage.groups) ? stage.groups : []
        var normalizedGroups = groups.map(function(group) {
            var rules = Array.isArray(group && group.rules) ? group.rules : []
            var normalizedRules = rules.map(function(rule) {
                return {
                    templateId: String(rule && rule.templateId || "").trim(),
                    termId: String(rule && rule.termId || "").trim(),
                    category: String(rule && rule.category || "").trim(),
                    defaultInjected: !!(rule && rule.defaultInjected)
                }
            })
            normalizedRules.sort(function(left, right) {
                var leftKey = [left.templateId, left.termId, left.category, left.defaultInjected ? "1" : "0"].join("|")
                var rightKey = [right.templateId, right.termId, right.category, right.defaultInjected ? "1" : "0"].join("|")
                return leftKey.localeCompare(rightKey, "zh-CN")
            })
            return {
                groupId: String(group && group.groupId || "").trim(),
                role: String(group && group.role || "").trim(),
                operator: String(group && group.operator || "").trim(),
                rules: normalizedRules
            }
        })
        normalizedGroups.sort(function(left, right) {
            return String(left.groupId || "").localeCompare(String(right.groupId || ""), "zh-CN")
        })
        return JSON.stringify(normalizedGroups)
    }

    function hasCustomizedMarketRuleComposerStage() {
        var currentStage = currentMarketRuleComposerStage()
        var defaultStage = defaultMarketRuleComposerStage()
        if (!currentStage && !defaultStage) {
            return false
        }
        if (!currentStage || !defaultStage) {
            return true
        }
        return marketRuleComposerStageSignature(currentStage) !== marketRuleComposerStageSignature(defaultStage)
    }

    function hasAnyMarketRuleComposerRules() {
        var currentStage = currentMarketRuleComposerStage()
        var groups = Array.isArray(currentStage && currentStage.groups) ? currentStage.groups : []
        for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            if (Array.isArray(groups[groupIndex].rules) && groups[groupIndex].rules.length > 0) {
                return true
            }
        }
        return false
    }

    function clearMarketRuleComposerStage() {
        var nextStages = cloneRuleComposerStages()
        var targetStage = null
        for (var stageIndex = 0; stageIndex < nextStages.length; ++stageIndex) {
            if ((nextStages[stageIndex].stageId || "") !== "market") {
                continue
            }
            targetStage = nextStages[stageIndex]
            break
        }

        if (!targetStage) {
            targetStage = defaultMarketRuleComposerStage()
            if (!targetStage) {
                return
            }
            nextStages.unshift(targetStage)
        }

        var groups = Array.isArray(targetStage.groups) ? targetStage.groups : []
        for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            groups[groupIndex].rules = []
        }

        root.ruleComposerStages = nextStages
        root.selectedRuleComposerStageId = "market"
        root.selectedRuleComposerGroupId = "market_gate"
        ensureSelectedRuleComposerGroup()
        syncRuleTemplateBindingPreviewState()
        syncDecoratedParameters()
    }

    function restoreDefaultMarketRuleComposerStage() {
        if (!root.strategyProfile || Object.keys(root.strategyProfile).length === 0) {
            root.strategyProfile = Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        }

        var nextStages = cloneRuleComposerStages()
        var defaultMarketStage = defaultMarketRuleComposerStage()
        if (!defaultMarketStage) {
            return
        }

        var replaced = false
        for (var stageIndex = 0; stageIndex < nextStages.length; ++stageIndex) {
            if ((nextStages[stageIndex].stageId || "") !== "market") {
                continue
            }
            nextStages[stageIndex] = defaultMarketStage
            replaced = true
            break
        }
        if (!replaced) {
            nextStages.unshift(defaultMarketStage)
        }

        root.ruleComposerStages = nextStages
        root.selectedRuleComposerStageId = "market"
        root.selectedRuleComposerGroupId = "market_gate"
        ensureSelectedRuleComposerGroup()
        syncRuleTemplateBindingPreviewState()
        syncDecoratedParameters()
    }

    function extractEditableParameterValues(sourceParameters) {
        var extracted = ({})
        var source = sourceParameters || ({})
        for (var key in source) {
            if (key === "rule_template_bindings"
                    || key === "rule_template_binding"
                    || key === "rule_profile"
                    || key === "execution_policy"
                    || key === "backtest_assumptions"
                    || key === "rule_composer_state"
                    || key === "factor_overlay") {
                continue
            }
            extracted[key] = source[key]
        }
        return extracted
    }

    function currentEditableParameterValues() {
        if (dynamicGenerator && typeof dynamicGenerator.getValues === "function") {
            var preservedValues = extractEditableParameterValues(root.strategyParameters)
            var dynamicValues = dynamicGenerator.getValues() || ({})
            for (var key in dynamicValues) {
                preservedValues[key] = dynamicValues[key]
            }
            return preservedValues
        }
        return extractEditableParameterValues(root.strategyParameters)
    }

    function resolvedRuleTemplateFileName(ruleLike) {
        var directFileName = String((ruleLike && (ruleLike.file_name || ruleLike.fileName)) || "").trim()
        if (directFileName) {
            return directFileName
        }
        return Utils.StrategyCreationUtils.resolveRuleTemplateFileName(
            ruleLike && (ruleLike.template_id || ruleLike.templateId || "")
        )
    }

    function supportedRuleBindingPhaseIndex(value) {
        var parsed = Number(value)
        if (!isFinite(parsed)) {
            return -1
        }
        parsed = Math.floor(parsed)
        return parsed >= 0 && parsed <= 6 ? parsed : -1
    }

    function composerStagePhaseIndex(stageId) {
        var normalizedStageId = String(stageId || "").trim().toLowerCase()
        var mapping = {
            market: 0,
            eligibility: 1,
            signal: 1,
            portfolio: 5,
            rebalance: 3,
            execution: 5,
            account_risk: 5
        }
        return mapping.hasOwnProperty(normalizedStageId) ? mapping[normalizedStageId] : -1
    }

    function composerRulePhaseIndex(rule, stageId) {
        var configured = supportedRuleBindingPhaseIndex(rule && (rule.bindingPhase !== undefined ? rule.bindingPhase : rule.phase))
        if (configured >= 0) {
            return configured
        }

        var tokens = [
            rule && rule.templateId,
            rule && rule.template_id,
            rule && rule.fileName,
            rule && rule.file_name,
            rule && rule.category,
            rule && rule.termId,
            rule && rule.term_id,
            rule && rule.termName,
            rule && rule.term_display_name,
            rule && rule.summary
        ].map(function(item) {
            return String(item || "").trim().toLowerCase()
        }).join(" ")

        if (tokens.indexOf("watch_") >= 0 || tokens.indexOf("watch") >= 0 || tokens.indexOf("invalid") >= 0) {
            return 6
        }
        if (tokens.indexOf("exit_") >= 0 || tokens.indexOf(" exit") >= 0 || tokens.indexOf("exit_management") >= 0) {
            return 4
        }
        if (tokens.indexOf("entry_") >= 0 || tokens.indexOf(" entry") >= 0 || tokens.indexOf("entry_pattern") >= 0) {
            return 2
        }

        return composerStagePhaseIndex(stageId)
    }

    function normalizedRuleComposerStagesForPersistence() {
        var stages = cloneRuleComposerStages()
        for (var stageIndex = 0; stageIndex < stages.length; ++stageIndex) {
            var stage = stages[stageIndex] || ({})
            var stagePhaseIndex = composerStagePhaseIndex(stage.stageId)
            if (stagePhaseIndex >= 0) {
                stage.phase = stagePhaseIndex
                stage.bindingPhase = stagePhaseIndex
            } else {
                delete stage.phase
                delete stage.bindingPhase
            }
            var groups = Array.isArray(stage.groups) ? stage.groups : []
            for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
                var group = groups[groupIndex] || ({})
                var rules = Array.isArray(group.rules) ? group.rules : []
                for (var ruleIndex = 0; ruleIndex < rules.length; ++ruleIndex) {
                    var rule = rules[ruleIndex] || ({})
                    var rulePhaseIndex = composerRulePhaseIndex(rule, stage.stageId)
                    if (rulePhaseIndex >= 0) {
                        rule.phase = rulePhaseIndex
                        rule.bindingPhase = rulePhaseIndex
                    } else {
                        delete rule.phase
                        delete rule.bindingPhase
                    }
                    var resolvedFileName = resolvedRuleTemplateFileName(rule)
                    if (resolvedFileName) {
                        rule.fileName = resolvedFileName
                    }
                }
            }
        }
        return stages
    }

    function buildRuleComposerStatePayload() {
        return {
            version: 1,
            selectedStageId: root.selectedRuleComposerStageId || "",
            selectedGroupId: root.selectedRuleComposerGroupId || "",
            stages: normalizedRuleComposerStagesForPersistence()
        }
    }

    function buildRuleProfileFieldPayload(sourceParameters) {
        var source = sourceParameters || ({})
        var payload = ({})

        if (source.stopLoss !== undefined && source.stopLoss !== null && source.stopLoss !== "") {
            payload.stopLossPercent = Number(source.stopLoss)
        }
        if (source.takeProfit !== undefined && source.takeProfit !== null && source.takeProfit !== "") {
            payload.takeProfitPercent = Number(source.takeProfit)
        }
        if (source.rebalanceDays !== undefined && source.rebalanceDays !== null && source.rebalanceDays !== "") {
            payload.rebalanceDays = Number(source.rebalanceDays)
        }
        if (source.maxDrawdownLimit !== undefined && source.maxDrawdownLimit !== null && source.maxDrawdownLimit !== "") {
            payload.maxDrawdownLimit = Number(source.maxDrawdownLimit)
        }

        return payload
    }

    function buildExecutionPolicyPayload(sourceParameters) {
        return {
            version: 1
        }
    }

    function buildBacktestAssumptionsPayload(sourceParameters) {
        return {
            version: 1
        }
    }

    function buildRuleProfilePayload(sourceParameters) {
        var payload = {
            version: 1,
            strategyProfile: root.strategyProfile || ({}),
            ruleComposerState: buildRuleComposerStatePayload()
        }

        var fieldPayload = buildRuleProfileFieldPayload(sourceParameters)
        for (var key in fieldPayload) {
            payload[key] = fieldPayload[key]
        }

        return payload
    }

    function syncDecoratedParameters(sourceValues) {
        var mergedValues = decorateParameters(sourceValues === undefined ? currentEditableParameterValues() : sourceValues)
        root.strategyParameters = mergedValues
        root.parametersChanged(mergedValues)
        emitValidationState(currentParameterValidationErrors())
        return mergedValues
    }

    function currentParameterValidationErrors() {
        if (dynamicGenerator && dynamicGenerator.validationErrors) {
            return dynamicGenerator.validationErrors
        }
        return ({})
    }

    function refreshRuleComposerValidation() {
        root.ruleComposerValidation = Utils.StrategyCreationUtils.validateRuleComposerConfiguration(
            root.strategyProfile,
            root.ruleComposerStages)
        return root.ruleComposerValidation
    }

    function combinedValidationPayload(parameterErrors) {
        var payload = ({})
        var sourceErrors = parameterErrors || ({})
        for (var key in sourceErrors) {
            payload[key] = sourceErrors[key]
        }

        payload.ruleComposerValidation = root.ruleComposerValidation
        var factorErrors = root.factorOverlayErrors()
        if (factorErrors.length > 0) {
            payload.factorOverlayErrors = factorErrors
        }
        if ((root.ruleComposerValidation.errors || []).length > 0) {
            payload.ruleComposerErrors = root.ruleComposerValidation.errors.map(function(item) {
                return item.message || ""
            })
        }
        if ((root.ruleComposerValidation.warnings || []).length > 0) {
            payload.ruleComposerWarnings = root.ruleComposerValidation.warnings.map(function(item) {
                return item.message || ""
            })
        }
        return payload
    }

    function emitValidationState(parameterErrors) {
        refreshRuleComposerValidation()
        root.validationChanged(root.parametersValid && root.ruleComposerConfigValid && root.factorOverlayErrors().length === 0, combinedValidationPayload(parameterErrors))
    }

    function decorateParameters(sourceParameters) {
        var merged = ({})
        var source = sourceParameters || ({})
        for (var key in source) {
            merged[key] = source[key]
        }
        mergeImportedFactorContext(merged, root.strategyParameters)
        mergeImportedFactorContext(merged, source)
        delete merged.rule_template_bindings
        delete merged.rule_template_binding
        merged.rule_composer_state = buildRuleComposerStatePayload()
        merged.rule_profile = buildRuleProfilePayload(source)
        merged.execution_policy = buildExecutionPolicyPayload(source)
        merged.backtest_assumptions = buildBacktestAssumptionsPayload(source)
        var normalizedFactorOverlay = normalizeFactorOverlay(root.factorOverlay, merged)
        if (normalizedFactorOverlay.enabled && normalizedFactorOverlay.allocations.length > 0) {
            merged.factor_overlay = normalizedFactorOverlay
        } else {
            delete merged.factor_overlay
        }
        delete merged.stopLoss
        delete merged.takeProfit
        delete merged.rebalanceDays
        delete merged.maxDrawdownLimit
        delete merged.turnoverLimit
        delete merged.slippageLimit
        delete merged.level1Breaker
        delete merged.level2Breaker
        delete merged.level3Breaker
        delete merged.factor_allocations
        delete merged.allocations
        delete merged.portfolio_allocations_json
        delete merged.portfolio_factor_ids
        delete merged.portfolio_factor_count
        return merged
    }

    function normalizedRuleTemplatePhase(phase) {
        var rawPhase = phase === undefined || phase === null ? "" : String(phase).trim().toLowerCase()
        if (!rawPhase) {
            return "signal"
        }

        var normalized = PreviewUtils.normalizePhaseKey(rawPhase)
        var validPhases = {
            market: true,
            eligibility: true,
            signal: true,
            portfolio: true,
            rebalance: true,
            execution: true,
            account_risk: true
        }
        return validPhases[normalized] ? normalized : ""
    }

    function roleDisplayName(role) {
        var mapping = {
            must_pass: "必须满足",
            any_pass: "任一满足",
            veto: "否决条件",
            score_boost: "评分增强",
            position_management: "仓位管理",
            execution_constraint: "执行限制",
            account_guard: "账户保护"
        }
        return mapping[role] || role || "规则组"
    }

    function operatorDisplayName(operatorValue) {
        var mapping = {
            all: "全部满足",
            any: "任一满足",
            at_least: "至少命中",
            score_sum: "累计评分",
            first_match: "首个命中"
        }
        return mapping[operatorValue] || operatorValue || "未设置"
    }

    function normalizeStructuredValue(rawValue) {
        if (rawValue === undefined || rawValue === null) {
            return rawValue
        }

        if (typeof rawValue !== "string") {
            if (typeof rawValue === "object") {
                try {
                    return JSON.parse(JSON.stringify(rawValue))
                } catch (error) {
                    return rawValue
                }
            }
            return rawValue
        }

        var text = String(rawValue).trim()
        if (!text) {
            return ({})
        }

        var firstChar = text.charAt(0)
        if (firstChar !== "{" && firstChar !== "[") {
            return rawValue
        }

        try {
            return JSON.parse(text)
        } catch (error) {
            console.warn("解析结构化规则数据失败:", error)
            return rawValue
        }
    }

    function normalizeRuleTemplateBindingEntries(rawValue) {
        var normalized = []
        var structuredValue = normalizeStructuredValue(rawValue)

        if (Array.isArray(structuredValue)) {
            for (var index = 0; index < structuredValue.length; ++index) {
                var arrayItem = normalizeStructuredValue(structuredValue[index])
                if (!arrayItem || typeof arrayItem !== "object") {
                    continue
                }
                normalized.push(normalizeStructuredValue(arrayItem))
            }
            return normalized
        }

        if (structuredValue && typeof structuredValue === "object") {
            var hasTypedBindingFields = structuredValue.template_id || structuredValue.templateId || structuredValue.file_name || structuredValue.fileName
            if (hasTypedBindingFields) {
                normalized.push(normalizeStructuredValue(structuredValue))
                return normalized
            }

            for (var key in structuredValue) {
                var entry = normalizeStructuredValue(structuredValue[key])
                if (!entry || typeof entry !== "object") {
                    continue
                }
                if (!entry.phase && entry.stageId) {
                    entry.phase = entry.stageId
                }
                if (!entry.phase) {
                    entry.phase = key
                }
                normalized.push(normalizeStructuredValue(entry))
            }
        }

        return normalized
    }

    function normalizeRuleTemplateBindings(rawValue) {
        var normalized = ({})
        var entries = normalizeRuleTemplateBindingEntries(rawValue)
        if (entries.length > 0) {
            for (var index = 0; index < entries.length; ++index) {
                var item = entries[index] || ({})
                var phase = normalizedRuleTemplatePhase(item.phase || item.stageId)
                if (!phase) {
                    continue
                }
                if (!normalized[phase] || Object.keys(normalized[phase]).length === 0) {
                    normalized[phase] = item
                }
            }
        }
        return normalized
    }

    function extractRuleTemplateBindingsFromComposerState(rawComposerState) {
        var composerState = normalizeStructuredValue(rawComposerState) || ({})
        var stages = Array.isArray(composerState.stages) ? composerState.stages : []
        var bindings = []
        var seenBindings = ({})

        for (var stageIndex = 0; stageIndex < stages.length; ++stageIndex) {
            var stage = stages[stageIndex] || ({})
            var stageId = normalizedRuleTemplatePhase(stage.stageId)
            var groups = Array.isArray(stage.groups) ? stage.groups : []

            for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
                var group = groups[groupIndex] || ({})
                var groupId = String(group.groupId || "").trim()
                var groupTitle = String(group.title || "").trim()
                var groupRole = String(group.role || "").trim().toLowerCase()
                var groupOperator = String(group.operator || "").trim().toLowerCase()
                var groupMinMatchCount = Number(
                    group.groupMinMatchCount || group.minMatchCount || group.minimumMatches || group.atLeastCount || 0
                )
                var rules = Array.isArray(group.rules) ? group.rules : []

                for (var ruleIndex = 0; ruleIndex < rules.length; ++ruleIndex) {
                    var rule = rules[ruleIndex] || ({})
                    var filePath = String(rule.filePath || "").trim()
                    var fileName = resolvedRuleTemplateFileName(rule)
                    var templateId = String(rule.templateId || "").trim()
                    if (!filePath && !fileName && !templateId) {
                        continue
                    }

                    var binding = {
                        phase: normalizedRuleTemplatePhase(rule.phase || stageId)
                    }

                    if (fileName) {
                        binding.file_name = fileName
                    }
                    if (filePath) {
                        binding.file_path = filePath
                    }
                    if (templateId) {
                        binding.template_id = templateId
                    }

                    var templateName = String(rule.templateName || "").trim()
                    if (templateName) {
                        binding.template_display_name = templateName
                    }

                    var summary = String(rule.summary || "").trim()
                    if (summary) {
                        binding.summary = summary
                    }

                    var category = String(rule.category || "").trim()
                    if (category) {
                        binding.category = category
                    }

                    var termId = String(rule.termId || "").trim()
                    if (termId) {
                        binding.term_id = termId
                    }

                    var termName = String(rule.termName || "").trim()
                    if (termName) {
                        binding.term_display_name = termName
                    }
                    if (rule.defaultInjected) {
                        binding.default_injected = true
                    }

                    if (groupId) {
                        binding.group_id = groupId
                    }
                    if (groupTitle) {
                        binding.group_title = groupTitle
                    }
                    if (groupRole) {
                        binding.group_role = groupRole
                    }
                    if (groupOperator) {
                        binding.group_operator = groupOperator
                    }
                    if (groupMinMatchCount > 0) {
                        binding.group_min_match_count = groupMinMatchCount
                    }

                    var bindingSignature = JSON.stringify(binding)
                    if (seenBindings[bindingSignature]) {
                        continue
                    }
                    seenBindings[bindingSignature] = true
                    bindings.push(binding)
                }
            }
        }

        return bindings
    }

    function buildRuleTemplateBindingsPayload() {
        var composerBindings = extractRuleTemplateBindingsFromComposerState(buildRuleComposerStatePayload())
        if (composerBindings.length > 0) {
            return composerBindings
        }
        return boundRuleTemplateBindingList()
    }

    function syncRuleTemplateBindingPreviewState() {
        var composerBindings = extractRuleTemplateBindingsFromComposerState(buildRuleComposerStatePayload())
        boundRuleTemplateBindingEntries = normalizeRuleTemplateBindingEntries(composerBindings)
        boundRuleTemplateBindings = normalizeRuleTemplateBindings(composerBindings)
    }

    function boundRuleTemplateBindingList() {
        var normalizedEntries = normalizeRuleTemplateBindingEntries(boundRuleTemplateBindingEntries)
        if (normalizedEntries.length > 0) {
            return normalizedEntries
        }

        var bindings = []
        var phases = ["market", "eligibility", "signal", "portfolio", "rebalance", "execution", "account_risk"]
        for (var index = 0; index < phases.length; ++index) {
            var phase = phases[index]
            var binding = (boundRuleTemplateBindings || ({}))[phase]
            if (binding && Object.keys(binding).length > 0) {
                bindings.push(binding)
            }
        }
        for (var key in (boundRuleTemplateBindings || ({}))) {
            var existing = boundRuleTemplateBindings[key]
            if (!existing || Object.keys(existing).length === 0) {
                continue
            }
            var alreadyIncluded = false
            for (var bindingIndex = 0; bindingIndex < bindings.length; ++bindingIndex) {
                if (bindings[bindingIndex] === existing) {
                    alreadyIncluded = true
                    break
                }
            }
            if (!alreadyIncluded) {
                bindings.push(existing)
            }
        }
        return bindings
    }

    function primaryRuleTemplateBinding(bindingList) {
        var bindings = Array.isArray(bindingList) ? bindingList : boundRuleTemplateBindingList()
        for (var index = 0; index < bindings.length; ++index) {
            var phase = normalizedRuleTemplatePhase(bindings[index].phase)
            if (phase === "signal") {
                return bindings[index]
            }
        }
        return bindings.length > 0 ? bindings[0] : ({})
    }

    function cloneRuleComposerStages() {
        return JSON.parse(JSON.stringify(root.ruleComposerStages || []))
    }

    function resolveSuggestionTargetLocation(suggestion, stages) {
        var stageList = Array.isArray(stages) ? stages : (root.ruleComposerStages || [])
        var preferredStageId = String(root.selectedRuleComposerStageId || "").trim()
        var preferredGroupId = String(root.selectedRuleComposerGroupId || "").trim()
        var fallbackStageId = normalizedRuleTemplatePhase((suggestion && suggestion.phase) || preferredStageId || "signal")

        function findStage(stageId) {
            for (var stageIndex = 0; stageIndex < stageList.length; ++stageIndex) {
                if (stageList[stageIndex].stageId === stageId) {
                    return stageList[stageIndex]
                }
            }
            return null
        }

        var resolvedStage = findStage(preferredStageId)
        if (!resolvedStage) {
            resolvedStage = findStage(fallbackStageId)
        }
        if (!resolvedStage && stageList.length > 0) {
            resolvedStage = stageList[0]
        }

        var resolvedGroup = null
        var groups = resolvedStage && Array.isArray(resolvedStage.groups) ? resolvedStage.groups : []
        for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            if (groups[groupIndex].groupId === preferredGroupId) {
                resolvedGroup = groups[groupIndex]
                break
            }
        }
        if (!resolvedGroup && groups.length > 0) {
            resolvedGroup = groups[0]
        }

        return {
            stageId: resolvedStage && resolvedStage.stageId ? resolvedStage.stageId : fallbackStageId,
            groupId: resolvedGroup && resolvedGroup.groupId ? resolvedGroup.groupId : preferredGroupId,
            group: resolvedGroup
        }
    }

    function upsertRuleComposerSuggestion(suggestion) {
        var nextStages = cloneRuleComposerStages()
        var targetLocation = resolveSuggestionTargetLocation(suggestion, nextStages)
        var targetStageId = targetLocation.stageId
        var targetGroupId = targetLocation.groupId
        var templateId = suggestion.template_id || suggestion.templateId || ""

        for (var stageIndex = 0; stageIndex < nextStages.length; ++stageIndex) {
            var stage = nextStages[stageIndex]
            if (stage.stageId !== targetStageId || !Array.isArray(stage.groups) || stage.groups.length === 0) {
                continue
            }

            var resolvedGroup = targetLocation.group || stage.groups[0]
            for (var groupIndex = 0; groupIndex < stage.groups.length; ++groupIndex) {
                if (stage.groups[groupIndex].groupId === targetGroupId) {
                    resolvedGroup = stage.groups[groupIndex]
                    break
                }
            }

            if (!Array.isArray(resolvedGroup.rules)) {
                resolvedGroup.rules = []
            }

            var updated = false
            for (var ruleIndex = 0; ruleIndex < resolvedGroup.rules.length; ++ruleIndex) {
                var existingRule = resolvedGroup.rules[ruleIndex]
                if ((existingRule.templateId || "") === templateId) {
                    resolvedGroup.rules[ruleIndex] = {
                        instanceId: existingRule.instanceId || ("rule_" + Date.now()),
                        templateId: templateId,
                        templateName: suggestion.template_display_name || suggestion.templateDisplayName || templateId || "未命名模板",
                        summary: suggestion.summary || "",
                        phase: targetStageId,
                        fileName: suggestion.file_name || suggestion.fileName || existingRule.fileName || "",
                        filePath: suggestion.file_path || suggestion.filePath || existingRule.filePath || "",
                        ready: !!(suggestion.is_ready),
                        termId: suggestion.term_id || suggestion.termId || "",
                        termName: suggestion.term_display_name || suggestion.termDisplayName || "",
                        category: suggestion.category || ""
                    }
                    updated = true
                    break
                }
            }

            if (!updated) {
                resolvedGroup.rules.push({
                    instanceId: "rule_" + Date.now() + "_" + Math.floor(Math.random() * 1000),
                    templateId: templateId,
                    templateName: suggestion.template_display_name || suggestion.templateDisplayName || templateId || "未命名模板",
                    summary: suggestion.summary || "",
                    phase: targetStageId,
                    fileName: suggestion.file_name || suggestion.fileName || "",
                    filePath: suggestion.file_path || suggestion.filePath || "",
                    ready: !!(suggestion.is_ready),
                    termId: suggestion.term_id || suggestion.termId || "",
                    termName: suggestion.term_display_name || suggestion.termDisplayName || "",
                    category: suggestion.category || ""
                })
            }

            root.ruleComposerStages = nextStages
            root.selectedRuleComposerStageId = targetStageId
            root.selectedRuleComposerGroupId = resolvedGroup.groupId || root.selectedRuleComposerGroupId
            syncRuleTemplateBindingPreviewState()
            return
        }
    }

    function removeRuleComposerInstance(stageId, groupId, instanceId) {
        var nextStages = cloneRuleComposerStages()
        for (var stageIndex = 0; stageIndex < nextStages.length; ++stageIndex) {
            var stage = nextStages[stageIndex]
            if (stage.stageId !== stageId || !Array.isArray(stage.groups)) {
                continue
            }
            for (var groupIndex = 0; groupIndex < stage.groups.length; ++groupIndex) {
                var group = stage.groups[groupIndex]
                if (group.groupId !== groupId || !Array.isArray(group.rules)) {
                    continue
                }
                group.rules = group.rules.filter(function(ruleItem) {
                    return (ruleItem.instanceId || "") !== instanceId
                })
                root.ruleComposerStages = nextStages
                syncRuleTemplateBindingPreviewState()
                syncDecoratedParameters()
                return
            }
        }
    }

    function updateRuleComposerGroup(stageId, groupId, patch) {
        var nextStages = cloneRuleComposerStages()
        for (var stageIndex = 0; stageIndex < nextStages.length; ++stageIndex) {
            var stage = nextStages[stageIndex]
            if (stage.stageId !== stageId || !Array.isArray(stage.groups)) {
                continue
            }
            for (var groupIndex = 0; groupIndex < stage.groups.length; ++groupIndex) {
                var group = stage.groups[groupIndex]
                if (group.groupId !== groupId) {
                    continue
                }
                for (var key in (patch || {})) {
                    if (patch[key] === undefined) {
                        continue
                    }
                    group[key] = patch[key]
                }
                if ((group.operator || "") !== "at_least") {
                    delete group.matchThreshold
                    delete group.groupMinMatchCount
                } else if (!(group.matchThreshold > 0)) {
                    group.matchThreshold = 1
                }
                if (group.matchThreshold > 0) {
                    group.groupMinMatchCount = group.matchThreshold
                }
                root.ruleComposerStages = nextStages
                syncRuleTemplateBindingPreviewState()
                syncDecoratedParameters()
                return
            }
        }
    }

    function moveRuleComposerInstance(stageId, groupId, instanceId, direction) {
        var nextStages = cloneRuleComposerStages()
        for (var stageIndex = 0; stageIndex < nextStages.length; ++stageIndex) {
            var stage = nextStages[stageIndex]
            if (stage.stageId !== stageId || !Array.isArray(stage.groups)) {
                continue
            }
            for (var groupIndex = 0; groupIndex < stage.groups.length; ++groupIndex) {
                var group = stage.groups[groupIndex]
                if (group.groupId !== groupId || !Array.isArray(group.rules)) {
                    continue
                }
                for (var ruleIndex = 0; ruleIndex < group.rules.length; ++ruleIndex) {
                    if ((group.rules[ruleIndex].instanceId || "") !== instanceId) {
                        continue
                    }
                    var nextIndex = ruleIndex + direction
                    if (nextIndex < 0 || nextIndex >= group.rules.length) {
                        return
                    }
                    var currentRule = group.rules[ruleIndex]
                    group.rules[ruleIndex] = group.rules[nextIndex]
                    group.rules[nextIndex] = currentRule
                    root.ruleComposerStages = nextStages
                    syncRuleTemplateBindingPreviewState()
                    syncDecoratedParameters()
                    return
                }
            }
        }
    }

    function removeRuleTemplateBinding(phase) {
        var key = normalizedRuleTemplatePhase(phase)
        var nextBindings = normalizeRuleTemplateBindings(boundRuleTemplateBindings)
        var nextEntries = normalizeRuleTemplateBindingEntries(boundRuleTemplateBindingEntries).filter(function(entry) {
            return normalizedRuleTemplatePhase(entry.phase) !== key
        })
        delete nextBindings[key]
        boundRuleTemplateBindingEntries = nextEntries
        boundRuleTemplateBindings = nextBindings
        rebuildRuleComposerState(false)
        syncDecoratedParameters()
    }

    function bindRuleTemplateSuggestion(suggestion, applyMode) {
        var mode = String(applyMode || "all").trim().toLowerCase()
        if (mode !== "all") {
            return
        }

        if (!suggestion) {
            boundRuleTemplateBindings = ({})
            syncDecoratedParameters()
            return
        }

        var targetLocation = resolveSuggestionTargetLocation(suggestion)
        var nextBinding = {
            template_id: suggestion.template_id || suggestion.templateId || "",
            template_display_name: suggestion.template_display_name || suggestion.templateDisplayName || "",
            term_id: suggestion.term_id || suggestion.termId || "",
            term_display_name: suggestion.term_display_name || suggestion.termDisplayName || "",
            file_name: suggestion.file_name || suggestion.fileName || "",
            file_path: suggestion.file_path || suggestion.filePath || "",
            phase: normalizedRuleTemplatePhase(targetLocation.stageId || suggestion.phase || "signal"),
            category: suggestion.category || "",
            summary: suggestion.summary || ""
        }
        var nextBindings = normalizeRuleTemplateBindings(boundRuleTemplateBindings)
        var nextEntries = normalizeRuleTemplateBindingEntries(boundRuleTemplateBindingEntries)
        nextBindings[nextBinding.phase] = nextBinding
        nextEntries.push(nextBinding)
        boundRuleTemplateBindingEntries = nextEntries
        boundRuleTemplateBindings = nextBindings
        upsertRuleComposerSuggestion(suggestion)
        syncDecoratedParameters()
    }

    function getAdvancedOptions() {
        function optionValue(currentIndex, values, fallbackValue) {
            if (currentIndex < 0 || currentIndex >= values.length) {
                return fallbackValue
            }
            return values[currentIndex]
        }

        return {
            enabled: !!root.enableAdvancedOptions,
            parameter_optimization_range: optionValue(parameterOptimizationRangeCombo.currentIndex, ["none", "small", "medium", "large"], "small"),
            sensitivity_analysis: optionValue(sensitivityAnalysisCombo.currentIndex, ["none", "basic", "detailed"], "basic"),
            parameter_constraints: optionValue(parameterConstraintsCombo.currentIndex, ["none", "linear", "nonlinear"], "none"),
            parameter_initialization_method: optionValue(parameterInitializationMethodCombo.currentIndex, ["random", "uniform", "empirical"], "random"),
            custom_parameter_script: customParameterScriptTextArea.text || ""
        }
    }

    function applyPersistedStrategy(strategyTypeIndex, parameters, advancedOptions) {
        var sourceParams = parameters || ({})
        var mappedValues = importedFactorContextPayload(sourceParams)
        var normalizedStrategyTypeIndex = Utils.StrategyCreationUtils.normalizeStrategyTypeIndex(strategyTypeIndex)
        var persistedRuleProfile = normalizeStructuredValue(sourceParams.rule_profile) || ({})
        var persistedExecutionPolicy = normalizeStructuredValue(sourceParams.execution_policy) || ({})
        var persistedBacktestAssumptions = normalizeStructuredValue(sourceParams.backtest_assumptions) || ({})

        function assignIfPresent(targetKey, sourceKeys, transform) {
            for (var index = 0; index < sourceKeys.length; ++index) {
                var key = sourceKeys[index]
                var resolvedValue = sourceParams[key]
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    resolvedValue = persistedRuleProfile[key]
                }
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    resolvedValue = persistedExecutionPolicy[key]
                }
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    resolvedValue = persistedBacktestAssumptions[key]
                }
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    if (key === "stopLoss") {
                        resolvedValue = persistedRuleProfile.stopLossPercent
                    } else if (key === "takeProfit") {
                        resolvedValue = persistedRuleProfile.takeProfitPercent
                    } else if (key === "rebalanceDays") {
                        resolvedValue = persistedRuleProfile.rebalanceDays
                    }
                }
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    continue
                }
                mappedValues[targetKey] = transform ? transform(resolvedValue) : resolvedValue
                return
            }
        }

        function ratioToPercent(value) {
            var numeric = Number(value)
            if (!isFinite(numeric)) {
                return value
            }
            return numeric <= 1 ? numeric * 100 : numeric
        }

        assignIfPresent("positionSize", ["positionSize"], ratioToPercent)
        assignIfPresent("stopLoss", ["stopLoss"], ratioToPercent)
        assignIfPresent("takeProfit", ["takeProfit"], ratioToPercent)
        assignIfPresent("maxDrawdownLimit", ["maxDrawdownLimit"], Number)
        assignIfPresent("rebalanceDays", ["rebalanceDays"], Number)

        if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.TrendFollowing) {
            assignIfPresent("fastPeriod", ["fastPeriod"], Number)
            assignIfPresent("slowPeriod", ["slowPeriod"], Number)
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.TrendBreakout) {
            assignIfPresent("longTrendPeriod", ["longTrendPeriod"], Number)
            assignIfPresent("breakoutLookbackPeriod", ["breakoutLookbackPeriod"], Number)
            assignIfPresent("breakoutThreshold", ["breakoutThreshold"], ratioToPercent)
            assignIfPresent("adxPeriod", ["adxPeriod"], Number)
            assignIfPresent("adxThreshold", ["adxThreshold"], Number)
            assignIfPresent("exitMaPeriod", ["exitMaPeriod"], Number)
            assignIfPresent("atrPeriod", ["atrPeriod"], Number)
            assignIfPresent("atrMultiplier", ["atrMultiplier"], Number)
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.MeanReversion) {
            assignIfPresent("bollPeriod", ["bollPeriod"], Number)
            assignIfPresent("bollStd", ["bollStd"], Number)
            assignIfPresent("reversionThreshold", ["reversionThreshold"], Number)
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.Momentum) {
            assignIfPresent("momentumPeriod", ["momentumPeriod"], Number)
            assignIfPresent("topN", ["topN"], Number)
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.Arbitrage) {
            assignIfPresent("spreadThreshold", ["spreadThreshold"], Number)
            assignIfPresent("entryZScore", ["entryZScore"], Number)
            assignIfPresent("exitZScore", ["exitZScore"], Number)
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.MachineLearning) {
            assignIfPresent("featureWindow", ["featureWindow"], Number)
            assignIfPresent("predictionDays", ["predictionDays"], Number)
            assignIfPresent("trainingDays", ["trainingDays"], Number)
            assignIfPresent("confidenceThreshold", ["confidenceThreshold"], ratioToPercent)
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.MultiFactor) {
            assignIfPresent("factorTypes", ["factorTypes"])
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.HighFrequency) {
            assignIfPresent("timeframe", ["timeframe"])
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.EventDriven) {
            assignIfPresent("eventTypes", ["eventTypes"])
        } else if (normalizedStrategyTypeIndex === Utils.StrategyCreationUtils.StrategyTypeIndex.Custom) {
            assignIfPresent("customCode", ["customCode"])
        }

        root.suppressRuleComposerReset = true
        root.selectedStrategyTypeIndex = normalizedStrategyTypeIndex
        loadParamConfigs()
        var persistedFactorOverlay = normalizeStructuredValue(sourceParams.factor_overlay) || ({})
        var persistedComposerState = normalizeStructuredValue(
            sourceParams.rule_composer_state
            || persistedRuleProfile.ruleComposerState
            || persistedRuleProfile.rule_composer_state
        )
            || ({})
        var persistedBindingEntries = normalizeRuleTemplateBindingEntries(
            sourceParams.rule_template_bindings
            || sourceParams.rule_template_binding
            || extractRuleTemplateBindingsFromComposerState(persistedComposerState)
            || ({})
        )
        boundRuleTemplateBindingEntries = persistedBindingEntries
        boundRuleTemplateBindings = normalizeRuleTemplateBindings(persistedBindingEntries)
        var persistedStrategyProfile = persistedRuleProfile.strategyProfile
            || persistedRuleProfile.strategy_profile
            || ({})

        console.log("applyPersistedStrategy:",
                "strategyTypeIndex=", normalizedStrategyTypeIndex,
                "bindingCount=", persistedBindingEntries.length,
                "hasComposerStages=", !!(persistedComposerState && persistedComposerState.stages),
                "composerStageCount=", (persistedComposerState && persistedComposerState.stages && persistedComposerState.stages.length) || 0,
                "overlayEnabled=", !!persistedFactorOverlay.enabled,
                "overlayAllocCount=", Array.isArray(persistedFactorOverlay.allocations) ? persistedFactorOverlay.allocations.length : 0)

        root.strategyProfile = Object.keys(persistedStrategyProfile).length > 0
            ? persistedStrategyProfile
            : Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        root.factorOverlay = normalizeFactorOverlay(persistedFactorOverlay, sourceParams)

        if (persistedComposerState && Array.isArray(persistedComposerState.stages) && persistedComposerState.stages.length > 0) {
            root.ruleComposerStages = normalizeStructuredValue(persistedComposerState.stages) || []
            var preferredSelection = firstPopulatedRuleComposerSelection(
                root.ruleComposerStages,
                persistedComposerState.selectedStageId || root.selectedRuleComposerStageId,
                persistedComposerState.selectedGroupId || root.selectedRuleComposerGroupId
            )
            root.selectedRuleComposerStageId = preferredSelection.stageId || root.selectedRuleComposerStageId
            root.selectedRuleComposerGroupId = preferredSelection.groupId || root.selectedRuleComposerGroupId
            ensureSelectedRuleComposerGroup()
        } else {
            rebuildRuleComposerState(false)
            var rebuiltSelection = firstPopulatedRuleComposerSelection(
                root.ruleComposerStages,
                root.selectedRuleComposerStageId,
                root.selectedRuleComposerGroupId
            )
            root.selectedRuleComposerStageId = rebuiltSelection.stageId || root.selectedRuleComposerStageId
            root.selectedRuleComposerGroupId = rebuiltSelection.groupId || root.selectedRuleComposerGroupId
            ensureSelectedRuleComposerGroup()
        }

                console.log("applyPersistedStrategy resolved:",
                    "stageCount=", root.ruleComposerStages.length,
                    "selectedStageId=", root.selectedRuleComposerStageId,
                    "selectedGroupId=", root.selectedRuleComposerGroupId,
                    "resolvedOverlayEnabled=", !!root.factorOverlay.enabled,
                    "resolvedOverlayAllocCount=", Array.isArray(root.factorOverlay.allocations) ? root.factorOverlay.allocations.length : 0)

        root.strategyParameters = decorateParameters(mappedValues)
        root.parametersChanged(root.strategyParameters)
        if (dynamicGenerator) {
            dynamicGenerator.setValues(mappedValues)
            root.parametersValid = dynamicGenerator.validateAll()
        }

        var options = advancedOptions || ({})
        root.enableAdvancedOptions = !!options.enabled
        if (parameterOptimizationRangeCombo) {
            parameterOptimizationRangeCombo.currentIndex = Math.max(0, ["none", "small", "medium", "large"].indexOf(options.parameter_optimization_range || "small"))
        }
        if (sensitivityAnalysisCombo) {
            sensitivityAnalysisCombo.currentIndex = Math.max(0, ["none", "basic", "detailed"].indexOf(options.sensitivity_analysis || "basic"))
        }
        if (parameterConstraintsCombo) {
            parameterConstraintsCombo.currentIndex = Math.max(0, ["none", "linear", "nonlinear"].indexOf(options.parameter_constraints || "none"))
        }
        if (parameterInitializationMethodCombo) {
            parameterInitializationMethodCombo.currentIndex = Math.max(0, ["random", "uniform", "empirical"].indexOf(options.parameter_initialization_method || "random"))
        }
        if (customParameterScriptTextArea) {
            customParameterScriptTextArea.text = options.custom_parameter_script || ""
        }
        Qt.callLater(function() {
            root.suppressRuleComposerReset = false
        })
        root.advancedOptionsChanged(root.enableAdvancedOptions)
        emitValidationState(currentParameterValidationErrors())
    }
    
    // 重置表单
    function reset() {
        if (dynamicGenerator) {
            dynamicGenerator.reset()
        }
        if (parameterOptimizationRangeCombo) parameterOptimizationRangeCombo.currentIndex = 1
        if (sensitivityAnalysisCombo) sensitivityAnalysisCombo.currentIndex = 1
        if (parameterConstraintsCombo) parameterConstraintsCombo.currentIndex = 0
        if (parameterInitializationMethodCombo) parameterInitializationMethodCombo.currentIndex = 0
        if (customParameterScriptTextArea) customParameterScriptTextArea.text = ""
        boundRuleTemplateBindings = ({})
        boundRuleTemplateBindingEntries = []
        root.strategyProfile = Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        root.factorOverlay = defaultFactorOverlay()
        rebuildRuleComposerState(false)
        root.strategyParameters = decorateParameters({})
        root.parametersValid = false
        root.enableAdvancedOptions = false
        emitValidationState({})
    }
    
    // 验证
    function isValid() {
        return root.parametersValid && root.ruleComposerConfigValid && root.factorOverlayErrors().length === 0 && Object.keys(root.strategyParameters).length > 0
    }
    
    // ============ 初始化和信号连接 ============
    
    Component.onCompleted: {
        // 注册参数组件
        paramComponents.registerAllComponents()

        if (!root.strategyProfile || Object.keys(root.strategyProfile).length === 0) {
            root.strategyProfile = Utils.StrategyCreationUtils.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        }
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        if (!Array.isArray(root.ruleComposerStages) || root.ruleComposerStages.length === 0) {
            rebuildRuleComposerState(true)
        }

        // 加载初始参数配置
        loadParamConfigs()
    }
    
    onSelectedStrategyTypeIndexChanged: {
        loadParamConfigs()
        if (root.suppressRuleComposerReset) {
            return
        }
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        rebuildRuleComposerState(true)
        syncDecoratedParameters()
    }

    onSelectedRuleComposerStageIdChanged: {
        ensureSelectedRuleComposerGroup()
        if (root.ruleComposerStages.length > 0) {
            syncDecoratedParameters()
        }
    }

    onSelectedRuleComposerGroupIdChanged: {
        if (root.ruleComposerStages.length > 0) {
            syncDecoratedParameters()
        }
    }
}
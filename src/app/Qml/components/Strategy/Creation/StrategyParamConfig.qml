// StrategyParamConfig.qml
// 策略参数配置组件 - 用于策略创建向导步骤2

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
// RuleTemplatePreviewUtils.js migrated to C++ StrategyBridge
import "../../FactorWorkbench/Creation/components" as PluginComponents
import "../../../utils/ColorLabelMaps.js" as ColorLabelMaps
import "../../../utils/PureUtils.js" as PureUtils
import "../../../utils/DomainConstants.js" as DomainConstants

Rectangle {
    id: root

    function clampWidth(minWidth, preferredWidth, maxWidth) { return PureUtils.clampWidth(minWidth, preferredWidth, maxWidth); }
    
    // ============ 属性 ============

    property int selectedStrategyTypeIndex: 0
    property var factorService: null
    property string strategyId: ""
    property var strategyParameters: ({})
    property var commonStrategyParameters: ({})
    property var personalizedStrategyParameters: ({})
    property var commonParameterConfigs: []
    property var personalizedParameterConfigs: []
    property var boundRuleTemplateBindings: ({})
    property var boundRuleTemplateBindingEntries: []
    property bool suppressRuleComposerReset: false
    property bool parametersValid: false
    property bool commonParametersValid: true
    property bool personalizedParametersValid: true
    property bool personalizedCardExpanded: true
    property bool enableAdvancedOptions: false
    property var strategyProfile: ({})
    property var ruleComposerStages: []
    property bool forbidDefaultRuleBuildInEdit: false
    property var ruleComposerValidation: ({ valid: true, errorCount: 0, warningCount: 0, errors: [], warnings: [], suggestions: [], groupIssues: ({}) })
    property string selectedRuleComposerStageId: "signal"
    property string selectedRuleComposerGroupId: ""
    readonly property var currentRuleComposerGroupQuickImportList: (
        typeof root.computeCurrentRuleComposerGroupQuickImportEntries === "function"
            ? (root.computeCurrentRuleComposerGroupQuickImportEntries() || [])
            : [])
    readonly property int selectedStrategyBehaviorKind: strategyService.strategyBehaviorKindFromTypeIndex(root.selectedStrategyTypeIndex)
    readonly property bool ruleComposerConfigValid: (root.ruleComposerValidation.errorCount || 0) === 0
    readonly property bool useNarrowRulePanels: width >= 1180
    readonly property bool useWideParamGrid: width >= 1200
    readonly property int parameterPaneMaxColumns: 3
    readonly property int parameterPaneMinColumnWidth: 260
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
    readonly property bool hasPersonalizedParameterConfigs: (root.personalizedParameterConfigs || []).length > 0
    property var factorOverlay: ({ enabled: false, targetPositionCount: 50, minimumCompositeScore: 0, combineMode: "rank_only", selectionScope: "rule_eligible", allocations: [] })
    property var factorSelectorDialog: null

    // ── 标的黑名单 ──
    property var blacklistSymbols: []
    property string blacklistInput: ""

    function reloadBlacklist() {
        var sid = String(root.strategyId || "")
        if (!sid) { blacklistSymbols = []; return }
        var raw = Bridge.StrategyBridge.getSymbolBlacklist(sid)
        blacklistSymbols = Array.isArray(raw) ? raw : []
    }
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

    // -- Bridge aliases: expose child component ids to existing JS code --
    property alias commonDynamicGenerator: commonParamCard.dynamicGenerator
    property alias personalizedDynamicGenerator: personalizedParamCard.dynamicGenerator
    property alias advancedParamsSwitch: advancedOptionsPanel.advancedParamsSwitch

    
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
            anchors.margins: 8
            
            // 参数配置标题
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                
                Text {
                    text: strategyService.tr('strategyCreation.step2Title')
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#f1f5f9"
                }
                
                Text {
                    text: strategyService.tr('strategyCreation.step2Description')
                    font.pixelSize: 12
                    color: "#94a3b8"
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: [
                            strategyService.tr('strategyCreation.commonParameters'),
                            strategyService.tr('strategyCreation.personalizedParameters')
                        ]

                        delegate: Rectangle {
                            radius: 8
                            color: "#172554"
                            border.width: 1
                            border.color: "#2563eb"
                            implicitHeight: 24
                            implicitWidth: tagLabel.implicitWidth + 14

                            Text {
                                id: tagLabel
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 11
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
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    radius: 8
                    color: "#1e293b"
                    border.width: 1
                    border.color: "#334155"
                    implicitHeight: parameterPanelLayout.implicitHeight + 18

                    ColumnLayout {
                        id: parameterPanelLayout
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Text {
                            text: strategyService.tr('strategyCreation.parameterConfigPanel')
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "#f1f5f9"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: strategyService.tr('strategyCreation.configuredParameters') + ": " +
                                      root.totalConfiguredParameterCount()
                                font.pixelSize: 11
                                color: "#94a3b8"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: root.parametersValid ?
                                      strategyService.tr('strategyCreation.parameterValidationPassed') :
                                      strategyService.tr('strategyCreation.parameterValidationRequired')
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                color: root.parametersValid ? "#10b981" : "#ef4444"
                            }
                        }
                    }
                }

                CommonParamCard {
                    id: commonParamCard
                    paramComponents: paramComponents
                    minColumnWidth: root.parameterPaneMinColumnWidth
                    maxColumns: root.parameterPaneMaxColumns
                    commonParameterConfigs: root.commonParameterConfigs
                    parametersValid: root.commonParametersValid
                    onParamsChanged: root.syncDecoratedParameters()
                    onValidationChanged: function(allValid) {
                        root.commonParametersValid = !!allValid
                        root.updateParameterValidationState()
                    }
                }

                PersonalizedParamCard {
                    id: personalizedParamCard
                    paramComponents: paramComponents
                    minColumnWidth: root.parameterPaneMinColumnWidth
                    maxColumns: root.parameterPaneMaxColumns
                    hasConfigs: root.hasPersonalizedParameterConfigs
                    cardExpanded: root.personalizedCardExpanded
                    personalizedParameterConfigs: root.personalizedParameterConfigs
                    parametersValid: root.personalizedParametersValid
                    onParamsChanged: root.syncDecoratedParameters()
                    onToggleExpand: root.personalizedCardExpanded = !root.personalizedCardExpanded
                    onValidationChanged: function(allValid) {
                        root.personalizedParametersValid = !!allValid
                        root.updateParameterValidationState()
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

                FactorOverlayPanel {
                    id: factorOverlayPanel
                    factorOverlayState: root.factorOverlay
                    cardMinWidth: root.factorOverlayCardMinWidth
                    cardMaxWidth: root.factorOverlayCardMaxWidth
                }


                // -- Blacklist --
                BlacklistPanel {
                    id: blacklistPanel
                }


                RuleComposerPanel {
                    id: ruleComposerPanel
                }


                AdvancedOptionsPanel {
                    id: advancedOptionsPanel
                }

        }
    }
    }
    
    // ============ 功能函数 ============
    
    // 加载参数配置
    function loadParamConfigs(initialValues) {
        var paramConfigs = strategyService.buildParamConfigs(root.selectedStrategyTypeIndex)
        var separatedConfigs = splitParameterConfigs(paramConfigs)
        root.commonParameterConfigs = separatedConfigs.common
        root.personalizedParameterConfigs = separatedConfigs.personalized
        if (commonDynamicGenerator) {
            commonDynamicGenerator.reloadConfigs(root.commonParameterConfigs, [], initialValues)
        }
        if (personalizedDynamicGenerator) {
            personalizedDynamicGenerator.reloadConfigs(root.personalizedParameterConfigs, [], initialValues)
        }
        root.personalizedCardExpanded = root.hasPersonalizedParameterConfigs
        root.commonParametersValid = true
        root.personalizedParametersValid = true
        updateParameterValidationState()
    }

    function splitParameterConfigs(paramConfigs) {
        var source = Array.isArray(paramConfigs) ? paramConfigs : []
        var common = []
        var personalized = []
        var commonCategory = strategyService.tr('strategyCreation.commonParameters')
        var personalizedCategory = strategyService.tr('strategyCreation.personalizedParameters')

        for (var index = 0; index < source.length; ++index) {
            var config = source[index]
            if (!config || typeof config !== "object") {
                continue
            }
            var category = String(config.category || "")
            if (category === commonCategory) {
                common.push(config)
            } else if (category === personalizedCategory) {
                personalized.push(config)
            } else {
                personalized.push(config)
            }
        }

        return {
            common: common,
            personalized: personalized
        }
    }

    function parameterValuesFromGenerator(generatorLike) {
        if (!generatorLike || typeof generatorLike.getValues !== "function") {
            return ({})
        }
        return generatorLike.getValues() || ({})
    }

    function totalConfiguredParameterCount() {
        var commonCount = commonDynamicGenerator && commonDynamicGenerator.configsList ? commonDynamicGenerator.configsList.length : 0
        var personalizedCount = personalizedDynamicGenerator && personalizedDynamicGenerator.configsList ? personalizedDynamicGenerator.configsList.length : 0
        return commonCount + personalizedCount
    }

    function extractValuesByConfigs(sourceValues, configs) {
        var source = sourceValues || ({})
        var extracted = ({})
        var configList = Array.isArray(configs) ? configs : []
        for (var index = 0; index < configList.length; ++index) {
            var config = configList[index]
            var key = String((config && config.id) || "").trim()
            if (!key || source[key] === undefined) {
                continue
            }
            extracted[key] = source[key]
        }
        return extracted
    }

    function updateParameterValidationState() {
        root.parametersValid = !!root.commonParametersValid && !!root.personalizedParametersValid
        emitValidationState(currentParameterValidationErrors())
    }

    function defaultFactorOverlay() { return DomainConstants.defaultFactorOverlay(); }

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

    function cloneValue(value) { return PureUtils.cloneValue(value); }

    function normalizedOverlayAllocation(rawAllocation) {
        var item = rawAllocation && typeof rawAllocation === "object" ? rawAllocation : ({})
        var factorId = String(item.factor_id || "").trim()
        if (!factorId) {
            return null
        }

        return {
            factor_id: factorId,
            display_name: String(item.display_name || root.factorDisplayName(factorId) || factorId),
            weight_percent: Number(item.weight_percent) || 0
        }
    }

    function normalizeFactorOverlay(rawOverlay, sourceParameters) {
        var overlay = normalizeStructuredValue(rawOverlay)
        overlay = overlay && typeof overlay === "object" ? cloneValue(overlay) : defaultFactorOverlay()
        var normalized = defaultFactorOverlay()
        normalized.targetPositionCount = Math.max(1, Number(overlay.targetPositionCount) || normalized.targetPositionCount)
        normalized.minimumCompositeScore = Number(overlay.minimumCompositeScore) || 0
        normalized.combineMode = String(overlay.combineMode || normalized.combineMode)
        normalized.selectionScope = String(overlay.selectionScope || normalized.selectionScope)

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
        console.log("[StrategyParamConfig] openFactorSelector called, factorService:", !!factorService)
        if (!factorService) {
            console.warn("[StrategyParamConfig] factorService is null/undefined, cannot open factor selector")
            return
        }

        if (factorService.initialize) {
            console.log("[StrategyParamConfig] initializing factorService...")
            factorService.initialize()
        }

        if (factorSelectorDialog) {
            factorSelectorDialog.destroy()
            factorSelectorDialog = null
        }

        var component = Qt.createComponent("../../FactorWorkbench/Backtest/FactorSelectorDialog.qml")
        console.log("[StrategyParamConfig] FactorSelectorDialog component status:", component.status, component.errorString())
        if (component.status !== Component.Ready) {
            console.warn("[StrategyParamConfig] 加载因子选择对话框失败:", component.errorString())
            return
        }

        factorSelectorDialog = component.createObject(root, {
            factorService: factorService,
            factorViewModel: factorService.getViewModel ? factorService.getViewModel() : null,
            requireSupportValidation: false,
            selectedFactorIds: (root.factorOverlay.allocations || []).map(function(item) { return item.factor_id || "" })
        })
        console.log("[StrategyParamConfig] factorSelectorDialog created:", !!factorSelectorDialog)
        if (!factorSelectorDialog) {
            console.warn("[StrategyParamConfig] factorSelectorDialog creation returned null")
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
        console.log("[StrategyParamConfig] factorSelectorDialog opened")
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

    function populatedRuleComposerGroups(stage) {
        var groups = Array.isArray(stage && stage.groups) ? stage.groups : []
        return groups.filter(function(group) {
            return Array.isArray(group && group.rules) && group.rules.length > 0
        })
    }

    function ensureSelectedRuleComposerGroup() {
        var stage = currentSelectedRuleComposerStage()
        var groups = Array.isArray(stage && stage.groups) ? stage.groups : []
        if (groups.length === 0) {
            root.selectedRuleComposerGroupId = ""
            return
        }

        var selectedGroupId = String(root.selectedRuleComposerGroupId || "").trim()
        for (var index = 0; index < groups.length; ++index) {
            var candidateId = String(groups[index] && groups[index].groupId || "").trim()
            if (candidateId && candidateId === selectedGroupId) {
                return
            }
        }

        root.selectedRuleComposerGroupId = String(groups[0] && groups[0].groupId || "").trim()
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

    function isPlainObject(value) { return PureUtils.isPlainObject(value); }

    function ensureStrategyProfile(profileCandidate) {
        if (isPlainObject(profileCandidate) && Object.keys(profileCandidate).length > 0) {
            return profileCandidate
        }

        try {
            var builtProfile = strategyService.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
            return isPlainObject(builtProfile) ? builtProfile : ({})
        } catch (error) {
            console.warn("buildDefaultStrategyProfile failed:", error)
            return ({})
        }
    }

    function safeBuildDefaultRuleComposerSkeleton(profile, bindings) {
        try {
            var stages = strategyService.buildDefaultRuleComposerSkeleton(profile, bindings || [])
            return Array.isArray(stages) ? stages : []
        } catch (error) {
            return []
        }
    }

    function safeBuildDefaultMarketRuleBindings(profile) {
        try {
            var entries = strategyService.buildDefaultMarketRuleBindings(profile)
            return Array.isArray(entries) ? entries : []
        } catch (error) {
            return []
        }
    }

    function safeBuildDefaultBaseRuleBindings(profile) {
        try {
            var entries = strategyService.buildDefaultBaseRuleBindings(profile)
            return Array.isArray(entries) ? entries : []
        } catch (error) {
            return []
        }
    }

    function defaultRuleComposerStages(forceBuild) {
        if (root.forbidDefaultRuleBuildInEdit && !forceBuild) {
            return []
        }
        var profile = ensureStrategyProfile(root.strategyProfile)
        return safeBuildDefaultRuleComposerSkeleton(profile, [])
    }

    function defaultRulePackEntries(forceBuild) {
        if (root.forbidDefaultRuleBuildInEdit && !forceBuild) {
            return []
        }
        var profile = ensureStrategyProfile(root.strategyProfile)
        var marketDefaults = safeBuildDefaultMarketRuleBindings(profile)
        var baseDefaults = safeBuildDefaultBaseRuleBindings(profile)
        if (!Array.isArray(marketDefaults)) {
            marketDefaults = []
        }
        if (!Array.isArray(baseDefaults)) {
            baseDefaults = []
        }
        return marketDefaults.concat(baseDefaults)
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

    function computeCurrentRuleComposerGroupQuickImportEntries() {
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
        var defaults = defaultRulePackEntries(false)
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
            templateId: entry.templateId || "",
            templateDisplayName: entry.templateDisplayName || entry.templateId || "",
            termId: entry.termId || "",
            termDisplayName: entry.termDisplayName || "",
            fileName: entry.fileName || "",
            stageId: currentStage.stageId || "signal",
            category: entry.category || "",
            summary: entry.summary || "",
            isReady: true
        })
        syncDecoratedParameters()
    }

    function restoreDefaultRuleComposerPack() {
        root.strategyProfile = ensureStrategyProfile(root.strategyProfile)

        var defaultStages = defaultRuleComposerStages(true)
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

        var defaults = defaultRuleComposerStages(true)
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
            stageId: normalizedRuleTemplatePhase((ruleData && ruleData.stageId) || (stageData && stageData.stageId) || "signal")
        }

        if (ruleData && ruleData.fileName) {
            binding.fileName = ruleData.fileName
        }
        if (ruleData && ruleData.filePath) {
            binding.filePath = ruleData.filePath
        }
        if (ruleData && ruleData.templateId) {
            binding.templateId = ruleData.templateId
        }
        if (ruleData && ruleData.templateName) {
            binding.templateDisplayName = ruleData.templateName
        }
        if (ruleData && ruleData.summary) {
            binding.summary = ruleData.summary
        }
        if (ruleData && ruleData.category) {
            binding.category = ruleData.category
        }
        if (ruleData && ruleData.termId) {
            binding.termId = ruleData.termId
        }
        if (ruleData && ruleData.termName) {
            binding.termDisplayName = ruleData.termName
        }
        if (ruleData && ruleData.defaultInjected) {
            binding.defaultInjected = true
        }
        if (groupData && groupData.groupId) {
            binding.groupId = groupData.groupId
        }
        if (groupData && groupData.title) {
            binding.groupTitle = groupData.title
        }
        if (groupData && groupData.role) {
            binding.groupRole = groupData.role
        }
        if (groupData && groupData.operator) {
            binding.groupOperator = groupData.operator
        }
        var groupMinMatchCount = Number(groupData && (groupData.groupMinMatchCount || groupData.matchThreshold || 0))
        if (groupMinMatchCount > 0) {
            binding.groupMinMatchCount = groupMinMatchCount
        }
        return binding
    }

    function rebuildRuleComposerState(resetProfile) {
        if (resetProfile) {
            root.strategyProfile = ensureStrategyProfile(null)
        } else {
            root.strategyProfile = ensureStrategyProfile(root.strategyProfile)
        }
        root.ruleComposerStages = safeBuildDefaultRuleComposerSkeleton(
            root.strategyProfile,
            root.boundRuleTemplateBindingList())
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

    function defaultMarketRuleComposerStage(forceBuild) {
        if (root.forbidDefaultRuleBuildInEdit && !forceBuild) {
            return null
        }
        var profile = ensureStrategyProfile(root.strategyProfile)
        var defaultStages = safeBuildDefaultRuleComposerSkeleton(profile, [])
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
        var defaultStage = defaultMarketRuleComposerStage(false)
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
            targetStage = defaultMarketRuleComposerStage(false)
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
        root.strategyProfile = ensureStrategyProfile(root.strategyProfile)

        var nextStages = cloneRuleComposerStages()
        var defaultMarketStage = defaultMarketRuleComposerStage(true)
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
            if (key === "rule_profile"
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
        var preservedValues = extractEditableParameterValues(root.strategyParameters)
        var commonValues = parameterValuesFromGenerator(commonDynamicGenerator)
        var personalizedValues = parameterValuesFromGenerator(personalizedDynamicGenerator)

        for (var commonKey in commonValues) {
            preservedValues[commonKey] = commonValues[commonKey]
        }
        for (var personalizedKey in personalizedValues) {
            preservedValues[personalizedKey] = personalizedValues[personalizedKey]
        }
        return preservedValues
    }

    function resolvedRuleTemplateFileName(ruleLike) {
        var directFileName = String((ruleLike && ruleLike.fileName) || "").trim()
        if (directFileName) {
            return directFileName
        }
        return strategyService.resolveRuleTemplateFileName(
            ruleLike && (ruleLike.templateId || "")
        )
    }

    function supportedRuleBindingPhaseIndex(value) { return DomainConstants.supportedRuleBindingPhaseIndex(value); }

    function composerStagePhaseIndex(stageId) { return DomainConstants.composerStagePhaseIndex(stageId); }

    function composerRulePhaseIndex(rule, stageId) {
        var configured = supportedRuleBindingPhaseIndex(rule && (rule.bindingPhase !== undefined ? rule.bindingPhase : rule.phase))
        if (configured >= 0) {
            return configured
        }

        var tokens = [
            rule && rule.templateId,
            rule && rule.fileName,
            rule && rule.category,
            rule && rule.termId,
            rule && rule.termName,
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

        if (source.stopLossPercent !== undefined && source.stopLossPercent !== null && source.stopLossPercent !== "") {
            payload.stopLossPercent = Number(source.stopLossPercent)
        }
        if (source.takeProfitPercent !== undefined && source.takeProfitPercent !== null && source.takeProfitPercent !== "") {
            payload.takeProfitPercent = Number(source.takeProfitPercent)
        }
        if (source.rebalanceDays !== undefined && source.rebalanceDays !== null && source.rebalanceDays !== "") {
            payload.rebalanceDays = Number(source.rebalanceDays)
        }
        if (source.maxDrawdownLimit !== undefined && source.maxDrawdownLimit !== null && source.maxDrawdownLimit !== "") {
            payload.maxDrawdownLimit = Number(source.maxDrawdownLimit)
        }

        return payload
    }

    function buildExecutionPolicyPayload(sourceParameters) { return DomainConstants.buildExecutionPolicyPayload(sourceParameters); }

    function buildBacktestAssumptionsPayload(sourceParameters) { return DomainConstants.buildBacktestAssumptionsPayload(sourceParameters); }

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
        root.commonStrategyParameters = extractValuesByConfigs(mergedValues, root.commonParameterConfigs)
        root.personalizedStrategyParameters = extractValuesByConfigs(mergedValues, root.personalizedParameterConfigs)
        root.parametersChanged(mergedValues)
        emitValidationState(currentParameterValidationErrors())
        return mergedValues
    }

    function currentParameterValidationErrors() {
        var combinedErrors = ({})
        var commonErrors = commonDynamicGenerator && commonDynamicGenerator.validationErrors ? commonDynamicGenerator.validationErrors : ({})
        var personalizedErrors = personalizedDynamicGenerator && personalizedDynamicGenerator.validationErrors ? personalizedDynamicGenerator.validationErrors : ({})
        for (var key in commonErrors) {
            combinedErrors[key] = commonErrors[key]
        }
        for (var personalizedKey in personalizedErrors) {
            if (combinedErrors[personalizedKey] === undefined) {
                combinedErrors[personalizedKey] = personalizedErrors[personalizedKey]
            } else {
                combinedErrors["personalized_" + personalizedKey] = personalizedErrors[personalizedKey]
            }
        }
        return combinedErrors
    }

    function refreshRuleComposerValidation() {
        root.ruleComposerValidation = strategyService.validateRuleComposerConfiguration(
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
        merged.rule_composer_state = buildRuleComposerStatePayload()
        merged.rule_profile = buildRuleProfilePayload(source)
        merged.execution_policy = buildExecutionPolicyPayload(source)
        merged.backtest_assumptions = buildBacktestAssumptionsPayload(source)
        var normalizedFactorOverlay = normalizeFactorOverlay(root.factorOverlay, merged)
        if (normalizedFactorOverlay.enabled && normalizedFactorOverlay.allocations.length > 0) {
            merged.factor_overlay = normalizedFactorOverlay
            // 同时提取顶层 factorIds — C++ readFactorIds 读的是顶层 key
            merged.factorIds = normalizedFactorOverlay.allocations.map(function(a) { return String(a.factor_id || "").trim() })
        } else {
            delete merged.factor_overlay
            delete merged.factorIds
        }
        delete merged.stopLoss
        delete merged.takeProfit
        delete merged.positionSize
        delete merged.rebalanceDays
        delete merged.maxDrawdownLimit
        delete merged.longTrendPeriod
        delete merged.breakoutLookbackPeriod
        delete merged.breakoutThreshold
        delete merged.adxPeriod
        delete merged.adxThreshold
        delete merged.exitMaPeriod
        delete merged.atrMultiplier
        delete merged.bollPeriod
        delete merged.bollStd
        delete merged.reversionThreshold
        delete merged.momentumPeriod
        delete merged.spreadThreshold
        delete merged.featureWindow
        delete merged.predictionDays
        delete merged.trainingDays
        delete merged.confidenceThreshold
        delete merged.factorTypes
        delete merged.eventTypes
        delete merged.timeframe
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

        var normalized = strategyService.normalizePhaseKey(rawPhase)
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

    function roleDisplayName(role) { return ColorLabelMaps.roleDisplayName(role); }

    function operatorDisplayName(operatorValue) { return ColorLabelMaps.operatorDisplayName(operatorValue); }

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
            var hasTypedBindingFields = structuredValue.templateId || structuredValue.fileName
            if (hasTypedBindingFields) {
                normalized.push(normalizeStructuredValue(structuredValue))
                return normalized
            }

            for (var key in structuredValue) {
                var entry = normalizeStructuredValue(structuredValue[key])
                if (!entry || typeof entry !== "object") {
                    continue
                }
                if (!entry.stageId) {
                    entry.stageId = key
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
                var stageId = normalizedRuleTemplatePhase(item.stageId)
                if (!stageId) {
                    continue
                }
                if (!normalized[stageId] || Object.keys(normalized[stageId]).length === 0) {
                    normalized[stageId] = item
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
                        stageId: normalizedRuleTemplatePhase(rule.stageId || stageId)
                    }

                    if (fileName) {
                        binding.fileName = fileName
                    }
                    if (filePath) {
                        binding.filePath = filePath
                    }
                    if (templateId) {
                        binding.templateId = templateId
                    }

                    var templateName = String(rule.templateName || "").trim()
                    if (templateName) {
                        binding.templateDisplayName = templateName
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
                        binding.termId = termId
                    }

                    var termName = String(rule.termName || "").trim()
                    if (termName) {
                        binding.termDisplayName = termName
                    }
                    if (rule.defaultInjected) {
                        binding.defaultInjected = true
                    }

                    if (groupId) {
                        binding.groupId = groupId
                    }
                    if (groupTitle) {
                        binding.groupTitle = groupTitle
                    }
                    if (groupRole) {
                        binding.groupRole = groupRole
                    }
                    if (groupOperator) {
                        binding.groupOperator = groupOperator
                    }
                    if (groupMinMatchCount > 0) {
                        binding.groupMinMatchCount = groupMinMatchCount
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
        var stageIds = ["market", "eligibility", "signal", "portfolio", "rebalance", "execution", "account_risk"]
        for (var index = 0; index < stageIds.length; ++index) {
            var stageId = stageIds[index]
            var binding = (boundRuleTemplateBindings || ({}))[stageId]
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
            var stageId = normalizedRuleTemplatePhase(bindings[index].stageId)
            if (stageId === "signal") {
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
        var fallbackStageId = normalizedRuleTemplatePhase((suggestion && suggestion.stageId) || preferredStageId || "signal")

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
        var templateId = suggestion.templateId || ""

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
                        templateName: suggestion.templateDisplayName || templateId || "未命名模板",
                        summary: suggestion.summary || "",
                        stageId: targetStageId,
                        fileName: suggestion.fileName || existingRule.fileName || "",
                        filePath: suggestion.filePath || existingRule.filePath || "",
                        ready: !!suggestion.isReady,
                        termId: suggestion.termId || "",
                        termName: suggestion.termDisplayName || "",
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
                    templateName: suggestion.templateDisplayName || templateId || "未命名模板",
                    summary: suggestion.summary || "",
                    stageId: targetStageId,
                    fileName: suggestion.fileName || "",
                    filePath: suggestion.filePath || "",
                    ready: !!suggestion.isReady,
                    termId: suggestion.termId || "",
                    termName: suggestion.termDisplayName || "",
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

    function countStageRules(stageData) { return PureUtils.countStageRules(stageData); }

    function selectRuleComposerStage(stageId) {
        root.selectedRuleComposerStageId = stageId
        root.ensureSelectedRuleComposerGroup()
    }

    // ── 阶段增删 ──
    readonly property var availableRuleStages: [
        { stageId: "market", title: "市场", accentColor: "#f59e0b" },
        { stageId: "eligibility", title: "入场过滤", accentColor: "#10b981" },
        { stageId: "signal", title: "入场信号", accentColor: "#3b82f6" },
        { stageId: "rebalance", title: "出场", accentColor: "#ef4444" },
        { stageId: "portfolio", title: "组合", accentColor: "#8b5cf6" }
    ]

    function addRuleComposerStage(stageId) {
        var nextStages = cloneRuleComposerStages()
        var def = root.availableRuleStages.find(function(s) { return s.stageId === stageId })
        if (!def) return
        nextStages.push({
            stageId: def.stageId, title: def.title, accentColor: def.accentColor,
            groups: [{ groupId: def.stageId + "_core", title: "默认组", role: "must_pass", operator: "any", rules: [] }]
        })
        root.ruleComposerStages = nextStages
        root.selectedRuleComposerStageId = def.stageId
        root.ensureSelectedRuleComposerGroup()
        syncDecoratedParameters()
    }

    function removeRuleComposerStage(stageId) {
        var nextStages = cloneRuleComposerStages().filter(function(s) { return s.stageId !== stageId })
        if (nextStages.length === 0) return
        root.ruleComposerStages = nextStages
        if (root.selectedRuleComposerStageId === stageId)
            root.selectedRuleComposerStageId = nextStages[0].stageId
        root.ensureSelectedRuleComposerGroup()
        syncDecoratedParameters()
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

    function removeRuleTemplateBinding(stageId) {
        var key = normalizedRuleTemplatePhase(stageId)
        var nextBindings = normalizeRuleTemplateBindings(boundRuleTemplateBindings)
        var nextEntries = normalizeRuleTemplateBindingEntries(boundRuleTemplateBindingEntries).filter(function(entry) {
            return normalizedRuleTemplatePhase(entry.stageId) !== key
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
            templateId: suggestion.templateId || "",
            templateDisplayName: suggestion.templateDisplayName || "",
            termId: suggestion.termId || "",
            termDisplayName: suggestion.termDisplayName || "",
            fileName: suggestion.fileName || "",
            filePath: suggestion.filePath || "",
            stageId: normalizedRuleTemplatePhase(targetLocation.stageId || suggestion.stageId || "signal"),
            category: suggestion.category || "",
            summary: suggestion.summary || "",
            rules: suggestion.rules || [],
            meta: {
                name: suggestion.templateDisplayName || suggestion.templateName || "",
                description: suggestion.summary || ""
            }
        }
        var nextBindings = normalizeRuleTemplateBindings(boundRuleTemplateBindings)
        var nextEntries = normalizeRuleTemplateBindingEntries(boundRuleTemplateBindingEntries)
        nextBindings[nextBinding.stageId] = nextBinding
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
        if (!sourceParams || typeof sourceParams !== "object" || Array.isArray(sourceParams)) {
            throw new Error("编辑参数必须为对象，且符合新字段合同")
        }
        if (sourceParams.rule_template_bindings !== undefined
                && sourceParams.rule_template_bindings !== null
                && sourceParams.rule_template_bindings !== "") {
            throw new Error("检测到旧字段 rule_template_bindings，当前仅支持 rule_composer_state 新结构")
        }
        if (sourceParams.rule_profile === undefined || sourceParams.rule_profile === null || sourceParams.rule_profile === "") {
            throw new Error("编辑参数缺少 rule_profile，当前仅支持新字段合同")
        }
        if (sourceParams.rule_composer_state === undefined || sourceParams.rule_composer_state === null || sourceParams.rule_composer_state === "") {
            throw new Error("编辑参数缺少 rule_composer_state，当前仅支持新字段合同")
        }
        var mappedValues = importedFactorContextPayload(sourceParams)
        var normalizedStrategyTypeIndex = strategyService.normalizeStrategyTypeIndex(strategyTypeIndex)
        var persistedRuleProfile = normalizeStructuredValue(sourceParams.rule_profile) || ({})
        var persistedComposerState = normalizeStructuredValue(sourceParams.rule_composer_state) || ({})
        if (!Array.isArray(persistedComposerState.stages) || persistedComposerState.stages.length === 0) {
            throw new Error("rule_composer_state.stages 缺失或为空，无法进入编辑态")
        }

        function assignIfPresent(targetKey, sourceKeys, transform) {
            for (var index = 0; index < sourceKeys.length; ++index) {
                var key = sourceKeys[index]
                var resolvedValue = sourceParams[key]
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    resolvedValue = persistedRuleProfile[key]
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

        assignIfPresent("allowShort", ["allowShort"], Boolean)
        assignIfPresent("maxPositions", ["maxPositions"], Number)
        assignIfPresent("maxWeightPerStock", ["maxWeightPerStock"], Number)
        assignIfPresent("minWeightPerStock", ["minWeightPerStock"], Number)
        assignIfPresent("weightScheme", ["weightScheme"], Number)
        assignIfPresent("rebalanceFrequency", ["rebalanceFrequency"], Number)

        if (normalizedStrategyTypeIndex === 0) {
            assignIfPresent("fastPeriod", ["fastPeriod"], Number)
            assignIfPresent("slowPeriod", ["slowPeriod"], Number)
            assignIfPresent("priceField", ["priceField"])
        } else if (normalizedStrategyTypeIndex === 1) {
            assignIfPresent("channelPeriod", ["channelPeriod"], Number)
            assignIfPresent("breakoutMultiplier", ["breakoutMultiplier"], Number)
            assignIfPresent("atrPeriod", ["atrPeriod"], Number)
        } else if (normalizedStrategyTypeIndex === 2) {
            assignIfPresent("period", ["period"], Number)
            assignIfPresent("standardDeviationMultiplier", ["standardDeviationMultiplier"], Number)
            assignIfPresent("entryThreshold", ["entryThreshold"], Number)
            assignIfPresent("exitThreshold", ["exitThreshold"], Number)
        } else if (normalizedStrategyTypeIndex === 3) {
            assignIfPresent("period", ["period"], Number)
            assignIfPresent("oversoldLevel", ["oversoldLevel"], Number)
            assignIfPresent("overboughtLevel", ["overboughtLevel"], Number)
        } else if (normalizedStrategyTypeIndex === 4 || normalizedStrategyTypeIndex === 6) {
            assignIfPresent("factorWeights", ["factorWeights"])
            assignIfPresent("topN", ["topN"], Number)
            assignIfPresent("industryNeutral", ["industryNeutral"], Boolean)
        } else if (normalizedStrategyTypeIndex === 5) {
            assignIfPresent("surpriseThreshold", ["surpriseThreshold"], Number)
            assignIfPresent("holdDays", ["holdDays"], Number)
            assignIfPresent("eventSources", ["eventSources"])
        } else if (normalizedStrategyTypeIndex === 6) {
            assignIfPresent("tradingPair", ["tradingPair"])
            assignIfPresent("hedgeRatio", ["hedgeRatio"], Number)
            assignIfPresent("lookback", ["lookback"], Number)
            assignIfPresent("entryZScore", ["entryZScore"], Number)
            assignIfPresent("exitZScore", ["exitZScore"], Number)
        } else if (normalizedStrategyTypeIndex === 7) {
            assignIfPresent("assets", ["assets"])
            assignIfPresent("volatilityLookback", ["volatilityLookback"], Number)
            assignIfPresent("targetVolatility", ["targetVolatility"], Number)
        } else if (normalizedStrategyTypeIndex === 8) {
            assignIfPresent("modelId", ["modelId"], Number)
            assignIfPresent("featureIds", ["featureIds"])
            assignIfPresent("topN", ["topN"], Number)
        } else if (normalizedStrategyTypeIndex === 9) {
            assignIfPresent("depthLevels", ["depthLevels"], Number)
            assignIfPresent("imbalanceThreshold", ["imbalanceThreshold"], Number)
            assignIfPresent("maxHoldSeconds", ["maxHoldSeconds"], Number)
        } else if (normalizedStrategyTypeIndex === 10) {
            assignIfPresent("underlying", ["underlying"])
            assignIfPresent("optionChainFilter", ["optionChainFilter"])
            assignIfPresent("historicalVolatilityWindow", ["historicalVolatilityWindow"], Number)
            assignIfPresent("entrySpreadUpper", ["entrySpreadUpper"], Number)
            assignIfPresent("entrySpreadLower", ["entrySpreadLower"], Number)
            assignIfPresent("deltaNeutral", ["deltaNeutral"], Boolean)
        } else if (normalizedStrategyTypeIndex === 9) {
            assignIfPresent("customCode", ["customCode"])
        }

        root.suppressRuleComposerReset = true
        root.selectedStrategyTypeIndex = normalizedStrategyTypeIndex
        if (commonDynamicGenerator) {
            commonDynamicGenerator.setValues(mappedValues)
        }
        if (personalizedDynamicGenerator) {
            personalizedDynamicGenerator.setValues(mappedValues)
        }
        var persistedFactorOverlay = normalizeStructuredValue(sourceParams.factor_overlay) || ({})
        var hasPersistedComposerStages = true
        var persistedBindingEntries = normalizeRuleTemplateBindingEntries(
            extractRuleTemplateBindingsFromComposerState(persistedComposerState)
            || ({})
        )
        boundRuleTemplateBindingEntries = persistedBindingEntries
        boundRuleTemplateBindings = normalizeRuleTemplateBindings(persistedBindingEntries)
        var persistedStrategyProfile = persistedRuleProfile.strategyProfile
            || ({})

        console.log("applyPersistedStrategy:",
                "strategyTypeIndex=", normalizedStrategyTypeIndex,
                "bindingCount=", persistedBindingEntries.length,
                "hasComposerStages=", hasPersistedComposerStages,
                "composerStageCount=", (persistedComposerState && persistedComposerState.stages && persistedComposerState.stages.length) || 0,
                "forbidDefaultRuleBuildInEdit=", hasPersistedComposerStages,
                "overlayEnabled=", !!persistedFactorOverlay.enabled,
                "overlayAllocCount=", Array.isArray(persistedFactorOverlay.allocations) ? persistedFactorOverlay.allocations.length : 0)

        if (!isPlainObject(persistedStrategyProfile) || Object.keys(persistedStrategyProfile).length === 0) {
            throw new Error("rule_profile.strategyProfile 缺失，无法进入编辑态")
        }
        root.strategyProfile = persistedStrategyProfile
        root.factorOverlay = normalizeFactorOverlay(persistedFactorOverlay, sourceParams)

        root.forbidDefaultRuleBuildInEdit = true
        root.ruleComposerStages = normalizeStructuredValue(persistedComposerState.stages) || []
        var preferredSelection = firstPopulatedRuleComposerSelection(
            root.ruleComposerStages,
            persistedComposerState.selectedStageId || root.selectedRuleComposerStageId,
            persistedComposerState.selectedGroupId || root.selectedRuleComposerGroupId
        )
        root.selectedRuleComposerStageId = preferredSelection.stageId || root.selectedRuleComposerStageId
        root.selectedRuleComposerGroupId = preferredSelection.groupId || root.selectedRuleComposerGroupId
        ensureSelectedRuleComposerGroup()

                console.log("applyPersistedStrategy resolved:",
                    "stageCount=", root.ruleComposerStages.length,
                    "selectedStageId=", root.selectedRuleComposerStageId,
                    "selectedGroupId=", root.selectedRuleComposerGroupId,
                    "resolvedOverlayEnabled=", !!root.factorOverlay.enabled,
                    "resolvedOverlayAllocCount=", Array.isArray(root.factorOverlay.allocations) ? root.factorOverlay.allocations.length : 0)

        root.strategyParameters = decorateParameters(mappedValues)
        root.parametersChanged(root.strategyParameters)
        // 读取凯利仓位并更新单票上限滑块
        var fk = Number(sourceParams.fullKelly || 0)
        var hk = Number(sourceParams.halfKelly || 0)
        if (hk > 0) root.updateMaxWeightByKelly(fk, hk)
        if (commonDynamicGenerator) {
            commonDynamicGenerator.setValues(mappedValues)
            root.commonParametersValid = commonDynamicGenerator.validateAll()
        }
        if (personalizedDynamicGenerator) {
            personalizedDynamicGenerator.setValues(mappedValues)
            root.personalizedParametersValid = personalizedDynamicGenerator.validateAll()
        }
        syncDecoratedParameters(mappedValues)
        updateParameterValidationState()

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
            // 重新加载当前策略类型的参数（切换类型时被 suppress 跳过了）
            loadParamConfigs({})
        })
        root.advancedOptionsChanged(root.enableAdvancedOptions)
        emitValidationState(currentParameterValidationErrors())
    }
    
    // ── 凯利仓位更新(仅改默认值, 不改上下限) ──
    function updateMaxWeightByKelly(fullKelly, halfKelly) {
        var hk = Number(halfKelly) || 0
        if (hk <= 0) return
        hk = Math.max(0.0, Math.min(0.50, hk))
        for (var i = 0; i < root.commonParameterConfigs.length; i++) {
            if (root.commonParameterConfigs[i].id === "maxWeightPerStock") {
                root.commonParameterConfigs[i].default = hk
                if (commonDynamicGenerator) commonDynamicGenerator.reloadConfigs(root.commonParameterConfigs, [], {})
                break
            }
        }
    }

    // 重置表单
    function reset() {
        if (commonDynamicGenerator) {
            commonDynamicGenerator.reset()
        }
        if (personalizedDynamicGenerator) {
            personalizedDynamicGenerator.reset()
        }
        if (parameterOptimizationRangeCombo) parameterOptimizationRangeCombo.currentIndex = 1
        if (sensitivityAnalysisCombo) sensitivityAnalysisCombo.currentIndex = 1
        if (parameterConstraintsCombo) parameterConstraintsCombo.currentIndex = 0
        if (parameterInitializationMethodCombo) parameterInitializationMethodCombo.currentIndex = 0
        if (customParameterScriptTextArea) customParameterScriptTextArea.text = ""
        boundRuleTemplateBindings = ({})
        boundRuleTemplateBindingEntries = []
        root.forbidDefaultRuleBuildInEdit = false
        root.strategyProfile = strategyService.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        root.factorOverlay = defaultFactorOverlay()
        rebuildRuleComposerState(false)
        root.strategyParameters = decorateParameters({})
        root.commonStrategyParameters = ({})
        root.personalizedStrategyParameters = ({})
        root.parametersValid = false
        root.commonParametersValid = true
        root.personalizedParametersValid = true
        root.enableAdvancedOptions = false
        emitValidationState({})
    }
    
    // 验证
    function isValid() {
        return root.parametersValid && root.ruleComposerConfigValid && root.factorOverlayErrors().length === 0 && Object.keys(root.strategyParameters).length > 0
    }
    
    // ============ 初始化和信号连接 ============
    
    // 机器学习/多因子策略自动启用因子覆盖层
    function autoEnableFactorOverlayIfNeeded() {
        if (root.selectedStrategyBehaviorKind === 4     // MultiFactor
            || root.selectedStrategyBehaviorKind === 5) { // MachineLearning
            if (!root.factorOverlay.enabled) {
                root.factorOverlay.enabled = true
                root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
            }
        }
    }

    Component.onCompleted: {
        // 注册参数组件
        paramComponents.registerAllComponents()

        if (!root.strategyProfile || Object.keys(root.strategyProfile).length === 0) {
            root.strategyProfile = strategyService.buildDefaultStrategyProfile(root.selectedStrategyTypeIndex)
        }
        autoEnableFactorOverlayIfNeeded()
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        if (!Array.isArray(root.ruleComposerStages) || root.ruleComposerStages.length === 0) {
            rebuildRuleComposerState(true)
        }

        // 加载初始参数配置 (新建策略无已保存值, 传空对象用默认值)
        loadParamConfigs({})
    }

    onSelectedStrategyTypeIndexChanged: {
        loadParamConfigs({})
        root.forbidDefaultRuleBuildInEdit = false
        autoEnableFactorOverlayIfNeeded()
        root.factorOverlay = normalizeFactorOverlay(root.factorOverlay)
        if (!root.suppressRuleComposerReset) {
            rebuildRuleComposerState(true)
        }
        syncDecoratedParameters()
    }

    onSelectedRuleComposerStageIdChanged: {
        ensureSelectedRuleComposerGroup()
        if (root.ruleComposerStages.length > 0) {
            syncDecoratedParameters()
        }
        reloadBlacklist()
    }

    onSelectedRuleComposerGroupIdChanged: {
        if (root.ruleComposerStages.length > 0) {
            syncDecoratedParameters()
        }
    }

}
// StrategyCreationPagePro.qml
// 专业策略创建页面 - 优化重构版本
// 使用组件化架构，支持多语言、表单校验和步骤切换动画

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import AStock.Bridge 1.0  // 导入C++桥接模块
import "../../utils/StrategyCreationUtils.js" as Utils
import "../../components/Strategy/Creation" as StrategyComponents

Page {
    id: root
    
    // ============ 页面属性 ============
    
    property alias currentStep: stepIndicator.currentStep
    property bool isCreating: false
    property string creationStatus: ""
    property bool isEditMode: false
    property string editingStrategyId: ""
    
    // 信号
    signal backClicked()
    signal requestBacktest(string strategyId, string strategyName, var backtestConfig)
    
    // 数据容器
    property string selectedStrategyType: "trend_following"
    property string strategyName: ""
    property string strategyDescription: ""
    property string assetType: "stock"
    property string timeFrame: "daily"
    property string riskLevel: "medium"
    property string optimizationMethod: "genetic"
    property var strategyTags: []
    
    property var strategyParameters: ({})
    property bool parametersValid: false
    property bool enableAdvancedOptions: false
    
    // C++服务引用
    property var strategyService: StrategyService
    property var factorService: FactorService
    
    // ============ 主布局 ============
    
    background: Rectangle {
        color: "#0f172a"
    }
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        
        // 步骤内容区域
        Rectangle {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            
            StackLayout {
                id: stepStack
                anchors.fill: parent
                anchors.margins: 14
                currentIndex: stepIndicator.currentStep - 1
                
                // 步骤1: 策略类型与基本信息
                Rectangle {
                    id: step1Content
                    readonly property real selectorPanelWidth: width >= 1560 ? 320 : (width >= 1320 ? 280 : 236)
                    readonly property bool useWideBasicInfoLayout: width >= 1320
                    color: "transparent"
                    
                    RowLayout {
                        anchors.fill: parent
                        spacing: 14
                        
                        // 左侧: 策略类型选择
                        StrategyComponents.StrategyTypeSelector {
                            id: strategyTypeSelector
                            Layout.fillHeight: true
                            Layout.preferredWidth: step1Content.selectorPanelWidth
                            Layout.minimumWidth: step1Content.selectorPanelWidth
                            Layout.maximumWidth: step1Content.selectorPanelWidth
                            
                            onStrategyTypeChanged: function(strategyType) {
                                root.selectedStrategyType = strategyType
                                strategyBasicInfo.applyStrategyTypeDefaults(strategyType, false)
                            }
                        }
                        
                        // 右侧: 策略基本信息
                        StrategyComponents.StrategyBasicInfo {
                            id: strategyBasicInfo
                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            selectedStrategyType: root.selectedStrategyType
                            useWideCardLayout: step1Content.useWideBasicInfoLayout
                            
                            onValidationChanged: function(isValid) {
                                step1Valid = isValid
                            }
                        }
                    }
                }
                
                // 步骤2: 参数配置
                StrategyComponents.StrategyParamConfig {
                    id: step2Content
                    selectedStrategyType: root.selectedStrategyType
                    factorService: root.factorService
                    
                    onParametersChanged: function(newParameters) {
                        root.strategyParameters = newParameters
                    }
                    
                    onValidationChanged: function(allValid, errors) {
                        step2Valid = allValid
                    }
                    
                    onAdvancedOptionsChanged: function(enabled) {
                        root.enableAdvancedOptions = enabled
                    }

                    onApplyRuleTemplateSuggestionRequested: function(payload) {
                        if (strategyBasicInfo && typeof strategyBasicInfo.applyRuleTemplateSuggestion === "function") {
                            strategyBasicInfo.applyRuleTemplateSuggestion(payload.suggestion, payload.applyMode)
                        }
                    }
                }
                
                // 步骤3: 创建确认
                Rectangle {
                    id: step3Content
                    color: "transparent"

                    function activeStages() {
                        return Array.isArray(step2Content.ruleComposerStages) ? step2Content.ruleComposerStages : []
                    }

                    function nonEmptyGroups(stage) {
                        var groups = Array.isArray(stage && stage.groups) ? stage.groups : []
                        return groups.filter(function(group) {
                            return Array.isArray(group.rules) && group.rules.length > 0
                        })
                    }

                    function groupSummary(group) {
                        var summary = (group.title || group.groupId || "规则组")
                            + " · " + step2Content.roleDisplayName(group.role)
                            + " / " + step2Content.operatorDisplayName(group.operator)
                        var threshold = Number(group.groupMinMatchCount || group.matchThreshold || 0)
                        if ((group.operator || "") === "at_least" && threshold > 0) {
                            summary += " / 阈值 " + threshold
                        }
                        summary += " / " + ((Array.isArray(group.rules) ? group.rules.length : 0) + " 条规则")
                        return summary
                    }

                    ScrollView {
                        id: step3ScrollView
                        anchors.fill: parent
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: step3ScrollView.availableWidth
                            spacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: confirmationHeader.implicitHeight + 36
                                radius: 12
                                color: "#0f172a"
                                border.width: 1
                                border.color: "#334155"

                                ColumnLayout {
                                    id: confirmationHeader
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 8

                                    Text {
                                        text: "创建确认"
                                        font.pixelSize: 20
                                        font.weight: Font.DemiBold
                                        color: "#f1f5f9"
                                    }

                                    Text {
                                        text: "本页只确认策略定义本身。回测周期、基准、交易成本、样本外测试等运行期参数统一在策略回测页面配置，不再放在创建流程里重复维护。"
                                        font.pixelSize: 13
                                        color: "#cbd5e1"
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: "策略创建完成后，可在策略库或策略回测页面单独配置并启动回测。"
                                        font.pixelSize: 12
                                        color: "#38bdf8"
                                    }
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: width > 980 ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 12

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 224
                                    radius: 12
                                    color: "#1e293b"
                                    border.width: 1
                                    border.color: "#334155"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 10

                                        Text {
                                            text: "策略概览"
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                            color: "#f1f5f9"
                                        }

                                        Text {
                                            text: "名称: " + (strategyBasicInfo.strategyName || "未命名策略")
                                            font.pixelSize: 13
                                            color: "#cbd5e1"
                                            wrapMode: Text.WordWrap
                                        }

                                        Text {
                                            text: "类型: " + Utils.StrategyCreationUtils.getStrategyTypeName(root.selectedStrategyType)
                                            font.pixelSize: 13
                                            color: "#cbd5e1"
                                        }

                                        Text {
                                            text: "资产类型: " + (strategyBasicInfo.getAssetTypeValue ? strategyBasicInfo.getAssetTypeValue() : root.assetType)
                                            font.pixelSize: 13
                                            color: "#cbd5e1"
                                        }

                                        Text {
                                            text: "时间周期: " + (strategyBasicInfo.getTimeFrameValue ? strategyBasicInfo.getTimeFrameValue() : root.timeFrame)
                                            font.pixelSize: 13
                                            color: "#cbd5e1"
                                        }

                                        Text {
                                            text: "风险等级: " + Utils.StrategyCreationUtils.getRiskLevelName(strategyBasicInfo.getRiskLevelValue ? strategyBasicInfo.getRiskLevelValue() : root.riskLevel)
                                            font.pixelSize: 13
                                            color: Utils.StrategyCreationUtils.getRiskLevelColor(strategyBasicInfo.getRiskLevelValue ? strategyBasicInfo.getRiskLevelValue() : root.riskLevel)
                                        }

                                        Text {
                                            text: "标签数: " + (strategyBasicInfo.getTagsList ? strategyBasicInfo.getTagsList().length : 0)
                                            font.pixelSize: 13
                                            color: "#cbd5e1"
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 224
                                    radius: 12
                                    color: "#1e293b"
                                    border.width: 1
                                    border.color: "#334155"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 10

                                        Text {
                                            text: "参数检查"
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                            color: "#f1f5f9"
                                        }

                                        Text {
                                            text: "策略参数项: " + Object.keys(root.strategyParameters || {}).length
                                            font.pixelSize: 13
                                            color: "#cbd5e1"
                                        }

                                        Text {
                                            text: "高级策略配置: " + (root.enableAdvancedOptions ? "已启用" : "未启用")
                                            font.pixelSize: 13
                                            color: root.enableAdvancedOptions ? "#10b981" : "#94a3b8"
                                        }

                                        Text {
                                            text: "运行期参数将在回测页配置，不再在创建页重复维护。"
                                            font.pixelSize: 13
                                            color: "#94a3b8"
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: step2Valid ? "参数校验已通过，可直接创建策略。" : "第二步仍有未完成项，请返回补全。"
                                            font.pixelSize: 13
                                            color: step2Valid ? "#10b981" : "#ef4444"
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: ruleSummaryColumn.implicitHeight + 32
                                radius: 12
                                color: "#1e293b"
                                border.width: 1
                                border.color: "#334155"

                                ColumnLayout {
                                    id: ruleSummaryColumn
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 10

                                    Text {
                                        text: "规则编排摘要"
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                        color: "#f1f5f9"
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "确认创建前，核对每个阶段已绑定的规则组、组合方式和规则数量。"
                                        font.pixelSize: 12
                                        color: "#94a3b8"
                                        wrapMode: Text.WordWrap
                                    }

                                    StrategyComponents.RuleComposerSummaryBar {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 96
                                        stages: step2Content.ruleComposerStages
                                        strategyProfile: step2Content.strategyProfile
                                    }

                                    Repeater {
                                        model: step3Content.activeStages()

                                        delegate: Rectangle {
                                            required property var modelData
                                            readonly property var populatedGroups: step3Content.nonEmptyGroups(modelData)

                                            Layout.fillWidth: true
                                            radius: 10
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: (modelData && modelData.accentColor) || "#334155"
                                            implicitHeight: stageSummaryColumn.implicitHeight + 18

                                            ColumnLayout {
                                                id: stageSummaryColumn
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                spacing: 8

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: (modelData.title || modelData.stageId || "阶段") + " · "
                                                          + (populatedGroups.length > 0
                                                             ? (populatedGroups.length + " 个规则组 / "
                                                                + populatedGroups.reduce(function(total, group) {
                                                                      return total + (Array.isArray(group.rules) ? group.rules.length : 0)
                                                                  }, 0)
                                                                + " 条规则")
                                                             : "未放入规则")
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    color: "#f8fafc"
                                                    wrapMode: Text.WordWrap
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: modelData.description || ""
                                                    font.pixelSize: 11
                                                    color: "#94a3b8"
                                                    wrapMode: Text.WordWrap
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 6
                                                    visible: populatedGroups.length > 0

                                                    Repeater {
                                                        model: populatedGroups

                                                        delegate: Rectangle {
                                                            required property var modelData
                                                            Layout.fillWidth: true
                                                            radius: 8
                                                            color: "#111827"
                                                            border.width: 1
                                                            border.color: "#1f2937"
                                                            implicitHeight: groupSummaryColumn.implicitHeight + 14

                                                            ColumnLayout {
                                                                id: groupSummaryColumn
                                                                anchors.fill: parent
                                                                anchors.margins: 8
                                                                spacing: 6

                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    text: step3Content.groupSummary(modelData)
                                                                    font.pixelSize: 12
                                                                    color: "#e2e8f0"
                                                                    wrapMode: Text.WordWrap
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

                                                                    delegate: Text {
                                                                        required property var modelData
                                                                        Layout.fillWidth: true
                                                                        text: "- " + (modelData.templateName || modelData.templateId || "未命名模板")
                                                                        font.pixelSize: 11
                                                                        color: "#cbd5e1"
                                                                        wrapMode: Text.WordWrap
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    visible: populatedGroups.length === 0
                                                    text: "当前阶段还没有放入规则，创建后会按默认空阶段保存。"
                                                    font.pixelSize: 11
                                                    color: "#64748b"
                                                    wrapMode: Text.WordWrap
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: processFlowLayout.implicitHeight + 32
                                radius: 12
                                color: "#1e293b"
                                border.width: 1
                                border.color: "#334155"

                                ColumnLayout {
                                    id: processFlowLayout
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 10

                                    Text {
                                        text: "创建后流程"
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                        color: "#f1f5f9"
                                    }

                                    Text {
                                        text: "创建页只负责策略定义，回测页负责运行期参数，避免两边维护两套配置。"
                                        font.pixelSize: 12
                                        color: "#94a3b8"
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: "1. 创建策略：保存策略名称、说明、标签、基础属性与核心参数。"
                                        font.pixelSize: 13
                                        color: "#cbd5e1"
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: "2. 进入回测页：配置回测年限、基准、交易成本、风控阈值、数据源等运行期参数。"
                                        font.pixelSize: 13
                                        color: "#cbd5e1"
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: "3. 执行回测：基于真实行情数据验证当前策略定义。"
                                        font.pixelSize: 13
                                        color: "#cbd5e1"
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // 底部操作栏
        Rectangle {
            id: footer
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: "#1e293b"
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16
                
                // 取消按钮
                Button {
                    id: cancelButton
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 40
                    text: Utils.StrategyCreationUtils.tr('common.cancel')
                    onClicked: {
                        root.backClicked()
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: "#334155"
                        border.width: 1
                        border.color: "#475569"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "#f1f5f9"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 步骤验证状态
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 8
                    color: "#334155"
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        
                        Rectangle {
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            radius: 10
                            color: stepIndicator.currentStepValid ? "#10b981" : "#ef4444"
                            
                            Text {
                                anchors.centerIn: parent
                                text: stepIndicator.currentStepValid ? "✓" : "⚠"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                color: "white"
                            }
                        }
                        
                        Text {
                            text: stepIndicator.currentStepValid ? 
                                  Utils.StrategyCreationUtils.tr('strategyCreation.validationPassed') : 
                                  Utils.StrategyCreationUtils.tr('strategyCreation.validationRequired')
                            font.pixelSize: 13
                            color: stepIndicator.currentStepValid ? "#10b981" : "#ef4444"
                        }
                        
                        Item { Layout.fillWidth: true }
                    }
                }
                
                // 上一步按钮
                Button {
                    id: prevButton
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 40
                    text: Utils.StrategyCreationUtils.tr('common.previous')
                    visible: stepIndicator.currentStep > 1
                    enabled: stepIndicator.currentStep > 1
                    onClicked: {
                        if (stepIndicator.currentStep > 1) {
                            stepIndicator.currentStep--
                        }
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? "#334155" : "#475569"
                        border.width: 1
                        border.color: parent.enabled ? "#475569" : "#64748b"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? "#f1f5f9" : "#94a3b8"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 创建按钮（仅在第3步显示）
                Button {
                    id: createButton
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 40
                    text: root.isEditMode ? "保存修改" : Utils.StrategyCreationUtils.tr('strategyCreation.create')
                    visible: stepIndicator.currentStep === 3
                    enabled: stepIndicator.currentStepValid
                    onClicked: {
                        createStrategy()
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? "#3b82f6" : "#475569"
                        border.width: 1
                        border.color: parent.enabled ? "#3b82f6" : "#64748b"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 下一步按钮（仅在前两步显示）
                Button {
                    id: nextButton
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 40
                    text: Utils.StrategyCreationUtils.tr('common.next')
                    visible: stepIndicator.currentStep < 3
                    enabled: stepIndicator.currentStepValid
                    onClicked: {
                        if (stepIndicator.currentStep < 3) {
                            stepIndicator.currentStep++
                        }
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? "#3b82f6" : "#475569"
                        border.width: 1
                        border.color: parent.enabled ? "#3b82f6" : "#64748b"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
    
    // ============ 内部状态 ============
    
    property bool step1Valid: false
    property bool step2Valid: false
    property bool step3Valid: true  // 风险管理步骤总是有效
    
    // 步骤指示器组件
    QtObject {
        id: stepIndicator
        property int currentStep: 1
        property bool currentStepValid: {
            switch(currentStep) {
                case 1: return step1Valid
                case 2: return step2Valid
                case 3: return step3Valid
                default: return false
            }
        }

    }
    
    // ============ 功能函数 ============

    function mapBackendTypeToFrontend(strategy) {
        var explicitSubtype = strategy ? strategy.sub_type : ""
        if (explicitSubtype) {
            return explicitSubtype
        }

        var backendType = String((strategy && strategy.strategy_type) || "CUSTOM")
        if (backendType === "TREND") return "trend_following"
        if (backendType === "MEAN_REVERSION") return "mean_reversion"
        if (backendType === "ARBITRAGE") return "arbitrage"
        if (backendType === "HFT") return "high_frequency"
        if (backendType === "ALPHA") return "momentum"
        return "custom"
    }

    function toPlainJsValue(rawValue) {
        if (rawValue === undefined || rawValue === null) {
            return rawValue
        }

        if (typeof rawValue === "object") {
            try {
                return JSON.parse(JSON.stringify(rawValue))
            } catch (error) {
            }
        }

        return rawValue
    }

    function loadStrategyForEdit(strategy) {
        var plainStrategy = toPlainJsValue(strategy) || ({})
        if (!plainStrategy || Object.keys(plainStrategy).length === 0) {
            return
        }

        var frontendType = mapBackendTypeToFrontend(plainStrategy)
        var parameters = toPlainJsValue(plainStrategy.parameters) || ({})
        var editableParameters = ({})
        for (var key in parameters) {
            editableParameters[key] = parameters[key]
        }
        if (!editableParameters.rule_profile && plainStrategy.ruleProfileSnapshot) {
            editableParameters.rule_profile = toPlainJsValue(plainStrategy.ruleProfileSnapshot)
        }
        if (!editableParameters.execution_policy && plainStrategy.executionPolicySnapshot) {
            editableParameters.execution_policy = toPlainJsValue(plainStrategy.executionPolicySnapshot)
        }
        if (!editableParameters.backtest_assumptions && plainStrategy.backtestAssumptionsSnapshot) {
            editableParameters.backtest_assumptions = toPlainJsValue(plainStrategy.backtestAssumptionsSnapshot)
        }
        if (!editableParameters.strategy_scope_context && plainStrategy.strategyScopeContextSnapshot) {
            editableParameters.strategy_scope_context = toPlainJsValue(plainStrategy.strategyScopeContextSnapshot)
        }
        var advancedOptions = toPlainJsValue(plainStrategy.advanced_options || plainStrategy.advancedOptions || parameters.advanced_options) || ({})
        var strategySnapshot = {
            strategy_id: plainStrategy.strategy_id || plainStrategy.strategyId || plainStrategy.id || "",
            strategy_name: plainStrategy.strategy_name || plainStrategy.strategyName || "",
            description: plainStrategy.description || parameters.description || "",
            parameters: editableParameters,
            asset_type: plainStrategy.asset_type || plainStrategy.assetType || parameters.asset_type || "stock",
            time_frame: plainStrategy.time_frame || plainStrategy.timeFrame || parameters.time_frame || "daily",
            risk_level: plainStrategy.risk_level || plainStrategy.riskLevel || parameters.risk_level || "medium",
            optimization_method: plainStrategy.optimization_method || plainStrategy.optimizationMethod || parameters.optimization_method || "genetic",
            tags: toPlainJsValue(plainStrategy.tags || parameters.tags) || []
        }

            console.log("loadStrategyForEdit:", strategySnapshot.strategy_id,
                    "parameterKeys=", Object.keys(editableParameters).length,
                    "hasRuleProfile=", !!editableParameters.rule_profile,
                    "hasComposerState=", !!editableParameters.rule_composer_state,
                    "hasRuleBindings=", !!editableParameters.rule_template_bindings)

        isEditMode = true
        editingStrategyId = strategySnapshot.strategy_id
        selectedStrategyType = frontendType
        enableAdvancedOptions = !!advancedOptions.enabled

        if (strategyTypeSelector && strategyTypeSelector.setSelectedStrategyType) {
            strategyTypeSelector.setSelectedStrategyType(frontendType, false)
        }
        if (strategyBasicInfo && strategyBasicInfo.setBasicInfo) {
            strategyBasicInfo.setBasicInfo(strategySnapshot)
        }
        if (step2Content && step2Content.applyPersistedStrategy) {
            Qt.callLater(function() {
                if (step2Content && step2Content.applyPersistedStrategy) {
                    step2Content.applyPersistedStrategy(frontendType, editableParameters, advancedOptions)
                }
            })
        }

        strategyParameters = step2Content.strategyParameters || ({})
        parametersValid = step2Content.parametersValid
        step1Valid = strategyBasicInfo.isValid ? strategyBasicInfo.isValid() : true
        step2Valid = step2Content.isValid ? step2Content.isValid() : true
        stepIndicator.currentStep = 1
    }

    function focusSymbolPoolEditor() {
        stepIndicator.currentStep = 1
        if (strategyBasicInfo && strategyBasicInfo.focusSymbolPoolField) {
            Qt.callLater(function() {
                strategyBasicInfo.focusSymbolPoolField()
            })
        }
    }

    // 创建策略
    function createStrategy() {
        console.log("开始创建策略...")

        if (step2Content && typeof step2Content.syncDecoratedParameters === "function") {
            strategyParameters = step2Content.syncDecoratedParameters()
            parametersValid = step2Content.isValid ? step2Content.isValid() : step2Content.parametersValid
            enableAdvancedOptions = step2Content.enableAdvancedOptions
        }

        if (step2Content && step2Content.isValid && !step2Content.isValid()) {
            var ruleComposerErrors = (step2Content.ruleComposerValidation && step2Content.ruleComposerValidation.errors) || []
            var blockedMessage = ruleComposerErrors.length > 0
                ? (ruleComposerErrors[0].message || "当前规则组合未通过校验")
                : "当前参数或规则组合未通过校验，请先修复后再创建策略。"
            showErrorDialog(blockedMessage)
            return
        }
        
        // 构建完整的策略数据
        var context = {
            strategyName: strategyBasicInfo.strategyName,
            strategyDescription: strategyBasicInfo.strategyDescription,
            linkedStockPool: strategyBasicInfo.getLinkedStockPoolBinding(),
            selectedStrategyType: selectedStrategyType,
            strategyTags: strategyBasicInfo.getTagsList(),
            assetType: strategyBasicInfo.getAssetTypeValue(),
            timeFrame: strategyBasicInfo.getTimeFrameValue(),
            riskLevel: strategyBasicInfo.getRiskLevelValue(),
            optimizationMethod: strategyBasicInfo.getOptimizationMethodValue(),
            enableAdvancedOptions: enableAdvancedOptions,
            strategyParameters: strategyParameters,
            parametersValid: step2Content && step2Content.isValid ? step2Content.isValid() : parametersValid
        }
        
        var strategyData = Utils.StrategyCreationUtils.buildCompleteStrategyData(context)
        var advancedOptions = step2Content.getAdvancedOptions ? step2Content.getAdvancedOptions() : ({ enabled: !!enableAdvancedOptions })
        var linkedStockPool = strategyBasicInfo.getLinkedStockPoolBinding ? strategyBasicInfo.getLinkedStockPoolBinding() : ({})
        
        console.log("策略数据构建完成:", JSON.stringify(strategyData, null, 2))
        
        // 设置创建状态
        isCreating = true
        creationStatus = Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreatedSuccess')
        
        // 检查StrategyService是否可用
        if (!strategyService) {
            console.error("StrategyService 未初始化，无法创建策略")
            showErrorDialog("策略服务未初始化，请重启应用程序")
            isCreating = false
            return
        }
        
        // 将前端数据结构映射到后端所需格式
        var backendStrategyData = {
            "strategy_name": strategyData.name,
            "strategy_type": mapStrategyTypeToBackend(strategyData.strategyType),
            "description": strategyData.description,
            "asset_type": strategyData.assetType,
            "time_frame": strategyData.timeFrame,
            "risk_level": strategyData.riskLevel,
            "optimization_method": strategyData.optimizationMethod,
            "advanced_options": advancedOptions,
            "parameters": strategyData.parameters,
            "clear_symbol_pool": true,
            "sub_type": strategyData.strategyType,
            
            // 元数据
            "status": "DRAFT",
            "version": "1.0",
            "language": "Python",
            "author": "System",

            // 标签
            "tags": strategyData.tags || []
        }

        if (linkedStockPool && linkedStockPool.hasBinding) {
            backendStrategyData.parameters.linked_stock_pool_id = linkedStockPool.poolId
            backendStrategyData.parameters.linked_stock_pool_name = linkedStockPool.poolName
            backendStrategyData.parameters.linked_stock_pool_symbols = linkedStockPool.symbols || []
        } else {
            delete backendStrategyData.parameters.linked_stock_pool_id
            delete backendStrategyData.parameters.linked_stock_pool_name
            delete backendStrategyData.parameters.linked_stock_pool_symbols
        }
        
        console.log("调用StrategyService创建策略...", JSON.stringify(backendStrategyData, null, 2))
        
        var strategyId = editingStrategyId
        var success = false
        if (isEditMode && editingStrategyId) {
            success = strategyService.updateStrategy(editingStrategyId, backendStrategyData)
        } else {
            strategyId = strategyService.createStrategy(backendStrategyData)
            success = !!strategyId
        }
        
        if (success) {
            console.log(isEditMode ? "策略更新成功，ID:" : "策略创建成功，ID:", strategyId)
            
            // 注意：不需要手动调用syncWithDatabase，因为StrategyService.createStrategy()方法
            // 内部已经会发送dataChanged信号，StrategyLibraryPage会监听这个信号并自动更新

            // 显示成功消息
            showSuccessDialog(strategyId)
        } else {
            console.error(isEditMode ? "策略更新失败" : "策略创建失败")
            showErrorDialog(isEditMode ? "策略更新失败，请检查参数" : "策略创建失败，请检查参数")
        }
        
        isCreating = false
    }
    
    // 映射策略类型到后端类型
    function mapStrategyTypeToBackend(frontendType) {
        var mapping = {
            "trend_following": "TREND",
            "trend_breakout": "TREND",
            "mean_reversion": "MEAN_REVERSION", 
            "momentum": "ALPHA",
            "arbitrage": "ARBITRAGE",
            "machine_learning": "ALPHA",
            "multi_factor": "ALPHA",
            "high_frequency": "HFT",
            "event_driven": "ALPHA",
            "custom": "CUSTOM"
        }
        return mapping[frontendType] || "CUSTOM"
    }
    
    // 显示成功对话框
    function showSuccessDialog(strategyId) {
        successDialog.strategyId = strategyId
        successDialog.strategyName = strategyBasicInfo.strategyName
        successDialog.dialogTitleText = isEditMode ? "策略更新成功" : "策略创建成功"
        successDialog.dialogMessageText = isEditMode
            ? "策略修改已保存到策略库，您可以返回策略库继续查看或编辑。"
            : "策略已保存到策略库，您可以返回策略库继续查看或编辑。"
        successDialog.open()
    }
    
    // 显示错误对话框
    function showErrorDialog(message) {
        errorDialog.errorMessage = message
        errorDialog.open()
    }
    
    // 重置表单
    function resetForm() {
        stepIndicator.currentStep = 1
        isCreating = false
        creationStatus = ""
        isEditMode = false
        editingStrategyId = ""
        
        // 重置各组件
        strategyTypeSelector.reset()
        strategyBasicInfo.reset()
        strategyBasicInfo.refreshLinkedStockPools()
        
        var resetData = Utils.StrategyCreationUtils.resetFormData()
        
        // 应用重置数据
        selectedStrategyType = resetData.selectedStrategyType
        strategyName = resetData.strategyName
        strategyDescription = resetData.strategyDescription
        strategyTags = resetData.strategyTags
        assetType = resetData.assetType
        timeFrame = resetData.timeFrame
        riskLevel = resetData.riskLevel
        optimizationMethod = resetData.optimizationMethod
        enableAdvancedOptions = resetData.enableAdvancedOptions
        strategyParameters = resetData.strategyParameters
        parametersValid = resetData.parametersValid
        strategyBasicInfo.applyStrategyTypeDefaults(selectedStrategyType, true)
    }
    
    // ============ 定时器和动画 ============
    
    Timer {
        id: creationTimer
        interval: 2000
        onTriggered: {
            isCreating = false
            creationStatus = ""
            console.log("策略创建完成")
            
            // 显示成功消息
            createSuccessDialog.open()
        }
    }
    
    // 创建成功对话框 (旧版本，保留兼容性)
    Dialog {
        id: createSuccessDialog
        title: Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreationSuccessDialogTitle')
        standardButtons: Dialog.Ok
        modal: true
        
        width: 400
        height: 200
        
        contentItem: ColumnLayout {
            spacing: 16
            
            Rectangle {
                Layout.preferredWidth: 60
                Layout.preferredHeight: 60
                radius: 30
                color: "#10b981"
                
                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: "white"
                }
            }
            
            Text {
                Layout.fillWidth: true
                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreatedSuccessDialogMessage')
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#f1f5f9"
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreatedSuccessDialogSubtitle')
                font.pixelSize: 13
                color: "#94a3b8"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
    
    // 策略创建成功对话框 (集成C++服务后使用)
    Dialog {
        id: successDialog
        title: "策略创建成功"
        standardButtons: Dialog.Ok
        modal: true
        
        width: 550
        height: 300
        
        property string strategyId: ""
        property string strategyName: ""
        property string dialogTitleText: "策略创建成功"
        property string dialogMessageText: "策略已保存到策略库，您可以返回策略库继续查看或编辑。"
        
        contentItem: ColumnLayout {
            spacing: 20
            
            Rectangle {
                Layout.preferredWidth: 70
                Layout.preferredHeight: 70
                radius: 35
                color: "#10b981"
                
                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    font.pixelSize: 32
                    font.weight: Font.Bold
                    color: "white"
                }
            }
            
            Text {
                Layout.fillWidth: true
                text: successDialog.dialogTitleText
                font.pixelSize: 20
                font.weight: Font.Bold
                color: "#10b981"
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: "策略名称: <b>" + successDialog.strategyName + "</b>"
                font.pixelSize: 14
                color: "#f1f5f9"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.RichText
            }
            
            Text {
                Layout.fillWidth: true
                text: "策略ID: " + successDialog.strategyId
                font.pixelSize: 12
                color: "#94a3b8"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: successDialog.dialogMessageText
                font.pixelSize: 13
                color: "#cbd5e1"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 10

                Rectangle {
                    Layout.preferredWidth: 132
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#334155"

                    Text {
                        anchors.centerIn: parent
                        text: "返回策略库"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("点击返回策略库按钮")
                            successDialog.close()
                            successDialog.accepted()
                        }
                    }
                }
            }
        }
        
        onAccepted: {
            // 默认操作：返回到策略库
            console.log("对话框确认按钮点击，返回到策略库")
            successDialog.close()
            root.backClicked()
        }
    }
    
    // 错误对话框
    Dialog {
        id: errorDialog
        title: "策略创建失败"
        standardButtons: Dialog.Ok
        modal: true
        
        width: 450
        height: 200
        
        property string errorMessage: ""
        
        contentItem: ColumnLayout {
            spacing: 20
            
            Rectangle {
                Layout.preferredWidth: 60
                Layout.preferredHeight: 60
                radius: 30
                color: "#ef4444"
                
                Text {
                    anchors.centerIn: parent
                    text: "⚠"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: "white"
                }
            }
            
            Text {
                Layout.fillWidth: true
                text: "策略创建失败"
                font.pixelSize: 18
                font.weight: Font.Bold
                color: "#ef4444"
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: errorDialog.errorMessage || "未知错误"
                font.pixelSize: 13
                color: "#f1f5f9"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
    
    // ============ 初始化和信号连接 ============
    
Component.onCompleted: {
    console.log("StrategyCreationPagePro 初始化完成")
    
    // 初始化数据
    resetForm()
    
    // StrategyService已经通过property绑定，直接使用
    if (strategyService) {
        console.log("StrategyService 初始化成功")
    } else {
        console.warn("StrategyService 未找到")
    }
    
    // 连接信号
    strategyBasicInfo.validationChanged.connect(function(isValid) {
        step1Valid = isValid
    })
    strategyBasicInfo.refreshLinkedStockPools()
}
}
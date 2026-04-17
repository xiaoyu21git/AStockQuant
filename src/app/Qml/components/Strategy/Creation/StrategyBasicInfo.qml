// StrategyBasicInfo.qml
// 策略基本信息组件 - 用于策略创建向导步骤1右侧部分

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../../../components/StockPools" as StockPoolComponents
import "../../../utils/StrategyCreationUtils.js" as Utils
import "../../../utils/CustomStockPoolStore.js" as CustomStockPoolStore

Rectangle {
    id: root

    // ============ 属性 ============

    // 策略基本信息
    property alias strategyName: strategyNameField.text
    property alias strategyDescription: strategyDescField.text
    property alias assetType: assetTypeCombo.currentIndex
    property alias timeFrame: timeFrameCombo.currentIndex
    property alias riskLevel: riskLevelCombo.currentIndex
    property alias optimizationMethod: optimizationCombo.currentIndex
    property alias strategyTags: tagsField.text
    property string selectedStrategyType: ""
    property bool useWideCardLayout: true
    property bool descriptionRecentlyUpdated: false
    property bool tagsRecentlyUpdated: false
    property string linkedStockPoolId: ""
    property string linkedStockPoolName: ""
    property var linkedStockPoolSymbols: []

    // 信号
    signal tagsChanged(var tagsList)
    signal validationChanged(bool isValid)

    property string lastAutoDescription: ""
    property string lastAutoTagsText: ""

    Timer {
        id: descriptionUpdateTimer
        interval: 6000
        repeat: false
        onTriggered: root.descriptionRecentlyUpdated = false
    }

    Timer {
        id: tagsUpdateTimer
        interval: 6000
        repeat: false
        onTriggered: root.tagsRecentlyUpdated = false
    }

    // ============ 主布局 ============

    color: "transparent"

    ScrollView {
        id: basicInfoScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: basicInfoScroll.availableWidth
            spacing: 12

            Text {
                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyBasicInfo')
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#f1f5f9"
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.useWideCardLayout ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: "#111827"
                    border.width: 1
                    border.color: "#334155"
                    implicitHeight: coreInfoColumn.implicitHeight + 24

                    ColumnLayout {
                        id: coreInfoColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Text {
                            text: "核心说明"
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: "#f1f5f9"
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            Text {
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyName')
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#f1f5f9"
                            }

                            TextField {
                                id: strategyNameField
                                Layout.fillWidth: true
                                placeholderText: Utils.StrategyCreationUtils.tr('strategyCreation.strategyNamePlaceholder')
                                text: ""

                                property bool hasError: false

                                background: Rectangle {
                                    implicitHeight: 42
                                    radius: 6
                                    color: "#0f172a"
                                    border.width: strategyNameField.hasError ? 2 : 1
                                    border.color: strategyNameField.hasError ? "#ef4444" : "#334155"

                                    Behavior on border.color {
                                        ColorAnimation { duration: 200 }
                                    }
                                }

                                color: "#f1f5f9"
                                font.pixelSize: 14
                                padding: 10
                                verticalAlignment: TextInput.AlignVCenter

                                onFocusChanged: {
                                    if (!focus && strategyNameField.text.trim() === "") {
                                        strategyNameField.hasError = true
                                    } else {
                                        strategyNameField.hasError = false
                                    }
                                    validateForm()
                                }

                                onTextChanged: {
                                    if (strategyNameField.text.trim() === "") {
                                        strategyNameField.hasError = true
                                    } else {
                                        strategyNameField.hasError = false
                                    }
                                    validateForm()
                                }

                                Keys.onReturnPressed: {
                                    focus = false
                                }
                            }

                            Text {
                                visible: strategyNameField.hasError && strategyNameField.text.trim() === ""
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyNameError')
                                font.pixelSize: 12
                                color: "#ef4444"
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyDescription')
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    color: "#f1f5f9"
                                }

                                Rectangle {
                                    visible: root.descriptionRecentlyUpdated
                                    radius: 10
                                    color: "#083344"
                                    border.width: 1
                                    border.color: "#14b8a6"
                                    implicitWidth: descriptionUpdatedLabel.implicitWidth + 12
                                    implicitHeight: 20

                                    Text {
                                        id: descriptionUpdatedLabel
                                        anchors.centerIn: parent
                                        text: "刚更新"
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                        color: "#ccfbf1"
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }

                            TextArea {
                                id: strategyDescField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 92
                                placeholderText: Utils.StrategyCreationUtils.tr('strategyCreation.strategyDescriptionPlaceholder')
                                text: ""
                                wrapMode: Text.WordWrap

                                background: Rectangle {
                                    radius: 6
                                    color: "#0f172a"
                                    border.width: root.descriptionRecentlyUpdated ? 2 : 1
                                    border.color: root.descriptionRecentlyUpdated ? "#14b8a6" : "#334155"

                                    Behavior on border.color {
                                        ColorAnimation { duration: 220 }
                                    }
                                }

                                color: "#f1f5f9"
                                font.pixelSize: 14
                                padding: 10

                                onTextChanged: {
                                    validateForm()
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: "#111827"
                    border.width: 1
                    border.color: "#334155"
                    implicitHeight: runtimeColumn.implicitHeight + 24

                    ColumnLayout {
                        id: runtimeColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: "运行属性"
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: "#f1f5f9"
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 540 ? 2 : 1
                            columnSpacing: 12
                            rowSpacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.assetType')
                                    font.pixelSize: 13
                                    color: "#cbd5e1"
                                }

                                ComboBox {
                                    id: assetTypeCombo
                                    Layout.fillWidth: true
                                    model: Utils.StrategyCreationUtils.tr('strategyCreation.assetTypes')
                                    currentIndex: 0

                                    background: Rectangle {
                                        implicitHeight: 36
                                        radius: 6
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    contentItem: Text {
                                        text: assetTypeCombo.displayText
                                        color: "#f1f5f9"
                                        font.pixelSize: 13
                                        padding: 8
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.timeFrame')
                                    font.pixelSize: 13
                                    color: "#cbd5e1"
                                }

                                ComboBox {
                                    id: timeFrameCombo
                                    Layout.fillWidth: true
                                    model: Utils.StrategyCreationUtils.tr('strategyCreation.timeFrames')
                                    currentIndex: 4

                                    background: Rectangle {
                                        implicitHeight: 36
                                        radius: 6
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    contentItem: Text {
                                        text: timeFrameCombo.displayText
                                        color: "#f1f5f9"
                                        font.pixelSize: 13
                                        padding: 8
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.riskLevel')
                                    font.pixelSize: 13
                                    color: "#cbd5e1"
                                }

                                ComboBox {
                                    id: riskLevelCombo
                                    Layout.fillWidth: true
                                    model: [
                                        Utils.StrategyCreationUtils.getRiskLevelName("low"),
                                        Utils.StrategyCreationUtils.getRiskLevelName("medium"),
                                        Utils.StrategyCreationUtils.getRiskLevelName("high"),
                                        Utils.StrategyCreationUtils.getRiskLevelName("aggressive")
                                    ]
                                    currentIndex: 1

                                    background: Rectangle {
                                        implicitHeight: 36
                                        radius: 6
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    contentItem: Text {
                                        text: riskLevelCombo.displayText
                                        color: "#f1f5f9"
                                        font.pixelSize: 13
                                        padding: 8
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.optimizationMethod')
                                    font.pixelSize: 13
                                    color: "#cbd5e1"
                                }

                                ComboBox {
                                    id: optimizationCombo
                                    Layout.fillWidth: true
                                    model: Utils.StrategyCreationUtils.tr('strategyCreation.optimizationMethods')
                                    currentIndex: 0

                                    background: Rectangle {
                                        implicitHeight: 36
                                        radius: 6
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    contentItem: Text {
                                        text: optimizationCombo.displayText
                                        color: "#f1f5f9"
                                        font.pixelSize: 13
                                        padding: 8
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: "#111827"
                    border.width: 1
                    border.color: "#334155"
                    implicitHeight: poolColumn.implicitHeight + 24

                    ColumnLayout {
                        id: poolColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Text {
                            text: "联动池"
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: "#f1f5f9"
                        }

                        StockPoolComponents.LinkedStockPoolSelector {
                            id: linkedStockPoolSelector
                            Layout.fillWidth: true
                            title: "关联自选股票池"
                            helperText: "关联后仅用于策略联动与实盘候选，不会在创建时绑定策略股票池。"

                            onBindingChanged: function(binding) {
                                root.linkedStockPoolId = binding.poolId || ""
                                root.linkedStockPoolName = binding.poolName || ""
                                root.linkedStockPoolSymbols = binding.symbols || []
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: "#111827"
                    border.width: 1
                    border.color: "#334155"
                    implicitHeight: tagsColumn.implicitHeight + 24

                    ColumnLayout {
                        id: tagsColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.tags')
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#f1f5f9"
                            }

                            Rectangle {
                                visible: root.tagsRecentlyUpdated
                                radius: 10
                                color: "#3f1d0d"
                                border.width: 1
                                border.color: "#fb923c"
                                implicitWidth: tagsUpdatedLabel.implicitWidth + 12
                                implicitHeight: 20

                                Text {
                                    id: tagsUpdatedLabel
                                    anchors.centerIn: parent
                                    text: "刚更新"
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                    color: "#fed7aa"
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        TextField {
                            id: tagsField
                            Layout.fillWidth: true
                            placeholderText: Utils.StrategyCreationUtils.tr('strategyCreation.tagsPlaceholder')
                            onEditingFinished: {
                                var tags = tagsField.text.split(',').map(function(tag) {
                                    return tag.trim();
                                }).filter(function(tag) {
                                    return tag.length > 0;
                                });
                                root.tagsChanged(tags)
                            }

                            background: Rectangle {
                                implicitHeight: 42
                                radius: 6
                                color: "#0f172a"
                                border.width: root.tagsRecentlyUpdated ? 2 : 1
                                border.color: root.tagsRecentlyUpdated ? "#fb923c" : "#334155"

                                Behavior on border.color {
                                    ColorAnimation { duration: 220 }
                                }
                            }

                            color: "#f1f5f9"
                            font.pixelSize: 14
                            padding: 10
                        }

                        Flow {
                            id: tagsPreviewFlow
                            Layout.fillWidth: true
                            spacing: 6

                            Repeater {
                                id: tagsRepeater
                                model: []

                                delegate: Rectangle {
                                    height: 28
                                    width: Math.min(Math.max(72, tagsPreviewFlow.width), tagText.implicitWidth + 16)
                                    radius: 14
                                    color: Qt.rgba(59/255, 130/255, 246/255, 0.1)
                                    border.width: 1
                                    border.color: "#3b82f6"

                                    Text {
                                        id: tagText
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins: 8
                                        text: modelData
                                        font.pixelSize: 11
                                        color: "#60a5fa"
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
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
    
    // 重置表单
    function reset() {
        strategyNameField.text = ""
        strategyDescField.text = ""
        assetTypeCombo.currentIndex = 0
        timeFrameCombo.currentIndex = 4
        riskLevelCombo.currentIndex = 1
        optimizationCombo.currentIndex = 0
        linkedStockPoolId = ""
        linkedStockPoolName = ""
        linkedStockPoolSymbols = []
        linkedStockPoolSelector.refreshPools()
        linkedStockPoolSelector.clearBinding()
        tagsField.text = ""
        tagsRepeater.model = []
        lastAutoDescription = ""
        lastAutoTagsText = ""
        descriptionRecentlyUpdated = false
        tagsRecentlyUpdated = false
        descriptionUpdateTimer.stop()
        tagsUpdateTimer.stop()
        validateForm()
    }

    function updateTagsPreview() {
        var tags = getTagsList()
        tagsRepeater.model = tags
        root.tagsChanged(tags)
    }

    function mergeTags(existingTags, incomingTags) {
        var merged = []
        var seen = {}
        var items = (existingTags || []).concat(incomingTags || [])
        for (var index = 0; index < items.length; ++index) {
            var token = String(items[index] || "").trim()
            var key = token.toLowerCase()
            if (!token || seen[key]) {
                continue
            }
            seen[key] = true
            merged.push(token)
        }
        return merged
    }

    function markFieldUpdated(fieldName) {
        var field = String(fieldName || "").trim().toLowerCase()
        if (field === "description") {
            descriptionRecentlyUpdated = true
            descriptionUpdateTimer.restart()
        } else if (field === "tags") {
            tagsRecentlyUpdated = true
            tagsUpdateTimer.restart()
        }
    }

    function buildSuggestionDescriptionBlock(suggestion) {
        if (!suggestion) {
            return ""
        }

        var termName = String(suggestion.term_display_name || suggestion.termDisplayName || "").trim()
        var templateName = String(suggestion.template_display_name || suggestion.templateDisplayName || suggestion.template_id || "").trim()
        var summary = String(suggestion.summary || "").trim()
        var recommendedActions = (suggestion.recommended_actions || suggestion.recommendedActions || []).map(function(item) {
            return String(item || "").trim()
        }).filter(function(item) {
            return item.length > 0
        })
        var matchedAliases = (suggestion.matched_aliases || suggestion.matchedAliases || []).map(function(item) {
            return String(item || "").trim()
        }).filter(function(item) {
            return item.length > 0
        })
        var descriptionLines = []
        if (termName) {
            descriptionLines.push("规则术语：" + termName)
        }
        if (templateName) {
            descriptionLines.push("参考模板：" + templateName)
        }
        if (summary) {
            descriptionLines.push("语义说明：" + summary)
        }
        if (recommendedActions.length > 0) {
            descriptionLines.push("建议动作：" + recommendedActions.join(" / "))
        }

        return descriptionLines.join("\n")
    }

    function buildSuggestionTags(suggestion) {
        if (!suggestion) {
            return getTagsList()
        }

        var termName = String(suggestion.term_display_name || suggestion.termDisplayName || "").trim()
        var recommendedActions = (suggestion.recommended_actions || suggestion.recommendedActions || []).map(function(item) {
            return String(item || "").trim()
        }).filter(function(item) {
            return item.length > 0
        })
        var matchedAliases = (suggestion.matched_aliases || suggestion.matchedAliases || []).map(function(item) {
            return String(item || "").trim()
        }).filter(function(item) {
            return item.length > 0
        })

        return mergeTags(
            getTagsList(),
            [termName, root.selectedStrategyType].concat(recommendedActions).concat(matchedAliases)
        )
    }

    function applyRuleTemplateSuggestion(suggestion, applyMode) {
        if (!suggestion) {
            return
        }

        var mode = String(applyMode || "all").trim().toLowerCase()
        if (mode === "") {
            mode = "all"
        }

        if (mode === "all" || mode === "description") {
            var suggestionBlock = buildSuggestionDescriptionBlock(suggestion)
            var currentDescription = strategyDescField.text.trim()
            if (suggestionBlock) {
                if (!currentDescription) {
                    strategyDescField.text = suggestionBlock
                    markFieldUpdated("description")
                } else if (strategyDescField.text.indexOf(suggestionBlock) === -1) {
                    strategyDescField.text = currentDescription + "\n\n" + suggestionBlock
                    markFieldUpdated("description")
                }
            }
        }

        if (mode === "all" || mode === "tags") {
            tagsField.text = buildSuggestionTags(suggestion).join(", ")
            markFieldUpdated("tags")
        }

        lastAutoDescription = strategyDescField.text
        lastAutoTagsText = tagsField.text
        updateTagsPreview()
        validateForm()
    }

    function applyStrategyTypeDefaults(strategyType, forceOverwrite) {
        var defaultDescription = Utils.StrategyCreationUtils.getDefaultStrategyDescription(strategyType)
        var defaultTags = Utils.StrategyCreationUtils.getDefaultStrategyTags(strategyType)
        var defaultTagsText = defaultTags.join(', ')

        if (forceOverwrite || strategyDescField.text.trim() === "" || strategyDescField.text === lastAutoDescription) {
            strategyDescField.text = defaultDescription
        }

        if (forceOverwrite || tagsField.text.trim() === "" || tagsField.text === lastAutoTagsText) {
            tagsField.text = defaultTagsText
        }

        lastAutoDescription = defaultDescription
        lastAutoTagsText = defaultTagsText
        updateTagsPreview()
        validateForm()
    }
    
    // 验证表单
    function validateForm() {
        var isValid = strategyNameField.text.trim() !== "" && 
                      strategyDescField.text.trim() !== ""
        root.validationChanged(isValid)
        return isValid
    }

    function isValid() {
        return validateForm()
    }

    function getSymbolPoolList() {
        return []
    }

    function getLinkedStockPoolBinding() {
        return {
            poolId: linkedStockPoolId,
            poolName: linkedStockPoolName,
            symbols: CustomStockPoolStore.CustomStockPoolStore.normalizeSymbolList(linkedStockPoolSymbols || []),
            hasBinding: !!String(linkedStockPoolId || "").trim()
        }
    }

    function refreshLinkedStockPools() {
        linkedStockPoolSelector.refreshPools()
    }

    function focusSymbolPoolField() {
        if (linkedStockPoolSelector && linkedStockPoolSelector.focusSelector) {
            linkedStockPoolSelector.focusSelector()
            return
        }
        root.forceActiveFocus()
    }
    
    // 获取资产类型值
    function getAssetTypeValue() {
        var values = Utils.StrategyCreationUtils.tr('strategyCreation.assetTypeValues')
        return values[assetTypeCombo.currentIndex] || "stock"
    }
    
    // 获取时间框架值
    function getTimeFrameValue() {
        var values = Utils.StrategyCreationUtils.tr('strategyCreation.timeFrameValues')
        return values[timeFrameCombo.currentIndex] || "daily"
    }
    
    // 获取风险等级值
    function getRiskLevelValue() {
        var values = ["low", "medium", "high", "aggressive"]
        return values[riskLevelCombo.currentIndex] || "medium"
    }
    
    // 获取优化方法值
    function getOptimizationMethodValue() {
        var values = Utils.StrategyCreationUtils.tr('strategyCreation.optimizationMethodValues')
        return values[optimizationCombo.currentIndex] || "genetic"
    }
    
    // 获取标签列表
    function getTagsList() {
        return tagsField.text.split(',').map(function(tag) {
            return tag.trim();
        }).filter(function(tag) {
            return tag.length > 0;
        });
    }

    function setBasicInfo(strategyData) {
        var values

        strategyNameField.text = strategyData.strategy_name || strategyData.strategyName || ""
        strategyDescField.text = strategyData.description || ""

        values = Utils.StrategyCreationUtils.tr('strategyCreation.assetTypeValues')
        assetTypeCombo.currentIndex = Math.max(0, values.indexOf(strategyData.asset_type || strategyData.assetType || "stock"))

        values = Utils.StrategyCreationUtils.tr('strategyCreation.timeFrameValues')
        timeFrameCombo.currentIndex = Math.max(0, values.indexOf(strategyData.time_frame || strategyData.timeFrame || "daily"))

        values = ["low", "medium", "high", "aggressive"]
        riskLevelCombo.currentIndex = Math.max(0, values.indexOf(strategyData.risk_level || strategyData.riskLevel || "medium"))

        values = Utils.StrategyCreationUtils.tr('strategyCreation.optimizationMethodValues')
        optimizationCombo.currentIndex = Math.max(0, values.indexOf(strategyData.optimization_method || strategyData.optimizationMethod || "genetic"))

        var linkedPoolBinding = CustomStockPoolStore.CustomStockPoolStore.extractLinkedStockPool(strategyData)
        linkedStockPoolId = linkedPoolBinding.poolId
        linkedStockPoolName = linkedPoolBinding.poolName
        linkedStockPoolSymbols = linkedPoolBinding.symbols
        linkedStockPoolSelector.refreshPools()
        linkedStockPoolSelector.setBinding(linkedStockPoolId, linkedStockPoolName, linkedStockPoolSymbols)

        var tags = strategyData.tags || []
        tagsField.text = typeof tags === "string" ? tags : (tags || []).join(', ')

        lastAutoDescription = strategyDescField.text
        lastAutoTagsText = tagsField.text
        updateTagsPreview()
        validateForm()
    }
    
    // ============ 初始化和信号连接 ============
    
    Component.onCompleted: {
        // 初始化标签预览
        tagsField.textChanged.connect(function() {
            updateTagsPreview()
        })
        
        // 初始化验证
        validateForm()
    }
}
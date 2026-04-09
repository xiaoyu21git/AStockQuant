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
    property alias symbolPoolText: symbolPoolField.text
    property alias assetType: assetTypeCombo.currentIndex
    property alias timeFrame: timeFrameCombo.currentIndex
    property alias riskLevel: riskLevelCombo.currentIndex
    property alias optimizationMethod: optimizationCombo.currentIndex
    property alias strategyTags: tagsField.text
    property string linkedStockPoolId: ""
    property string linkedStockPoolName: ""
    property var linkedStockPoolSymbols: []
    
    // 信号
    signal tagsChanged(var tagsList)
    signal validationChanged(bool isValid)

    property string lastAutoDescription: ""
    property string lastAutoTagsText: ""
    
    // ============ 主布局 ============
    
    color: "transparent"
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        Layout.alignment: Qt.AlignTop
        
        Text {
            text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyBasicInfo')
            font.pixelSize: 16
            font.weight: Font.Medium
            color: "#f1f5f9"
            Layout.fillWidth: true
        }
        
        // 策略名称
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
            
            // 策略名称错误提示
            Text {
                visible: strategyNameField.hasError && strategyNameField.text.trim() === ""
                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyNameError')
                font.pixelSize: 12
                color: "#ef4444"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        
        // 策略描述
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            
            Text {
                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyDescription')
                font.pixelSize: 14
                font.weight: Font.Medium
                color: "#f1f5f9"
            }
            
            TextArea {
                id: strategyDescField
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                placeholderText: Utils.StrategyCreationUtils.tr('strategyCreation.strategyDescriptionPlaceholder')
                text: ""
                wrapMode: Text.WordWrap
                
                background: Rectangle {
                    radius: 6
                    color: "#0f172a"
                    border.width: 1
                    border.color: "#334155"
                }
                
                color: "#f1f5f9"
                font.pixelSize: 14
                padding: 10
                
                onTextChanged: {
                    validateForm()
                }
            }
        }
        
        // 基本属性网格
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            
            // 资产类型
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
            
            // 时间框架
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
            
            // 风险等级
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
            
            // 优化方法
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
        
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "标的池"
                font.pixelSize: 14
                font.weight: Font.Medium
                color: "#f1f5f9"
            }

            TextArea {
                id: symbolPoolField
                Layout.fillWidth: true
                Layout.preferredHeight: 86
                placeholderText: "输入标的代码，支持逗号、空格或换行分隔，例如：600000.SH, 000001.SZ"
                wrapMode: TextEdit.Wrap

                property bool hasError: false

                background: Rectangle {
                    radius: 6
                    color: "#0f172a"
                    border.width: symbolPoolField.hasError ? 2 : 1
                    border.color: symbolPoolField.hasError ? "#ef4444" : "#334155"
                }

                color: "#f1f5f9"
                font.pixelSize: 14
                padding: 10

                onActiveFocusChanged: {
                    if (!activeFocus) {
                        symbolPoolField.hasError = root.getSymbolPoolList().length === 0
                    }
                    validateForm()
                }

                onTextChanged: {
                    symbolPoolField.hasError = text.trim() !== "" && root.getSymbolPoolList().length === 0
                    validateForm()
                }
            }

            Text {
                visible: symbolPoolField.hasError || (symbolPoolField.text.trim() === "" && !validateForm())
                text: "至少绑定一个标的，策略只会对池内 symbol 生成交易信号"
                font.pixelSize: 12
                color: "#ef4444"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }

        StockPoolComponents.LinkedStockPoolSelector {
            id: linkedStockPoolSelector
            Layout.fillWidth: true
            title: "关联自选股票池"
            helperText: "关联后会单独用于策略联动与实盘候选，不会覆盖这里的回测股票池。"

            onBindingChanged: function(binding) {
                root.linkedStockPoolId = binding.poolId || ""
                root.linkedStockPoolName = binding.poolName || ""
                root.linkedStockPoolSymbols = binding.symbols || []
            }
        }

        // 标签输入
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            
            Text {
                text: Utils.StrategyCreationUtils.tr('strategyCreation.tags')
                font.pixelSize: 14
                font.weight: Font.Medium
                color: "#f1f5f9"
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
                    border.width: 1
                    border.color: "#334155"
                }
                
                color: "#f1f5f9"
                font.pixelSize: 14
                padding: 10
            }
            
            // 标签预览
            Flow {
                Layout.fillWidth: true
                spacing: 6
                
                Repeater {
                    id: tagsRepeater
                    model: []
                    
                    delegate: Rectangle {
                        height: 28
                        width: Math.min(100, tagText.width + 16)
                        radius: 14
                        color: Qt.rgba(59/255, 130/255, 246/255, 0.1)
                        border.width: 1
                        border.color: "#3b82f6"
                        
                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 11
                            color: "#60a5fa"
                            padding: 4
                        }
                    }
                }
            }
        }
        
        // 占位Item，确保布局顶部对齐后下方有空间
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
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
        symbolPoolField.text = ""
        symbolPoolField.hasError = false
        linkedStockPoolId = ""
        linkedStockPoolName = ""
        linkedStockPoolSymbols = []
        linkedStockPoolSelector.refreshPools()
        linkedStockPoolSelector.clearBinding()
        tagsField.text = ""
        tagsRepeater.model = []
        lastAutoDescription = ""
        lastAutoTagsText = ""
        validateForm()
    }

    function updateTagsPreview() {
        var tags = getTagsList()
        tagsRepeater.model = tags
        root.tagsChanged(tags)
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
                      strategyDescField.text.trim() !== "" &&
                      getSymbolPoolList().length > 0
        root.validationChanged(isValid)
        return isValid
    }

    function isValid() {
        return validateForm()
    }

    function normalizeSymbolPoolInput(rawValue) {
        var values = []
        if (Array.isArray(rawValue)) {
            values = rawValue
        } else if (rawValue !== undefined && rawValue !== null) {
            values = String(rawValue).split(/[,;\s，；]+/)
        }

        var normalized = []
        for (var index = 0; index < values.length; ++index) {
            var token = String(values[index] || "").trim().toUpperCase()
            if (!token || normalized.indexOf(token) !== -1) {
                continue
            }
            normalized.push(token)
        }
        return normalized
    }

    function getSymbolPoolList() {
        return normalizeSymbolPoolInput(symbolPoolField.text)
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
        symbolPoolField.forceActiveFocus()
        symbolPoolField.cursorPosition = symbolPoolField.length
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

        var symbolPool = strategyData.symbol_pool || strategyData.symbolPool || []
        symbolPoolField.text = normalizeSymbolPoolInput(symbolPool).join(', ')
        symbolPoolField.hasError = false

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
// StrategyParamConfig.qml
// 策略参数配置组件 - 用于策略创建向导步骤2

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../../../utils/StrategyCreationUtils.js" as Utils
import "../../FactorWorkbench/Creation/components" as PluginComponents

Rectangle {
    id: root
    
    // ============ 属性 ============
    
    property string selectedStrategyType: "trend_following"
    property var strategyParameters: ({})
    property bool parametersValid: false
    property bool enableAdvancedOptions: false
    
    // 信号
    signal parametersChanged(var newParameters)
    signal validationChanged(bool allValid, var errors)
    signal advancedOptionsChanged(bool enabled)
    
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
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        
        // 隐藏滚动条
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        
        ColumnLayout {
            anchors.fill: parent
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
                            Utils.StrategyCreationUtils.tr('strategyCreation.personalizedParameters'),
                            Utils.StrategyCreationUtils.tr('strategyCreation.advancedParameters')
                        ]

                        delegate: Rectangle {
                            radius: 10
                            color: index === 2 ? "#1e293b" : "#172554"
                            border.width: 1
                            border.color: index === 2 ? "#475569" : "#2563eb"
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
            
            // 动态参数生成器
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 360
                radius: 10
                color: "#0f172a"
                border.width: 1
                border.color: "#334155"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    
                    Text {
                        text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterConfigPanel')
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        color: "#f1f5f9"
                    }
                    
                    // 使用现有的动态参数生成器
                    PluginComponents.DynamicParamGenerator {
                        id: dynamicGenerator
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        itemSpacing: 8
                        
                        paramRegistry: paramComponents
                        
                        onParamsChanged: function(newValues) {
                            root.strategyParameters = newValues
                            root.parametersChanged(newValues)
                        }
                        
                        onValidationChanged: function(allValid, errors) {
                            root.parametersValid = allValid
                            root.validationChanged(allValid, errors)
                        }
                        
                        Component.onCompleted: {
                            loadParamConfigs()
                        }
                    }
                    
                    // 参数统计信息
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
            
            // 高级参数选项
            Rectangle {
                Layout.fillWidth: true
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
                            text: Utils.StrategyCreationUtils.tr('strategyCreation.advancedParameters')
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
                            columns: 2
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
        }
    }
    
    // ============ 功能函数 ============
    
    // 加载参数配置
    function loadParamConfigs() {
        var paramConfigs = Utils.StrategyCreationUtils.buildParamConfigs(root.selectedStrategyType)
        if (dynamicGenerator) {
            dynamicGenerator.reloadConfigs(paramConfigs, [])
        }
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

    function applyPersistedStrategy(strategyType, parameters, advancedOptions) {
        var sourceParams = parameters || ({})
        var mappedValues = ({})

        function assignIfPresent(targetKey, sourceKeys, transform) {
            for (var index = 0; index < sourceKeys.length; ++index) {
                var key = sourceKeys[index]
                if (sourceParams[key] === undefined || sourceParams[key] === null || sourceParams[key] === "") {
                    continue
                }
                mappedValues[targetKey] = transform ? transform(sourceParams[key]) : sourceParams[key]
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

        assignIfPresent("positionSize", ["position_size", "positionSize"], ratioToPercent)
        assignIfPresent("stopLoss", ["stop_loss", "stopLoss"], ratioToPercent)
        assignIfPresent("takeProfit", ["take_profit", "takeProfit"], ratioToPercent)
        assignIfPresent("rebalanceDays", ["rebalance_days", "rebalanceDays", "rebalancingPeriod"], Number)

        if (strategyType === "trend_following") {
            assignIfPresent("fastPeriod", ["fast_period", "fastPeriod"], Number)
            assignIfPresent("slowPeriod", ["slow_period", "slowPeriod"], Number)
        } else if (strategyType === "mean_reversion") {
            assignIfPresent("bollPeriod", ["boll_period", "bollPeriod", "lookbackPeriod"], Number)
            assignIfPresent("bollStd", ["boll_std", "bollStd"], Number)
            assignIfPresent("reversionThreshold", ["reversion_threshold", "reversionThreshold"], Number)
        } else if (strategyType === "momentum") {
            assignIfPresent("momentumPeriod", ["momentum_period", "momentumPeriod"], Number)
            assignIfPresent("topN", ["top_n", "topN"], Number)
        } else if (strategyType === "arbitrage") {
            assignIfPresent("spreadThreshold", ["spread_threshold", "spreadThreshold"], Number)
            assignIfPresent("entryZScore", ["entry_z_score", "entryZScore"], Number)
            assignIfPresent("exitZScore", ["exit_z_score", "exitZScore"], Number)
        } else if (strategyType === "machine_learning") {
            assignIfPresent("featureWindow", ["feature_window", "featureWindow"], Number)
            assignIfPresent("predictionDays", ["prediction_days", "predictionDays"], Number)
            assignIfPresent("trainingDays", ["training_days", "trainingDays"], Number)
            assignIfPresent("confidenceThreshold", ["confidence_threshold", "confidenceThreshold"], ratioToPercent)
        } else if (strategyType === "multi_factor") {
            assignIfPresent("factorTypes", ["factor_types", "factorTypes"])
        } else if (strategyType === "high_frequency") {
            assignIfPresent("timeframe", ["execution_timeframe", "timeframe"])
        } else if (strategyType === "event_driven") {
            assignIfPresent("eventTypes", ["event_types", "eventTypes"])
        } else if (strategyType === "custom") {
            assignIfPresent("customCode", ["custom_code", "customCode"])
        }

        root.selectedStrategyType = strategyType || root.selectedStrategyType
        loadParamConfigs()
        root.strategyParameters = mappedValues
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
        root.advancedOptionsChanged(root.enableAdvancedOptions)
        root.validationChanged(root.parametersValid, {})
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
        root.strategyParameters = {}
        root.parametersValid = false
        root.enableAdvancedOptions = false
    }
    
    // 验证
    function isValid() {
        return root.parametersValid && Object.keys(root.strategyParameters).length > 0
    }
    
    // ============ 初始化和信号连接 ============
    
    Component.onCompleted: {
        // 注册参数组件
        paramComponents.registerAllComponents()
        
        // 加载初始参数配置
        loadParamConfigs()
    }
    
    onSelectedStrategyTypeChanged: {
        loadParamConfigs()
    }
}
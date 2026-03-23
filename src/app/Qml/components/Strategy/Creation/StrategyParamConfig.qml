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
            spacing: 16
            anchors.margins: 12
            
            // 参数配置标题
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                
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
                    spacing: 12
                    
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
                        itemSpacing: 10
                        
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
                            console.log("动态参数生成器初始化完成")
                            loadParamConfigs()
                        }
                    }
                    
                    // 参数统计信息
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        
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
                Layout.minimumHeight: root.enableAdvancedOptions ? 200 : 60
                radius: 10
                color: "#0f172a"
                border.width: 1
                border.color: "#334155"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    
                    // 标题和切换
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        
                        Text {
                            text: "高级参数配置"
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
                        spacing: 12
                        visible: root.enableAdvancedOptions
                        
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 12
                            
                            // 参数优化范围
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterOptimizationRange')
                                    font.pixelSize: 12
                                    color: "#cbd5e1"
                                }
                                
                                ComboBox {
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
                                spacing: 6
                                
                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.sensitivityAnalysis')
                                    font.pixelSize: 12
                                    color: "#cbd5e1"
                                }
                                
                                ComboBox {
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
                                spacing: 6
                                
                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterConstraints')
                                    font.pixelSize: 12
                                    color: "#cbd5e1"
                                }
                                
                                ComboBox {
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
                                spacing: 6
                                
                                Text {
                                    text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterInitializationMethod')
                                    font.pixelSize: 12
                                    color: "#cbd5e1"
                                }
                                
                                ComboBox {
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
        console.log("加载参数配置，策略类型:", root.selectedStrategyType)
        var paramConfigs = Utils.StrategyCreationUtils.buildParamConfigs(root.selectedStrategyType)
        console.log("参数配置数量:", paramConfigs.length)
        if (dynamicGenerator) {
            dynamicGenerator.reloadConfigs(paramConfigs, [])
        }
    }
    
    // 重置表单
    function reset() {
        if (dynamicGenerator) {
            dynamicGenerator.reset()
        }
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
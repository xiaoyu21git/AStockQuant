// SliderParam.qml
// 滑块参数组件 - 用于数值类型参数
// 支持整数和浮点数，带数值输入和滑块

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 滑块参数组件
 * 
 * 配置项 (config):
 *   - id: 参数唯一标识
 *   - label: 显示标签
 *   - description: 描述文本
 *   - min: 最小值（默认0）
 *   - max: 最大值（默认100）
 *   - step: 步长（默认1）
 *   - default: 默认值
 *   - unit: 单位（如 "天", "%"）
 *   - required: 是否必填
 *   - decimals: 小数位数（默认0为整数）
 *   - showPresets: 是否显示预设值按钮
 *   - presets: 预设值数组 [20, 60, 120, 250]
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    property var config: ({})
    property var value: config.default !== undefined ? config.default : 0
    property bool isValid: true
    property string errorMessage: ""
    
    // 计算属性
    property string paramId: config.id || ""
    property string label: config.label || config.displayName || paramId
    property string description: config.description || ""
    property real minValue: config.min !== undefined ? config.min : 0
    property real maxValue: config.max !== undefined ? config.max : 100
    property real stepValue: config.step !== undefined ? config.step : 1
    property string unit: config.unit || ""
    property bool required: config.required || false
    property int decimals: config.decimals !== undefined ? config.decimals : 0
    property bool showPresets: config.showPresets !== undefined ? config.showPresets : false
    property var presets: config.presets || config.commonValues || []
    
    // 自定义信号
    signal paramValueChanged(string id, var newValue)
    signal paramValidationChanged(string id, bool valid, string message)
    
    // ============ 外观配置 ============
    
    implicitWidth: parent ? parent.width : 400
    implicitHeight: contentLayout.implicitHeight + 16
    radius: 8
    color: mouseArea.containsMouse ? "#1E293B" : "transparent"
    border.color: !isValid ? "#EF4444" : mouseArea.containsMouse ? "#334155" : "transparent"
    border.width: 1
    
    // ============ UI 布局 ============
    
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
    
    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8
        
        // 标签行
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            
            // 标签
            Text {
                text: root.label
                font.pixelSize: 14
                font.weight: Font.Medium
                color: "#F1F5F9"
            }
            
            // 必填标记
            Text {
                text: "*"
                font.pixelSize: 14
                color: "#EF4444"
                visible: root.required
            }
            
            Item { Layout.fillWidth: true }
            
            // 当前值显示
            Text {
                text: formatValue(root.value) + (root.unit ? " " + root.unit : "")
                font.pixelSize: 14
                font.weight: Font.Bold
                color: "#3B82F6"
            }
        }
        
        // 滑块和输入行
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            
            // 最小值标签
            Text {
                text: formatValue(root.minValue)
                font.pixelSize: 11
                color: "#64748B"
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
            
            // 滑块
            Slider {
                id: slider
                Layout.fillWidth: true
                from: root.minValue
                to: root.maxValue
                stepSize: root.stepValue
                value: root.value
                
                background: Rectangle {
                    x: slider.leftPadding
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    implicitWidth: 200
                    implicitHeight: 6
                    width: slider.availableWidth
                    height: implicitHeight
                    radius: 3
                    color: "#334155"
                    
                    Rectangle {
                        width: slider.visualPosition * parent.width
                        height: parent.height
                        color: "#3B82F6"
                        radius: 3
                    }
                }
                
                handle: Rectangle {
                    x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    implicitWidth: 18
                    implicitHeight: 18
                    radius: 9
                    color: slider.pressed ? "#60A5FA" : "#3B82F6"
                    border.color: "#1E293B"
                    border.width: 2
                    
                    Behavior on color { ColorAnimation { duration: 100 } }
                }
                
                onValueChanged: {
                    if (activeFocus || pressed) {
                        updateValue(value)
                    }
                }
            }
            
            // 最大值标签
            Text {
                text: formatValue(root.maxValue)
                font.pixelSize: 11
                color: "#64748B"
                Layout.preferredWidth: 40
            }
            
            // 数值输入框
            SpinBox {
                id: spinBox
                Layout.preferredWidth: 100
                from: root.minValue * Math.pow(10, root.decimals)
                to: root.maxValue * Math.pow(10, root.decimals)
                stepSize: root.stepValue * Math.pow(10, root.decimals)
                value: root.value * Math.pow(10, root.decimals)
                editable: true
                
                property real realValue: value / Math.pow(10, root.decimals)
                
                textFromValue: function(value, locale) {
                    return formatValue(value / Math.pow(10, root.decimals))
                }
                
                valueFromText: function(text, locale) {
                    return parseFloat(text) * Math.pow(10, root.decimals)
                }
                
                onValueModified: {
                    updateValue(realValue)
                }
                
                background: Rectangle {
                    implicitWidth: 100
                    implicitHeight: 32
                    radius: 6
                    color: "#1E293B"
                    border.color: spinBox.activeFocus ? "#3B82F6" : "#334155"
                    border.width: 1
                }
                
                contentItem: TextInput {
                    text: spinBox.textFromValue(spinBox.value, spinBox.locale)
                    font.pixelSize: 13
                    color: "#F1F5F9"
                    horizontalAlignment: Qt.AlignHCenter
                    verticalAlignment: Qt.AlignVCenter
                    selectByMouse: true
                    validator: DoubleValidator {
                        bottom: root.minValue
                        top: root.maxValue
                    }
                }
            }
        }
        
        // 预设值按钮行
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.showPresets && root.presets.length > 0
            
            Text {
                text: "快速设置:"
                font.pixelSize: 12
                color: "#94A3B8"
            }
            
            Repeater {
                model: root.presets
                
                Rectangle {
                    width: presetText.implicitWidth + 16
                    height: 24
                    radius: 12
                    color: root.value === modelData ? "#3B82F6" : "#1E293B"
                    border.color: root.value === modelData ? "#60A5FA" : "#334155"
                    border.width: 1
                    
                    Text {
                        id: presetText
                        anchors.centerIn: parent
                        text: formatValue(modelData) + (root.unit ? root.unit : "")
                        font.pixelSize: 11
                        color: root.value === modelData ? "#FFFFFF" : "#94A3B8"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            updateValue(modelData)
                        }
                    }
                    
                    Behavior on color { ColorAnimation { duration: 100 } }
                }
            }
            
            Item { Layout.fillWidth: true }
        }
        
        // 描述文本
        Text {
            text: root.description
            font.pixelSize: 12
            color: "#64748B"
            visible: root.description !== ""
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        
        // 错误信息
        Text {
            text: root.errorMessage
            font.pixelSize: 12
            color: "#EF4444"
            visible: !root.isValid && root.errorMessage !== ""
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    
    // ============ 方法 ============
    
    function formatValue(val) {
        if (root.decimals > 0) {
            return val.toFixed(root.decimals)
        }
        return Math.round(val).toString()
    }
    
    function updateValue(newValue) {
        // 约束值在范围内
        newValue = Math.max(root.minValue, Math.min(root.maxValue, newValue))
        
        if (root.value !== newValue) {
            root.value = newValue
            slider.value = newValue
            spinBox.value = newValue * Math.pow(10, root.decimals)
            
            // 验证
            validate()
            
            // 发出信号
            root.paramValueChanged(root.paramId, newValue)
        }
    }
    
    function validate() {
        var validation = { valid: true, message: "" }
        
        // 必填验证
        if (root.required && (root.value === undefined || root.value === null)) {
            validation.valid = false
            validation.message = root.label + " 不能为空"
        }
        
        // 范围验证
        if (root.value < root.minValue) {
            validation.valid = false
            validation.message = root.label + " 不能小于 " + root.minValue
        }
        if (root.value > root.maxValue) {
            validation.valid = false
            validation.message = root.label + " 不能大于 " + root.maxValue
        }
        
        root.isValid = validation.valid
        root.errorMessage = validation.message
        
        root.paramValidationChanged(root.paramId, validation.valid, validation.message)
        
        return validation.valid
    }
    
    function getValue() {
        return root.value
    }
    
    function setValue(newValue) {
        updateValue(newValue)
    }
    
    function reset() {
        var defaultVal = config.default !== undefined ? config.default : root.minValue
        updateValue(defaultVal)
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化值
        if (config.default !== undefined) {
            root.value = config.default
        }
        slider.value = root.value
        spinBox.value = root.value * Math.pow(10, root.decimals)
    }
    
    onConfigChanged: {
        if (config.default !== undefined) {
            updateValue(config.default)
        }
    }
}

// DynamicParamForm.qml
// 基于 JSON Schema 的动态表单生成器
// 支持多种参数类型：数字、字符串、布尔、枚举、数组、对象

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 基于 JSON Schema 的动态表单生成器
 * schema 格式：
 * {
 *   "title": "参数配置",
 *   "properties": {
 *     "lookback": {
 *       "type": "number",
 *       "label": "回看周期",
 *       "description": "计算因子值所需的历史数据长度",
 *       "default": 20,
 *       "minimum": 5,
 *       "maximum": 250,
 *       "step": 5,
 *       "unit": "天"
 *     },
 *     "method": {
 *       "type": "string",
 *       "label": "计算方法",
 *       "description": "使用的计算方法",
 *       "enum": ["简单动量", "加权动量", "残差动量"],
 *       "default": "简单动量"
 *     },
 *     "neutralize": {
 *       "type": "boolean",
 *       "label": "中性化处理",
 *       "description": "是否进行行业中性化",
 *       "default": false
 *     }
 *   }
 * }
 */
ColumnLayout {
    id: root
    
    // ============ 公共属性 ============
    
    property var schema: ({})  // JSON Schema
    property var values: ({})  // 当前值
    property var errors: ({})  // 验证错误
    
    property int spacing: 16
    property int labelWidth: 160
    
    // 参数变更信号
    signal valueChanged(string key, var value)
    signal validationChanged(bool valid, string message)
    
    // ============ UI 布局 ============

    // 动态生成控件
    Repeater {
        id: paramsRepeater
        model: {
            // 将 schema.properties 转换为数组供 Repeater 使用
            var props = []
            if (root.schema && root.schema.properties) {
                for (var key in root.schema.properties) {
                    props.push({
                        key: key,
                        config: root.schema.properties[key]
                    })
                }
            }
            return props
        }
        
        delegate: Item {
            id: paramDelegate
            Layout.fillWidth: true
            Layout.preferredHeight: controlLoader.height + (errorText.visible ? errorText.height + 4 : 0)
            
            property string paramKey: modelData.key
            property var paramConfig: modelData.config
            
            // 动态选择控件类型
            Loader {
                id: controlLoader
                width: parent.width
                sourceComponent: {
                    if (!paramConfig || !paramConfig.type) return stringInputComponent
                    
                    switch (paramConfig.type) {
                        case "integer":
                        case "number":
                            return numberInputComponent
                        case "boolean":
                            return booleanInputComponent
                        case "string":
                            return paramConfig.enum ? dropdownComponent : stringInputComponent
                        case "array":
                            return arrayInputComponent
                        case "object":
                            return objectInputComponent
                        default:
                            return stringInputComponent
                    }
                }
                
                // 传递属性到控件
                onLoaded: {
                    if (!item) return
                    
                    item.label = paramConfig.label || paramKey
                    item.description = paramConfig.description || ""
                    item.required = paramConfig.required || false
                    
                    // 设置默认值
                    if (!(paramKey in root.values) && paramConfig.default !== undefined) {
                        root.values[paramKey] = paramConfig.default
                    }
                    
                    // 传递特定类型属性
                    if (paramConfig.type === "number" || paramConfig.type === "integer") {
                        item.minimum = paramConfig.minimum !== undefined ? paramConfig.minimum : 0
                        item.maximum = paramConfig.maximum !== undefined ? paramConfig.maximum : 100
                        item.step = paramConfig.step || 1
                        item.unit = paramConfig.unit || ""
                        item.decimals = paramConfig.decimals || 2
                    }
                    
                    if (paramConfig.enum) {
                        item.model = paramConfig.enum
                    }
                    
                    // 设置初始值
                    item.value = root.values[paramKey] !== undefined ? 
                        root.values[paramKey] : paramConfig.default
                    
                    // 监听值变化
                    item.valueModified.connect(function(value) {
                        root.values[paramKey] = value
                        root.valueChanged(paramKey, value)
                        validateField(paramKey, value)
                    })
                }
            }
            
            // 错误提示
            Text {
                id: errorText
                anchors.top: controlLoader.bottom
                anchors.topMargin: 4
                anchors.left: parent.left
                anchors.right: parent.right
                text: root.errors[paramKey] || ""
                color: "#EF4444"
                font.pixelSize: 12
                visible: text !== ""
                wrapMode: Text.WordWrap
            }
        }
    }
    
    // ============ 工具函数 ============
    
    // 验证单个字段
    function validateField(key, value) {
        var config = schema.properties[key]
        if (!config) {
            delete errors[key]
            return true
        }
        
        // 必填验证
        if (config.required && (value === undefined || value === null || value === "")) {
            errors[key] = `${config.label || key} 不能为空`
            updateValidationState()
            return false
        }
        
        // 数值范围验证
        if ((config.type === "number" || config.type === "integer") && value !== undefined) {
            if (config.minimum !== undefined && value < config.minimum) {
                errors[key] = `${config.label || key} 不能小于 ${config.minimum}`
                updateValidationState()
                return false
            }
            if (config.maximum !== undefined && value > config.maximum) {
                errors[key] = `${config.label || key} 不能大于 ${config.maximum}`
                updateValidationState()
                return false
            }
        }
        
        // 枚举值验证
        if (config.enum && value !== undefined) {
            if (!config.enum.includes(value)) {
                errors[key] = `${config.label || key} 必须是有效选项`
                updateValidationState()
                return false
            }
        }
        
        delete errors[key]
        updateValidationState()
        return true
    }
    
    // 整体验证
    function validate() {
        var valid = true
        for (var key in schema.properties) {
            if (!validateField(key, root.values[key])) {
                valid = false
            }
        }
        return valid
    }
    
    // 获取所有参数值
    function getValues() {
        return root.values
    }
    
    // 重置所有参数为默认值
    function resetToDefaults() {
        for (var key in schema.properties) {
            var config = schema.properties[key]
            if (config.default !== undefined) {
                root.values[key] = config.default
                valueChanged(key, config.default)
            }
        }
        errors = {}
        updateValidationState()
    }
    
    // 更新验证状态
    function updateValidationState() {
        var isValid = Object.keys(errors).length === 0
        var message = isValid ? "参数验证通过" : "存在验证错误"
        validationChanged(isValid, message)
    }
    
    // ============ 控件组件定义 ============
    
    // 数字输入组件
    Component {
        id: numberInputComponent
        
        ColumnLayout {
            property alias label: labelText.text
            property string description: ""
            property var value: 0
            property real minimum: 0
            property real maximum: 100
            property real step: 1
            property string unit: ""
            property int decimals: 2
            property bool required: false
            
            signal valueModified(var newValue)
            
            spacing: 4
            
            // 标签行
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    id: labelText
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: "#F1F5F9"
                    Layout.preferredWidth: root.labelWidth
                }
                
                Text {
                    text: required ? "*" : ""
                    color: "#EF4444"
                    font.pixelSize: 14
                    visible: required
                }
                
                Item { Layout.fillWidth: true }
                
                Text {
                    text: unit
                    font.pixelSize: 12
                    color: "#94A3B8"
                    visible: unit !== ""
                }
            }
            
            // 输入控件行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                
                // 滑块
                Slider {
                    id: slider
                    Layout.fillWidth: true
                    from: minimum
                    to: maximum
                    stepSize: step
                    value: parent.value
                    
                    onValueChanged: {
                        if (activeFocus) {
                            spinBox.value = value
                            parent.valueModified(value)
                        }
                    }
                }
                
                // 数字输入框
                SpinBox {
                    id: spinBox
                    Layout.preferredWidth: 100
                    from: slider.from
                    to: slider.to
                    stepSize: step
                    value: slider.value
                    
                    onValueChanged: {
                        if (activeFocus) {
                            slider.value = value
                            parent.valueModified(value)
                        }
                    }
                }
            }
            
            // 描述文本
            Text {
                text: description
                font.pixelSize: 12
                color: "#94A3B8"
                visible: description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 下拉选择组件
    Component {
        id: dropdownComponent
        
        ColumnLayout {
            property alias label: labelText.text
            property string description: ""
            property var value: ""
            property var model: []
            property bool required: false
            
            signal valueModified(var newValue)
            
            spacing: 4
            
            // 标签行
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    id: labelText
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: "#F1F5F9"
                    Layout.preferredWidth: root.labelWidth
                }
                
                Text {
                    text: required ? "*" : ""
                    color: "#EF4444"
                    font.pixelSize: 14
                    visible: required
                }
            }
            
            // 下拉框
            ComboBox {
                Layout.fillWidth: true
                model: parent.model
                currentIndex: {
                    var currentValue = parent.value
                    for (var i = 0; i < parent.model.length; i++) {
                        if (parent.model[i] === currentValue) {
                            return i
                        }
                    }
                    return -1
                }
                
                onCurrentTextChanged: {
                    if (activeFocus && currentIndex >= 0) {
                        parent.value = currentText
                        parent.valueModified(currentText)
                    }
                }
            }
            
            // 描述文本
            Text {
                text: description
                font.pixelSize: 12
                color: "#94A3B8"
                visible: description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 布尔输入组件
    Component {
        id: booleanInputComponent
        
        RowLayout {
            property alias label: labelText.text
            property string description: ""
            property bool value: false
            property bool required: false
            
            signal valueModified(bool newValue)
            
            spacing: 12
            
            // 复选框
            CheckBox {
                checked: parent.value
                
                onCheckedChanged: {
                    if (activeFocus) {
                        parent.value = checked
                        parent.valueModified(checked)
                    }
                }
            }
            
            // 标签和描述
            ColumnLayout {
                spacing: 2
                
                RowLayout {
                    Text {
                        id: labelText
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                    }
                    
                    Text {
                        text: required ? "*" : ""
                        color: "#EF4444"
                        font.pixelSize: 14
                        visible: required
                    }
                }
                
                Text {
                    text: description
                    font.pixelSize: 12
                    color: "#94A3B8"
                    visible: description !== ""
                    wrapMode: Text.WordWrap
                }
            }
            
            Item { Layout.fillWidth: true }
        }
    }
    
    // 字符串输入组件
    Component {
        id: stringInputComponent
        
        ColumnLayout {
            property alias label: labelText.text
            property string description: ""
            property string value: ""
            property bool required: false
            
            signal valueModified(string newValue)
            
            spacing: 4
            
            // 标签行
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    id: labelText
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: "#F1F5F9"
                    Layout.preferredWidth: root.labelWidth
                }
                
                Text {
                    text: required ? "*" : ""
                    color: "#EF4444"
                    font.pixelSize: 14
                    visible: required
                }
            }
            
            // 文本输入框
            TextField {
                Layout.fillWidth: true
                text: parent.value
                placeholderText: description || "请输入..."
                
                onTextChanged: {
                    if (activeFocus) {
                        parent.value = text
                        parent.valueModified(text)
                    }
                }
            }
            
            // 描述文本
            Text {
                text: description
                font.pixelSize: 12
                color: "#94A3B8"
                visible: description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 数组输入组件（简化版）
    Component {
        id: arrayInputComponent
        
        ColumnLayout {
            property alias label: labelText.text
            property string description: ""
            property var value: []
            property bool required: false
            
            signal valueModified(var newValue)
            
            spacing: 4
            
            // 标签行
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    id: labelText
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: "#F1F5F9"
                    Layout.preferredWidth: root.labelWidth
                }
                
                Text {
                    text: required ? "*" : ""
                    color: "#EF4444"
                    font.pixelSize: 14
                    visible: required
                }
            }
            
            // 数组项显示
            Text {
                text: "数组类型参数（" + (value ? value.length : 0) + " 个元素）"
                font.pixelSize: 12
                color: "#94A3B8"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            
            // 描述文本
            Text {
                text: description
                font.pixelSize: 12
                color: "#94A3B8"
                visible: description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 对象输入组件（简化版）
    Component {
        id: objectInputComponent
        
        ColumnLayout {
            property alias label: labelText.text
            property string description: ""
            property var value: {}
            property bool required: false
            
            signal valueModified(var newValue)
            
            spacing: 4
            
            // 标签行
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    id: labelText
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: "#F1F5F9"
                    Layout.preferredWidth: root.labelWidth
                }
                
                Text {
                    text: required ? "*" : ""
                    color: "#EF4444"
                    font.pixelSize: 14
                    visible: required
                }
            }
            
            // 对象类型显示
            Text {
                text: "对象类型参数"
                font.pixelSize: 12
                color: "#94A3B8"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            
            // 描述文本
            Text {
                text: description
                font.pixelSize: 12
                color: "#94A3B8"
                visible: description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始验证
        if (schema && schema.properties) {
            for (var key in schema.properties) {
                validateField(key, values[key])
            }
        }
    }
    
    onSchemaChanged: {
        // 重置验证状态
        errors = {}
        updateValidationState()
    }
}

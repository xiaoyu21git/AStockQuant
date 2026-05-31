// InputParam.qml
// 文本输入参数组件 - 用于字符串类型参数
// 支持单行文本、多行文本、密码输入等

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 文本输入参数组件
 * 
 * 配置项 (config):
 *   - id: 参数唯一标识
 *   - label: 显示标签
 *   - description: 描述文本
 *   - default: 默认值
 *   - required: 是否必填
 *   - placeholder: 占位文本
 *   - multiline: 是否多行输入
 *   - password: 是否密码输入
 *   - maxLength: 最大长度限制
 *   - minLength: 最小长度限制
 *   - pattern: 正则表达式验证
 *   - patternMessage: 正则验证失败提示
 *   - validator: 自定义验证器类型 ("email", "url", "number", "integer")
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    property var config: ({})
    property var value: ""
    property bool isValid: true
    property string errorMessage: ""
    
    // 计算属性
    property string paramId: config.id || ""
    property string label: config.label || config.displayName || paramId
    property string description: config.description || ""
    property bool required: config.required || false
    property string placeholder: config.placeholder || "请输入..."
    property bool multiline: config.multiline || false
    property bool password: config.password || false
    property bool serializeAsJson: config.serializeAsJson || false
    property int maxLength: config.maxLength || 100
    property int minLength: config.minLength || 0
    property string pattern: config.pattern || ""
    property string patternMessage: config.patternMessage || "格式不正确"
    property string validatorType: config.validator || ""
    
    // 信号
    signal paramValueChanged(string id, var newValue)
    signal validationChanged(string id, bool valid, string message)
    
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
            
            // 字符计数
            Text {
                text: root.value.length + "/" + root.maxLength
                font.pixelSize: 11
                color: root.value.length > root.maxLength ? "#EF4444" : "#64748B"
                visible: root.maxLength > 0
            }
        }
        
        // 文本输入框
        Loader {
            id: inputLoader
            Layout.fillWidth: true
            Layout.preferredHeight: root.multiline ? 60 : 36
            
            sourceComponent: root.multiline ? textAreaComponent : textFieldComponent
            
            onLoaded: {
                if (item) {
                    item.text = root.value
                    item.placeholderText = root.placeholder
                    if (!root.multiline) {
                        item.echoMode = root.password ? TextInput.Password : TextInput.Normal
                    }
                    
                    // 设置验证器
                    setupValidator(item)
                }
            }
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
    
    // ============ 组件定义 ============
    
    // 单行文本输入框组件
    Component {
        id: textFieldComponent
        
        TextField {
            id: textField
            
            background: Rectangle {
                implicitWidth: 200
                implicitHeight: 40
                radius: 8
                color: textField.activeFocus ? "#1E293B" : "#1E293B"
                border.color: textField.activeFocus ? "#3B82F6" : 
                             textField.hovered ? "#475569" : "#334155"
                border.width: 1
                
                Behavior on border.color { ColorAnimation { duration: 100 } }
            }
            
            color: "#F1F5F9"
            font.pixelSize: 14
            selectByMouse: true
            maximumLength: root.maxLength
            
            onTextChanged: {
                if (activeFocus) {
                    updateValue(text)
                }
            }
            
            onEditingFinished: {
                validate()
            }
        }
    }
    
    // 多行文本输入框组件
    Component {
        id: textAreaComponent
        
        TextArea {
            id: textArea
            
            background: Rectangle {
                implicitWidth: 200
                implicitHeight: 80
                radius: 8
                color: textArea.activeFocus ? "#1E293B" : "#1E293B"
                border.color: textArea.activeFocus ? "#3B82F6" : 
                             textArea.hovered ? "#475569" : "#334155"
                border.width: 1
                
                Behavior on border.color { ColorAnimation { duration: 100 } }
            }
            
            color: "#F1F5F9"
            font.pixelSize: 14
            selectByMouse: true
            wrapMode: Text.WordWrap
            // maximumLength: root.maxLength  // TextArea 不支持 maximumLength
            
            onTextChanged: {
                if (activeFocus) {
                    updateValue(text)
                }
            }
            
            onEditingFinished: {
                validate()
            }
        }
    }
    
    // ============ 方法 ============
    
    function setupValidator(inputItem) {
        // 已弃用，validator 绑定已在 QML 组件内实现
        return
    }
    
    function updateValue(newValue) {
        if (root.value !== newValue) {
            root.value = newValue
            // 更新输入框显示
            if (inputLoader.item) {
                inputLoader.item.text = newValue
            }
            // 验证
            validate()
            // 发出信号
            root.paramValueChanged(root.paramId, newValue)
        }
    }
    
    function validate() {
        var validation = { valid: true, message: "" }
        
        // 必填验证
        if (root.required && (root.value === undefined || root.value === null || root.value === "")) {
            validation.valid = false
            validation.message = root.label + " 不能为空"
        }
        
        // 长度验证
        if (root.value !== undefined && root.value !== null) {
            var strValue = String(root.value)
            
            if (root.minLength > 0 && strValue.length < root.minLength) {
                validation.valid = false
                validation.message = root.label + " 长度不能少于 " + root.minLength + " 个字符"
            }
            
            if (root.maxLength > 0 && strValue.length > root.maxLength) {
                validation.valid = false
                validation.message = root.label + " 长度不能超过 " + root.maxLength + " 个字符"
            }
            
            // 正则表达式验证
            if (root.pattern && strValue !== "") {
                var regex = new RegExp(root.pattern)
                if (!regex.test(strValue)) {
                    validation.valid = false
                    validation.message = root.label + " " + root.patternMessage
                }
            }
            
            // 特定类型验证
            if (root.validatorType && strValue !== "") {
                switch (root.validatorType) {
                    case "email":
                        var emailRegex = /^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/
                        if (!emailRegex.test(strValue)) {
                            validation.valid = false
                            validation.message = root.label + " 邮箱格式不正确"
                        }
                        break
                    case "url":
                        var urlRegex = /^(https?:\/\/)?([\da-z.-]+)\.([a-z.]{2,6})([/\w .-]*)*\/?$/
                        if (!urlRegex.test(strValue)) {
                            validation.valid = false
                            validation.message = root.label + " URL格式不正确"
                        }
                        break
                    case "number":
                        if (isNaN(parseFloat(strValue))) {
                            validation.valid = false
                            validation.message = root.label + " 必须是数字"
                        }
                        break
                    case "integer":
                        if (!/^\d+$/.test(strValue)) {
                            validation.valid = false
                            validation.message = root.label + " 必须是整数"
                        }
                        break
                    case "json":
                        try {
                            JSON.parse(strValue)
                        } catch (error) {
                            validation.valid = false
                            validation.message = root.label + " 必须是合法的 JSON"
                        }
                        break
                }
            }
        }
        
        root.isValid = validation.valid
        root.errorMessage = validation.message
        
        root.validationChanged(root.paramId, validation.valid, validation.message)
        
        return validation.valid
    }
    
    function getValue() {
        return root.value
    }
    
    function setValue(newValue) {
        var nextValue = newValue
        if (root.serializeAsJson) {
            if (nextValue === undefined || nextValue === null || nextValue === "") {
                nextValue = "[]"
            } else if (typeof nextValue !== "string") {
                try {
                    nextValue = JSON.stringify(nextValue, null, 2)
                } catch (error) {
                    nextValue = String(nextValue)
                }
            }
        }
        updateValue(nextValue)
    }
    
    function reset() {
        var defaultVal = config.default !== undefined ? config.default : ""
        updateValue(defaultVal)
    }
    
    function focusInput() {
        if (inputLoader.item) {
            inputLoader.item.forceActiveFocus()
        }
    }
    
    function selectAll() {
        if (inputLoader.item) {
            inputLoader.item.selectAll()
        }
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化值
        if (config.default !== undefined) {
            setValue(config.default)
        }
    }
    
    onConfigChanged: {
        if (config.default !== undefined) {
            setValue(config.default)
        }
    }
}

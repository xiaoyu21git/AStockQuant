// ToggleParam.qml
// 开关切换参数组件 - 用于布尔类型参数
// 支持开关切换、复选框等布尔值输入

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 开关切换参数组件
 * 
 * 配置项 (config):
 *   - id: 参数唯一标识
 *   - label: 显示标签
 *   - description: 描述文本
 *   - default: 默认值（true/false）
 *   - required: 是否必填（对于布尔值通常为false）
 *   - style: 显示样式 ("switch", "checkbox", "toggle")
 *   - trueLabel: 开启状态标签
 *   - falseLabel: 关闭状态标签
 *   - trueIcon: 开启状态图标
 *   - falseIcon: 关闭状态图标
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    property var config: ({})
    property var value: false
    property bool isValid: true
    property string errorMessage: ""
    
    // 计算属性
    property string paramId: config.id || ""
    property string label: config.label || config.displayName || paramId
    property string description: config.description || ""
    property bool required: config.required || false
    property string style: config.style || "switch"
    property string trueLabel: config.trueLabel || "开启"
    property string falseLabel: config.falseLabel || "关闭"
    property string trueIcon: config.trueIcon || "✓"
    property string falseIcon: config.falseIcon || "✗"
    
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
            
            // 状态显示
            Text {
                text: root.value ? root.trueLabel : root.falseLabel
                font.pixelSize: 12
                font.weight: Font.Medium
                color: root.value ? "#10B981" : "#64748B"
            }
        }
        
        // 开关控件
        Loader {
            id: toggleLoader
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            
            sourceComponent: {
                switch (root.style) {
                    case "checkbox": return checkboxComponent
                    case "toggle": return toggleButtonComponent
                    default: return switchComponent
                }
            }
            
            onLoaded: {
                if (item) {
                    // 安全设置checked属性，确保组件已完全初始化
                    if (typeof item.checked !== "undefined") {
                        item.checked = root.value
                    } else {
                        // 如果checked属性不存在，使用Qt.callLater稍后设置
                        Qt.callLater(function() {
                            if (item && typeof item.checked !== "undefined") {
                                item.checked = root.value
                            }
                        })
                    }
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
    
    // 开关组件
    Component {
        id: switchComponent
        
        RowLayout {
            id: switchRow
            property bool checked: false
            
            spacing: 12
            
            // 开关控件
            Rectangle {
                id: switchTrack
                width: 52
                height: 28
                radius: 14
                color: switchRow.checked ? "#10B981" : "#334155"
                border.color: switchRow.checked ? "#34D399" : "#475569"
                border.width: 1
                
                Rectangle {
                    id: switchHandle
                    width: 24
                    height: 24
                    radius: 12
                    color: "#FFFFFF"
                    anchors.verticalCenter: parent.verticalCenter
                    x: switchRow.checked ? parent.width - width - 2 : 2
                    
                    Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        switchRow.checked = !switchRow.checked
                        updateValue(switchRow.checked)
                    }
                }
                
                Behavior on color { ColorAnimation { duration: 150 } }
            }
            
            // 开关标签
            ColumnLayout {
                spacing: 2
                
                Text {
                    text: switchRow.checked ? root.trueLabel : root.falseLabel
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: switchRow.checked ? "#10B981" : "#64748B"
                }
                
                Text {
                    text: switchRow.checked ? "已启用" : "已禁用"
                    font.pixelSize: 11
                    color: "#94A3B8"
                }
            }
            
            Item { Layout.fillWidth: true }
        }
    }
    
    // 复选框组件
    Component {
        id: checkboxComponent
        
        RowLayout {
            property bool checked: false
            
            spacing: 12
            
            // 复选框
            Rectangle {
                width: 20
                height: 20
                radius: 4
                color: parent.checked ? "#3B82F6" : "#1E293B"
                border.color: parent.checked ? "#60A5FA" : "#334155"
                border.width: 1
                
                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    font.pixelSize: 12
                    color: "#FFFFFF"
                    visible: parent.checked
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        parent.checked = !parent.checked
                        updateValue(parent.checked)
                    }
                }
                
                Behavior on color { ColorAnimation { duration: 100 } }
            }
            
            // 复选框标签
            ColumnLayout {
                spacing: 2
                
                Text {
                    text: root.label
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: "#F1F5F9"
                }
                
                Text {
                    text: parent.checked ? root.trueLabel : root.falseLabel
                    font.pixelSize: 11
                    color: "#94A3B8"
                }
            }
            
            Item { Layout.fillWidth: true }
        }
    }
    
    // 切换按钮组件
    Component {
        id: toggleButtonComponent
        
        RowLayout {
            property bool checked: false
            
            spacing: 8
            
            // 关闭状态按钮
            Rectangle {
                width: 80
                height: 36
                radius: 8
                color: !parent.checked ? "#3B82F6" : "#1E293B"
                border.color: !parent.checked ? "#60A5FA" : "#334155"
                border.width: 1
                
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    Text {
                        text: root.falseIcon
                        font.pixelSize: 14
                        color: !parent.checked ? "#FFFFFF" : "#94A3B8"
                    }
                    
                    Text {
                        text: root.falseLabel
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: !parent.checked ? "#FFFFFF" : "#94A3B8"
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (parent.checked) {
                            parent.checked = false
                            updateValue(false)
                        }
                    }
                }
                
                Behavior on color { ColorAnimation { duration: 100 } }
            }
            
            // 开启状态按钮
            Rectangle {
                width: 80
                height: 36
                radius: 8
                color: parent.checked ? "#10B981" : "#1E293B"
                border.color: parent.checked ? "#34D399" : "#334155"
                border.width: 1
                
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    Text {
                        text: root.trueIcon
                        font.pixelSize: 14
                        color: parent.checked ? "#FFFFFF" : "#94A3B8"
                    }
                    
                    Text {
                        text: root.trueLabel
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: parent.checked ? "#FFFFFF" : "#94A3B8"
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!parent.checked) {
                            parent.checked = true
                            updateValue(true)
                        }
                    }
                }
                
                Behavior on color { ColorAnimation { duration: 100 } }
            }
            
            Item { Layout.fillWidth: true }
        }
    }
    
    // ============ 方法 ============
    
    function updateValue(newValue) {
        if (root.value !== newValue) {
            root.value = newValue
            
            // 更新控件显示
            if (toggleLoader.item) {
                toggleLoader.item.checked = newValue
            }
            
            // 验证
            validate()
            
            // 发出信号
            root.paramValueChanged(root.paramId, newValue)
        }
    }
    
    function validate() {
        var validation = { valid: true, message: "" }
        
        // 布尔值通常不需要验证，但可以添加特殊逻辑
        if (root.required && root.value === undefined) {
            validation.valid = false
            validation.message = root.label + " 必须选择"
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
    
    function toggle() {
        updateValue(!root.value)
    }
    
    function reset() {
        var defaultVal = config.default !== undefined ? config.default : false
        updateValue(defaultVal)
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化值
        if (config.default !== undefined) {
            root.value = config.default
        }
    }
    
    onConfigChanged: {
        if (config.default !== undefined) {
            updateValue(config.default)
        }
    }
}

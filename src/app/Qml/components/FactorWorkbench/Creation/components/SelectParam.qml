// SelectParam.qml
// 下拉选择参数组件 - 用于枚举类型参数
// 支持单选下拉列表

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 下拉选择参数组件
 * 
 * 配置项 (config):
 *   - id: 参数唯一标识
 *   - label: 显示标签
 *   - description: 描述文本
 *   - options: 选项数组
 *       - 简单格式: ["选项1", "选项2", "选项3"]
 *       - 对象格式: [{value: "opt1", label: "选项1"}, ...]
 *   - default: 默认值
 *   - required: 是否必填
 *   - placeholder: 占位文本
 *   - searchable: 是否支持搜索（大量选项时）
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
    property var options: normalizeOptions(config.options || [])
    property bool required: config.required || false
    property string placeholder: config.placeholder || "请选择..."
    property bool searchable: config.searchable || false
    
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
        }
        
        // 下拉选择框
        ComboBox {
            id: comboBox
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            
            model: root.options
            textRole: "label"
            valueRole: "value"
            
            currentIndex: {
                for (var i = 0; i < root.options.length; i++) {
                    if (root.options[i].value === root.value) {
                        return i
                    }
                }
                return -1
            }
            
            displayText: currentIndex >= 0 ? root.options[currentIndex].label : root.placeholder
            
            // 自定义背景
            background: Rectangle {
                implicitWidth: 200
                implicitHeight: 40
                radius: 8
                color: comboBox.pressed ? "#334155" : "#1E293B"
                border.color: comboBox.activeFocus ? "#3B82F6" : 
                             comboBox.hovered ? "#475569" : "#334155"
                border.width: 1
                
                Behavior on border.color { ColorAnimation { duration: 100 } }
            }
            
            // 自定义内容
            contentItem: RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 36
                spacing: 8
                
                // 选中项图标（如果有）
                Text {
                    text: comboBox.currentIndex >= 0 && root.options[comboBox.currentIndex].icon 
                          ? root.options[comboBox.currentIndex].icon : ""
                    font.pixelSize: 16
                    visible: text !== ""
                }
                
                // 选中项文本
                Text {
                    Layout.fillWidth: true
                    text: comboBox.displayText
                    font.pixelSize: 14
                    color: comboBox.currentIndex >= 0 ? "#F1F5F9" : "#64748B"
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
            
            // 下拉箭头
            indicator: Item {
                x: comboBox.width - width - 12
                y: comboBox.topPadding + (comboBox.availableHeight - height) / 2
                width: 16
                height: 16
                
                Text {
                    anchors.centerIn: parent
                    text: comboBox.popup.visible ? "▲" : "▼"
                    font.pixelSize: 10
                    color: "#94A3B8"
                }
            }
            
            // 下拉弹出框
            popup: Popup {
                y: comboBox.height + 4
                width: comboBox.width
                implicitHeight: Math.min(contentItem.implicitHeight + 8, 300)
                padding: 4
                
                background: Rectangle {
                    radius: 8
                    color: "#1E293B"
                    border.color: "#334155"
                    border.width: 1
                    
                    layer.enabled: true
                    layer.effect: Item {
                        // 阴影效果占位
                    }
                }
                
                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: comboBox.popup.visible ? comboBox.delegateModel : null
                    currentIndex: comboBox.highlightedIndex
                    
                    ScrollIndicator.vertical: ScrollIndicator {}
                }
            }
            
            // 选项委托
            delegate: ItemDelegate {
                width: comboBox.width - 8
                height: 40
                
                highlighted: comboBox.highlightedIndex === index
                
                background: Rectangle {
                    radius: 6
                    color: parent.highlighted ? "#334155" : 
                           parent.hovered ? "#2D3748" : "transparent"
                    
                    Behavior on color { ColorAnimation { duration: 100 } }
                }
                
                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    
                    // 选项图标
                    Text {
                        text: modelData.icon || ""
                        font.pixelSize: 16
                        visible: text !== ""
                    }
                    
                    // 选项文本
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        
                        Text {
                            text: modelData.label
                            font.pixelSize: 14
                            color: "#F1F5F9"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        
                        // 选项描述
                        Text {
                            text: modelData.description || ""
                            font.pixelSize: 11
                            color: "#64748B"
                            visible: text !== ""
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    
                    // 选中标记
                    Text {
                        text: "✓"
                        font.pixelSize: 14
                        color: "#3B82F6"
                        visible: modelData.value === root.value
                    }
                }
                
                onClicked: {
                    updateValue(modelData.value)
                    comboBox.popup.close()
                }
            }
            
            onActivated: function(index) {
                if (index >= 0 && index < root.options.length) {
                    updateValue(root.options[index].value)
                }
            }
        }
        
        // 选项快捷按钮（选项少于5个时显示）
        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: root.options.length > 0 && root.options.length <= 5 && config.showChips !== false
            
            Repeater {
                model: root.options
                
                Rectangle {
                    width: chipText.implicitWidth + 20
                    height: 28
                    radius: 14
                    color: root.value === modelData.value ? "#3B82F6" : "#1E293B"
                    border.color: root.value === modelData.value ? "#60A5FA" : "#334155"
                    border.width: 1
                    
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4
                        
                        Text {
                            text: modelData.icon || ""
                            font.pixelSize: 12
                            visible: text !== ""
                        }
                        
                        Text {
                            id: chipText
                            text: modelData.label
                            font.pixelSize: 12
                            color: root.value === modelData.value ? "#FFFFFF" : "#94A3B8"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            updateValue(modelData.value)
                        }
                    }
                    
                    Behavior on color { ColorAnimation { duration: 100 } }
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
    
    // ============ 方法 ============
    
    // 缓存上次的原始 options 和标准化结果, 避免每次 configChanged 重建数组导致死循环
    property var _lastRawOptions: undefined
    property var _cachedNormalized: []

    function normalizeOptions(opts) {
        if (!opts || !Array.isArray(opts)) return []
        if (_lastRawOptions !== undefined && _lastRawOptions === opts) return _cachedNormalized

        _lastRawOptions = opts
        _cachedNormalized = opts.map(function(opt) {
            if (typeof opt === "object") {
                return {
                    value: opt.value !== undefined ? opt.value : opt.label,
                    label: opt.label || opt.value || String(opt),
                    icon: opt.icon || "",
                    description: opt.description || ""
                }
            } else {
                return {
                    value: opt,
                    label: String(opt),
                    icon: "",
                    description: ""
                }
            }
        })
        return _cachedNormalized
    }
    
    function updateValue(newValue) {
        if (root.value !== newValue) {
            root.value = newValue
            // 更新ComboBox显示
            for (var i = 0; i < root.options.length; i++) {
                if (root.options[i].value === newValue) {
                    comboBox.currentIndex = i
                    break
                }
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
        
        // 选项验证
        if (root.value && root.options.length > 0) {
            var validValues = root.options.map(function(opt) { return opt.value })
            if (!validValues.includes(root.value)) {
                validation.valid = false
                validation.message = root.label + " 必须是有效选项"
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
        updateValue(newValue)
    }
    
    function getSelectedOption() {
        for (var i = 0; i < root.options.length; i++) {
            if (root.options[i].value === root.value) {
                return root.options[i]
            }
        }
        return null
    }
    
    function reset() {
        var defaultVal = config.default !== undefined ? config.default : 
                        (root.options.length > 0 ? root.options[0].value : "")
        updateValue(defaultVal)
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化值
        if (config.default !== undefined) {
            root.value = config.default
        } else if (root.options.length > 0) {
            root.value = root.options[0].value
        }
        
        // 更新ComboBox显示
        for (var i = 0; i < root.options.length; i++) {
            if (root.options[i].value === root.value) {
                comboBox.currentIndex = i
                break
            }
        }
    }
    
    onConfigChanged: {
        root.options = normalizeOptions(config.options || [])
        
        if (config.default !== undefined) {
            updateValue(config.default)
        }
    }
}

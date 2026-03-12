// MetaForm.qml - 通用表单组件
// 配置驱动，支持多种表单字段类型
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 通用表单组件
 * 完全由配置驱动，不包含任何业务逻辑
 * 支持：文本、数字、布尔、下拉选择、标签、日期等字段类型
 */
Rectangle {
    id: root
    
    // ============ 输入属性 ============
    
    // 字段配置数组
    property var fields: []
    // 表单初始值
    property var values: ({})
    // 表单操作按钮
    property var actions: []
    // 表单标题
    property string title: ""
    // 表单副标题
    property string subtitle: ""
    // 布局方式：vertical/horizontal/grid
    property string layout: "vertical"
    // 是否显示必填标记
    property bool showRequiredMark: true
    
    // ============ 输出信号 ============
    
    // 字段值变化
    signal valueChanged(string fieldName, var newValue)
    // 表单操作触发
    signal actionTriggered(string actionName, var formData)
    // 表单验证结果
    signal validationChanged(bool isValid)
    
    // ============ 内部状态 ============
    
    // 表单数据
    property var formData: ({})
    // 验证状态
    property bool formValid: false
    
    // ============ 视觉样式 ============
    
    radius: 8  // borderRadiusLg
    color: "#FAFAFA"  // bgSecondary
    border.color: "#D9D9D9"  // borderDefault
    border.width: 1
    
    // ============ 主布局 ============
    
    Flickable {
        anchors.fill: parent
        anchors.margins: 16  // spacing4
        contentHeight: contentColumn.height
        clip: true
        
        ColumnLayout {
            id: contentColumn
            width: parent.width
            spacing: 24  // spacing6
            
            // 标题区域
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4  // spacing1
                visible: title !== "" || subtitle !== ""
                
                Text {
                    text: title
                    font.pixelSize: 24  // fontSize2xl
                    font.weight: Font.DemiBold
                    color: "#262626"  // textPrimary
                    visible: title !== ""
                }
                
                Text {
                    text: subtitle
                    font.pixelSize: 12  // fontSizeSm
                    color: "#666666"  // textSecondary
                    visible: subtitle !== ""
                }
            }
            
            // 表单字段
            GridLayout {
                id: formGrid
                Layout.fillWidth: true
                columns: layout === "grid" ? 2 : 1
                columnSpacing: 16  // spacing4
                rowSpacing: 16  // spacing4
                
                Repeater {
                    model: fields.filter(function(f) { return f.visible !== false })
                    
                    Loader {
                        Layout.fillWidth: true
                        
                        sourceComponent: getFieldComponent(modelData.type)
                        
                        property var field: modelData
                        property var currentValue: root.formData[field.name]
                        
                        onCurrentValueChanged: {
                            if (currentValue !== undefined) {
                                root.formData[field.name] = currentValue
                                root.valueChanged(field.name, currentValue)
                                validateForm()
                            }
                        }
                    }
                }
            }
            
            // 操作按钮
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 24
                spacing: 12
                visible: actions.length > 0
                
                Repeater {
                    model: actions
                    
                    delegate: Rectangle {
                        Layout.preferredWidth: actionText.width + 16 * 2
                        Layout.preferredHeight: 36
                        radius: 6
                        color: modelData.type === "primary" ? "#1890FF" : 
                               modelData.type === "danger" ? "#F5222D" + "20" : 
                               "transparent"
                        border.color: modelData.type === "default" ? "#D9D9D9" : "transparent"
                        border.width: 1
                        
                        Text {
                            id: actionText
                            anchors.centerIn: parent
                            text: modelData.label
                            font.pixelSize: 14
                            color: modelData.type === "primary" ? "white" : 
                                   modelData.type === "danger" ? "#F5222D" : 
                                   "#262626"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            
                            onClicked: {
                                root.actionTriggered(modelData.name, root.formData)
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 表单字段组件 ============
    
    // 文本输入字段
    Component {
        id: textFieldComponent
        
        ColumnLayout {
            spacing: 8
            
            // 标签
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                
                Text {
                    text: field.label || field.name
                    font.pixelSize: 14
                    color: "#262626"
                }
                
                Text {
                    text: "*"
                    color: "#F5222D"
                    visible: field.required && showRequiredMark
                }
                
                Item { Layout.fillWidth: true }
            }
            
            // 输入框
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: 6
                color: "#F5F5F5"
                border.color: inputField.activeFocus ? "#1890FF" : 
                          "#D9D9D9"
                border.width: 1
                
                TextInput {
                    id: inputField
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 14
                    color: "#262626"
                    text: currentValue || ""
                    
                    onTextChanged: currentValue = text
                    
                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: field.placeholder || ""
                        font: inputField.font
                        color: "#999999"
                        visible: !inputField.text && !inputField.activeFocus
                    }
                }
            }
            
            // 描述文本
            Text {
                text: field.description || ""
                font.pixelSize: 12
                color: "#666666"
                visible: field.description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 数字输入字段
    Component {
        id: numberFieldComponent
        
        ColumnLayout {
            spacing: 8
            
            // 标签
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                
                Text {
                    text: field.label || field.name
                    font.pixelSize: 14
                    color: "#262626"
                }
                
                Text {
                    text: "*"
                    color: "#F5222D"
                    visible: field.required && showRequiredMark
                }
                
                Item { Layout.fillWidth: true }
            }
            
            // 输入框和单位
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#F5F5F5"
                    border.color: numberInput.activeFocus ? "#1890FF" : 
                              "#D9D9D9"
                    border.width: 1
                    
                    TextInput {
                        id: numberInput
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 14
                        color: "#262626"
                        text: currentValue !== undefined && currentValue !== null ? currentValue : ""
                        validator: DoubleValidator {
                            bottom: field.min !== undefined ? field.min : -Infinity
                            top: field.max !== undefined ? field.max : Infinity
                        }
                        
                        onTextChanged: {
                            var num = parseFloat(text)
                            if (!isNaN(num)) {
                                currentValue = num
                            }
                        }
                        
                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            text: field.placeholder || ""
                            font: numberInput.font
                            color: "#999999"
                            visible: !numberInput.text && !numberInput.activeFocus
                        }
                    }
                }
                
                Text {
                    text: field.unit || ""
                    font.pixelSize: 14
                    color: "#666666"
                    visible: field.unit !== ""
                }
            }
            
            // 滑块（如果有范围限制）
            Slider {
                Layout.fillWidth: true
                from: field.min !== undefined ? field.min : 0
                to: field.max !== undefined ? field.max : 100
                value: currentValue || 0
                stepSize: field.step || 1
                visible: field.min !== undefined && field.max !== undefined
                
                onValueChanged: currentValue = value
            }
            
            // 描述文本
            Text {
                text: field.description || ""
                font.pixelSize: 12
                color: "#666666"
                visible: field.description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 布尔（开关）字段
    Component {
        id: booleanFieldComponent
        
        RowLayout {
            spacing: 12
            
            // 标签
            Text {
                text: field.label || field.name
                font.pixelSize: 14
                color: "#262626"
                Layout.fillWidth: true
            }
            
            // 开关
            Switch {
                checked: currentValue || false
                onCheckedChanged: currentValue = checked
            }
            
            // 必填标记
            Text {
                text: "*"
                color: "#F5222D"
                visible: field.required && showRequiredMark
            }
        }
    }
    
    // 下拉选择字段
    Component {
        id: selectFieldComponent
        
        ColumnLayout {
            spacing: 8
            
            // 标签
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                
                Text {
                    text: field.label || field.name
                    font.pixelSize: 14
                    color: "#262626"
                }
                
                Text {
                    text: "*"
                    color: "#F5222D"
                    visible: field.required && showRequiredMark
                }
                
                Item { Layout.fillWidth: true }
            }
            
            // 下拉框
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: 6
                color: "#F5F5F5"
                border.color: selectComboBox.popup.visible ? "#1890FF" : 
                          "#D9D9D9"
                border.width: 1
                
                ComboBox {
                    id: selectComboBox
                    anchors.fill: parent
                    anchors.margins: 2
                    
                    model: field.options || []
                    textRole: "label"
                    valueRole: "value"
                    
                    currentIndex: {
                        for (var i = 0; i < (field.options || []).length; i++) {
                            if (field.options[i].value === currentValue) {
                                return i
                            }
                        }
                        return -1
                    }
                    
                    onCurrentIndexChanged: {
                        if (currentIndex >= 0 && field.options) {
                            currentValue = field.options[currentIndex].value
                        }
                    }
                    
                    // 自定义样式
                    background: Rectangle {
                        color: "transparent"
                    }
                    
                    contentItem: Text {
                        text: selectComboBox.displayText
                        font.pixelSize: 14
                        color: "#262626"
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 12
                    }
                    
                    indicator: Text {
                        text: "▼"
                        font.pixelSize: 12
                        color: "#999999"
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
            
            // 描述文本
            Text {
                text: field.description || ""
                font.pixelSize: 12
                color: "#666666"
                visible: field.description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 标签输入字段
    Component {
        id: tagsFieldComponent
        
        ColumnLayout {
            spacing: 8
            
            // 标签
            Text {
                text: field.label || field.name
                font.pixelSize: 14
                color: "#262626"
            }
            
            // 标签输入框
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#F5F5F5"
                    border.color: tagInput.activeFocus ? "#1890FF" : 
                              "#D9D9D9"
                    border.width: 1
                    
                    TextInput {
                        id: tagInput
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 14
                        color: "#262626"
                        placeholderText: field.placeholder || "输入标签后按回车"
                        
                        onAccepted: {
                            if (text.trim() !== "") {
                                var tags = currentValue || []
                                tags.push(text.trim())
                                currentValue = tags
                                text = ""
                            }
                        }
                    }
                }
                
                Rectangle {
                    width: 40
                    height: 40
                    radius: 6
                    color: "#1890FF"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        font.pixelSize: 16
                        color: "white"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        
                        onClicked: {
                            if (tagInput.text.trim() !== "") {
                                var tags = currentValue || []
                                tags.push(tagInput.text.trim())
                                currentValue = tags
                                tagInput.text = ""
                            }
                        }
                    }
                }
            }
            
            // 已选标签
            Flow {
                Layout.fillWidth: true
                spacing: 8
                
                Repeater {
                    model: currentValue || []
                    
                    delegate: Rectangle {
                        height: 28
                        width: tagText.width + 24
                        radius: 14
                        color: "#F0F0F0"
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 4
                            spacing: 4
                            
                            Text {
                                id: tagText
                                text: modelData
                                font.pixelSize: 12
                                color: "#666666"
                            }
                            
                            Text {
                                text: "×"
                                font.pixelSize: 14
                                color: "#999999"
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    
                                    onClicked: {
                                        var tags = currentValue || []
                                        var index = tags.indexOf(modelData)
                                        if (index >= 0) {
                                            tags.splice(index, 1)
                                            currentValue = tags
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 描述文本
            Text {
                text: field.description || ""
                font.pixelSize: 12
                color: "#666666"
                visible: field.description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 日期选择字段
    Component {
        id: dateFieldComponent
        
        ColumnLayout {
            spacing: 8
            
            // 标签
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                
                Text {
                    text: field.label || field.name
                    font.pixelSize: 14
                    color: "#262626"
                }
                
                Text {
                    text: "*"
                    color: "#F5222D"
                    visible: field.required && showRequiredMark
                }
                
                Item { Layout.fillWidth: true }
            }
            
            // 日期输入框
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: 6
                color: "#F5F5F5"
                border.color: dateInput.activeFocus ? "#1890FF" : 
                          "#D9D9D9"
                border.width: 1
                
                TextInput {
                    id: dateInput
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 14
                    color: "#262626"
                    text: currentValue || ""
                    
                    onTextChanged: currentValue = text
                    
                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: field.placeholder || "YYYY-MM-DD"
                        font: dateInput.font
                        color: "#999999"
                        visible: !dateInput.text && !dateInput.activeFocus
                    }
                }
            }
            
            // 描述文本
            Text {
                text: field.description || ""
                font.pixelSize: 12
                color: "#666666"
                visible: field.description !== ""
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 获取字段组件
    function getFieldComponent(type) {
        switch(type) {
            case "text": return textFieldComponent
            case "number": return numberFieldComponent
            case "boolean": return booleanFieldComponent
            case "select": return selectFieldComponent
            case "tags": return tagsFieldComponent
            case "date": return dateFieldComponent
            default: return textFieldComponent
        }
    }
    
    // ============ 表单验证 ============
    
    function validateForm() {
        var valid = true
        
        for (var i = 0; i < fields.length; i++) {
            var field = fields[i]
            if (field.required) {
                var value = formData[field.name]
                if (value === undefined || value === null || value === "" || 
                    (Array.isArray(value) && value.length === 0)) {
                    valid = false
                    break
                }
            }
        }
        
        formValid = valid
        validationChanged(valid)
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 合并初始值
        for (var i = 0; i < fields.length; i++) {
            var field = fields[i]
            var defaultValue = values[field.name] !== undefined ? 
                              values[field.name] : field.defaultValue
            formData[field.name] = defaultValue
        }
        
        // 初始验证
        validateForm()
    }
}
// FactorParameterInput.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

/**
 * 因子参数输入组件
 * 支持不同类型参数（整数、浮点数、枚举、布尔）的输入
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    property string paramName: ""
    property string displayName: ""
    property string paramType: "integer"  // integer, float, enum, multiselect, boolean, string
    property string description: ""
    property var commonValues: []
    property var defaultValue: null
    property var minValue: null
    property var maxValue: null
    property var stepValue: null
    
    property var currentValue: defaultValue
    
    // 信号
    signal valueChanged(var newValue)
    
    // ============ 视觉属性 ============
    
    implicitWidth: 300
    implicitHeight: paramContent.height + 16  // spacing4
    color: "transparent"
    
    // ============ 主布局 ============
    
    ColumnLayout {
        id: paramContent
        anchors.fill: parent
        spacing: 8  // spacing2
        
        // 参数标题和描述
        ColumnLayout {
            spacing: 4  // spacing1
            
            // 显示名称
            Text {
                text: displayName
                font.pixelSize: 14  // fontSizeMd
                font.weight: Font.Medium
                color: "#F1F5F9"  // textPrimary
            }
            
            // 描述
            Text {
                text: description
                font.pixelSize: 12  // fontSizeSm
                color: "#94A3B8"  // textSecondary
                wrapMode: Text.WordWrap
                visible: description !== ""
            }
        }
        
        // 参数输入控件
        Loader {
            id: inputLoader
            Layout.fillWidth: true
            
            // 根据参数类型加载不同的控件
            sourceComponent: {
                switch (paramType) {
                    case "integer":
                    case "float":
                        return numericInputComponent
                    case "enum":
                        return enumInputComponent
                    case "multiselect":
                        return multiselectInputComponent
                    case "boolean":
                        return booleanInputComponent
                    case "string":
                        return stringInputComponent
                    default:
                        return numericInputComponent
                }
            }
            
                    // 数值输入组件（整数/浮点数）
                    Component {
                        id: numericInputComponent
                        
                        RowLayout {
                            spacing: 12  // spacing3
                            
                            // 滑块
                            Slider {
                                id: numericSlider
                                Layout.fillWidth: true
                                from: minValue !== null ? minValue : 0
                                to: maxValue !== null ? maxValue : 100
                                value: currentValue !== null ? currentValue : defaultValue
                                stepSize: stepValue !== null ? stepValue : 1
                                enabled: minValue !== null && maxValue !== null
                                
                                onValueChanged: {
                                    root.currentValue = value
                                    root.valueChanged(value)
                                }
                            }
                            
                            // 数值显示和输入
                            Rectangle {
                                Layout.preferredWidth: 100
                                Layout.preferredHeight: 40
                                radius: 8  // borderRadiusMd
                                color: "#334155"  // bgTertiary
                                border.color: "#475569"  // borderDefault
                                border.width: 1
                                
                                TextInput {
                                    id: numericInput
                                    anchors.fill: parent
                                    anchors.margins: 8  // spacing2
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: 14  // fontSizeMd
                                    color: "#F1F5F9"  // textPrimary
                                    text: root.currentValue !== null ? root.currentValue : ""
                                    
                                    onTextChanged: {
                                        if (text !== "") {
                                            var num = paramType === "integer" ? parseInt(text) : parseFloat(text)
                                            if (!isNaN(num)) {
                                                root.currentValue = num
                                                root.valueChanged(num)
                                                
                                                // 更新滑块位置
                                                if (minValue !== null && maxValue !== null) {
                                                    numericSlider.value = Math.max(minValue, Math.min(maxValue, num))
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            
                            // 单位/说明
                            Text {
                                text: paramType === "integer" ? "整数" : "小数"
                                font.pixelSize: 12  // fontSizeSm
                                color: "#94A3B8"  // textSecondary
                                visible: numericSlider.enabled
                            }
                        }
                    }
            
            // 枚举输入组件
            Component {
                id: enumInputComponent
                
                ColumnLayout {
                    spacing: 8  // spacing2
                    
                    // 单选按钮组
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8  // spacing2
                        
                        Repeater {
                            model: commonValues && commonValues.length > 0 ? commonValues : []
                            
                            delegate: Rectangle {
                                width: optionText.contentWidth + 16  // spacing4
                                height: 32
                                radius: 8  // borderRadiusMd
                                color: root.currentValue === modelData ? "#3B82F6"  // factorMomentum
                                                                       : "#334155"  // bgTertiary
                                border.color: "#475569"  // borderDefault
                                border.width: 1
                                
                                Text {
                                    id: optionText
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.pixelSize: 12  // fontSizeSm
                                    color: root.currentValue === modelData ? "white" 
                                                                           : "#F1F5F9"  // textPrimary
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.currentValue = modelData
                                        root.valueChanged(modelData)
                                    }
                                }
                            }
                        }
                    }
                    
                    // 下拉选择（备选）
                    ComboBox {
                        Layout.fillWidth: true
                        model: commonValues && commonValues.length > 0 ? commonValues : ["无选项"]
                        currentIndex: commonValues && commonValues.indexOf(root.currentValue) >= 0 ? 
                                      commonValues.indexOf(root.currentValue) : 0
                        
                        onCurrentIndexChanged: {
                            if (commonValues && commonValues.length > 0) {
                                root.currentValue = commonValues[currentIndex]
                                root.valueChanged(commonValues[currentIndex])
                            }
                        }
                    }
                }
            }

            // 多选输入组件
            Component {
                id: multiselectInputComponent

                ColumnLayout {
                    spacing: 8

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: normalizedCommonValues()

                            delegate: Rectangle {
                                property var optionValue: modelData.value
                                property string optionLabel: modelData.label
                                property bool selected: Array.isArray(root.currentValue) && root.currentValue.indexOf(optionValue) >= 0
                                width: optionText.implicitWidth + 28
                                height: 32
                                radius: 8
                                color: selected ? "#3B82F6" : "#334155"
                                border.color: selected ? "#60A5FA" : "#475569"
                                border.width: 1

                                Text {
                                    id: optionText
                                    anchors.centerIn: parent
                                    text: optionLabel
                                    font.pixelSize: 12
                                    color: selected ? "white" : "#F1F5F9"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var nextValues = Array.isArray(root.currentValue) ? root.currentValue.slice() : []
                                        var optionIndex = nextValues.indexOf(optionValue)
                                        if (optionIndex >= 0) {
                                            nextValues.splice(optionIndex, 1)
                                        } else {
                                            nextValues.push(optionValue)
                                        }
                                        root.currentValue = nextValues
                                        root.valueChanged(nextValues)
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        text: Array.isArray(root.currentValue) ? ("已选 " + root.currentValue.length) : "已选 0"
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }
                }
            }
            
            // 布尔输入组件
            Component {
                id: booleanInputComponent
                
                RowLayout {
                    spacing: 16  // spacing4
                    
                    // 开关控件
                    Rectangle {
                        width: 60
                        height: 32
                        radius: 9999  // borderRadiusFull
                        color: root.currentValue ? "#3B82F6"  // factorMomentum
                                                 : "#475569"  // bgQuaternary
                        
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 9999  // borderRadiusFull
                            color: "white"
                            x: root.currentValue ? parent.width - width - 2 : 2
                            y: 2
                            
                            Behavior on x {
                                NumberAnimation { duration: 200 }
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.currentValue = !root.currentValue
                                root.valueChanged(root.currentValue)
                            }
                        }
                    }
                    
                    // 标签
                    Text {
                        text: root.currentValue ? "是" : "否"
                        font.pixelSize: 14  // fontSizeMd
                        color: "#F1F5F9"  // textPrimary
                    }
                    
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
            
            // 字符串输入组件
            Component {
                id: stringInputComponent
                
                Rectangle {
                    height: 40
                    radius: 8  // borderRadiusMd
                    color: "#334155"  // bgTertiary
                    border.color: "#475569"  // borderDefault
                    border.width: 1
                    
                    TextInput {
                        anchors.fill: parent
                        anchors.margins: 12  // spacing3
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 14  // fontSizeMd
                        color: "#F1F5F9"  // textPrimary
                        text: root.currentValue !== null ? root.currentValue : ""
                        
                        onTextChanged: {
                            root.currentValue = text
                            root.valueChanged(text)
                        }
                    }
                }
            }
        }
        
        // 常见值提示
        Row {
            spacing: 8  // spacing2
            visible: commonValues && commonValues.length > 0
            
            Text {
                text: "常见值:"
                font.pixelSize: 10  // fontSizeXs
                color: "#64748B"  // textTertiary
            }
            
            Repeater {
                model: commonValues && commonValues.length > 0 ? commonValues : []
                
                delegate: Text {
                    text: modelData
                    font.pixelSize: 10  // fontSizeXs
                    color: "#3B82F6"  // factorMomentum
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.currentValue = modelData
                            root.valueChanged(modelData)
                        }
                    }
                }
            }
        }
        
        // 值范围提示
        Text {
            text: {
                if (minValue !== null && maxValue !== null) {
                    return "范围: " + minValue + " - " + maxValue
                } else if (minValue !== null) {
                    return "最小值: " + minValue
                } else if (maxValue !== null) {
                    return "最大值: " + maxValue
                }
                return ""
            }
            font.pixelSize: 10  // fontSizeXs
            color: "#64748B"  // textTertiary
            visible: text !== ""
        }
    }

    function normalizedCommonValues() {
        if (!commonValues || !Array.isArray(commonValues)) {
            return []
        }

        return commonValues.map(function(option) {
            if (option && typeof option === "object") {
                return {
                    value: option.value !== undefined ? option.value : option.label,
                    label: option.label !== undefined ? option.label : String(option.value !== undefined ? option.value : "")
                }
            }

            return {
                value: option,
                label: String(option)
            }
        })
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        if (currentValue === undefined && defaultValue !== null) {
            currentValue = defaultValue
        }
    }
}
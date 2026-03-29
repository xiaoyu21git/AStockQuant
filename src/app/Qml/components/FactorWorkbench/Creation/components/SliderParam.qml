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
    
    // 内部整数表示属性（避免浮点数精度问题）
    property int _intValue: 0
    property int _intMinValue: 0
    property int _intMaxValue: 100
    property int _intStepValue: 1
    property real _resolvedMin: 0
    property real _resolvedMax: 100
    property real _resolvedStep: 1
    property bool _suppressUpdates: false
    
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
    
    // 将浮点数转换为整数表示
    function floatToInt(floatVal) {
        var normalizedValue = normalizeNumericValue(floatVal, 0)
        if (root.decimals > 0) {
            var multiplier = Math.pow(10, root.decimals)
            return Math.round(normalizedValue * multiplier)
        }
        return Math.round(normalizedValue)
    }
    
    // 将整数转换为浮点数
    function intToFloat(intVal) {
        if (root.decimals > 0) {
            var divisor = Math.pow(10, root.decimals)
            return intVal / divisor
        }
        return intVal
    }
    
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
                text: formatValue(root._resolvedMin)
                font.pixelSize: 11
                color: "#64748B"
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
            
            // 滑块
            Slider {
                id: slider
                Layout.fillWidth: true
                from: root._resolvedMin
                to: root._resolvedMax
                stepSize: root._resolvedStep
                value: root.value
                
                // 添加自定义属性，跟踪实际整数值
                property int intValue: root._intValue
                property int intMinValue: root._intMinValue
                property int intMaxValue: root._intMaxValue
                property int intStepValue: root._intStepValue
                
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
                    if (root._suppressUpdates) {
                        return
                    }
                    // 修复：移除条件判断，始终更新值
                    // 原条件 activeFocus || pressed 在某些情况下不满足，导致滑块无法更新
                    updateValue(value)
                }
                
                // 添加移动状态跟踪，确保拖动结束时值被处理
                property bool moving: false
                
                onPressedChanged: {
                    moving = pressed
                    if (!pressed && !root._suppressUpdates) {
                        // 拖动结束时确保处理最后的值
                        updateValue(value)
                    }
                }
            }
            
            // 最大值标签
            Text {
                text: formatValue(root._resolvedMax)
                font.pixelSize: 11
                color: "#64748B"
                Layout.preferredWidth: 40
            }
            
            // 数值输入框
            SpinBox {
                id: spinBox
                Layout.preferredWidth: 100
                from: root._intMinValue
                to: root._intMaxValue
                stepSize: root._intStepValue
                value: root._intValue
                editable: true
                
                property real realValue: value / Math.pow(10, root.decimals)
                
                textFromValue: function(value, locale) {
                    // 使用root的formatValue函数确保一致的精度处理
                    var displayValue = value / Math.pow(10, root.decimals)
                    return root.formatValue(displayValue)
                }
                
                valueFromText: function(text, locale) {
                    // 解析文本并转换为整数表示（避免浮点数精度问题）
                    var floatValue = parseFloat(text)
                    if (isNaN(floatValue)) return value
                    
                    // 转换为整数表示
                    if (root.decimals > 0) {
                        var multiplier = Math.pow(10, root.decimals)
                        var intValue = Math.round(floatValue * multiplier)
                        return intValue
                    } else {
                        return floatValue
                    }
                }
                
                onValueModified: {
                    if (root._suppressUpdates) {
                        return
                    }
                    // 确保使用正确的精度处理
                    var actualValue = value / Math.pow(10, root.decimals)
                    updateValue(actualValue)
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
                        bottom: root._resolvedMin
                        top: root._resolvedMax
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
                    readonly property real presetValue: normalizeNumericValue(modelData)
                    width: presetText.implicitWidth + 16
                    height: 24
                    radius: 12
                    color: Math.abs(root.value - presetValue) < 0.000000001 ? "#3B82F6" : "#1E293B"
                    border.color: Math.abs(root.value - presetValue) < 0.000000001 ? "#60A5FA" : "#334155"
                    border.width: 1
                    
                    Text {
                        id: presetText
                        anchors.centerIn: parent
                        text: formatValue(parent.presetValue) + (root.unit ? root.unit : "")
                        font.pixelSize: 11
                        color: Math.abs(root.value - parent.presetValue) < 0.000000001 ? "#FFFFFF" : "#94A3B8"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            applyResolvedValue(parent.presetValue, true)
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
            // 使用更精确的舍入避免浮点数精度问题
            var multiplier = Math.pow(10, root.decimals)
            var rounded = Math.round(val * multiplier) / multiplier
            return rounded.toFixed(root.decimals)
        }
        return Math.round(val).toString()
    }

    function normalizeNumericValue(rawValue, fallbackValue) {
        var numericValue = Number(rawValue)
        if (isNaN(numericValue)) {
            return fallbackValue !== undefined ? fallbackValue : 0
        }
        return numericValue
    }

    function buildResolvedState(preferredValue) {
        var resolvedMin = normalizeNumericValue(root.minValue, 0)
        var resolvedMax = normalizeNumericValue(root.maxValue, resolvedMin)
        var resolvedStep = normalizeNumericValue(root.stepValue, 1)

        if (resolvedMax < resolvedMin) {
            resolvedMax = resolvedMin
        }
        if (resolvedStep <= 0) {
            resolvedStep = 1
        }

        var resolvedMinInt = root.floatToInt(resolvedMin)
        var resolvedMaxInt = root.floatToInt(resolvedMax)
        var resolvedStepInt = Math.max(1, root.floatToInt(resolvedStep))

        if (resolvedMaxInt < resolvedMinInt) {
            resolvedMaxInt = resolvedMinInt
        }

        var resolvedValue = preferredValue
        if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
            resolvedValue = config.default !== undefined ? config.default : resolvedMin
        }
        resolvedValue = normalizeNumericValue(resolvedValue, resolvedMin)

        var resolvedIntValue = root.floatToInt(resolvedValue)
        resolvedIntValue = Math.max(resolvedMinInt, Math.min(resolvedMaxInt, resolvedIntValue))

        if (resolvedStepInt > 1 && resolvedMaxInt > resolvedMinInt) {
            var snappedSteps = Math.round((resolvedIntValue - resolvedMinInt) / resolvedStepInt)
            resolvedIntValue = resolvedMinInt + snappedSteps * resolvedStepInt
            resolvedIntValue = Math.max(resolvedMinInt, Math.min(resolvedMaxInt, resolvedIntValue))
        }

        return {
            min: root.intToFloat(resolvedMinInt),
            max: root.intToFloat(resolvedMaxInt),
            step: root.intToFloat(resolvedStepInt),
            minInt: resolvedMinInt,
            maxInt: resolvedMaxInt,
            stepInt: resolvedStepInt,
            valueInt: resolvedIntValue,
            valueFloat: root.intToFloat(resolvedIntValue)
        }
    }

    function applyState(state, emitChange) {
        root._resolvedMin = state.min
        root._resolvedMax = state.max
        root._resolvedStep = state.step
        root._intMinValue = state.minInt
        root._intMaxValue = state.maxInt
        root._intStepValue = state.stepInt

        root._suppressUpdates = true
        root._intValue = state.valueInt
        root.value = state.valueFloat
        slider.value = state.valueFloat
        spinBox.value = state.valueInt
        root._suppressUpdates = false

        validate()

        if (emitChange === true) {
            root.paramValueChanged(root.paramId, state.valueFloat)
        }
    }

    function applyResolvedValue(preferredValue, emitChange) {
        applyState(buildResolvedState(preferredValue), emitChange)
    }
    
    function updateValue(newValue) {
        if (root._suppressUpdates) {
            return
        }

        var resolvedState = buildResolvedState(newValue)
        var valueChanged = root._intValue !== resolvedState.valueInt
            || Math.abs(root.value - resolvedState.valueFloat) > 0.000000001
            || root._intMinValue !== resolvedState.minInt
            || root._intMaxValue !== resolvedState.maxInt
            || root._intStepValue !== resolvedState.stepInt
        
        if (valueChanged) {
            applyState(resolvedState, false)
            
            // 记录调试信息（显示正确处理后的值）
            console.log("滑块值更新:", root.paramId, 
                       "整数值:", resolvedState.valueInt,
                       "浮点值:", resolvedState.valueFloat,
                       "显示值:", root.formatValue(resolvedState.valueFloat))
            
            // 发出信号，传递格式化的值
            root.paramValueChanged(root.paramId, resolvedState.valueFloat)
        } else {
            validate()
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
        if (root.value < root._resolvedMin) {
            validation.valid = false
            validation.message = root.label + " 不能小于 " + root._resolvedMin
        }
        if (root.value > root._resolvedMax) {
            validation.valid = false
            validation.message = root.label + " 不能大于 " + root._resolvedMax
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
        var defaultVal = config.default !== undefined ? config.default : root._resolvedMin
        updateValue(defaultVal)
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        applyResolvedValue(config.default, false)
        
        console.log("滑块组件初始化完成:", root.paramId, 
                   "浮点值:", root.value, 
                   "整数值:", root._intValue,
                   "小数位数:", root.decimals)
    }
    
    onConfigChanged: {
        applyResolvedValue(config.default, false)
    }

    onValueChanged: {
        if (root._suppressUpdates) {
            return
        }

        var resolvedState = buildResolvedState(root.value)
        if (root._intValue !== resolvedState.valueInt || Math.abs(root.value - resolvedState.valueFloat) > 0.000000001) {
            applyState(resolvedState, false)
        }
    }
    
    // 监听内部整数值变化，同步到外部值
    on_IntValueChanged: {
        if (root._suppressUpdates) {
            return
        }
        if (root.decimals > 0) {
            var floatVal = root.intToFloat(root._intValue)
            if (Math.abs(root.value - floatVal) > 0.000000001) {
                root._suppressUpdates = true
                root.value = floatVal
                slider.value = floatVal
                spinBox.value = root._intValue
                root._suppressUpdates = false
                
                // 验证并发出信号
                validate()
                root.paramValueChanged(root.paramId, floatVal)
            }
        }
    }
}

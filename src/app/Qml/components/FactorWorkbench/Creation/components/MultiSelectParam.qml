// MultiSelectParam.qml
// 多选参数组件 - 用于多选枚举型参数

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var config: ({})
    property var value: []
    property bool isValid: true
    property string errorMessage: ""

    property string paramId: config.id || ""
    property string label: config.label || config.displayName || paramId
    property string description: config.description || ""
    property var options: normalizeOptions(config.options || [])
    property bool required: config.required || false

    signal paramValueChanged(string id, var newValue)
    signal validationChanged(string id, bool valid, string message)

    implicitWidth: parent ? parent.width : 400
    implicitHeight: contentLayout.implicitHeight + 16
    radius: 8
    color: mouseArea.containsMouse ? "#1E293B" : "transparent"
    border.color: !isValid ? "#EF4444" : mouseArea.containsMouse ? "#334155" : "transparent"
    border.width: 1

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

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: root.label
                font.pixelSize: 14
                font.weight: Font.Medium
                color: "#F1F5F9"
            }

            Text {
                text: "*"
                font.pixelSize: 14
                color: "#EF4444"
                visible: root.required
            }

            Item { Layout.fillWidth: true }

            Text {
                text: Array.isArray(root.value) ? ("已选 " + root.value.length) : "已选 0"
                font.pixelSize: 12
                color: "#94A3B8"
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: root.options

                Rectangle {
                    property bool selected: containsValue(modelData.value)
                    width: optionText.implicitWidth + 28
                    height: 30
                    radius: 15
                    color: selected ? "#2563EB" : "#1E293B"
                    border.color: selected ? "#60A5FA" : "#334155"
                    border.width: 1

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 5

                        Text {
                            text: selected ? "✓" : "+"
                            font.pixelSize: 11
                            color: selected ? "#FFFFFF" : "#94A3B8"
                        }

                        Text {
                            id: optionText
                            text: modelData.label
                            font.pixelSize: 12
                            color: selected ? "#FFFFFF" : "#CBD5E1"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: toggleOption(modelData.value)
                    }

                    Behavior on color { ColorAnimation { duration: 100 } }
                }
            }
        }

        Text {
            text: root.description
            font.pixelSize: 12
            color: "#64748B"
            visible: root.description !== ""
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Text {
            text: root.errorMessage
            font.pixelSize: 12
            color: "#EF4444"
            visible: !root.isValid && root.errorMessage !== ""
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    function normalizeOptions(opts) {
        if (!opts || !Array.isArray(opts)) return []

        return opts.map(function(opt) {
            if (typeof opt === "object") {
                return {
                    value: opt.value !== undefined ? opt.value : opt.label,
                    label: opt.label || opt.value || String(opt)
                }
            }
            return {
                value: opt,
                label: String(opt)
            }
        })
    }

    function normalizeSelectedValues(sourceValue) {
        var values = normalizedValueList(sourceValue)
        if (values.length === 0 || root.options.length === 0) {
            return values
        }

        var normalizedValues = []
        for (var i = 0; i < values.length; ++i) {
            var rawValue = values[i]
            var canonicalValue = resolveOptionValue(rawValue)
            if (canonicalValue !== "" && isKnownOptionValue(canonicalValue) && normalizedValues.indexOf(canonicalValue) < 0) {
                normalizedValues.push(canonicalValue)
            }
        }
        return normalizedValues
    }

    function resolveOptionValue(rawValue) {
        var candidate = rawValue
        if (candidate && typeof candidate === "object") {
            if (candidate.value !== undefined) {
                candidate = candidate.value
            } else if (candidate.label !== undefined) {
                candidate = candidate.label
            }
        }

        var stringCandidate = String(candidate === undefined || candidate === null ? "" : candidate).trim()
        if (!stringCandidate) {
            return ""
        }

        for (var i = 0; i < root.options.length; ++i) {
            var option = root.options[i]
            if (!option) {
                continue
            }
            if (option.value === stringCandidate || option.label === stringCandidate) {
                return option.value
            }
        }

        return stringCandidate
    }

    function isKnownOptionValue(optionValue) {
        for (var i = 0; i < root.options.length; ++i) {
            var option = root.options[i]
            if (!option) {
                continue
            }
            if (option.value === optionValue) {
                return true
            }
        }
        return false
    }

    function normalizedValueList(sourceValue) {
        if (Array.isArray(sourceValue)) {
            return sourceValue.slice()
        }
        if (sourceValue === undefined || sourceValue === null || sourceValue === "") {
            return []
        }
        return [sourceValue]
    }

    function containsValue(optionValue) {
        return normalizeSelectedValues(root.value).indexOf(optionValue) >= 0
    }

    function toggleOption(optionValue) {
        var values = normalizeSelectedValues(root.value)
        var optionIndex = values.indexOf(optionValue)

        if (optionIndex >= 0) {
            values.splice(optionIndex, 1)
        } else {
            values.push(optionValue)
        }

        updateValue(values)
    }

    function updateValue(newValue) {
        root.value = normalizeSelectedValues(newValue)
        validate()
        root.paramValueChanged(root.paramId, root.value)
    }

    function validate() {
        var values = normalizeSelectedValues(root.value)
        var validation = { valid: true, message: "" }

        if (root.required && values.length === 0) {
            validation.valid = false
            validation.message = root.label + " 至少选择一个选项"
        }

        if (values.length > 0 && root.options.length > 0) {
            var validValues = root.options.map(function(opt) { return opt.value })
            for (var i = 0; i < values.length; ++i) {
                if (validValues.indexOf(values[i]) < 0) {
                    validation.valid = false
                    validation.message = root.label + " 包含无效选项"
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
        return normalizeSelectedValues(root.value)
    }

    function setValue(newValue) {
        root.value = normalizeSelectedValues(newValue)
        validate()
    }

    function reset() {
        setValue(config.default !== undefined ? config.default : [])
    }

    Component.onCompleted: {
        setValue(root.value && normalizedValueList(root.value).length > 0
            ? root.value
            : (config.default !== undefined ? config.default : []))
    }

    onConfigChanged: {
        root.options = normalizeOptions(config.options || [])
        if (root.value !== undefined && root.value !== null && normalizedValueList(root.value).length > 0) {
            setValue(root.value)
        } else if (config.default !== undefined) {
            setValue(config.default)
        }
    }

    onOptionsChanged: {
        if (root.value !== undefined && root.value !== null && normalizedValueList(root.value).length > 0) {
            setValue(root.value)
        }
    }
}
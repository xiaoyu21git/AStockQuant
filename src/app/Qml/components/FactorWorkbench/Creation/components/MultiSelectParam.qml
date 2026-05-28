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
    property var options: []
    property bool required: config.required || false
    property bool queryListMode: String(config.presentation || "").toLowerCase() === "query_list"
    property bool searchable: config.searchable !== false
    property string placeholderText: config.placeholder || (queryListMode ? "点击查询并选择" : "")
    property string popupTitle: config.queryTitle || label
    property bool optionsLoaded: false
    property bool optionsLoading: false
    property string queryText: ""
    readonly property var filteredOptions: buildFilteredOptions(queryText, options)

    signal paramValueChanged(string id, var newValue)
    signal validationChanged(string id, bool valid, string message)

    Component.onCompleted: syncOptionsFromConfig()
    onConfigChanged: syncOptionsFromConfig()

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
            visible: !root.queryListMode

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

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.queryListMode

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: 8
                color: "#0F172A"
                border.color: popupTriggerArea.containsMouse ? "#3B82F6" : "#334155"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: root.selectedSummaryText()
                        font.pixelSize: 12
                        color: Array.isArray(root.value) && root.value.length > 0 ? "#E2E8F0" : "#94A3B8"
                        elide: Text.ElideRight
                    }

                    Text {
                        text: root.optionsLoading ? "加载中" : "查询"
                        font.pixelSize: 12
                        color: "#60A5FA"
                    }
                }

                MouseArea {
                    id: popupTriggerArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.ensureOptionsLoaded()
                        optionsPopup.open()
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 8
                visible: root.normalizeSelectedValues(root.value).length > 0

                Repeater {
                    model: root.normalizeSelectedValues(root.value)

                    Rectangle {
                        radius: 14
                        height: 28
                        width: selectedOptionLabel.implicitWidth + 34
                        color: "#1D4ED8"
                        border.width: 1
                        border.color: "#60A5FA"

                        Text {
                            id: selectedOptionLabel
                            anchors.centerIn: parent
                            text: root.labelForValue(modelData)
                            font.pixelSize: 12
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleOption(modelData)
                        }
                    }
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

    Popup {
        id: optionsPopup
        modal: true
        focus: true
        width: Math.min(460, root.width > 0 ? root.width : 460)
        height: 420
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        parent: Overlay.overlay

        onOpened: {
            root.ensureOptionsLoaded()
            root.queryText = ""
            if (root.searchable) {
                searchField.forceActiveFocus()
            }
        }

        background: Rectangle {
            radius: 12
            color: "#0F172A"
            border.width: 1
            border.color: "#334155"
        }

        contentItem: ColumnLayout {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: root.popupTitle
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F8FAFC"
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "已选 " + root.normalizeSelectedValues(root.value).length
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
            }

            TextField {
                id: searchField
                Layout.fillWidth: true
                visible: root.searchable
                placeholderText: "输入关键字筛选"
                color: "#E2E8F0"
                selectByMouse: true
                text: root.queryText
                onTextChanged: root.queryText = text

                background: Rectangle {
                    radius: 8
                    color: "#111827"
                    border.width: 1
                    border.color: searchField.activeFocus ? "#3B82F6" : "#334155"
                }
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: root.optionsLoading
                visible: running
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: "#111827"
                border.width: 1
                border.color: "#1F2937"

                ListView {
                    id: optionsList
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 4
                    model: root.filteredOptions

                    delegate: Rectangle {
                        width: optionsList.width
                        height: 36
                        radius: 8
                        color: optionMouseArea.containsMouse ? "#1E293B" : "transparent"
                        border.width: selected ? 1 : 0
                        border.color: "#3B82F6"
                        property bool selected: root.containsValue(modelData.value)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10

                            Rectangle {
                                width: 18
                                height: 18
                                radius: 4
                                color: parent.parent.selected ? "#2563EB" : "transparent"
                                border.width: 1
                                border.color: parent.parent.selected ? "#60A5FA" : "#475569"

                                Text {
                                    anchors.centerIn: parent
                                    text: parent.parent.selected ? "✓" : ""
                                    font.pixelSize: 11
                                    color: "#FFFFFF"
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                font.pixelSize: 12
                                color: "#E2E8F0"
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: optionMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleOption(modelData.value)
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !root.optionsLoading && optionsList.count === 0
                        text: root.optionsLoaded ? "没有匹配项" : "点击上方查询加载选项"
                        font.pixelSize: 12
                        color: "#64748B"
                    }
                }
            }
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

    function syncOptionsFromConfig() {
        var nextOptions = normalizeOptions(config.options || [])
        if (nextOptions.length > 0) {
            root.options = nextOptions
            root.optionsLoaded = true
        } else if (!root.queryListMode) {
            root.options = []
            root.optionsLoaded = false
        }
        validate()
    }

    function ensureOptionsLoaded() {
        if (!root.queryListMode || root.optionsLoading || root.optionsLoaded) {
            return
        }

        var provider = config.optionsProvider
        if (typeof provider !== "function") {
            root.optionsLoaded = true
            return
        }

        root.optionsLoading = true
        try {
            root.options = normalizeOptions(provider() || [])
            root.optionsLoaded = true
            validate()
        } finally {
            root.optionsLoading = false
        }
    }

    function buildFilteredOptions(searchText, opts) {
        var normalizedOptions = Array.isArray(opts) ? opts : []
        var keyword = String(searchText || "").trim().toLowerCase()
        if (!keyword) {
            return normalizedOptions
        }

        return normalizedOptions.filter(function(option) {
            if (!option) {
                return false
            }
            return String(option.label || "").toLowerCase().indexOf(keyword) >= 0
                || String(option.value || "").toLowerCase().indexOf(keyword) >= 0
        })
    }

    function labelForValue(optionValue) {
        for (var i = 0; i < root.options.length; ++i) {
            var option = root.options[i]
            if (option && option.value === optionValue) {
                return option.label
            }
        }
        return String(optionValue)
    }

    function selectedSummaryText() {
        var values = normalizeSelectedValues(root.value)
        if (values.length === 0) {
            return root.placeholderText
        }

        if (root.options.length === 0) {
            return "已选 " + values.length + " 项"
        }

        if (values.length <= 2) {
            return values.map(function(optionValue) {
                return root.labelForValue(optionValue)
            }).join("、")
        }

        return values.slice(0, 2).map(function(optionValue) {
            return root.labelForValue(optionValue)
        }).join("、") + " 等 " + values.length + " 项"
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

        if (candidate === undefined || candidate === null || candidate === "") {
            return ""
        }

        for (var i = 0; i < root.options.length; ++i) {
            var option = root.options[i]
            if (!option) {
                continue
            }
            if (option.value === candidate) {
                return option.value
            }
        }

        return candidate
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

    onOptionsChanged: {
        if (root.value !== undefined && root.value !== null && normalizedValueList(root.value).length > 0) {
            setValue(root.value)
        }
    }
}
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property string label: ""
    property var options: []
    property string textRole: "label"
    property string placeholder: "请选择"
    property real fieldWidth: 180
    property real fieldHeight: 36
    property int popupMaxHeight: 240
    property alias currentIndex: comboBox.currentIndex
    property string currentText: optionTextForIndex(comboBox.currentIndex)
    property bool enabled: true

    signal optionSelected(int index, var option)

    implicitWidth: fieldWidth
    implicitHeight: contentColumn.implicitHeight
    Layout.preferredWidth: fieldWidth

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        spacing: 4

        Text {
            text: root.label
            font.pixelSize: 12
            color: "#94A3B8"
        }

        ComboBox {
            id: comboBox
            Layout.preferredWidth: root.fieldWidth
            Layout.preferredHeight: root.fieldHeight
            model: root.options
            enabled: root.enabled
            displayText: root.optionTextForIndex(currentIndex)

            onCurrentIndexChanged: {
                if (currentIndex < 0 || currentIndex >= root.options.length) {
                    return
                }
                root.optionSelected(currentIndex, root.options[currentIndex])
            }

            background: Rectangle {
                implicitHeight: root.fieldHeight
                radius: 6
                color: "#0F172A"
                border.width: 1
                border.color: "#334155"
                opacity: comboBox.enabled ? 1.0 : 0.5
            }

            contentItem: Text {
                text: comboBox.displayText
                font.pixelSize: 12
                color: comboBox.currentIndex >= 0 ? "#F1F5F9" : "#64748B"
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
                rightPadding: 24
                elide: Text.ElideRight
            }

            indicator: Text {
                x: comboBox.width - width - 10
                y: comboBox.topPadding + (comboBox.availableHeight - height) / 2
                text: comboBox.popup.visible ? "▲" : "▼"
                font.pixelSize: 10
                color: "#94A3B8"
            }

            popup: Popup {
                y: comboBox.height + 4
                width: comboBox.width
                implicitHeight: Math.min(contentItem.implicitHeight + 8, root.popupMaxHeight)
                padding: 4

                contentItem: ListView {
                    implicitHeight: contentHeight
                    model: comboBox.popup.visible ? comboBox.delegateModel : null
                    currentIndex: comboBox.highlightedIndex
                    clip: true
                    ScrollIndicator.vertical: ScrollIndicator {}
                }

                background: Rectangle {
                    color: "#1E293B"
                    border.width: 1
                    border.color: "#334155"
                    radius: 6
                }
            }

            delegate: ItemDelegate {
                width: comboBox.width - 8
                height: 32
                highlighted: comboBox.highlightedIndex === index

                onClicked: {
                    comboBox.currentIndex = index
                    comboBox.popup.close()
                }

                contentItem: Text {
                    text: root.optionText(modelData)
                    color: highlighted ? "#3B82F6" : "#F1F5F9"
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    color: highlighted ? "#0F172A" : "transparent"
                    radius: 4
                }
            }
        }
    }

    function optionText(option) {
        if (option === undefined || option === null) {
            return ""
        }
        if (typeof option === "string") {
            return option
        }
        if (textRole && option[textRole] !== undefined && option[textRole] !== null && option[textRole] !== "") {
            return String(option[textRole])
        }
        if (option.label !== undefined && option.label !== null && option.label !== "") {
            return String(option.label)
        }
        if (option.displayText !== undefined && option.displayText !== null && option.displayText !== "") {
            return String(option.displayText)
        }
        if (option.value !== undefined && option.value !== null) {
            return String(option.value)
        }
        return String(option)
    }

    function optionTextForIndex(index) {
        if (index >= 0 && index < options.length) {
            return optionText(options[index])
        }
        return placeholder
    }
}

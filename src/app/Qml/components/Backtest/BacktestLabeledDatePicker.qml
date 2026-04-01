import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as SharedComponents

Item {
    id: root

    property string label: ""
    property string placeholder: "YYYY-MM-DD"
    property alias selectedDate: datePicker.selectedDate
    property real fieldWidth: 180
    property real fieldHeight: 36

    signal dateChanged(string dateText)

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

        SharedComponents.DatePicker {
            id: datePicker
            Layout.preferredWidth: root.fieldWidth
            Layout.preferredHeight: root.fieldHeight
            placeholder: root.placeholder

            onDateChanged: function(dateText) {
                root.dateChanged(dateText)
            }
        }
    }
}
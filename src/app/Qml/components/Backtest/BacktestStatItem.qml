import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property string label: ""
    property string value: ""
    property color labelColor: "#94A3B8"
    property color valueColor: "#F1F5F9"
    property int itemHeight: 34
    property int labelSize: 9
    property int valueSize: 14

    Layout.fillWidth: true
    Layout.preferredHeight: itemHeight

    ColumnLayout {
        anchors.fill: parent
        spacing: 1

        Text {
            text: root.label
            font.pixelSize: root.labelSize
            color: root.labelColor
        }

        Text {
            text: root.value
            font.pixelSize: root.valueSize
            font.weight: Font.DemiBold
            color: root.valueColor
        }
    }
}

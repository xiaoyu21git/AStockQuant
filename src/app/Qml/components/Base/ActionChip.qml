import QtQuick 2.15

Rectangle {
    id: root

    property string label: ""
    property string tone: "link"
    property bool chipEnabled: true
    property bool useCustomColors: false
    property color customBackgroundColor: "transparent"
    property color customBorderColor: "transparent"
    property color customTextColor: "#60A5FA"

    signal clicked()

    implicitWidth: chipLabel.implicitWidth + 16
    implicitHeight: 24
    width: implicitWidth
    height: implicitHeight
    radius: 12
    color: useCustomColors ? customBackgroundColor : (tone === "muted" ? "#334155" : "transparent")
    border.width: useCustomColors ? (customBorderColor === "transparent" ? 0 : 1) : (tone === "muted" ? 1 : 0)
    border.color: useCustomColors ? customBorderColor : (tone === "muted" ? "#475569" : "transparent")
    opacity: chipEnabled ? 1.0 : 0.7

    Text {
        id: chipLabel
        anchors.centerIn: parent
        text: root.label
        font.pixelSize: 12
        font.weight: Font.Medium
        color: useCustomColors ? customTextColor : (tone === "muted" ? "#CBD5E1" : "#60A5FA")
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.chipEnabled
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
import QtQuick 2.15

Rectangle {
    id: root

    property var widgetConfig: ({})

    color: "transparent"

    Text {
        anchors.fill: parent
        anchors.margins: 8
        text: root.widgetConfig.text || "双击编辑文本..."
        color: "#E2E8F0"
        font.pixelSize: 14
        wrapMode: Text.WordWrap
    }
}

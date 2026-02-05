// components/ButtonIcon.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as Constants

Item {
    id: root
    width: isRound ? 32 : 28
    height: isRound ? 32 : 28

    property string iconChar: ""
    property string tooltip: ""
    property bool isRound: false

    signal clicked()

    Rectangle {
        anchors.fill: parent
        radius: root.isRound ? width / 2 : Constants.borderRadiusMedium
        color: mouseArea.containsMouse
               ? Constants.hoverBg
               : "transparent"
        border.color: Constants.borderColor
    }

    Text {
        anchors.centerIn: parent
        text: root.iconChar
        font.family: Constants.iconFont
        font.pixelSize: 14
        color: Constants.textPrimary
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        onClicked: root.clicked()
    }

    ToolTip.visible: tooltip.length > 0 && mouseArea.containsMouse
    ToolTip.text: tooltip
    ToolTip.delay: 500
}

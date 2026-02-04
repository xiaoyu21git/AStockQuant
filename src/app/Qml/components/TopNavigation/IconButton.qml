// components/IconButton.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as Constants

Rectangle {
    id: iconButton
    width: 36
    height: 36
    radius: Constants.borderRadiusMedium
    color: Constants.tertiaryBg
    border.color: Constants.borderLight
    
    property string icon: ""
    property string tooltip: ""
    
    signal clicked()
    
    Text {
        anchors.centerIn: parent
        text: iconButton.icon
        font.family: fontAwesome.name
        color: Constants.textSecondary
        font.pixelSize: 14
    }
    
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        
        onEntered: {
            parent.color = Constants.borderLight;
            if (tooltip !== "") {
                tooltipTimer.start();
            }
        }
        
        onExited: {
            parent.color = Constants.tertiaryBg;
            tooltipPopup.close();
        }
        
        onClicked: {
            parent.clicked();
        }
    }
    
    // 工具提示
    Timer {
        id: tooltipTimer
        interval: 500
        onTriggered: {
            tooltipPopup.open();
        }
    }
    
    // 工具提示弹窗
    Popup {
        id: tooltipPopup
        width: tooltipText.width + 16
        height: tooltipText.height + 8
        padding: 0
        background: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.8)
            radius: 4
        }
        
        Text {
            id: tooltipText
            anchors.centerIn: parent
            text: iconButton.tooltip
            color: "white"
            font.pixelSize: 12
        }
        
        // 定位到按钮下方
        y: iconButton.height + 5
        x: (iconButton.width - width) / 2
    }
}
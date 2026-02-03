import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: control
    
    property string iconChar: "+"
    property bool isRound: false
    
    implicitWidth: 36
    implicitHeight: 36
    
    text: iconChar
    
    font {
        family: "Segoe UI Emoji, Noto Color Emoji"
        pixelSize: 18
    }
    
    background: Rectangle {
        id: bgRect
        radius: control.isRound ? height/2 : 6
        color: control.hovered ? "#3B82F610" : "#33415580"
        
        Behavior on color { ColorAnimation { duration: 150 } }
    }
    
    contentItem: Text {
        text: control.text
        font: control.font
        color: "#3B82F6"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}

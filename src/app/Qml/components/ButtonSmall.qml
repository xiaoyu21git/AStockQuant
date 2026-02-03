import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: control
    
    property string buttonText: "刷新"
    
    implicitWidth: 70
    implicitHeight: 32
    
    text: buttonText
    
    font {
        family: "Inter, Noto Sans SC"
        pixelSize: 13
        weight: Font.Normal
    }
    
    background: Rectangle {
        id: bgRect
        radius: 4
        color: control.hovered ? "#475569CC" : "#334155B3"
        
        Behavior on color { ColorAnimation { duration: 150 } }
    }
    
    contentItem: Text {
        text: control.text
        font: control.font
        color: "#F8FAFC"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}

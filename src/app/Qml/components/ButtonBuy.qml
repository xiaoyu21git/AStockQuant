import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 

Button {
    id: control
    
    property string buttonText: "买入 AAPL"
    property bool isPulsing: true
    
    implicitWidth: 120
    implicitHeight: 44
    
    text: buttonText
    
    font {
        family: "Inter, Noto Sans SC"
        pixelSize: 14
        weight: Font.Bold
        letterSpacing: 0.5
    }
    
    background: Rectangle {
        id: bgRect
        implicitWidth: 120
        implicitHeight: 44
        radius: 8
        gradient: Gradient {
            GradientStop { position: 0.0; color: control.hovered ? "#60A5FA" : "#10B981" }
            GradientStop { position: 1.0; color: control.hovered ? "#3B82F6" : "#059669" }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: control.hovered ? "#60A5FA30" : "transparent"
            Behavior on color { ColorAnimation { duration: 150 } }
        }
        layer.enabled: true
        layer.effect: DropShadow {
            id: shadowEffect
            transparentBorder: true
            color: "#10B98166"
            radius: control.hovered ? 12 : 8
            samples: 17
            horizontalOffset: 0
            verticalOffset: control.hovered ? 4 : 2
        }
        
        // 脉动动画
        SequentialAnimation {
            running: control.isPulsing && control.enabled
            loops: Animation.Infinite
            
            PropertyAnimation {
                target: shadowEffect
                property: "radius"
                from: 8
                to: 16
                duration: 1000
            }
            
            PropertyAnimation {
                target: shadowEffect
                property: "radius"
                from: 16
                to: 8
                duration: 1000
            }
        }
        Behavior on gradient {
            GradientAnimation { duration: 150 }
        }
    }
    
    contentItem: Text {
        text: control.text
        font: control.font
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    
    // 悬停和点击效果
    onHoveredChanged: {
        if (hovered && enabled) {
            bgRect.scale = 1.03
            shadowEffect.verticalOffset = 4
        } else {
            bgRect.scale = 1.0
            shadowEffect.verticalOffset = 2
        }
    }
    
    onPressedChanged: {
        if (pressed) {
            bgRect.scale = 0.98
            bgRect.layer.effect.verticalOffset = 0
        } else if (hovered) {
            bgRect.scale = 1.02
            bgRect.layer.effect.verticalOffset = 4
        } else {
            bgRect.scale = 1.0
            bgRect.layer.effect.verticalOffset = 2
        }
    }
}

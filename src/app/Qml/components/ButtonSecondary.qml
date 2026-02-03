import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import QtQuick.Controls.Material 2.15
Button {
    id: control

// 关键设置
Material.background: "transparent"  // 将 Material 背景设为透明
Material.elevation: control.hovered ? 4 : 2  // 控制 Material 阴影
Material.accent: "#3B82F6"  // 设置主色调
    property string buttonText: "参数设置"
    
    implicitWidth: 100
    implicitHeight: 36
    
    text: buttonText
    
    font {
        family: "Inter, Noto Sans SC"
        pixelSize: 14
        weight: Font.Medium
    }
    
    background: Rectangle {
        id: bgRect
        implicitWidth: 100
        implicitHeight: 36
        radius: 6 // 圆角
        // color: control.hovered ? "#99f15e" : "#1190e6" // 移除原生按钮色，完全自定义
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: control.hovered ? "#60A5FA" : "transparent"
            }
            GradientStop {
                position: 1.0
                color: control.hovered ? "#3B82F6" : "transparent"
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: control.hovered ? "#60A5FA30" : "transparent"
            Behavior on color { ColorAnimation { duration: 150 } }
        }
        border.color: "#3B82F6"
        border.width: 1
        layer.enabled: true
        layer.effect: DropShadow {
            transparentBorder: true
            color: "#3B82F680"
            radius: control.hovered ? 10 : 6
            horizontalOffset: 0
            verticalOffset: control.hovered ? 3 : 1
            spread: 0.1
        }
    }
    onPressedChanged: {
        if (pressed) {
            bgRect.scale = 0.98
            bgRect.layer.effect.verticalOffset = 0
        } else if (hovered) {
            bgRect.scale = 1.02
            bgRect.layer.effect.verticalOffset = 3
        } else {
            bgRect.scale = 1.0
            bgRect.layer.effect.verticalOffset = 1
        }
    }
    
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? (control.hovered ? "#2563eb" : "#1bc568") : "#94a3b8" // 冷色主色/高亮色/禁用色
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}

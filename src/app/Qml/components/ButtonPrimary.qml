import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import Qt5Compat.GraphicalEffects
Button {
    id: control
    
    // 自定义属性
    property string iconText: ""
    property string buttonText: "执行策略"
    property bool showIcon: iconText !== ""
    
    // 尺寸属性
    implicitWidth: 120
    implicitHeight: 40
    
    // 文本内容
    text: showIcon ? iconText + " " + buttonText : buttonText
    
    // 字体样式
    font {
        family: "Inter, Noto Sans SC"
        pixelSize: 14
        weight: Font.DemiBold
        letterSpacing: 0.5
    }
    
    // 使用 Material 样式，但自定义颜色
    Material.background: "transparent"
    Material.elevation: control.hovered ? 4 : 2
    Material.accent: "#3B82F6"
    
    // 自定义背景
    background: Rectangle {
        id: bgRect
        implicitWidth: 120
        implicitHeight: 40
        radius: 8
        
        // 渐变背景
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: control.hovered ? "#5CA0F7" : "#3B82F6"
            }
            GradientStop {
                position: 1.0
                color: control.hovered ? "#3B82F6" : "#1D4ED8"
            }
        }
        
        // 自定义高光效果（无白色遮罩）
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: control.hovered ? "#3B82F630" : "transparent"
            Behavior on color { ColorAnimation { duration: 150 } }
        }
        
        // 阴影效果
        layer.enabled: true
        layer.effect: DropShadow {
            transparentBorder: true
            color: "#3B82F680"
            radius: control.hovered ? 12 : 8
            horizontalOffset: 0
            verticalOffset: control.hovered ? 4 : 2
            spread: 0.1
        }
    }
    
    // 点击效果
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
    
    // 禁用状态
    onEnabledChanged: {
        if (!enabled) {
            bgRect.opacity = 0.4
            bgRect.layer.effect.opacity = 0.4
        } else {
            bgRect.opacity = 1.0
            bgRect.layer.effect.opacity = 1.0
        }
    }
}
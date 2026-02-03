import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: alert
    
    property string title: "交易执行成功"
    property string message: "买入 AAPL 100股 @ $182.45"
    
    implicitWidth: 400
    implicitHeight: 80
    
    radius: 8
    color: "#065F46E6"
    border.left: 4
    border.color: "#10B981"
    
    // 模糊背景效果（需要 QtGraphicalEffects）
    layer.enabled: true
    layer.effect: OpacityMask {
        maskSource: Rectangle {
            width: alert.width
            height: alert.height
            radius: alert.radius
        }
    }
    
    // 进入动画
    OpacityAnimator {
        id: fadeInAnimator
        target: alert
        from: 0
        to: 1
        duration: 300
    }
    
    Component.onCompleted: fadeInAnimator.start()
    
    Row {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        
        // 图标
        Text {
            id: icon
            text: "✓"
            font.pixelSize: 20
            color: "#A7F3D0"
            verticalAlignment: Text.AlignVCenter
        }
        
        // 内容区域
        Column {
            width: parent.width - icon.width - parent.spacing - closeBtn.width
            spacing: 4
            
            Text {
                text: alert.title
                font {
                    family: "Inter, Noto Sans SC"
                    pixelSize: 14
                    weight: Font.DemiBold
                }
                color: "#A7F3D0"
                elide: Text.ElideRight
                width: parent.width
            }
            
            Text {
                text: alert.message
                font {
                    family: "Inter, Noto Sans SC"
                    pixelSize: 13
                    weight: Font.Normal
                }
                color: "#A7F3D0"
                opacity: 0.9
                elide: Text.ElideRight
                width: parent.width
            }
        }
        
        // 关闭按钮
        Button {
            id: closeBtn
            width: 24
            height: 24
            
            background: Rectangle {
                radius: 4
                color: closeBtn.hovered ? "#FFFFFF10" : "transparent"
            }
            
            contentItem: Text {
                text: "×"
                font.pixelSize: 16
                color: "#A7F3D0"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            
            onClicked: {
                fadeOutAnimator.start()
            }
        }
    }
    
    // 淡出动画
    OpacityAnimator {
        id: fadeOutAnimator
        target: alert
        from: 1
        to: 0
        duration: 300
        
        onFinished: alert.destroy()
    }
}

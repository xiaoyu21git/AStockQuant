// components/CreateStrategyButton.qml
import QtQuick 2.15

Rectangle {
    id: createButton
    implicitWidth: 140
    implicitHeight: 44
    radius: 8  // borderRadiusMedium
    
    // 属性
    property alias text: buttonText.text
    property bool hovered: false
    
    // 信号
    signal clicked()
    
    // 颜色
    readonly property color accentBlue: "#3B82F6"
    readonly property color accentBlueDark: "#1D4ED8"
    
    gradient: Gradient {
        GradientStop { 
            position: 0.0; 
            color: hovered ? accentBlueDark : accentBlue 
        }
        GradientStop { 
            position: 1.0; 
            color: hovered ? "#1E40AF" : accentBlueDark 
        }
    }
    
    Row {
        anchors.centerIn: parent
        spacing: 8
        
        // 图标
        Text {
            text: "\uf067"  // fa-plus
            font.pixelSize: 14
            color: "white"
        }
        
        // 文本
        Text {
            id: buttonText
            text: "新建策略"
            font.pixelSize: 14
            font.weight: Font.Medium
            color: "white"
        }
    }
    
    // 交互区域
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        
        onEntered: hovered = true
        onExited: hovered = false
        onClicked: createButton.clicked()
    }
    
    // 点击效果
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "white"
        opacity: 0
    }
    
    // 点击动画
    SequentialAnimation {
        id: clickAnimation
        PropertyAnimation {
            target: createButton.children[createButton.children.length - 1]
            property: "opacity"
            to: 0.2
            duration: 100
        }
        PropertyAnimation {
            target: createButton.children[createButton.children.length - 1]
            property: "opacity"
            to: 0
            duration: 100
        }
    }
}
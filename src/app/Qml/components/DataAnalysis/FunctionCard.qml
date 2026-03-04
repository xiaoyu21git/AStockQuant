// FunctionCard.qml - 通用功能卡片组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as Theme

Rectangle {
    id: functionCard
    
    // 属性
    property string title: ""
    property string description: ""
    property string subtitle: ""
    property string icon: ""
    property string iconColor: "#2196F3"
    property string buttonText: "操作"
    property string buttonColor: "#2196F3"
    property var onClicked: function() { console.log("点击功能卡片") }
    
    // 尺寸
    width: 280
    height: 120
    radius: 8
    color: Theme.darkCard
    border.color: Theme.darkBorder
    border.width: 1
    
    // 鼠标悬停效果
    property bool hovered: false
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: functionCard.hovered = true
        onExited: functionCard.hovered = false
        onClicked: functionCard.onClicked()
    }
    
    // 悬停效果
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
    }
    
    // 内容
    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        
        // 图标和标题行
        Row {
            spacing: 10
            
            // 图标
            Rectangle {
                width: 40
                height: 40
                radius: 8
                color: iconColor
                
                Text {
                    anchors.centerIn: parent
                    text: icon
                    font.pixelSize: 20
                    color: "white"
                }
            }
            
            // 标题和副标题
            Column {
                spacing: 2
                anchors.verticalCenter: parent.verticalCenter
                
                Text {
                    text: title
                    font.pixelSize: 16
                    font.bold: true
                    color: Theme.darkText
                }
                
                Text {
                    text: subtitle
                    font.pixelSize: 12
                    color: Theme.darkTextSecondary
                }
            }
        }
        
        // 描述
        Text {
            text: description
            font.pixelSize: 11
            color: Theme.darkTextSecondary
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 操作按钮
        Rectangle {
            width: 100
            height: 28
            radius: 6
            color: buttonColor
            
            Text {
                anchors.centerIn: parent
                text: buttonText
                font.pixelSize: 12
                font.bold: true
                color: "white"
            }
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: functionCard.onClicked()
            }
        }
    }
}
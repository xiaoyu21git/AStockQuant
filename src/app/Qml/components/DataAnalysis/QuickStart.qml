// QuickStart.qml - 快速开始区域组件

import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as Theme
Rectangle {
    id: quickStart
    width: parent.width
    height: 180
    color: "transparent"
    
    property alias title: titleText.text
    property alias description: descText.text
    
    signal newProjectClicked()
    signal loadTemplateClicked()
    
    // 渐变背景
    Rectangle {
        anchors.fill: parent
        radius: 10
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2a3b8f" }
            GradientStop { position: 1.0; color: "#1a237e" }
        }
        border.color: Theme.darkBorder
        border.width: 1
        
        // 装饰条纹
        Rectangle {
            width: 300
            height: parent.height
            anchors.right: parent.right
            transform: Rotation { origin.x: 0; origin.y: 0; angle: -15 }
            color: "transparent"
            
            Rectangle {
                width: parent.width
                height: parent.height
                color: "transparent"
                
                Rectangle {
                    width: parent.width
                    height: parent.height
                    color:  Qt.rgba(255, 255, 255, 0.05)
                }
            }
        }
    }
    
    // 内容区域
    Row{
        anchors.fill: parent
        anchors.margins: 30
        spacing: 40
        
        // 文本区域
        Column {
            width: parent.width * 0.6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            
            Text {
                id: titleText
                text: "开始您的量化分析"
                font.pixelSize: 28
                font.bold: true
                color: "white"
            }
            
            Text {
                id: descText
                text: "选择快速开始模板或从零创建新项目。我们提供完整的工作流程引导，帮助您快速上手数据分析的每一步。"
                font.pixelSize: 16
                color:  Qt.rgba(255, 255, 255, 0.9)
                wrapMode: Text.WordWrap
                width: parent.width
            }
        }
        
        // 按钮区域
        Row {
            width: parent.width * 0.35
            anchors.verticalCenter: parent.verticalCenter
            spacing: 15
            
            // 新建项目按钮
            Button {
                id: newProjectBtn
                text: "新建项目"
                width: 150
                height: 48
                
                background: Rectangle {
                    radius: 6
                    color: Theme.accentColor
                    
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: parent.color
                        opacity: parent.parent.pressed ? 0.8 : 1.0
                    }
                }
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 16
                    font.bold: true
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                Image {
                    source: "qrc:/icons/plus.svg"
                    width: 20
                    height: 20
                    anchors.left: parent.left
                    anchors.leftMargin: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                onClicked: quickStart.newProjectClicked()
            }
            
            // 使用模板按钮
            Button {
                id: templateBtn
                text: "使用模板"
                width: 150
                height: 48
                
                background: Rectangle {
                    radius: 6
                    color:  Qt.rgba(255, 255, 255, 0.1)
                    border.color: Qt.rgba(255, 255, 255, 0.2)
                    border.width: 1
                    
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: parent.color
                        opacity: parent.parent.pressed ? 0.8 : 1.0
                    }
                }
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 16
                    font.bold: true
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                Image {
                    source: "qrc:/icons/file.svg"
                    width: 20
                    height: 20
                    anchors.left: parent.left
                    anchors.leftMargin: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                onClicked: quickStart.loadTemplateClicked()
            }
        }
    }
}
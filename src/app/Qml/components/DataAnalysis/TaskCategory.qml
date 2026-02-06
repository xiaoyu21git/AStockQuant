// TaskCategory.qml - 任务分类组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15  // 添加这行
import ConsoleUi 1.0 as Theme
Rectangle {
    id: taskCategory
    Layout.fillWidth: true
    Layout.preferredHeight: 240  // 增加高度以容纳更多内容
    radius: 8
    color: Qt.rgba(26/255, 35/255, 126/255, 0.2)
    border.color: Theme.darkBorder
    border.width: 1
    
    property string iconSource: "qrc:/icons/database.svg"
    property string title: "任务分类"
    property var tasks: []
    signal taskClicked(string taskName)
    signal categoryClicked()
    // 内容
    Column {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15
        
        // 标题
        Row {
            width: parent.width
            spacing: 10
            
            Image {
                source: taskCategory.iconSource
                width: 20
                height: 20
                //color: Theme.accentColor
                anchors.verticalCenter: parent.verticalCenter
            }
            
            Text {
                text: taskCategory.title
                font.pixelSize: 16
                font.bold: true
                color: Theme.accentColor
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        
        // 任务列表
        Column {
            width: parent.width
            spacing: 8
            
            Repeater {
                model: taskCategory.tasks
                
                Rectangle {
                    width: parent.width
                    height: 36
                    color: "transparent"
                    
                    Row {
                        anchors.fill: parent
                        spacing: 10
                        
                        Rectangle {
                            width: parent.width - 80
                            height: parent.height
                            color: "transparent"
                            
                            Text {
                                text: modelData.name
                                font.pixelSize: 14
                                color: Theme.darkText
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }
                        
                        Rectangle {
                            width: 60
                            height: 20
                            radius: 10
                            color: modelData.status === "running" ? 
                                   Qt.rgba(0, 188/255, 212/255, 0.2)  : 
                                   Qt.rgba(76/255, 175/255, 80/255, 0.2)
                            anchors.verticalCenter: parent.verticalCenter
                            
                            Text {
                                text: modelData.status === "running" ? "运行中" : "已完成"
                                font.pixelSize: 10
                                color: modelData.status === "running" ? 
                                       Theme.accentColor : Theme.successColor
                                anchors.centerIn: parent
                            }
                        }
                    }
                    
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.darkBorder
                        anchors.bottom: parent.bottom
                    }
                }
            }
        }
    }
}
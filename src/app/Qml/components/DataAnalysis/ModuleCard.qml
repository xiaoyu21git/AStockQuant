// ModuleCard.qml - 功能模块卡片组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: moduleCard
    width: 350
    height: 320
    radius: 10
    color: Theme.darkCard
    border.color: Theme.darkBorder
    border.width: 1
    clip: true
    
    property string moduleId: ""
    property string iconSource: "qrc:/icons/database.svg"
    property string title: "模块标题"
    property string description: "模块描述"
    property var actions: []
    property var recentTasks: []
    
    signal actionClicked(string actionId)
    signal cardClicked()
    
    // 顶部边框
    Rectangle {
        width: parent.width
        height: 4
        color: Theme.accentColor
    }
    
    // 鼠标悬停效果
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        
        onEntered: {
            moduleCard.scale = 1.02
            moduleCard.z = 1
        }
        onExited: {
            moduleCard.scale = 1.0
            moduleCard.z = 0
        }
        onClicked: moduleCard.cardClicked()
    }
    
    // 内容布局
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10
        
        // 头部
        RowLayout {
            Layout.fillWidth: true
            spacing: 15
            
            // 图标
            Rectangle {
                Layout.preferredWidth: 50
                Layout.preferredHeight: 50
                radius: 10
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.primaryColor }
                    GradientStop { position: 1.0; color: Theme.secondaryColor }
                }
                
                Image {
                    source: moduleCard.iconSource
                    width: 24
                    height: 24
                    anchors.centerIn: parent
                    //color: "white"
                }
            }
            
            // 标题
            Text {
                Layout.fillWidth: true
                text: moduleCard.title
                font.pixelSize: 20
                font.bold: true
                color: Theme.darkText
                elide: Text.ElideRight
            }
        }
        
        // 描述
        Text {
            Layout.fillWidth: true
            text: moduleCard.description
            font.pixelSize: 14
            //color: "#aaa"
            wrapMode: Text.WordWrap
            Layout.bottomMargin: 10
        }
        
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.darkBorder
        }
        
        // 操作按钮区域
        Flow {
            Layout.fillWidth: true
            spacing: 10
            
            Repeater {
                model: moduleCard.actions
                
                Button {
                    text: modelData.label
                    width: 80
                    height: 36
                    
                    background: Rectangle {
                        radius: 4
                        color: modelData.primary ? Theme.primaryColor : "rgba(57, 73, 171, 0.2)"
                        border.color: modelData.primary ? Theme.primaryColor : Theme.darkBorder
                        border.width: 1
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        color: modelData.primary ? "white" : Theme.darkText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    Image {
                        source: modelData.icon
                        width: 14
                        height: 14
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                       // color: modelData.primary ? "white" : Theme.darkText
                    }
                    
                    onClicked: moduleCard.actionClicked(modelData.id)
                }
            }
        }
        
        // 最近任务
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 5
            
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    text: "最近任务"
                    font.pixelSize: 12
                    color: "#aaa"
                }
                
                Text {
                    text: "查看全部"
                    font.pixelSize: 10
                    color: "#777"
                    Layout.alignment: Qt.AlignRight
                }
            }
            
            // 任务列表
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: moduleCard.recentTasks
                clip: true
                
                delegate: Rectangle {
                    width: parent.width
                    height: 36
                    color: "transparent"
                    
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        
                        Image {
                            source: modelData.icon
                            width: 16
                            height: 16
                            //color: modelData.iconColor
                        }
                        
                        Text {
                            text: modelData.name
                            font.pixelSize: 12
                            color: Theme.darkText
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        
                        Rectangle {
                            width: 60
                            height: 20
                            radius: 10
                            color: modelData.status === "running" ? 
                                   "rgba(0, 188, 212, 0.2)" : 
                                   "rgba(76, 175, 80, 0.2)"
                            
                            Text {
                                text: modelData.status === "running" ? "运行中" : "已完成"
                                font.pixelSize: 10
                                color: modelData.status === "running" ? 
                                       Theme.accentColor : Theme.successColor
                                anchors.centerIn: parent
                            }
                        }
                    }
                }
            }
        }
    }
}
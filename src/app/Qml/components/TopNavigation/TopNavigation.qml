// Qml/page/TopNavigation.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0

Rectangle {
    id: topNav
    height: 64
    color: "#121828"
    
    // 属性
    property string title: ""
    property bool showBackButton: false
    property var rightItems: []
    
    // 信号
    signal notificationClicked()
    signal settingsClicked()
    signal searchRequested(string text)
    signal backButtonClicked()
    
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 16
        
        // Logo 区域
        Row {
            spacing: 12
            Layout.alignment: Qt.AlignLeft
            
            Rectangle {
                width: 36
                height: 36
                radius: 10
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#3b82f6" }
                    GradientStop { position: 1.0; color: "#1d4ed8" }
                }
                
                Text {
                    anchors.centerIn: parent
                    text: "Q"
                    color: "white"
                    font.pixelSize: 18
                    font.bold: true
                }
            }
            
            Text {
                text: "QuantumPro"
                font.pixelSize: 20
                font.bold: true
                color: "#3b82f6"
            }
        }
        
        // 页面标题（如果有）
        Text {
            text: topNav.title || ""
            font.pixelSize: 18
            font.bold: true
            color: "white"
            visible: topNav.title
        }
        
        Item { Layout.fillWidth: true }
        
        // 右侧操作按钮
        Row {
            spacing: 8
            
            // 右侧自定义按钮
            Repeater {
                model: topNav.rightItems
                
                delegate: Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: "#1a2235"
                    
                    Text {
                        anchors.centerIn: parent
                        text: modelData.icon || "⚙"
                        color: "#94a3b8"
                        font.pixelSize: 14
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.onClicked) {
                                modelData.onClicked()
                            }
                        }
                    }
                }
            }
            
            // 通知和设置图标
            Row {
                spacing: 12
                
                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: "#1a2235"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "🔔"
                        color: "#94a3b8"
                        font.pixelSize: 16
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: topNav.notificationClicked()
                    }
                }
                
                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: "#1a2235"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "⚙"
                        color: "#94a3b8"
                        font.pixelSize: 16
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: topNav.settingsClicked()
                    }
                }
            }
        }
    }
}
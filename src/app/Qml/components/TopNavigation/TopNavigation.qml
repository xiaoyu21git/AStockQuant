// Qml/page/TopNavigation.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0

Rectangle {
    id: topNav
    height: 64
    color: "#121828"
    
    // 信号
    signal notificationClicked()
    signal settingsClicked()
    signal searchRequested(string text)
    
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
        
        Item { Layout.fillWidth: true }
        
        // 搜索框
        Rectangle {
            width: 300
            height: 36
            radius: 18
            color: "#1a2235"
            border.color: "#2d3748"
            border.width: 1
            
            Row {
                anchors.fill: parent
                anchors.leftMargin: 16
                spacing: 8
                
                Text {
                    text: "🔍"
                    color: "#64748b"
                    font.pixelSize: 14
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Text {
                    text: "搜索策略名称、标签..."
                    color: "#64748b"
                    font.pixelSize: 14
                    anchors.verticalCenter: parent.verticalCenter
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
            }
        }
    }
}
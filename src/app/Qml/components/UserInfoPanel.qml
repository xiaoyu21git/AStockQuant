// UserInfoPanel.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: userInfoPanel
    height: 68
    
    // 属性
    property string userName: "量化交易员"
    property string userStatus: "专业版 · 在线"
    property string userInitials: "QT"
    property color onlineColor: "#10b981"
    property bool isOnline: true
    
    // 用户信息更新信号
    signal userInfoUpdated()
    signal profileClicked()
    
    Rectangle {
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: 8
        color: "#1a2235"
        radius: 12
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: profileClicked()
        }
        
        RowLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12
            
            // 用户头像
            Item {
                width: 36
                height: 36
                
                Rectangle {
                    anchors.fill: parent
                    radius: 18
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#8b5cf6" }
                        GradientStop { position: 1.0; color: "#6366f1" }
                    }
                    
                    Text {
                        anchors.centerIn: parent
                        text: userInitials
                        color: "white"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
            }
            
            // 用户信息
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                
                Text {
                    text: userName
                    color: "#f1f5f9"
                    font.pixelSize: 14
                    font.bold: true
                    elide: Text.ElideRight
                }
                
                Text {
                    text: userStatus
                    color: "#64748b"
                    font.pixelSize: 12
                }
            }
            
            // 在线状态指示器
            Item {
                width: 8
                height: 8
                
                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: isOnline ? onlineColor : "#64748b"
                    
                    // 在线状态动画
                    SequentialAnimation on opacity {
                        running: isOnline
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.5; to: 1; duration: 1000 }
                        NumberAnimation { from: 1; to: 0.5; duration: 1000 }
                    }
                }
            }
        }
    }
}
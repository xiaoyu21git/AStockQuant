// SecondaryMenu.qml
import QtQuick 2.15

Item {
    id: secondaryMenu
    height: 44
    width: parent ? parent.width : 280
    
    // 属性
    property string title: ""
    property string icon: ""
    property string badge: ""
    property string code: ""
    property bool active: false
    
    // 信号
    signal clicked(string menuCode, string menuTitle)
    
    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: 8
        color: active ? "#1a2235" : 
               mouseArea.containsMouse ? "#1a2235" : "transparent"
        border.color: active ? "#3b82f6" : "transparent"
        border.width: active ? 1 : 0
        
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                secondaryMenu.clicked(code, title)
            }
        }
        
        RowLayout {
            anchors.fill: parent
            spacing: 12
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            
            // 图标
            Item {
                width: 20
                height: 20
                Text { 
                    anchors.centerIn: parent
                    text: icon
                    font.pixelSize: 16
                    color: active ? "#3b82f6" : "#94a3b8"
                }
            }
            
            // 标题
            Text {
                text: title
                color: active ? "#f1f5f9" : "#94a3b8"
                font.pixelSize: 14
                font.bold: active
            }
            
            Item { Layout.fillWidth: true }
            
            // 角标（支持不同类型的角标颜色）
            Rectangle {
                visible: badge !== ""
                width: Math.max(24, badgeText.width + 12)
                height: 20
                radius: 10
                color: getBadgeColor(active, badge)
                border.color: getBadgeBorderColor(active, badge)
                border.width: 1
                
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: badge
                    color: getBadgeTextColor(active, badge)
                    font.pixelSize: 11
                }
            }
        }
    }
    
    // 获取角标颜色
    function getBadgeColor(isActive, badge) {
        if (isActive) return "#3b82f620"
        if (badge === "!" || badge === "警告") return "#ef444420"
        if (badge === "新" || badge === "New") return "#10b98120"
        if (badge === "Beta") return "#f59e0b20"
        return "#3b82f620"
    }
    
    // 获取角标边框颜色
    function getBadgeBorderColor(isActive, badge) {
        if (isActive) return "#3b82f6"
        if (badge === "!" || badge === "警告") return "#ef4444"
        if (badge === "新" || badge === "New") return "#10b981"
        if (badge === "Beta") return "#f59e0b"
        return "#3b82f6"
    }
    
    // 获取角标文本颜色
    function getBadgeTextColor(isActive, badge) {
        if (isActive) return "#3b82f6"
        if (badge === "!" || badge === "警告") return "#ef4444"
        if (badge === "新" || badge === "New") return "#10b981"
        if (badge === "Beta") return "#f59e0b"
        return "#3b82f6"
    }
}
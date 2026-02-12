import QtQuick 2.15

Row {
    id: viewModeToggle
    spacing: 4
    
    // 属性
    property string currentMode: "grid"
    
    // 信号
    signal modeChanged(string newMode)
    
    // 颜色常量
    readonly property color tertiaryBg: "#334155"
    readonly property color accentBlue: "#3B82F6"
    readonly property color textSecondary: "#94A3B8"
    
    // 网格视图按钮
    Rectangle {
        id: gridButton
        width: 32
        height: 32
        radius: 6
        
        color: currentMode === "grid" ? Qt.rgba(59/255, 130/255, 246/255, 0.2) : tertiaryBg
        border.color: currentMode === "grid" ? accentBlue : "transparent"
        border.width: currentMode === "grid" ? 2 : 0
        
        Text {
            anchors.centerIn: parent
            text: "☷"  // 网格图标
            font.pixelSize: 14
            color: currentMode === "grid" ? accentBlue : textSecondary
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                currentMode = "grid";
                modeChanged("grid");
            }
        }
    }
    
    // 列表视图按钮
    Rectangle {
        id: listButton
        width: 32
        height: 32
        radius: 6
        
        color: currentMode === "list" ? Qt.rgba(59/255, 130/255, 246/255, 0.2) : tertiaryBg
        border.color: currentMode === "list" ? accentBlue : "transparent"
        border.width: currentMode === "list" ? 2 : 0
        
        Text {
            anchors.centerIn: parent
            text: "☰"  // 列表图标
            font.pixelSize: 14
            color: currentMode === "list" ? accentBlue : textSecondary
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                currentMode = "list";
                modeChanged("list");
            }
        }
    }
}
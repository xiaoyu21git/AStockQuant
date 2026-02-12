import QtQuick 2.15

Rectangle {
    id: filterButton
    implicitWidth: 80
    implicitHeight: 40
    radius: 8  // borderRadiusMedium
    color: "#334155"  // tertiaryBg
    border.color: "#64748B"  // borderLight
    
    // 属性
    property bool hovered: false
    
    // 信号
    signal clicked()
    
    // 颜色常量
    readonly property color textSecondary: "#94A3B8"
    
    Row {
        anchors.centerIn: parent
        spacing: 6
        
        // 筛选图标
        Text {
            text: "🔍"
            font.pixelSize: 14
            color: textSecondary
        }
        
        // 文本
        Text {
            text: "筛选"
            font.pixelSize: 14
            color: textSecondary
        }
    }
    
    // 交互区域
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        
        onEntered: hovered = true
        onExited: hovered = false
        onClicked: filterButton.clicked()
    }
    
    // 悬停效果
    states: State {
        when: hovered
        PropertyChanges { target: filterButton; scale: 1.05 }
    }
    
    transitions: Transition {
        NumberAnimation { properties: "scale"; duration: 150 }
    }
}
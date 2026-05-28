import QtQuick 2.15

Rectangle {
    id: filterButton
    implicitWidth: 92
    implicitHeight: 40
    radius: 8
    color: active ? "#10261D" : "#0B1220"
    border.color: active ? "#1D6B4F" : "#334155"
    border.width: 1
    
    // 属性
    property bool hovered: false
    property bool active: false
    
    // 信号
    signal clicked()
    
    // 颜色常量
    readonly property color activeText: "#D1FAE5"
    readonly property color inactiveText: "#CBD5E1"
    
    Row {
        anchors.centerIn: parent
        spacing: 6
        
        // 筛选图标
        Text {
            text: "筛"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: active ? activeText : inactiveText
        }
        
        // 文本
        Text {
            text: "筛选"
            font.pixelSize: 14
            font.weight: Font.Medium
            color: active ? activeText : inactiveText
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
    
    states: State {
        when: hovered && !active
        PropertyChanges {
            target: filterButton
            color: "#111C2D"
            border.color: "#475569"
        }
    }
}
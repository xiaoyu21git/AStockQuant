import QtQuick 2.15

Rectangle {
    id: sortButton
    implicitWidth: 92
    implicitHeight: 40
    radius: 8
    color: active ? "#102033" : "#0B1220"
    border.color: active ? "#2563EB" : "#334155"
    border.width: 1
    
    // 属性
    property bool hovered: false
    property bool active: false
    
    // 信号
    signal clicked()
    
    // 颜色常量
    readonly property color activeText: "#DBEAFE"
    readonly property color inactiveText: "#CBD5E1"
    
    Row {
        anchors.centerIn: parent
        spacing: 6
        
        // 排序图标
        Text {
            text: "序"
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: active ? activeText : inactiveText
        }
        
        // 文本
        Text {
            text: "排序"
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
        onClicked: sortButton.clicked()
    }
    
    states: State {
        when: hovered && !active
        PropertyChanges {
            target: sortButton
            color: "#111C2D"
            border.color: "#475569"
        }
    }
}
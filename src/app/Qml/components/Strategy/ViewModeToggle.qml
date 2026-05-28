import QtQuick 2.15

Rectangle {
    id: viewModeToggle
    implicitWidth: toggleRow.implicitWidth + 8
    implicitHeight: 40
    radius: 8
    color: "#0B1220"
    border.width: 1
    border.color: "#334155"
    
    // 属性
    property string currentMode: "grid"
    
    // 信号
    signal modeChanged(string newMode)
    
    // 颜色常量
    readonly property color inactiveBg: "transparent"
    readonly property color activeBg: "#102033"
    readonly property color accentBlue: "#3B82F6"
    readonly property color textSecondary: "#CBD5E1"
    readonly property color mutedBorder: "#334155"

    Row {
        id: toggleRow
        anchors.centerIn: parent
        spacing: 4
    
        Rectangle {
            id: gridButton
            width: 66
            height: 32
            radius: 6

            color: currentMode === "grid" ? activeBg : inactiveBg
            border.color: currentMode === "grid" ? accentBlue : "transparent"
            border.width: currentMode === "grid" ? 1 : 0

            Text {
                anchors.centerIn: parent
                text: "网格"
                font.pixelSize: 13
                font.weight: Font.Medium
                color: currentMode === "grid" ? "#DBEAFE" : textSecondary
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

        Rectangle {
            id: listButton
            width: 66
            height: 32
            radius: 6

            color: currentMode === "list" ? activeBg : inactiveBg
            border.color: currentMode === "list" ? accentBlue : "transparent"
            border.width: currentMode === "list" ? 1 : 0

            Text {
                anchors.centerIn: parent
                text: "列表"
                font.pixelSize: 13
                font.weight: Font.Medium
                color: currentMode === "list" ? "#DBEAFE" : textSecondary
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
}
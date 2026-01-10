import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Rectangle {
    id: dayCell
    
    // ========== 属性 ==========
    property date date: new Date()
    property bool isToday: false
    property bool isSelected: false
    property bool isCurrentMonth: true
    property bool isWeekend: false
    property int weekNumber: -1
    property bool enabled: true
    
    signal clicked()
    
    // ========== 外观 ==========
    radius: 4
    color: {
        if (!enabled) return Material.backgroundColor
        if (isSelected) return Material.accent
        if (isToday) return todayColor
        return "transparent"
    }
    
    // ========== 交互 ==========
    MouseArea {
        anchors.fill: parent
        enabled: dayCell.enabled
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
        onClicked: dayCell.clicked()
        
        // 悬停效果
        hoverEnabled: true
        onEntered: {
            if (enabled && !isSelected) {
                parent.color = Material.highlightedButtonColor
            }
        }
        onExited: {
            if (enabled && !isSelected) {
                parent.color = isToday ? todayColor : "transparent"
            }
        }
    }
    
    // ========== 内容 ==========
    Row {
        anchors.centerIn: parent
        spacing: 2
        
        // 周数（可选）
        Text {
            visible: weekNumber > 0
            text: weekNumber
            font.pixelSize: 10
            color: Material.secondaryTextColor
        }
        
        // 日期数字
        Text {
            text: date.getDate()
            font.pixelSize: 14
            font.bold: isSelected || isToday
            color: {
                if (!enabled) return Material.disabledTextColor
                if (isSelected) return Material.primaryHighlightedTextColor
                if (!isCurrentMonth) return Material.secondaryTextColor
                if (isWeekend) return Material.color(Material.Red)
                return Material.primaryTextColor
            }
        }
    }
    
    // ========== 今天标记 ==========
    Rectangle {
        visible: isToday && !isSelected
        width: 4
        height: 4
        radius: 2
        color: Material.accent
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
    }
}
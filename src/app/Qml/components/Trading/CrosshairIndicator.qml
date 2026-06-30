// CrosshairIndicator.qml — 十字光标 (独立组件, 可复用)
import QtQuick 2.15

Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 100

    property var chartView: null
    property real mouseX: 0
    property real mouseY: 0
    property string priceLabel: ""
    property string timeLabel: ""

    // 垂直线
    Rectangle {
        width: 1; color: "#666688"
        x: Math.min(Math.max(root.mouseX, 0), parent.width)
        anchors { top: parent.top; bottom: parent.bottom }
    }

    // 水平线
    Rectangle {
        height: 1; color: "#666688"
        y: Math.min(Math.max(root.mouseY, 0), parent.height)
        anchors { left: parent.left; right: parent.right }
    }

    // 价格标签
    Rectangle {
        color: "#2a2a4e"; radius: 2; width: 64; height: 20
        anchors.right: parent.right; anchors.rightMargin: 6
        y: Math.min(Math.max(root.mouseY - 10, 0), parent.height - 20)
        Text {
            anchors.centerIn: parent
            text: root.priceLabel; color: "white"; font.pixelSize: 10
        }
        visible: root.priceLabel !== ""
    }

    // 时间标签
    Rectangle {
        color: "#2a2a4e"; radius: 2; width: 60; height: 18
        anchors.bottom: parent.bottom; anchors.bottomMargin: 4
        x: Math.min(Math.max(root.mouseX - 30, 0), parent.width - 60)
        Text {
            anchors.centerIn: parent
            text: root.timeLabel; color: "#aaaaaa"; font.pixelSize: 10
        }
        visible: root.timeLabel !== ""
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: function(mouse) {
            root.visible = true
            root.mouseX = mouse.x
            root.mouseY = mouse.y
            // 坐标映射由 CandlestickChart 处理并设置 priceLabel / timeLabel
        }
        onExited: root.visible = false
    }
}

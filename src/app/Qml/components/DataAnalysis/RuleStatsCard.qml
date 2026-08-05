// RuleStatsCard.qml — 规则统计指标卡片（紧凑型）
import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    width: 140
    height: 80
    radius: 8
    color: "#1E293B"
    border.width: 1
    border.color: "#334155"

    property string label: ""
    property var value: 0
    property string suffix: ""
    property string cardColor: "#38BDF8"
    property bool isLoading: false
    property string tooltipText: ""

    ToolTip {
        visible: tooltipText !== "" && mouseArea.containsMouse
        text: tooltipText
        delay: 500
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
    }

    Column {
        anchors.centerIn: parent
        spacing: 4

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.label
            font.pixelSize: 11
            color: "#94A3B8"
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 2

            Text {
                text: {
                    if (root.isLoading) return "—"  // em dash
                    if (root.value === -1) return "N/A"
                    if (typeof root.value === "number") {
                        // 若为比率类值 (0~1区间)，显示为百分比
                        if (root.suffix === "%" && root.value <= 1.0 && root.value >= 0.0)
                            return (root.value * 100).toFixed(1)
                        return root.value.toFixed(1)
                    }
                    return String(root.value)
                }
                font.pixelSize: 22
                font.bold: true
                color: (root.value === -1 && !root.isLoading) ? "#64748B" : root.cardColor
            }

            Text {
                text: root.suffix
                font.pixelSize: 11
                color: "#64748B"
                visible: !root.isLoading && root.value !== -1
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}

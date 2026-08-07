import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: metricCardRoot

    required property string title
    required property string value
    property string description: ""
    property string color: "#F1F5F9"
    property string trend: "neutral"
    readonly property string trendColor: trend === "up" ? "#EF4444"
        : (trend === "down" ? "#10B981" : "#94A3B8")

    signal clicked()

    Layout.fillWidth: true
    Layout.preferredHeight: 70

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: "#0F172A"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2

            Text {
                text: title
                font.pixelSize: 10
                color: "#94A3B8"
            }

            Row {
                spacing: 4

                Text {
                    text: value
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    color: metricCardRoot.color
                }

                // 趋势指示器
                Text {
                    visible: trend !== "neutral"
                    text: trend === "up" ? "↑" : "↓"
                    font.pixelSize: 12
                    color: trendColor
                }
            }

            Text {
                text: description
                font.pixelSize: 9
                color: "#64748B"
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: metricCardRoot.clicked()
    }
}

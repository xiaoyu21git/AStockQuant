import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property string title: ""
    property string value: ""
    property string description: ""
    property string trend: "neutral"
    property color backgroundColor: "#0F172A"
    property color titleColor: "#F1F5F9"
    property color descriptionColor: "#94A3B8"
    property color neutralColor: "#F1F5F9"
    property color upColor: "#EF4444"
    property color downColor: "#10B981"
    property bool hideTrendWhenNeutral: true
    property bool hideTrendForPlaceholder: true
    property int cardHeight: 84
    property int titleSize: 13
    property int valueSize: 18
    property int descriptionSize: 9
    property int trendSize: 14

    Layout.fillWidth: true
    Layout.preferredHeight: cardHeight

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: root.backgroundColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 2

            Text {
                text: root.title
                font.pixelSize: root.titleSize
                font.weight: Font.DemiBold
                color: root.titleColor
            }

            Row {
                spacing: 6

                Text {
                    text: root.value
                    font.pixelSize: root.valueSize
                    font.weight: Font.DemiBold
                    color: root.getValueColor()
                }

                Text {
                    visible: root.showTrendIndicator()
                    text: root.trend === "up" ? "↑" : "↓"
                    font.pixelSize: root.trendSize
                    color: root.trend === "up" ? root.upColor : root.downColor
                }
            }

            Text {
                text: root.description
                font.pixelSize: root.descriptionSize
                color: root.descriptionColor
            }
        }
    }

    function showTrendIndicator() {
        if (hideTrendWhenNeutral && trend === "neutral") {
            return false
        }
        if (hideTrendForPlaceholder && (value === "--" || value === "N/A")) {
            return false
        }
        return trend === "up" || trend === "down"
    }

    function getValueColor() {
        if (value === "--" || value === "N/A") return descriptionColor
        if (trend === "up") return upColor
        if (trend === "down") return downColor
        return neutralColor
    }
}

import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: statusSummaryCard

    property var cardData: ({})
    property color fallbackAccentColor: "#3b82f6"
    property bool centered: false

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: "#121828"
        border.color: "#2d3748"
        border.width: 1
        clip: true

        Rectangle {
            width: parent.width
            height: 3
            color: statusSummaryCard.cardData.accentColor || statusSummaryCard.fallbackAccentColor
            radius: 1.5
        }

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 8

            RowLayout {
                width: parent.width

                Text {
                    text: statusSummaryCard.cardData.title || ""
                    color: "#94a3b8"
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Item {
                    width: 32
                    height: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: statusSummaryCard.cardData.accentBackground || "#3b82f620"
                        border.color: statusSummaryCard.cardData.accentBorder || "#3b82f655"
                        border.width: 1

                        Image {
                            anchors.centerIn: parent
                            source: statusSummaryCard.cardData.iconSource || "qrc:/resources/icons/chart-line.svg"
                            width: 16
                            height: 16
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                }
            }

            Text {
                text: statusSummaryCard.cardData.value || ""
                color: statusSummaryCard.cardData.valueColor || "#f1f5f9"
                font.pixelSize: 20
                font.weight: Font.Medium
                width: parent.width
                elide: Text.ElideRight
            }

            RowLayout {
                spacing: 4
                width: parent.width

                Item {
                    width: 12
                    height: 12

                    Text {
                        anchors.centerIn: parent
                        text: statusSummaryCard.cardData.indicatorText || "•"
                        color: statusSummaryCard.cardData.accentColor || statusSummaryCard.fallbackAccentColor
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                }

                Text {
                    text: statusSummaryCard.cardData.detail || ""
                    color: statusSummaryCard.cardData.detailColor || "#94a3b8"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }
}
import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: marketGrid

    property var marketData: []
    property var marketSections: []

    function sectionStock(stockReference) {
        if (typeof stockReference === "number") {
            return marketGrid.marketData[stockReference] || ({})
        }
        return stockReference || ({})
    }

    function instrumentLabel(stockData) {
        var symbolText = String((stockData || {}).symbol || "--")
        var nameText = String((stockData || {}).name || "")
        return nameText.length > 0 ? (symbolText + " " + nameText) : symbolText
    }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: "#121828"
        border.color: "#2d3748"
        border.width: 1
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            Item {
                Layout.fillWidth: true
                height: 30

                RowLayout {
                    anchors.fill: parent

                    Text {
                        text: "热门板块"
                        color: "#f1f5f9"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }

                    Item { Layout.fillWidth: true }

                    Item {
                        width: 24
                        height: 24

                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: "#3b82f620"

                            Text {
                                anchors.centerIn: parent
                                text: "🔄"
                                font.pixelSize: 12
                                color: "#3b82f6"
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                RowLayout {
                    anchors.fill: parent
                    spacing: 12

                    Repeater {
                        model: marketGrid.marketSections.length > 0 ? marketGrid.marketSections.length : 0

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            readonly property var sectionData: marketGrid.marketSections[index] || ({ title: "", icon: "", stocks: [] })

                            Rectangle {
                                anchors.fill: parent
                                radius: 12
                                color: "#1a2235"

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10

                                    Item {
                                        Layout.fillWidth: true
                                        height: 30

                                        RowLayout {
                                            anchors.fill: parent

                                            Text {
                                                text: sectionData.title
                                                color: "#94a3b8"
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Item { Layout.fillWidth: true }

                                            Item {
                                                width: 28
                                                height: 28

                                                Rectangle {
                                                    anchors.fill: parent
                                                    radius: 6
                                                    color: "#3b82f620"

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: sectionData.icon
                                                        font.pixelSize: 13
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        spacing: 6
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true

                                        Repeater {
                                            model: sectionData.stocks

                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 44
                                                readonly property var stockData: marketGrid.sectionStock(modelData)

                                                Rectangle {
                                                    anchors.fill: parent
                                                    radius: 8
                                                    color: "#121828"

                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.margins: 12
                                                        spacing: 12

                                                        Item {
                                                            width: 28
                                                            height: 28

                                                            Rectangle {
                                                                anchors.fill: parent
                                                                radius: 7
                                                                color: (stockData.color || "#3b82f6") + "20"

                                                                Text {
                                                                    anchors.centerIn: parent
                                                                    text: String(stockData.symbol || "").slice(0, 2)
                                                                    color: stockData.color || "#3b82f6"
                                                                    font.pixelSize: 11
                                                                    font.weight: Font.Medium
                                                                }
                                                            }
                                                        }

                                                        ColumnLayout {
                                                            spacing: 2
                                                            Layout.fillWidth: true

                                                            Text {
                                                                text: marketGrid.instrumentLabel(stockData)
                                                                color: "#f1f5f9"
                                                                font.pixelSize: 12
                                                                font.weight: Font.Medium
                                                                elide: Text.ElideRight
                                                                Layout.fillWidth: true
                                                            }

                                                            Text {
                                                                text: stockData.updatedAt || "待同步"
                                                                color: "#64748b"
                                                                font.pixelSize: 10
                                                                elide: Text.ElideRight
                                                                Layout.fillWidth: true
                                                            }
                                                        }

                                                        Item { Layout.fillWidth: true }

                                                        ColumnLayout {
                                                            spacing: 2
                                                            Layout.alignment: Qt.AlignRight

                                                            Text {
                                                                text: "¥" + Number(stockData.price || 0).toFixed(2)
                                                                color: "#f1f5f9"
                                                                font.pixelSize: 13
                                                                font.weight: Font.Medium
                                                            }

                                                            Text {
                                                                text: (Number(stockData.change || 0) > 0 ? "+" : "") +
                                                                      Number(stockData.change || 0).toFixed(2) + "%"
                                                                color: Number(stockData.change || 0) > 0 ? "#ef4444" : "#10b981"
                                                                font.pixelSize: 11
                                                                font.weight: Font.Medium
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

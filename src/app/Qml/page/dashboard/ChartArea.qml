import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: chartArea

    property var marketData: []
    property string currentSymbol: "000001.SZ"
    property string currencySymbol: "¥"
    property int selectedPeriodIndex: 0
    readonly property var quoteData: currentQuote()
    readonly property real latestPrice: Number(quoteData.price !== undefined ? quoteData.price : (quoteData.close || 0))
    readonly property real dayChange: Number(quoteData.change || 0)

    function currentQuote() {
        for (var index = 0; index < marketData.length; ++index) {
            if ((marketData[index] || {}).symbol === currentSymbol) {
                return marketData[index]
            }
        }
        return marketData.length > 0 ? marketData[0] : ({ symbol: currentSymbol, name: "", price: 0, change: 0 })
    }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: "#121828"
        border.color: "#2d3748"
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "实时K线"
                        color: "#f8fafc"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }

                    Text {
                        text: (chartArea.quoteData.symbol || chartArea.currentSymbol) + ((chartArea.quoteData.name || "").length > 0 ? ("  " + chartArea.quoteData.name) : "")
                        color: "#94a3b8"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                ColumnLayout {
                    spacing: 4

                    Text {
                        text: chartArea.latestPrice > 0 ? (chartArea.currencySymbol + chartArea.latestPrice.toFixed(2)) : "--"
                        color: chartArea.dayChange >= 0 ? "#ef4444" : "#10b981"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignRight
                    }

                    Text {
                        text: chartArea.latestPrice > 0 ? ((chartArea.dayChange >= 0 ? "+" : "") + chartArea.dayChange.toFixed(2) + "%") : "等待行情"
                        color: chartArea.dayChange >= 0 ? "#ef4444" : "#10b981"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "#0b1120"
                border.color: "#243042"
                border.width: 1

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "图表区域暂时留空"
                        color: "#cbd5e1"
                        font.pixelSize: 18
                        font.weight: Font.Medium
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "后续切换到新的行情图方案后再接入"
                        color: "#64748b"
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}

import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: marketGrid

    property var marketData: []
    property var marketSections: []

    // 动态工作区自绑定: 当外部未提供数据时, 从 Bridge 自动拉取
    Component.onCompleted: {
        if (marketSections.length === 0 && Bridge.MarketDataBridge) {
            updateSectorSection()
            if (marketSections.length === 0 && typeof Bridge.MarketDataBridge.fetchSectorHeat === "function")
                Bridge.MarketDataBridge.fetchSectorHeat()
        }
    }
    Connections {
        target: Bridge.MarketDataBridge
        enabled: Bridge.MarketDataBridge !== null
        function onSectorHeatDataChanged() { marketGrid.updateSectorSection() }
    }

    function updateSectorSection() {
        var raw = Bridge.MarketDataBridge ? Bridge.MarketDataBridge.sectorHeatData : []
        if (raw.length === 0) return
        var stocks = []
        for (var i = 0; i < raw.length; i++) {
            var s = raw[i]
            var sig = (s.signal===0) ? "真机会" : (s.signal===1 ? "诱多" : (s.signal===2 ? "抄底" : "走弱"))
            stocks.push({
                symbol: s.name, name: sig + " " + (s.lead||""),
                price: s.chg, change: s.chg,
                color: (s.signal===0) ? "#22c55e" : (s.signal===1 ? "#ef4444" : (s.signal===2 ? "#f59e0b" : "#888")),
                updatedAt: (s.netIn||0>=0?"+":"") + (Math.abs(s.netIn||0)>=1e8?(Math.abs(s.netIn)/1e8).toFixed(1)+"亿":Math.abs(s.netIn||0)>=1e4?(Math.abs(s.netIn)/1e4).toFixed(0)+"万":Math.abs(s.netIn||0).toFixed(0)) + " 净流入率" + (s.netInRate||0).toFixed(1)+"%"
            })
        }
        var secs = marketSections.slice()
        secs.push({ title: "热门板块", icon: "🔥", stocks: stocks })
        marketSections = secs
    }

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
                        model: marketGrid.marketSections

                        Item {
                            id: sectionCard
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            readonly property var sectionData: sectionCard.modelData || ({ title: "", icon: "", stocks: [] })

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
                                                text: sectionCard.sectionData.title
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
                                                        text: sectionCard.sectionData.icon
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
                                            model: (sectionCard.sectionData.stocks || []).map(function(stockReference) {
                                                var stockData = marketGrid.sectionStock(stockReference)
                                                return {
                                                    stockData: stockData,
                                                    instrumentLabel: marketGrid.instrumentLabel(stockData)
                                                }
                                            })

                                            Item {
                                                id: stockRow
                                                required property var modelData
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 44
                                                readonly property var stockData: stockRow.modelData.stockData || ({})

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
                                                                color: (stockRow.stockData.color || "#3b82f6") + "20"

                                                                Text {
                                                                    anchors.centerIn: parent
                                                                    text: String(stockRow.stockData.symbol || "").slice(0, 2)
                                                                    color: stockRow.stockData.color || "#3b82f6"
                                                                    font.pixelSize: 11
                                                                    font.weight: Font.Medium
                                                                }
                                                            }
                                                        }

                                                        ColumnLayout {
                                                            spacing: 2
                                                            Layout.fillWidth: true

                                                            Text {
                                                                text: stockRow.modelData.instrumentLabel || "--"
                                                                color: "#f1f5f9"
                                                                font.pixelSize: 12
                                                                font.weight: Font.Medium
                                                                elide: Text.ElideRight
                                                                Layout.fillWidth: true
                                                            }

                                                            Text {
                                                                text: stockRow.stockData.updatedAt || "待同步"
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
                                                                text: "¥" + Number(stockRow.stockData.price || 0).toFixed(2)
                                                                color: "#f1f5f9"
                                                                font.pixelSize: 13
                                                                font.weight: Font.Medium
                                                            }

                                                            Text {
                                                                text: (Number(stockRow.stockData.change || 0) > 0 ? "+" : "") +
                                                                      Number(stockRow.stockData.change || 0).toFixed(2) + "%"
                                                                color: Number(stockRow.stockData.change || 0) > 0 ? "#ef4444" : "#10b981"
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

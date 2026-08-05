import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: positionsPanel

    property var positions: {
        if (typeof Bridge !== "undefined" && Bridge.PositionAccountBridge)
            return Bridge.PositionAccountBridge.positions || []
        return []
    }
    property real totalMarketValue: 0
    property string currencySymbol: "$"
    property var marketDataService: null

    Connections {
        target: typeof Bridge !== "undefined" ? Bridge.PositionAccountBridge : null
        function onPositionsChanged() {
            positionsPanel.positions = Bridge.PositionAccountBridge.positions || []
        }
    }

    function pnlColor(value) {
        return Number(value || 0) >= 0 ? "#ef4444" : "#10b981"
    }

    function changeColor(value) {
        return Number(value || 0) >= 0 ? "#ef4444" : "#10b981"
    }

    function resolvePositionDisplayName(positionData) {
        var position = positionData || ({})
        var explicitName = String(position.name || "").trim()
        if (explicitName.length > 0) {
            return explicitName
        }

        var symbol = String(position.symbol || "").trim().toUpperCase()
        if (symbol.length > 0 && marketDataService && marketDataService.resolveInstrument) {
            var instrument = marketDataService.resolveInstrument(symbol)
            var instrumentName = instrument && instrument.name ? String(instrument.name).trim() : ""
            if (instrumentName.length > 0) {
                return instrumentName
            }
        }

        return symbol.length > 0 ? symbol : "--"
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
            anchors.margins: 18
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: "当前持仓"
                    color: "#f1f5f9"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }

                Rectangle {
                    radius: 8
                    color: "#172033"
                    border.color: "#2f3d55"
                    border.width: 1
                    implicitWidth: positionCountText.implicitWidth + 14
                    implicitHeight: 24

                    Text {
                        id: positionCountText
                        anchors.centerIn: parent
                        text: String(positionsPanel.positions.length) + " 只"
                        color: "#cbd5e1"
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: positionsPanel.currencySymbol + Number(positionsPanel.totalMarketValue).toLocaleString(Qt.locale(), 'f', 2)
                    color: "#3b82f6"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "#0f1724"
                border.color: "#243042"
                border.width: 1

                Item {
                    anchors.fill: parent
                    visible: positionsPanel.positions.length === 0

                    Column {
                        anchors.centerIn: parent
                        spacing: 8

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "当前暂无持仓回流"
                            color: "#94a3b8"
                            font.pixelSize: 13
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "收到真实仓位后，这里会显示完整持仓列表"
                            color: "#64748b"
                            font.pixelSize: 11
                        }
                    }
                }

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 10
                    contentWidth: width
                    contentHeight: positionsColumn.height
                    clip: true
                    visible: positionsPanel.positions.length > 0

                    Column {
                        id: positionsColumn
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: positionsPanel.positions.map(function(position) {
                                var positionData = position || ({})
                                var pnlValue = Number(positionData.pnl || 0)
                                var changeValue = Number(positionData.change || 0)
                                return {
                                    positionData: positionData,
                                    displayName: positionsPanel.resolvePositionDisplayName(positionData),
                                    sharesText: String(Number(positionData.shares || 0)) + "股 / 可卖 " + String(Number(positionData.availableQuantity || 0)),
                                    lastPriceText: "现价 " + positionsPanel.currencySymbol + Number(positionData.lastPrice || 0).toFixed(2),
                                    lastPriceColor: positionsPanel.changeColor(changeValue),
                                    avgPriceText: "成本 " + positionsPanel.currencySymbol + Number(positionData.avgPrice || 0).toFixed(2),
                                    currentValueText: positionsPanel.currencySymbol + Number(positionData.currentValue || 0).toLocaleString(Qt.locale(), 'f', 2),
                                    pnlText: (pnlValue >= 0 ? "+" : "-")
                                        + positionsPanel.currencySymbol + Math.abs(pnlValue).toLocaleString(Qt.locale(), 'f', 2)
                                        + " · " + (Number(positionData.pnlRate || 0) >= 0 ? "+" : "") + Number(positionData.pnlRate || 0).toFixed(2) + "%"
                                        + " · 仓位 " + Number(positionData.weight || 0).toFixed(1) + "%",
                                    pnlColor: positionsPanel.pnlColor(pnlValue)
                                }
                            })

                            Rectangle {
                                id: positionCard
                                width: positionCard.parent ? positionCard.parent.width : 0
                                height: 62
                                radius: 10
                                color: "#182133"
                                border.color: "#243042"
                                border.width: 1
                                required property var modelData
                                readonly property var positionData: positionCard.modelData.positionData || ({})

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10

                                    ColumnLayout {
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 150
                                        spacing: 2

                                        Text {
                                            text: positionCard.modelData.displayName || "--"
                                            color: "#f8fafc"
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: positionCard.modelData.sharesText || "--"
                                            color: "#64748b"
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.preferredWidth: 110
                                        spacing: 2
                                        Layout.alignment: Qt.AlignRight

                                        Text {
                                            text: positionCard.modelData.lastPriceText || "现价 --"
                                            color: positionCard.modelData.lastPriceColor || "#64748b"
                                            font.pixelSize: 12
                                            horizontalAlignment: Text.AlignRight
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: positionCard.modelData.avgPriceText || "成本 --"
                                            color: "#64748b"
                                            font.pixelSize: 10
                                            horizontalAlignment: Text.AlignRight
                                            Layout.fillWidth: true
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Layout.alignment: Qt.AlignRight

                                        Text {
                                            text: positionCard.modelData.currentValueText || "--"
                                            color: "#f8fafc"
                                            font.pixelSize: 13
                                            font.weight: Font.Medium
                                            horizontalAlignment: Text.AlignRight
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: positionCard.modelData.pnlText || "--"
                                            color: positionCard.modelData.pnlColor || "#64748b"
                                            font.pixelSize: 10
                                            horizontalAlignment: Text.AlignRight
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
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

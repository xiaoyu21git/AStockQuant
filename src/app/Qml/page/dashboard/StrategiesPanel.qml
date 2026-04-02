import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: strategiesPanel

    property var strategies: []

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

            Item {
                Layout.fillWidth: true
                height: 32

                RowLayout {
                    anchors.fill: parent

                    Text {
                        text: "策略监控"
                        color: "#f1f5f9"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "8/12 运行中"
                        color: "#3b82f6"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Repeater {
                        model: strategiesPanel.strategies.length

                        Item {
                            Layout.fillWidth: true
                            height: 60
                            readonly property var strategyData: strategiesPanel.strategies[index] || ({})

                            Rectangle {
                                anchors.fill: parent
                                radius: 12
                                color: "#1a2235"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 12

                                    ColumnLayout {
                                        spacing: 4

                                        Text {
                                            text: strategyData.name || ""
                                            color: "#f1f5f9"
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                        }

                                        RowLayout {
                                            spacing: 6

                                            Item {
                                                width: 8
                                                height: 8

                                                Rectangle {
                                                    anchors.fill: parent
                                                    radius: 4
                                                    color: strategyData.status === "running" ? "#10b981" : "#f59e0b"
                                                }
                                            }

                                            Text {
                                                text: strategyData.status === "running" ? "运行中 · " : "已暂停 · "
                                                color: "#64748b"
                                                font.pixelSize: 12
                                            }

                                            Text {
                                                text: strategyData.stocks || ""
                                                color: "#94a3b8"
                                                font.pixelSize: 12
                                            }
                                        }
                                    }

                                    Item { Layout.fillWidth: true }

                                    ColumnLayout {
                                        spacing: 2
                                        Layout.alignment: Qt.AlignRight

                                        Text {
                                            text: `${Number(strategyData.returns || 0) > 0 ? "+" : ""}${Number(strategyData.returns || 0).toFixed(1)}%`
                                            color: Number(strategyData.returns || 0) > 0 ? "#10b981" : "#ef4444"
                                            font.pixelSize: 16
                                            font.bold: true
                                        }

                                        Text {
                                            text: `${strategyData.trades || 0} 笔交易`
                                            color: "#64748b"
                                            font.pixelSize: 12
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

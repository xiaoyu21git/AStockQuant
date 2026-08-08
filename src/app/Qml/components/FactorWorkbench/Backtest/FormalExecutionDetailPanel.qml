import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot
        Layout.fillWidth: true
        Layout.preferredHeight: formalTradingDetailPanelHeight()
        visible: formalTradingDetailPanelHeight() > 0
        radius: 12
        color: "#111827"
        border.width: 1
        border.color: "#243041"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "正式执行资产轨迹"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F8FAFC"
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "最新在前"
                    font.pixelSize: 10
                    color: "#94A3B8"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                radius: 8
                color: "#0F172A"
                border.width: 1
                border.color: "#243041"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    Text {
                        Layout.preferredWidth: 104
                        text: "执行日"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        color: "#94A3B8"
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "现金"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        color: "#94A3B8"
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "持仓市值"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        color: "#94A3B8"
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "总资产"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        color: "#94A3B8"
                    }
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: formalTradingDetailColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: formalTradingDetailColumn
                    width: parent.width
                    spacing: 8

                    Repeater {
                        model: formalTradingDetailRows()

                        delegate: Rectangle {
                            width: formalTradingDetailColumn.width
                            height: 32
                            radius: 8
                            color: index % 2 === 0 ? "#0F172A" : "#111827"
                            border.width: 1
                            border.color: "#243041"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12

                                Text {
                                    Layout.preferredWidth: 104
                                    text: String(modelData.date || "--")
                                    font.pixelSize: 10
                                    color: "#CBD5E1"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: formatOptionalAssetMetric(modelData.cash)
                                    font.pixelSize: 10
                                    color: "#CBD5E1"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: formatOptionalAssetMetric(modelData.marketValue)
                                    font.pixelSize: 10
                                    color: "#CBD5E1"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: formatOptionalAssetMetric(modelData.totalAsset)
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    color: formalTradingAccentColor(formalTradingStatus())
                                }
                            }
                        }
                    }
                }
            }
        }
    }

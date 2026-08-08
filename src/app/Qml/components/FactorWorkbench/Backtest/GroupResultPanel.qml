import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot

    Layout.fillWidth: true
    Layout.fillHeight: true
    radius: 12
    color: "#1E293B"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "📊 分组内容"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }

            Item { Layout.fillWidth: true }

            ComboBox {
                id: resultSelector
                Layout.preferredWidth: 220
                visible: false
                model: displayedBacktestResults()
                currentIndex: selectedBacktestResultIndex

                delegate: ItemDelegate {
                    width: resultSelector.width
                    text: displayedBacktestResultName(modelData)
                }

                contentItem: Text {
                    text: resultSelector.currentIndex >= 0 && resultSelector.currentIndex < displayedBacktestResults().length
                        ? displayedBacktestResultName(displayedBacktestResults()[resultSelector.currentIndex])
                        : "选择回测结果"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: 8
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                }

                onActivated: function(index) {
                    selectedBacktestResultIndex = index
                    applyDisplayedBacktestResult(backtestResult)
                }
            }

            Text {
                text: groupResults.length > 0 ? "共 " + groupResults.length + " 个分组" : "等待回测结果"
                font.pixelSize: 12
                color: "#94A3B8"
            }
        }

        ListView {
            id: groupListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 220
            model: groupResults
            clip: true
            spacing: 8

            delegate: Rectangle {
                width: groupListView.width
                height: 60
                radius: 8
                color: "#1E293B"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        radius: 16
                        color: "#0F172A"

                        Text {
                            anchors.centerIn: parent
                            text: modelData.groupIndex || (index + 1)
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            color: "#F1F5F9"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: "第 " + (modelData.groupIndex || (index + 1)) + " 组"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "#F1F5F9"
                        }

                        RowLayout {
                            spacing: 16

                            Text {
                                text: "股票: " + formatMetric(modelData.stockCount, 0, false)
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }

                            Text {
                                text: "因子值: " + formatMetric(modelData.minFactorValue, 2, false) + " - " + formatMetric(modelData.maxFactorValue, 2, false)
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: 2

                        Text {
                            text: formatPercentMetric(modelData.returnRate, 2, false)
                            font.pixelSize: 16
                            font.weight: Font.Bold
                            color: returnMetricColor(modelData.returnRate)
                        }

                        Text {
                            text: "收益"
                            font.pixelSize: 10
                            color: "#94A3B8"
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    color: "#3B82F620"
                    border.width: 2
                    border.color: "#3B82F6"
                    visible: isBacktesting && currentGroup === (index + 1)
                }
            }

            Text {
                anchors.centerIn: parent
                text: isBacktesting ? "正在计算分组..." : "请开始回测查看分组内容"
                font.pixelSize: 14
                color: "#94A3B8"
                visible: groupResults.length === 0
            }
        }
    }
}

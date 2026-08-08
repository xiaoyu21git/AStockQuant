import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot
        Layout.fillWidth: true
        Layout.preferredHeight: 122
        radius: 10
        color: "#0F172A"
        border.width: 1
        border.color: "#2B3A55"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "因子结果对比"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F8FAFC"
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: previousBacktestReport && Object.keys(previousBacktestReport).length > 0
                        ? ("上一轮基线: " + ((previousBacktestReport.config && previousBacktestReport.config.factorName) || previousBacktestReport.factorId || "当前因子"))
                        : "上一轮基线: 暂无"
                    font.pixelSize: 11
                    color: "#93C5FD"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: [
                        { title: "上一轮基线", value: previousBacktestReport && Object.keys(previousBacktestReport).length > 0 ? "已存在" : "暂无", accent: "#38BDF8" },
                        { title: "本轮结果", value: currentDisplayedBacktestResult() ? "已生成" : "待回测", accent: "#34D399" },
                        { title: "比较维度", value: "指标/有效期", accent: "#F59E0B" }
                    ]

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        radius: 8
                        color: "#111827"
                        border.width: 1
                        border.color: modelData.accent

                        Row {
                            anchors.centerIn: parent
                            spacing: 8

                            Text {
                                text: modelData.title
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }

                            Text {
                                text: modelData.value
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: modelData.accent
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: root.buildFactorStockPoolComparisonText() + " 回测完成后，系统会自动比较上一轮和本轮结果；结果明显时自动覆盖，结果接近时再让你确认。"
                    font.pixelSize: 10
                    color: "#94A3B8"
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

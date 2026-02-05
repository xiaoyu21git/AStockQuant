// HistoryContent.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Rectangle {
    id: historyContent
    color: "transparent"
    
    property var userData: null

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        // 标题区域
        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5

                Text {
                    text: "我的交易历史分析"
                    color: "#e2e8f0"
                    font.pointSize: 22
                    font.bold: true
                }

                Text {
                    text: "基于您的交易记录生成的个性化分析"
                    color: "#94a3b8"
                    font.pointSize: 14
                }
            }

            Rectangle {
                width: 120
                height: 40
                radius: 8
                color: "#475569"
                border.color: "#64748b"
                border.width: 1

                MouseArea {
                    anchors.fill: parent
                    onClicked: console.log("导出报告")

                    Row {
                        spacing: 8
                        anchors.centerIn: parent

                        Text {
                            text: "📥"
                            color: "#e2e8f0"
                            font.pointSize: 14
                        }

                        Text {
                            text: "导出报告"
                            color: "#e2e8f0"
                            font.pointSize: 14
                        }
                    }
                }
            }
        }

        // 图表区域
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            // 月度表现图表
            ChartContainer {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "我的月度交易表现"
                
                ChartView {
                    anchors.fill: parent
                    antialiasing: true
                    backgroundColor: "transparent"
                    
                    BarSeries {
                        axisX: BarCategoryAxis {
                            categories: ["1月", "2月", "3月", "4月", "5月", "6月", 
                                        "7月", "8月", "9月", "10月", "11月", "12月"]
                            labelsColor: "#e2e8f0"
                        }
                        axisY: ValueAxis {
                            min: -2
                            max: 5
                            labelFormat: "%d%"
                            labelsColor: "#94a3b8"
                        }
                        
                        BarSet {
                            label: "月度收益率"
                            values: [2.3, 1.8, -0.5, 3.2, 2.1, 1.5, 
                                    2.8, -1.2, 3.5, 2.9, 1.7, 4.2]
                        }
                    }
                }
            }

            // 行业偏好图表
            ChartContainer {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "我的行业偏好分析"
                
                ChartView {
                    anchors.fill: parent
                    antialiasing: true
                    backgroundColor: "transparent"
                    legend.visible: true
                    legend.labelColor: "#e2e8f0"
                    legend.alignment: Qt.AlignRight
                    
                    PieSeries {
                        PieSlice {
                            label: "科技"
                            value: 35
                            color: "#3b82f6"
                        }
                        PieSlice {
                            label: "金融"
                            value: 25
                            color: "#10b981"
                        }
                        PieSlice {
                            label: "消费"
                            value: 15
                            color: "#f59e0b"
                        }
                        PieSlice {
                            label: "医疗"
                            value: 10
                            color: "#ef4444"
                        }
                        PieSlice {
                            label: "能源"
                            value: 8
                            color: "#8b5cf6"
                        }
                        PieSlice {
                            label: "工业"
                            value: 7
                            color: "#94a3b8"
                        }
                    }
                }
            }
        }

        // 最佳/最差交易图表
        ChartContainer {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            title: "我的最佳/最差交易分析"
            
            ChartView {
                anchors.fill: parent
                antialiasing: true
                backgroundColor: "transparent"
                
                HorizontalBarSeries {
                    axisY: BarCategoryAxis {
                        categories: ["AAPL (+23%)", "TSLA (+18%)", "MSFT (+15%)", 
                                    "GOOGL (-8%)", "AMZN (-5%)"]
                        labelsColor: "#e2e8f0"
                    }
                    axisX: ValueAxis {
                        min: -10
                        max: 25
                        labelFormat: "%d%"
                        labelsColor: "#94a3b8"
                    }
                    
                    BarSet {
                        label: "收益率"
                        values: [23, 18, 15, -8, -5]
                    }
                }
            }
        }
    }
}
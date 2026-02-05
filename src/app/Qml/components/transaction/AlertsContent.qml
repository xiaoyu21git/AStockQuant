// AlertsContent.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Rectangle {
    id: alertsContent
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
                    text: "个性化预警设置"
                    color: "#e2e8f0"
                    font.pointSize: 22
                    font.bold: true
                }

                Text {
                    text: "根据您的交易习惯定制的提醒和预警"
                    color: "#94a3b8"
                    font.pointSize: 14
                }
            }

            Rectangle {
                width: 120
                height: 40
                radius: 8
                color: "#3b82f6"
                border.color: "#2563eb"
                border.width: 1

                MouseArea {
                    anchors.fill: parent
                    onClicked: console.log("添加预警")

                    Row {
                        spacing: 8
                        anchors.centerIn: parent

                        Text {
                            text: "➕"
                            color: "white"
                            font.pointSize: 14
                        }

                        Text {
                            text: "添加预警"
                            color: "white"
                            font.pointSize: 14
                        }
                    }
                }
            }
        }

        // 预警列表
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: userData.alerts

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    radius: 10
                    color: "#0f172a"
                    border.left: 4
                    border.color: modelData.color

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 20

                        // 预警图标
                        Rectangle {
                            width: 60
                            height: 60
                            radius: 10
                            color: Qt.lighter(modelData.color, 1.3)
                            opacity: 0.2

                            Text {
                                text: {
                                    if (modelData.type === "股价突破") return "📈";
                                    else if (modelData.type === "收益率目标") return "🎯";
                                    else if (modelData.type === "风险系数") return "⚠️";
                                    else return "🔔";
                                }
                                color: modelData.color
                                font.pointSize: 24
                                anchors.centerIn: parent
                            }
                        }

                        // 预警信息
                        ColumnLayout {
                            spacing: 5
                            Layout.fillWidth: true

                            Text {
                                text: {
                                    if (modelData.type === "股价突破") 
                                        return modelData.type + "预警";
                                    else if (modelData.type === "收益率目标")
                                        return "个性化收益率目标达成";
                                    else if (modelData.type === "风险系数")
                                        return modelData.type + "超标预警";
                                    else
                                        return "交易频率异常";
                                }
                                color: "#e2e8f0"
                                font.pointSize: 16
                                font.bold: true
                            }

                            Text {
                                text: {
                                    if (modelData.type === "股价突破") 
                                        return modelData.symbol + " 超过 $" + modelData.price + " 时提醒 • 您已设置关注";
                                    else if (modelData.type === "收益率目标")
                                        return "当月收益率超过 " + modelData.target + "% 时提醒 • 基于您的历史表现";
                                    else if (modelData.type === "风险系数")
                                        return "投资组合风险系数超过 " + modelData.threshold + " 时提醒 • 您的偏好值为 0.75";
                                    else
                                        return "单日交易次数超过 " + modelData.threshold + " 次时提醒 • 基于您的平均交易频率";
                                }
                                color: "#94a3b8"
                                font.pointSize: 14
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        // 预警操作按钮
                        Row {
                            spacing: 10
                            Layout.alignment: Qt.AlignRight

                            Rectangle {
                                width: 80
                                height: 36
                                radius: 8
                                color: "#475569"
                                border.color: "#64748b"
                                border.width: 1

                                Text {
                                    text: "编辑"
                                    color: "#e2e8f0"
                                    font.pointSize: 14
                                    anchors.centerIn: parent
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: console.log("编辑预警: " + modelData.type)
                                }
                            }

                            Rectangle {
                                width: 80
                                height: 36
                                radius: 8
                                color: "#3b82f6"
                                border.color: "#2563eb"
                                border.width: 1

                                Text {
                                    text: modelData.enabled ? "启用" : "禁用"
                                    color: "white"
                                    font.pointSize: 14
                                    anchors.centerIn: parent
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        modelData.enabled = !modelData.enabled;
                                        console.log("切换预警状态: " + modelData.type);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // 预警历史图表
        ChartContainer {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            title: "我的预警触发历史"
            
            ChartView {
                anchors.fill: parent
                antialiasing: true
                backgroundColor: "transparent"
                legend.visible: true
                legend.labelColor: "#e2e8f0"
                
                LineSeries {
                    name: "股价预警"
                    color: "#3b82f6"
                    width: 3
                    
                    XYPoint { x: 0; y: 3 }
                    XYPoint { x: 1; y: 5 }
                    XYPoint { x: 2; y: 2 }
                    XYPoint { x: 3; y: 6 }
                    XYPoint { x: 4; y: 4 }
                    XYPoint { x: 5; y: 7 }
                    XYPoint { x: 6; y: 5 }
                }
                
                LineSeries {
                    name: "风险预警"
                    color: "#ef4444"
                    width: 3
                    
                    XYPoint { x: 0; y: 1 }
                    XYPoint { x: 1; y: 2 }
                    XYPoint { x: 2; y: 1 }
                    XYPoint { x: 3; y: 3 }
                    XYPoint { x: 4; y: 2 }
                    XYPoint { x: 5; y: 1 }
                    XYPoint { x: 6; y: 2 }
                }
                
                LineSeries {
                    name: "收益率预警"
                    color: "#10b981"
                    width: 3
                    
                    XYPoint { x: 0; y: 0 }
                    XYPoint { x: 1; y: 1 }
                    XYPoint { x: 2; y: 0 }
                    XYPoint { x: 3; y: 2 }
                    XYPoint { x: 4; y: 1 }
                    XYPoint { x: 5; y: 3 }
                    XYPoint { x: 6; y: 2 }
                }
                
                ValueAxis {
                    id: axisX
                    min: 0
                    max: 6
                    labelFormat: "%d日"
                    labelsColor: "#94a3b8"
                }
                
                ValueAxis {
                    id: axisY
                    min: 0
                    max: 8
                    labelsColor: "#94a3b8"
                }
            }
        }
    }
}
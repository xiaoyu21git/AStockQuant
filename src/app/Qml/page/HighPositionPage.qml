import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AStock.Engine 1.0

Page {
    id: highPage

    // 顶部工具栏
    header: ToolBar {
        RowLayout {
            anchors.fill: parent

            Button {
                text: "← 返回"
                onClicked: stackView.pop()
            }

            Label {
                text: "高位股监控"
                font.pixelSize: 16
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Label {
                text: "近 60 日涨幅大、仍处高位的标的"
                color: "#888"
                font.pixelSize: 12
            }
        }
    }

    // 使用 C++ 高位股模型，从数据库真实筛选
    HighPositionModel {
        id: highStockModel
    }

    // 页面加载时从数据库扫描
    Component.onCompleted: {
        highStockModel.refresh(60)
    }

    // 主体内容
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // 顶部筛选 / 操作栏
        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "规则: 近 60 日涨幅 > 50%, 回撤 < 10% (示意)"
                color: "#666"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Button {
                text: highStockModel.busy ? "扫描中..." : "刷新扫描"
                enabled: !highStockModel.busy
                onClicked: highStockModel.refresh(60)
            }
        }

        // 高位股列表
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "white"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4

                // 表头
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    color: "#f5f5f5"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6

                        Label { text: "代码";        Layout.preferredWidth: 110; font.bold: true }
                        Label { text: "名称";        Layout.preferredWidth: 120; font.bold: true }
                        Label { text: "当前价";      Layout.preferredWidth: 80;  font.bold: true }
                        Label { text: "昨日涨跌幅";  Layout.preferredWidth: 100; font.bold: true }
                        Label { text: "近 N 日涨幅"; Layout.preferredWidth: 110; font.bold: true }
                        Label { text: "距高点回撤"; Layout.preferredWidth: 110; font.bold: true }
                        Label { text: "距严重异动"; Layout.preferredWidth: 110; font.bold: true }
                        Label { text: "近5日主力净流入(万)"; Layout.preferredWidth: 150; font.bold: true }
                        Label { text: "最近龙虎榜"; Layout.preferredWidth: 90; font.bold: true }
                        Item  { Layout.fillWidth: true }
                        Label { text: "操作";    Layout.preferredWidth: 90; font.bold: true }
                    }
                }

                // 列表
                ListView {
                    id: listView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: highStockModel

                    delegate: Rectangle {
                        width: listView.width
                        height: 40
                        color: index % 2 === 0 ? "#ffffff" : "#f8f9fa"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 6

                            Label {
                                text: symbol
                                Layout.preferredWidth: 110
                            }

                            Label {
                                text: name
                                Layout.preferredWidth: 120
                            }

                            Label {
                                text: lastPrice.toFixed(2)
                                Layout.preferredWidth: 80
                            }

                            Label {
                                // previousChange 已是百分比数值，例如 2.5 表示 +2.5%
                                text: previousChange.toFixed(1) + "%"
                                Layout.preferredWidth: 100
                                color: previousChange >= 0 ? "#c0392b" : "#27ae60" // 红涨绿跌
                            }

                            Label {
                                text: (riseFromLow * 100).toFixed(1) + "%"
                                Layout.preferredWidth: 110
                                color: "#e67e22"
                            }

                            Label {
                                text: (drawdownFromHigh * 100).toFixed(1) + "%"
                                Layout.preferredWidth: 110
                                color: "#c0392b"
                            }

                            Label {
                                text: (extremeGap * 100).toFixed(1) + "%"
                                Layout.preferredWidth: 110
                                color: "#2980b9"
                            }

                            Label {
                                text: (netMainInflow5d / 10000).toFixed(1)
                                Layout.preferredWidth: 150
                                color: netMainInflow5d > 0 ? "#27ae60" : (netMainInflow5d < 0 ? "#c0392b" : "#666")
                            }

                            Label {
                                text: recentOnLhb ? "是" : "否"
                                Layout.preferredWidth: 90
                                color: recentOnLhb ? "#e67e22" : "#666"
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: "回测该股"
                                Layout.preferredWidth: 90
                                onClicked: {
                                    // 一键跳转到回测页面，并预选当前标的
                                    // 与本文件同目录，因此直接引用 "BacktestPage.qml"
                                    stackView.push("BacktestPage.qml", {
                                        preselectedSymbol: symbol
                                    })
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

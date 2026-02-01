import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AStock.Engine 1.0

Page {
    id: dashboardPage
    
    header: ToolBar {
        height: 50
        
        RowLayout {
            anchors.fill: parent
            
            ToolButton {
                icon.source: "qrc:/icons/arrow_back.svg"
                onClicked: {
                    if (StackView.view) {
                        StackView.view.pop()
                    }
                }
            }
            
            Label {
                text: "📊 回测仪表盘"
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
            }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20
        
        ColumnLayout {
            spacing: 4

            Text {
                text: "欢迎使用 AStockQuant"
                font.pixelSize: 24
                font.bold: true
            }

            Text {
                text: "当前页面展示的是最近一次本地回测结果，不代表实盘账户收益。"
                font.pixelSize: 11
                color: "#888888"
                wrapMode: Text.WordWrap
            }
        }

        // 仪表盘内容
        GridLayout {
            columns: 2
            columnSpacing: 20
            rowSpacing: 20
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 当前权益（资金规模）
            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#3498db"

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        text: "当前权益"
                        color: "white"
                        font.pixelSize: 14
                    }

                    Text {
                        text: GlobalEquityModel && GlobalEquityModel.lastEquity > 0
                              ? ("¥" + GlobalEquityModel.lastEquity.toFixed(2))
                              : "--"
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }

            // 累计收益
            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#2ecc71"

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        text: "累计收益"
                        color: "white"
                        font.pixelSize: 14
                    }

                    Text {
                        text: GlobalEquityModel && GlobalEquityModel.firstEquity > 0
                              ? ((GlobalEquityModel.returnPct * 100).toFixed(2) + "%")
                              : "0.00%"
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }

            // 当前持仓数量
            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#9b59b6"

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        text: "当前持仓数量"
                        color: "white"
                        font.pixelSize: 14
                    }

                    Text {
                        text: GlobalTradeModel ? (GlobalTradeModel.currentPosition.toFixed(0) + " 股") : "0 股"
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }
        }
    }
}
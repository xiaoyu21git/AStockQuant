import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AStock.Engine 1.0

Page {
    id: backtestPage
    
    // 工具栏
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            
            Button {
                text: "← 返回"
                onClicked: stackView.pop()
            }
            
            Label {
                text: "策略回测"
                font.pixelSize: 16
                font.bold: true
                Layout.fillWidth: true
            }
        }
    }
    
    // 主内容
    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        
        // 配置卡片
        GroupBox {
            title: "回测配置"
            Layout.fillWidth: true
            
            GridLayout {
                columns: 2
                rowSpacing: 10
                columnSpacing: 20
                anchors.fill: parent
                
                Label { text: "开始日期:" }
                TextField {
                    id: startDate
                    text: "2023-01-01"
                    placeholderText: "YYYY-MM-DD"
                }
                
                Label { text: "结束日期:" }
                TextField {
                    id: endDate
                    text: "2023-12-31"
                    placeholderText: "YYYY-MM-DD"
                }
                
                Label { text: "初始资金:" }
                TextField {
                    id: capital
                    text: "100000"
                    validator: DoubleValidator { bottom: 0 }
                }
                
                Label { text: "选择策略:" }
                ComboBox {
                    id: strategyCombo
                    model: ["移动平均线策略", "RSI策略", "布林带策略"]
                }
            }
        }
        
        // 按钮组
        RowLayout {
            Layout.fillWidth: true
            
            Button {
                text: "开始回测"
                highlighted: true
                onClicked: startBacktest()
            }
            
            Button {
                text: "停止"
                enabled: false
            }
            
            Button {
                text: "导出结果"
            }
            
            Item { Layout.fillWidth: true }
        }
        
        // 结果区域
        TabBar {
            id: resultTabs
            Layout.fillWidth: true
            
            TabButton { text: "交易记录" }
            TabButton { text: "收益曲线" }
            TabButton { text: "统计指标" }
        }
        
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: resultTabs.currentIndex
            
            // 交易记录表格
            ScrollView {
                ListView {
                    id: tradeList
                    model: tradeModel
                    
                    delegate: Rectangle {
                        width: tradeList.width
                        height: 40
                        color: index % 2 === 0 ? "#f9f9f9" : "white"
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            
                            Label {
                                text: model.time || "2023-01-01"
                                Layout.preferredWidth: 100
                            }
                            Label {
                                text: model.symbol || "000001"
                                Layout.preferredWidth: 80
                            }
                            Label {
                                text: model.price || "10.00"
                                Layout.preferredWidth: 80
                            }
                            Label {
                                text: model.volume || "100"
                                Layout.preferredWidth: 80
                            }
                            Label {
                                text: model.side || "买入"
                                Layout.preferredWidth: 60
                            }
                        }
                    }
                }
            }
            
            // 收益曲线占位
            Rectangle {
                color: "lightgray"
                Label {
                    anchors.centerIn: parent
                    text: "收益曲线图"
                }
            }
            
            // 统计指标占位
            Rectangle {
                color: "lightgray"
                Label {
                    anchors.centerIn: parent
                    text: "统计指标表"
                }
            }
        }
    }
    
    // 回测模型
    TradeRecordModel {
        id: tradeModel
    }
    
    // 开始回测函数
    function startBacktest() {
        console.log("开始回测...")
        console.log("配置:", {
            start: startDate.text,
            end: endDate.text,
            capital: capital.text,
            strategy: strategyCombo.currentText
        })
        
        // 模拟添加一些测试数据
        for (let i = 0; i < 10; i++) {
            tradeModel.addTrade(
                "000001.SZ",
                10.0 + i * 0.1,
                100 * (i + 1),
                i % 2 === 0 ? "买入" : "卖出"
            )
        }
    }
}
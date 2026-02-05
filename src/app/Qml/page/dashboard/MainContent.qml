import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0
Item {
    id: mainContent
    
    // 属性
    property var marketData: []
    property var statusCards: []
    property var positions: []
    property var strategies: []
    
    Rectangle {
        anchors.fill: parent
        color: "#0a0f1a"
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            
            // 顶部导航栏 - 固定
            Item {
                height: 64
                
                Rectangle {
                    anchors.fill: parent
                    color: "#121828"
                    border.color: "#2d3748"
                    border.width: 1
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 16
                        
                        // 面包屑导航
                        Item {
                            Layout.fillWidth: true
                            
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "首页 / 交易台 / 实时交易"
                                color: "#94a3b8"
                                font.pixelSize: 14
                            }
                        }
                    }
                }
            }
            
            // 状态卡片区域 - 固定
            Item {
                height: 180
                Layout.fillWidth: true
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 16
                    
                    Repeater {
                        model: statusCards
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            Rectangle {
                                anchors.fill: parent
                                radius: 12
                                color: "#121828"
                                border.color: "#2d3748"
                                border.width: 1
                                
                                Rectangle {
                                    width: parent.width
                                    height: 3
                                    color: index === 1 ? "#10b981" : 
                                           index === 2 ? "#f59e0b" : "#3b82f6"
                                    radius: 1.5
                                }
                                
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    spacing: 8
                                    
                                    RowLayout {
                                        //居中
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        Text {
                                            text: modelData.title
                                            color: "#94a3b8"
                                            font.pixelSize: 13
                                            font.weight: Font.Medium
                                        }
                                        
                                        Item { 
                                            Layout.fillWidth: true 
                                            // 使用绑定到自定义间距
                                            Layout.minimumWidth: 150
                                        }
                                        
                                        Item {
                                            width: 32
                                            height: 32
                                                Image {
                                                    anchors.centerIn: parent
                                                    //这里根据序号判断应该显示那个图标
                                                    source:  {
                                                        if (index === 0) "qrc:/resources/icons/chart-line.svg"
                                                        else if (index === 1) "qrc:/resources/icons/100.svg"
                                                        else if (index === 2) "qrc:/resources/icons/shield-alt.svg"
                                                        else "qrc:/resources/icons/robot.svg"
                                                    }
                                                    width: 16
                                                    height: 16
                                                    fillMode: Image.PreserveAspectFit   
                                                } 
                                            // }
                                        }
                                    }
                                    
                                    Text {
                                        text: modelData.value
                                        color: "#f1f5f9"
                                        font.pixelSize: 24
                                        font.bold: true
                                        //水平居中
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    
                                    RowLayout {
                                        spacing: 4
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        Item {
                                            width: 12
                                            height: 12
                                            Text { anchors.centerIn: parent; text: "↑"; color: index === 1 ? "#10b981" : "#3b82f6" }
                                        }
                                        Text {
                                            text: {
                                                if (accountValue !== undefined && accountValue !== null) {
                                                    return "$" + accountValue.toFixed(2)
                                                }
                                                return "$0.00"
                                            }
                                        }
                                        // Text {
                                        //     text: index === 2 ? modelData.changeText : 
                                        //           `+${modelData.change.toFixed(2)}%`
                                        //     color: index === 1 ? "#10b981" : 
                                        //            index === 2 ? "#f59e0b" : "#3b82f6"
                                        //     font.pixelSize: 12
                                        //     font.weight: Font.Medium
                                        // }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 主要内容区域 - 可滚动区域（包含图表和所有其他内容）
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                Flickable {
                    id: mainFlickable
                    anchors.fill: parent
                    contentWidth: parent.width
                    contentHeight: contentColumn.height + 48 // 添加一些底部边距
                    clip: true
                    
                    ColumnLayout {
                        id: contentColumn
                        width: parent.width
                        spacing: 24
                        anchors.margins: 24
                        
                        // 图表和交易区域
                        Item {
                            Layout.fillWidth: true
                            height: 500
                            
                            RowLayout {
                                anchors.fill: parent
                                spacing: 20
                                
                                // 实时行情图表区域
                                ChartArea {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    marketData: mainContent.marketData
                                }
                                
                                // 右侧交易面板
                                TradingPanel {
                                    width: 320
                                    Layout.fillHeight: true
                                    marketData: mainContent.marketData
                                }
                            }
                        }
                        
                        // 板块/市场数据网格
                        Item {
                            Layout.fillWidth: true
                            height: 220
                            
                            MarketGrid {
                                anchors.fill: parent
                                marketData: mainContent.marketData
                            }
                        }
                        
                        // 持仓和策略区域
                        Item {
                            Layout.fillWidth: true
                            height: 600
                            
                            RowLayout {
                                anchors.fill: parent
                                spacing: 20
                                
                                // 左侧：持仓和策略
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: 20
                                        
                                        // 持仓面板
                                        Item {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            
                                            PositionsPanel {
                                                anchors.fill: parent
                                                positions: mainContent.positions
                                            }
                                        }
                                        
                                        // 策略监控面板
                                        Item {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 200
                                            
                                            StrategiesPanel {
                                                anchors.fill: parent
                                                strategies: mainContent.strategies
                                            }
                                        }
                                    }
                                }
                                // 右侧：订单簿
                                Item {
                                    width: 320
                                    Layout.fillHeight: true 
                                    OrderBook {
                                        anchors.fill: parent
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
import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: tradingPanel
    
    property var marketData: []
    property var tradeFields: [
        { label: "交易类型", value: "市价单", suffix: "" },
        { label: "数量", value: "100", suffix: "股" },
        { label: "价格", value: "182.45", suffix: "USD" },
        { label: "总金额", value: "18,245.00", suffix: "USD" }
    ]
    
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
                        text: "交易面板"
                        color: "#f1f5f9"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 交易对占位符
                    Item {
                        height: 28
                        width: 80
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: "#1a2235"
                            
                            Text {
                                anchors.centerIn: parent
                                text: "AAPL"
                                color: "#3b82f6"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
            
            // 交易表单
            ColumnLayout {
                spacing: 12
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                Repeater {
                    model: tradingPanel.tradeFields.length
                    
                    Item {
                        Layout.fillWidth: true
                        height: 64
                        readonly property var fieldData: tradingPanel.tradeFields[index] || ({})
                        
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 4
                            
                            Text {
                                text: fieldData.label || ""
                                color: "#94a3b8"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            
                            // 输入框
                            Item {
                                Layout.fillWidth: true
                                height: 40
                                
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 8
                                    color: "#1a2235"
                                    border.color: "#475569"
                                    border.width: 1
                                    
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        
                                        Text {
                                            text: fieldData.value || ""
                                            color: "#f1f5f9"
                                            font.pixelSize: 13
                                        }
                                        
                                        Item { Layout.fillWidth: true }
                                        
                                        Text {
                                            text: fieldData.suffix || ""
                                            color: "#64748b"
                                            font.pixelSize: 13
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 交易按钮
            Item {
                Layout.fillWidth: true
                height: 44
                
                RowLayout {
                    anchors.fill: parent
                    spacing: 12
                    
                    // 买入按钮，之后要用按钮模版替换
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#10b981" }
                                GradientStop { position: 1.0; color: "#059669" }
                            }
                            
                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 8
                                
                                Text {
                                    text: "↑"
                                    color: "white"
                                    font.pixelSize: 14
                                }
                                
                                Text {
                                    text: "买入"
                                    color: "white"
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }
                    
                    // 卖出按钮
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#ef4444" }
                                GradientStop { position: 1.0; color: "#dc2626" }
                            }
                            
                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 8
                                
                                Text {
                                    text: "↓"
                                    color: "white"
                                    font.pixelSize: 14
                                }
                                
                                Text {
                                    text: "卖出"
                                    color: "white"
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
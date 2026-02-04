import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: marketGrid
    
    property var marketData: []
    
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
                height: 24
                
                RowLayout {
                    anchors.fill: parent
                    
                    Text {
                        text: "热门板块"
                        color: "#f1f5f9"
                        font.pixelSize: 16
                        font.weight: Font.SemiBold
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Item {
                        width: 24
                        height: 24
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: "#3b82f620"
                            
                            Text {
                                anchors.centerIn: parent
                                text: "🔄"
                                font.pixelSize: 12
                                color: "#3b82f6"
                            }
                        }
                    }
                }
            }
            
            // 市场数据网格
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                RowLayout {
                    anchors.fill: parent
                    spacing: 16
                    
                    Repeater {
                        model: [
                            {title: "美股市场", icon: "📈", stocks: [0, 1]},
                            {title: "科技板块", icon: "💻", stocks: [2, 3]},
                            {title: "热门股票", icon: "🔥", stocks: [4, 5]}
                        ]
                        
                        // 市场卡片
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            Rectangle {
                                anchors.fill: parent
                                radius: 12
                                color: "#1a2235"
                                
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 12
                                    
                                    Item {
                                        Layout.fillWidth: true
                                        height: 24
                                        
                                        RowLayout {
                                            anchors.fill: parent
                                            
                                            Text {
                                                text: modelData.title
                                                color: "#94a3b8"
                                                font.pixelSize: 14
                                                font.weight: Font.SemiBold
                                            }
                                            
                                            Item { Layout.fillWidth: true }
                                            
                                            // 图标占位符
                                            Item {
                                                width: 32
                                                height: 32
                                                
                                                Rectangle {
                                                    anchors.fill: parent
                                                    radius: 8
                                                    color: "#3b82f620"
                                                    
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData.icon
                                                        font.pixelSize: 16
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    
                                    ColumnLayout {
                                        spacing: 10
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        
                                        Repeater {
                                            model: modelData.stocks
                                            
                                            // 市场项目
                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 50
                                                
                                                Rectangle {
                                                    anchors.fill: parent
                                                    radius: 8
                                                    color: "#121828"
                                                    
                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.margins: 12
                                                        spacing: 12
                                                        
                                                        // 股票图标
                                                        Item {
                                                            width: 32
                                                            height: 32
                                                            
                                                            Rectangle {
                                                                anchors.fill: parent
                                                                radius: 8
                                                                color: marketGrid.marketData[modelData].color + "20"
                                                                
                                                                Text {
                                                                    anchors.centerIn: parent
                                                                    text: marketGrid.marketData[modelData].symbol[0]
                                                                    color: marketGrid.marketData[modelData].color
                                                                    font.pixelSize: 14
                                                                    font.bold: true
                                                                }
                                                            }
                                                        }
                                                        
                                                        ColumnLayout {
                                                            spacing: 2
                                                            Text {
                                                                text: marketGrid.marketData[modelData].symbol
                                                                color: "#f1f5f9"
                                                                font.pixelSize: 14
                                                                font.weight: Font.SemiBold
                                                            }
                                                            Text {
                                                                text: marketGrid.marketData[modelData].name
                                                                color: "#64748b"
                                                                font.pixelSize: 11
                                                            }
                                                        }
                                                        
                                                        Item { Layout.fillWidth: true }
                                                        
                                                        ColumnLayout {
                                                            spacing: 2
                                                            Layout.alignment: Qt.AlignRight
                                                            Text {
                                                                text: "$" + marketGrid.marketData[modelData].price.toFixed(2)
                                                                color: "#f1f5f9"
                                                                font.pixelSize: 14
                                                                font.weight: Font.SemiBold
                                                            }
                                                            Text {
                                                                text: (marketGrid.marketData[modelData].change > 0 ? "+" : "") + 
                                                                      marketGrid.marketData[modelData].change.toFixed(2) + "% " + 
                                                                      (marketGrid.marketData[modelData].change > 0 ? "↗" : "↘")
                                                                color: marketGrid.marketData[modelData].change > 0 ? "#10b981" : "#ef4444"
                                                                font.pixelSize: 12
                                                                font.weight: Font.Medium
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
                    }
                }
            }
        }
    }
}
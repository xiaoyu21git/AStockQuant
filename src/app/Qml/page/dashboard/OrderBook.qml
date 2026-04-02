import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: orderBook

    property var orderEntries: [
        { price: 182.50, amount: 200, total: 36500, type: "ask" },
        { price: 182.45, amount: 150, total: 27367.5, type: "ask" },
        { price: 182.40, amount: 300, total: 54720, type: "ask" },
        { price: 182.35, amount: "-", total: "最新", type: "current" },
        { price: 182.30, amount: 250, total: 45575, type: "bid" },
        { price: 182.25, amount: 180, total: 32805, type: "bid" },
        { price: 182.20, amount: 350, total: 63770, type: "bid" }
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
                        text: "订单簿 - AAPL"
                        color: "#f1f5f9"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 订单簿标签占位符
                    RowLayout {
                        spacing: 4
                        
                        Repeater {
                            model: ["深度", "近期", "历史"]
                            
                            Item {
                                width: 50
                                height: 28
                                
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 6
                                    color: index === 0 ? "#3b82f6" : "transparent"
                                    border.color: index === 0 ? "#3b82f6" : "#475569"
                                    border.width: 1
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: index === 0 ? "white" : "#94a3b8"
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 订单簿表格
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    
                    // 表头
                    Item {
                        Layout.fillWidth: true
                        height: 30
                        
                        RowLayout {
                            anchors.fill: parent
                            
                            Text {
                                text: "价格 (USD)"
                                color: "#64748b"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                Layout.preferredWidth: 80
                            }
                            
                            Text {
                                text: "数量"
                                color: "#64748b"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                Layout.preferredWidth: 80
                                Layout.alignment: Qt.AlignHCenter
                            }
                            
                            Text {
                                text: "总额"
                                color: "#64748b"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                Layout.preferredWidth: 80
                                Layout.alignment: Qt.AlignRight
                            }
                        }
                    }
                    
                    // 订单行
                    Repeater {
                        model: orderBook.orderEntries.length
                        
                        Item {
                            Layout.fillWidth: true
                            height: 30
                            readonly property var entryData: orderBook.orderEntries[index] || ({})
                            
                            RowLayout {
                                anchors.fill: parent
                                
                                Text {
                                    text: "$" + (typeof entryData.price === 'number' ? entryData.price.toFixed(2) : (entryData.price || ""))
                                    color: entryData.type === "ask" ? "#ef4444" : 
                                           entryData.type === "bid" ? "#10b981" : "#3b82f6"
                                    font.pixelSize: 13
                                    font.weight: entryData.type === "current" ? Font.Bold : Font.Normal
                                    Layout.preferredWidth: 80
                                }
                                
                                Text {
                                    text: entryData.amount || ""
                                    color: entryData.type === "current" ? "#3b82f6" : "#94a3b8"
                                    font.pixelSize: 13
                                    font.weight: entryData.type === "current" ? Font.Bold : Font.Normal
                                    Layout.preferredWidth: 80
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                
                                Text {
                                    text: typeof entryData.total === 'number' ? 
                                          "$" + entryData.total.toLocaleString(Qt.locale(), 'f', 0) : 
                                          (entryData.total || "")
                                    color: entryData.type === "current" ? "#3b82f6" : "#94a3b8"
                                    font.pixelSize: 13
                                    font.weight: entryData.type === "current" ? Font.Bold : Font.Normal
                                    Layout.preferredWidth: 80
                                    Layout.alignment: Qt.AlignRight
                                }
                            }
                            
                            Rectangle {
                                width: parent.width
                                height: 1
                                color: "#2d374850"
                                visible: index === 3 || index === orderBook.orderEntries.length - 1
                                y: parent.height - height
                            }
                        }
                    }
                }
            }
        }
    }
}
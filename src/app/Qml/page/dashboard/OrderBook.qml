import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: orderBook

    property string currentSymbol: "000001.SZ"
    property string currencySymbol: "¥"
    property var recentOrders: []
    property int activeTabIndex: 0

    function normalizedOrderStatus(status) {
        return String(status || "").toUpperCase()
    }

    function displayOrderStatus(status) {
        var statusText = normalizedOrderStatus(status)
        if (statusText === "SUBMITTED") {
            return "已报"
        }
        if (statusText === "PENDING") {
            return "待处理"
        }
        if (statusText === "PARTIAL_FILLED") {
            return "部分成交"
        }
        if (statusText === "FILLED") {
            return "已成交"
        }
        if (statusText === "CANCELLED") {
            return "已撤单"
        }
        if (statusText === "REJECTED") {
            return "已拒绝"
        }
        return statusText || "--"
    }

    readonly property var orderEntries: recentOrders && recentOrders.length > 0 ? recentOrders.slice(0, 7).map(function(order, index) {
        var statusText = String(order.status || "")
        return {
            price: Number(order.fillPrice || order.price || 0),
            amount: order.fillQuantity || order.quantity || "--",
            total: Number(order.filledNotional || order.requestedNotional || 0),
            type: statusText === "FILLED" ? "bid" : (statusText === "SUBMITTED" ? "current" : "ask"),
            status: displayOrderStatus(statusText || "--")
        }
    }) : [
        { price: 12.52, amount: 1200, total: 15024, type: "ask", status: "挂单" },
        { price: 12.50, amount: 800, total: 10000, type: "current", status: "最新" },
        { price: 12.48, amount: 1600, total: 19968, type: "bid", status: "回流" }
    ]
    
    Rectangle {
        anchors.fill: parent
        radius: 16
        color: "#121828"
        border.color: "#2d3748"
        border.width: 1
        clip: true
        
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
                        text: "执行队列 - " + orderBook.currentSymbol
                        color: "#f1f5f9"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                    }
                    
                    // 订单簿标签占位符
                    RowLayout {
                        spacing: 4
                        Layout.preferredWidth: 162
                        Layout.minimumWidth: 150
                        
                        Repeater {
                            model: ["回流", "状态", "历史"]
                            
                            Item {
                                width: 46
                                height: 28
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: orderBook.activeTabIndex = index
                                }
                                
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 6
                                    color: orderBook.activeTabIndex === index ? "#3b82f6" : "transparent"
                                    border.color: orderBook.activeTabIndex === index ? "#3b82f6" : "#475569"
                                    border.width: 1
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: orderBook.activeTabIndex === index ? "white" : "#94a3b8"
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
                            spacing: 8
                            
                            Text {
                                text: "价格"
                                color: "#64748b"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.minimumWidth: 0
                            }
                            
                            Text {
                                text: "数量"
                                color: "#64748b"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.minimumWidth: 0
                                horizontalAlignment: Text.AlignHCenter
                            }
                            
                            Text {
                                text: "金额 / 状态"
                                color: "#64748b"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1.6
                                Layout.minimumWidth: 0
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                    
                    // 订单行
                    Repeater {
                        model: orderBook.orderEntries.length
                        
                        Item {
                            Layout.fillWidth: true
                            height: 40
                            readonly property var entryData: orderBook.orderEntries[index] || ({})
                            
                            RowLayout {
                                anchors.fill: parent
                                spacing: 8
                                
                                Text {
                                    text: orderBook.currencySymbol + (typeof entryData.price === 'number' ? entryData.price.toFixed(2) : (entryData.price || ""))
                                     color: entryData.type === "ask" ? "#10b981" : 
                                         entryData.type === "bid" ? "#ef4444" : "#3b82f6"
                                    font.pixelSize: 13
                                    font.weight: entryData.type === "current" ? Font.Bold : Font.Normal
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.minimumWidth: 0
                                    elide: Text.ElideRight
                                }
                                
                                Text {
                                    text: entryData.amount || ""
                                    color: entryData.type === "current" ? "#3b82f6" : "#94a3b8"
                                    font.pixelSize: 13
                                    font.weight: entryData.type === "current" ? Font.Bold : Font.Normal
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.minimumWidth: 0
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                }
                                
                                Text {
                                    text: typeof entryData.total === 'number' ? 
                                        (orderBook.currencySymbol + entryData.total.toLocaleString(Qt.locale(), 'f', 0) + " · " + (entryData.status || "")) : 
                                        ((entryData.total || "") + " " + (entryData.status || ""))
                                    color: entryData.type === "current" ? "#3b82f6" : "#94a3b8"
                                    font.pixelSize: 13
                                    font.weight: entryData.type === "current" ? Font.Bold : Font.Normal
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1.6
                                    Layout.minimumWidth: 0
                                    horizontalAlignment: Text.AlignRight
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
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
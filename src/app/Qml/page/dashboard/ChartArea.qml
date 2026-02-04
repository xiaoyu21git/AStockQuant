import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15

Item {
    id: chartArea
    
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
            
            // 图表标题和控制
            Item {
                Layout.fillWidth: true
                height: 40
                
                RowLayout {
                    anchors.fill: parent
                    
                    Text {
                        text: "实时行情图表 - AAPL 苹果公司"
                        color: "#f1f5f9"
                        font.pixelSize: 16
                        font.weight: Font.SemiBold
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 股票选择器
                    Item {
                        height: 40
                        width: 180
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: "#1a2235"
                            border.color: "#475569"
                            border.width: 1
                            
                            RowLayout {
                                anchors.fill: parent
                                spacing: 8
                                anchors.margins: 12
                                
                                Item {
                                    width: 24
                                    height: 24
                                    
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 6
                                        color: "#3b82f620"
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: "A"
                                            color: "#3b82f6"
                                            font.pixelSize: 12
                                            font.bold: true
                                        }
                                    }
                                }
                                
                                ColumnLayout {
                                    spacing: 2
                                    Text {
                                        text: "苹果公司"
                                        color: "#f1f5f9"
                                        font.pixelSize: 13
                                        font.weight: Font.SemiBold
                                    }
                                    Text {
                                        text: "$182.45 ↗"
                                        color: "#10b981"
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                    }
                                }
                                
                                Item {
                                    width: 12
                                    height: 12
                                    Text { anchors.centerIn: parent; text: "▼"; color: "#64748b" }
                                }
                            }
                        }
                    }
                    
                    // 时间周期选择器
                    RowLayout {
                        spacing: 4
                        
                        Repeater {
                            model: ["1D", "1W", "1M", "3M", "1Y"]
                            
                            Item {
                                width: 40
                                height: 32
                                
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 6
                                    color: index === 0 ? "#3b82f6" : "#1a2235"
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: index === 0 ? "white" : "#94a3b8"
                                        font.pixelSize: 13
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 图表占位符
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    color: "#0a0f1a"
                    
                    // 模拟图表线条
                    Shape {
                        anchors.fill: parent
                        anchors.margins: 20
                        
                        ShapePath {
                            strokeWidth: 3
                            strokeColor: "#3b82f6"
                            fillColor: "transparent"
                            startX: 0; startY: parent.height * 0.7
                            
                            PathCurve { x: parent.width * 0.25; y: parent.height * 0.4 }
                            PathCurve { x: parent.width * 0.5; y: parent.height * 0.5 }
                            PathCurve { x: parent.width * 0.75; y: parent.height * 0.3 }
                            PathCurve { x: parent.width; y: parent.height * 0.6 }
                        }
                    }
                    
                    // 图表网格
                    Repeater {
                        model: 5
                        Rectangle {
                            width: parent.width
                            height: 1
                            color: "#2d374850"
                            y: parent.height * (index / 4)
                        }
                    }
                    
                    Repeater {
                        model: 6
                        Rectangle {
                            width: 1
                            height: parent.height
                            color: "#2d374850"
                            x: parent.width * (index / 5)
                        }
                    }
                    
                    Text {
                        text: "📈 实时行情图表区域"
                        color: "#64748b"
                        font.pixelSize: 16
                        anchors.centerIn: parent
                    }
                }
            }
        }
    }
}
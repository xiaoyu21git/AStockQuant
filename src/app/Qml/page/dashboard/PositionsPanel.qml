import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: positionsPanel
    
    property var positions: []
    
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
                        text: "当前持仓"
                        color: "#f1f5f9"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Text {
                        text: "$245,680.00"
                        color: "#3b82f6"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }
            }
            
            // 持仓列表
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    
                    Repeater {
                        model: positionsPanel.positions.length
                        
                        Item {
                            Layout.fillWidth: true
                            height: 70
                            readonly property var positionData: positionsPanel.positions[index] || ({})
                            
                            Rectangle {
                                anchors.fill: parent
                                radius: 8
                                color: "#1a2235"
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 12
                                    
                                    // 持仓图标
                                    Item {
                                        width: 40
                                        height: 40
                                        
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 10
                                            color: (positionData.color || "#3b82f6") + "20"
                                            
                                            Text {
                                                anchors.centerIn: parent
                                                text: (positionData.symbol || "")[0] || ""
                                                color: positionData.color || "#3b82f6"
                                                font.pixelSize: 16
                                                font.bold: true
                                            }
                                        }
                                    }
                                    
                                    ColumnLayout {
                                        spacing: 2
                                        Text {
                                            text: positionData.symbol || ""
                                            color: "#f1f5f9"
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            text: `${positionData.shares || 0}股 · 均价 $${Number(positionData.avgPrice || 0).toFixed(2)}`
                                            color: "#64748b"
                                            font.pixelSize: 12
                                        }
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    ColumnLayout {
                                        spacing: 2
                                        Layout.alignment: Qt.AlignRight
                                        Text {
                                            text: "$" + Number(positionData.currentValue || 0).toLocaleString(Qt.locale(), 'f', 2)
                                            color: "#f1f5f9"
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            text: `${Number(positionData.pnl || 0) >= 0 ? "+" : "-"}$${Math.abs(Number(positionData.pnl || 0)).toLocaleString(Qt.locale(), 'f', 2)} ${Number(positionData.pnl || 0) >= 0 ? "↗" : "↘"}`
                                            color: Number(positionData.pnl || 0) >= 0 ? "#10b981" : "#ef4444"
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 查看所有持仓按钮
            Item {
                Layout.fillWidth: true
                height: 44
                
                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#3b82f6" }
                        GradientStop { position: 1.0; color: "#1d4ed8" }
                    }
                    
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 8
                        
                        Text {
                            text: "📊"
                            color: "white"
                            font.pixelSize: 14
                        }
                        
                        Text {
                            text: "查看所有持仓"
                            color: "white"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }
                    }
                }
            }
        }
    }
}
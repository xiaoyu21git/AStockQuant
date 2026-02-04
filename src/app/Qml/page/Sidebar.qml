import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: sidebar
    width: 280
    
    // 属性
    property real accountValue: 0
    property real accountChange: 0
    property real accountChangePercent: 0
    
    Rectangle {
        anchors.fill: parent
        color: "#121828"
        border.color: "#2d3748"
        border.width: 1
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            // 导航菜单
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    
                    // 交易菜单标题
                    Text {
                        text: "交易"
                        color: "#64748b"
                        font.pixelSize: 12
                        font.bold: true
                        leftPadding: 20
                        topPadding: 20
                    }
                    
                    // 交易菜单项
                    ColumnLayout {
                        spacing: 2
                        
                        Repeater {
                            model: [
                                {text: "交易台", icon: "chart-line.svg", badge: "实时", active: true},
                                {text: "策略交易", icon: "robot.svg", badge: ""},
                                {text: "策略回测", icon: "history.svg", badge: ""},
                                {text: "数据分析", icon: "chart-bar.svg", badge: ""}
                            ]
                            
                            Item {
                                height: 44
                                Layout.fillWidth: true   // ⭐ 关键
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    radius: 8
                                    color: modelData.active ? "#1a2235" : "transparent"
                                    border.color: modelData.active ? "#3b82f6" : "transparent"
                                    border.width: modelData.active ? 1 : 0
                                    MouseArea{
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            // 这里可以添加导航逻辑
                                            console.log("导航到: 交易 " )
                                        }
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 12
                                        anchors.leftMargin: 20
                                        anchors.rightMargin: 20
                                        
                                        Item {
                                            width: 20
                                            height: 20
                                            Text { anchors.centerIn: parent; text: "📈"; font.pixelSize: 16 }
                                        }
                                        
                                        Text {
                                            text: modelData.text
                                            color: modelData.active ? "#f1f5f9" : "#94a3b8"
                                            font.pixelSize: 14
                                            font.bold: modelData.active
                                        }
                                        
                                        Item { Layout.fillWidth: true }
                                        
                                        Item {
                                            visible: modelData.badge !== ""
                                            width: 36
                                            height: 20
                                            
                                            Rectangle {
                                                anchors.fill: parent
                                                radius: 10
                                                color: modelData.active ? "#3b82f620" : "transparent"
                                                border.color: modelData.active ? "#3b82f6" : "transparent"
                                                border.width: 1
                                                
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: modelData.badge
                                                    color: "#3b82f6"
                                                    font.pixelSize: 11
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // 管理菜单标题
                    Text {
                        text: "管理"
                        color: "#64748b"
                        font.pixelSize: 12
                        font.bold: true
                        leftPadding: 20
                        topPadding: 24
                    }
                    
                    // 管理菜单项
                    ColumnLayout {
                        spacing: 2
                        
                        Repeater {
                            model: [
                                {text: "资金管理", icon: "wallet.svg", badge: ""},
                                {text: "风险管理", icon: "shield-alt.svg", badge: "3"},
                                {text: "数据管理", icon: "database.svg", badge: ""},
                                {text: "系统设置", icon: "cogs.svg", badge: ""}
                            ]
                            
                            Item {
                                height: 44
                                Layout.fillWidth: true   // ⭐ 关键
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    radius: 8
                                    color: "transparent"
                                    MouseArea{
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            // 这里可以添加导航逻辑
                                            console.log("导航到: 管理" )
                                        }
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 12
                                        anchors.leftMargin: 20
                                        anchors.rightMargin: 20
                                        
                                        Item {
                                            width: 20
                                            height: 20
                                            Text { anchors.centerIn: parent; text: "💼"; font.pixelSize: 16 }//未来更新图标的位置
                                        }
                                        
                                        Text {
                                            text: modelData.text
                                            color: "#94a3b8"
                                            font.pixelSize: 14
                                        }
                                        
                                        Item { Layout.fillWidth: true }
                                        
                                        Item {
                                            visible: modelData.badge !== ""
                                            width: 24
                                            height: 20
                                            
                                            Rectangle {
                                                anchors.fill: parent
                                                radius: 10
                                                color: "#f59e0b20"
                                                border.color: "#f59e0b"
                                                border.width: 1
                                                
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: modelData.badge
                                                    color: "#f59e0b"
                                                    font.pixelSize: 11
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    Item { Layout.fillHeight: true }
                    
                    // 账户信息
                    Item {
                        height: 160
                        
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 16
                            anchors.bottomMargin: 0
                            color: "#1a2235"
                            radius: 12
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 8
                                
                                RowLayout {
                                    Text {
                                        text: "账户净值"
                                        color: "#94a3b8"
                                        font.pixelSize: 13
                                        font.bold: true
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    Item {
                                        width: 16
                                        height: 16
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                console.log("导航到:刷新")
                                            }
                                        }
                                        Text { anchors.centerIn: parent; text: "🔄"; font.pixelSize: 14 }
                                    }
                                }
                                
                                Text {
                                    text: "$" + accountValue.toLocaleString(Qt.locale(), 'f', 2)
                                    color: "#f1f5f9"
                                    font.pixelSize: 24
                                    font.bold: true
                                }
                                
                                Text {
                                    text: `↗ +$${accountChange.toLocaleString(Qt.locale(), 'f', 2)} (+${accountChangePercent.toFixed(2)}%)`
                                    color: "#10b981"
                                    font.pixelSize: 13
                                }
                                
                                // 快速操作按钮
                                RowLayout {
                                    spacing: 10
                                    
                                    // 入金按钮
                                    Item {
                                        Layout.fillWidth: true
                                        height: 36
                                        
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 8
                                            color: "#3b82f6"
                                            MouseArea{
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: {
                                                // 这里可以添加导航逻辑
                                                console.log("入金: ")
                                                }
                                            }
                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 6
                                                
                                                Item {
                                                    width: 14
                                                    height: 14
                                                    Text { anchors.centerIn: parent; text: "+"; color: "white"; font.pixelSize: 12 }
                                                }
                                                
                                                Text {
                                                    text: "入金"
                                                    color: "white"
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                }
                                            }
                                        }
                                    }
                                    
                                    // 出金按钮
                                    Item {
                                        Layout.fillWidth: true
                                        height: 36
                                        
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 8
                                            color: "#222c44"
                                            border.color: "#475569"
                                            border.width: 1
                                            MouseArea{
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: {
                                                // 这里可以添加导航逻辑
                                                console.log("出金: ")
                                                }
                                            }
                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: 6
                                                
                                                Item {
                                                    width: 14
                                                    height: 14
                                                    Text { anchors.centerIn: parent; text: "↓"; color: "#f1f5f9"; font.pixelSize: 12 }
                                                }
                                                
                                                Text {
                                                    text: "出金"
                                                    color: "#f1f5f9"
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // 用户信息
                    Item {
                        height: 68
                        
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 16
                            anchors.topMargin: 8
                            color: "#1a2235"
                            radius: 12
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 12
                                
                                Item {
                                    width: 36
                                    height: 36
                                    
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 18
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: "#8b5cf6" }
                                            GradientStop { position: 1.0; color: "#6366f1" }
                                        }
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: "QT"
                                            color: "white"
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                    }
                                }
                                
                                ColumnLayout {
                                    spacing: 2
                                    Text {
                                        text: "量化交易员"
                                        color: "#f1f5f9"
                                        font.pixelSize: 14
                                        font.bold: true
                                    }
                                    Text {
                                        text: "专业版 · 在线"
                                        color: "#64748b"
                                        font.pixelSize: 12
                                    }
                                }
                                
                                Item {
                                    width: 8
                                    height: 8
                                    
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 4
                                        color: "#10b981"
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
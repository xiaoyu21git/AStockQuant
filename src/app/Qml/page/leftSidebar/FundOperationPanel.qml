// FundOperationPanel.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: fundOperationPanel
    height: 160
    
    // 属性
    property real accountValue: 0
    property real accountChange: 0
    property real accountChangePercent: 0
    
    // 信号
    signal depositClicked()
    signal withdrawClicked()
    signal refreshClicked()
    
    function formatCurrency(value) {
        return "$" + value.toLocaleString(Qt.locale(), 'f', 2)
    }
    
    function formatChange(value) {
        return value >= 0 ? 
            `↗ +$${value.toLocaleString(Qt.locale(), 'f', 2)} (+${accountChangePercent.toFixed(2)}%)` :
            `↘ -$${Math.abs(value).toLocaleString(Qt.locale(), 'f', 2)} (${accountChangePercent.toFixed(2)}%)`
    }
    
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
            
            // 标题栏
            RowLayout {
                Text {
                    text: "账户净值"
                    color: "#94a3b8"
                    font.pixelSize: 13
                    font.bold: true
                }
                
                Item { Layout.fillWidth: true }
                
                // 刷新按钮
                Rectangle {
                    width: 28
                    height: 28
                    radius: 6
                    color: "transparent"
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onEntered: parent.color = "#2d3748"
                        onExited: parent.color = "transparent"
                        onClicked: refreshClicked()
                    }
                    
                    Text { 
                        anchors.centerIn: parent
                        text: "🔄"
                        font.pixelSize: 14
                        color: "#94a3b8"
                    }
                }
            }
            
            // 账户价值
            Text {
                text: formatCurrency(accountValue)
                color: "#f1f5f9"
                font.pixelSize: 24
                font.bold: true
            }
            
            // 账户变化
            Text {
                text: formatChange(accountChange)
                color: accountChange >= 0 ? "#10b981" : "#ef4444"
                font.pixelSize: 13
            }
            
            // 操作按钮
            RowLayout {
                spacing: 10
                
                // 入金按钮
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 8
                    color: "#3b82f6"
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onEntered: parent.opacity = 0.9
                        onExited: parent.opacity = 1
                        onClicked: depositClicked()
                    }
                    
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6
                        
                        Text { 
                            text: "+"
                            color: "white"
                            font.pixelSize: 14
                            font.bold: true
                        }
                        
                        Text {
                            text: "入金"
                            color: "white"
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                }
                
                // 出金按钮
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 8
                    color: "#222c44"
                    border.color: "#475569"
                    border.width: 1
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onEntered: parent.opacity = 0.9
                        onExited: parent.opacity = 1
                        onClicked: withdrawClicked()
                    }
                    
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6
                        
                        Text { 
                            text: "↓"
                            color: "#f1f5f9"
                            font.pixelSize: 14
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
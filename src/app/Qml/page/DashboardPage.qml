import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: dashboardPage
    
    header: ToolBar {
        height: 50
        
        RowLayout {
            anchors.fill: parent
            
            ToolButton {
                icon.source: "qrc:/icons/arrow_back.svg"
                onClicked: {
                    if (StackView.view) {
                        StackView.view.pop()
                    }
                }
            }
            
            Label {
                text: "📊 仪表盘"
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
            }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20
        
        Text {
            text: "欢迎使用 AStockQuant"
            font.pixelSize: 24
            font.bold: true
        }
        
        // 仪表盘内容
        GridLayout {
            columns: 2
            columnSpacing: 20
            rowSpacing: 20
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            // 统计卡片
            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#3498db"
                
                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    
                    Text {
                        text: "总交易数"
                        color: "white"
                        font.pixelSize: 14
                    }
                    
                    Text {
                        text: "1,234"
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#2ecc71"
                
                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    
                    Text {
                        text: "今日收益"
                        color: "white"
                        font.pixelSize: 14
                    }
                    
                    Text {
                        text: "+2.34%"
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }
        }
    }
}
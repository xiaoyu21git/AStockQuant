import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AStock.Engine 1.0

ApplicationWindow {
    id: mainWindow
    width: 1200
    height: 800
    visible: true
    title: "AStockQuant Engine v1.0"
    
    // 左侧导航
    Drawer {
        id: drawer
        width: 200
        height: parent.height
        
        ColumnLayout {
            anchors.fill: parent
            
            Button {
                text: "策略回测"
                Layout.fillWidth: true
                onClicked: {
                    stackView.push("qrc:/AStockQuant/BacktestPage.qml")
                    drawer.close()
                }
            }
            
            Button {
                text: "交易记录"
                Layout.fillWidth: true
            }
            
            Button {
                text: "系统设置"
                Layout.fillWidth: true
            }
            
            Item { Layout.fillHeight: true } // 占位
        }
    }
    
    // 主内容区
    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: initialPage
    }
    
    // 初始页面
    Component {
        id: initialPage
        
        Rectangle {
            color: "#f5f5f5"
            
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 20
                
                Image {
                    source: "qrc:/icons/logo.png"
                    sourceSize: Qt.size(200, 200)
                    Layout.alignment: Qt.AlignHCenter
                }
                
                Text {
                    text: "AStockQuant 量化交易引擎"
                    font.pixelSize: 24
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                
                Text {
                    text: "版本 1.0.0"
                    font.pixelSize: 14
                    color: "#666"
                    Layout.alignment: Qt.AlignHCenter
                }
                
                Button {
                    text: "开始使用"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: drawer.open()
                }
            }
        }
    }
    
    // 菜单栏
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            
            ToolButton {
                icon.source: "qrc:/icons/menu.svg"
                onClicked: drawer.open()
            }
            
            Label {
                text: "AStockQuant"
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
            }
            
            ToolButton {
                text: "关于"
                onClicked: aboutDialog.open()
            }
        }
    }
    
    // 关于对话框
    Dialog {
        id: aboutDialog
        title: "关于 AStockQuant"
        modal: true
        standardButtons: Dialog.Ok
        
        ColumnLayout {
            spacing: 10
            
            Label {
                text: "AStockQuant 量化交易引擎"
                font.bold: true
            }
            
            Label {
                text: "版本: 1.0.0"
            }
            
            Label {
                text: "© 2023 AStockQuant Team"
                color: "#666"
            }
        }
    }
}
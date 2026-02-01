import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AStock.Engine 1.0


ApplicationWindow {
    id: mainWindow
    // 默认采用全屏/最大化布局，适配桌面宽屏
    visible: true
    visibility: Window.Maximized
    minimumWidth: 1200
    minimumHeight: 800
    title: "AStockQuant Engine v1.0"

    // 主体布局：左侧固定导航 + 右侧内容区（常见量化软件布局）
    RowLayout {
        id: mainLayout
        anchors.fill: parent
        spacing: 0

        // 左侧导航栏
        Rectangle {
            id: sideBar
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: "#2c3e50"

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                Rectangle {
                    Layout.fillWidth: true
                    height: 60
                    color: "#273746"

                    Text {
                        anchors.centerIn: parent
                        text: "AStockQuant"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 18
                    }
                }

                Item { Layout.preferredHeight: 10 }

                // 仪表盘
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: mouseAreaDashboard.pressed || mouseAreaDashboard.containsMouse ? "#34495e" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16

                        Text {
                            text: "🏠 回测仪表盘"
                            color: "#ecf0f1"
                            font.pixelSize: 14
                        }
                    }

                    MouseArea {
                        id: mouseAreaDashboard
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            stackView.clear()
                            stackView.push("page/DashboardPage.qml")
                        }
                    }
                }

                // 实盘仪表盘
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: mouseAreaLiveDashboard.pressed || mouseAreaLiveDashboard.containsMouse ? "#34495e" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16

                        Text {
                            text: "📡 实盘仪表盘"
                            color: "#ecf0f1"
                            font.pixelSize: 14
                        }
                    }

                    MouseArea {
                        id: mouseAreaLiveDashboard
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            stackView.clear()
                            stackView.push("page/LiveDashboardPage.qml")
                        }
                    }
                }

                // 策略回测
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: mouseAreaBacktest.pressed || mouseAreaBacktest.containsMouse ? "#34495e" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16

                        Text {
                            text: "📊 策略回测"
                            color: "#ecf0f1"
                            font.pixelSize: 14
                        }
                    }

                    MouseArea {
                        id: mouseAreaBacktest
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            stackView.clear()
                            stackView.push("page/BacktestPage.qml")
                        }
                    }
                }

                // 高位股监控
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: mouseAreaHighPos.pressed || mouseAreaHighPos.containsMouse ? "#34495e" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16

                        Text {
                            text: "📈 高位股监控"
                            color: "#ecf0f1"
                            font.pixelSize: 14
                        }
                    }

                    MouseArea {
                        id: mouseAreaHighPos
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            stackView.clear()
                            stackView.push("page/HighPositionPage.qml")
                        }
                    }
                }

                // 交易记录
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: mouseAreaTrade.pressed || mouseAreaTrade.containsMouse ? "#34495e" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16

                        Text {
                            text: "📝 交易记录"
                            color: "#ecf0f1"
                            font.pixelSize: 14
                        }
                    }

                    MouseArea {
                        id: mouseAreaTrade
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            stackView.clear()
                            stackView.push("page/TradeRecordPage.qml")
                        }
                    }
                }

                // 系统设置
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: mouseAreaSettings.pressed || mouseAreaSettings.containsMouse ? "#34495e" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16

                        Text {
                            text: "⚙️ 系统设置"
                            color: "#ecf0f1"
                            font.pixelSize: 14
                        }
                    }

                    MouseArea {
                        id: mouseAreaSettings
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // 预留设置页
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // 帮助/关于
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: mouseAreaHelp.pressed || mouseAreaHelp.containsMouse ? "#34495e" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16

                        Text {
                            text: "❓ 帮助 / 关于"
                            color: "#bdc3c7"
                            font.pixelSize: 13
                        }
                    }

                    MouseArea {
                        id: mouseAreaHelp
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: aboutDialog.open()
                    }
                }
            }
        }

        // 右侧页面栈
        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: initialPage

            // 页面切换动画
            pushEnter: Transition {
                PropertyAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 200
                }
            }

            pushExit: Transition {
                PropertyAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: 200
                }
            }

            popEnter: Transition {
                PropertyAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 200
                }
            }

            popExit: Transition {
                PropertyAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: 200
                }
            }
        }
    }
    
    // 交易记录页面组件
    Component {
        id: tradeRecordPage
        
        Rectangle {
            color: "#f5f5f7"
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                
                // 页面标题栏
                Rectangle {
                    Layout.fillWidth: true
                    height: 60
                    color: "white"
                    
                    // 底部边框
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: "#e0e0e0"
                    }
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        
                        Text {
                            text: "📝 交易记录"
                            font.pixelSize: 18
                            font.bold: true
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 操作按钮
                        Row {
                            spacing: 10
                            
                            Button {
                                text: "刷新"
                                onClicked: {
                                    // 刷新数据逻辑
                                }
                            }
                            
                            Button {
                                text: "导出"
                                onClicked: {
                                    // 导出数据逻辑
                                }
                            }
                        }
                    }
                }
                
                // 表格区域
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "white"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        
                        // 表头
                        Rectangle {
                            Layout.fillWidth: true
                            height: 45
                            color: "#f8f9fa"
                            
                            Row {
                                anchors.fill: parent
                                spacing: 0
                                
                                // 策略列
                                Rectangle {
                                    width: 120
                                    height: parent.height
                                    color: parent.parent.color
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: "策略"
                                        font.bold: true
                                        font.pixelSize: 13
                                        color: "#495057"
                                    }
                                }
                                
                                // 代码列
                                Rectangle {
                                    width: 80
                                    height: parent.height
                                    color: parent.parent.color
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: "代码"
                                        font.bold: true
                                        font.pixelSize: 13
                                        color: "#495057"
                                    }
                                }
                                
                                // 时间列
                                Rectangle {
                                    width: 180
                                    height: parent.height
                                    color: parent.parent.color
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: "时间"
                                        font.bold: true
                                        font.pixelSize: 13
                                        color: "#495057"
                                    }
                                }
                                
                                // 价格列
                                Rectangle {
                                    width: 90
                                    height: parent.height
                                    color: parent.parent.color
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: "价格"
                                        font.bold: true
                                        font.pixelSize: 13
                                        color: "#495057"
                                    }
                                }
                                
                                // 方向列
                                Rectangle {
                                    width: 70
                                    height: parent.height
                                    color: parent.parent.color
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: "方向"
                                        font.bold: true
                                        font.pixelSize: 13
                                        color: "#495057"
                                    }
                                }
                            }
                            
                            // 底部边框
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: "#dee2e6"
                            }
                        }
                        
                        // 表格内容 - 使用 ListView 替代 TableView（更简单）
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: tradeRecordModel
                            clip: true
                            
                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 48
                                color: index % 2 === 0 ? "#ffffff" : "#f8f9fa"
                                
                                Row {
                                    anchors.fill: parent
                                    spacing: 0
                                    
                                    // 策略单元格
                                    Rectangle {
                                        width: 120
                                        height: parent.height
                                        color: parent.parent.color
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: model.strategy
                                            font.pixelSize: 13
                                            color: "#2c3e50"
                                        }
                                    }
                                    
                                    // 代码单元格
                                    Rectangle {
                                        width: 80
                                        height: parent.height
                                        color: parent.parent.color
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: model.symbol
                                            font.pixelSize: 13
                                            color: "#2c3e50"
                                        }
                                    }
                                    
                                    // 时间单元格
                                    Rectangle {
                                        width: 180
                                        height: parent.height
                                        color: parent.parent.color
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: model.time
                                            font.pixelSize: 13
                                            color: "#2c3e50"
                                        }
                                    }
                                    
                                    // 价格单元格
                                    Rectangle {
                                        width: 90
                                        height: parent.height
                                        color: parent.parent.color
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: model.price ? model.price.toFixed(2) : "0.00"
                                            font.pixelSize: 13
                                            color: "#2c3e50"
                                        }
                                    }
                                    
                                    // 方向单元格
                                    Rectangle {
                                        width: 70
                                        height: parent.height
                                        color: parent.parent.color
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: model.isBuy ? "买入" : "卖出"
                                            font.pixelSize: 13
                                            color: model.isBuy ? "#27ae60" : "#e74c3c"
                                        }
                                    }
                                }
                                
                                // 底部边框
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: "#dee2e6"
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 初始页面
    Component {
        id: initialPage
        
        Rectangle {
            color: "#f5f5f7"
            
            Column {
                anchors.centerIn: parent
                spacing: 30
                
                // Logo区域
                Column {
                    spacing: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    
                    Rectangle {
                        width: 120
                        height: 120
                        radius: 60
                        color: "#3498db"
                        anchors.horizontalCenter: parent.horizontalCenter
                        
                        Text {
                            anchors.centerIn: parent
                            text: "AQ"
                            color: "white"
                            font.pixelSize: 36
                            font.bold: true
                        }
                }
                
                Text {
                        text: "AStockQuant"
                        anchors.horizontalCenter: parent.horizontalCenter
                        font.pixelSize: 32
                    font.bold: true
                        color: "#2c3e50"
                }
                
                Text {
                        text: "量化交易引擎 v1.0.0"
                        anchors.horizontalCenter: parent.horizontalCenter
                        font.pixelSize: 16
                        color: "#7f8c8d"
                    }
                }
                
                // 快速开始按钮：直接进入策略回测
                Button {
                    text: "🚀 快速开始"
                    font.pixelSize: 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: {
                        stackView.clear()
                        stackView.push("page/BacktestPage.qml")
                    }

                    background: Rectangle {
                        radius: 8
                        color: "#3498db"
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
    
    // 菜单栏
    header: ToolBar {
        height: 50
        background: Rectangle {
            color: "white"

            // 底部边框
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: "#e0e0e0"
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8

            Label {
                text: "AStockQuant 量化终端"
                font.pixelSize: 18
                font.bold: true
                color: "#2c3e50"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: "环境: 回测 / 研究"
                color: "#7f8c8d"
                font.pixelSize: 12
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
        anchors.centerIn: parent
        width: 400
        
        Column {
            spacing: 15
            width: parent.width
            
            Row {
                spacing: 15
                
                Rectangle {
                    width: 60
                    height: 60
                    radius: 30
                    color: "#3498db"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "AQ"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 20
                    }
                }
                
                Column {
                    spacing: 5
                    
                    Text {
                text: "AStockQuant 量化交易引擎"
                font.bold: true
                        font.pixelSize: 16
            }
            
                    Text {
                text: "版本: 1.0.0"
                        color: "#666"
                    }
                }
            }
            
            Text {
                text: "一个专业、高效的量化交易平台，支持策略回测、实时交易和风险管理。"
                width: parent.width
                wrapMode: Text.WordWrap
            }
            
            Rectangle {
                width: parent.width
                height: 1
                color: "#e0e0e0"
            }
            
            Text {
                text: "© 2023 AStockQuant Team. 保留所有权利。"
                color: "#999"
                font.pixelSize: 12
            }
        }
    }
}
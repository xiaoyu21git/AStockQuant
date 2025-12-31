// resources/qml/main.qml
import QtQuick 
import QtQuick.Controls 
import QtQuick.Layouts 

ApplicationWindow {
    id: mainWindow
    title: "股票量化分析系统"
    width: 1280
    height: 800
    
    // 菜单栏
    menuBar: MenuBar {
        Menu {
            title: "文件"
            Action { text: "打开"; onTriggered: fileManager.open() }
            Action { text: "保存"; onTriggered: fileManager.save() }
            MenuSeparator {}
            Action { text: "退出"; onTriggered: Qt.quit() }
        }
        
        Menu {
            title: "视图"
            Action { text: "K线图"; checkable: true; checked: true }
            Action { text: "分时图" }
            Action { text: "自选股" }
        }
    }
    
    // 主布局
    SplitView {
        anchors.fill: parent
        
        // // 左侧：股票列表
        // StockListView {
        //     id: stockList
        //     width: 200
        //     onStockSelected: chartView.loadStock(symbol)
        // }
        
        // 中间：图表区域
        // ChartView {
        //     id: chartView
        //     Layout.fillWidth: true
        // }
        
        // 右侧：信息面板
        // InfoPanel {
        //     id: infoPanel
        //     width: 300
        // }
    }
    
    // 状态栏
    // statusBar: StatusBar {
    //     Label { text: "就绪" }
    //     Label { text: "数据时间: " + dataProvider.lastUpdateTime }
    // }
}
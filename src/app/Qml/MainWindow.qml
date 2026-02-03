import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

ApplicationWindow {
    id: mainWindow
    width: 1280
    height: 800
    visible: true
    title: "量化引擎主界面"

    // 主题切换（可扩展）
    property bool darkTheme: false

    // 当前选中页面索引
    property int currentPage: 0
    
    // 侧边栏导航项
    property var navItems: [
        { icon: "user", label: "账户" },
        { icon: "strategy", label: "策略" },
        { icon: "settings", label: "参数" },
        { icon: "database", label: "数据源" },
        { icon: "history", label: "历史" },
        { icon: "chart", label: "折线图" },
        { icon: "log", label: "日志" },
        { icon: "risk", label: "风控" },
        { icon: "notification", label: "通知" },
        { icon: "advanced", label: "高级设置" }
    ]

    // 页面路径数组，供 Loader 使用
    property var pageSources: [
        "qrc:/Qml/page/AccountPage.qml",
        "qrc:/Qml/page/StrategyPage.qml",
        "qrc:/Qml/page/SettingsPage.qml",
        "qrc:/Qml/page/DataSourcePage.qml",
        "qrc:/Qml/page/HistoryPage.qml",
        "qrc:/Qml/page/ChartPage.qml",
        "qrc:/Qml/page/LogPage.qml",
        "qrc:/Qml/page/RiskPage.qml",
        "qrc:/Qml/page/NotificationPage.qml",
        "qrc:/Qml/page/AdvancedSettingsPage.qml"
    ]

    // 主布局
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 侧边栏
        Rectangle {
            width: 180
            color: darkTheme ? "#232323" : "#f5f5f5"
            Layout.fillHeight: true
            Column {
                anchors.fill: parent
                spacing: 8
                // Logo/标题
                Rectangle {
                    height: 64
                    width: parent.width
                    color: "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "AStockQuant"
                        font.bold: true
                        font.pixelSize: 22
                        color: darkTheme ? "#fff" : "#222"
                    }
                }
                // 导航按钮
                Repeater {
                    model: navItems
                    delegate: Rectangle {
                        width: parent.width - 16
                        height: 44
                        radius: 12
                        color: index === currentPage ? (darkTheme ? "#1976d2" : "#e3f2fd") : "transparent"
                        border.color: index === currentPage ? "#1976d2" : "#bdbdbd"
                        border.width: index === currentPage ? 2 : 1
                        anchors.horizontalCenter: parent.horizontalCenter
                        RowLayout {
                            anchors.fill: parent
                            spacing: 10
                            Image {
                                source: "qrc:/icons/" + model.icon + ".svg"
                                width: 24; height: 24
                                fillMode: Image.PreserveAspectFit
                            }
                            Text {
                                text: model.label
                                color: index === currentPage ? "#1976d2" : (darkTheme ? "#fff" : "#222")
                                font.pixelSize: 18
                                font.bold: index === currentPage
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: currentPage = index
                            onEntered: parent.color = index === currentPage ? parent.color : (darkTheme ? "#2c3e50" : "#e0e0e0")
                            onExited: parent.color = index === currentPage ? parent.color : "transparent"
                        }
                        Behavior on color { ColorAnimation { duration: 180 } }
                    }
                }
                // 主题切换
                Rectangle {
                    height: 48
                    width: parent.width
                    color: "transparent"
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        Text { text: darkTheme ? "暗色" : "亮色"; color: darkTheme ? "#fff" : "#222" }
                        Switch {
                            checked: darkTheme
                            onCheckedChanged: darkTheme = checked
                        }
                    }
                }
            }
        }

        // 内容区
        Rectangle {
            color: darkTheme ? "#181818" : "#fff"
            Layout.fillWidth: true
            Layout.fillHeight: true
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.left: undefined
            
            Loader {
                id: pageLoader
                anchors.fill: parent
                source: pageSources[currentPage]
                onLoaded: {
                    if (pageLoader.item) {
                        pageLoader.item.opacity = 0;
                        pageLoader.item.opacity = 1;
                    }
                }
            }
        }
    }

    // 统一消息提示框（可扩展为全局Snackbar/Dialog）
    Snackbar {
        id: globalSnackbar
        anchors.horizontalCenter: parent.horizontalCenter
        text: "操作成功！"
        visible: false
    }
}

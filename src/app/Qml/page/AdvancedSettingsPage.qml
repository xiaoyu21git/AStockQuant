import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "qrc:/Qml/component/GlobalSnackbar.qml" as GlobalSnackbar
import "qrc:/Qml/component/ThemeSwitcher.qml" as ThemeSwitcher

Item {
    id: advancedSettingsPage
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32
        Rectangle {
            color: "#e3f2fd"
            radius: 16
            border.color: "#1976d2"
            border.width: 2
            Layout.fillWidth: true
            height: 56
            Text {
                anchors.centerIn: parent
                text: "高级设置"
                font.pixelSize: 28
                font.bold: true
                color: "#1976d2"
            }
        }

        // 示例静态模型，便于布局调试
        ListModel {
            id: systemParamsModel
            ListElement { label: "线程数"; value: "8"; key: "threadCount" }
            ListElement { label: "缓存大小"; value: "1024MB"; key: "cacheSize" }
        }
        ListModel {
            id: apiConfigsModel
            ListElement { label: "行情接口"; value: "MyQuant"; key: "marketApi" }
            ListElement { label: "交易接口"; value: "BrokerX"; key: "tradeApi" }
        }

        // 系统参数区
        GroupBox {
            title: "系统参数"
            Layout.fillWidth: true
            FormLayout {
                anchors.fill: parent
                Repeater {
                    model: systemParamsModel
                    RowLayout {
                        spacing: 16
                        Label { text: model.label; font.pixelSize: 18; color: Qt.application.palette.text }
                        TextField {
                            text: model.value
                            font.pixelSize: 18
                            color: Qt.application.palette.text
                        }
                    }
                }
            }
        }

        // 接口配置区
        GroupBox {
            title: "接口配置"
            Layout.fillWidth: true
            FormLayout {
                anchors.fill: parent
                Repeater {
                    model: apiConfigsModel
                    RowLayout {
                        spacing: 16
                        Label { text: model.label; font.pixelSize: 18; color: Qt.application.palette.text }
                        TextField {
                            text: model.value
                            font.pixelSize: 18
                            color: Qt.application.palette.text
                        }
                    }
                }
            }
        }

        // 日志级别区
        GroupBox {
            title: "日志级别"
            Layout.fillWidth: true
            RowLayout {
                spacing: 24
                Label { text: "当前级别："; font.pixelSize: 18; color: Qt.application.palette.text }
                ComboBox {
                    id: logLevelCombo
                    model: ["DEBUG", "INFO", "WARN", "ERROR"]
                    currentIndex: advancedModel.logLevelIndex
                    onCurrentIndexChanged: advancedModel.setLogLevel(currentIndex)
                    font.pixelSize: 18
                }
            }
        }

        // 操作按钮区
        RowLayout {
            spacing: 16
            Button {
                text: "保存全部"
                onClicked: {
                    advancedModel.saveAll()
                    globalSnackbar.show("高级设置已保存", "success")
                }
            }
            Button {
                text: "重置"
                onClicked: {
                    advancedModel.resetAll()
                    globalSnackbar.show("高级设置已重置", "info")
                }
            }
        }

        // 页面顶部添加主题切换组件
        RowLayout {
            spacing: 16
            ThemeSwitcher {
                id: themeSwitcher
            }
        }

        // 页面底部挂载全局消息弹窗
        GlobalSnackbar {
            id: globalSnackbar
        }
    }
}

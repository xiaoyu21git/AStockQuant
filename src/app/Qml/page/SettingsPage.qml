import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Engine 1.0
import "qrc:/Qml/component/GlobalSnackbar.qml" as GlobalSnackbar
import "qrc:/Qml/component/ThemeSwitcher.qml" as ThemeSwitcher

Page {
    id: settingsPage
    Flickable {
        anchors.fill: parent
        contentWidth: parent.width
        contentHeight: card.height + 32
        clip: true
        Rectangle {
            id: card
            width: Math.min(parent.width, 520)
            height: column.height + 32
            radius: 18
            color: "#fafdff"
            border.color: "#d0d7e2"
            border.width: 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 24
            ColumnLayout {
                id: column
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 24
                spacing: 24

                // 主题切换
                RowLayout {
                    spacing: 16
                    ThemeSwitcher { id: themeSwitcher }
                }

                // 标题
                RowLayout {
                    spacing: 10
                    Image { source: "qrc:/icons/joinquant.png"; width: 32; height: 32 }
                    Label { text: "聚宽账号配置与控制"; font.pixelSize: 22; font.bold: true; color: "#2980b9" }
                }
                Rectangle { color: "#eaf2fb"; height: 1; Layout.fillWidth: true; radius: 1 }

                // 账号输入
                RowLayout {
                    spacing: 12
                    Label { text: "账号："; font.pixelSize: 17; color: "#34495e" }
                    TextField {
                        id: jqUsernameField
                        placeholderText: "请输入聚宽账号"
                        onTextChanged: AStock.Engine.GlobalState.jqUsername = text
                        Layout.preferredWidth: 220
                        font.pixelSize: 16
                    }
                }
                RowLayout {
                    spacing: 12
                    Label { text: "密码："; font.pixelSize: 17; color: "#34495e" }
                    TextField {
                        id: jqPasswordField
                        placeholderText: "请输入聚宽密码"
                        echoMode: TextInput.Password
                        onTextChanged: AStock.Engine.GlobalState.jqPassword = text
                        Layout.preferredWidth: 220
                        font.pixelSize: 16
                    }
                }

                // 操作按钮与状态
                RowLayout {
                    spacing: 18
                    Button {
                        id: jqConnectBtn
                        text: "连接聚宽"
                        onClicked: {
                            AStock.Engine.GlobalState.jqConnecting = true
                            // TODO: 调用后端聚宽登录接口，成功后设置 jqConnected=true
                        }
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 38
                        font.bold: true
                        background: Rectangle {
                            color: jqConnectBtn.pressed ? (AStock.Engine.GlobalState.jqConnected ? "#27ae60" : "#2980b9") : "#bdc3c7"
                            radius: 8
                        }
                        contentItem: Text {
                            anchors.centerIn: parent
                            text: jqConnectBtn.pressed ? (AStock.Engine.GlobalState.jqConnected ? "已连接" : "连接聚宽") : "连接中..."
                            color: jqConnectBtn.pressed ? "white" : "#888"
                            font.pixelSize: 16
                            font.bold: true
                        }
                    }
                    Button {
                        id: jqLogoutBtn
                        text: "登出"
                        onClicked: {
                            // TODO: 调用后端聚宽登出接口，成功后设置 jqConnected=false
                        }
                        Layout.preferredWidth: 80
                        Layout.preferredHeight: 38
                        font.bold: true
                        background: Rectangle {
                            color: jqLogoutBtn.enabled ? "#e74c3c" : "#bdc3c7"
                            radius: 8
                        }
                    }
                    Label {
                        text: AStock.Engine.GlobalState.jqConnected ? "状态：已连接" : (AStock.Engine.GlobalState.jqConnecting ? "状态：连接中..." : "状态：未连接")
                        color: AStock.Engine.GlobalState.jqConnected ? "#27ae60" : (AStock.Engine.GlobalState.jqConnecting ? "#f39c12" : "#c0392b")
                        font.pixelSize: 16
                        font.bold: true
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                // 自动保存提示
                Rectangle {
                    color: "#f6f8fa"; radius: 6; height: 32; Layout.fillWidth: true; border.color: "#d0d7e2"; border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "账号和密码修改后会自动保存，连接状态实时刷新。"
                        color: "#888"
                        font.pixelSize: 14
                    }
                }

                // 参数设置区
                Text {
                    text: "参数设置"
                    font.pixelSize: 28
                    font.bold: true
                    color: Qt.application.palette.text
                    Layout.alignment: Qt.AlignHCenter
                }
                GroupBox {
                    title: "策略参数"
                    Layout.fillWidth: true
                    FormLayout {
                        anchors.fill: parent
                        Repeater {
                            model: configModel.list
                            RowLayout {
                                spacing: 16
                                Label { text: model.label; font.pixelSize: 18; color: Qt.application.palette.text }
                                TextField {
                                    text: model.value
                                    onEditingFinished: configModel.set(model.key, text)
                                    font.pixelSize: 18
                                    color: Qt.application.palette.text
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    spacing: 16
                    Button {
                        text: "保存"
                        onClicked: {
                            configModel.save()
                            globalSnackbar.show("参数保存成功", "success")
                        }
                    }
                    Button {
                        text: "重置"
                        onClicked: {
                            configModel.reset()
                            globalSnackbar.show("参数已重置", "info")
                        }
                    }
                    Button {
                        text: "批量导入"
                        onClicked: {
                            configModel.importBatch()
                            globalSnackbar.show("批量导入完成", "success")
                        }
                    }
                    Button {
                        text: "批量导出"
                        onClicked: {
                            configModel.exportBatch()
                            globalSnackbar.show("批量导出完成", "success")
                        }
                    }
                }
            }
        }
    }
    // 页面底部挂载全局消息弹窗
    GlobalSnackbar { id: globalSnackbar }
}
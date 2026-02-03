import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: accountPage
    anchors.fill: parent
    
    // 响应式布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32
        
        // 标题
        Text {
            text: "账户信息"
            font.pixelSize: 28
            font.bold: true
            color: Qt.application.palette.text
            Layout.alignment: Qt.AlignHCenter
        }
        
        // 账户卡片
        Rectangle {
            Layout.fillWidth: true
            radius: 12
            color: Qt.application.palette.window
            border.color: Qt.application.palette.mid
            border.width: 1
            height: 160
            RowLayout {
                anchors.fill: parent
                spacing: 32
                
                // 用户头像
                Rectangle {
                    width: 80; height: 80
                    radius: 40
                    color: "#e0e0e0"
                    Image {
                        anchors.centerIn: parent
                        source: userModel.avatar
                        width: 64; height: 64
                        fillMode: Image.PreserveAspectFit
                    }
                }
                // 账户信息
                ColumnLayout {
                    spacing: 12
                    Text { text: "用户名：" + userModel.username; font.pixelSize: 20; color: Qt.application.palette.text }
                    Text { text: "资金：" + userModel.balance; font.pixelSize: 20; color: Qt.application.palette.text }
                    Text { text: "状态：" + userModel.status; font.pixelSize: 18; color: userModel.status === "正常" ? "#4caf50" : "#f44336" }
                }
                // 操作按钮
                ColumnLayout {
                    spacing: 16
                    Button { text: "切换账户"; onClicked: userModel.switchAccount() }
                    Button { text: "登出"; onClicked: userModel.logout() }
                }
            }
        }
        
        // 账户明细区
        GroupBox {
            title: "账户明细"
            Layout.fillWidth: true
            ListView {
                anchors.fill: parent
                model: userModel.details
                delegate: Row {
                    spacing: 24
                    Text { text: model.key; font.pixelSize: 16; color: Qt.application.palette.text }
                    Text { text: model.value; font.pixelSize: 16; color: Qt.application.palette.text }
                }
            }
        }
    }
}

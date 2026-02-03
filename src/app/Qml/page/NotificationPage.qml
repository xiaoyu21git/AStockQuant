import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: notificationPage
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32

        // 标题
        Rectangle {
            color: "#e3f2fd"
            radius: 16
            border.color: "#1976d2"
            border.width: 2
            Layout.fillWidth: true
            height: 56
            Text {
                anchors.centerIn: parent
                text: "通知中心"
                font.pixelSize: 28
                font.bold: true
                color: "#1976d2"
            }
        }

        // 示例静态模型，便于布局调试
        ListModel {
            id: notificationListModel
            ListElement { title: "系统更新"; time: "2026-02-03 09:00"; read: false; index: 0 }
            ListElement { title: "策略回测完成"; time: "2026-02-03 10:15"; read: true; index: 1 }
        }

        // 通知列表区
        GroupBox {
            title: "系统通知"
            Layout.fillWidth: true
            ListView {
                anchors.fill: parent
                model: notificationListModel
                delegate: Rectangle {
                    width: parent.width
                    height: 56
                    color: model.read ? "#f5f5f5" : "#e3f2fd"
                    border.color: "#bdbdbd"
                    border.width: 1
                    radius: 8
                    RowLayout {
                        anchors.fill: parent
                        spacing: 16
                        Text { text: model.title; font.pixelSize: 18; color: Qt.application.palette.text }
                        Text { text: model.time; font.pixelSize: 16; color: "#888" }
                        Button { text: model.read ? "已读" : "标为已读"; enabled: !model.read }
                        Button { text: "删除" }
                    }
                }
            }
        }

        // 操作按钮区
        RowLayout {
            spacing: 16
            Button { text: "全部标为已读"; onClicked: notificationModel.markAllRead() }
            Button { text: "刷新"; onClicked: notificationModel.refresh() }
        }
    }
}

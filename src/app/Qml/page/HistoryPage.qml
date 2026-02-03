import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: historyPage
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32

        // 标题
        Text {
            text: "历史/快照管理"
            font.pixelSize: 28
            font.bold: true
            color: Qt.application.palette.text
            Layout.alignment: Qt.AlignHCenter
        }

        // 快照列表区
        GroupBox {
            title: "快照列表"
            Layout.fillWidth: true
            ListView {
                anchors.fill: parent
                model: historyModel.snapshots
                delegate: Rectangle {
                    width: parent.width
                    height: 56
                    color: model.selected ? "#e3f2fd" : "#fff"
                    border.color: "#bdbdbd"
                    border.width: 1
                    radius: 8
                    RowLayout {
                        anchors.fill: parent
                        spacing: 16
                        Text { text: model.name; font.pixelSize: 18; color: Qt.application.palette.text }
                        Text { text: model.time; font.pixelSize: 16; color: "#888" }
                        Button { text: "回滚"; onClicked: historyModel.rollback(model.index) }
                        Button { text: "删除"; onClicked: historyModel.remove(model.index) }
                    }
                }
            }
        }

        // 快照操作区
        RowLayout {
            spacing: 16
            Button { text: "新建快照"; onClicked: historyModel.createSnapshot() }
            Button { text: "刷新"; onClicked: historyModel.refresh() }
        }

        // 变更历史区
        GroupBox {
            title: "变更历史"
            Layout.fillWidth: true
            ListView {
                anchors.fill: parent
                model: historyModel.changes
                delegate: Row {
                    spacing: 24
                    Text { text: model.time; font.pixelSize: 16; color: "#888" }
                    Text { text: model.desc; font.pixelSize: 16; color: Qt.application.palette.text }
                }
            }
        }
    }
}

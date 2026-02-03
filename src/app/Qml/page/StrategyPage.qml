import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: strategyPage
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32

        // 标题
        Text {
            text: "策略管理"
            font.pixelSize: 28
            font.bold: true
            color: Qt.application.palette.text
            Layout.alignment: Qt.AlignHCenter
        }

        // 策略列表区
        GroupBox {
            title: "策略列表"
            Layout.fillWidth: true
            ListView {
                anchors.fill: parent
                model: strategyModel.list
                delegate: Rectangle {
                    width: parent.width
                    height: 64
                    color: model.running ? "#e8f5e9" : "#fff"
                    border.color: "#bdbdbd"
                    border.width: 1
                    radius: 8
                    RowLayout {
                        anchors.fill: parent
                        spacing: 16
                        Text { text: model.name; font.pixelSize: 20; color: Qt.application.palette.text }
                        Text { text: "状态：" + (model.running ? "运行中" : "已停止"); color: model.running ? "#4caf50" : "#f44336"; font.pixelSize: 16 }
                        Text { text: "收益：" + model.pnl; font.pixelSize: 16; color: Qt.application.palette.text }
                        Button { text: model.running ? "停止" : "启动"; onClicked: strategyModel.toggle(model.index) }
                        Button { text: "编辑"; onClicked: strategyModel.edit(model.index) }
                        Button { text: "删除"; onClicked: strategyModel.remove(model.index) }
                    }
                }
            }
        }

        // 新增策略入口
        RowLayout {
            spacing: 16
            Button { text: "新增策略"; onClicked: strategyModel.add() }
            Button { text: "批量导入"; onClicked: strategyModel.importBatch() }
        }
    }
}

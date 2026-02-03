import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: logPage
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32

        // 标题
        Text {
            text: "日志区"
            font.pixelSize: 28
            font.bold: true
            color: Qt.application.palette.text
            Layout.alignment: Qt.AlignHCenter
        }

        // 日志操作区
        RowLayout {
            spacing: 16
            Button { text: "刷新日志"; onClicked: logModel.refresh() }
            Button { text: "导出日志"; onClicked: logModel.export() }
            Button { text: "清空日志"; onClicked: logModel.clear() }
        }

        // 日志列表区
        GroupBox {
            title: "系统日志"
            Layout.fillWidth: true
            Layout.preferredHeight: 400
            ListView {
                anchors.fill: parent
                model: logModel.entries
                clip: true
                delegate: Rectangle {
                    width: parent.width
                    height: 32
                    color: model.level === "ERROR" ? "#ffebee" : (model.level === "WARN" ? "#fffde7" : "#e3f2fd")
                    border.color: "#bdbdbd"
                    border.width: 1
                    radius: 6
                    RowLayout {
                        anchors.fill: parent
                        spacing: 12
                        Text { text: model.time; font.pixelSize: 14; color: "#888" }
                        Text { text: model.level; font.pixelSize: 14; color: model.level === "ERROR" ? "#d32f2f" : (model.level === "WARN" ? "#fbc02d" : "#1976d2") }
                        Text { text: model.message; font.pixelSize: 14; color: Qt.application.palette.text; elide: Text.ElideRight }
                    }
                }
            }
        }
    }
}

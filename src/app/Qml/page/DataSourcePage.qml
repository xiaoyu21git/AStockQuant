import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: dataSourcePage
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32

        // 标题
        Text {
            text: "数据源管理"
            font.pixelSize: 28
            font.bold: true
            color: Qt.application.palette.text
            Layout.alignment: Qt.AlignHCenter
        }

        // 数据源选择区
        GroupBox {
            title: "数据源选择"
            Layout.fillWidth: true
            RowLayout {
                spacing: 16
                ComboBox {
                    model: dataSourceModel.list
                    currentIndex: dataSourceModel.currentIndex
                    onCurrentIndexChanged: dataSourceModel.select(currentIndex)
                    Layout.preferredWidth: 220
                }
                Text { text: "状态：" + dataSourceModel.status; font.pixelSize: 18; color: dataSourceModel.status === "已连接" ? "#4caf50" : "#f44336" }
                Button { text: "刷新"; onClicked: dataSourceModel.refresh() }
            }
        }

        // 数据源明细区
        GroupBox {
            title: "数据源明细"
            Layout.fillWidth: true
            ListView {
                anchors.fill: parent
                model: dataSourceModel.details
                delegate: Row {
                    spacing: 24
                    Text { text: model.key; font.pixelSize: 16; color: Qt.application.palette.text }
                    Text { text: model.value; font.pixelSize: 16; color: Qt.application.palette.text }
                }
            }
        }
    }
}

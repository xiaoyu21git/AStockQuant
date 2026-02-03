import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: riskPage
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
                text: "风控管理"
                font.pixelSize: 28
                font.bold: true
                color: "#1976d2"
            }
        }

        // 示例静态模型，便于布局调试
        ListModel {
            id: riskParamModel
            ListElement { label: "最大持仓"; value: "10"; key: "maxPosition" }
            ListElement { label: "单股最大仓位"; value: "3"; key: "maxSingleStock" }
        }
        ListModel {
            id: riskRuleModel
            ListElement { name: "止损"; desc: "亏损超过5%自动卖出"; index: 0 }
            ListElement { name: "止盈"; desc: "盈利超过10%自动卖出"; index: 1 }
        }

        // 风控参数区
        GroupBox {
            title: "风控参数设置"
            Layout.fillWidth: true
            FormLayout {
                anchors.fill: parent
                Repeater {
                    model: riskParamModel
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

        // 风控规则区
        GroupBox {
            title: "风控规则"
            Layout.fillWidth: true
            ListView {
                anchors.fill: parent
                model: riskRuleModel
                delegate: Rectangle {
                    width: parent.width
                    height: 48
                    color: "#f5f5f5"
                    border.color: "#bdbdbd"
                    border.width: 1
                    radius: 8
                    RowLayout {
                        anchors.fill: parent
                        spacing: 16
                        Text { text: model.name; font.pixelSize: 18; color: Qt.application.palette.text }
                        Text { text: model.desc; font.pixelSize: 16; color: "#888" }
                        Button { text: "启用" }
                        Button { text: "禁用" }
                    }
                }
            }
        }

        // 操作按钮区
        RowLayout {
            spacing: 16
            Button { text: "保存参数"; onClicked: riskModel.saveParams() }
            Button { text: "刷新规则"; onClicked: riskModel.refreshRules() }
        }
    }
}

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Item {
    id: chartPage
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32

        // 标题
        Text {
            text: "折线图分析"
            font.pixelSize: 28
            font.bold: true
            color: Qt.application.palette.text
            Layout.alignment: Qt.AlignHCenter
        }

        // 图表区
        GroupBox {
            title: "历史行情折线图"
            Layout.fillWidth: true
            Layout.preferredHeight: 400
            ChartView {
                anchors.fill: parent
                antialiasing: true
                legend.visible: true
                ValueAxis { id: axisX; min: 0; max: 100; titleText: "时间" }
                ValueAxis { id: axisY; min: 0; max: 100; titleText: "价格" }
                LineSeries {
                    name: "主线"
                    axisX: axisX
                    axisY: axisY
                    pointsVisible: true
                    color: "#1976d2"
                    // 示例数据，后续可绑定数据模型
                    XYPoint { x: 0; y: 10 }
                    XYPoint { x: 10; y: 30 }
                    XYPoint { x: 20; y: 25 }
                    XYPoint { x: 30; y: 40 }
                    XYPoint { x: 40; y: 35 }
                    XYPoint { x: 50; y: 50 }
                }
            }
        }

        // 图表操作区
        RowLayout {
            spacing: 16
            Button { text: "刷新数据"; onClicked: chartModel.refresh() }
            Button { text: "导出图片"; onClicked: chartModel.exportImage() }
        }

        // 明细区
        GroupBox {
            title: "数据明细"
            Layout.fillWidth: true
            ListView {
                anchors.fill: parent
                model: chartModel.details
                delegate: Row {
                    spacing: 24
                    Text { text: model.time; font.pixelSize: 16; color: "#888" }
                    Text { text: model.value; font.pixelSize: 16; color: Qt.application.palette.text }
                }
            }
        }
    }
}

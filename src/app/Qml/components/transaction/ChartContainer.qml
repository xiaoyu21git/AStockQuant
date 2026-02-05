// ChartContainer.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: chartContainer
    radius: 12
    color: "#1e293b"
    border.color: "#334155"
    border.width: 1
    
    property string title: ""
    property string chartType: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 25
        spacing: 20

        // 图表标题行
        RowLayout {
            Layout.fillWidth: true

            Row {
                spacing: 10
                Layout.alignment: Qt.AlignLeft

                Text {
                    text: {
                        if (title.includes("资产")) return "👤";
                        else if (title.includes("关注")) return "❤️";
                        else if (title.includes("时段")) return "⏰";
                        else if (title.includes("权重")) return "⚖️";
                        else if (title.includes("预警")) return "📜";
                        else if (title.includes("月度")) return "📅";
                        else if (title.includes("行业")) return "🏭";
                        else if (title.includes("最佳")) return "🏆";
                        else return "📊";
                    }
                    color: "#e2e8f0"
                    font.pointSize: 16
                }

                Text {
                    text: chartContainer.title
                    color: "#e2e8f0"
                    font.pointSize: 18
                    font.bold: true
                }
            }

            Row {
                spacing: 10
                Layout.alignment: Qt.AlignRight

                // 时间选择器
                Rectangle {
                    width: 120
                    height: 36
                    radius: 8
                    color: "#0f172a"
                    border.color: "#334155"
                    border.width: 1

                    Text {
                        text: {
                            if (title.includes("资产")) return "近6个月";
                            else if (title.includes("预警")) return "最近30天";
                            else return "选择时间";
                        }
                        color: "#94a3b8"
                        font.pointSize: 14
                        anchors.centerIn: parent
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: console.log("选择时间范围")
                    }
                }

                // 编辑按钮
                Rectangle {
                    visible: title.includes("关注")
                    width: 80
                    height: 36
                    radius: 8
                    color: "#475569"
                    border.color: "#64748b"
                    border.width: 1

                    Text {
                        text: "编辑"
                        color: "#e2e8f0"
                        font.pointSize: 14
                        anchors.centerIn: parent
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: console.log("编辑图表")
                    }
                }
            }
        }

        // 图表内容区域
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            // 这里放置具体的ChartView
            children: chartContainer.children.length > 2 ? chartContainer.children[2] : null
        }
    }
}
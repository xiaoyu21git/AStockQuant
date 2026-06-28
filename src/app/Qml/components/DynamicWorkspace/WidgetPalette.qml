import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Popup {
    id: root

    // ============ 属性 ============
    property var registry: null

    // ============ 信号 ============
    signal widgetSelected(string typeName)

    // ============ 弹窗配置 ============
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 480
    height: 360
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 0

    background: Rectangle {
        color: "#1E293B"
        border.color: "#475569"
        border.width: 1
        radius: 12
    }

    // ============ 内容 ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // 标题
        Text {
            text: "选择控件类型"
            color: "#F1F5F9"
            font.pixelSize: 16
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        // 控件类型网格
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 12

            Repeater {
                model: root.registry ? root.registry.getWidgetTypes() : []

                delegate: Rectangle {
                    id: typeCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: 80
                    radius: 8
                    color: typeMa.containsMouse ? "#1E3A5F" : "#0F172A"
                    border.color: typeMa.containsMouse ? "#3B82F6" : "#334155"
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: 150 } }
                    Behavior on border.color { ColorAnimation { duration: 150 } }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        Text {
                            text: modelData.icon || "📦"
                            font.pixelSize: 24
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: modelData.label || modelData.typeName
                            color: "#F1F5F9"
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    MouseArea {
                        id: typeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.widgetSelected(modelData.typeName)
                    }
                }
            }
        }

        // 提示文字
        Text {
            text: "点击选择要添加的控件类型"
            color: "#64748B"
            font.pixelSize: 11
            Layout.alignment: Qt.AlignHCenter
        }
    }
}

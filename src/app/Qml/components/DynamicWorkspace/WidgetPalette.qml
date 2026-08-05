import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Popup {
    id: root

    property var registry: null
    signal widgetSelected(string typeName)

    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 400
    height: 440
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 0

    background: Rectangle {
        color: "#1E293B"
        border.color: "#475569"
        border.width: 1
        radius: 12
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Text {
            text: "选择控件"
            color: "#F1F5F9"
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: root.registry ? root.registry.getWidgetTypes() : []

            delegate: Rectangle {
                width: listView.width
                height: 52
                radius: 6
                color: typeMa.containsMouse ? "#1E3A5F" : "#0F172A"
                border.color: typeMa.containsMouse ? "#3B82F6" : "#334155"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: modelData.label
                            color: "#F1F5F9"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                        Text {
                            text: modelData.description
                            color: "#94A3B8"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Text {
                        text: modelData.defaultColSpan + "×" + modelData.defaultRowSpan
                        color: "#64748B"
                        font.pixelSize: 11
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

        Text {
            text: "点击选择要添加的控件"
            color: "#64748B"
            font.pixelSize: 11
            Layout.alignment: Qt.AlignHCenter
        }
    }
}

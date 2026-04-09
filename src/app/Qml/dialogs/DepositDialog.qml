import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: root

    property real currentBalance: 0
    signal submitted(real amount, string note)

    modal: true
    focus: true
    width: 440
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0

    function normalizedAmount() {
        var parsed = Number(amountField.text)
        return isNaN(parsed) ? 0 : parsed
    }

    onOpened: {
        amountField.text = ""
        noteField.text = ""
        amountField.forceActiveFocus()
    }

    background: Rectangle {
        radius: 14
        color: "#111827"
        border.width: 1
        border.color: "#334155"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            radius: 14
            color: "#0f172a"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 6

                Text {
                    text: "入金申请"
                    color: "#f8fafc"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }

                Text {
                    text: "当前账户净值: " + root.currentBalance.toLocaleString(Qt.locale(), 'f', 2)
                    color: "#94a3b8"
                    font.pixelSize: 12
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "入金金额"
                color: "#cbd5e1"
                font.pixelSize: 13
            }

            TextField {
                id: amountField
                Layout.fillWidth: true
                placeholderText: "输入本次入金金额"
                color: "#f8fafc"
                selectByMouse: true

                background: Rectangle {
                    radius: 10
                    color: "#1e293b"
                    border.width: 1
                    border.color: amountField.activeFocus ? "#3b82f6" : "#475569"
                }
            }

            Text {
                text: "备注"
                color: "#cbd5e1"
                font.pixelSize: 13
            }

            TextArea {
                id: noteField
                Layout.fillWidth: true
                Layout.preferredHeight: 88
                placeholderText: "可选，记录入金来源或用途"
                color: "#f8fafc"
                selectByMouse: true
                wrapMode: TextEdit.Wrap

                background: Rectangle {
                    radius: 10
                    color: "#1e293b"
                    border.width: 1
                    border.color: noteField.activeFocus ? "#3b82f6" : "#475569"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            Button {
                text: "取消"
                onClicked: root.close()
            }

            Button {
                text: "确认入金"
                enabled: root.normalizedAmount() > 0
                onClicked: {
                    root.submitted(root.normalizedAmount(), noteField.text)
                    root.close()
                }
            }
        }
    }
}
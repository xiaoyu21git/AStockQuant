import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: root

    property string userName: ""
    property string userStatus: ""
    property string userInitials: ""
    signal profileSaved(string userName, string userStatus, string userInitials)

    modal: true
    focus: true
    width: 480
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0

    onOpened: {
        nameField.text = root.userName
        statusField.text = root.userStatus
        initialsField.text = root.userInitials
        nameField.forceActiveFocus()
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
            Layout.preferredHeight: 80
            radius: 14
            color: "#0f172a"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 52
                    radius: 26
                    color: "#2563eb"

                    Text {
                        anchors.centerIn: parent
                        text: initialsField.text || root.userInitials || "QT"
                        color: "white"
                        font.pixelSize: 18
                        font.weight: Font.Bold
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "用户资料"
                        color: "#f8fafc"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: "更新侧边栏展示的姓名、状态和头像缩写"
                        color: "#94a3b8"
                        font.pixelSize: 12
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "姓名"
                color: "#cbd5e1"
                font.pixelSize: 13
            }

            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "输入展示姓名"
                color: "#f8fafc"
                selectByMouse: true

                background: Rectangle {
                    radius: 10
                    color: "#1e293b"
                    border.width: 1
                    border.color: nameField.activeFocus ? "#3b82f6" : "#475569"
                }
            }

            Text {
                text: "状态"
                color: "#cbd5e1"
                font.pixelSize: 13
            }

            TextField {
                id: statusField
                Layout.fillWidth: true
                placeholderText: "例如: 专业版 · 在线"
                color: "#f8fafc"
                selectByMouse: true

                background: Rectangle {
                    radius: 10
                    color: "#1e293b"
                    border.width: 1
                    border.color: statusField.activeFocus ? "#3b82f6" : "#475569"
                }
            }

            Text {
                text: "头像缩写"
                color: "#cbd5e1"
                font.pixelSize: 13
            }

            TextField {
                id: initialsField
                Layout.preferredWidth: 120
                placeholderText: "最多 3 位"
                color: "#f8fafc"
                selectByMouse: true
                maximumLength: 3

                onTextChanged: {
                    var normalized = text.toUpperCase()
                    if (normalized !== text) {
                        text = normalized
                    }
                }

                background: Rectangle {
                    radius: 10
                    color: "#1e293b"
                    border.width: 1
                    border.color: initialsField.activeFocus ? "#3b82f6" : "#475569"
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
                text: "保存"
                enabled: nameField.text.trim().length > 0
                onClicked: {
                    root.profileSaved(nameField.text.trim(), statusField.text.trim(), initialsField.text.trim())
                    root.close()
                }
            }
        }
    }
}
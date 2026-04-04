// ModuleCard.qml - 功能模块卡片组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0 as Theme

Rectangle {
    id: moduleCard
    width: parent.width / 3
    height: 320
    radius: 10
    color: "#182868"
    border.color: Theme.darkBorder
    border.width: 1
    clip: true

    property string moduleId: ""
    property string iconSource: "qrc:/resources/icons/database.svg"
    property string title: "模块标题"
    property string description: "模块描述"
    property var actions: []
    property var recentTasks: []

    signal actionClicked(string actionId)
    signal cardClicked()

    function simulateAction(actionId) {
        console.log("模拟操作:", actionId)
    }

    Rectangle {
        width: parent.width
        height: 4
        color: Theme.accentColor
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onEntered: {
            moduleCard.scale = 1.02
            moduleCard.z = 1
        }
        onExited: {
            moduleCard.scale = 1.0
            moduleCard.z = 0
        }
        onClicked: moduleCard.cardClicked()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 15

            Rectangle {
                Layout.preferredWidth: 50
                Layout.preferredHeight: 50
                radius: 10
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.primaryColor }
                    GradientStop { position: 1.0; color: Theme.secondaryColor }
                }

                Image {
                    source: moduleCard.iconSource
                    width: 24
                    height: 24
                    anchors.centerIn: parent
                }
            }

            Text {
                Layout.fillWidth: true
                text: moduleCard.title
                font.pixelSize: 20
                font.bold: true
                color: Theme.darkText
                elide: Text.ElideRight
            }
        }

        Text {
            Layout.fillWidth: true
            text: moduleCard.description
            font.pixelSize: 14
            color: "#aaa"
            wrapMode: Text.WordWrap
            Layout.bottomMargin: 10
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.darkBorder
        }

        Flow {
            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: moduleCard.actions.length

                Button {
                    readonly property var actionData: moduleCard.actions[index] || ({})
                    text: actionData.label || ""
                    width: 80
                    height: 36

                    background: Rectangle {
                        radius: 4
                        color: actionData.primary ? Theme.primaryColor : Qt.rgba(57, 73, 171, 0.2)
                        border.color: actionData.primary ? Theme.primaryColor : Theme.darkBorder
                        border.width: 1
                    }

                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        color: actionData.primary ? "white" : Theme.darkText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    Image {
                        source: actionData.icon || ""
                        width: 14
                        height: 14
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    onClicked: moduleCard.actionClicked(actionData.id || "")
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 5

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "最近任务"
                    font.pixelSize: 12
                    color: "#aaa"
                }

                Text {
                    text: "查看全部"
                    font.pixelSize: 10
                    color: "#777"
                    Layout.alignment: Qt.AlignRight
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: moduleCard.recentTasks.length
                clip: true

                delegate: Rectangle {
                    readonly property var recentTaskData: moduleCard.recentTasks[index] || ({})
                    width: parent.width
                    height: 36
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        spacing: 8

                        Image {
                            source: recentTaskData.icon || ""
                            width: 16
                            height: 16
                        }

                        Text {
                            text: recentTaskData.name || ""
                            font.pixelSize: 12
                            color: Theme.darkText
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            width: 60
                            height: 20
                            radius: 10
                            color: recentTaskData.status === "running"
                                ? Qt.rgba(255, 193, 7, 0.2)
                                : Qt.rgba(76, 175, 80, 0.2)

                            Text {
                                text: recentTaskData.status === "running" ? "运行中" : "已完成"
                                font.pixelSize: 10
                                color: recentTaskData.status === "running"
                                    ? (Theme.accentColor !== undefined ? Theme.accentColor : "#FF5722")
                                    : (Theme.successColor !== undefined ? Theme.successColor : "#4CAF50")
                                anchors.centerIn: parent
                            }
                        }
                    }
                }
            }
        }
    }
}
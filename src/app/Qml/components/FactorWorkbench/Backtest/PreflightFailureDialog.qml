import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: dialogRoot
    required property QtObject page

    modal: true
    title: "组合回测预检失败明细"
    standardButtons: Dialog.Ok
    width: Math.min(page.width - 48, 760)
    property var failures: []
    property string exportText: ""

    onFailuresChanged: {
        exportText = page.buildPreflightFailureExportText(failures || [])
    }

    onOpened: {
        exportText = page.buildPreflightFailureExportText(failures || [])
    }

    background: Rectangle {
        radius: 12
        color: "#111827"
        border.width: 1
        border.color: "#334155"
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: "以下因子未通过本次组合回测预检。每条记录都会明确显示失败类别，便于区分实例异常、实现未接入、缓存字段缺失、字段值异常或样本不足。"
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: "#CBD5E1"
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 320
            radius: 8
            color: "#0F172A"
            border.width: 1
            border.color: "#1E293B"

            ListView {
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                spacing: 8
                model: dialogRoot.failures

                delegate: Rectangle {
                    width: ListView.view.width
                    implicitHeight: failureDetailColumn.implicitHeight + 20
                    radius: 8
                    color: "#131C2E"
                    border.width: 1
                    border.color: failureMeta.accentColor

                    property var failureMeta: page.preflightCategoryMeta(modelData && modelData.category)

                    ColumnLayout {
                        id: failureDetailColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: page.resolveFactorDisplayName(modelData.factorId || "")
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                color: "#FEE2E2"
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                radius: 9
                                color: failureMeta.chipBackground
                                border.width: 1
                                border.color: failureMeta.chipBorder
                                implicitWidth: detailChipText.implicitWidth + 12
                                implicitHeight: detailChipText.implicitHeight + 8

                                Text {
                                    id: detailChipText
                                    anchors.centerIn: parent
                                    text: failureMeta.shortText
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                    color: failureMeta.chipText
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.instanceId ? ("instanceId: " + modelData.instanceId) : "instanceId: 未解析"
                            font.pixelSize: 11
                            color: "#FCA5A5"
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.reason || "未知预检失败"
                            font.pixelSize: 11
                            color: "#CBD5E1"
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: page.preflightFailureDetailText(modelData)
                            font.pixelSize: 10
                            color: "#94A3B8"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {}
            }
        }

        Text {
            Layout.fillWidth: true
            text: "诊断文本支持手动全选复制，可直接用于问题排查或反馈。"
            font.pixelSize: 11
            color: "#94A3B8"
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 132
                Layout.preferredHeight: 30
                radius: 6
                color: "#1D4ED8"

                Text {
                    anchors.centerIn: parent
                    text: "复制诊断文本"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    color: "#EFF6FF"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        preflightFailureExportTextArea.selectAll()
                        preflightFailureExportTextArea.copy()
                        showToast("📋 诊断文本已复制")
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: 8
            color: "#0B1220"
            border.width: 1
            border.color: "#1E293B"

            ScrollView {
                anchors.fill: parent
                anchors.margins: 8

                TextArea {
                    id: preflightFailureExportTextArea
                    readOnly: true
                    selectByMouse: true
                    text: dialogRoot.exportText
                    wrapMode: TextEdit.NoWrap
                    color: "#CBD5E1"
                    selectionColor: "#1D4ED8"
                    selectedTextColor: "#F8FAFC"
                    font.pixelSize: 11
                    background: null
                }
            }
        }
    }
}

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0

Rectangle {
    id: root

    property var bindingData: ({})
    property bool compact: false
    property bool showTemplateHeader: false
    readonly property var preview: RuleTemplateDetailHelper.describeBinding(bindingData || ({}))

    radius: compact ? 8 : 10
    color: "#0b1220"
    border.width: 1
    border.color: "#334155"
    implicitHeight: contentColumn.implicitHeight + (compact ? 16 : 20)

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: root.compact ? 8 : 10
        spacing: root.compact ? 8 : 10

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: root.showTemplateHeader

            Text {
                Layout.fillWidth: true
                text: root.preview.templateName || "规则模板"
                font.pixelSize: root.compact ? 11 : 12
                font.weight: Font.DemiBold
                color: "#f8fafc"
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: !!root.preview.templateDescription
                text: root.preview.templateDescription || ""
                font.pixelSize: 11
                color: "#94a3b8"
                wrapMode: Text.WordWrap
            }
        }

        Text {
            Layout.fillWidth: true
            visible: !root.preview.valid
            text: root.preview.errorMessage || "未能展开模板条件"
            font.pixelSize: 11
            color: "#fca5a5"
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: root.preview.valid && Array.isArray(root.preview.rules) ? root.preview.rules : []

            delegate: Rectangle {
                required property var modelData

                Layout.fillWidth: true
                radius: 8
                color: "#111827"
                border.width: 1
                border.color: "#1f2937"
                implicitHeight: ruleColumn.implicitHeight + 16

                ColumnLayout {
                    id: ruleColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: modelData.name || modelData.id || "未命名规则"
                            font.pixelSize: root.compact ? 11 : 12
                            font.weight: Font.DemiBold
                            color: "#e2e8f0"
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: modelData.priority !== undefined && modelData.priority !== null && modelData.priority !== ""
                            text: "P" + modelData.priority
                            font.pixelSize: 10
                            color: "#60a5fa"
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !!modelData.description
                        text: modelData.description || ""
                        font.pixelSize: 11
                        color: "#94a3b8"
                        wrapMode: Text.WordWrap
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: Array.isArray(modelData.conditionLines) && modelData.conditionLines.length > 0

                        Text {
                            text: "触发条件"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            color: "#7dd3fc"
                        }

                        Repeater {
                            model: Array.isArray(modelData.conditionLines) ? modelData.conditionLines : []

                            delegate: Item {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: conditionText.implicitHeight

                                Text {
                                    id: conditionText
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: (modelData.level || 0) * 14
                                    text: "- " + (modelData.text || "")
                                    font.pixelSize: 11
                                    color: "#e2e8f0"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: Array.isArray(modelData.actionLines) && modelData.actionLines.length > 0

                        Text {
                            text: "动作结果"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            color: "#34d399"
                        }

                        Repeater {
                            model: Array.isArray(modelData.actionLines) ? modelData.actionLines : []

                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: "- " + (modelData.text || "")
                                font.pixelSize: 11
                                color: "#d1fae5"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: Array.isArray(modelData.payloadLines) && modelData.payloadLines.length > 0

                        Text {
                            text: "Payload"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            color: "#fbbf24"
                        }

                        Repeater {
                            model: Array.isArray(modelData.payloadLines) ? modelData.payloadLines : []

                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: "- " + (modelData.text || "")
                                font.pixelSize: 11
                                color: "#fde68a"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: Array.isArray(modelData.stateLines) && modelData.stateLines.length > 0

                        Text {
                            text: "状态写入"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            color: "#c084fc"
                        }

                        Repeater {
                            model: Array.isArray(modelData.stateLines) ? modelData.stateLines : []

                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: "- " + (modelData.text || "")
                                font.pixelSize: 11
                                color: "#e9d5ff"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
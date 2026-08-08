import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot
        Layout.fillWidth: true
        implicitHeight: preflightSummaryColumn.implicitHeight + 24
        radius: 10
        color: "#3F1D24"
        border.width: 1
        border.color: "#F87171"
        visible: !isBacktesting && lastPreflightFailures.length > 0

        ColumnLayout {
            id: preflightSummaryColumn
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "⚠️ 组合回测预检未通过"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: "#FECACA"
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "失败因子: " + lastPreflightFailures.length + " 个"
                    font.pixelSize: 11
                    color: "#FCA5A5"
                }

                Rectangle {
                    Layout.preferredWidth: 92
                    Layout.preferredHeight: 28
                    radius: 6
                    color: "#7F1D1D"

                    Text {
                        anchors.centerIn: parent
                        text: "查看明细"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        color: "#FEE2E2"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            preflightFailureDialog.failures = lastPreflightFailures
                            preflightFailureDialog.open()
                        }
                    }
                }
            }

            Repeater {
                model: Math.min(lastPreflightFailures.length, 2)

                delegate: Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: "#2A1520"
                    border.width: 1
                    border.color: failureMeta.accentColor
                    implicitHeight: failureSummaryColumn.implicitHeight + 14

                    property var failure: lastPreflightFailures[index]
                    property var failureMeta: preflightCategoryMeta(failure && failure.category)

                    ColumnLayout {
                        id: failureSummaryColumn
                        anchors.fill: parent
                        anchors.margins: 7
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: resolveFactorDisplayName((failure && failure.factorId) || "")
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                color: "#FEE2E2"
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                radius: 9
                                color: failureMeta.chipBackground
                                border.width: 1
                                border.color: failureMeta.chipBorder
                                implicitWidth: failureChipText.implicitWidth + 12
                                implicitHeight: failureChipText.implicitHeight + 8

                                Text {
                                    id: failureChipText
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
                            text: (failure && failure.reason) || "Preflight failed"
                            font.pixelSize: 11
                            color: "#FECACA"
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Text {
                visible: lastPreflightFailures.length > 2
                text: "More failures: " + (lastPreflightFailures.length - 2)
                font.pixelSize: 10
                color: "#FCA5A5"
            }
        }
    }
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot

    // 暴露内部 Switch 供 root.onEnableAdvancedOptionsChanged 访问
    property alias advancedParamsSwitch: advancedParamsSwitch
    property alias parameterOptimizationRangeCombo: parameterOptimizationRangeCombo
    property alias sensitivityAnalysisCombo: sensitivityAnalysisCombo
    property alias parameterConstraintsCombo: parameterConstraintsCombo
    property alias parameterInitializationMethodCombo: parameterInitializationMethodCombo
    property alias customParameterScriptTextArea: customParameterScriptTextArea

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    Layout.minimumHeight: root.enableAdvancedOptions ? 184 : 56
    radius: 10
    color: "#1e293b"
    border.width: 1
    border.color: "#334155"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // 标题和切换
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "优化与脚本"
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#f1f5f9"
            }

            Item { Layout.fillWidth: true }

            Switch {
                id: advancedParamsSwitch
                checked: root.enableAdvancedOptions
                onCheckedChanged: {
                    root.enableAdvancedOptions = checked
                    root.advancedOptionsChanged(checked)
                }

                indicator: Rectangle {
                    implicitWidth: 36
                    implicitHeight: 20
                    radius: 10
                    color: parent.checked ? "#3b82f6" : "#334155"
                    border.width: 1
                    border.color: parent.checked ? "#3b82f6" : "#475569"

                    Rectangle {
                        x: parent.checked ? parent.width - width - 2 : 2
                        y: 2
                        width: 16
                        height: 16
                        radius: 8
                        color: "white"
                        Behavior on x {
                            NumberAnimation { duration: 200 }
                        }
                    }
                }
            }
        }

        // 高级选项内容
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: root.enableAdvancedOptions

            GridLayout {
                Layout.fillWidth: true
                columns: root.useWideParamGrid ? 2 : 1
                columnSpacing: 12
                rowSpacing: 10

                // 参数优化范围
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    Text {
                        text: strategyService.tr('strategyCreation.parameterOptimizationRange')
                        font.pixelSize: 12
                        color: "#cbd5e1"
                    }

                    ComboBox {
                        id: parameterOptimizationRangeCombo
                        Layout.fillWidth: true
                        model: strategyService.tr('strategyCreation.parameterOptimizationRangeOptions').split(',')
                        currentIndex: 1

                        background: Rectangle {
                            implicitHeight: 36
                            radius: 6
                            color: "#1e293b"
                            border.width: 1
                            border.color: "#334155"
                        }

                        contentItem: Text {
                            text: parent.displayText
                            color: "#f1f5f9"
                            font.pixelSize: 12
                            padding: 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // 参数敏感性分析
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    Text {
                        text: strategyService.tr('strategyCreation.sensitivityAnalysis')
                        font.pixelSize: 12
                        color: "#cbd5e1"
                    }

                    ComboBox {
                        id: sensitivityAnalysisCombo
                        Layout.fillWidth: true
                        model: strategyService.tr('strategyCreation.sensitivityAnalysisOptions').split(',')
                        currentIndex: 1

                        background: Rectangle {
                            implicitHeight: 36
                            radius: 6
                            color: "#1e293b"
                            border.width: 1
                            border.color: "#334155"
                        }

                        contentItem: Text {
                            text: parent.displayText
                            color: "#f1f5f9"
                            font.pixelSize: 12
                            padding: 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // 参数约束
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    Text {
                        text: strategyService.tr('strategyCreation.parameterConstraints')
                        font.pixelSize: 12
                        color: "#cbd5e1"
                    }

                    ComboBox {
                        id: parameterConstraintsCombo
                        Layout.fillWidth: true
                        model: strategyService.tr('strategyCreation.parameterConstraintOptions').split(',')
                        currentIndex: 0

                        background: Rectangle {
                            implicitHeight: 36
                            radius: 6
                            color: "#1e293b"
                            border.width: 1
                            border.color: "#334155"
                        }

                        contentItem: Text {
                            text: parent.displayText
                            color: "#f1f5f9"
                            font.pixelSize: 12
                            padding: 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // 参数初始化方式
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    Text {
                        text: strategyService.tr('strategyCreation.parameterInitializationMethod')
                        font.pixelSize: 12
                        color: "#cbd5e1"
                    }

                    ComboBox {
                        id: parameterInitializationMethodCombo
                        Layout.fillWidth: true
                        model: strategyService.tr('strategyCreation.parameterInitializationMethods').split(',')
                        currentIndex: 0

                        background: Rectangle {
                            implicitHeight: 36
                            radius: 6
                            color: "#1e293b"
                            border.width: 1
                            border.color: "#334155"
                        }

                        contentItem: Text {
                            text: parent.displayText
                            color: "#f1f5f9"
                            font.pixelSize: 12
                            padding: 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            // 自定义参数脚本
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: strategyService.tr('strategyCreation.customParameterScript')
                    font.pixelSize: 12
                    color: "#cbd5e1"
                }

                TextArea {
                    id: customParameterScriptTextArea
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    placeholderText: strategyService.tr('strategyCreation.customParameterScriptPlaceholder')
                    wrapMode: Text.WordWrap

                    background: Rectangle {
                        radius: 6
                        color: "#1e293b"
                        border.width: 1
                        border.color: "#334155"
                    }

                    color: "#f1f5f9"
                    font.pixelSize: 12
                    padding: 10
                }
            }
        }
    }
}

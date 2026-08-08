import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0 as ConsoleUiComponents
import "../../components/FactorWorkbench/Creation/components" as PluginComponents

Rectangle {
    id: panelRoot

    // 数据流入
    required property var dynamicParamConfigs
    required property var dynamicParamGroups
    required property var dynamicParamValues
    required property var paramComponents
    required property bool parametersLoaded
    required property bool autoStopEnabled
    required property bool forceTwoColumnSections

    // 数据流入（只读状态）
    required property int loadedParamCount

    // 主题常量
    readonly property color pageText: "#E2E8F0"
    readonly property color secondaryText: "#94A3B8"
    readonly property color subtleText: "#64748B"
    readonly property color elevatedCardBg: "#111827"
    readonly property color cardBorder: "#334155"
    readonly property color cardBorderSoft: "#273449"
    readonly property color headerStripBg: "#131F33"
    readonly property color insetPanelBg: "#132238"
    readonly property color insetPanelBorder: "#26486E"
    readonly property color successGreen: "#10B981"
    readonly property color successSoft: "#0F2F22"
    readonly property color warningOrange: "#F97316"
    readonly property color warningSoft: "#3A2A10"
    readonly property color primaryBlueSoft: "#172554"
    readonly property int cardRadius: 20
    readonly property int subPanelRadius: 16
    readonly property int cardInnerPadding: 24
    readonly property int sectionHeaderHeight: 28
    readonly property int sectionIntroHeight: 36

    // 事件流出
    signal saveRequested()
    signal applyRequested()
    signal resetRequested()
    signal autoStopToggled(bool checked)
    signal paramsChanged(var newValues)

    Layout.fillWidth: true
    radius: cardRadius
    color: elevatedCardBg
    border.color: cardBorder
    border.width: 1
    implicitHeight: configCardBody.implicitHeight + 76

    ColumnLayout {
        anchors.fill: parent

        // 头部操作栏
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: headerStripBg
            border.color: cardBorderSoft
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: cardInnerPadding
                anchors.rightMargin: cardInnerPadding
                spacing: 12

                Text {
                    text: "风控规则配置"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: pageText
                }

                Item { Layout.fillWidth: true }

                ConsoleUiComponents.ActionButton {
                    label: "保存全部规则"
                    tone: "primary"
                    buttonWidth: 110
                    buttonHeight: 38
                    labelSize: 13
                    onClicked: panelRoot.saveRequested()
                }

                ConsoleUiComponents.ActionButton {
                    label: "应用配置"
                    tone: "success"
                    buttonWidth: 96
                    buttonHeight: 38
                    labelSize: 13
                    onClicked: panelRoot.applyRequested()
                }

                ConsoleUiComponents.ActionButton {
                    label: "恢复默认"
                    tone: "muted"
                    buttonWidth: 96
                    buttonHeight: 38
                    labelSize: 13
                    onClicked: panelRoot.resetRequested()
                }
            }
        }

        // 主体内容
        ColumnLayout {
            id: configCardBody
            Layout.fillWidth: true
            Layout.leftMargin: cardInnerPadding
            Layout.rightMargin: cardInnerPadding
            Layout.topMargin: cardInnerPadding
            Layout.bottomMargin: cardInnerPadding
            spacing: 18

            // 两列配置区（核心 + 可选）
            Item {
                Layout.fillWidth: true
                implicitHeight: configSectionsFlow.implicitHeight

                Flow {
                    id: configSectionsFlow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 24

                    // 核心配置列
                    Item {
                        id: coreConfigSection
                        width: panelRoot.forceTwoColumnSections
                            ? Math.max(0, (configSectionsFlow.width - configSectionsFlow.spacing) / 2)
                            : configSectionsFlow.width
                        implicitHeight: coreConfigSectionColumn.implicitHeight

                        ColumnLayout {
                            id: coreConfigSectionColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: sectionHeaderHeight
                                radius: 8
                                color: insetPanelBg
                                border.color: cardBorderSoft
                                border.width: 1

                                Text {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 12
                                    text: "组合层面与持仓限制"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: secondaryText
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.preferredHeight: sectionIntroHeight
                                text: "核心参数直接决定仓位、回撤和单标的风险边界，保持常驻显示并支持快速微调。"
                                font.pixelSize: 12
                                color: subtleText
                                wrapMode: Text.WordWrap
                                verticalAlignment: Text.AlignVCenter
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Repeater {
                                    model: [
                                        { label: "配置已同步", tone: "success" },
                                        { label: "回测已同步", tone: "info" },
                                        { label: "执行已生效", tone: "warning" }
                                    ]

                                    delegate: Rectangle {
                                        radius: 10
                                        height: 24
                                        width: statusChipLabel.implicitWidth + 18
                                        color: modelData.tone === "success"
                                            ? successSoft
                                            : (modelData.tone === "warning" ? warningSoft : primaryBlueSoft)
                                        border.color: modelData.tone === "success"
                                            ? successGreen
                                            : (modelData.tone === "warning" ? warningOrange : "#31539A")
                                        border.width: 1

                                        Text {
                                            id: statusChipLabel
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            color: modelData.tone === "success"
                                                ? "#6EE7B7"
                                                : (modelData.tone === "warning" ? "#FDBA74" : "#BFDBFE")
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(coreControlsColumn.implicitHeight, optionalControlsColumn.implicitHeight) + 28
                                radius: subPanelRadius
                                color: insetPanelBg
                                border.color: cardBorderSoft
                                border.width: 1

                                ColumnLayout {
                                    id: coreControlsColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 14
                                    spacing: 0

                                    Text {
                                        Layout.fillWidth: true
                                        text: "组合与持仓风控"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: secondaryText
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "详细风险参数统一放在下方高级参数区展示与编辑，当前区域仅保留概览与常用开关。"
                                        font.pixelSize: 12
                                        color: subtleText
                                        wrapMode: Text.WordWrap
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 58
                                        color: "transparent"
                                        border.color: cardBorderSoft
                                        border.width: 0

                                        RowLayout {
                                            anchors.fill: parent
                                            spacing: 14

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2

                                                Text {
                                                    text: "自动止损"
                                                    font.pixelSize: 14
                                                    font.weight: Font.Medium
                                                    color: pageText
                                                }

                                                Text {
                                                    text: "达到止损线自动执行平仓"
                                                    font.pixelSize: 12
                                                    color: subtleText
                                                }
                                            }

                                            Switch {
                                                id: stopSwitch
                                                Layout.alignment: Qt.AlignVCenter
                                                checked: panelRoot.autoStopEnabled
                                                onToggled: panelRoot.autoStopToggled(checked)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 可选配置列
                    Item {
                        id: optionalConfigSection
                        width: panelRoot.forceTwoColumnSections
                            ? Math.max(0, (configSectionsFlow.width - configSectionsFlow.spacing) / 2)
                            : configSectionsFlow.width
                        implicitHeight: optionalConfigSectionColumn.implicitHeight

                        ColumnLayout {
                            id: optionalConfigSectionColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: sectionHeaderHeight
                                radius: 8
                                color: insetPanelBg
                                border.color: cardBorderSoft
                                border.width: 1

                                Text {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 12
                                    text: "可选执行参数"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: secondaryText
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.preferredHeight: sectionIntroHeight
                                text: "执行风控和熔断更偏向交易侧保护，当前保留配置能力，但不和左侧做不对称折叠。"
                                font.pixelSize: 12
                                color: subtleText
                                wrapMode: Text.WordWrap
                                verticalAlignment: Text.AlignVCenter
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Repeater {
                                    model: [
                                        { label: "配置已同步", tone: "success" },
                                        { label: "回测已透传", tone: "info" },
                                        { label: "执行部分生效", tone: "warning" }
                                    ]

                                    delegate: Rectangle {
                                        radius: 10
                                        height: 24
                                        width: optionalStatusChipLabel.implicitWidth + 18
                                        color: modelData.tone === "success"
                                            ? successSoft
                                            : (modelData.tone === "warning" ? warningSoft : primaryBlueSoft)
                                        border.color: modelData.tone === "success"
                                            ? successGreen
                                            : (modelData.tone === "warning" ? warningOrange : "#31539A")
                                        border.width: 1

                                        Text {
                                            id: optionalStatusChipLabel
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            color: modelData.tone === "success"
                                                ? "#6EE7B7"
                                                : (modelData.tone === "warning" ? "#FDBA74" : "#BFDBFE")
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(coreControlsColumn.implicitHeight, optionalControlsColumn.implicitHeight) + 28
                                radius: subPanelRadius
                                color: insetPanelBg
                                border.color: cardBorderSoft
                                border.width: 1

                                ColumnLayout {
                                    id: optionalControlsColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 14
                                    spacing: 0

                                    Text {
                                        Layout.fillWidth: true
                                        text: "交易执行与熔断"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: secondaryText
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "执行侧参数也统一由下方高级参数区管理，避免同一配置在页面上重复渲染。"
                                        font.pixelSize: 12
                                        color: subtleText
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 高级参数区
            Rectangle {
                Layout.fillWidth: true
                radius: subPanelRadius
                color: insetPanelBg
                border.color: insetPanelBorder
                border.width: 1
                implicitHeight: advancedParamsColumn.implicitHeight + 32

                ColumnLayout {
                    id: advancedParamsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "高级参数"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: pageText
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: panelRoot.parametersLoaded ? "已加载 " + panelRoot.loadedParamCount + " 项" : "正在加载参数"
                            font.pixelSize: 12
                            color: panelRoot.parametersLoaded ? successGreen : warningOrange
                        }
                    }

                    PluginComponents.DynamicParamGenerator {
                        id: dynamicParamGenerator
                        Layout.fillWidth: true
                        Layout.preferredHeight: 360
                        visible: panelRoot.parametersLoaded && panelRoot.dynamicParamConfigs.length > 0
                        minColumnWidth: width < 900 ? 280 : 320
                        maxColumns: panelRoot.width >= 1500 ? 3 : (width < 900 ? 1 : 2)
                        paramRegistry: panelRoot.paramComponents
                        configs: panelRoot.dynamicParamConfigs
                        groups: panelRoot.dynamicParamGroups
                        showGroups: panelRoot.dynamicParamGroups.length > 0
                        values: panelRoot.dynamicParamValues

                        onParamsChanged: function(newValues) {
                            panelRoot.paramsChanged(newValues)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 176
                        visible: !panelRoot.parametersLoaded
                        radius: 12
                        color: insetPanelBg
                        border.color: insetPanelBorder
                        border.width: 1

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 8

                            BusyIndicator {
                                Layout.alignment: Qt.AlignHCenter
                                running: parent.parent.visible
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "正在准备风控参数..."
                                font.pixelSize: 12
                                color: pageText
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "参数未就绪前只显示占位，不让动态表单后置跳出。"
                                font.pixelSize: 11
                                color: subtleText
                            }
                        }
                    }
                }
            }
        }
    }
}

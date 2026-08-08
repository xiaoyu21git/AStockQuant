import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Popup {
    id: dialogRoot
    required property QtObject page

    // 暴露内部控件 ID 供父组件 JS 函数 (loadRuntimeParamsDialog / applyRuntimeParamsDialog) 访问
    property alias runtimeAdjustPriceTypeGroup: runtimeAdjustPriceTypeGroup
    property alias runtimeAdjustPriceTypePreButton: runtimeAdjustPriceTypePreButton
    property alias runtimeAdjustPriceTypePostButton: runtimeAdjustPriceTypePostButton
    property alias runtimeInitialCapitalField: runtimeInitialCapitalField
    property alias runtimeForwardDaysField: runtimeForwardDaysField
    property alias runtimeMarketEnvironmentComboBox: runtimeMarketEnvironmentComboBox
    property alias runtimeRebalanceDaysField: runtimeRebalanceDaysField
    property alias runtimeSignalThresholdField: runtimeSignalThresholdField
    property alias runtimeEnableTurnoverLimitBox: runtimeEnableTurnoverLimitBox
    property alias runtimeMaxRebalanceTurnoverField: runtimeMaxRebalanceTurnoverField
    property alias runtimeTransactionCostField: runtimeTransactionCostField
    property alias runtimeSlippageRateField: runtimeSlippageRateField
    property alias runtimeRiskFreeRateField: runtimeRiskFreeRateField
    property alias runtimeBenchmarkSymbolField: runtimeBenchmarkSymbolField

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: Math.max(24, (page.width - width) / 2)
    y: Math.max(24, (page.height - height) / 2)
    width: Math.min(780, page.width - 32)
    height: Math.min(660, page.height - 32)

    ButtonGroup {
        id: runtimeAdjustPriceTypeGroup
    }

    background: Rectangle {
        radius: 18
        border.width: 1
        border.color: "#273244"
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0B1220" }
            GradientStop { position: 0.6; color: "#0F172A" }
            GradientStop { position: 1.0; color: "#111827" }
        }
    }

    contentItem: Item {
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 14

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: "回测参数设置"
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: "#F8FAFC"
                    }

                    Text {
                        text: page.runtimeParamsSummaryText()
                        font.pixelSize: 11
                        color: "#38BDF8"
                        horizontalAlignment: Text.AlignRight
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 14
                border.width: 1
                border.color: "#243244"
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#122033" }
                    GradientStop { position: 1.0; color: "#0F172A" }
                }

                implicitHeight: bannerColumn.implicitHeight + 26

                ColumnLayout {
                    id: bannerColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8

                    Text {
                        text: "先设置复权，再调整参数"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: "#F8FAFC"
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "复权方式会直接影响价格序列、收益和回撤的展示口径，建议先确认这里再做其它参数微调。"
                        font.pixelSize: 11
                        color: "#94A3B8"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    id: runtimeParamsContent
                    width: Math.max(0, dialogRoot.width - 60)
                    spacing: 14

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        border.width: 1
                        border.color: "#243244"
                        color: "#0F172A"
                        implicitHeight: adjustCardColumn.implicitHeight + 28

                        ColumnLayout {
                            id: adjustCardColumn
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: "复权方式"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                    }

                                    Text {
                                        text: "默认使用后复权；如果更关注历史原始价格走势，可切换为前复权。"
                                        font.pixelSize: 11
                                        color: "#94A3B8"
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                Rectangle {
                                    radius: 999
                                    color: "#0B1220"
                                    border.width: 1
                                    border.color: "#334155"
                                    implicitWidth: defaultAdjustChip.implicitWidth + 18
                                    implicitHeight: defaultAdjustChip.implicitHeight + 10

                                    Text {
                                        id: defaultAdjustChip
                                        anchors.centerIn: parent
                                        text: "默认后复权"
                                        font.pixelSize: 10
                                        font.weight: Font.Medium
                                        color: "#93C5FD"
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                RadioButton {
                                    id: runtimeAdjustPriceTypePreButton
                                    ButtonGroup.group: runtimeAdjustPriceTypeGroup
                                    text: "前复权"
                                    checked: false
                                    Layout.fillWidth: true
                                }

                                RadioButton {
                                    id: runtimeAdjustPriceTypePostButton
                                    ButtonGroup.group: runtimeAdjustPriceTypeGroup
                                    text: "后复权"
                                    checked: true
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 14
                            border.width: 1
                            border.color: "#243244"
                            color: "#0F172A"
                            implicitHeight: coreCardColumn.implicitHeight + 28

                            ColumnLayout {
                                id: coreCardColumn
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10

                                Text {
                                    text: "核心调仓参数"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: "#F8FAFC"
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 12
                                    rowSpacing: 10

                                    Text { text: "初始资金"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeInitialCapitalField
                                        Layout.fillWidth: true
                                        text: "1000000"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        validator: DoubleValidator { bottom: 1; top: 1000000000000; decimals: 2 }
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeInitialCapitalField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }

                                    Text { text: "持仓天数"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeForwardDaysField
                                        Layout.fillWidth: true
                                        text: "30"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        validator: IntValidator { bottom: 1; top: 3650 }
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeForwardDaysField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }

                                    Text { text: "市场环境"; font.pixelSize: 11; color: "#94A3B8" }
                                    ComboBox {
                                        id: runtimeMarketEnvironmentComboBox
                                        Layout.fillWidth: true
                                        model: page.marketEnvironmentOptions
                                        textRole: "label"
                                        currentIndex: 0
                                        font.pixelSize: 12
                                        onActivated: page.applyRuntimeParamsDialog()
                                    }

                                    Text { text: "调仓天数"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeRebalanceDaysField
                                        Layout.fillWidth: true
                                        text: "15"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        validator: IntValidator { bottom: 1; top: 3650 }
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeRebalanceDaysField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }

                                    Text { text: "信号阈值(σ)"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeSignalThresholdField
                                        Layout.fillWidth: true
                                        text: "0.30"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        validator: DoubleValidator { bottom: 0; top: 100 }
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeSignalThresholdField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }

                                    Text { text: "换手上限"; font.pixelSize: 11; color: "#94A3B8" }
                                    RadioButton {
                                        id: runtimeEnableTurnoverLimitBox
                                        Layout.fillWidth: true
                                        text: "启用换手上限"
                                        checked: false
                                        font.pixelSize: 12
                                    }

                                    Text { text: "最大换手"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeMaxRebalanceTurnoverField
                                        Layout.fillWidth: true
                                        text: "0.50"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        validator: DoubleValidator { bottom: 0; top: 100 }
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeMaxRebalanceTurnoverField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 14
                            border.width: 1
                            border.color: "#243244"
                            color: "#0F172A"
                            implicitHeight: costCardColumn.implicitHeight + 28

                            ColumnLayout {
                                id: costCardColumn
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10

                                Text {
                                    text: "交易成本与基准"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: "#F8FAFC"
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 12
                                    rowSpacing: 10

                                    Text { text: "手续费(%)"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeTransactionCostField
                                        Layout.fillWidth: true
                                        text: "0.10"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        validator: DoubleValidator { bottom: 0; top: 100 }
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeTransactionCostField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }

                                    Text { text: "滑点(%)"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeSlippageRateField
                                        Layout.fillWidth: true
                                        text: "0.10"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        validator: DoubleValidator { bottom: 0; top: 100 }
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeSlippageRateField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }

                                    Text { text: "无风险利率(%)"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeRiskFreeRateField
                                        Layout.fillWidth: true
                                        text: "2.00"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        validator: DoubleValidator { bottom: 0; top: 100 }
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeRiskFreeRateField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }

                                    Text { text: "基准代码"; font.pixelSize: 11; color: "#94A3B8" }
                                    TextField {
                                        id: runtimeBenchmarkSymbolField
                                        Layout.fillWidth: true
                                        text: "000300.SH"
                                        color: "#F1F5F9"
                                        font.pixelSize: 12
                                        placeholderText: "000300.SH"
                                        background: Rectangle {
                                            radius: 10
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: runtimeBenchmarkSymbolField.activeFocus ? "#3B82F6" : "#334155"
                                        }
                                        onEditingFinished: page.applyRuntimeParamsDialog()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2

                Text {
                    text: "修改后会直接写入当前回测参数，开始回测时自动生效"
                    font.pixelSize: 10
                    color: "#64748B"
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.preferredWidth: 92
                    Layout.preferredHeight: 34
                    radius: 8
                    color: "#334155"

                    Text {
                        anchors.centerIn: parent
                        text: "重载"
                        font.pixelSize: 12
                        color: "#E2E8F0"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.loadRuntimeParamsDialog()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 92
                    Layout.preferredHeight: 34
                    radius: 8
                    color: "#2563EB"

                    Text {
                        anchors.centerIn: parent
                        text: "应用"
                        font.pixelSize: 12
                        color: "white"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            page.applyRuntimeParamsDialog()
                            dialogRoot.close()
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 92
                    Layout.preferredHeight: 34
                    radius: 8
                    color: "#334155"

                    Text {
                        anchors.centerIn: parent
                        text: "关闭"
                        font.pixelSize: 12
                        color: "#E2E8F0"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dialogRoot.close()
                    }
                }
            }
        }
    }
}

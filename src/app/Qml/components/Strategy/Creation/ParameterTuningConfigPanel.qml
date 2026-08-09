// ParameterTuningConfigPanel.qml
// 参数自动调优配置面板 — 选择策略类型/参数范围/优化目标
// 作为 Dialog 嵌入策略页面

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import AStock.Bridge 1.0 as Bridge

Popup {
    id: root

    // ── 外部输入 ──
    property string strategyId: ""
    property int strategyTypeIndex: 0
    property string strategyName: ""
    property var backtestParams: ({})  // {startDate, endDate, initialCapital, ...}

    // ── 配置结果 ──
    signal tuningStarted(var config)

    width: 680
    height: 620
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    // ── 主题色 ──
    readonly property color bgPrimary: "#0F172A"
    readonly property color bgSecondary: "#1E293B"
    readonly property color bgTertiary: "#334155"
    readonly property color borderColor: "#475569"
    readonly property color accentBlue: "#3B82F6"
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color successGreen: "#10B981"
    readonly property color warningOrange: "#F59E0B"

    background: Rectangle {
        color: root.bgPrimary
        radius: 12
        border.width: 1
        border.color: root.borderColor

        layer.enabled: true
        layer.effect: DropShadow {
            radius: 16
            samples: 25
            color: "#00000060"
        }
    }

    // ── 动态数据 ──
    property var paramRanges: Bridge.ParameterTuningBridge
        ? Bridge.ParameterTuningBridge.getTuningParamRanges(strategyTypeIndex) : []
    property int totalCombinations: Bridge.ParameterTuningBridge
        ? Bridge.ParameterTuningBridge.estimateCombinations(strategyTypeIndex) : 0

    onStrategyTypeIndexChanged: {
        paramRanges = Bridge.ParameterTuningBridge
            ? Bridge.ParameterTuningBridge.getTuningParamRanges(strategyTypeIndex) : []
        totalCombinations = Bridge.ParameterTuningBridge
            ? Bridge.ParameterTuningBridge.estimateCombinations(strategyTypeIndex) : 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 标题栏 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16

                Text {
                    text: "参数自动调优"
                    color: root.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                ButtonSecondary {
                    text: "取消"
                    onClicked: root.close()
                }
            }
        }

        // ── 内容 ──
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: parent.width - 4
                spacing: 12
                anchors.margins: 16

                // ── 策略信息 ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: root.bgSecondary
                    radius: 8

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        Text {
                            text: "策略: " + (strategyName || strategyId || "未选择")
                            color: root.textPrimary
                            font.pixelSize: 13
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "类型: " + Bridge.StrategyBridge.strategyTypeName(strategyTypeIndex)
                            color: root.textSecondary
                            font.pixelSize: 12
                        }
                    }
                }

                // ── 优化器选择 ──
                Text {
                    text: "优化算法"
                    color: root.textSecondary
                    font.pixelSize: 12
                    font.bold: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: root.bgSecondary
                    radius: 8

                    ComboBox {
                        id: optimizerCombo
                        anchors.centerIn: parent
                        width: parent.width - 20
                        model: ["网格搜索 (GridSearch)", "贝叶斯优化 (Bayesian) — 待上线"]
                        currentIndex: 0
                        background: Rectangle {
                            color: "transparent"
                            border.color: root.borderColor
                            radius: 6
                        }
                        contentItem: Text {
                            text: optimizerCombo.currentText
                            color: root.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }
                    }
                }

                // ── 优化目标 ──
                Text {
                    text: "优化目标"
                    color: root.textSecondary
                    font.pixelSize: 12
                    font.bold: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: root.bgSecondary
                    radius: 8

                    ComboBox {
                        id: objectiveCombo
                        anchors.centerIn: parent
                        width: parent.width - 20
                        model: [
                            "夏普比率 (SharpeRatio)",
                            "年化收益率 (AnnualizedReturn)",
                            "卡玛比率 (CalmarRatio)",
                            "索提诺比率 (SortinoRatio)",
                            "盈亏比 (ProfitFactor)"
                        ]
                        currentIndex: 0
                        background: Rectangle {
                            color: "transparent"
                            border.color: root.borderColor
                            radius: 6
                        }
                        contentItem: Text {
                            text: objectiveCombo.currentText
                            color: root.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }
                    }
                }

                // ── 最大试运行次数 ──
                Text {
                    text: "最大试运行次数 (0=全部组合, 共 " + totalCombinations + " 种)"
                    color: root.textSecondary
                    font.pixelSize: 12
                    font.bold: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: root.bgSecondary
                    radius: 8

                    SpinBox {
                        id: maxTrialsSpin
                        anchors.centerIn: parent
                        from: 0
                        to: Math.min(totalCombinations, 100000)
                        value: 0
                        stepSize: 50

                        contentItem: TextInput {
                            text: maxTrialsSpin.value
                            color: root.textPrimary
                            horizontalAlignment: TextInput.AlignHCenter
                            verticalAlignment: TextInput.AlignVCenter
                        }
                        background: Rectangle {
                            color: "transparent"
                            border.color: root.borderColor
                            radius: 6
                        }
                    }
                }

                // ── 组合爆炸警告 ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: totalCombinations > 10000 ? 36 : 0
                    visible: totalCombinations > 10000
                    color: "#78350F"
                    radius: 6

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        Text {
                            text: "⚠ 共 " + totalCombinations + " 种组合，可能耗时较长"
                            color: root.warningOrange
                            font.pixelSize: 12
                        }
                    }
                }

                // ── 可调参数预览 ──
                Text {
                    text: "参数空间预览 (" + paramRanges.length + " 维)"
                    color: root.textSecondary
                    font.pixelSize: 12
                    font.bold: true
                }

                Repeater {
                    model: paramRanges

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        color: index % 2 === 0 ? root.bgSecondary : "#1A2236"
                        radius: 6

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Text {
                                text: modelData.name || ""
                                color: root.textPrimary
                                font.pixelSize: 12
                                Layout.preferredWidth: 160
                            }

                            Text {
                                text: {
                                    switch (modelData.type) {
                                    case 0: return "整数 [" + modelData.min + "→" + modelData.max + " step " + modelData.step + "]"
                                    case 1: return "浮点 [" + modelData.min.toFixed(2) + "→" + modelData.max.toFixed(2) + " step " + modelData.step.toFixed(2) + "]"
                                    case 2: return "布尔"
                                    case 3: return "枚举 (" + (modelData.options ? modelData.options.length : 0) + " 选项)"
                                    default: return ""
                                    }
                                }
                                color: root.textSecondary
                                font.pixelSize: 11
                                Layout.fillWidth: true
                            }

                            Text {
                                text: "×" + (modelData.candidateCount || 0)
                                color: root.accentBlue
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                    }
                }

                // 底部留白
                Item { Layout.preferredHeight: 8 }
            }
        }

        // ── 底部操作栏 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16

                Text {
                    text: totalCombinations > 0
                        ? "预计 " + totalCombinations + " 种组合"
                        : "无可用参数"
                    color: root.textSecondary
                    font.pixelSize: 12
                }

                Item { Layout.fillWidth: true }

                ButtonSecondary {
                    text: "取消"
                    onClicked: root.close()
                    Layout.preferredWidth: 80
                }

                ButtonPrimary {
                    text: "开始调优"
                    enabled: totalCombinations > 0 && !Bridge.ParameterTuningBridge.isRunning
                    Layout.preferredWidth: 120
                    onClicked: {
                        let config = {
                            strategyId: root.strategyId,
                            strategyTypeIndex: root.strategyTypeIndex,
                            optimizerKind: optimizerCombo.currentIndex,
                            objectiveMetric: ["sharpe","annualizedReturn","calmar","sortino","profitFactor"][objectiveCombo.currentIndex],
                            maxTrials: maxTrialsSpin.value,
                            startDate: backtestParams.startDate || "2020-01-01",
                            endDate: backtestParams.endDate || "2025-12-31",
                            initialCapital: backtestParams.initialCapital || 1000000,
                            datasetCacheId: backtestParams.datasetCacheId || -1,
                            strategyName: root.strategyName
                        }
                        root.tuningStarted(config)
                        root.close()
                    }
                }
            }
        }
    }

    // ── 引用共享样式按钮 ──
    component ButtonPrimary: Rectangle {
        property string text: ""
        property bool enabled: true
        signal clicked()

        width: 120; height: 36; radius: 8
        color: enabled ? root.accentBlue : root.bgTertiary
        opacity: enabled ? 1.0 : 0.5

        Text {
            anchors.centerIn: parent
            text: parent.text
            color: "white"
            font.pixelSize: 13
            font.bold: true
        }
        MouseArea {
            anchors.fill: parent
            enabled: parent.enabled
            onClicked: parent.clicked()
        }
    }

    component ButtonSecondary: Rectangle {
        property string text: ""
        signal clicked()

        width: 80; height: 36; radius: 8
        color: "transparent"
        border.width: 1; border.color: root.borderColor

        Text {
            anchors.centerIn: parent
            text: parent.text
            color: root.textSecondary
            font.pixelSize: 13
        }
        MouseArea {
            anchors.fill: parent
            onClicked: parent.clicked()
        }
    }
}

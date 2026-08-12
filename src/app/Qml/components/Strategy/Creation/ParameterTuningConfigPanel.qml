// ParameterTuningConfigPanel.qml — v0.16.0 重写
// 参数自动调优配置面板 — 独立弹窗, 动态加载, 不阻塞策略库页面
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

Popup {
    id: root

    // ── 外部输入 ──
    property string strategyId: ""
    property int strategyType: Bridge.StrategyTypes.StrategyType.DoubleMovingAverage
    property string strategyName: ""
    property var backtestParams: ({})
    property var paramRanges: []
    property int totalCombinations: 0
    property bool ready: false

    // ── 配置结果信号 ──
    signal tuningStarted(var config)

    width: 640
    height: 560
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
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
        radius: 14
        border.width: 1
        border.color: root.borderColor
    }

    // ── 打开时刷新参数 ──
    onOpened: {
        if (!Bridge.ParameterTuningBridge) return
        paramRanges = Bridge.ParameterTuningBridge.getTuningParamRanges(strategyType)
        totalCombinations = Bridge.ParameterTuningBridge.estimateCombinations(strategyType)
        ready = true
    }

    onClosed: { ready = false }

    // ═══════════════════════════════════════════════════
    // 主布局
    // ═══════════════════════════════════════════════════
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 标题栏 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20; anchors.rightMargin: 16
                anchors.topMargin: 4; anchors.bottomMargin: 0

                Text {
                    text: "参数自动调优"
                    color: root.textPrimary
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }

                Item { Layout.fillWidth: true }

                // 关闭按钮
                Rectangle {
                    width: 28; height: 28; radius: 14
                    color: mouseClose.containsMouse ? "#334155" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"; font.pixelSize: 14; color: root.textSecondary
                    }

                    MouseArea {
                        id: mouseClose
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.close()
                    }
                }
            }
        }

        // 分隔线
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.borderColor
            opacity: 0.4
        }

        // ── 可滚动内容 ──
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 14
                anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.topMargin: 16

                // ── 策略信息卡片 ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: root.bgSecondary
                    radius: 8
                    border.width: 1
                    border.color: Qt.rgba(71/255, 85/255, 105/255, 0.3)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14

                        Text {
                            text: "策略:"
                            color: root.textSecondary
                            font.pixelSize: 13
                        }
                        Text {
                            text: (strategyName || strategyId || "未选择")
                            color: root.textPrimary
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            radius: 4; width: 8; height: 8
                            color: root.successGreen
                        }
                        Text {
                            text: Bridge.StrategyBridge.strategyTypeName(strategyType)
                            color: root.textSecondary
                            font.pixelSize: 12
                        }
                    }
                }

                // ── 配置区卡片 ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: configCard.implicitHeight + 32
                    color: root.bgSecondary
                    radius: 10
                    border.width: 1
                    border.color: Qt.rgba(71/255, 85/255, 105/255, 0.3)

                    ColumnLayout {
                        id: configCard
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 14

                        // 优化算法
                        // 优化算法
                        Text {
                            text: "优化算法"
                            color: root.textSecondary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        ComboBox {
                            id: optimizerCombo
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            leftPadding: 12
                            model: ["网格搜索 (GridSearch)", "贝叶斯优化 (Bayesian)"]
                            background: Rectangle {
                                color: root.bgPrimary
                                border.color: root.borderColor
                                border.width: 1
                                radius: 8
                            }
                            contentItem: Text {
                                text: optimizerCombo.displayText
                                color: root.textPrimary
                                font.pixelSize: 13
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 0
                            }
                        }

                        // 优化目标
                        Text {
                            text: "优化目标"
                            color: root.textSecondary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        ComboBox {
                            id: objectiveCombo
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            leftPadding: 12
                            model: ["夏普比率", "年化收益率", "卡玛比率", "索提诺比率", "盈亏比"]
                            background: Rectangle {
                                color: root.bgPrimary
                                border.color: root.borderColor
                                border.width: 1
                                radius: 8
                            }
                            contentItem: Text {
                                text: objectiveCombo.displayText
                                color: root.textPrimary
                                font.pixelSize: 13
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 0
                            }
                        }

                        // 最大试运行次数
                        Text {
                            text: "最大试运行次数 (0=全部 " + totalCombinations + " 种)"
                            color: root.textSecondary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        SpinBox {
                            id: maxTrialsSpin
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            from: 0
                            to: Math.min(totalCombinations, 100000)
                            value: 0
                            stepSize: 50
                            contentItem: TextInput {
                                text: maxTrialsSpin.value
                                color: root.textPrimary
                                font.pixelSize: 13
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                            }
                            background: Rectangle {
                                color: root.bgPrimary
                                border.color: root.borderColor
                                border.width: 1
                                radius: 8
                            }
                        }

                        // 组合爆炸警告
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: totalCombinations > 10000 ? 32 : 0
                            visible: totalCombinations > 10000
                            color: "#78350F"
                            radius: 6

                            Text {
                                anchors.centerIn: parent
                                text: "⚠ 共 " + totalCombinations + " 种组合，可能耗时较长"
                                color: root.warningOrange
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                // ── 参数空间预览 ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: paramSection.implicitHeight + 20
                    visible: paramRanges.length > 0
                    color: root.bgSecondary
                    radius: 10
                    border.width: 1
                    border.color: Qt.rgba(71/255, 85/255, 105/255, 0.3)

                    ColumnLayout {
                        id: paramSection
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 8

                        RowLayout {
                            Text {
                                text: "参数空间"
                                color: root.textSecondary
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            Rectangle {
                                radius: 4; Layout.preferredWidth: 8; Layout.preferredHeight: 8
                                color: totalCombinations > 10000 ? root.warningOrange : root.accentBlue
                            }
                            Text {
                                text: paramRanges.length + " 维 × " + totalCombinations + " 组合"
                                color: totalCombinations > 10000 ? root.warningOrange : root.accentBlue
                                font.pixelSize: 11
                            }
                        }

                        Repeater {
                            model: paramRanges

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                color: index % 2 === 0 ? Qt.rgba(51/255, 65/255, 85/255, 0.3) : "transparent"
                                radius: 4

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 10

                                    Text {
                                        text: modelData.name || ""
                                        color: root.textPrimary
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        Layout.preferredWidth: 140
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: formatParamRange(modelData)
                                        color: root.textSecondary
                                        font.pixelSize: 11
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: "×" + (modelData.candidateCount || 0)
                                        color: root.accentBlue
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: 4 }
            }
        }

        // ── 底部操作栏 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "transparent"

            Rectangle {
                anchors.top: parent.top
                width: parent.width; height: 1
                color: root.borderColor
                opacity: 0.4
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16

                Text {
                    text: totalCombinations > 0
                        ? "预计 " + totalCombinations + " 种组合"
                        : "无可用参数范围"
                    color: root.textSecondary
                    font.pixelSize: 12
                }

                Item { Layout.fillWidth: true }

                // 取消按钮
                Rectangle {
                    Layout.preferredWidth: 80; Layout.preferredHeight: 36
                    radius: 8; color: "transparent"
                    border.width: 1; border.color: root.borderColor

                    Text {
                        anchors.centerIn: parent
                        text: "取消"; color: root.textSecondary; font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.close()
                    }
                }

                // 开始调优按钮
                Rectangle {
                    Layout.preferredWidth: 120; Layout.preferredHeight: 36
                    radius: 8
                    color: canStart ? root.accentBlue : root.bgTertiary
                    opacity: canStart ? 1.0 : 0.5

                    property bool canStart: totalCombinations > 0
                        && (!Bridge.ParameterTuningBridge || !Bridge.ParameterTuningBridge.isRunning)

                    Text {
                        anchors.centerIn: parent
                        text: "开始调优"
                        color: "white"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: parent.canStart
                        onClicked: {
                            root.tuningStarted({
                                strategyId: root.strategyId,
                                strategyType: root.strategyType,
                                optimizerKind: optimizerCombo.currentIndex,
                                objectiveMetric: ["sharpe","annualizedReturn","calmar","sortino","profitFactor"][objectiveCombo.currentIndex],
                                maxTrials: maxTrialsSpin.value,
                                startDate: backtestParams.startDate || "2020-01-01",
                                endDate: backtestParams.endDate || "2025-12-31",
                                initialCapital: backtestParams.initialCapital || 1000000,
                                datasetCacheId: backtestParams.datasetCacheId || -1,
                                strategyName: root.strategyName
                            })
                            root.close()
                        }
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════
    // 工具函数
    // ═══════════════════════════════════════════════════
    function formatParamRange(p) {
        switch (p.type) {
        case 0: return "整数 [" + p.min + " → " + p.max + "] 步长 " + p.step
        case 1: return "浮点 [" + p.min.toFixed(2) + " → " + p.max.toFixed(2) + "] 步长 " + p.step.toFixed(2)
        case 2: return "布尔"
        case 3: return "枚举 (" + (p.options ? p.options.length : 0) + " 选项)"
        default: return ""
        }
    }

}

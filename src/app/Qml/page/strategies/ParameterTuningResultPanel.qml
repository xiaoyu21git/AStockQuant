// ParameterTuningResultPanel.qml
// 参数调优结果面板 — 进度条 + 最优参数 + 试运行排名
// 连接到 ParameterTuningBridge 的单例信号

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import AStock.Bridge 1.0 as Bridge

Rectangle {
    id: root

    property bool isRunning: false
    property var tuningResult: null
    property string statusText: ""

    // ── 主题色 ──
    readonly property color bgPrimary: "#0F172A"
    readonly property color bgSecondary: "#1E293B"
    readonly property color bgTertiary: "#334155"
    readonly property color borderColor: "#475569"
    readonly property color accentBlue: "#3B82F6"
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color successGreen: "#10B981"
    readonly property color lossRed: "#EF4444"
    readonly property color warningOrange: "#F59E0B"

    color: bgPrimary

    // ── 连接 Bridge 信号 ──
    Connections {
        target: Bridge.ParameterTuningBridge
        enabled: Bridge.ParameterTuningBridge !== null

        function onIsRunningChanged() {
            root.isRunning = Bridge.ParameterTuningBridge.isRunning
        }
        function onCurrentTrialChanged() {
            // 触发进度更新
        }
        function onProgressChanged() {
            // 触发进度条更新
        }
        function onStatusChanged() {
            root.statusText = Bridge.ParameterTuningBridge.status
        }
        function onTuningCompleted(result) {
            root.tuningResult = result
            root.isRunning = false
        }
        function onTuningFailed(error) {
            root.statusText = "失败: " + error
            root.isRunning = false
        }
        function onTuningCancelled() {
            root.statusText = "已取消"
            root.isRunning = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 标题栏 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12

                Text {
                    text: "参数调优结果"
                    color: root.textPrimary
                    font.pixelSize: 16
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                // 取消按钮
                Rectangle {
                    visible: root.isRunning
                    width: cancelBtnText.implicitWidth + 20
                    height: 30
                    radius: 6
                    color: root.bgTertiary
                    border.width: 1; border.color: root.borderColor

                    Text {
                        id: cancelBtnText
                        anchors.centerIn: parent
                        text: "取消"
                        color: root.textSecondary
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: Bridge.ParameterTuningBridge.cancelTuning()
                    }
                }
            }
        }

        // ── 进度区 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.isRunning ? 80 : 0
            visible: root.isRunning
            color: root.bgSecondary
            radius: 8
            Layout.margins: 12

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: root.statusText || "调优中..."
                        color: root.textPrimary
                        font.pixelSize: 14
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Bridge.ParameterTuningBridge
                            ? Bridge.ParameterTuningBridge.currentTrial + " / " + Bridge.ParameterTuningBridge.totalTrials
                            : "0 / 0"
                        color: root.textSecondary
                        font.pixelSize: 12
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    radius: 4
                    color: root.bgTertiary

                    Rectangle {
                        height: 8
                        width: parent.width * (Bridge.ParameterTuningBridge
                            ? Bridge.ParameterTuningBridge.progress / 100.0 : 0)
                        radius: 4
                        color: root.accentBlue
                    }
                }
            }
        }

        // ── 结果区 ──
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            visible: !root.isRunning && root.tuningResult !== null

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                // 最优参数卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    color: root.bgSecondary
                    radius: 10
                    border.width: 1
                    border.color: root.successGreen

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 6

                        RowLayout {
                            Text {
                                text: "最优参数组合"
                                color: root.textPrimary
                                font.pixelSize: 15
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: root.tuningResult
                                    ? "目标值: " + (root.tuningResult.bestObjectiveValue || 0).toFixed(4)
                                    : ""
                                color: root.successGreen
                                font.pixelSize: 14
                                font.bold: true
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 6

                            Repeater {
                                model: root.tuningResult && root.tuningResult.bestParams
                                    ? Object.keys(root.tuningResult.bestParams) : []

                                Rectangle {
                                    width: labelText.implicitWidth + 20
                                    height: 26
                                    radius: 13
                                    color: root.bgTertiary

                                    Text {
                                        id: labelText
                                        anchors.centerIn: parent
                                        text: modelData + ": " + (root.tuningResult.bestParams[modelData] || 0).toFixed(2)
                                        color: root.textPrimary
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }

                // 统计摘要
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Repeater {
                        model: [
                            { label: "总次数", value: root.tuningResult ? root.tuningResult.totalTrials || 0 : 0 },
                            { label: "失败", value: root.tuningResult ? root.tuningResult.failedTrials || 0 : 0 },
                            { label: "违规", value: root.tuningResult ? root.tuningResult.constraintViolations || 0 : 0 },
                            { label: "耗时(秒)", value: root.tuningResult ? (root.tuningResult.elapsedSeconds || 0).toFixed(1) : 0 }
                        ]

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 56
                            color: root.bgSecondary
                            radius: 8

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2

                                Text {
                                    text: modelData.value
                                    color: root.textPrimary
                                    font.pixelSize: 18
                                    font.bold: true
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                                Text {
                                    text: modelData.label
                                    color: root.textSecondary
                                    font.pixelSize: 11
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }
                        }
                    }
                }

                // 试运行排名表
                Text {
                    text: "试运行排名 (按目标值降序)"
                    color: root.textSecondary
                    font.pixelSize: 12
                    font.bold: true
                }

                // 表头
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#1A2236"
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Text { text: "#"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 30; font.bold: true }
                        Text { text: "目标值"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 80; font.bold: true }
                        Text { text: "参数摘要"; color: root.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; font.bold: true }
                        Text { text: "状态"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 60; font.bold: true }
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.tuningResult ? (root.tuningResult.trials || []) : []

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 36
                        color: index % 2 === 0 ? root.bgSecondary : "#1A2236"
                        radius: 4

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 8

                            Text {
                                text: (modelData.trialIndex || 0)
                                color: modelData.isViable ? root.textPrimary : root.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 30
                            }

                            Text {
                                text: modelData.objectiveValue !== undefined
                                    ? modelData.objectiveValue.toFixed(4)
                                    : "N/A"
                                color: modelData.isViable ? root.successGreen : root.lossRed
                                font.pixelSize: 12
                                Layout.preferredWidth: 80
                                font.bold: true
                            }

                            Text {
                                text: {
                                    if (!modelData.params) return ""
                                    let keys = Object.keys(modelData.params)
                                    return keys.slice(0, 4).map(k =>
                                        k + "=" + modelData.params[k].toFixed(1)
                                    ).join("  ")
                                }
                                color: root.textSecondary
                                font.pixelSize: 11
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: modelData.constraintViolated ? "违规" :
                                      (modelData.errorMessage ? "失败" : "通过")
                                color: modelData.isViable ? root.successGreen : root.warningOrange
                                font.pixelSize: 11
                                Layout.preferredWidth: 60
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {}
                }
            }
        }

        // ── 空状态 ──
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.isRunning && root.tuningResult === null

            Text {
                anchors.centerIn: parent
                text: root.statusText || "等待调优启动..."
                color: root.textSecondary
                font.pixelSize: 14
            }
        }
    }
}

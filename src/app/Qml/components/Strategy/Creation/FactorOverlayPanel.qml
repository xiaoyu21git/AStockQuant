import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot

    // 数据流入（从 root 派生的只读快照）
    required property var factorOverlayState
    required property int cardMinWidth
    required property int cardMaxWidth

    // 因子卡片宽度计算
    function factorOverlayCardWidth(containerWidth) {
        var wb = Math.max(0, Number(containerWidth) || 0)
        if (wb <= 0) return cardMinWidth
        if (wb >= cardMinWidth * 2 + 12)
            return Math.min(Math.max(cardMinWidth, (wb - 12) / 2), cardMaxWidth)
        return Math.min(wb, cardMaxWidth)
    }

    Layout.fillWidth: true
    radius: 10
    color: "#0b1220"
    border.width: 1
    border.color: root.factorOverlay.enabled ? "#0ea5e9" : "#334155"
    implicitHeight: factorOverlayColumn.implicitHeight + 20

    ColumnLayout {
        id: factorOverlayColumn
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "因子排序层"
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: "#f1f5f9"
            }

            Rectangle {
                radius: 9
                color: "#1e293b"
                border.width: 1
                border.color: "#334155"
                implicitWidth: modeChipText.implicitWidth + 14
                implicitHeight: 22

                Text {
                    id: modeChipText
                    anchors.centerIn: parent
                    text: "规则先筛选，因子后排序"
                    font.pixelSize: 10
                    color: "#93c5fd"
                }
            }

            Item { Layout.fillWidth: true }

            Switch {
                id: factorOverlaySwitch
                checked: !!root.factorOverlay.enabled
                onCheckedChanged: {
                    root.factorOverlay.enabled = checked
                    root.factorOverlay = root.normalizeFactorOverlay(root.factorOverlay)
                    root.syncDecoratedParameters()
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.factorOverlay.enabled
                  ? "规则模板只负责放行/否决，因子层只在同一交易日的合格候选之间做排序和持仓数量裁剪。"
                  : "未启用因子排序层时，当前策略仅按规则模板和基础参数运行。"
            font.pixelSize: 11
            color: root.factorOverlay.enabled ? "#bae6fd" : "#94a3b8"
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: root.factorOverlay.enabled

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "选择因子"
                    onClicked: root.openFactorSelector()
                }

                Button {
                    text: "等权重"
                    enabled: (root.factorOverlay.allocations || []).length > 0
                    onClicked: root.rebalanceFactorOverlayWeights()
                }

                Button {
                    text: "清空"
                    enabled: (root.factorOverlay.allocations || []).length > 0
                    onClicked: root.clearFactorOverlayAllocations()
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "已选 " + ((root.factorOverlay.allocations || []).length) + " 个因子"
                    font.pixelSize: 11
                    color: "#cbd5e1"
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 12
                rowSpacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "因子组合模式"
                        font.pixelSize: 11
                        color: "#cbd5e1"
                    }

                    ComboBox {
                        id: combineModeCombo
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "value"
                        model: [
                            { label: "纯排名 (RankOnly)", value: "rank_only" },
                            { label: "交集 (Intersection)", value: "intersection" },
                            { label: "并集 (Union)", value: "union" },
                            { label: "配额 (Quota)", value: "quota" }
                        ]
                        currentIndex: {
                            var mode = String(root.factorOverlay.combineMode || "rank_only")
                            if (mode === "intersection") return 1
                            if (mode === "union") return 2
                            if (mode === "quota") return 3
                            return 0
                        }
                        onCurrentIndexChanged: {
                            var item = model[currentIndex]
                            if (item) {
                                root.factorOverlay.combineMode = item.value
                                root.syncDecoratedParameters()
                            }
                        }
                    }

                    Text {
                        text: currentIndex === 0
                              ? "所有标的按因子分排名"
                              : currentIndex === 1
                                ? "所有因子条件都满足才进池"
                                : currentIndex === 2
                                  ? "任一因子条件满足即可进池"
                                  : "各因子独立排名, 按权重比例分池"
                        font.pixelSize: 10
                        color: "#64748b"
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 100
                    spacing: 4

                    Text {
                        text: "最低综合分"
                        font.pixelSize: 11
                        color: "#cbd5e1"
                    }

                    TextField {
                        id: factorMinimumScoreField
                        Layout.fillWidth: true
                        text: String(root.factorOverlay.minimumCompositeScore || 0)
                        placeholderText: "0"
                        onEditingFinished: {
                            var parsed = Number(text)
                            root.factorOverlay.minimumCompositeScore = isNaN(parsed) ? 0 : parsed
                            text = String(root.factorOverlay.minimumCompositeScore)
                            root.syncDecoratedParameters()
                        }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 110
                    spacing: 4

                    Text {
                        text: "目标持仓数"
                        font.pixelSize: 11
                        color: "#cbd5e1"
                    }

                    TextField {
                        id: targetPositionField
                        Layout.fillWidth: true
                        text: String(root.factorOverlay.targetPositionCount || 50)
                        placeholderText: "50"
                        onEditingFinished: {
                            var parsed = parseInt(text, 10)
                            root.factorOverlay.targetPositionCount = (isNaN(parsed) || parsed < 1) ? 50 : parsed
                            text = String(root.factorOverlay.targetPositionCount)
                            root.syncDecoratedParameters()
                        }
                    }

                    Text {
                        text: "候选池大小"
                        font.pixelSize: 10
                        color: "#64748b"
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: (root.factorOverlay.allocations || []).length > 0

                Text {
                    Layout.fillWidth: true
                    text: "已选因子"
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: "#f8fafc"
                }

                Item {
                    Layout.fillWidth: true
                    implicitHeight: factorAllocationFlow.implicitHeight

                    Flow {
                        id: factorAllocationFlow
                        width: parent.width
                        spacing: 10

                        Repeater {
                            model: root.factorOverlay.allocations || []

                            delegate: Rectangle {
                                required property int index
                                required property var modelData

                                width: factorOverlayCardWidth(factorAllocationFlow.width)
                                radius: 12
                                color: "#0b1220"
                                border.width: 1
                                border.color: "#1f3b5b"
                                implicitHeight: factorAllocationColumn.implicitHeight + 16

                                ColumnLayout {
                                    id: factorAllocationColumn
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 6

                                    RowLayout {
                                        width: parent.width
                                        spacing: 8

                                        Rectangle {
                                            radius: 9
                                            color: "#0ea5e9"
                                            border.width: 1
                                            border.color: "#38bdf8"
                                            implicitWidth: 42
                                            implicitHeight: 18

                                            Text {
                                                anchors.centerIn: parent
                                                text: "已选"
                                                font.pixelSize: 9
                                                color: "white"
                                            }
                                        }

                                        Item { Layout.fillWidth: true }

                                        Button {
                                            text: "移除"
                                            onClicked: root.removeFactorOverlayAllocation(index)
                                        }
                                    }

                                    Text {
                                        width: parent.width
                                        text: modelData.display_name || modelData.factor_id || "未命名因子"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "#e2e8f0"
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        text: modelData.factor_id || ""
                                        font.pixelSize: 9
                                        color: "#94a3b8"
                                        wrapMode: Text.WrapAnywhere
                                    }

                                    RowLayout {
                                        width: parent.width
                                        spacing: 8

                                        Rectangle {
                                            Layout.preferredWidth: 96
                                            implicitHeight: 28
                                            radius: 8
                                            color: "#1a2332"
                                            border.width: 1
                                            border.color: "#334155"

                                            TextField {
                                                anchors.fill: parent
                                                anchors.leftMargin: 6
                                                anchors.rightMargin: 6
                                                text: String(modelData.weight_percent !== undefined ? modelData.weight_percent : 0)
                                                placeholderText: "权重%"
                                                horizontalAlignment: Text.AlignRight
                                                verticalAlignment: Text.AlignVCenter
                                                color: "#e2e8f0"
                                                font.pixelSize: 10
                                                background: null
                                                onEditingFinished: root.updateFactorOverlayWeight(index, text)
                                            }
                                        }

                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            Repeater {
                                                model: [
                                                    { label: "排序因子", fg: "#93c5fd", bg: "#0f172a" },
                                                    { label: "权重 " + String(modelData.weight_percent !== undefined ? modelData.weight_percent : 0) + "%", fg: "#fde68a", bg: "#2a2110" }
                                                ]

                                                delegate: Rectangle {
                                                    required property var modelData
                                                    radius: 9
                                                    color: modelData.bg
                                                    border.width: 1
                                                    border.color: Qt.darker(modelData.bg, 1.12)
                                                    implicitWidth: chipText.implicitWidth + 10
                                                    implicitHeight: chipText.implicitHeight + 6

                                                    Text {
                                                        id: chipText
                                                        anchors.centerIn: parent
                                                        text: modelData.label
                                                        font.pixelSize: 8
                                                        color: modelData.fg
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.factorOverlayErrors().length > 0
                text: root.factorOverlayErrors().join("；")
                font.pixelSize: 11
                color: "#fca5a5"
                wrapMode: Text.WordWrap
            }
        }
    }
}

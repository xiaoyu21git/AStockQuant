import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

/// @brief 绩效归因面板 — 板块/因子/择时(Brinson)三维拆解
/// 数据源: Bridge 层 attributionReportToMap → backtestResult.attribution
Rectangle {
    id: root

    /// 归因数据 (来自 backtestResult.attribution)
    property var attrData: ({})

    /// 是否可见 (数据就绪时自动显示)
    readonly property bool hasData: attrData && attrData.isValid === true

    Layout.fillWidth: true
    implicitHeight: hasData ? attributionContent.implicitHeight + 24 : 0
    Layout.preferredHeight: implicitHeight
    radius: 10
    color: "#0F172A"
    border.width: 1
    border.color: "#1F2937"
    visible: hasData

    ColumnLayout {
        id: attributionContent
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        visible: root.hasData

        // ── 标题行 ──
        RowLayout {
            spacing: 8
            Text {
                text: "📊 绩效归因"
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.timingLabel()
                font.pixelSize: 11
                color: "#94A3B8"
            }
        }

        // ── 选项卡 ──
        TabBar {
            id: attrTabBar
            Layout.fillWidth: true
            background: Rectangle { color: "#111827"; radius: 6 }

            TabButton {
                text: "🏢 板块归因"
                font.pixelSize: 12
                contentItem: Text {
                    text: parent.text; font: parent.font
                    color: parent.checked ? "#F8FAFC" : "#94A3B8"
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: parent.checked ? "#1E3A5F" : "transparent"
                    radius: 6
                }
            }
            TabButton {
                text: "🧬 因子归因"
                font.pixelSize: 12
                contentItem: Text {
                    text: parent.text; font: parent.font
                    color: parent.checked ? "#F8FAFC" : "#94A3B8"
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: parent.checked ? "#1E3A5F" : "transparent"
                    radius: 6
                }
            }
            TabButton {
                text: "⏱ 择时归因"
                font.pixelSize: 12
                contentItem: Text {
                    text: parent.text; font: parent.font
                    color: parent.checked ? "#F8FAFC" : "#94A3B8"
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: parent.checked ? "#1E3A5F" : "transparent"
                    radius: 6
                }
            }
        }

        StackLayout {
            id: attrStack
            Layout.fillWidth: true
            currentIndex: attrTabBar.currentIndex

            // ──────────────────────────────────────────────
            // Tab 0: 板块归因 — 水平条形图
            // ──────────────────────────────────────────────
            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true

                // 表头
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Item { Layout.preferredWidth: 90
                        Text { text: "行业"; font.pixelSize: 11; color: "#94A3B8" } }
                    Item { Layout.fillWidth: true
                        Text { text: "贡献占比"; font.pixelSize: 11; color: "#94A3B8" } }
                    Item { Layout.preferredWidth: 60
                        Text { text: "交易笔数"; font.pixelSize: 11; color: "#94A3B8" } }
                    Item { Layout.preferredWidth: 72
                        Text { text: "贡献%"; font.pixelSize: 11; color: "#94A3B8" } }
                }

                Repeater {
                    model: root.sortedSectors()

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: "transparent"

                        RowLayout {
                            anchors.fill: parent
                            spacing: 12

                            // 行业名称
                            Item {
                                Layout.preferredWidth: 90
                                Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.sectorName || "--"
                                    font.pixelSize: 12; color: "#E2E8F0"
                                    elide: Text.ElideRight
                                    width: parent.width - 4
                                }
                            }

                            // 条形图
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.max(4, parent.width * root.barScale(modelData))
                                    height: 16
                                    radius: 3
                                    color: root.barColor(modelData)
                                }
                            }

                            // 交易笔数
                            Item {
                                Layout.preferredWidth: 60
                                Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.tradeCount || 0
                                    font.pixelSize: 12; color: "#94A3B8"
                                }
                            }

                            // 贡献百分比
                            Item {
                                Layout.preferredWidth: 72
                                Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.fmtPct(modelData.returnContribution)
                                    font.pixelSize: 12; font.weight: Font.Medium
                                    color: root.barColor(modelData)
                                }
                            }
                        }
                    }
                }

                // 因子近似估算 提示
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    radius: 4; color: "#1A2332"
                    visible: root.sortedSectors().length === 0
                    Text {
                        anchors.centerIn: parent
                        text: "暂无板块归因数据 (需配置行业查询回调)"
                        font.pixelSize: 11; color: "#64748B"
                    }
                }
            }

            // ──────────────────────────────────────────────
            // Tab 1: 因子归因 — 表格
            // ──────────────────────────────────────────────
            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true

                // 表头
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Item { Layout.preferredWidth: 110
                        Text { text: "因子ID"; font.pixelSize: 11; color: "#94A3B8" } }
                    Item { Layout.preferredWidth: 50
                        Text { text: "权重"; font.pixelSize: 11; color: "#94A3B8" } }
                    Item { Layout.preferredWidth: 50
                        Text { text: "IC"; font.pixelSize: 11; color: "#94A3B8" } }
                    Item { Layout.preferredWidth: 50
                        Text { text: "覆盖天数"; font.pixelSize: 11; color: "#94A3B8" } }
                    Item { Layout.fillWidth: true
                        Text { text: "估算贡献"; font.pixelSize: 11; color: "#94A3B8" } }
                    Item { Layout.preferredWidth: 55
                        Text { text: "方法"; font.pixelSize: 11; color: "#94A3B8" } }
                }

                Repeater {
                    model: root.attrData.factorBreakdown || []

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: "transparent"

                        RowLayout {
                            anchors.fill: parent
                            spacing: 10

                            Item {
                                Layout.preferredWidth: 110; Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.factorId || "--"
                                    font.pixelSize: 12; color: "#E2E8F0"
                                    elide: Text.ElideRight; width: parent.width - 4
                                }
                            }
                            Item {
                                Layout.preferredWidth: 50; Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.fmtNum(modelData.factorWeight, 3)
                                    font.pixelSize: 12; color: "#CBD5E1"
                                }
                            }
                            Item {
                                Layout.preferredWidth: 50; Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.fmtNum(modelData.factorIC, 4)
                                    font.pixelSize: 12; color: "#CBD5E1"
                                }
                            }
                            Item {
                                Layout.preferredWidth: 50; Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.coveredDays || 0
                                    font.pixelSize: 12; color: "#CBD5E1"
                                }
                            }
                            Item {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.fmtNum(modelData.estimatedContribution, 6)
                                    font.pixelSize: 12; font.weight: Font.Medium
                                    color: modelData.estimatedContribution >= 0 ? "#EF4444" : "#10B981"
                                }
                            }
                            Item {
                                Layout.preferredWidth: 55; Layout.fillHeight: true
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.estimationMethod === 0 ? "近似" : "精确"
                                    font.pixelSize: 11
                                    color: modelData.estimationMethod === 0 ? "#F59E0B" : "#10B981"
                                }
                            }
                        }
                    }
                }

                // 近似估算说明
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 28
                    radius: 4; color: "#1A2332"
                    Text {
                        anchors.centerIn: parent
                        text: "⚠ 因子贡献为近似估算值 (权重 × IC × 覆盖率)，仅供参考。精确计算需对接因子暴露矩阵 (Phase 2)"
                        font.pixelSize: 10; color: "#F59E0B"
                    }
                }
            }

            // ──────────────────────────────────────────────
            // Tab 2: 择时归因 (Brinson)
            // ──────────────────────────────────────────────
            ColumnLayout {
                spacing: 8
                Layout.fillWidth: true

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    BacktestStatItem {
                        label: "组合收益"
                        value: root.fmtPct(root.timingData().portfolioReturn)
                        valueColor: (root.timingData().portfolioReturn || 0) >= 0 ? "#EF4444" : "#10B981"
                    }
                    BacktestStatItem {
                        label: "基准收益"
                        value: root.fmtPct(root.timingData().benchmarkReturn)
                        valueColor: (root.timingData().benchmarkReturn || 0) >= 0 ? "#EF4444" : "#10B981"
                    }
                    BacktestStatItem {
                        label: "超额收益"
                        value: root.fmtPct(root.timingData().excessReturn)
                        valueColor: (root.timingData().excessReturn || 0) >= 0 ? "#EF4444" : "#10B981"
                    }
                    BacktestStatItem {
                        label: "模式"
                        value: root.timingData().isSimplified ? "简化版" : "完整Brinson"
                        valueColor: root.timingData().isSimplified ? "#F59E0B" : "#10B981"
                    }

                    BacktestStatItem {
                        label: "配置效应"
                        value: root.fmtPct(root.timingData().allocationEffect)
                        valueColor: "#CBD5E1"
                    }
                    BacktestStatItem {
                        label: "选股效应"
                        value: root.fmtPct(root.timingData().selectionEffect)
                        valueColor: "#CBD5E1"
                    }
                    BacktestStatItem {
                        label: "交互效应"
                        value: root.fmtPct(root.timingData().interactionEffect)
                        valueColor: "#CBD5E1"
                    }
                    Item { Layout.fillWidth: true }
                }

                // 简化模式说明
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 28
                    radius: 4; color: "#1A2332"
                    visible: root.timingData().isSimplified === true
                    Text {
                        anchors.centerIn: parent
                        text: "⚠ 简化模式: 配置/选股/交互效应待基准行业数据就绪后启用 (Phase 2)"
                        font.pixelSize: 10; color: "#F59E0B"
                    }
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    // 辅助函数 (每个 < 30 行, 符合 CLAUDE.md)
    // ══════════════════════════════════════════════════════════

    function sortedSectors() {
        return root.attrData.sectorBreakdown || []
    }

    function timingData() {
        return root.attrData.timingBreakdown || {}
    }

    function timingLabel() {
        var td = root.timingData()
        if (!td || Object.keys(td).length === 0) return ""
        var er = root.fmtPct(td.excessReturn || 0)
        var tag = td.isSimplified ? "简化" : "完整"
        return "超额: " + er + " (" + tag + ")"
    }

    function barScale(sector) {
        var sectors = root.sortedSectors()
        if (sectors.length === 0) return 0
        var maxAbs = 0
        for (var i = 0; i < sectors.length; i++) {
            maxAbs = Math.max(maxAbs, Math.abs(sectors[i].returnContribution || 0))
        }
        return maxAbs > 0 ? Math.abs(sector.returnContribution || 0) / maxAbs : 0
    }

    function barColor(sector) {
        return (sector.returnContribution || 0) >= 0 ? "#EF4444" : "#10B981"
    }

    function fmtPct(val) {
        if (val === undefined || val === null || isNaN(Number(val))) return "--"
        return (Number(val)).toFixed(2) + "%"
    }

    function fmtNum(val, dec) {
        if (val === undefined || val === null || isNaN(Number(val))) return "--"
        return Number(val).toFixed(dec || 2)
    }
}

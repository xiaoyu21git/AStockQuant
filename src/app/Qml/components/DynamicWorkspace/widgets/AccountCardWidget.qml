import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

// ============================================================================
// AccountCardWidget — 账户概览 (参考 FundOperationPanel 卡片风格)
// 数据源: PositionAccountBridge + TradeExecutionBridge
// ============================================================================

Item {
    id: root
    property var widgetConfig: ({})
    clip: true

    readonly property real scaleFactor: Math.min(1.2, Math.max(0.6, height / 300))
    function cs(v) { return Math.max(1, Math.round(v * scaleFactor)) }

    // ── Bridge 数据 ──
    property var snap: Bridge.PositionAccountBridge
        && Bridge.PositionAccountBridge.accountSnapshot
        ? Bridge.PositionAccountBridge.accountSnapshot : ({})
    property var positions: Bridge.PositionAccountBridge
        && Bridge.PositionAccountBridge.positions
        ? Bridge.PositionAccountBridge.positions : []
    property var orders: Bridge.TradeExecutionBridge
        && Bridge.TradeExecutionBridge.recentOrders
        ? Bridge.TradeExecutionBridge.recentOrders : []

    Connections {
        target: Bridge.PositionAccountBridge
        function onAccountSnapshotChanged() { root.snap = Bridge.PositionAccountBridge.accountSnapshot || ({}); }
        function onPositionsChanged() { root.positions = Bridge.PositionAccountBridge.positions || []; }
    }
    Connections {
        target: Bridge.TradeExecutionBridge
        function onRecentOrdersChanged() { root.orders = Bridge.TradeExecutionBridge.recentOrders || []; }
    }

    // ── 派生数据 ──
    readonly property double totalAsset: Number(snap.totalAsset || snap.total_asset || 0)
    readonly property double availableCash: Number(snap.availableCash || snap.available || 0)
    readonly property double marketValue: Number(snap.marketValue || snap.market_value || 0)
    readonly property double frozenCash: Number(snap.frozenCash || snap.frozen || 0)
    readonly property double unrealizedPnl: Number(snap.unrealizedPnl || snap.unrealized_pnl || 0)
    readonly property double todayPnl: Number(snap.todayPnl || snap.daily_pnl || 0)
    readonly property color riseColor: "#ef4444"
    readonly property color fallColor: "#10b981"
    readonly property int positionCount: Array.isArray(positions) ? positions.length : 0

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: cs(8)

            // ── 账户净值主卡片 (FundOperationPanel 风格) ──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: cs(120)
                radius: 12
                color: "#1a2235"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: cs(16)
                    spacing: cs(6)

                    RowLayout {
                        Text {
                            text: "账户净值"
                            color: "#94a3b8"
                            font.pixelSize: cs(13)
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: new Date().toLocaleTimeString(Qt.locale(), "hh:mm")
                            color: "#64748b"
                            font.pixelSize: cs(11)
                        }
                    }

                    Text {
                        text: totalAsset > 0
                            ? "¥ " + totalAsset.toLocaleString(Qt.locale(), 'f', 2) : "——"
                        color: "#f1f5f9"
                        font.pixelSize: cs(28)
                        font.weight: Font.Bold
                    }

                    RowLayout {
                        spacing: cs(10)
                        visible: todayPnl !== 0

                        Text {
                            text: todayPnl >= 0
                                ? "↗ +¥" + todayPnl.toLocaleString(Qt.locale(), 'f', 2)
                                : "↘ ¥" + todayPnl.toLocaleString(Qt.locale(), 'f', 2)
                            color: todayPnl >= 0 ? riseColor : fallColor
                            font.pixelSize: cs(13)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: (todayPnl / Math.max(totalAsset - todayPnl, 1) * 100).toFixed(2) + "%"
                            color: todayPnl >= 0 ? riseColor : fallColor
                            font.pixelSize: cs(12)
                            visible: totalAsset > 0
                        }
                    }
                }
            }

            // ── 资金指标卡片网格 ──
            GridLayout {
                Layout.fillWidth: true
                columns: width > 300 ? 3 : 2
                columnSpacing: cs(6)
                rowSpacing: cs(6)

                Repeater {
                    model: [
                        { label: "可用资金", value: availableCash, color: "#3b82f6" },
                        { label: "持仓市值", value: marketValue, color: "#8b5cf6" },
                        { label: "冻结资金", value: frozenCash, color: "#64748b" },
                        { label: "浮动盈亏", value: unrealizedPnl, color: unrealizedPnl >= 0 ? fallColor : riseColor },
                        { label: "持仓数", value: positionCount, color: "#f59e0b" },
                        { label: "今盈亏", value: todayPnl, color: todayPnl >= 0 ? fallColor : riseColor }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: cs(58)
                        radius: 8
                        color: "#1e293b"
                        border.width: 1
                        border.color: "#334155"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: cs(10)
                            spacing: cs(2)

                            Text {
                                text: modelData.label
                                color: "#94a3b8"
                                font.pixelSize: cs(11)
                            }

                            Text {
                                text: typeof modelData.value === "number" && modelData.value !== 0
                                    ? (modelData.value >= 1e4
                                        ? (modelData.value / 1e4).toFixed(2) + "万"
                                        : modelData.value.toFixed(2))
                                    : (typeof modelData.value === "number" ? "0.00" : String(modelData.value))
                                color: modelData.color
                                font.pixelSize: cs(15)
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
        }
    }
}

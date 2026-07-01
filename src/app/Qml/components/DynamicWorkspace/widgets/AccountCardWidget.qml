import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

// ============================================================================
// AccountCardWidget — 账户概览 (FundOperationPanel 卡片风格)
// ============================================================================

Item {
    id: root
    property var widgetConfig: ({})
    clip: true

    function cs(v) { return Math.max(1, Math.round(v * Math.min(1.1, Math.max(0.65, height / 300)))) }

    // ── Bridge 数据 (直接绑定, QML 自动追踪) ──
    readonly property var snap: (Bridge.PositionAccountBridge && Bridge.PositionAccountBridge.accountSnapshot)
        ? Bridge.PositionAccountBridge.accountSnapshot : ({})
    readonly property var positions: (Bridge.PositionAccountBridge && Bridge.PositionAccountBridge.positions)
        ? Bridge.PositionAccountBridge.positions : []

    readonly property double totalAsset: Number(snap.totalAsset || 0)
    readonly property double availableCash: Number(snap.availableCash || 0)
    readonly property double marketValue: Number(snap.marketValue || 0)
    readonly property double unrealizedPnl: Number(snap.unrealizedPnl || 0)
    readonly property int positionCount: Array.isArray(positions) ? positions.length : 0

    // ── 空状态 ──
    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: 12
        color: "#1a2235"
        visible: totalAsset <= 0

        Text {
            anchors.centerIn: parent
            text: "等待账户数据..."
            color: "#64748b"
            font.pixelSize: 14
        }
    }

    // ── 主内容 ──
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: cs(6)
        visible: totalAsset > 0

        // 净值主卡片
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: cs(90)
            radius: 12
            color: "#1a2235"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: cs(12)
                spacing: cs(3)

                Text {
                    text: "账户净值"
                    color: "#94a3b8"
                    font.pixelSize: cs(12)
                    font.weight: Font.DemiBold
                }

                Text {
                    text: "¥ " + totalAsset.toLocaleString(Qt.locale(), 'f', 2)
                    color: "#f1f5f9"
                    font.pixelSize: cs(24)
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Text {
                    text: unrealizedPnl >= 0
                        ? "↗ +¥" + unrealizedPnl.toLocaleString(Qt.locale(), 'f', 2)
                        : "↘ ¥" + unrealizedPnl.toLocaleString(Qt.locale(), 'f', 2)
                    color: unrealizedPnl >= 0 ? "#ef4444" : "#10b981"
                    font.pixelSize: cs(12)
                    visible: unrealizedPnl !== 0
                }
            }
        }

        // 指标网格
        GridLayout {
            Layout.fillWidth: true
            columns: width > 260 ? 3 : 2
            columnSpacing: cs(4)
            rowSpacing: cs(4)

            Repeater {
                model: [
                    { label: "可用", value: availableCash, color: "#3b82f6" },
                    { label: "市值", value: marketValue, color: "#8b5cf6" },
                    { label: "持仓", value: positionCount, color: "#f59e0b" },
                    { label: "浮盈", value: unrealizedPnl, color: unrealizedPnl >= 0 ? "#ef4444" : "#10b981" }
                ]

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: cs(44)
                    radius: 8
                    color: "#1e293b"
                    border.width: 1
                    border.color: "#334155"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: cs(8)
                        spacing: 0

                        Text {
                            text: modelData.label
                            color: "#64748b"
                            font.pixelSize: cs(10)
                        }

                        Text {
                            text: typeof modelData.value === "number"
                                ? (Math.abs(modelData.value) >= 1e4
                                    ? (modelData.value / 1e4).toFixed(1) + "万"
                                    : modelData.value.toFixed(0))
                                : String(modelData.value)
                            color: modelData.color
                            font.pixelSize: cs(13)
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }
    }
}

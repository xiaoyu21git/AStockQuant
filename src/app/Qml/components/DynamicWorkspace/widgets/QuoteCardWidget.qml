import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

// ============================================================================
// QuoteCardWidget — 报价卡片 (日内价格区间布局)
// 数据源: MarketDataBridge.primarySymbol → marketSnapshots
// ============================================================================

Item {
    id: root
    property var widgetConfig: ({})
    clip: true

    // 自适应缩放
    readonly property real scaleFactor: Math.min(1.2, Math.max(0.6, height / 280))
    function cs(v) { return Math.max(1, Math.round(v * scaleFactor)) }

    // ── 行情数据 ──
    readonly property string sym: Bridge.MarketDataBridge
        ? (Bridge.MarketDataBridge.primarySymbol || "000001.SZ") : "000001.SZ"
    readonly property var snap: Bridge.MarketDataBridge
        && Bridge.MarketDataBridge.marketSnapshots
        ? (Bridge.MarketDataBridge.marketSnapshots[sym] || ({})) : ({})

    Connections {
        target: Bridge.MarketDataBridge
        function onMarketSnapshotsChanged() { root.sym; root.snap; }
        function onPrimarySymbolChanged() { root.sym; root.snap; }
    }

    // ── 派生数据 ──
    readonly property double price: Number(snap.price || 0)
    readonly property double open: Number(snap.open || 0)
    readonly property double high: Number(snap.high || 0)
    readonly property double low: Number(snap.low || 0)
    readonly property double preClose: Number(snap.preClose || 0)
    readonly property double volume: Number(snap.volume || 0)
    readonly property double changePct: Number(snap.changePct || snap.changePercent || 0)
    readonly property double limitPct: Number(snap.limitPct || 10)
    readonly property bool isLimitUp: Boolean(snap.limitUp || false)
    readonly property bool isLimitDown: Boolean(snap.limitDown || false)
    readonly property bool isRise: changePct >= 0
    readonly property color riseColor: "#ef4444"
    readonly property color fallColor: "#10b981"
    readonly property color priceColor: preClose > 0
        ? (isRise ? riseColor : fallColor) : "#f1f5f9"

    // ── 日内价格区间比例 ──
    readonly property double dayRange: high - low
    readonly property double pricePosInRange: dayRange > 0
        ? Math.max(0, Math.min(1, (price - low) / dayRange)) : 0.5

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: 12
        color: "#1a2235"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: cs(14)
            spacing: cs(6)

            // ── 标题行 ──
            RowLayout {
                spacing: cs(8)

                Text {
                    text: sym.replace(".SZ","").replace(".SH","").replace(".BJ","")
                    color: "#f1f5f9"
                    font.pixelSize: cs(15)
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    radius: 4
                    color: "#334155"
                    implicitWidth: tagText.width + cs(12)
                    implicitHeight: tagText.height + cs(4)

                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        text: sym.indexOf(".SH") >= 0 ? "沪" : (sym.indexOf(".SZ") >= 0 ? "深" : "京")
                        color: "#94a3b8"
                        font.pixelSize: cs(10)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // ── 当前价格 (大字) ──
            Text {
                text: price > 0 ? price.toFixed(2) : "——"
                color: priceColor
                font.pixelSize: cs(32)
                font.weight: Font.Bold
                Layout.alignment: Qt.AlignLeft
            }

            // ── 涨跌幅 ──
            RowLayout {
                spacing: cs(6)
                visible: preClose > 0

                Rectangle {
                    radius: 4
                    color: isRise ? Qt.rgba(239/255, 68/255, 67/255, 0.15) : Qt.rgba(16/255, 185/255, 129/255, 0.15)
                    implicitWidth: changeText.width + cs(14)
                    implicitHeight: changeText.height + cs(6)

                    Text {
                        id: changeText
                        anchors.centerIn: parent
                        text: (isRise ? "+" : "") + changePct.toFixed(2) + "%"
                        color: priceColor
                        font.pixelSize: cs(14)
                        font.weight: Font.DemiBold
                    }
                }

                Text {
                    text: (isRise ? "+" : "") + (price - preClose).toFixed(2)
                    color: priceColor
                    font.pixelSize: cs(13)
                }

                // 涨跌停标记
                Rectangle {
                    visible: isLimitUp || isLimitDown
                    radius: 4
                    color: isLimitUp ? Qt.rgba(239/255, 68/255, 67/255, 0.25) : Qt.rgba(16/255, 185/255, 129/255, 0.25)
                    implicitWidth: limitText.width + cs(12)
                    implicitHeight: limitText.height + cs(6)

                    Text {
                        id: limitText
                        anchors.centerIn: parent
                        text: isLimitUp ? "涨停" : "跌停"
                        color: priceColor
                        font.pixelSize: cs(11)
                        font.weight: Font.Bold
                    }
                }
            }

            Item { Layout.preferredHeight: cs(4) }

            // ── 日内价格区间条 ──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: cs(26)
                radius: 6
                color: "#0f172a"
                visible: dayRange > 0

                // 低→高渐变条
                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: cs(8)
                    radius: 4
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: fallColor }
                        GradientStop { position: 1.0; color: riseColor }
                    }
                }

                // 当前价格指针
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Math.max(0, Math.min(parent.width - cs(3),
                        parent.width * pricePosInRange - cs(1.5)))
                    width: cs(3)
                    height: parent.height
                    radius: 2
                    color: priceColor
                    border.width: 1
                    border.color: "#f1f5f9"
                }
            }

            // ── OHLC + 成交量 ──
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: cs(4)
                rowSpacing: cs(2)

                Repeater {
                    model: [
                        { label: "开盘", value: open > 0 ? open.toFixed(2) : "——", color: open >= preClose && preClose > 0 ? riseColor : fallColor },
                        { label: "最高", value: high > 0 ? high.toFixed(2) : "——", color: "#f59e0b" },
                        { label: "最低", value: low > 0 ? low.toFixed(2) : "——", color: "#3b82f6" },
                        { label: "昨收", value: preClose > 0 ? preClose.toFixed(2) : "——", color: "#94a3b8" },
                        { label: "成交量", value: volume >= 1e8 ? (volume/1e8).toFixed(2)+"亿" : (volume >= 1e4 ? (volume/1e4).toFixed(0)+"万" : volume.toFixed(0)), color: "#cbd5e1" },
                        { label: "涨跌停", value: "±"+limitPct.toFixed(0)+"%", color: "#64748b" }
                    ]

                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            text: modelData.label
                            color: "#64748b"
                            font.pixelSize: cs(10)
                            Layout.alignment: Qt.AlignLeft
                        }
                        Text {
                            text: modelData.value
                            color: modelData.color
                            font.pixelSize: cs(12)
                            font.weight: Font.Medium
                            Layout.alignment: Qt.AlignRight
                        }
                    }
                }
            }
        }
    }
}

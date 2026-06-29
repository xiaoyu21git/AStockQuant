import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading" as Trading
import "../../../utils/TradingConstants.js" as Const

// ============================================================================
// ChartWorkspaceWidget — 图表工作区 (复合控件, 独立设计)
//
// 不套用 TradingWidgetBase, 内部独立响应式布局:
//   宽度 > 800px:  K线 75% + 盘口 25% 侧栏
//   宽度 500-800px: K线 100%, 盘口隐藏
//   宽度 < 500px:  K线 100% + 缩略信息条, 盘口隐藏
//
// 内部复用: DepthPanelWidget (Loader) + CompactChart
// 持仓联动条: 从 PositionAccountBridge 筛选当前 symbol 的持仓
//
// Bridges: MarketDataBridge + PositionAccountBridge
// ============================================================================

Rectangle {
    id: root
    property var widgetConfig: ({})
    color: Const.chartBg
    radius: 10
    border.color: Const.chartBorder
    border.width: 1
    clip: true

    // ============ 联动 symbol ============
    readonly property string chartSymbol: (widgetConfig && widgetConfig.symbol
        ? String(widgetConfig.symbol).trim().toUpperCase()
        : (Bridge.MarketDataBridge ? String(Bridge.MarketDataBridge.primarySymbol || "") : "")
        ) || "000001.SZ"

    // ============ 持仓联动数据 ============
    property var positions: Bridge.PositionAccountBridge
        ? (Bridge.PositionAccountBridge.positions || []) : []

    Connections {
        target: Bridge.PositionAccountBridge
        function onPositionsChanged() {
            positions = Bridge.PositionAccountBridge.positions || []
        }
    }

    // 当前 symbol 的持仓信息
    readonly property var linkedPosition: {
        for (var i = 0; i < positions.length; i++) {
            var sym = String(positions[i].symbol || "")
            if (sym.indexOf(chartSymbol.replace(/\.(SZ|SH|BJ)$/, "")) >= 0
                || sym === chartSymbol) {
                return positions[i]
            }
        }
        return null
    }

    // ============ 响应式开关 ============
    readonly property bool showDepthSidebar: width >= 800
    readonly property bool showPositionBar: width >= 500

    // ============ 布局 ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 2

        // --- 主区域: K线 + 盘口侧栏 ---
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            // K线主区
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#0b1220"
                radius: 6

                Trading.CompactChart {
                    anchors.fill: parent
                    anchors.margins: 2
                    symbol: chartSymbol
                    marketDataService: Bridge.MarketDataBridge
                    clip: true
                }
            }

            // 盘口侧栏 (宽度 >= 800px 时显示)
            Loader {
                Layout.preferredWidth: showDepthSidebar ? Math.min(280, root.width * 0.28) : 0
                Layout.fillHeight: true
                visible: showDepthSidebar
                active: showDepthSidebar

                source: "../widgets/DepthPanelWidget.qml"

                onLoaded: {
                    if (item) {
                        item.widgetConfig = ({ symbol: chartSymbol })
                    }
                }
            }
        }

        // --- 持仓联动条 (宽度 >= 500px) ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            radius: 4
            color: "#0f1726"
            border.color: "#223147"
            border.width: 1
            visible: showPositionBar

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 12

                Text {
                    text: chartSymbol
                    color: Const.chartLabelText
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                Text {
                    text: linkedPosition
                        ? "持仓 " + Number(linkedPosition.quantity || 0)
                          + " · 现价 " + Number(linkedPosition.lastPrice || 0).toFixed(2)
                          + " · 盈亏 " + (Number(linkedPosition.unrealizedPnl || 0) >= 0 ? "+" : "")
                          + Number(linkedPosition.unrealizedPnl || 0).toFixed(2)
                        : "未持仓"
                    color: linkedPosition
                        ? (Number(linkedPosition.unrealizedPnl || 0) >= 0 ? Const.chartProfitText : Const.chartLossText)
                        : Const.chartNeutralText
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    text: linkedPosition
                        ? "市值 " + (Number(linkedPosition.marketValue || 0) >= 1e4
                            ? (Number(linkedPosition.marketValue) / 1e4).toFixed(2) + "万"
                            : Number(linkedPosition.marketValue || 0).toFixed(2))
                        : ""
                    color: Const.chartLabelText
                    font.pixelSize: 10
                    visible: linkedPosition !== null
                }
            }
        }
    }
}

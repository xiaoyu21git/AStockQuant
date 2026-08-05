import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../utils/TradingConstants.js" as Const

// ============================================================================
// TradingPage — 精简为布局容器, 所有交易组件复用 DynamicWorkspace 的独立控件
// 每个控件内部直连 Bridge 获取数据, 页面只负责布局和标的同步
// ============================================================================

Item {
    id: root

    property var marketData: []

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // Row 1: 账户概览 (全宽)
        Loader {
            id: accountLoader
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            active: true
            source: "../../components/DynamicWorkspace/widgets/AccountCardWidget.qml"
        }

        // Row 2: K线 + (五档 | 下单)
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            // K线 — symbol 由 C++ primarySymbol 驱动
            Loader {
                id: klineLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: true
                source: "../../components/DynamicWorkspace/widgets/CandlestickChartWidget.qml"
            }

            // 右侧面板: 五档 + 下单
            ColumnLayout {
                Layout.preferredWidth: Math.max(280, parent.width * 0.28)
                Layout.fillHeight: true
                spacing: 6

                // 五档 — symbol 由 C++ primarySymbol 驱动
                Loader {
                    id: depthLoader
                    Layout.fillWidth: true
                    Layout.preferredHeight: 280
                    active: true
                    source: "../../components/DynamicWorkspace/widgets/DepthPanelWidget.qml"
                }

                // 下单 — 标的变更 → C++ ensureWatchSymbol
                Loader {
                    id: orderFormLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: true
                    source: "../../components/DynamicWorkspace/widgets/OrderFormWidget.qml"
                }
            }
        }

        // Row 3: 持仓 + 委托
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            spacing: 6

            Loader {
                id: positionLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: true
                source: "../../components/DynamicWorkspace/widgets/PositionListWidget.qml"
            }

            Loader {
                id: orderListLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: true
                source: "../../components/DynamicWorkspace/widgets/OrderListWidget.qml"
            }
        }

        // Row 4: 执行日志 + 策略监控
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            spacing: 6

            Loader {
                id: execLogLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: true
                source: "../../components/DynamicWorkspace/widgets/ExecutionLogWidget.qml"
            }

            Loader {
                id: strategyLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: true
                source: "../../components/DynamicWorkspace/widgets/StrategyMonitorWidget.qml"
            }
        }
    }
}

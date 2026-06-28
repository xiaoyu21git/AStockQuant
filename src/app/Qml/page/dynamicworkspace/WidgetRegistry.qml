pragma Singleton
import QtQuick 2.15

QtObject {
    id: root

    property var widgetTypes: ({
        "order_form": {
            label: "下单控件", icon: "📝",
            description: "完整下单面板 (TradingFormPanel)",
            defaultColSpan: 6, defaultRowSpan: 4,
            source: "../../components/DynamicWorkspace/widgets/OrderFormWidget.qml"
        },
        "kline_chart": {
            label: "K线图表", icon: "📈",
            description: "日/周/月K线 + 信号标注",
            defaultColSpan: 8, defaultRowSpan: 3,
            source: "../../components/DynamicWorkspace/widgets/KLineChartWidget.qml"
        },
        "depth_panel": {
            label: "五档盘口", icon: "📊",
            description: "实时五档买卖盘 + 逐笔成交",
            defaultColSpan: 4, defaultRowSpan: 3,
            source: "../../components/DynamicWorkspace/widgets/DepthPanelWidget.qml"
        },
        "account_card": {
            label: "账户概览", icon: "💰",
            description: "总资产/市值/可用/盈亏",
            defaultColSpan: 6, defaultRowSpan: 1,
            source: "../../components/DynamicWorkspace/widgets/AccountCardWidget.qml"
        },
        "strategy_status": {
            label: "策略状态", icon: "🎯",
            description: "策略运行规则评估面板",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/StrategyStatusWidget.qml"
        },
        "chart_workspace": {
            label: "图表工作区", icon: "📉",
            description: "K线 + 盘口 + 仓位联动",
            defaultColSpan: 12, defaultRowSpan: 3,
            source: "../../components/DynamicWorkspace/widgets/ChartWorkspaceWidget.qml"
        },
        "metric_card": {
            label: "指标卡片", icon: "📊",
            description: "自定义数值指标",
            defaultColSpan: 4, defaultRowSpan: 1,
            source: "../../components/DynamicWorkspace/widgets/MetricCardWidget.qml"
        },
        "positions": {
            label: "持仓概览", icon: "💰",
            description: "实时持仓列表 (代码/数量/市值/盈亏)",
            defaultColSpan: 8, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/PositionsWidget.qml"
        },
        "trading_summary": {
            label: "交易概览", icon: "📊",
            description: "账户统计 + 订单汇总 + 风险指标",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/TradingSummaryWidget.qml"
        },
        "trade_list": {
            label: "交易列表", icon: "📋",
            description: "实时委托/成交列表",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/TradeListWidget.qml"
        },
        "text_label": {
            label: "文本标签", icon: "📝",
            description: "自定义文本备注",
            defaultColSpan: 6, defaultRowSpan: 1,
            source: "../../components/DynamicWorkspace/widgets/TextLabelWidget.qml"
        }
    })

    function getWidgetMeta(typeName) {
        return root.widgetTypes[typeName] || null
    }

    function getWidgetTypes() {
        var result = []
        for (var key in root.widgetTypes) {
            var m = root.widgetTypes[key]
            result.push({
                typeName: key, label: m.label, icon: m.icon,
                description: m.description,
                defaultColSpan: m.defaultColSpan,
                defaultRowSpan: m.defaultRowSpan, source: m.source
            })
        }
        return result
    }
}

pragma Singleton
import QtQuick 2.15

QtObject {
    id: root

    property var widgetTypes: ({
        // ====================================================================
        // 保留 (不变)
        // ====================================================================
        "order_form": {
            label: "下单控件", icon: "📝",
            description: "完整下单面板 (股票/期货/融资/融券/期权)",
            defaultColSpan: 6, defaultRowSpan: 4,
            source: "../../components/DynamicWorkspace/widgets/OrderFormWidget.qml"
        },
        "kline_chart": {
            label: "K线图表", icon: "📈",
            description: "日/周/月K线 + 信号标注 + 十字光标 (重构中)",
            defaultColSpan: 8, defaultRowSpan: 3,
            source: "../../components/DynamicWorkspace/widgets/CandlestickChartWidget.qml"
        },

        // ====================================================================
        // 全新重做 (支持三级密度模式 + 响应式列裁剪)
        // ====================================================================
        "depth_panel": {
            label: "五档盘口", icon: "📊",
            description: "实时买卖盘口 + L2逐笔成交",
            defaultColSpan: 6, defaultRowSpan: 5,
            source: "../../components/DynamicWorkspace/widgets/DepthPanelWidget.qml"
        },
        "account_card": {
            label: "账户概览", icon: "💰",
            description: "总资产/总盈亏/总持仓/总市值 + 持仓买入卖出撤单Tab",
            defaultColSpan: 12, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/AccountCardWidget.qml"
        },
        "quote_card": {
            label: "报价卡片", icon: "🏷️",
            description: "当前价+涨跌幅+日内区间OHLC+成交量+涨跌停",
            defaultColSpan: 4, defaultRowSpan: 1,
            source: "../../components/DynamicWorkspace/widgets/QuoteCardWidget.qml"
        },
        "position_list": {
            label: "持仓列表", icon: "📋",
            description: "分组持仓 (股票/融资/期货/期权) + 排序 + 汇总条",
            defaultColSpan: 8, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/PositionListWidget.qml"
        },
        "order_list": {
            label: "委托成交", icon: "📜",
            description: "委托/成交双视图切换 + 状态颜色标识 + 列自适应",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/OrderListWidget.qml"
        },
        "strategy_monitor": {
            label: "策略监控", icon: "🎯",
            description: "策略运行状态 + 信号计数 + 规则命中历史",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/StrategyMonitorWidget.qml"
        },
        "execution_log": {
            label: "执行日志", icon: "📝",
            description: "订单全链路日志 (提交→回报→成交→拒绝)",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/ExecutionLogWidget.qml"
        },
        "chart_workspace": {
            label: "图表工作区", icon: "📉",
            description: "K线 + 盘口联动 + 持仓信息条 (重构中)",
            defaultColSpan: 12, defaultRowSpan: 3,
            source: "../../components/DynamicWorkspace/widgets/CandlestickChartWidget.qml"
        },

        // ====================================================================
        // 兼容别名 (旧持久化数据迁移)
        // ====================================================================
        "positions": {
            label: "持仓列表 (旧)", icon: "📋",
            description: "已迁移到 position_list",
            defaultColSpan: 8, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/PositionListWidget.qml"
        },
        "trade_list": {
            label: "交易列表 (旧)", icon: "📜",
            description: "已迁移到 order_list",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/OrderListWidget.qml"
        },
        "trading_summary": {
            label: "交易概览 (旧)", icon: "📊",
            description: "已合并到 account_card",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/AccountCardWidget.qml"
        },
        "strategy_status": {
            label: "策略状态 (旧)", icon: "🎯",
            description: "已迁移到 strategy_monitor",
            defaultColSpan: 6, defaultRowSpan: 2,
            source: "../../components/DynamicWorkspace/widgets/StrategyMonitorWidget.qml"
        }
    })

    function getWidgetMeta(typeName) {
        return root.widgetTypes[typeName] || null
    }

    function getWidgetTypes() {
        // 仅返回主类型 (排除兼容别名)
        var mainTypes = [
            "order_form", "kline_chart",
            "depth_panel", "account_card", "position_list", "order_list",
            "strategy_monitor", "execution_log", "chart_workspace"
        ]
        var result = []
        for (var i = 0; i < mainTypes.length; i++) {
            var key = mainTypes[i]
            var m = root.widgetTypes[key]
            if (m) {
                result.push({
                    typeName: key, label: m.label, icon: m.icon,
                    description: m.description,
                    defaultColSpan: m.defaultColSpan,
                    defaultRowSpan: m.defaultRowSpan, source: m.source
                })
            }
        }
        return result
    }
}

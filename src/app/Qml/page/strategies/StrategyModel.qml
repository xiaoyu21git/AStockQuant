// models/StrategyModel.qml
import QtQuick 2.15

ListModel {
    id: strategyModel
    
    // 添加策略数据
    function initialize() {
        clear();
        append({
            name: "双均线交叉策略",
            description: "基于短期均线和长期均线的交叉信号进行交易，适合趋势跟踪。",
            status: "running",
            returns: "+12.4%",
            maxDrawdown: "-8.2%",
            sharpeRatio: "2.1",
            winRate: "62.3%",
            tags: ["趋势跟踪", "股票", "日内"],
            runningDays: "32",
            tradesCount: "24",
            position: "$45,680",
            dailyPnL: "+$1,245"
        });
        
        append({
            name: "RSI超买超卖策略",
            description: "基于RSI指标的超买超卖信号进行反转交易，适合震荡市场。",
            status: "paused",
            returns: "+8.7%",
            maxDrawdown: "-6.5%",
            sharpeRatio: "1.8",
            winRate: "58.9%",
            tags: ["均值回归", "加密货币", "短线"],
            runningDays: "18",
            tradesCount: "15",
            position: "$0",
            dailyPnL: "$0"
        });
        
        append({
            name: "布林带突破策略",
            description: "基于布林带上下轨的突破信号进行趋势跟踪交易，波动率自适应。",
            status: "running",
            returns: "+15.2%",
            maxDrawdown: "-9.8%",
            sharpeRatio: "2.4",
            winRate: "54.7%",
            tags: ["突破交易", "期货", "趋势"],
            runningDays: "45",
            tradesCount: "32",
            position: "$68,920",
            dailyPnL: "+$2,310"
        });
        
        append({
            name: "机器学习预测策略",
            description: "基于LSTM神经网络的价格预测模型，自动学习和优化交易信号。",
            status: "stopped",
            returns: "+21.8%",
            maxDrawdown: "-11.3%",
            sharpeRatio: "2.9",
            winRate: "65.2%",
            tags: ["机器学习", "AI", "预测"],
            runningDays: "0",
            tradesCount: "0",
            position: "$0",
            dailyPnL: "$0"
        });
    }
    
    Component.onCompleted: initialize()
}
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15
import QtCharts 2.15
import ConsoleUi 1.0
ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    title: "QuantumTrader Pro - 专业量化交易平台"
    color: "#0a0f1a"
    
    // 属性定义
    property real accountValue: 1248450.85
    property real accountChange: 12580.45
    property real accountChangePercent: 1.02
    
    property var marketData: [
        {symbol: "AAPL", name: "苹果公司", price: 182.45, change: 2.34, color: "#3b82f6"},
        {symbol: "MSFT", name: "微软公司", price: 335.67, change: 1.28, color: "#10b981"},
        {symbol: "GOOGL", name: "谷歌", price: 138.92, change: -0.82, color: "#8b5cf6"},
        {symbol: "NVDA", name: "英伟达", price: 520.15, change: 4.21, color: "#3b82f6"},
        {symbol: "TSLA", name: "特斯拉", price: 245.30, change: 3.45, color: "#3b82f6"},
        {symbol: "AMZN", name: "亚马逊", price: 156.78, change: -0.56, color: "#10b981"}
    ]
    
    property var statusCards: [
        {title: "今日盈亏", value: "+$12,450.85", change: 1.02, icon: "chart-line", color: "#3b82f6"},
        {title: "总收益率", value: "28.45%", change: 2.3, icon: "percentage", color: "#10b981"},
        {title: "当前风险", value: "低风险", changeText: "良好", icon: "shield-alt", color: "#f59e0b"},
        {title: "策略运行", value: "8/12", changeText: "运行中", icon: "robot", color: "#3b82f6"}
    ]
    
    property var positions: [
        {symbol: "AAPL", shares: 100, avgPrice: 165.20, currentValue: 18245.00, pnl: 1725.00, color: "#3b82f6"},
        {symbol: "MSFT", shares: 80, avgPrice: 305.40, currentValue: 26853.60, pnl: 2415.60, color: "#10b981"},
        {symbol: "NVDA", shares: 40, avgPrice: 480.50, currentValue: 20806.00, pnl: 1586.00, color: "#8b5cf6"}
    ]
    
    property var strategies: [
        {name: "双均线策略", status: "running", stocks: "AAPL, MSFT", returns: 12.4, trades: 24},
        {name: "RSI策略", status: "paused", stocks: "GOOGL, NVDA", returns: -1.2, trades: 18},
        {name: "动量策略", status: "running", stocks: "TSLA, AMZN", returns: 8.7, trades: 16}
    ]
    
    RowLayout {
        anchors.fill: parent
        spacing: 0
        
        // 左侧边栏（从Sidebar.qml导入）
        Sidebar {
            width: 280
            Layout.fillHeight: true
            accountValue: window.accountValue
            accountChange: window.accountChange
            accountChangePercent: window.accountChangePercent
        }
        
        // 主内容区域
        MainContent {
            Layout.fillWidth: true
            Layout.fillHeight: true
            marketData: window.marketData
            statusCards: window.statusCards
            positions: window.positions
            strategies: window.strategies
        }
    }
    
    // 模拟数据更新
    Timer {
        interval: 3000
        running: true
        repeat: true
        onTriggered: {
            // 更新市场数据
            for (var i = 0; i < marketData.length; i++) {
                var change = (Math.random() - 0.5) * 0.5;
                marketData[i].price = marketData[i].price * (1 + change / 100);
                marketData[i].change = (Math.random() - 0.5) * 0.5;
            }
            
            // 触发UI更新
            marketDataChanged();
        }
    }
}
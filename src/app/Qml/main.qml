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
    // 去掉标题栏
    title: "量化交易系统"
    flags: Qt.Window | Qt.FramelessWindowHint
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
    ColumnLayout {
        anchors.fill: parent
        TopNavigation {
            Layout.fillWidth: true
            Layout.preferredHeight: 64  // 明确指定高度
            onSearchRequested: {
                // 处理搜索请求
            }
            onNotificationClicked: {
                // 处理通知点击
            }
            onSettingsClicked: {
                // 处理设置点击
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            
                   // === 侧边栏 ===
            Sidebar {
                id: sidebar
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                
                        // 连接侧边栏信号
                        onMenuClicked: function(menuCode, menuTitle) {
                            switchPage(menuCode, menuTitle)
            }
            
            onDepositClicked: {
                showDepositDialog()
            }
            
            onWithdrawClicked: {
                showWithdrawDialog()
            }
            
            onAccountRefreshClicked: {
                refreshAccountData()
            }
            
            onUserProfileClicked: {
                showUserProfile()
                }
            }
            
            // 主内容区域 - StackLayout 切换页面
            StackLayout {
                id: mainStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: sidebar.menuModel.primaryMenus.findIndex(function(item){ return item.code === sidebar.menuModel.currentPrimaryMenu })

                MainContent {
                    id: dashboardPage
                    marketData: window.marketData
                    statusCards: window.statusCards
                    positions: window.positions
                    strategies: window.strategies
                }
                StrategyLibraryPage{}
                Datamain{}
                //BacktestPage { id: backtestPage }
                // 可继续添加其它页面
            }
        }   
    }
     // === 初始化函数 ===
    Component.onCompleted: {
        console.log("应用程序启动")
        initializeApp()
                        }
     // === 应用初始化 ===
    function initializeApp() {
        // 模拟加载数据
        Qt.callLater(function() {
            // 1. 初始化账户数据
            sidebar.updateAccountData(1500000, 12500, 0.83)
            
            // 2. 初始化用户信息
            sidebar.updateUserInfo("高级交易员", "VIP版 · 在线", "AT")
            
            // 3. 设置初始菜单
            sidebar.setCurrentMenu("fund_management")
            
            // 4. 更新菜单角标（示例）
            sidebar.menuModel.updateBadge("risk_management", "5", false)
            sidebar.menuModel.updateBadge("trade_desk", "🔥", true)
            
            console.log("应用初始化完成")
            console.log("当前菜单:", sidebar.getCurrentMenu())
        })
        
    }
    // === 页面切换 ===
    property string currentPage: "dashboard" // 默认初始页面为仪表盘
        property int mainStackIndex: 0 // 当前页面索引，0为仪表盘
    function switchPage(menuCode, menuTitle) {
        // 菜单与页面索引映射
        var pageMap = {
            "dashboard": 0,
            "strategy_trade": 1,
            // 可继续扩展其它菜单与页面索引
        };
        var idx = pageMap[menuCode] !== undefined ? pageMap[menuCode] : 0;
        mainStackIndex = idx;
        console.log("切换页面:", menuCode, "页面索引:", idx);
    }
    // === 页面映射表 ===
    function getPageSource(menuCode) {
        // 策略相关菜单显示回测页面，其余显示交易台页面
        var strategyPages = [
            "strategy_trade",
            "strategy_backtest",
            "strategy_optimize",
            "strategy_library"
        ];
        if (strategyPages.indexOf(menuCode) !== -1) {
            return "qrc:/page/backtest/BacktestPage.qml";
        }
        // 其它菜单和默认都显示仪表盘页面
        return "qrc:/page/dashboard/MainContent.qml";
    }
    // === 业务功能函数 ===
    
    function showDepositDialog() {
        console.log("显示入金对话框")
        // 实现入金对话框
        var component = Qt.createComponent("dialogs/DepositDialog.qml")
        if (component.status === Component.Ready) {
            var dialog = component.createObject(mainWindow)
            dialog.open()
        }
    }
     function showWithdrawDialog() {
        console.log("显示出金对话框")
        // 实现出金对话框
        var component = Qt.createComponent("dialogs/WithdrawDialog.qml")
        if (component.status === Component.Ready) {
            var dialog = component.createObject(mainWindow)
            dialog.open()
                        }
                    }
     function refreshAccountData() {
        console.log("刷新账户数据")
        // 模拟API调用
        var mockValue = 1500000 + Math.random() * 100000
        var mockChange = Math.random() * 20000 - 10000
        var mockPercent = (mockChange / mockValue) * 100
        
        sidebar.updateAccountData(mockValue, mockChange, mockPercent)
        
        // 显示刷新提示
        showToast("账户数据已更新")
    }
     function showUserProfile() {
        console.log("显示用户资料")
        var component = Qt.createComponent("dialogs/UserProfileDialog.qml")
        if (component.status === Component.Ready) {
            var dialog = component.createObject(mainWindow)
            dialog.open()
                    }
                }
    function showToast(message) {
        // 简单的toast提示
        console.log("Toast:", message)
    }
}
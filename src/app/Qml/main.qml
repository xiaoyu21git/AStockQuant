import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15
import QtCharts 2.15
import ConsoleUi 1.0
import AStock.Bridge 1.0 as Bridge
import "./components/DataAnalysis" as DataAnalysis
import "./components/Factor" as FactorComponents
import "./page/backtest" as BacktestPages
import "./page/risk" as RiskPages
import "./page/settings" as SettingsPages
import "./page/strategies" as Strategies
import "./page/trading" as TradingPages
ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    // 去掉标题
    title: "量化交易系统"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#0F172A"
    
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
        spacing: 0
        
        // 顶部导航
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
        
        // 主内容区
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            
            // === 侧边栏 ===
            Sidebar {
                id: sidebar
                Layout.preferredWidth: 240
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
            
            // 右侧主内容区
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                
                // 量化交易流程流水线- 替换原来的GlobalWorkflow
                DataAnalysis.ProcessFlow {
                    id: processFlow
                    Layout.fillWidth: true
                    Layout.preferredHeight: 114
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6
                    linkedStep: workflowStepForMenu(currentMenuCode)
                    
                    // 连接信号
                    onStepActivated: function(stepIndex) {
                       // console.log("流程流水线步骤激活", stepIndex)
                        
                        // 根据步骤索引切换到相应的页面
                        switch(stepIndex) {
                            case 1: // 数据准备
                                sidebar.setCurrentMenu("data_dashboard")
                                break
                            case 2: // 策略开发
                                sidebar.setCurrentMenu("strategy_factor")
                                break
                            case 3: // 回测验证
                                sidebar.setCurrentMenu("strategy_backtest")
                                break
                            case 4: // 风险管理
                                sidebar.setCurrentMenu("risk_management")
                                break
                            case 5: // 实盘部署
                                sidebar.setCurrentMenu("live_trading")
                                break
                        }
                        
                        showNotification("切换到步骤" + stepIndex + ": " + getStepName(stepIndex))
                    }
                    
                    onStepActionTriggered: function(stepIndex, actionType) {
                        //console.log("流程步骤操作触发:", stepIndex, "类型:", actionType)
                        showNotification("执行步骤" + stepIndex + "操作: " + actionType)
                        
                        // 根据步骤索引执行相应的操作
                        switch(stepIndex) {
                            case 1: // 数据准备
                                if (actionType === "page") {
                                    switchPage("data_dashboard", "数据准备")
                                }
                                break
                            case 2: // 策略开发
                                if (actionType === "page") {
                                    switchPage("strategy_factor", "策略开发")
                                }
                                break
                            case 3: // 回测验证
                                if (actionType === "inline") {
                                    switchPage("strategy_backtest", "回测验证")
                                }
                                break
                            case 4: // 风险管理
                                if (actionType === "page") {
                                    switchPage("risk_management", "风险管理")
                                }
                                break
                            case 5: // 实盘部署
                                if (actionType === "external") {
                                    switchPage("live_trading", "实盘部署")
                                }
                                break
                        }
                    }
                    
                    onFlowCompleted: {
                        //.log("流程完成")
                        showNotification("恭喜！量化交易流程已完成")
                    }
                }
                
    // 主内容区域- StackLayout 切换页面
    StackLayout {
        id: mainStack
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.topMargin: 10  // 添加统一的顶部边距，确保所有页面与工作流导航栏保持一致的间距
        currentIndex: getStackIndex(currentMenuCode)

                    // 数据管理页面
                    Datamain {
                        id: dataManagementPage
                    }
                    
// 策略开发页面 - 直接使用StrategyLibraryPage
StrategyLibraryPage {
    id: strategyDevelopmentPage
}
                    
                    // 专业策略创建页面
                    Strategies.StrategyCreationPagePro {
                        id: strategyCreationProPage
                    }
                    
                    // 策略回测页面
                    BacktestPages.StrategyBacktestPage {
                        id: strategyBacktestPage
                    }
                    
                    // 因子库页面（按需加载，降低首屏初始化成本）
                    Loader {
                        id: factorWorkbenchLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 4 || item !== null
                        asynchronous: true
                        sourceComponent: factorWorkbenchLibraryComponent
                    }
                    
                    // 因子分析页面（按需加载，避免与因子库一起初始化）
                    Loader {
                        id: factorAnalysisPageLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 5 || item !== null
                        asynchronous: true
                        sourceComponent: factorWorkbenchAnalysisComponent
                    }
                    
                    // 组合构建页面
                    PortfolioBuilderPage {
                        id: portfolioBuilderPage

                        onRequestBacktest: function(strategyId, strategyName, backtestConfig) {
                            if (window && typeof window.handleStrategyBacktestRequest === "function") {
                                window.handleStrategyBacktestRequest(strategyId, strategyName, backtestConfig)
                            }
                        }
                    }
                    
                    // 风险管理页面
                    RiskPages.RiskConfigurationPage {
                        id: riskManagementPage
                    }
                    
                    // 独立交易执行页面
                    Loader {
                        id: tradingExecutionPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 8 || item !== null
                        asynchronous: true
                        sourceComponent: tradingExecutionPageComponent
                    }
                    
                    // 实盘交易总览页面
                    Loader {
                        id: liveTradingPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 9 || item !== null
                        asynchronous: true
                        sourceComponent: liveTradingPageComponent
                    }
                    
                    // 监控面板页面
                    Item {
                        id: monitoringPage
                        
                        Text {
                            anchors.centerIn: parent
                            text: "监控面板页面\n（开发中）"
                            font.pixelSize: 24
                            color: "white"
                        }
                    }
                    
                    // 系统设置页面
                    Loader {
                        id: settingsPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 11 || item !== null
                        asynchronous: true
                        sourceComponent: settingsPageComponent
                    }
                }
            }
        }   
    }

    Component {
        id: tradingExecutionPageComponent

        TradingPages.TradingPage {
            marketData: window.marketData
        }
    }

    Component {
        id: liveTradingPageComponent

        MainContent {
            marketData: window.marketData
            statusCards: window.statusCards
            positions: window.positions
            strategies: window.strategies
        }
    }

    Component {
        id: factorWorkbenchLibraryComponent

        FactorWorkbench {
            currentMode: "library"
        }
    }

    Component {
        id: factorWorkbenchAnalysisComponent

        FactorWorkbench {
            currentMode: "library"
        }
    }

    Component {
        id: settingsPageComponent

        SettingsPages.SystemSettingsPage {
            configService: Bridge.TradingConnectionConfigService
        }
    }

     // === 初始化函数 ===
    Component.onCompleted: {
       // console.log("应用程序启动")
       initializeApp()
                        }
    // === 应用初始化 ===
    function initializeApp() {
        // 模拟加载数据
        Qt.callLater(function() {
            // 1. 初始化账户数据
            sidebar.updateAccountData(1500000, 12500, 0.83)
            
            // 2. 初始化用户信息
            sidebar.updateUserInfo("高级交易员", "VIP· 在线", "AT")
            
            // 3. 设置初始菜单为数据管理
            sidebar.setCurrentMenu("data_dashboard")
            
            // 4. 更新菜单角标（示例）
            sidebar.menuModel.updateBadge("risk_monitoring", "!", false)  // 风险监控有警告
            sidebar.menuModel.updateBadge("alert_center", "3", false)     // 报警中心3个报警
            sidebar.menuModel.updateBadge("risk_management", "!", true)   // 风险管理一级菜单有警告
            
            console.log("应用初始化完成")
            console.log("当前菜单:", sidebar.getCurrentMenu())
        })
        
    }
    // === 页面切换 ===
    property string currentPage: "dashboard" // 默认初始页面为仪表盘
    property int mainStackIndex: 0 // 当前页面索引，为仪表盘
    property string currentMenuCode: "data_dashboard" // 当前菜单代码，用于StackLayout切换

    function workflowStepForMenu(menuCode) {
        var menuToStep = {
            "data_dashboard": 1,
            "data_management": 1,
            "data_export": 1,
            "strategy_factor": 2,
            "strategy_library": 2,
            "strategy_creation": 2,
            "strategy_creation_pro": 2,
            "factor_library": 2,
            "factor_analysis": 2,
            "portfolio_builder": 2,
            "strategy_backtest": 3,
            "strategy_optimization": 3,
            "risk_management": 4,
            "risk_configuration": 4,
            "risk_monitoring": 4,
            "stress_testing": 4,
            "risk_reporting": 4,
            "compliance_check": 4,
            "live_trading": 5,
            "trade_execution": 5,
            "position_management": 5,
            "fund_management": 5,
            "trade_records": 5,
            "performance_analysis": 5
        }

        return menuToStep[menuCode] || 1
    }
    
    function switchPage(menuCode, menuTitle) {
        console.log("切换页面:", menuCode, "菜单标题:", menuTitle)
        
        // 更新当前菜单代码
        currentMenuCode = menuCode
        
        // 确保侧边栏菜单状态同步
        if (sidebar && sidebar.setCurrentMenu) {
            sidebar.setCurrentMenu(menuCode)
        }
        
        // 同步工作流程与菜单
        syncWorkflowWithMenu(menuCode)
        
        // 显示通知
        showNotification("切换到页面: " + menuTitle)
    }
    // === 获取StackLayout索引 ===
    function getStackIndex(menuCode) {
        var menuToIndex = {
            "data_management": 0,      // 数据管理 -> Datamain (索引0)
            "strategy_factor": 1,      // 策略与因子 -> StrategyLibraryPage (索引1)
            "strategy_backtest": 3,    // 策略回测 -> StrategyBacktestPage (索引3)
            "factor_analysis": 5,      // 因子分析 -> FactorWorkbench (索引5)
            "portfolio_builder": 6,    // 组合构建 -> PortfolioBuilderPage (索引6)
            "risk_management": 7,      // 风险管理 -> riskManagementPage (索引7)
            "live_trading": 8,         // 实盘交易 -> TradingPage (索引8)
            "monitoring": 10,          // 监控面板 -> monitoringPage (索引10)
            "settings": 11             // 系统设置 -> settingsPage (索引11)
        };
        
        // 二级菜单映射到对应的页面
        var secondaryMenuToIndex = {
            "data_dashboard": 0,              // 数据看板 -> 数据管理 (索引0)
            "data_export": 0,                 // 数据导出 -> 数据管理 (索引0)
            "strategy_creation_pro": 2,       // 专业策略创建 -> StrategyCreationPagePro (索引2)
            "strategy_creation": 1,           // 策略创建 -> 策略与因子 (索引1)
            "strategy_optimization": 3,       // 策略优化 -> 策略回测 (索引3)
            "strategy_library": 1,            // 策略库 -> 策略与因子 (索引1)
            "strategy_backtest": 3,           // 策略回测 -> StrategyBacktestPage (索引3)
            "factor_library": 4,              // 因子库 -> 因子库工作台 (索引4)
            "factor_analysis": 5,             // 因子分析 -> FactorWorkbench (索引5)
            "risk_configuration": 7,          // 风险配置 -> 风险管理 (索引7)
            "risk_monitoring": 7,             // 风险监控 -> 风险管理 (索引7)
            "stress_testing": 7,              // 压力测试 -> 风险管理 (索引7)
            "risk_reporting": 7,              // 风险报告 -> 风险管理 (索引7)
            "compliance_check": 7,            // 合规检查 -> 风险管理 (索引7)
            "trade_execution": 8,             // 交易执行 -> 实盘交易 (索引8)
            "position_management": 9,         // 仓位管理 -> 实盘总览 (索引9)
            "fund_management": 9,             // 资金管理 -> 实盘总览 (索引9)
            "trade_records": 9,               // 交易记录 -> 实盘总览 (索引9)
            "performance_analysis": 9,        // 绩效分析 -> 实盘总览 (索引9)
            "real_time_monitoring": 10,       // 实时监控 -> 监控面板 (索引10)
            "alert_center": 10,               // 报警中心 -> 监控面板 (索引10)
            "system_status": 10,              // 系统状态 -> 监控面板 (索引10)
            "log_viewer": 10,                 // 日志查看 -> 监控面板 (索引10)
            "personal_settings": 11,          // 个人设置 -> 系统设置 (索引11)
            "trade_settings": 11,             // 交易设置 -> 系统设置 (索引11)
            "notification_settings": 11,      // 通知设置 -> 系统设置 (索引11)
            "permission_management": 11,      // 权限管理 -> 系统设置 (索引11)
            "system_configuration": 11        // 系统配置 -> 系统设置 (索引11)
        };
        
        // 首先检查一级菜单
        var index = menuToIndex[menuCode];
        if (index === undefined) {
            // 如果不是一级菜单，检查二级菜单映射
            index = secondaryMenuToIndex[menuCode];
        }
        
        if (index === undefined) {
            console.log("未找到菜单对应的页面索引，使用默认索引0，菜单代码:", menuCode);
            return 0;
        }
        
        console.log("菜单代码:", menuCode, "-> 页面索引:", index);
        return index;
    }
    
    // === 页面映射表 ===
    function getPageSource(menuCode) {
         if (menuCode === "trade_execution") {
            return "qrc:/page/trading/TradingPage.qml";
        }
        if (menuCode === "strategy_backtest" || menuCode === "strategy_optimization") {
            return "qrc:/ConsoleUi/Qml/page/backtest/StrategyBacktestPage.qml";
        }
        if (menuCode === "strategy_library" || menuCode === "strategy_creation") {
            return "qrc:/ConsoleUi/Qml/page/strategies/StrategyLibraryPage.qml";
        }
        if (menuCode === "factor_analysis" || menuCode === "factor_library") {
            return "qrc:/ConsoleUi/Qml/page/FactorWorkbench.qml";
        }
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
    
    // === 全局工作流程辅助函数 ===
    function getStepName(stepIndex) {
        var stepNames = ["数据准备", "策略开发", "回测验证", "风险管理", "实盘部署"]
        return stepIndex >= 1 && stepIndex <= 5 ? stepNames[stepIndex - 1] : "未知步骤"
    }
    
    function showNotification(message) {
        console.log("通知:", message)
        // 这里可以实现更复杂的通知系统
        // 暂时使用简单的日志记录
    }
    
    // === 处理策略回测请求 ===
    function handleStrategyBacktestRequest(strategyId, strategyName, backtestConfig) {
        console.log("处理策略回测请求，策略ID:", strategyId, "策略名称:", strategyName)
        
        // 切换到策略回测页面
        switchPage("strategy_backtest", "策略回测")
        
        // 将策略ID传递给回测页面
        if (strategyBacktestPage && typeof strategyBacktestPage.setSelectedStrategy === 'function') {
            strategyBacktestPage.setSelectedStrategy(strategyId, strategyName, backtestConfig)
        }
        
        // 显示通知
        showNotification("正在切换到策略回测页面，准备测试策略: " + strategyName)
    }
    
    // === 同步工作流程与菜单 ===
    function syncWorkflowWithMenu(menuCode) {
        console.log("同步工作流程与菜单", menuCode)

        var step = workflowStepForMenu(menuCode)
        if (step && processFlow.currentStep !== step) {
            processFlow.goToStep(step)
            console.log("流程流水线同步到步骤:", step)
        }
    }
}
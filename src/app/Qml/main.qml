import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15
import QtCharts 2.15
import ConsoleUi 1.0
import "./components/DataAnalysis" as DataAnalysis
import "./components/Factor" as FactorComponents
ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    // 去掉标题�?
    title: "量化交易系统"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#0F172A"
    
    // 属性定�?
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
        
        // 顶部导航�?
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
        
        // 主内容区�?
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            
            // === 侧边�?===
            Sidebar {
                id: sidebar
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                
                // 连接侧边栏信�?
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
            
            // 右侧主内容区�?
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                
                // 量化交易流程流水�?- 替换原来的GlobalWorkflow
                DataAnalysis.ProcessFlow {
                    id: processFlow
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140  // 缩小高度，与ProcessFlow.qml保持一�?
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.topMargin: 10
                    Layout.bottomMargin: 20  // 增加底部边距，与下方页面保持距离
                    
                    // 连接信号
                    onStepActivated: function(stepIndex) {
                        console.log("流程流水线步骤激�?", stepIndex)
                        
                        // 根据步骤索引切换到相应的页面
                        switch(stepIndex) {
                            case 1: // 数据准备
                                sidebar.setCurrentMenu("data_dashboard")
                                break
                            case 2: // 策略开�?
                                sidebar.setCurrentMenu("strategy_development")
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
                        console.log("流程步骤操作触发:", stepIndex, "类型:", actionType)
                        showNotification("执行步骤" + stepIndex + "操作: " + actionType)
                        
                        // 根据步骤索引执行相应的操�?
                        switch(stepIndex) {
                            case 1: // 数据准备
                                if (actionType === "modal") {
                                    // 这里可以触发数据源添加弹�?
                                    if (dataManagementPage && typeof dataManagementPage.showAddDataSourcePopup === 'function') {
                                        dataManagementPage.showAddDataSourcePopup()
                                    }
                                }
                                break
                            case 2: // 策略开�?
                                if (actionType === "page") {
                                    // 这里可以触发因子分析页面
                                    showNotification("打开因子分析页面")
                                }
                                break
                            case 3: // 回测验证
                                if (actionType === "inline") {
                                    // 这里可以触发回测配置
                                    showNotification("展开回测配置面板")
                                }
                                break
                            case 4: // 风险管理
                                if (actionType === "page") {
                                    // 这里可以触发风险管理页面
                                    showNotification("打开风险管理页面")
                                }
                                break
                            case 5: // 实盘部署
                                if (actionType === "external") {
                                    showNotification("连接到实盘交易系�?..")
                                }
                                break
                        }
                    }
                    
                    onFlowCompleted: {
                        console.log("流程完成")
                        showNotification("恭喜！量化交易流程已完成")
                    }
                }
                
    // 主内容区�?- StackLayout 切换页面
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
                    
                    // 策略开发页�?
                    StrategyLibraryPage {
                        id: strategyDevelopmentPage
                    }
                    
                    // 因子库页面（使用统一工作台）
                    FactorWorkbench {
                        id: factorWorkbench
                        // 当从因子库菜单进入时，默认显示home模式（首页）
                        currentMode: "home"
                    }
                    
                    // 因子分析页面（使用统一工作台）
                    FactorWorkbench {
                        id: factorAnalysisPage
                        // 当从因子分析菜单进入时，默认显示home模式
                                    currentMode: "home"
                    }
                    
                    // 组合构建页面
                    PortfolioBuilderPage {
                        id: portfolioBuilderPage
                    }
                    
                    // 风险管理页面（暂时注释，待QML类型注册问题解决�?
                    // RiskConfigurationPage {
                    //     id: riskManagementPage
                    // }
                    
                    // 临时占位页面
                    Item {
                        id: riskManagementPage
                        
                        Text {
                            anchors.centerIn: parent
                            text: "风险管理页面\n（开发中）"
                            font.pixelSize: 24
                            color: "white"
                        }
                    }
                    
                    // 实盘交易页面
                    MainContent {
                        id: liveTradingPage
                        marketData: window.marketData
                        statusCards: window.statusCards
                        positions: window.positions
                        strategies: window.strategies
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
                    Item {
                        id: settingsPage
                        
                        Text {
                            anchors.centerIn: parent
                            text: "系统设置页面\n（开发中）"
                            font.pixelSize: 24
                            color: "white"
                        }
                    }
                }
            }
        }   
    }
     // === 初始化函�?===
    Component.onCompleted: {
        console.log("应用程序启动")
       initializeApp()
                        }
    // === 应用初始�?===
    function initializeApp() {
        // 模拟加载数据
        Qt.callLater(function() {
            // 1. 测试因子组件是否加载（恢复完整测试以诊断问题�?
            console.log("=== 测试因子组件加载 ===")
            try {
                // 测试FactorDesignSystem (Singleton)
                // if (typeof FactorDesignSystem !== 'undefined') {
                //     console.log("✅ FactorDesignSystem loaded successfully")
                //     console.log("  - textPrimary color:", FactorDesignSystem.textPrimary)
                //     console.log("  - factorMomentum color:", FactorDesignSystem.factorMomentum)
                // } else {
                //     console.log("❌ FactorDesignSystem not defined")
                // }
                
                // 测试创建FactorCard组件
                var component = Qt.createComponent("qrc:/ConsoleUi/Qml/components/Factor/FactorCard.qml")
                if (component.status === Component.Ready) {
                    console.log("✅ FactorCard component ready")
                    // 可以创建但不显示，只测试
                    var testCard = component.createObject(null, {visible: false})
                    if (testCard) {
                        console.log("  - FactorCard created successfully")
                        testCard.destroy()
                    }
                } else {
                    console.log("❌ FactorCard component error:", component.errorString())
                }
                
                // 测试FactorLibraryPage
                var pageComponent = Qt.createComponent("qrc:/ConsoleUi/Qml/components/FactorWorkbench/Library/FactorLibraryPage.qml")
                if (pageComponent.status === Component.Ready) {
                    console.log("✅ FactorLibraryPage component ready")
                } else {
                    console.log("❌ FactorLibraryPage component error:", pageComponent.errorString())
                }
                
                console.log("=== 因子组件测试完成 ===")
            } catch (e) {
                console.log("Error testing factor components:", e)
            }
            
            // 2. 初始化账户数�?
            sidebar.updateAccountData(1500000, 12500, 0.83)
            
            // 3. 初始化用户信息
            sidebar.updateUserInfo("高级交易员", "VIP· 在线", "AT")
            
            // 4. 设置初始菜单为数据管�?
            sidebar.setCurrentMenu("data_dashboard")
            
            // 5. 更新菜单角标（示例）
            sidebar.menuModel.updateBadge("risk_monitoring", "!", false)  // 风险监控有警�?
            sidebar.menuModel.updateBadge("alert_center", "3", false)     // 报警中心�?个报�?
            sidebar.menuModel.updateBadge("risk_management", "!", true)   // 风险管理一级菜单有警告
            
            console.log("应用初始化完成")
            console.log("当前菜单:", sidebar.getCurrentMenu())
        })
        
    }
    // === 页面切换 ===
    property string currentPage: "dashboard" // 默认初始页面为仪表盘
    property int mainStackIndex: 0 // 当前页面索引�?为仪表盘
    property string currentMenuCode: "data_dashboard" // 当前菜单代码，用于StackLayout切换
    
    function switchPage(menuCode, menuTitle) {
        console.log("切换页面:", menuCode, "菜单标题:", menuTitle)
        
        // 更新当前菜单代码
        currentMenuCode = menuCode
        
        // 确保侧边栏菜单状态同�?
        if (sidebar && sidebar.setCurrentMenu) {
            sidebar.setCurrentMenu(menuCode)
        }
        
        // 同步工作流程与菜�?
        syncWorkflowWithMenu(menuCode)
        
        // 显示通知
        showNotification("切换到页面: " + menuTitle)
    }
    // === 获取StackLayout索引 ===
    function getStackIndex(menuCode) {
        var menuToIndex = {
            "data_management": 0,      // 数据管理 -> Datamain (索引0)
            "strategy_development": 1, // 策略开�?-> StrategyLibraryPage (索引1)
            "portfolio_builder": 4,    // 组合构建 -> PortfolioBuilderPage (索引4)
            "risk_management": 5,      // 风险管理 -> riskManagementPage (索引5)
            "live_trading": 6,         // 实盘交易 -> liveTradingPage (索引6)
            "monitoring": 7,           // 监控面板 -> monitoringPage (索引7)
            "settings": 8              // 系统设置 -> settingsPage (索引8)
        };
        
        // 二级菜单映射到对应的页面
        var secondaryMenuToIndex = {
            "factor_analysis": 2,      // 因子分析 -> FactorWorkbench (索引2)
            "portfolio_composition": 4, // 组合构建 -> PortfolioBuilderPage (索引4)
            "data_dashboard": 0,       // 数据看板 -> 数据管理 (索引0)
            "data_source_management": 0, // 数据源管�?-> 数据管理 (索引0)
            "strategy_creation": 1,    // 策略创建 -> 策略开�?(索引1)
            "strategy_backtest": 1,    // 策略回测 -> 策略开�?(索引1)
            "strategy_optimization": 1, // 策略优化 -> 策略开�?(索引1)
            "strategy_library": 1      // 策略�?-> 策略开�?(索引1)
        };
        
        // 首先检查一级菜�?
        var index = menuToIndex[menuCode];
        if (index === undefined) {
            // 如果不是一级菜单，检查二级菜单映�?
            index = secondaryMenuToIndex[menuCode];
        }
        
        if (index === undefined) {
            console.log("未找到菜单对应的页面索引，使用默认索引0，菜单代码:", menuCode);
            return 0;
        }
        
        console.log("菜单代码:", menuCode, "-> 页面索引:", index);
        return index;
    }
    
    // === 页面映射�?===
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
        // 其它菜单和默认都显示仪表盘页�?
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
    
    // === 同步工作流程与菜�?===
    function syncWorkflowWithMenu(menuCode) {
        console.log("同步工作流程与菜�?", menuCode)
        
        var menuToStep = {
            "data_dashboard": 1,          // 数据准备
            "data_management": 1,         // 数据管理
            "strategy_development": 2,    // 策略开�?
            "strategy_backtest": 3,       // 回测验证
            "risk_management": 4,         // 风险管理
            "live_trading": 5             // 实盘部署
        };
        
        var step = menuToStep[menuCode];
        if (step && processFlow.currentStep !== step) {
            processFlow.goToStep(step)
            console.log("流程流水线同步到步骤:", step)
        }
    }
}

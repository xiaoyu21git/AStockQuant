import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0
import AStock.Bridge 1.0 as Bridge
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
    
    property var marketData: []
    
    property var statusCards: []
    
    property var positions: []
    
    property var strategies: []
    property string userName: "量化交易员"
    property string userStatus: "专业版 · 在线"
    property string userInitials: "QT"
    readonly property string factorWorkbenchRouteMode: window.currentMenuCode === "factor_analysis" ? "analyze" : "library"
    readonly property var strategyDevelopmentPage: strategyDevelopmentPageLoader.item
    readonly property var strategyCreationProPage: strategyCreationProPageLoader.item
    readonly property var factorWorkbenchPage: factorWorkbenchPageLoader.item
    readonly property var riskManagementPage: riskManagementPageLoader.item
    property var pendingRiskNavigationPayload: ({})
    property var pendingFactorWorkbenchRequest: ({})
    property var pendingStrategyImportRequest: ({})
    property var pendingStrategyBacktestRequest: ({})

    function openStrategyCreationFromFactorImport(importPayload) {
        var payload = importPayload && typeof importPayload === "object" ? importPayload : ({})
        if (Object.keys(payload).length === 0) {
            return
        }

        pendingStrategyImportRequest = payload
        window.switchPage("strategy_factor", "策略开发")

        if (strategyDevelopmentPage && typeof strategyDevelopmentPage.openStrategyCreation === "function") {
            strategyDevelopmentPage.openStrategyCreation(payload)
            pendingStrategyImportRequest = ({})
        }
    }

    function openStrategyBacktest(preferredStrategyId) {
        pendingStrategyBacktestRequest = {
            requested: true,
            strategyId: String(preferredStrategyId || "")
        }
        window.switchPage("strategy_library", "回测验证")

        if (strategyDevelopmentPage && typeof strategyDevelopmentPage.openStrategyBacktest === "function") {
            strategyDevelopmentPage.openStrategyBacktest(pendingStrategyBacktestRequest.strategyId)
            pendingStrategyBacktestRequest = ({})
        }
    }

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
                    window.showDepositDialog()
                }
                
                onWithdrawClicked: {
                    window.showWithdrawDialog()
                }
                
                onAccountRefreshClicked: {
                    window.refreshAccountData()
                }
                
                onUserProfileClicked: {
                    window.showUserProfile()
                }
            }
            
            // 右侧主内容区
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                
                // 量化交易流程流水线- 替换原来的GlobalWorkflow
                ProcessFlow {
                    id: processFlow
                    Layout.fillWidth: true
                    Layout.preferredHeight: 114
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6
                    linkedStep: window.workflowStepForMenu(window.currentMenuCode)
                    
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
                                window.openStrategyBacktest("")
                                break
                            case 4: // 风险管理
                                sidebar.setCurrentMenu("risk_management")
                                break
                            case 5: // 实盘部署
                                sidebar.setCurrentMenu("live_trading")
                                break
                        }
                        
                        window.showNotification("切换到步骤" + stepIndex + ": " + window.getStepName(stepIndex))
                    }
                    
                    onStepActionTriggered: function(stepIndex, actionType) {
                        //console.log("流程步骤操作触发:", stepIndex, "类型:", actionType)
                        window.showNotification("执行步骤" + stepIndex + "操作: " + actionType)
                        
                        // 根据步骤索引执行相应的操作
                        switch(stepIndex) {
                            case 1: // 数据准备
                                if (actionType === "page") {
                                    window.switchPage("data_dashboard", "数据准备")
                                }
                                break
                            case 2: // 策略开发
                                if (actionType === "page") {
                                    window.switchPage("strategy_factor", "策略开发")
                                }
                                break
                            case 3: // 回测验证
                                if (actionType === "page") {
                                    window.openStrategyBacktest("")
                                }
                                break
                            case 4: // 风险管理
                                if (actionType === "page") {
                                    window.switchPage("risk_management", "风险管理")
                                }
                                break
                            case 5: // 实盘部署
                                if (actionType === "external") {
                                    window.switchPage("live_trading", "实盘部署")
                                }
                                break
                        }
                    }
                    
                    onFlowCompleted: {
                        //.log("流程完成")
                        window.showNotification("恭喜！量化交易流程已完成")
                    }
                }
                
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.topMargin: 10

    // 主内容区域- StackLayout 切换页面
    StackLayout {
        id: mainStack
        anchors.fill: parent
        currentIndex: window.getStackIndex(window.currentMenuCode)

                    // 数据管理页面
                    Datamain {
                        id: dataManagementPage
                    }

                    // 策略开发页面 - 按需加载，避免冷启动初始化策略库
                    Loader {
                        id: strategyDevelopmentPageLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 1 || item !== null
                        asynchronous: true
                        sourceComponent: strategyDevelopmentPageComponent
                        onLoaded: {
                            if (item && typeof item.warmupPage === "function") {
                                item.warmupPage()
                            }
                            if (item
                                    && pendingStrategyImportRequest
                                    && Object.keys(pendingStrategyImportRequest).length > 0
                                    && typeof item.openStrategyCreation === "function") {
                                item.openStrategyCreation(pendingStrategyImportRequest)
                                pendingStrategyImportRequest = ({})
                            }
                            if (item
                                    && pendingStrategyBacktestRequest
                                    && pendingStrategyBacktestRequest.requested
                                    && typeof item.openStrategyBacktest === "function") {
                                item.openStrategyBacktest(pendingStrategyBacktestRequest.strategyId || "")
                                pendingStrategyBacktestRequest = ({})
                            }
                        }
                    }
                    
                    // 专业策略创建页面
                    Loader {
                        id: strategyCreationProPageLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 2 || item !== null
                        asynchronous: true
                        sourceComponent: strategyCreationProPageComponent
                    }
                    
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                    
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                    
                    // 因子工作台页面 - 仅承载分析/创建/调试/回测等重模式
                    Loader {
                        id: factorWorkbenchPageLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 5 || item !== null
                        asynchronous: true
                        sourceComponent: factorWorkbenchPageComponent
                        onLoaded: {
                            if (!item) {
                                return
                            }
                            if (typeof item.warmupPage === "function") {
                                item.warmupPage()
                            }
                            if (pendingFactorWorkbenchRequest && pendingFactorWorkbenchRequest.factorId) {
                                item.selectedFactorId = pendingFactorWorkbenchRequest.factorId
                                item.latestBacktestReport = pendingFactorWorkbenchRequest.latestBacktestReport || ({})
                                if (typeof item.switchMode === "function") {
                                    item.switchMode(pendingFactorWorkbenchRequest.mode || window.factorWorkbenchRouteMode)
                                }
                                pendingFactorWorkbenchRequest = ({})
                            }
                        }
                    }
                    
                    // 风险管理页面
                    Loader {
                        id: riskManagementPageLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 6 || item !== null
                        asynchronous: true
                        sourceComponent: riskManagementPageComponent
                        onLoaded: {
                            if (item && pendingRiskNavigationPayload
                                    && Object.keys(pendingRiskNavigationPayload).length > 0
                                    && typeof item.applyExternalContext === "function") {
                                item.applyExternalContext(pendingRiskNavigationPayload)
                                pendingRiskNavigationPayload = ({})
                            }
                        }
                    }
                    
                    // 独立交易执行页面
                    TradingPage {
                        id: tradingExecutionPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        marketData: window.marketData
                    }
                    
                    // 实盘交易总览页面
                    MainContent {
                        id: liveTradingPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        pageMode: "live-trading"
                        currentMenuCode: window.currentMenuCode
                        marketDataService: Bridge.MarketDataService
                        positionAccountService: Bridge.PositionAccountService
                        tradeExecutionService: Bridge.TradeExecutionService
                        strategyService: null
                        marketData: window.marketData
                        statusCards: window.statusCards
                        positions: window.positions
                        strategies: window.strategies
                    }
                    
                    // 监控面板页面
                    Loader {
                        id: monitoringPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 9 || item !== null
                        asynchronous: true
                        sourceComponent: monitoringPageComponent
                    }
                    
                    // 系统设置页面
                    Loader {
                        id: settingsPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 10 || item !== null
                        asynchronous: true
                        sourceComponent: settingsPageComponent
                    }

                    Loader {
                        id: cacheManagementPageLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: mainStack.currentIndex === 11 || item !== null
                        asynchronous: true
                        sourceComponent: cacheManagementPageComponent
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
            }
        }   
    }

    Component {
        id: cacheManagementPageComponent

        CacheManagementPage {}
    }

    Component {
        id: strategyDevelopmentPageComponent

        Item {
            id: strategyWorkbenchHost

            function warmupPage() {
                if (pageLoader.item && typeof pageLoader.item.warmupPage === "function") {
                    pageLoader.item.warmupPage()
                }
            }

            function openStrategyCreation(strategyDetail) {
                if (pageLoader.item && typeof pageLoader.item.openStrategyCreation === "function") {
                    pageLoader.item.openStrategyCreation(strategyDetail)
                }
            }

            function openStrategyBacktest(strategyId) {
                if (pageLoader.item && typeof pageLoader.item.openBacktestWorkbench === "function") {
                    pageLoader.item.openBacktestWorkbench(strategyId)
                }
            }

            Loader {
                id: pageLoader
                anchors.fill: parent
                asynchronous: true
                source: "page/strategies/StrategyLibraryPage.qml"
            }
        }
    }

    Component {
        id: strategyCreationProPageComponent

        StrategyCreationPagePro {
        }
    }

    Component {
        id: factorWorkbenchPageComponent

        FactorWorkbench {
            requestedRouteMode: window.factorWorkbenchRouteMode
            onRequestOpenStrategyCreation: function(importPayload) {
                window.openStrategyCreationFromFactorImport(importPayload || ({}))
            }
        }
    }

    Component {
        id: riskManagementPageComponent

        RiskConfigurationPage {}
    }

    Component {
        id: settingsPageComponent

        SystemSettingsPage {
            configService: Bridge.TradingConnectionConfigService
        }
    }

    Component {
        id: monitoringPageComponent

        MonitoringPage {}
    }

    DepositDialog {
        id: depositDialog
        currentBalance: window.accountValue

        onSubmitted: function(amount, note) {
            var previousValue = window.accountValue
            window.accountValue = previousValue + amount
            window.accountChange = amount
            window.accountChangePercent = previousValue > 0 ? (amount / previousValue) * 100 : 0
            sidebar.updateAccountData(window.accountValue, window.accountChange, window.accountChangePercent)
            window.showToast("已提交入金申请: " + amount.toLocaleString(Qt.locale(), 'f', 2))
            if (note) {
                console.log("入金备注:", note)
            }
        }
    }

    WithdrawDialog {
        id: withdrawDialog
        availableBalance: window.accountValue

        onSubmitted: function(amount, note) {
            var previousValue = window.accountValue
            window.accountValue = Math.max(0, previousValue - amount)
            window.accountChange = -amount
            window.accountChangePercent = previousValue > 0 ? (-amount / previousValue) * 100 : 0
            sidebar.updateAccountData(window.accountValue, window.accountChange, window.accountChangePercent)
            window.showToast("已提交出金申请: " + amount.toLocaleString(Qt.locale(), 'f', 2))
            if (note) {
                console.log("出金备注:", note)
            }
        }
    }

    UserProfileDialog {
        id: userProfileDialog
        userName: window.userName
        userStatus: window.userStatus
        userInitials: window.userInitials

        onProfileSaved: function(name, status, initials) {
            window.userName = name
            window.userStatus = status || "专业版 · 在线"
            window.userInitials = initials || "QT"
            sidebar.updateUserInfo(window.userName, window.userStatus, window.userInitials)
            window.showToast("用户资料已更新")
        }
    }

     // === 初始化函数 ===
    Component.onCompleted: {
       // console.log("应用程序启动")
       Qt.callLater(function() {
           window.raise()
           window.requestActivate()
       })
       initializeApp()
                        }

    // === 应用初始化 ===
    function initializeApp() {
        // 模拟加载数据
        Qt.callLater(function() {
            window.raise()
            window.requestActivate()

            // 1. 初始化账户数据
            window.accountValue = 1500000
            window.accountChange = 12500
            window.accountChangePercent = 0.83
            sidebar.updateAccountData(1500000, 12500, 0.83)
            
            // 2. 初始化用户信息
            window.userName = "高级交易员"
            window.userStatus = "VIP· 在线"
            window.userInitials = "AT"
            sidebar.updateUserInfo(window.userName, window.userStatus, window.userInitials)
            
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
            "cache_management": 1,
            "rule_management": 1,
            "data_management": 1,
            "data_export": 1,
            "strategy_factor": 2,
            "strategy_library": 2,
            "strategy_creation": 2,
            "strategy_creation_pro": 2,
            "factor_library": 2,
            "factor_analysis": 2,
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
            var currentSidebarMenu = sidebar.getCurrentMenu ? sidebar.getCurrentMenu() : ({})
            if (String(currentSidebarMenu.secondary || "") !== String(menuCode || "")
                    && String(currentSidebarMenu.primary || "") !== String(menuCode || "")) {
                sidebar.setCurrentMenu(menuCode, true)
            }
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
            "factor_analysis": 5,      // 因子分析 -> FactorWorkbench (索引5)
            "risk_management": 6,      // 风险管理 -> riskManagementPage (索引6)
            "live_trading": 7,         // 实盘交易 -> TradingPage (索引7)
            "monitoring": 9,           // 监控面板 -> monitoringPage (索引9)
            "settings": 10             // 系统设置 -> settingsPage (索引10)
        };
        
        // 二级菜单映射到对应的页面
        var secondaryMenuToIndex = {
            "data_dashboard": 0,              // 数据看板 -> 数据管理 (索引0)
            "cache_management": 12,           // 缓存管理 -> 独立缓存管理页 (索引12)
            "rule_management": 0,             // 规则管理 -> 数据管理 (索引0)
            "data_export": 0,                 // 数据导出 -> 数据管理 (索引0)
            "strategy_creation_pro": 2,       // 专业策略创建 -> StrategyCreationPagePro (索引2)
            "strategy_creation": 1,           // 策略创建 -> 策略与因子 (索引1)
            "strategy_library": 1,            // 策略库 -> 策略与因子 (索引1)
            "factor_library": 5,              // 因子库 -> FactorWorkbench (索引5)
            "factor_analysis": 5,             // 因子分析 -> FactorWorkbench (索引5)
            "risk_configuration": 6,          // 风险配置 -> 风险管理 (索引6)
            "risk_monitoring": 6,             // 风险监控 -> 风险管理 (索引6)
            "stress_testing": 6,              // 压力测试 -> 风险管理 (索引6)
            "risk_reporting": 6,              // 风险报告 -> 风险管理 (索引6)
            "compliance_check": 6,            // 合规检查 -> 风险管理 (索引6)
            "trade_execution": 7,             // 交易执行 -> 实盘交易 (索引7)
            "position_management": 8,         // 仓位管理 -> 实盘总览 (索引8)
            "fund_management": 8,             // 资金管理 -> 实盘总览 (索引8)
            "trade_records": 8,               // 交易记录 -> 实盘总览 (索引8)
            "performance_analysis": 8,        // 绩效分析 -> 实盘总览 (索引8)
            "real_time_monitoring": 9,        // 实时监控 -> 监控面板 (索引9)
            "alert_center": 9,                // 报警中心 -> 监控面板 (索引9)
            "system_status": 9,               // 系统状态 -> 监控面板 (索引9)
            "log_viewer": 9,                  // 日志查看 -> 监控面板 (索引9)
            "personal_settings": 10,          // 个人设置 -> 系统设置 (索引10)
            "trade_settings": 10,             // 交易设置 -> 系统设置 (索引10)
            "notification_settings": 10,      // 通知设置 -> 系统设置 (索引10)
            "permission_management": 10,      // 权限管理 -> 系统设置 (索引10)
            "system_configuration": 10        // 系统配置 -> 系统设置 (索引10)
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
        if (menuCode === "strategy_library" || menuCode === "strategy_creation") {
            return "qrc:/page/strategies/StrategyLibraryPage.qml";
        }
        if (menuCode === "factor_analysis" || menuCode === "factor_library") {
            return "qrc:/ConsoleUi/Qml/page/FactorWorkbench.qml";
        }
        return "qrc:/page/dashboard/MainContent.qml";
    }

    function showDepositDialog() {
        depositDialog.open()
    }

    function showWithdrawDialog() {
        withdrawDialog.open()
    }

    function refreshAccountData() {
        var mockValue = window.accountValue
        var mockChange = window.accountChange
        var mockPercent = window.accountChangePercent

        sidebar.updateAccountData(mockValue, mockChange, mockPercent)

        // 显示刷新提示
        showToast("账户数据已更新")
    }

    function showUserProfile() {
        console.log("显示用户资料")
        userProfileDialog.open()
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
    
    function openFactorWorkbench(mode, factorId, menuTitle) {
        var normalizedMode = String(mode || "analyze")
        var normalizedFactorId = String(factorId || "").trim()
        pendingFactorWorkbenchRequest = {
            mode: normalizedMode,
            factorId: normalizedFactorId,
            latestBacktestReport: ({})
        }

        window.switchPage("factor_analysis", menuTitle || "因子分析")

        if (factorWorkbenchPage) {
            factorWorkbenchPage.selectedFactorId = normalizedFactorId
            factorWorkbenchPage.latestBacktestReport = ({})
            if (typeof factorWorkbenchPage.switchMode === "function") {
                factorWorkbenchPage.switchMode(normalizedMode)
            }
            pendingFactorWorkbenchRequest = ({})
        }
    }
    
    // === 同步工作流程与菜单 ===
    function syncWorkflowWithMenu(menuCode) {
        console.log("同步工作流程与菜单", menuCode)

        var step = window.workflowStepForMenu(menuCode)
        if (step && processFlow.currentStep !== step) {
            processFlow.goToStep(step)
            console.log("流程流水线同步到步骤:", step)
        }
    }
}
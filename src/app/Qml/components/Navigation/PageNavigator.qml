// PageNavigator.qml - 页面导航管理器
import QtQuick 2.15
import QtQuick.Controls 2.15

QtObject {
    id: pageNavigator
    
    // === 页面映射表 ===
    property var pageMapping: {
        // 数据管理相关页面
        "data_dashboard": { stackIndex: 0, pageName: "数据看板", moduleId: "data-integration" },
        "cache_management": { stackIndex: 12, pageName: "缓存管理", moduleId: "data-integration" },
        "rule_management": { stackIndex: 0, pageName: "规则管理", moduleId: "data-integration" },
        "data_export": { stackIndex: 0, pageName: "数据导出", moduleId: "data-integration" },
        
        // 策略开发相关页面
        "strategy_creation_pro": { stackIndex: 2, pageName: "专业策略创建", moduleId: "factor-analysis" },
        "strategy_creation": { stackIndex: 1, pageName: "策略创建", moduleId: "factor-analysis" },
        "factor_analysis": { stackIndex: 5, pageName: "因子分析", moduleId: "factor-analysis" },
        "strategy_library": { stackIndex: 1, pageName: "策略库", moduleId: "factor-analysis" },
        
        // 风险管理相关页面
        "risk_configuration": { stackIndex: 7, pageName: "风险配置", moduleId: "risk-management" },
        "risk_monitoring": { stackIndex: 7, pageName: "风险监控", moduleId: "risk-management" },
        "stress_testing": { stackIndex: 7, pageName: "压力测试", moduleId: "risk-management" },
        "risk_reporting": { stackIndex: 7, pageName: "风险报告", moduleId: "risk-management" },
        "compliance_check": { stackIndex: 7, pageName: "合规检查", moduleId: "risk-management" },
        
        // 实盘交易相关页面
        "trade_execution": { stackIndex: 8, pageName: "交易执行", moduleId: "live-trading" },
        "fund_management": { stackIndex: 9, pageName: "资金管理", moduleId: "live-trading" },
        "trade_records": { stackIndex: 9, pageName: "交易记录", moduleId: "live-trading" },
        "performance_analysis": { stackIndex: 9, pageName: "绩效分析", moduleId: "live-trading" },
        "factor_performance":  { stackIndex: 13, pageName: "因子绩效", moduleId: "strategy-factor" },
        
        // 监控面板相关页面
        "real_time_monitoring": { stackIndex: 10, pageName: "实时监控", moduleId: "monitoring" },
        "alert_center": { stackIndex: 10, pageName: "报警中心", moduleId: "monitoring" },
        "system_status": { stackIndex: 10, pageName: "系统状态", moduleId: "monitoring" },
        "log_viewer": { stackIndex: 10, pageName: "日志查看", moduleId: "monitoring" },
        
        // 系统设置相关页面
        "personal_settings": { stackIndex: 11, pageName: "个人设置", moduleId: "settings" },
        "trade_settings": { stackIndex: 11, pageName: "交易设置", moduleId: "settings" },
        "notification_settings": { stackIndex: 11, pageName: "通知设置", moduleId: "settings" },
        "permission_management": { stackIndex: 11, pageName: "权限管理", moduleId: "settings" },
        "system_configuration": { stackIndex: 11, pageName: "系统配置", moduleId: "settings" }
    }
    
    // === 模块到页面的映射 ===
    property var moduleToPages: {
        "data-integration": [
            "data_dashboard", "cache_management", "rule_management", "data_export"
        ],
        "factor-analysis": [
            "strategy_creation_pro", "strategy_creation", "factor_analysis", "strategy_library"
        ],
        "risk-management": [
            "risk_configuration", "risk_monitoring", "stress_testing",
            "risk_reporting", "compliance_check"
        ],
        "live-trading": [
            "trade_execution", "fund_management",
            "trade_records", "performance_analysis"
        ],
        "monitoring": [
            "real_time_monitoring", "alert_center", "system_status", "log_viewer"
        ],
        "settings": [
            "personal_settings", "trade_settings", "notification_settings",
            "permission_management", "system_configuration"
        ]
    }
    
    // === 信号 ===
    signal pageChanged(string pageCode, string pageName, int stackIndex)
    signal moduleNavigation(string moduleId, string targetPageCode)
    signal navigationError(string errorMessage)
    
    // === 导航到页面 ===
    function navigateToPage(pageCode) {
        console.log("导航到页面:", pageCode)
        
        var pageInfo = pageMapping[pageCode]
        if (!pageInfo) {
            console.error("未找到页面:", pageCode)
            navigationError("未找到页面: " + pageCode)
            return false
        }
        
        console.log("页面信息:", JSON.stringify(pageInfo))
        
        // 发送页面更改信号
        pageChanged(pageCode, pageInfo.pageName, pageInfo.stackIndex)
        
        // 这里需要调用主窗口的页面切换函数
        // 假设主窗口有一个全局的页面切换函数
        if (typeof window !== 'undefined' && window.switchPage) {
            window.switchPage(pageCode, pageInfo.pageName)
        }
        
        // 同步工作流程
        if (typeof window !== 'undefined' && window.syncWorkflowWithMenu) {
            window.syncWorkflowWithMenu(pageCode)
        }
        
        return true
    }
    
    // === 从模块导航 ===
    function navigateFromModule(moduleId, actionType) {
        console.log("从模块导航:", moduleId, "动作类型:", actionType)
        
        var pages = moduleToPages[moduleId]
        if (!pages || pages.length === 0) {
            console.error("模块没有对应的页面:", moduleId)
            navigationError("模块 " + moduleId + " 没有对应的页面")
            return false
        }
        
        // 根据动作类型选择目标页面
        var targetPageCode = null
        
        switch(actionType) {
            case "open":
                // 打开模块的主页面
                targetPageCode = pages[0]
                break
            case "config":
                // 打开配置页面
                targetPageCode = findConfigPage(pages)
                break
            case "run":
                // 打开运行/执行页面
                targetPageCode = findRunPage(pages)
                break
            default:
                // 默认打开第一个页面
                targetPageCode = pages[0]
        }
        
        if (!targetPageCode) {
            targetPageCode = pages[0]
        }
        
        console.log("目标页面:", targetPageCode)
        
        // 发送模块导航信号
        moduleNavigation(moduleId, targetPageCode)
        
        // 导航到目标页面
        return navigateToPage(targetPageCode)
    }
    
    // === 查找配置页面 ===
    function findConfigPage(pages) {
        for (var i = 0; i < pages.length; i++) {
            var pageCode = pages[i]
            if (pageCode.includes("config") || 
                pageCode.includes("setting") || 
                pageCode.includes("management")) {
                return pageCode
            }
        }
        return pages[0]
    }
    
    // === 查找运行页面 ===
    function findRunPage(pages) {
        for (var i = 0; i < pages.length; i++) {
            var pageCode = pages[i]
            if (pageCode.includes("run") || 
                pageCode.includes("execution") || 
                pageCode.includes("backtest") ||
                pageCode.includes("cleaning") ||
                pageCode.includes("monitoring")) {
                return pageCode
            }
        }
        return pages[0]
    }
    
    // === 获取模块对应的页面列表 ===
    function getPagesForModule(moduleId) {
        var pages = moduleToPages[moduleId]
        if (!pages) {
            return []
        }
        
        var result = []
        for (var i = 0; i < pages.length; i++) {
            var pageCode = pages[i]
            var pageInfo = pageMapping[pageCode]
            if (pageInfo) {
                result.push({
                    code: pageCode,
                    name: pageInfo.pageName,
                    stackIndex: pageInfo.stackIndex
                })
            }
        }
        
        return result
    }
    
    // === 获取当前页面信息 ===
    function getCurrentPageInfo() {
        // 这里需要从主窗口获取当前页面信息
        if (typeof window !== 'undefined' && window.sidebar) {
            var currentMenu = window.sidebar.menuModel.currentSecondaryMenu
            var pageInfo = pageMapping[currentMenu]
            if (pageInfo) {
                return {
                    code: currentMenu,
                    name: pageInfo.pageName,
                    stackIndex: pageInfo.stackIndex,
                    moduleId: pageInfo.moduleId
                }
            }
        }
        
        return null
    }
    
    // === 检查页面是否属于模块 ===
    function isPageInModule(pageCode, moduleId) {
        var pages = moduleToPages[moduleId]
        if (!pages) {
            return false
        }
        
        return pages.indexOf(pageCode) !== -1
    }
    
    // === 获取模块的主页面 ===
    function getMainPageForModule(moduleId) {
        var pages = moduleToPages[moduleId]
        if (!pages || pages.length === 0) {
            return null
        }
        
        return pages[0]
    }
    
    // === 获取页面所属的模块 ===
    function getModuleForPage(pageCode) {
        for (var moduleId in moduleToPages) {
            var pages = moduleToPages[moduleId]
            if (pages && pages.indexOf(pageCode) !== -1) {
                return moduleId
            }
        }
        return null
    }
    
    // === 初始化 ===
    Component.onCompleted: {
        console.log("PageNavigator初始化完成")
        console.log("页面映射表:", Object.keys(pageMapping).length, "个页面")
        console.log("模块映射表:", Object.keys(moduleToPages).length, "个模块")
    }
}
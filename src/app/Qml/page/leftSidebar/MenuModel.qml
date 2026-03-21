// MenuModel.qml - 完整修复版
import QtQuick 2.15

QtObject {
    id: menuModel
    
    // === 顶级标题 ===
    property string mainTitle: "交易"
    
    // === 一级菜单数据 ===
    property var primaryMenus: [
        {title: "数据管理", icon: "📊", badge: "", code: "data_management"},
        {title: "策略开发", icon: "🤖", badge: "", code: "strategy_development"},
        {title: "风险管理", icon: "🛡️", badge: "!", code: "risk_management"},
        {title: "实盘交易", icon: "📈", badge: "实时", code: "live_trading"},
        {title: "监控面板", icon: "📋", badge: "", code: "monitoring"},
        {title: "系统设置", icon: "⚙️", badge: "", code: "settings"}
    ]
    
    // === 二级菜单数据映射 ===
    property var secondaryMenus: {
        "data_management": {
            title: "数据操作",
            items: [
                {title: "数据看板", icon: "📈", badge: "", code: "data_dashboard"},
                {title: "数据导出", icon: "📤", badge: "", code: "data_export"}
            ]
        },
        "strategy_development": {
            title: "策略开发",
            items: [
                {title: "专业策略创建", icon: "🔵", badge: "专家", code: "strategy_creation_pro"},
                {title: "策略创建", icon: "🚀", badge: "", code: "strategy_creation"},
                {title: "因子分析", icon: "📊", badge: "", code: "factor_analysis"},
                {title: "策略回测", icon: "🔄", badge: "", code: "strategy_backtest"},
                {title: "策略优化", icon: "⚙️", badge: "新", code: "strategy_optimization"},
                {title: "策略库", icon: "📚", badge: "", code: "strategy_library"}
            ]
        },
        "risk_management": {
            title: "风险管理",
            items: [
                {title: "风险配置", icon: "⚙️", badge: "", code: "risk_configuration"},
                {title: "风险监控", icon: "👁️", badge: "!", code: "risk_monitoring"},
                {title: "压力测试", icon: "📉", badge: "", code: "stress_testing"},
                {title: "风险报告", icon: "📋", badge: "", code: "risk_reporting"},
                {title: "合规检查", icon: "✅", badge: "", code: "compliance_check"}
            ]
        },
        "live_trading": {
            title: "实盘交易",
            items: [
                {title: "交易执行", icon: "⚡", badge: "实时", code: "trade_execution"},
                {title: "仓位管理", icon: "💰", badge: "", code: "position_management"},
                {title: "资金管理", icon: "💵", badge: "", code: "fund_management"},
                {title: "交易记录", icon: "📝", badge: "", code: "trade_records"},
                {title: "绩效分析", icon: "📈", badge: "", code: "performance_analysis"}
            ]
        },
        "monitoring": {
            title: "监控面板",
            items: [
                {title: "实时监控", icon: "👁️", badge: "", code: "real_time_monitoring"},
                {title: "报警中心", icon: "🔔", badge: "3", code: "alert_center"},
                {title: "系统状态", icon: "📊", badge: "", code: "system_status"},
                {title: "日志查看", icon: "📋", badge: "", code: "log_viewer"}
            ]
        },
        "settings": {
            title: "系统设置",
            items: [
                {title: "个人设置", icon: "👤", badge: "", code: "personal_settings"},
                {title: "交易设置", icon: "⚡", badge: "", code: "trade_settings"},
                {title: "通知设置", icon: "🔔", badge: "", code: "notification_settings"},
                {title: "权限管理", icon: "👥", badge: "", code: "permission_management"},
                {title: "系统配置", icon: "⚙️", badge: "", code: "system_configuration"}
            ]
        }
    }
    
    // === 当前选中状态 ===
    property string currentPrimaryMenu: "data_management"
    property string currentSecondaryMenu: "data_dashboard"
    
    // === 信号 ===
    signal primaryMenuChanged(string menuCode, string menuTitle)
    signal secondaryMenuChanged(string menuCode, string menuTitle)
    signal menuItemClicked(string menuCode, string menuText)
    
    // === 选择一级菜单 ===
    function selectPrimaryMenu(menuCode) {
       // console.log("selectPrimaryMenu called with:", menuCode)
        
        // 更新当前一级菜单
        currentPrimaryMenu = menuCode
        
        // 获取对应的二级菜单，选择第一个
        var secondaryData = secondaryMenus[menuCode]
        if (secondaryData && secondaryData.items.length > 0) {
            selectSecondaryMenu(secondaryData.items[0].code)
        }
        
        // 发送信号
        var menuTitle = getPrimaryMenuTitle(menuCode)
        primaryMenuChanged(menuCode, menuTitle)
    }
    
    // === 选择二级菜单 ===
    function selectSecondaryMenu(menuCode) {
       // console.log("selectSecondaryMenu called with:", menuCode)
        
        // 更新当前二级菜单
        currentSecondaryMenu = menuCode
        
        // 发送信号
        var menuTitle = getSecondaryMenuTitle(menuCode)
        secondaryMenuChanged(menuCode, menuTitle)
        menuItemClicked(menuCode, menuTitle)
    }
    
    // === 获取一级菜单标题 ===
    function getPrimaryMenuTitle(menuCode) {
        for (var i = 0; i < primaryMenus.length; i++) {
            if (primaryMenus[i].code === menuCode) {
                return primaryMenus[i].title
            }
        }
        return ""
    }
    
    // === 获取二级菜单标题 ===
    function getSecondaryMenuTitle(menuCode) {
        for (var key in secondaryMenus) {
            var menuGroup = secondaryMenus[key]
            for (var i = 0; i < menuGroup.items.length; i++) {
                if (menuGroup.items[i].code === menuCode) {
                    return menuGroup.items[i].title
                }
            }
        }
        return ""
    }
    
    // === 获取当前二级菜单 ===
    function getCurrentSecondaryMenus() {
        var secondaryData = secondaryMenus[currentPrimaryMenu]
        if (secondaryData) {
          //  console.log("获取当前二级菜单，一级菜单:", currentPrimaryMenu)
            return secondaryData.items
        }
       // console.log("没有找到二级菜单，返回空数组")
        return []
    }
    
    // === 获取当前二级标题 ===
    function getCurrentSecondaryTitle() {
        var secondaryData = secondaryMenus[currentPrimaryMenu]
        if (secondaryData) {
            //console.log("获取当前二级标题:", secondaryData.title)
            return secondaryData.title || "菜单"
        }
        //console.log("没有找到二级菜单标题，返回默认标题")
        return "菜单"
    }
    
    // === 设置当前菜单（兼容方法）===
    function setCurrentMenu(menuCode) {
       // console.log("setCurrentMenu called with:", menuCode)
        
        // 检查是否是一级菜单
        for (var i = 0; i < primaryMenus.length; i++) {
            if (primaryMenus[i].code === menuCode) {
                selectPrimaryMenu(menuCode)
                return
            }
        }
        
        // 检查是否是二级菜单
        for (var key in secondaryMenus) {
            var menuGroup = secondaryMenus[key]
            for (var j = 0; j < menuGroup.items.length; j++) {
                if (menuGroup.items[j].code === menuCode) {
                    // 先选择对应的一级菜单
                    selectPrimaryMenu(key)
                    // 然后选择二级菜单
                    selectSecondaryMenu(menuCode)
                    return
                }
            }
        }
        
       //console.log("未找到菜单:", menuCode)
    }
    
    // === 获取菜单统计信息 ===
    function getMenuStats() {
        var stats = {
            mainTitle: mainTitle,
            totalPrimaryMenus: primaryMenus.length,
            totalSecondaryGroups: Object.keys(secondaryMenus).length,
            totalSecondaryMenus: 0
        }
        
        // 计算二级菜单总数
        for (var key in secondaryMenus) {
            stats.totalSecondaryMenus += secondaryMenus[key].items.length
        }
        
       // console.log("菜单统计:", JSON.stringify(stats))
        return stats
    }
    
    // === 获取所有二级标题 ===
    function getAllSubTitles() {
        var titles = []
        for (var key in secondaryMenus) {
            var menuGroup = secondaryMenus[key]
            if (menuGroup.title && !titles.includes(menuGroup.title)) {
                titles.push(menuGroup.title)
            }
        }
        return titles
    }
    
    // === 获取当前激活的菜单信息 ===
    function getCurrentMenuInfo() {
        var primaryTitle = getPrimaryMenuTitle(currentPrimaryMenu)
        var secondaryTitle = getSecondaryMenuTitle(currentSecondaryMenu)
        
        return {
            primary: currentPrimaryMenu,
            secondary: currentSecondaryMenu,
            primaryTitle: primaryTitle,
            secondaryTitle: secondaryTitle
        }
    }
    
    // === 更新菜单角标 ===
    function updateBadge(menuCode, badgeText, isPrimary) {
        if (isPrimary) {
            // 更新一级菜单角标
            for (var i = 0; i < primaryMenus.length; i++) {
                if (primaryMenus[i].code === menuCode) {
                    primaryMenus[i].badge = badgeText
                    //console.log("更新一级菜单角标:", primaryMenus[i].title, "->", badgeText)
                    return true
                }
            }
        } else {
            // 更新二级菜单角标
            for (var key in secondaryMenus) {
                var menuGroup = secondaryMenus[key]
                for (var j = 0; j < menuGroup.items.length; j++) {
                    if (menuGroup.items[j].code === menuCode) {
                        menuGroup.items[j].badge = badgeText
                       // console.log("更新二级菜单角标:", menuGroup.items[j].title, "->", badgeText)
                        return true
                    }
                }
            }
        }
        return false
    }
    
    // === 添加新的一级菜单 ===
    function addPrimaryMenu(menuData) {
        primaryMenus.push(menuData)
        return true
    }
    
    // === 获取当前菜单状态 ===
    function getCurrentMenu() {
        return {
            primary: currentPrimaryMenu,
            secondary: currentSecondaryMenu,
            primaryTitle: getPrimaryMenuTitle(currentPrimaryMenu),
            secondaryTitle: getSecondaryMenuTitle(currentSecondaryMenu)
        }
    }
}
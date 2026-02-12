// DashboardPage.qml - 修复清理窗口调用问题
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../components/DataAnalysis" as Components
import ConsoleUi 1.0 as Theme
import AStock.Engine 1.0

Item {
    id: dashboardPage
    
    // 添加这些属性
    property var currentRules: loadRulesFromConfig()
    property var dataPreviewInfo: ({})
    property var cleanedData: []
    
    // 滚动区域
    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        
        Column {
            id: contentColumn
            width: Math.min(dashboardPage.width, 1200) - 25
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 30
            
            // ============= 1. 快速开始区域 =============
            Components.QuickStart {
                width: parent.width - 25
                anchors.horizontalCenter: parent.horizontalCenter
                
                onNewProjectClicked: {
                    // 新建项目时重置工作流程到第一步
                    workflow.reset()
                }
                onLoadTemplateClicked: {
                    // 加载模板时设置工作流程到第三步（策略回测）
                    workflow.goToStep(3)
                }
            }
            
            // ============= 2. 工作流程 - 已替换为增强版本 =============
            // 标题
            Row {
                width: parent.width - 25
                anchors.horizontalCenter: parent.horizontalCenter
                height: 40
                spacing: 20
                
                Text {
                    text: "标准工作流程"
                    font.pixelSize: 28
                    font.bold: true
                    color: Theme.darkText
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Rectangle {
                    width: 200
                    height: 36
                    radius: 6
                    color: Theme.darkCard
                    border.color: Theme.darkBorder
                    border.width: 1
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        
                        Image {
                            source: "qrc:/icons/project-diagram.svg"
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "点击任意步骤开始"
                            font.pixelSize: 14
                            color: Theme.darkText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
            
            // 嵌入式工作流程组件
            Components.EmbeddedWorkflow {
                id: workflow
                width: parent.width - 25
                height: 320
                anchors.horizontalCenter: parent.horizontalCenter
                
                // 适配现有界面的颜色主题
                backgroundColor: Theme.darkCard
                cardBackground: "#1a237e"
                activeColor: Theme.accentColor
                completedColor: Theme.successColor
                pendingColor: "#2d3a7c"
                textColor: Theme.darkText
                textSecondaryColor: Theme.darkTextSecondary
                
                currentStep: 2  // 默认从第二步开始
                
                // 步骤数据
                steps: [
                    {
                        "title": "数据整合与清洗",
                        "description": "导入并准备分析数据",
                        "icon": "database",
                        "status": "completed"
                    },
                    {
                        "title": "因子分析与特征工程", 
                        "description": "开发量化因子和特征",
                        "icon": "chart-bar",
                        "status": "active"
                    },
                    {
                        "title": "策略回测与验证",
                        "description": "历史测试策略表现",
                        "icon": "history",
                        "status": "pending"
                    },
                    {
                        "title": "风险管理与优化",
                        "description": "调整风险与优化组合",
                        "icon": "shield-alt",
                        "status": "pending"
                    },
                    {
                        "title": "报告与部署", 
                        "description": "生成报告并部署策略",
                        "icon": "file-alt",
                        "status": "pending"
                    }
                ]
                
                // 控制显示哪些部分
                showProgressBar: true
                showNavigation: true
                compactMode: false
                
                // 信号处理
                onStepActivated: {
                    console.log("步骤激活:", stepIndex)
                    // 这里可以更新界面状态或显示相应内容
                    showNotification("切换到步骤 " + stepIndex + ": " + getStepName(stepIndex))
                    
                    // 更新对应的任务状态
                    updateTaskStatusForStep(stepIndex)
                }
                
                onStepStarted: {
                    console.log("步骤开始:", stepIndex)
                    // 打开相应的工作模块
                    openModule(stepIndex)
                }
                
                onStepDetailRequested: {
                    console.log("查看步骤详情:", stepIndex)
                    // 显示步骤详细信息
                    showStepDetails(stepIndex)
                }
                
                onNavigationAction: {
                    console.log("导航操作:", action)
                    // 处理导航动作
                    handleNavigation(action)
                }
            }
            
            // ============= 3. 核心功能模块 =============
            // 标题
            Row {
                width: parent.width - 25
                anchors.horizontalCenter: parent.horizontalCenter
                height: 40
                spacing: 20
                
                Text {
                    text: "核心功能模块"
                    font.pixelSize: 28
                    font.bold: true
                    color: Theme.darkText
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Rectangle {
                    width: 200
                    height: 36
                    radius: 6
                    color: Theme.darkCard
                    border.color: Theme.darkBorder
                    border.width: 1
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        
                        Image {
                            source: "qrc:/icons/cogs.svg"
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "点击模块查看详情"
                            font.pixelSize: 14
                            color: Theme.darkText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
            
            // 模块网格
            GridLayout {
                width: parent.width - 25
                anchors.horizontalCenter: parent.horizontalCenter
                columns: 2
                rowSpacing: 15
                columnSpacing: 24
                
                // 数据整合与清洗模块
                Components.ModuleCard {
                    id: dataIntegrationModule
                    Layout.fillWidth: true
                    moduleId: "data-integration"
                    iconSource: "qrc:/icons/database.svg"
                    title: "数据整合与清洗"
                    description: "整合行情、基本面、宏观、舆情等多源数据，进行标准化处理。"
                    
                    actions: [
                        { id: "add-source", label: "添加数据源", icon: "qrc:/icons/plus.svg", primary: true },
                        { id: "run-clean", label: "运行清洗", icon: "qrc:/icons/play.svg", primary: false },
                        { id: "config-rules", label: "配置规则", icon: "qrc:/icons/sliders.svg", primary: false },
                        { id: "preview-data", label: "数据预览", icon: "qrc:/icons/eye.svg", primary: false }
                    ]
                    
                    recentTasks: [
                        { name: "A股日频数据更新", icon: "qrc:/icons/check.svg", iconColor: Theme.successColor, status: "completed" },
                        { name: "财务数据清洗", icon: "qrc:/icons/sync.svg", iconColor: Theme.accentColor, status: "running" }
                    ]
                    
                    onActionClicked: function(actionId) {
                        console.log("模块操作点击: data-integration - " + actionId)
                        if (actionId === "add-source") {
                            showAddDataSourcePopup()
                            workflow.goToStep(1)
                        } else if (actionId === "config-rules") {
                            showRulesConfigPopup()
                            workflow.goToStep(1)
                        } else if (actionId === "run-clean") {
                            runDataCleaning()
                            workflow.goToStep(1)
                        } else if (actionId === "preview-data") {
                            // 数据预览 - 检查是否有清理过的数据
                            if (cleanedData && cleanedData.length > 0) {
                                // 使用实际清理后的数据
                                showDataPreviewWithData(cleanedData)
                            } else {
                                // 使用模拟数据
                                var savedRules = loadRulesFromConfig()
                                if (savedRules) {
                                    showDataPreview({
                                        appliedRules: Object.keys(savedRules).length,
                                        stockCount: 1450,
                                        timeRange: savedRules.timeRange ? 
                                            savedRules.timeRange.start + " 至 " + savedRules.timeRange.end : 
                                            "未设置",
                                        priceRange: savedRules.priceFilter ? 
                                            savedRules.priceFilter.min + "元 至 " + savedRules.priceFilter.max + "元" : 
                                            "未设置",
                                        volumeFilter: savedRules.volumeFilter ? 
                                            "成交量 > " + savedRules.volumeFilter.minVolume + "手" : 
                                            "未设置",
                                        completeness: "99.2%",
                                        sampleData: [
                                            { 
                                                date: "2024-01-15", 
                                                code: "000001", 
                                                name: "平安银行", 
                                                open: 12.35, 
                                                close: 12.45, 
                                                change: 1.2, 
                                                volume: 1520000 
                                            },
                                            { 
                                                date: "2024-01-15", 
                                                code: "600519", 
                                                name: "贵州茅台", 
                                                open: 1670.00, 
                                                close: 1680.50, 
                                                change: 0.8, 
                                                volume: 32000 
                                            }
                                        ]
                                    })
                                } else {
                                    showNotification("请先配置规则并运行数据清理")
                                    showRulesConfigPopup()
                                }
                            }
                            workflow.goToStep(1)
                        }
                    }
                    
                    onCardClicked: {
                        workflow.goToStep(1)
                    }
                }
                
                // 因子分析与特征工程模块
                Components.ModuleCard {
                    id: factorAnalysisModule
                    Layout.fillWidth: true
                    moduleId: "factor-analysis"
                    iconSource: "qrc:/icons/chart-bar.svg"
                    title: "因子分析与特征工程"
                    description: "提取有效预测指标，评估因子有效性。"
                    
                    actions: [
                        { id: "new-factor", label: "新建因子", icon: "qrc:/icons/plus.svg", primary: true },
                        { id: "factor-backtest", label: "因子回测", icon: "qrc:/icons/calculator.svg", primary: false },
                        { id: "feature-engineering", label: "特征工程", icon: "qrc:/icons/project.svg", primary: false },
                        { id: "factor-library", label: "因子库", icon: "qrc:/icons/box.svg", primary: false }
                    ]
                    
                    recentTasks: [
                        { name: "动量因子V2.1", icon: "qrc:/icons/star.svg", iconColor: Theme.warningColor, status: "completed" },
                        { name: "质量因子Q1", icon: "qrc:/icons/star.svg", iconColor: Theme.warningColor, status: "completed" }
                    ]
                    
                    onActionClicked: function(actionId) {
                        console.log("模块操作点击: factor-analysis - " + actionId)
                        // 同步更新工作流程
                        if (actionId === "new-factor" || actionId === "factor-backtest") {
                            workflow.goToStep(2)
                        }
                    }
                    
                    onCardClicked: {
                        workflow.goToStep(2)
                    }
                }
                
                // 策略回测与验证模块
                Components.ModuleCard {
                    id: backtestingModule
                    Layout.fillWidth: true
                    moduleId: "backtesting"
                    iconSource: "qrc:/icons/history.svg"
                    title: "策略回测与验证"
                    description: "历史模拟策略表现，进行绩效分析。"
                    
                    actions: [
                        { id: "new-backtest", label: "新建回测", icon: "qrc:/icons/plus.svg", primary: true },
                        { id: "run-backtest", label: "运行回测", icon: "qrc:/icons/play-circle.svg", primary: false },
                        { id: "performance", label: "绩效分析", icon: "qrc:/icons/chart-line.svg", primary: false },
                        { id: "compare", label: "多策略对比", icon: "qrc:/icons/exchange.svg", primary: false }
                    ]
                    
                    recentTasks: [
                        { name: "多因子选股策略", icon: "qrc:/icons/chart-line.svg", iconColor: Theme.primaryColor, status: "completed" },
                        { name: "均值回归策略", icon: "qrc:/icons/chart-line.svg", iconColor: Theme.primaryColor, status: "completed" }
                    ]
                    
                    onActionClicked: function(actionId) {
                        console.log("模块操作点击: backtesting - " + actionId)
                        // 同步更新工作流程
                        if (actionId === "new-backtest" || actionId === "run-backtest") {
                            workflow.goToStep(3)
                        }
                    }
                    
                    onCardClicked: {
                        workflow.goToStep(3)
                    }
                }
                
                // 风险管理与优化模块
                Components.ModuleCard {
                    id: riskManagementModule
                    Layout.fillWidth: true
                    moduleId: "risk-management"
                    iconSource: "qrc:/icons/shield.svg"
                    title: "风险管理与优化"
                    description: "资产配置优化，风险建模与监控。"
                    
                    actions: [
                        { id: "portfolio-opt", label: "组合优化", icon: "qrc:/icons/cogs.svg", primary: true },
                        { id: "risk-monitor", label: "风险监控", icon: "qrc:/icons/exclamation.svg", primary: false },
                        { id: "stress-test", label: "压力测试", icon: "qrc:/icons/chart-pie.svg", primary: false },
                        { id: "risk-attribution", label: "风险归因", icon: "qrc:/icons/search.svg", primary: false }
                    ]
                    
                    recentTasks: [
                        { name: "组合VaR", icon: "qrc:/icons/exclamation-circle.svg", iconColor: Theme.successColor, status: "completed" },
                        { name: "最大回撤", icon: "qrc:/icons/exclamation-circle.svg", iconColor: Theme.dangerColor, status: "completed" }
                    ]
                    
                    onActionClicked: function(actionId) {
                        console.log("模块操作点击: risk-management - " + actionId)
                        // 同步更新工作流程
                        if (actionId === "portfolio-opt" || actionId === "risk-monitor") {
                            workflow.goToStep(4)
                        }
                    }
                    
                    onCardClicked: {
                        workflow.goToStep(4)
                    }
                }
            }
            
            // ============= 4. 系统概览 =============
            // 标题
            Row {
                width: parent.width - 25
                height: 40
                spacing: 10
                
                Text {
                    text: "系统概览"
                    font.pixelSize: 28
                    font.bold: true
                    color: Theme.darkText
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Rectangle {
                    width: 200
                    height: 36
                    radius: 6
                    color: Theme.darkCard
                    border.color: Theme.darkBorder
                    border.width: 1
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        
                        Image {
                            source: "qrc:/icons/calendar.svg"
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "2023年1月 - 2023年12月"
                            font.pixelSize: 14
                            color: Theme.darkText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
            
            // 统计卡片
            GridLayout {
                width: parent.width - 25
                anchors.horizontalCenter: parent.horizontalCenter
                columns: 4
                rowSpacing: 10
                columnSpacing: 24
                
                Components.StatsCard {
                    iconSource: "qrc:/icons/filter.svg"
                    iconColor: Theme.primaryColor
                    label: "数据清洗任务"
                    value: "18"
                    trendText: "今日完成: 12"
                    trendUp: true
                    
                    onCardClicked: {
                        console.log("数据清洗任务卡片点击")
                        workflow.goToStep(1)
                    }
                }
                
                Components.StatsCard {
                    iconSource: "qrc:/icons/calculator.svg"
                    iconColor: Theme.accentColor
                    label: "因子计算任务"
                    value: "24"
                    trendText: "较上周 +8"
                    trendUp: true
                    
                    onCardClicked: {
                        console.log("因子计算任务卡片点击")
                        workflow.goToStep(2)
                    }
                }
                
                Components.StatsCard {
                    iconSource: "qrc:/icons/history.svg"
                    iconColor: Theme.successColor
                    label: "策略回测任务"
                    value: "9"
                    trendText: "较上周 -3"
                    trendUp: false
                    
                    onCardClicked: {
                        console.log("策略回测任务卡片点击")
                        workflow.goToStep(3)
                    }
                }
                
                Components.StatsCard {
                    iconSource: "qrc:/icons/shield.svg"
                    iconColor: Theme.dangerColor
                    label: "风险监控任务"
                    value: "15"
                    trendText: "实时运行中"
                    trendUp: true
                    
                    onCardClicked: {
                        console.log("风险监控任务卡片点击")
                        workflow.goToStep(4)
                    }
                }
            }
            
            // ============= 5. 模块任务状态 =============
            // 标题
            Row {
                width: parent.width - 25
                anchors.horizontalCenter: parent.horizontalCenter
                height: 40
                spacing: 20
                
                Text {
                    text: "模块任务状态"
                    font.pixelSize: 28
                    font.bold: true
                    color: Theme.darkText
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Rectangle {
                    width: 250
                    height: 36
                    radius: 6
                    color: Theme.darkCard
                    border.color: Theme.darkBorder
                    border.width: 1
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        
                        Image {
                            source: "qrc:/icons/tasks.svg"
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "各模块子任务执行情况"
                            font.pixelSize: 14
                            color: Theme.darkText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
            
            // 任务分类
            Column {
                width: parent.width - 25
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 20
                
                GridLayout {
                    width: parent.width
                    columns: 3
                    rowSpacing: 15
                    columnSpacing: 20
                    
                    Components.TaskCategory {
                        id: dataIntegrationTasks
                        Layout.fillWidth: true
                        iconSource: "qrc:/icons/database.svg"
                        title: "数据整合模块"
                        
                        tasks: [
                            { name: "行情数据同步", status: "running" },
                            { name: "财务数据清洗", status: "completed" },
                            { name: "宏观数据更新", status: "completed" },
                            { name: "舆情数据抓取", status: "running" }
                        ]
                        
                        onTaskClicked: function(taskName) {
                            console.log("数据整合任务点击:", taskName)
                            
                            // 根据具体任务跳转到相应步骤
                            workflow.goToStep(1)
                            
                            // 更新任务状态（示例）
                            updateTaskStatus(dataIntegrationTasks, taskName, "completed")
                            
                            // 显示通知
                            showNotification("数据整合任务: " + taskName + " 已执行")
                        }
                        
                        onCategoryClicked: {
                            console.log("数据整合模块分类点击")
                            workflow.goToStep(1)
                        }
                    }
                    
                    Components.TaskCategory {
                        id: factorAnalysisTasks
                        Layout.fillWidth: true
                        iconSource: "qrc:/icons/chart-bar.svg"
                        title: "因子分析模块"
                        
                        tasks: [
                            { name: "动量因子计算", status: "completed" },
                            { name: "价值因子回测", status: "running" },
                            { name: "质量因子验证", status: "completed" },
                            { name: "波动率因子优化", status: "running" }
                        ]
                        
                        onTaskClicked: function(taskName) {
                            console.log("因子分析任务点击:", taskName)
                            
                            // 根据具体任务跳转到相应步骤
                            workflow.goToStep(2)
                            
                            // 根据任务类型处理
                            if (taskName.includes("因子计算")) {
                                factorAnalysisModule.simulateAction("new-factor")
                                showNotification("开始创建新的因子...")
                            } else if (taskName.includes("因子回测")) {
                                factorAnalysisModule.simulateAction("factor-backtest")
                                showNotification("开始因子回测...")
                            }
                            
                            // 更新任务状态
                            updateTaskStatus(factorAnalysisTasks, taskName, "completed")
                        }
                        
                        onCategoryClicked: {
                            console.log("因子分析模块分类点击")
                            workflow.goToStep(2)
                        }
                    }
                    
                    Components.TaskCategory {
                        id: strategyRiskTasks
                        Layout.fillWidth: true
                        iconSource: "qrc:/icons/cogs.svg"
                        title: "策略与风控模块"
                        
                        tasks: [
                            { name: "多因子策略回测", status: "running" },
                            { name: "组合风险监控", status: "completed" },
                            { name: "压力测试执行", status: "running" },
                            { name: "绩效归因分析", status: "completed" }
                        ]
                        
                        onTaskClicked: function(taskName) {
                            console.log("策略风控任务点击:", taskName)
                            
                            // 根据任务类型跳转到不同步骤
                            if (taskName.includes("策略回测") || taskName.includes("绩效归因")) {
                                // 策略相关任务
                                workflow.goToStep(3)
                                
                                if (taskName.includes("策略回测")) {
                                    backtestingModule.simulateAction("new-backtest")
                                    showNotification("开始策略回测配置...")
                                }
                            } else {
                                // 风控相关任务
                                workflow.goToStep(4)
                                
                                if (taskName.includes("风险监控")) {
                                    riskManagementModule.simulateAction("risk-monitor")
                                    showNotification("打开风险监控面板...")
                                } else if (taskName.includes("压力测试")) {
                                    riskManagementModule.simulateAction("stress-test")
                                    showNotification("开始压力测试...")
                                }
                            }
                            
                            // 更新任务状态
                            updateTaskStatus(strategyRiskTasks, taskName, "completed")
                        }
                        
                        onCategoryClicked: {
                            console.log("策略与风控模块分类点击")
                            // 默认跳转到策略回测（第三步）
                            workflow.goToStep(3)
                        }
                    }
                }
            }
            
            // ============= 6. 历史数据统计表格 =============
            Components.DataTable {
                width: parent.width - 50
                anchors.horizontalCenter: parent.horizontalCenter
                title: "历史数据统计"
                subtitle: "数据更新至: 2023-12-31"
                
                tableData: [
                    { 
                        category: "A股行情数据", 
                        coverage: "4,800+ 只股票", 
                        timeRange: "2005-01-01 至今", 
                        frequency: "日频/分钟级", 
                        completeness: "99.8%", 
                        recentUpdate: "updated",
                        status: "active"
                    },
                    { 
                        category: "财务数据", 
                        coverage: "4,200+ 家公司", 
                        timeRange: "2000-01-01 至今", 
                        frequency: "季度/年度", 
                        completeness: "98.5%", 
                        recentUpdate: "updated",
                        status: "active"
                    },
                    { 
                        category: "宏观数据", 
                        coverage: "200+ 指标", 
                        timeRange: "1990-01-01 至今", 
                        frequency: "月度/季度", 
                        completeness: "99.2%", 
                        recentUpdate: "updated",
                        status: "active"
                    },
                    { 
                        category: "舆情数据", 
                        coverage: "5,000+ 个来源", 
                        timeRange: "2018-01-01 至今", 
                        frequency: "实时", 
                        completeness: "95.3%", 
                        recentUpdate: "delayed",
                        status: "warning"
                    },
                    { 
                        category: "基金持仓数据", 
                        coverage: "8,000+ 只基金", 
                        timeRange: "2005-01-01 至今", 
                        frequency: "季度", 
                        completeness: "97.8%", 
                        recentUpdate: "updated",
                        status: "active"
                    },
                    { 
                        category: "另类数据", 
                        coverage: "15+ 个维度", 
                        timeRange: "2020-01-01 至今", 
                        frequency: "日频/周频", 
                        completeness: "92.4%", 
                        recentUpdate: "updated",
                        status: "active"
                    }
                ]
                
                onRowClicked: function(rowData) {
                    console.log("数据行点击:", rowData.category)
                    
                    // 根据数据类型跳转到相应的工作流程步骤
                    if (rowData.category.includes("行情") || rowData.category.includes("财务") || rowData.category.includes("宏观")) {
                        workflow.goToStep(1)
                        showNotification("打开" + rowData.category + "管理界面")
                    } else if (rowData.category.includes("舆情") || rowData.category.includes("另类")) {
                        workflow.goToStep(2)
                        showNotification("开始分析" + rowData.category)
                    } else if (rowData.category.includes("基金持仓")) {
                        workflow.goToStep(3)
                        showNotification("使用" + rowData.category + "进行策略回测")
                    }
                }
            }
            
            // ============= 7. 页脚 =============
            Rectangle {
                width: parent.width
                height: 60
                color: "#080d24"
                
                Text {
                    text: "© 2023 QuantAnalytica 量化软件数据分析平台"
                    font.pixelSize: 14
                    color: "#777"
                    anchors.centerIn: parent
                }
            }
        }
    }
    
    // ============= 辅助函数 =============
    
    // 数据清理进度弹窗（新版实现，供runDataCleaning等调用）
    function showCleaningProgressPopup(rules) {
        console.log("显示数据清理进度弹窗，规则:", JSON.stringify(rules))
        
        var component = Qt.createComponent("../../components/DataAnalysis/DataCleaningModal.qml")
        if (component.status === Component.Ready) {
            var popup = component.createObject(dashboardPage)
            if (popup) {
                console.log("数据清理弹窗创建成功")
                // 设置尺寸和位置
                popup.width = Math.min(dashboardPage.width * 0.6, 600)
                popup.height = Math.min(dashboardPage.height * 0.9, 470)
                popup.x = (dashboardPage.width - popup.width) / 2
                popup.y = (dashboardPage.height - popup.height) / 2
                
                // ✅ 只传递规则，不传递数据
                popup.rules = rules
                
                // 连接清洗请求信号
                popup.cleaningRequested.connect(function(rules) {
                    console.log("清洗请求，规则:", JSON.stringify(rules))
                    
                    // 调用C++异步清洗方法，使用DataFetchController中的当前数据
                    console.log("调用C++异步清洗方法")
                    dataFetchController.cleanDataAsync(dataFetchController.fetchedData, rules)
                })
                
                // 连接清洗完成信号
                popup.cleaningCompleted.connect(function() {
                    console.log("数据清理完成")
                    showNotification("数据清理完成")
                    // 更新任务状态
                    updateTaskStatus(dataIntegrationTasks, "财务数据清洗", "completed")
                })
                
                popup.closed.connect(function() {
                    console.log("数据清理弹窗关闭")
                    Qt.callLater(function() {
                        if (popup) {
                            popup.destroy()
                        }
                    })
                })
                
                // 打开弹窗
                popup.open()
                
                showNotification("数据清洗窗口已打开")
                
            } else {
                console.error("数据清理弹窗对象创建失败")
            }
        } else if (component.status === Component.Error) {
            console.error("数据清理组件加载失败:", component.errorString())
            showNotification("数据清理功能暂时不可用")
        }
    }
    
    // 数据预览弹窗（完整版）
    function showDataPreview(data) {
        console.log("显示数据预览弹窗")
        
        // 确保数据格式正确
        var previewData = {
            appliedRules: data.appliedRules || Object.keys(currentRules || {}).length,
            stockCount: data.stockCount || (cleanedData ? cleanedData.length : 0),
            timeRange: data.timeRange || (currentRules.timeRange ? 
                currentRules.timeRange.start + " 至 " + currentRules.timeRange.end : 
                "未设置"),
            priceRange: data.priceRange || (currentRules.priceFilter ? 
                currentRules.priceFilter.min + "元 至 " + currentRules.priceFilter.max + "元" : 
                "未设置"),
            volumeFilter: data.volumeFilter || (currentRules.volumeFilter ? 
                "成交量 > " + currentRules.volumeFilter.min + "手" : 
                "未设置"),
            completeness: data.completeness || "99.5%",
            sampleData: data.sampleData || (cleanedData ? cleanedData.slice(0, 5).map(item => ({
                code: item.code || "",
                name: item.name || "",
                price: item.close || 0,
                change: (item.change || 0) > 0 ? "+" + item.change.toFixed(2) + "%" : item.change.toFixed(2) + "%"
            })) : [])
        }
        
        var component = Qt.createComponent("../../components/DataAnalysis/DataPreviewModal.qml")
        if (component.status === Component.Ready) {
            var popup = component.createObject(dashboardPage)
            if (popup) {
                console.log("数据预览弹窗创建成功")
                
                popup.width = Math.min(dashboardPage.width * 0.9, 1000)
                popup.height = Math.min(dashboardPage.height * 0.9, 800)
                popup.x = (dashboardPage.width - popup.width) / 2
                popup.y = (dashboardPage.height - popup.height) / 2
                
                // 传递清理后的数据
                popup.cleanedData = cleanedData || []
                
                // 连接导出完成信号
                popup.exportCompleted.connect(function() {
                    console.log("数据导出完成")
                    showNotification("数据已成功导出到下一流程")
                })
                
                popup.closed.connect(function() {
                    console.log("数据预览弹窗关闭")
                    Qt.callLater(function() {
                        if (popup) {
                            popup.destroy()
                        }
                    })
                })
                
                popup.open()
                
            } else {
                console.error("数据预览弹窗对象创建失败")
                showNotification("无法创建数据预览窗口")
            }
        } else if (component.status === Component.Error) {
            console.error("数据预览组件加载失败:", component.errorString())
            showNotification("数据预览功能暂时不可用")
        }
    }
    
    // 数据预览弹窗（完整版，使用清理后的数据）
    function showDataPreviewWithData(cleanedData) {
        console.log("显示数据预览弹窗（完整版），数据条数:", cleanedData.length)
        
        var component = Qt.createComponent("../../components/DataAnalysis/DataPreviewModal.qml")
        if (component.status === Component.Ready) {
            var popup = component.createObject(dashboardPage)
            if (popup) {
                console.log("数据预览弹窗创建成功")
                
                popup.width = Math.min(dashboardPage.width * 0.9, 1000)
                popup.height = Math.min(dashboardPage.height * 0.9, 800)
                popup.x = (dashboardPage.width - popup.width) / 2
                popup.y = (dashboardPage.height - popup.height) / 2
                
                // 传递清理后的数据
                popup.cleanedData = cleanedData
                
                // 连接导出完成信号
                popup.exportCompleted.connect(function() {
                    console.log("数据导出完成")
                    showNotification("数据已成功导出到下一流程")
                })
                
                popup.closed.connect(function() {
                    console.log("数据预览弹窗关闭")
                    Qt.callLater(function() {
                        if (popup) {
                            popup.destroy()
                        }
                    })
                })
                
                popup.open()
                
            } else {
                console.error("数据预览弹窗对象创建失败")
            }
        } else if (component.status === Component.Error) {
            console.error("数据预览组件加载失败:", component.errorString())
            // 备用方案：显示简化版预览
            showDataPreview({
                appliedRules: Object.keys(currentRules || {}).length,
                stockCount: cleanedData.length,
                timeRange: currentRules.timeRange ? 
                    currentRules.timeRange.start + " 至 " + currentRules.timeRange.end : 
                    "未设置",
                priceRange: currentRules.priceFilter ? 
                    currentRules.priceFilter.min + "元 至 " + currentRules.priceFilter.max + "元" : 
                    "未设置",
                volumeFilter: currentRules.volumeFilter ? 
                    "成交量 > " + currentRules.volumeFilter.min + "手" : 
                    "未设置",
                completeness: "99.5%",
                sampleData: cleanedData.slice(0, 5).map(item => ({
                    code: item.code || "",
                    name: item.name || "",
                    price: item.close || 0,
                    change: (item.change || 0) > 0 ? "+" + item.change.toFixed(2) + "%" : item.change.toFixed(2) + "%"
                }))
            })
        }
    }

    // 保存清理结果
    function saveCleanedData(cleanedData) {
        console.log("保存清理结果，数据条数:", cleanedData.length)
        
        // 保存到全局属性
        dashboardPage.cleanedData = cleanedData
        
        // 这里可以实际保存到数据库或文件
        if (cleanedData.length > 0) {
            console.log("第一条数据示例:", JSON.stringify(cleanedData[0]))
        }
        
        // 更新数据预览信息
        dataPreviewInfo = {
            appliedRules: Object.keys(currentRules || {}).length,
            stockCount: cleanedData.length,
            timeRange: currentRules.timeRange ? 
                currentRules.timeRange.start + " 至 " + currentRules.timeRange.end : 
                "未设置",
            priceRange: currentRules.priceFilter ? 
                currentRules.priceFilter.min + "元 至 " + currentRules.priceFilter.max + "元" : 
                "未设置",
            volumeFilter: currentRules.volumeFilter ? 
                "成交量 > " + currentRules.volumeFilter.min + "手" : 
                "未设置",
            completeness: "99.5%",
            sampleData: cleanedData.slice(0, 5).map(item => ({
                code: item.code || "",
                name: item.name || "",
                price: item.close || 0,
                change: (item.change || 0) > 0 ? "+" + item.change.toFixed(2) + "%" : item.change.toFixed(2) + "%"
            }))
        }
        
        showNotification("清洗结果已保存，可在数据预览中查看")
    }
    
    // 数据导出函数
    function exportData(format, data) {
        console.log("导出数据，格式:", format)
        showNotification("开始导出" + format + "格式数据...")
        
        // 使用Timer代替setTimeout
        var exportTimer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 1500; running: true }', dashboardPage)
        exportTimer.triggered.connect(function() {
            showNotification("数据导出完成")
            exportTimer.destroy()
        })
    }
    
    // 显示规则配置弹窗
    function showRulesConfigPopup() {
        console.log("显示规则配置弹出窗口")
        
        var component = Qt.createComponent("../../components/DataAnalysis/RulesConfigModal.qml")
        
        if (component.status === Component.Ready) {
            var popup = component.createObject(dashboardPage)
            
            if (!popup) {
                console.error("创建弹窗失败:", component.errorString())
                return
            }
            
            console.log("规则配置弹窗创建成功")
            
            popup.width = Math.min(dashboardPage.width * 0.8, 900)
            popup.height = Math.min(dashboardPage.height * 0.8, 500)
            popup.x = (dashboardPage.width - popup.width) / 2
            popup.y = (dashboardPage.height - popup.height) / 2
            
            // 连接规则保存信号
            popup.rulesSaved.connect(function(rules) {
                console.log("规则已保存:", JSON.stringify(rules))
                showNotification("数据处理规则已更新")
                
                // 保存规则到配置文件
                saveRulesToConfig(rules)
            })
            
            // 连接关闭信号
            popup.closed.connect(function() {
                console.log("规则配置弹窗关闭")
                if (popup) {
                    popup.destroy()
                }
            })
            
            // 打开弹窗
            popup.open()
            
        } else if (component.status === Component.Error) {
            console.error("组件创建失败:", component.errorString())
        }
    }
    
    // 保存规则到配置文件
    function saveRulesToConfig(rules) {
        console.log("保存规则到配置:", rules)
        
        try {
            // 这里保存到本地存储或发送到服务器
            console.log("规则已保存到配置")
            
            // 更新当前规则
            currentRules = rules
            
            // 可以在这里更新UI状态
            if (dataIntegrationModule && dataIntegrationModule.updateModuleStatus) {
                dataIntegrationModule.updateModuleStatus("configured")
            }
            
            // ❌ 移除自动运行数据清理的逻辑
            // 用户明确要求：不要在保存规则时开始数据查询
            // 应该让规则窗口完全结束，然后用户手动点击开始清洗时才执行
            
            showNotification("规则已保存，请在数据清洗窗口中点击开始清洗")
            
        } catch (error) {
            console.error("保存规则失败:", error)
            showNotification("保存规则失败: " + error.message)
        }
    }
    
    // 修改 runDataCleaning 函数，确保能正确调用
    function runDataCleaning() {
        console.log("运行数据清理")
        
        // 获取当前规则
        var currentRules = loadRulesFromConfig()
        
        if (currentRules) {
            showNotification("开始数据清理处理...")
            
            // ✅ 正确调用数据清理弹窗
            showCleaningProgressPopup(currentRules)
            
            // 更新任务状态
            updateTaskStatus(dataIntegrationTasks, "财务数据清洗", "running")
            
        } else {
            showNotification("请先配置数据清理规则")
            // 自动打开规则配置窗口
            showRulesConfigPopup()
        }
    }
    
    // 加载已保存的规则
    function loadRulesFromConfig() {
        console.log("加载已保存的规则")
        
        try {
            // 获取当前年份 - 现在是2026年
            var currentYear = 2026
            var startDate = currentYear + "-01-01"
            var endDate = currentYear + "-12-31"
            
            console.log("使用当前年份时间范围:", startDate, "到", endDate)
            
            // 返回默认规则，使用当前年份
            var rules = {
                market: { aShares: true, hk: false, us: false },
                timeRange: { start: startDate, end: endDate },
                priceFilter: { 
                    enabled: true,
                    min: 10, 
                    max: 2000 
                },
                volumeFilter: { 
                    enabled: true,
                    min: 100000,
                    minVolume: 10000, 
                    minTurnover: 1.0 
                },
                completenessFilter: true,
                outlierFilter: true
            }
            
            console.log("返回规则:", JSON.stringify(rules))
            return rules
            
        } catch (error) {
            console.error("加载规则失败:", error)
            return null
        }
    }
    
    // 其他辅助函数保持不变
    function showNotification(message) {
        console.log("通知:", message)
        // 这里可以实现更复杂的通知系统
        notificationPanel.show(message)
    }
    
    function getStepName(stepIndex) {
        var stepNames = ["数据整合与清洗", "因子分析与特征工程", "策略回测与验证", "风险管理与优化", "报告与部署"]
        return stepIndex >= 1 && stepIndex <= 5 ? stepNames[stepIndex - 1] : "未知步骤"
    }
    
    function openModule(stepIndex) {
        console.log("打开模块，步骤:", stepIndex)
        // 根据步骤索引打开相应的功能模块
        switch(stepIndex) {
            case 1:
                // 打开数据整合模块
                dataIntegrationModule.cardClicked()
                showNotification("打开数据整合与清洗模块")
                break
            case 2:
                // 打开因子分析模块
                factorAnalysisModule.cardClicked()
                showNotification("打开因子分析与特征工程模块")
                break
            case 3:
                // 打开策略回测模块
                backtestingModule.cardClicked()
                showNotification("打开策略回测与验证模块")
                break
            case 4:
                // 打开风险管理模块
                riskManagementModule.cardClicked()
                showNotification("打开风险管理与优化模块")
                break
            case 5:
                // 打开报告部署模块
                console.log("打开报告部署功能")
                showNotification("打开报告与部署模块")
                // 这里可以添加打开报告模块的代码
                break
        }
    }
    
    function showAddDataSourcePopup() {
        console.log("显示添加数据源弹出窗口")
        
        var component = Qt.createComponent("../../components/DataAnalysis/DataSourceModal.qml")
        if (component.status === Component.Ready) {
            console.log("DataSourceModal 组件加载成功")
            
            var popup = component.createObject(dashboardPage)
            
            if (popup) {
                console.log("弹窗对象创建成功")
                
                popup.width = Math.min(dashboardPage.width * 0.8, 620)
                popup.height = Math.min(dashboardPage.height * 0.8, 480)
                popup.x = (dashboardPage.width - popup.width) / 2
                popup.y = (dashboardPage.height - popup.height) / 2
                
                if (popup.sourceAdded !== undefined) {
                    popup.sourceAdded.connect(function(sourceInfo) {
                        console.log("数据源已添加:", sourceInfo)
                        showNotification("数据源 '" + sourceInfo.name + "' 已成功添加")
                        
                        updateTaskStatus(dataIntegrationTasks, "行情数据同步", "running")
                        
                        triggerDataSourceSync(sourceInfo)
                    })
                }
                
                popup.closed.connect(function() {
                    console.log("DataSourceModal 弹窗关闭")
                    if (popup) {
                        popup.destroy()
                    }
                })
                
                popup.open()
            } else {
                console.error("DataSourceModal 弹窗对象创建失败")
            }
        } else if (component.status === Component.Error) {
            console.error("DataSourceModal 组件加载失败:", component.errorString())
        }
    }
    
    function triggerDataSourceSync(sourceInfo) {
        console.log("触发数据源同步:", sourceInfo.name)
        
        showNotification("开始同步 " + sourceInfo.name + " 数据...")
        
        // 总是加载整个数据集（空股票代码表示加载整个市场）
        var startDate = sourceInfo.timeRange.start
        var endDate = sourceInfo.timeRange.end
        
        console.log("加载数据集:", sourceInfo.name, "时间范围:", startDate, "-", endDate)
        
        // 调用DataFetchController加载整个数据集
        dataFetchController.loadFromDatabase("", startDate, endDate)
        
        // 监听数据加载完成信号
        dataFetchController.dataLoadedFromDatabase.connect(function(success, message, count) {
            if (success) {
                console.log("数据集同步完成:", count, "条数据")
                showNotification(sourceInfo.name + " 数据集同步完成 (" + count + "条)")
                
                // 更新任务状态
                updateTaskStatus(dataIntegrationTasks, "行情数据同步", "completed")
            } else {
                console.log("数据集同步失败:", message)
                showNotification(sourceInfo.name + " 数据集同步失败: " + message)
            }
            
            // 断开连接，避免重复监听
            dataFetchController.dataLoadedFromDatabase.disconnect(arguments.callee)
        })
    }
    
    function showStepDetails(stepIndex) {
        console.log("显示步骤详情:", stepIndex)
        var message = "步骤 " + stepIndex + ": " + getStepName(stepIndex) + "\n\n"
        
        switch(stepIndex) {
            case 1:
                message += "功能: 数据导入、清洗、标准化处理\n"
                message += "支持的数据源: 行情数据、财务数据、宏观数据、舆情数据\n"
                message += "输出: 清洗后的标准化数据集"
                break
            case 2:
                message += "功能: 因子开发、特征工程、因子有效性检验\n"
                message += "支持的分析: 动量因子、价值因子、质量因子、技术因子\n"
                message += "输出: 有效因子库和特征数据集"
                break
            case 3:
                message += "功能: 策略回测、绩效分析、参数优化\n"
                message += "支持的策略: 多因子选股、均值回归、趋势跟踪\n"
                message += "输出: 回测报告和绩效指标"
                break
            case 4:
                message += "功能: 风险管理、组合优化、压力测试\n"
                message += "支持的风险指标: VaR、最大回撤、夏普比率\n"
                message += "输出: 风险报告和优化建议"
                break
            case 5:
                message += "功能: 报告生成、策略部署、实时监控\n"
                message += "支持的格式: PDF、Excel、HTML报告\n"
                message += "输出: 完整分析报告和部署配置"
                break
        }
        
        showNotification(message)
    }
    
    function handleNavigation(action) {
        console.log("处理导航:", action)
        switch(action) {
            case "prev":
                if (workflow.currentStep > 1) {
                    workflow.currentStep--
                    showNotification("返回步骤 " + workflow.currentStep)
                }
                break
            case "next":
                if (workflow.currentStep < 5) {
                    workflow.currentStep++
                    showNotification("前进到步骤 " + workflow.currentStep)
                } else {
                    showNotification("恭喜！所有步骤已完成！")
                }
                break
            case "complete":
                if (workflow.currentStep < 5) {
                    var currentStep = workflow.currentStep
                    workflow.currentStep++
                    showNotification("步骤" + currentStep + "完成，前进到步骤" + workflow.currentStep)
                    
                    updateModuleTasks(currentStep)
                } else {
                    showNotification("所有步骤已完成！")
                }
                break
        }
    }
    
    function updateTaskStatus(taskCategory, taskName, newStatus) {
        console.log("更新任务状态:", taskCategory.title, taskName, newStatus)
        
        for (var i = 0; i < taskCategory.tasks.length; i++) {
            if (taskCategory.tasks[i].name === taskName) {
                taskCategory.tasks[i].status = newStatus
                break
            }
        }
        
        // 触发更新（如果组件有这个方法）
        if (taskCategory.updateTasks && typeof taskCategory.updateTasks === "function") {
            taskCategory.updateTasks()
        }
    }
    
    function updateModuleTasks(stepIndex) {
        switch(stepIndex) {
            case 1:
                updateTaskStatus(dataIntegrationTasks, "行情数据同步", "completed")
                updateTaskStatus(dataIntegrationTasks, "财务数据清洗", "completed")
                break
            case 2:
                updateTaskStatus(factorAnalysisTasks, "动量因子计算", "completed")
                updateTaskStatus(factorAnalysisTasks, "价值因子回测", "completed")
                break
            case 3:
                updateTaskStatus(strategyRiskTasks, "多因子策略回测", "completed")
                updateTaskStatus(strategyRiskTasks, "绩效归因分析", "completed")
                break
            case 4:
                updateTaskStatus(strategyRiskTasks, "组合风险监控", "completed")
                updateTaskStatus(strategyRiskTasks, "压力测试执行", "completed")
                break
        }
    }
    
    function updateTaskStatusForStep(stepIndex) {
        console.log("为步骤更新任务状态:", stepIndex)
        // 避免访问可能导致上下文问题的属性
        // 只进行简单的日志记录
        try {
            // 安全的日志记录
            console.log("步骤", stepIndex, "的任务状态更新")
        } catch (error) {
            console.error("更新任务状态时出错:", error)
        }
    }
    
    // 模拟通知面板
    Item {
        id: notificationPanel
        
        function show(message) {
            console.log("显示通知:", message)
        }
    }
    
    // 创建DataFetchController实例
    DataFetchController {
        id: dataFetchController
        
        onDataLoadedFromDatabase: function(success, message, count) {
            console.log("数据加载完成:", success, message, count)
            if (success) {
                console.log("成功加载", count, "条数据")
                // 数据已加载到dataFetchController.fetchedData中
            } else {
                console.log("数据加载失败:", message)
            }
        }
        
        onDataCleaningCompleted: function(success, message, cleanedData) {
            console.log("数据清洗完成:", success, message, "数据条数:", cleanedData.length)
            if (success) {
                console.log("清洗成功，保留", cleanedData.length, "条数据")
                // 更新全局的cleanedData属性
                dashboardPage.cleanedData = cleanedData
                showNotification("数据清洗完成，保留 " + cleanedData.length + " 条数据")
            } else {
                console.log("清洗失败:", message)
                showNotification("数据清洗失败: " + message)
            }
        }
        
        onDataCleaningProgress: function(progress, message) {
            console.log("清洗进度:", progress, message)
            // 这里可以更新UI进度条
        }
    }
    
    // 从数据库加载真实数据的函数 - 如果没有数据就返回空
    function loadRealDataFromDatabase() {
        console.log("尝试从数据库加载真实数据")
        
        try {
            // 调试：检查currentRules
            console.log("currentRules:", JSON.stringify(currentRules))
            console.log("currentRules.timeRange:", currentRules ? currentRules.timeRange : "undefined")
            
            // 调用DataFetchController的loadFromDatabase方法
            // 使用默认参数：空股票代码，使用规则中的时间范围
            var startDate = currentRules && currentRules.timeRange ? currentRules.timeRange.start : "2026-01-01"
            var endDate = currentRules && currentRules.timeRange ? currentRules.timeRange.end : "2026-12-31"
            
            // 确保日期格式正确
            if (startDate && typeof startDate === 'object' && startDate.toISOString) {
                startDate = startDate.toISOString().split('T')[0]
            }
            if (endDate && typeof endDate === 'object' && endDate.toISOString) {
                endDate = endDate.toISOString().split('T')[0]
            }
            
            console.log("调用C++加载数据，时间范围:", startDate, "-", endDate)
            
            // 调用C++方法加载数据
            dataFetchController.loadFromDatabase("", startDate, endDate)
            
            // 等待数据加载完成
            // 注意：这是一个异步操作，我们需要等待信号
            // 这里我们直接返回空数组，让UI显示"没有数据"
            console.log("数据加载请求已发送，等待C++处理")
            
            // 检查是否有已加载的数据
            if (dataFetchController.fetchedData && dataFetchController.fetchedData.length > 0) {
                console.log("已有缓存数据:", dataFetchController.fetchedData.length, "条")
                return dataFetchController.fetchedData
            } else {
                console.log("没有缓存数据，返回空数组")
                return []
            }
            
        } catch (error) {
            console.error("加载真实数据失败:", error)
            return []
        }
    }
}

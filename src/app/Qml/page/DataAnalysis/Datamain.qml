// DashboardPage.qml - 仪表板页面（已嵌入工作流程组件）- 更新TaskCategory信号处理
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../components/DataAnalysis" as Components
import ConsoleUi 1.0 as Theme

Item {
    id: dashboardPage
    
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
                    console.log("新建项目 clicked")
                    // 新建项目时重置工作流程到第一步
                    workflow.reset()
                }
                
                onLoadTemplateClicked: {
                    console.log("使用模板 clicked")
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
                        // 如果点击的是数据相关操作，同步更新工作流程
                        if (actionId === "add-source" || actionId === "run-clean") {
                            workflow.goToStep(1)
                        }
                    }
                    
                    onCardClicked: {
                        console.log("模块卡片点击: data-integration")
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
    
    // 辅助函数
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
    
    function showStepDetails(stepIndex) {
        console.log("显示步骤详情:", stepIndex)
        // 显示步骤的详细信息和配置选项
        var message = "步骤 " + stepIndex + ": " + getStepName(stepIndex) + "\n\n"
        
        // 根据步骤添加详细说明
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
                // 标记当前步骤为完成
                if (workflow.currentStep < 5) {
                    var currentStep = workflow.currentStep
                    workflow.currentStep++
                    showNotification("步骤" + currentStep + "完成，前进到步骤" + workflow.currentStep)
                    
                    // 更新对应模块的任务状态
                    updateModuleTasks(currentStep)
                } else {
                    showNotification("所有步骤已完成！")
                }
                break
        }
    }
    
    function updateTaskStatus(taskCategory, taskName, newStatus) {
        console.log("更新任务状态:", taskCategory.title, taskName, newStatus)
        
        // 在实际应用中，这里会更新任务模型数据
        // 这里只是模拟更新
        for (var i = 0; i < taskCategory.tasks.length; i++) {
            if (taskCategory.tasks[i].name === taskName) {
                taskCategory.tasks[i].status = newStatus
                break
            }
        }
        
        // 刷新任务显示
        taskCategory.updateTasks()
    }
    
    function updateModuleTasks(stepIndex) {
        // 根据完成的步骤更新对应模块的任务状态
        switch(stepIndex) {
            case 1:
                // 完成数据整合步骤，更新相关任务
                updateTaskStatus(dataIntegrationTasks, "行情数据同步", "completed")
                updateTaskStatus(dataIntegrationTasks, "财务数据清洗", "completed")
                break
            case 2:
                // 完成因子分析步骤，更新相关任务
                updateTaskStatus(factorAnalysisTasks, "动量因子计算", "completed")
                updateTaskStatus(factorAnalysisTasks, "价值因子回测", "completed")
                break
            case 3:
                // 完成策略回测步骤，更新相关任务
                updateTaskStatus(strategyRiskTasks, "多因子策略回测", "completed")
                updateTaskStatus(strategyRiskTasks, "绩效归因分析", "completed")
                break
            case 4:
                // 完成风险管理步骤，更新相关任务
                updateTaskStatus(strategyRiskTasks, "组合风险监控", "completed")
                updateTaskStatus(strategyRiskTasks, "压力测试执行", "completed")
                break
        }
    }
    
    function updateTaskStatusForStep(stepIndex) {
        // 当步骤被激活时，更新对应的任务状态
        console.log("为步骤更新任务状态:", stepIndex)
        
        // 这里可以根据步骤索引激活相应的任务
        // 在实际应用中，可能会有更复杂的逻辑
    }
    
    // 模拟通知面板
    Item {
        id: notificationPanel
        
        function show(message) {
            console.log("显示通知:", message)
            // 在实际应用中，这里会显示一个通知组件
        }
    }
}
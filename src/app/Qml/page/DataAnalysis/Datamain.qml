// DashboardPage.qml - 仪表板页面
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../components/DataAnalysis" as Components
import ConsoleUi 1.0 as Theme

Page {
    id: dashboardPage
    width: parent.width
    height: parent.height
    padding: 0
    
    // 滚动区域
    ScrollView {
        anchors.fill: parent
        clip: true
        
        Column {
            width: dashboardPage.width
            spacing: 30
            padding: 30
            
            // 快速开始区域
            Components.QuickStart {
                id: quickStart
                width: parent.width
                
                onNewProjectClicked: {
                    console.log("新建项目 clicked")
                    // 这里可以打开新建项目对话框
                }
                
                onLoadTemplateClicked: {
                    console.log("使用模板 clicked")
                    // 这里可以打开模板选择对话框
                }
            }
            
            // 工作流程
            Components.WorkflowSteps {
                id: workflow
                width: parent.width
                
                onStepClicked: {
                    console.log("步骤点击: " + index)
                    workflow.activeStep = index
                    // 这里可以根据步骤跳转到相应模块
                }
            }
            
            // 核心功能模块标题
            Row {
                width: parent.width
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
                            //color: Theme.darkText
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
            
            // 核心功能模块网格
            GridLayout {
                width: parent.width
                columns: 2
                rowSpacing: 24
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
                    
                    onActionClicked: (actionId) => {
                        console.log("模块操作点击: " + moduleId + " - " + actionId)
                    }
                    
                    onCardClicked: {
                        console.log("模块卡片点击: " + moduleId)
                        // 这里可以跳转到模块详情页面
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
                    
                    onActionClicked: (actionId) => {
                        console.log("模块操作点击: " + moduleId + " - " + actionId)
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
                    
                    onActionClicked: (actionId) => {
                        console.log("模块操作点击: " + moduleId + " - " + actionId)
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
                    
                    onActionClicked: (actionId) => {
                        console.log("模块操作点击: " + moduleId + " - " + actionId)
                    }
                }
            }
            
            // 系统概览标题
            Row {
                width: parent.width
                height: 40
                spacing: 20
                
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
                            //color: Theme.darkText
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
            
            // 系统概览卡片
            GridLayout {
                width: parent.width
                columns: 4
                rowSpacing: 24
                columnSpacing: 24
                
                Components.StatsCard {
                    iconSource: "qrc:/icons/filter.svg"
                    iconColor: Theme.primaryColor
                    label: "数据清洗任务"
                    value: "18"
                    trendText: "今日完成: 12"
                    trendUp: true
                }
                
                Components.StatsCard {
                    iconSource: "qrc:/icons/calculator.svg"
                    iconColor: Theme.accentColor
                    label: "因子计算任务"
                    value: "24"
                    trendText: "较上周 +8"
                    trendUp: true
                }
                
                Components.StatsCard {
                    iconSource: "qrc:/icons/history.svg"
                    iconColor: Theme.successColor
                    label: "策略回测任务"
                    value: "9"
                    trendText: "较上周 -3"
                    trendUp: false
                }
                
                Components.StatsCard {
                    iconSource: "qrc:/icons/shield.svg"
                    iconColor: Theme.dangerColor
                    label: "风险监控任务"
                    value: "15"
                    trendText: "实时运行中"
                    trendUp: true
                }
            }
            
            // 模块任务状态
            Column {
                width: parent.width
                spacing: 20
                
                Row {
                    width: parent.width
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
                                //color: Theme.darkText
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
                
                // 任务分类网格
                GridLayout {
                    width: parent.width
                    columns: 3
                    rowSpacing: 20
                    columnSpacing: 20
                    
                    Components.TaskCategory {
                        id: dataTasks
                        Layout.fillWidth: true
                        iconSource: "qrc:/icons/database.svg"
                        title: "数据整合模块"
                        
                        tasks: [
                            { name: "行情数据同步", status: "running" },
                            { name: "财务数据清洗", status: "completed" },
                            { name: "宏观数据更新", status: "completed" },
                            { name: "舆情数据抓取", status: "running" }
                        ]
                    }
                    
                    Components.TaskCategory {
                        id: factorTasks
                        Layout.fillWidth: true
                        iconSource: "qrc:/icons/chart-bar.svg"
                        title: "因子分析模块"
                        
                        tasks: [
                            { name: "动量因子计算", status: "completed" },
                            { name: "价值因子回测", status: "running" },
                            { name: "质量因子验证", status: "completed" },
                            { name: "波动率因子优化", status: "running" }
                        ]
                    }
                    
                    Components.TaskCategory {
                        id: strategyTasks
                        Layout.fillWidth: true
                        iconSource: "qrc:/icons/cogs.svg"
                        title: "策略与风控模块"
                        
                        tasks: [
                            { name: "多因子策略回测", status: "running" },
                            { name: "组合风险监控", status: "completed" },
                            { name: "压力测试执行", status: "running" },
                            { name: "绩效归因分析", status: "completed" }
                        ]
                    }
                }
            }
            
            // 历史数据统计表格
            Components.DataTable {
                id: dataStatistics
                width: parent.width
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
            }
            
            // 简单的页脚
            Rectangle {
                width: parent.width
                height: 60
                color: "#080d24"
                radius: 0
                
                Text {
                    text: "© 2023 QuantAnalytica 量化软件数据分析平台"
                    font.pixelSize: 14
                    color: "#777"
                    anchors.centerIn: parent
                }
            }
        }
    }
    
    // 定时更新数据
    Timer {
        interval: 10000
        running: true
        repeat: true
        
        onTriggered: {
            updateStatsCards()
            updateTaskStatus()
        }
    }
    
    // 更新统计卡片函数
    function updateStatsCards() {
        // 随机更新统计卡片数值
        var cards = [dataIntegrationModule, factorAnalysisModule, backtestingModule, riskManagementModule]
        for (var i = 0; i < 4; i++) {
            var change = Math.floor(Math.random() * 3) - 1 // -1, 0, 1
            // 这里应该更新对应的统计数据
        }
    }
    
    // 更新任务状态函数
    function updateTaskStatus() {
        // 随机切换任务状态
        console.log("更新任务状态...")
    }
}
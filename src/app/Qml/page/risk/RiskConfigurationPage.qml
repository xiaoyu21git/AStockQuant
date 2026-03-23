// RiskConfigurationPage.qml - 风险管理配置页面（动态参数版本）
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../components/FactorWorkbench/Creation/components" as PluginComponents
import "../../utils/RiskBacktestMetaLoader.js" as RiskBacktestMeta
import ConsoleUi 1.0 as Theme

Item {
    id: riskConfigPage
    
    // ============ 动态参数配置 ============
    
    // 动态参数生成器
    property var dynamicParamConfigs: []
    property var dynamicParamValues: ({})
    property bool parametersLoaded: false
    
    // 风险配置摘要
    property var riskSummary: ({
        stopLossPercent: 10.0,
        takeProfitPercent: 20.0,
        maxDrawdownLimit: 0.2,
        maxPositionPercent: 30.0,
        maxPositions: 10,
        maxTotalExposure: 95.0,
        maxIndustryExposure: 40.0,
        maxThemeExposure: 25.0,
        maxDailyLoss: -3.0,
        maxCorrelation: 70.0
    })
    
    // 插件化组件注册表
    PluginComponents.ParamComponents {
        id: paramComponents
        Component.onCompleted: {
            console.log("参数组件初始化完成")
            // 注册所有组件
            if (typeof paramComponents.registerAllComponents === 'function') {
                paramComponents.registerAllComponents()
            }
            // 初始化动态参数
            riskConfigPage.initDynamicParams()
        }
    }
    
    // ============ UI 布局 ============
    
    // 滚动区域
    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        
        ColumnLayout {
            width: Math.min(parent.width, 1200) - 20
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 20
            
            // ============= 1. 页面标题 =============
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                
                Text {
                    text: "风险管理配置"
                    font.pixelSize: 28
                    font.bold: true
                    color: Theme.darkText
                }
                
                Item { Layout.fillWidth: true }
                
                // 保存按钮
                Button {
                    text: "保存配置"
                    font.pixelSize: 14
                    font.bold: true
                    padding: 12
                    background: Rectangle {
                        radius: 8
                        color: Theme.accentColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        saveRiskConfiguration()
                    }
                }
            }
            
            // ============= 2. 风险配置说明 =============
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                radius: 8
                color: Theme.darkCard
                border.color: Theme.darkBorder
                border.width: 1
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    
                    Image {
                        source: "qrc:/icons/shield.svg"
                        width: 32
                        height: 32
                        Layout.alignment: Qt.AlignVCenter
                    }
                    
                    ColumnLayout {
                        spacing: 4
                        
                        Text {
                            text: "风险配置说明"
                            font.pixelSize: 16
                            font.bold: true
                            color: Theme.darkText
                        }
                        
                        Text {
                            text: "配置止损止盈、仓位限制、风险监控等参数，确保交易安全"
                            font.pixelSize: 14
                            color: Theme.darkTextSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            
            // ============= 3. 动态参数配置区域 =============
            Rectangle {
                id: dynamicParamsContainer
                Layout.fillWidth: true
                Layout.preferredHeight: 600
                radius: 8
                color: Theme.darkCard
                border.color: Theme.darkBorder
                border.width: 1
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16
                    
                    Text {
                        text: "风险管理参数配置"
                        font.pixelSize: 18
                        font.bold: true
                        color: Theme.darkText
                    }
                    
                    Text {
                        text: "使用统一的动态参数生成器，配置风险管理和风险控制相关参数"
                        font.pixelSize: 14
                        color: Theme.darkTextSecondary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    
                    // 动态参数生成器实例
                    PluginComponents.DynamicParamGenerator {
                        id: dynamicParamGenerator
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        paramRegistry: paramComponents.paramRegistry
                        configs: riskConfigPage.dynamicParamConfigs
                        values: riskConfigPage.dynamicParamValues
                        
                        // 当参数值变化时更新
                        onParamsChanged: function(newValues) {
                            console.log("动态参数变化:", newValues)
                            riskConfigPage.dynamicParamValues = newValues
                            riskConfigPage.updateRiskSummary(newValues)
                        }
                    }
                    
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: parametersLoaded ? "参数配置已加载" : "正在加载动态参数配置..."
                        font.pixelSize: 14
                        color: parametersLoaded ? Theme.successColor : Theme.warningColor
                    }
                }
            }
            
            // ============= 4. 配置预览 =============
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                radius: 8
                color: Theme.darkCard
                border.color: Theme.darkBorder
                border.width: 1
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
                    Text {
                        text: "配置预览"
                        font.pixelSize: 18
                        font.bold: true
                        color: Theme.darkText
                    }
                    
                    // 配置摘要
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 20
                        
                        // 第一列
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "止损: " + riskSummary.stopLossPercent.toFixed(1) + "%"
                                font.pixelSize: 14
                                color: riskSummary.stopLossPercent < 5 ? Theme.dangerColor : Theme.successColor
                            }
                            
                            Text {
                                text: "止盈: " + riskSummary.takeProfitPercent.toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.successColor
                            }
                            
                            Text {
                                text: "单股仓位: " + riskSummary.maxPositionPercent.toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                            
                            Text {
                                text: "最大持仓数: " + riskSummary.maxPositions + " 只"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                        }
                        
                        // 第二列
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "总仓位: " + riskSummary.maxTotalExposure.toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                            
                            Text {
                                text: "单日亏损: " + riskSummary.maxDailyLoss.toFixed(1) + "%"
                                font.pixelSize: 14
                                color: riskSummary.maxDailyLoss < -5 ? Theme.dangerColor : Theme.warningColor
                            }
                            
                            Text {
                                text: "行业限制: " + riskSummary.maxIndustryExposure.toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                            
                            Text {
                                text: "题材限制: " + riskSummary.maxThemeExposure.toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                        }
                    }
                    
                    // 风险等级评估
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        radius: 4
                        color: getRiskLevelColor()
                        
                        Text {
                            anchors.centerIn: parent
                            text: "风险等级: " + getRiskLevelText()
                            font.pixelSize: 14
                            font.bold: true
                            color: "white"
                        }
                    }
                }
            }
            
            // ============= 5. 操作按钮 =============
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                spacing: 16
                
                Button {
                    text: "重置为默认"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    font.pixelSize: 14
                    
                    onClicked: {
                        resetToDefaults()
                    }
                }
                
                Button {
                    text: "应用配置"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    font.pixelSize: 14
                    font.bold: true
                    background: Rectangle {
                        radius: 8
                        color: Theme.accentColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        applyRiskConfiguration()
                    }
                }
            }
        }
    }
    
    // ============ 动态参数方法 ============
    
    // 初始化动态参数配置
    function initDynamicParams() {
        console.log("初始化风险管理动态参数配置")
        
        // 生成动态参数配置
        generateDynamicParamConfigs()
        
        // 初始化动态值
        initDynamicValues()
    }
    
    // 生成动态参数配置（从JSON文件动态加载）
    function generateDynamicParamConfigs() {
        console.log("开始动态加载风险管理参数配置")
        
        // 从配置文件加载
        RiskBacktestMeta.loadMetaFile("qrc:/config/views/risk_backtest_params.json", function(meta) {
            if (meta) {
                console.log("成功加载风险管理参数配置")
                
                // 清空现有配置
                dynamicParamConfigs = []
                
                // 只加载风险管理相关的参数
                var riskParamConfigs = RiskBacktestMeta.getParameterConfigs("risk")
                
                // 转换为动态参数生成器所需的格式
                riskParamConfigs.forEach(function(paramConfig) {
                    var config = {
                        id: paramConfig.id,
                        type: paramConfig.type,
                        label: paramConfig.label,
                        description: paramConfig.description,
                        default: paramConfig.default,
                        category: paramConfig.category,
                        group: paramConfig.category || "风险管理"
                    }
                    
                    // 根据类型添加特定属性
                    switch (paramConfig.type) {
                        case "slider":
                            config.min = paramConfig.min
                            config.max = paramConfig.max
                            config.step = paramConfig.step || 0.01
                            config.unit = paramConfig.unit || ""
                            break
                        case "select":
                            config.type = "select"
                            config.options = paramConfig.options || []
                            config.multiple = paramConfig.multiple || false
                            break
                        case "toggle":
                            config.type = "toggle"
                            config.trueLabel = paramConfig.trueLabel || "是"
                            config.falseLabel = paramConfig.falseLabel || "否"
                            break
                    }
                    
                    // 处理可见性条件
                    if (paramConfig.visibleWhen) {
                        config.visibleWhen = paramConfig.visibleWhen
                    }
                    
                    dynamicParamConfigs.push(config)
                })
                
                console.log("风险管理动态参数配置加载完成，数量:", dynamicParamConfigs.length)
                
                // 初始化动态值
                initDynamicValues()
                
                // 设置动态参数生成器的配置
                if (dynamicParamGenerator) {
                    dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, [])
                }
                
                parametersLoaded = true
                
                // 更新风险摘要
                updateRiskSummary(dynamicParamValues)
            } else {
                console.error("加载风险管理参数配置失败，使用默认配置")
                generateFallbackParamConfigs()
            }
        })
    }
    
    // 后备参数配置（当动态加载失败时使用）
    function generateFallbackParamConfigs() {
        dynamicParamConfigs = []
        
        // 基础风险管理参数
        dynamicParamConfigs.push({
            id: "stopLossPercent",
            type: "slider",
            label: "止损比例",
            description: "单个头寸的最大亏损比例，达到此比例自动平仓",
            min: 1,
            max: 30,
            step: 0.5,
            default: 10,
            unit: "%",
            category: "risk",
            group: "基础风险控制"
        })
        
        dynamicParamConfigs.push({
            id: "takeProfitPercent",
            type: "slider",
            label: "止盈比例",
            description: "单个头寸的目标盈利比例，达到此比例自动平仓",
            min: 5,
            max: 50,
            step: 1,
            default: 20,
            unit: "%",
            category: "risk",
            group: "基础风险控制"
        })
        
        dynamicParamConfigs.push({
            id: "maxDrawdownLimit",
            type: "slider",
            label: "最大回撤限制",
            description: "策略总体账户的最大允许回撤比例",
            min: 0.05,
            max: 0.5,
            step: 0.01,
            default: 0.2,
            unit: "%",
            category: "risk",
            group: "基础风险控制"
        })
        
        dynamicParamConfigs.push({
            id: "maxPositionPercent",
            type: "slider",
            label: "最大持仓比例",
            description: "单一个股或行业最大持仓占账户总资产的比例",
            min: 0.01,
            max: 0.5,
            step: 0.01,
            default: 0.3,
            unit: "%",
            category: "position",
            group: "仓位管理"
        })
        
        dynamicParamConfigs.push({
            id: "maxPositions",
            type: "slider",
            label: "最大持仓数",
            description: "同时持有的最大股票数量",
            min: 1,
            max: 50,
            step: 1,
            default: 10,
            unit: "只",
            category: "position",
            group: "仓位管理"
        })
        
        dynamicParamConfigs.push({
            id: "maxTotalExposure",
            type: "slider",
            label: "最大总仓位",
            description: "所有持仓总市值占资金比例",
            min: 10,
            max: 100,
            step: 1,
            default: 95,
            unit: "%",
            category: "position",
            group: "仓位管理"
        })
        
        dynamicParamConfigs.push({
            id: "maxIndustryExposure",
            type: "slider",
            label: "单一行业最大仓位",
            description: "同一行业股票总持仓比例限制",
            min: 10,
            max: 100,
            step: 1,
            default: 40,
            unit: "%",
            category: "industry",
            group: "行业与题材仓位限制"
        })
        
        dynamicParamConfigs.push({
            id: "maxThemeExposure",
            type: "slider",
            label: "单一题材最大仓位",
            description: "同一题材股票总持仓比例限制",
            min: 5,
            max: 50,
            step: 1,
            default: 25,
            unit: "%",
            category: "industry",
            group: "行业与题材仓位限制"
        })
        
        dynamicParamConfigs.push({
            id: "maxDailyLoss",
            type: "slider",
            label: "单日最大亏损",
            description: "单日账户净值最大回撤限制",
            min: -10,
            max: 0,
            step: 0.5,
            default: -3,
            unit: "%",
            category: "account",
            group: "账户风险配置"
        })
        
        dynamicParamConfigs.push({
            id: "maxCorrelation",
            type: "slider",
            label: "最大持仓相关性",
            description: "持仓股票间最大允许相关性",
            min: 0,
            max: 100,
            step: 1,
            default: 70,
            unit: "%",
            category: "other",
            group: "其他配置"
        })
        
        console.log("使用后备风险管理参数配置，数量:", dynamicParamConfigs.length)
        parametersLoaded = true
    }
    
    // 初始化动态参数值
    function initDynamicValues() {
        var values = {}
        dynamicParamConfigs.forEach(function(config) {
            if (config.default !== undefined) {
                values[config.id] = config.default
            }
        })
        dynamicParamValues = values
        
        // 更新动态参数生成器的值
        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValues(values)
        }
        
        // 更新风险摘要
        updateRiskSummary(values)
        
        console.log("初始化风险管理动态参数值完成:", values)
    }
    
    // 更新风险摘要
    function updateRiskSummary(values) {
        riskSummary = {
            stopLossPercent: values.stopLossPercent || 10.0,
            takeProfitPercent: values.takeProfitPercent || 20.0,
            maxDrawdownLimit: (values.maxDrawdownLimit || 0.2) * 100,
            maxPositionPercent: (values.maxPositionPercent || 0.3) * 100,
            maxPositions: values.maxPositions || 10,
            maxTotalExposure: (values.maxTotalExposure || 0.95) * 100,
            maxIndustryExposure: (values.maxIndustryExposure || 0.4) * 100,
            maxThemeExposure: (values.maxThemeExposure || 0.25) * 100,
            maxDailyLoss: (values.maxDailyLoss || -0.03) * 100,
            maxCorrelation: (values.maxCorrelation || 0.7) * 100
        }
        console.log("风险摘要更新:", riskSummary)
    }
    
    // ============ 辅助函数 ============
    
    // 保存风险配置
    function saveRiskConfiguration() {
        console.log("保存风险配置:", JSON.stringify(dynamicParamValues))
        showNotification("风险配置已保存")
        
        // 这里可以添加保存到数据库或配置文件的逻辑
        // riskService.saveConfiguration(dynamicParamValues)
    }
    
    // 应用风险配置
    function applyRiskConfiguration() {
        console.log("应用风险配置:", JSON.stringify(dynamicParamValues))
        showNotification("风险配置已应用")
        
        // 这里可以添加应用到风险管理的逻辑
        // riskManager.applyConfiguration(dynamicParamValues)
        
        // 保存配置
        saveRiskConfiguration()
    }
    
    // 重置为默认值
    function resetToDefaults() {
        console.log("重置风险配置为默认值")
        
        initDynamicValues()
        showNotification("已重置为默认风险配置")
    }
    
    // 获取风险等级颜色
    function getRiskLevelColor() {
        var riskScore = calculateRiskScore()
        
        if (riskScore < 3) {
            return Theme.successColor  // 低风险 - 绿色
        } else if (riskScore < 7) {
            return Theme.warningColor  // 中风险 - 黄色
        } else {
            return Theme.dangerColor   // 高风险 - 红色
        }
    }
    
    // 获取风险等级文本
    function getRiskLevelText() {
        var riskScore = calculateRiskScore()
        
        if (riskScore < 3) {
            return "低风险"
        } else if (riskScore < 7) {
            return "中风险"
        } else {
            return "高风险"
        }
    }
    
    // 计算风险评分（简化版）
    function calculateRiskScore() {
        var score = 0
        
        // 止损止盈评分
        if (riskSummary.stopLossPercent < 5) score += 2
        else if (riskSummary.stopLossPercent < 10) score += 1
        
        if (riskSummary.takeProfitPercent > 30) score += 2
        else if (riskSummary.takeProfitPercent > 20) score += 1
        
        // 仓位控制评分
        if (riskSummary.maxPositionPercent > 50) score += 2
        else if (riskSummary.maxPositionPercent > 30) score += 1
        
        if (riskSummary.maxTotalExposure > 95) score += 2
        else if (riskSummary.maxTotalExposure > 90) score += 1
        
        // 账户风险评分
        if (riskSummary.maxDailyLoss < -5) score += 2
        else if (riskSummary.maxDailyLoss < -3) score += 1
        
        // 行业集中度评分
        if (riskSummary.maxIndustryExposure > 50) score += 2
        else if (riskSummary.maxIndustryExposure > 40) score += 1
        
        return Math.min(score, 10) // 最高10分
    }
    
    // 显示通知
    function showNotification(message) {
        console.log("通知:", message)
        // 这里可以实现更复杂的通知系统
        // notificationPanel.show(message)
    }
    
    // ============ 初始化和信号连接 ============
    
    Component.onCompleted: {
        console.log("RiskConfigurationPage 初始化")
        
        // 注册参数组件
        if (paramComponents) {
            paramComponents.registerAllComponents()
        }
    }
}

// StrategyRiskConfig.qml
// 风险管理与回测配置组件 - 完全采用动态参数

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../../../utils/StrategyCreationUtils.js" as Utils
import "../../FactorWorkbench/Creation/components" as PluginComponents

Rectangle {
    id: root
    
    // ============ 组件属性 ============
    
    // 输入属性（保持向后兼容）
    property double stopLossPercent: 10.0
    property double takeProfitPercent: 20.0
    property double maxDrawdownLimit: 20.0  // 百分比值，20% = 0.2
    property double maxPositionPercent: 80.0
    property int positionSizingMethod: 1
    property int backtestYears: 3
    property string benchmark: Utils.StrategyCreationUtils.tr('strategyCreation.defaultBenchmark')
    property double transactionCost: 0.0015
    property double slippageCost: 0.001
    
    // 高级选项
    property bool enableAdvancedOptions: false
    property bool enableWalkForward: false
    property bool enableMonteCarlo: false
    property int monteCarloSamples: 1000
    property bool enableOutOfSample: false
    property double outOfSampleRatio: 0.3
    
    // 通用参数属性
    property var commonParameters: ({})
    property bool commonParametersValid: false
    
    // ============ 动态参数配置 ============
    
    // 动态参数生成器
    property var dynamicParamConfigs: []
    property var dynamicParamValues: ({})
    property bool useDynamicParams: true  // 始终启用动态参数
    
    // 插件化组件注册表
    PluginComponents.ParamComponents {
        id: paramComponents
        Component.onCompleted: {
            console.log("ParamComponents 初始化完成")
            // 注册所有组件
            if (typeof paramComponents.registerAllComponents === 'function') {
                paramComponents.registerAllComponents()
                console.log("参数组件注册完成")
            } else {
                console.error("registerAllComponents 函数未定义")
            }
        }
    }
    
    // 避免更新循环的标志
    property bool updatingTransactionCost: false
    property bool updatingCommonParameters: false
    
    // 当transactionCost变化时更新参数
    onTransactionCostChanged: {
        // 更新动态值
        if (root.dynamicParamValues && root.dynamicParamValues.transactionCost !== undefined) {
            root.dynamicParamValues.transactionCost = transactionCost
        }
        root.updateCommonParameters()
    }
    
    // ============ 组件布局 ============
    
    color: "transparent"
    
    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        
        ColumnLayout {
            width: parent.width
            spacing: 20
            
            // 策略摘要
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 10
                color: "#0f172a"
                border.width: 1
                border.color: "#334155"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8
                    
                    Text {
                        text: Utils.StrategyCreationUtils.tr('strategyCreation.strategySummary')
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: "#f1f5f9"
                    }
                    
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 2
                        columnSpacing: 20
                        rowSpacing: 12
                        
                        // 策略类型（从外部传入）
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyType')
                                font.pixelSize: 12
                                color: "#94a3b8"
                            }
                            
                            Text {
                                id: strategyTypeText
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#f1f5f9"
                            }
                        }
                        
                        // 策略名称（从外部传入）
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyName')
                                font.pixelSize: 12
                                color: "#94a3b8"
                            }
                            
                            Text {
                                id: strategyNameText
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#64748b"
                            }
                        }
                        
                        // 风险等级（从外部传入）
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.riskLevelLabel')
                                font.pixelSize: 12
                                color: "#94a3b8"
                            }
                            
                            Text {
                                id: riskLevelText
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#3b82f6"
                            }
                        }
                        
                        // 回测周期
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.backtestPeriod')
                                font.pixelSize: 12
                                color: "#94a3b8"
                            }
                            
                            Text {
                                text: root.backtestYears + "年"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#f1f5f9"
                            }
                        }
                        
                        // 参数数量
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: Utils.StrategyCreationUtils.tr('strategyCreation.parameterCount')
                                font.pixelSize: 12
                                color: "#94a3b8"
                            }
                            
                            Text {
                                id: parameterCountText
                                text: "0个"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#f1f5f9"
                            }
                        }
                    }
                }
            }
            
            // 动态参数生成器
            Rectangle {
                id: dynamicParamsContainer
                Layout.fillWidth: true
                Layout.preferredHeight: 600
                radius: 10
                color: "#0f172a"
                border.width: 1
                border.color: "#334155"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16
                    
                    Text {
                        text: "风险和回测参数配置"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: "#f1f5f9"
                    }
                    
                    Text {
                        text: "使用统一的动态参数生成器，配置风险管理和回测相关参数"
                        font.pixelSize: 12
                        color: "#94a3b8"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    
                    // 动态参数生成器实例
                    PluginComponents.DynamicParamGenerator {
                        id: dynamicParamGenerator
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        paramRegistry: paramComponents
                        values: root.dynamicParamValues
                        
                        // 当参数值变化时更新
                        onParamsChanged: function(newValues) {
                            console.log("动态参数变化:", newValues)
                            root.dynamicParamValues = newValues
                            root.syncToLegacyProperties(newValues)
                            root.updateCommonParameters()
                        }
                    }
                    
                    Text {
                        text: "正在加载动态参数配置..."
                        font.pixelSize: 14
                        color: "#64748b"
                        visible: !dynamicParamGenerator || dynamicParamGenerator.configs.length === 0
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
            
            // 占位Item，确保滚动区域有足够的底部空间
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
            }
        }
    }
    
    // ============ 组件方法 ============
    
    // 设置摘要信息
    function setSummaryInfo(strategyType, strategyName, riskLevel, parameterCount) {
        strategyTypeText.text = Utils.StrategyCreationUtils.getStrategyTypeName(strategyType)
        strategyNameText.text = strategyName || Utils.StrategyCreationUtils.tr('strategyCreation.strategyNamePlaceholder')
        riskLevelText.text = Utils.StrategyCreationUtils.getRiskLevelName(riskLevel)
        riskLevelText.color = Utils.StrategyCreationUtils.getRiskLevelColor(riskLevel)
        parameterCountText.text = parameterCount + "个"
    }

    
    // 重置组件
    function reset() {
        stopLossPercent = 10.0
        takeProfitPercent = 20.0
        maxDrawdownLimit = 20.0  // 百分比值，20% = 0.2
        maxPositionPercent = 80.0
        positionSizingMethod = 1
        backtestYears = 3
        benchmark = Utils.StrategyCreationUtils.tr('strategyCreation.defaultBenchmark')
        transactionCost = 0.0015
        enableAdvancedOptions = false
        enableWalkForward = false
        enableMonteCarlo = false
        monteCarloSamples = 1000
        enableOutOfSample = false
        outOfSampleRatio = 0.3
        
        // 重置动态参数值
        if (dynamicParamConfigs.length > 0) {
            var values = {}
            dynamicParamConfigs.forEach(function(config) {
                if (config.default !== undefined) {
                    values[config.id] = config.default
                }
            })
            dynamicParamValues = values
            syncToLegacyProperties(values)
            updateCommonParameters()
            
            // 更新动态参数生成器的值
            if (dynamicParamGenerator) {
                dynamicParamGenerator.setValues(values)
            }
        }
    }
    
    // ============ 动态参数方法 ============
    
    // 初始化动态参数配置 - 与步骤2保持一致，使用JS硬编码配置
    function initDynamicParams() {
        console.log("StrategyRiskConfig: 开始初始化动态参数配置")
        
        // 生成风险参数配置（硬编码方式，与StrategyParamConfig一致）
        console.log("StrategyRiskConfig: 生成硬编码风险参数配置")
        buildRiskParamConfigs()
        
        // 设置动态参数生成器的配置（与第二步保持一致）
        if (dynamicParamGenerator) {
            dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, [])
        }
        
        // 初始化动态值
        initDynamicValues()
        
        // 更新参数数量显示
        updateParameterCount()
        
        console.log("StrategyRiskConfig: 配置完成，参数数量:", dynamicParamConfigs.length)
    }
    
    // 构建风险参数配置（硬编码方式，与步骤2的buildParamConfigs保持一致）
    function buildRiskParamConfigs() {
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
            group: "风险管理"
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
            group: "风险管理"
        })
        
        dynamicParamConfigs.push({
            id: "maxDrawdownLimit",
            type: "slider",
            label: "最大回撤限制",
            description: "策略总体账户的最大允许回撤比例",
            min: 1,
            max: 50,
            step: 1,
            default: 3,
            unit: "%",
            category: "risk",
            group: "风险管理"
        })
        
        // 基础回测设置参数
        dynamicParamConfigs.push({
            id: "backtestYears",
            type: "select",
            label: "回测周期",
            description: "历史回测的时间长度",
            options: [
                {value: "1", label: "最近1年"},
                {value: "3", label: "最近3年"},
                {value: "5", label: "最近5年"},
                {value: "10", label: "最近10年"}
            ],
            default: "3",
            category: "basic",
            group: "回测设置"
        })
        
        dynamicParamConfigs.push({
            id: "benchmark",
            type: "select",
            label: "基准指数",
            description: "策略绩效对比的基准指数",
            options: [
                {value: "000001.SH", label: "上证指数"},
                {value: "399001.SZ", label: "深证成指"},
                {value: "000300.SH", label: "沪深300"},
                {value: "000905.SH", label: "中证500"}
            ],
            default: "000300.SH",
            category: "basic",
            group: "回测设置"
        })
        
        dynamicParamConfigs.push({
            id: "transactionCost",
            type: "slider",
            label: "交易成本",
            description: "单边交易手续费率（含佣金和印花税）",
            min: 2,
            max: 8,
            step: 0.5,
            default: 3,
            unit: "%",
            category: "cost",
            group: "成本设置"
        })
        
        // 仓位管理参数
        dynamicParamConfigs.push({
            id: "maxPositionPercent",
            type: "slider",
            label: "最大持仓比例",
            description: "单一个股或行业最大持仓占账户总资产的比例",
            min: 1,
            max: 50,
            step: 1,
            default: 10,
            unit: "%",
            category: "position",
            group: "仓位管理"
        })
        
        dynamicParamConfigs.push({
            id: "positionSizingMethod",
            type: "select",
            label: "仓位管理方法",
            description: "决定每次交易投入资金比例的方法",
            default: "fixed",
            options: [
                {value: "fixed", label: "固定比例"},
                {value: "kelly", label: "凯利公式"},
                {value: "equalWeight", label: "等权重"},
                {value: "riskParity", label: "风险平价"}
            ],
            category: "position",
            group: "仓位管理"
        })
        
        // 高级选项参数
        dynamicParamConfigs.push({
            id: "enableAdvancedOptions",
            type: "toggle",
            label: "启用高级回测选项",
            description: "是否启用高级回测功能",
            default: false,
            category: "advanced",
            group: "高级选项"
        })
        
        dynamicParamConfigs.push({
            id: "enableWalkForward",
            type: "toggle",
            label: "滚动窗口优化",
            description: "是否进行滚动窗口参数优化",
            default: false,
            category: "advanced",
            group: "高级选项",
            visibleWhen: "enableAdvancedOptions == true"
        })
        
        dynamicParamConfigs.push({
            id: "enableMonteCarlo",
            type: "toggle",
            label: "蒙特卡洛模拟",
            description: "是否进行蒙特卡洛模拟检验",
            default: false,
            category: "advanced",
            group: "高级选项",
            visibleWhen: "enableAdvancedOptions == true"
        })
        
        dynamicParamConfigs.push({
            id: "monteCarloSamples",
            type: "slider",
            label: "模拟样本数",
            description: "蒙特卡洛模拟的抽样次数",
            min: 100,
            max: 10000,
            step: 100,
            default: 1000,
            unit: "次",
            category: "advanced",
            group: "高级选项",
            visibleWhen: "enableMonteCarlo == true"
        })
        
        dynamicParamConfigs.push({
            id: "enableOutOfSample",
            type: "toggle",
            label: "样本外测试",
            description: "是否进行样本外数据测试",
            default: false,
            category: "advanced",
            group: "高级选项",
            visibleWhen: "enableAdvancedOptions == true"
        })
        
        dynamicParamConfigs.push({
            id: "outOfSampleRatio",
            type: "slider",
            label: "样本外比例",
            description: "样本外数据占总数据的比例",
            min: 1,
            max: 50,
            step: 1,
            default: 30,
            unit: "%",
            category: "advanced",
            group: "高级选项",
            visibleWhen: "enableOutOfSample == true"
        })
        
        console.log("风险参数配置生成完成，数量:", dynamicParamConfigs.length)
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
        
        // 同步到现有属性（向后兼容）
        syncToLegacyProperties(values)
        
        // 更新动态参数生成器的值
        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValues(values)
        }
        
        console.log("初始化动态参数值完成:", values)
    }
    
    // 同步到现有的属性（向后兼容）
    function syncToLegacyProperties(values) {
        if (!values) return;  // 如果values是undefined或null，直接返回
        
        if (values.stopLossPercent !== undefined && values.stopLossPercent !== null) {
            root.stopLossPercent = values.stopLossPercent
        }
        if (values.takeProfitPercent !== undefined && values.takeProfitPercent !== null) {
            root.takeProfitPercent = values.takeProfitPercent
        }
        if (values.maxDrawdownLimit !== undefined && values.maxDrawdownLimit !== null) {
            // 配置中的百分比值（如3表示3%），直接赋值
            root.maxDrawdownLimit = values.maxDrawdownLimit
        }
        if (values.maxPositionPercent !== undefined && values.maxPositionPercent !== null) {
            // 配置中的百分比值（如10表示10%），直接赋值
            root.maxPositionPercent = values.maxPositionPercent
        }
        if (values.positionSizingMethod !== undefined && values.positionSizingMethod !== null) {
            // 映射到旧的值
            var methodMap = {
                "fixed": 1,
                "kelly": 2,
                "equalWeight": 3,
                "riskParity": 4
            }
            root.positionSizingMethod = methodMap[values.positionSizingMethod] || 1
        }
        if (values.backtestYears !== undefined && values.backtestYears !== null) {
            var years = parseInt(values.backtestYears)
            if (!isNaN(years)) {
                root.backtestYears = years
            }
        }
        if (values.benchmark !== undefined && values.benchmark !== null) {
            root.benchmark = values.benchmark
        }
        if (values.transactionCost !== undefined && values.transactionCost !== null) {
            root.transactionCost = values.transactionCost
        }
        if (values.slippageCost !== undefined && values.slippageCost !== null) {
            root.slippageCost = values.slippageCost
        }
        if (values.enableAdvancedOptions !== undefined && values.enableAdvancedOptions !== null) {
            root.enableAdvancedOptions = values.enableAdvancedOptions
        }
        if (values.enableWalkForward !== undefined && values.enableWalkForward !== null) {
            root.enableWalkForward = values.enableWalkForward
        }
        if (values.enableMonteCarlo !== undefined && values.enableMonteCarlo !== null) {
            root.enableMonteCarlo = values.enableMonteCarlo
        }
        if (values.monteCarloSamples !== undefined && values.monteCarloSamples !== null) {
            root.monteCarloSamples = values.monteCarloSamples
        }
        if (values.enableOutOfSample !== undefined && values.enableOutOfSample !== null) {
            root.enableOutOfSample = values.enableOutOfSample
        }
        if (values.outOfSampleRatio !== undefined && values.outOfSampleRatio !== null) {
            root.outOfSampleRatio = values.outOfSampleRatio
        }
    }
    
    // 更新通用参数（用于向后兼容）
    function updateCommonParameters() {
        // 从动态值构建通用参数
        var params = {}
        for (var key in dynamicParamValues) {
            params[key] = dynamicParamValues[key]
        }
        
        // 添加其他参数
        params.slippageCost = root.slippageCost
        params.positionSizingMethod = root.positionSizingMethod
        params.maxPositionPercent = root.maxPositionPercent
        params.enableAdvancedOptions = root.enableAdvancedOptions
        params.enableWalkForward = root.enableWalkForward
        params.enableMonteCarlo = root.enableMonteCarlo
        params.monteCarloSamples = root.monteCarloSamples
        params.enableOutOfSample = root.enableOutOfSample
        params.outOfSampleRatio = root.outOfSampleRatio
        
        root.commonParameters = params
        root.commonParametersValid = true
        
        console.log("通用参数更新:", params)
    }
    
    // 更新参数数量显示
    function updateParameterCount() {
        var count = dynamicParamConfigs.length
        parameterCountText.text = count + "个"
    }
    
    // ============ 初始化和信号连接 ============
    
    Component.onCompleted: {
        console.log("StrategyRiskConfig 初始化")
        
        // 注册参数组件
        if (paramComponents) {
            paramComponents.registerAllComponents()
        }
        
        // 初始化动态参数（使用延时确保组件完全加载）
        if (typeof root.initDynamicParams === 'function') {
            // 添加微小延时，确保 DynamicParamGenerator 组件已经创建
            var timer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 50; running: true; repeat: false }', 
                                          root, 'dynamicParamsTimer')
            timer.triggered.connect(function() {
                console.log("开始初始化动态参数")
                root.initDynamicParams()
                timer.destroy()
            })
        } else {
            console.error("initDynamicParams 函数未定义")
        }
    }
}
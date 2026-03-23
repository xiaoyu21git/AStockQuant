// StrategyCreationUtils.js
// 策略创建工具函数，抽离业务逻辑
// 支持参数验证、数据构建、类型映射等

.pragma library

// 导入国际化模块
.import "./I18n.js" as I18nModule

// 翻译函数包装
function tr(key, language) {
    return I18nModule.tr(key, language);
}

// 获取翻译
function getTranslation(key, language) {
    return I18nModule.tr(key, language);
}

// ============ 策略类型相关 ============

// 获取策略类型名称
function getStrategyTypeName(typeId) {
    return tr('strategyCreation.strategyTypes.' + typeId) || typeId;
}

// 获取策略类型描述
function getStrategyTypeDescription(typeId) {
    return tr('strategyCreation.strategyTypeDescriptions.' + typeId) || tr('strategyCreation.strategyTypeDescriptions.custom');
}

// 获取策略类型图标
function getStrategyIcon(typeId) {
    switch(typeId) {
        case "trend_following": return "📈";
        case "mean_reversion": return "🔄";
        case "momentum": return "🚀";
        case "arbitrage": return "⚖️";
        case "machine_learning": return "🤖";
        case "multi_factor": return "🧩";
        case "high_frequency": return "⚡";
        case "event_driven": return "📰";
        case "custom": return "🛠️";
        default: return "📊";
    }
}

// 获取策略类型简要描述
function getBriefDescription(typeId) {
    var descriptions = {
        "trend_following": "跟随价格趋势交易",
        "mean_reversion": "价格偏离均值后回归",
        "momentum": "跟随强势股票动量",
        "arbitrage": "利用价差套利交易",
        "machine_learning": "AI预测价格走势",
        "multi_factor": "多维度综合评分",
        "high_frequency": "高频数据快速交易",
        "event_driven": "事件驱动交易机会",
        "custom": "用户自定义策略"
    };
    return descriptions[typeId] || "策略类型";
}

// ============ 风险等级相关 ============

// 获取风险等级名称
function getRiskLevelName(level) {
    return tr('strategyCreation.riskLevels.' + level) || level;
}

// 获取风险等级颜色
function getRiskLevelColor(level) {
    var colors = {
        "low": "#10b981",
        "medium": "#3b82f6",
        "high": "#f59e0b",
        "aggressive": "#ef4444"
    };
    return colors[level] || "#94a3b8";
}

// ============ 步骤相关 ============

// 获取步骤标签
function getStepLabel(step) {
    var labels = [
        tr('strategyCreation.step1'),
        tr('strategyCreation.step2'),
        tr('strategyCreation.step3')
    ];
    return labels[step - 1] || "";
}

// 获取步骤标题
function getStepTitle(step) {
    var titles = [
        tr('strategyCreation.step1Title'),
        tr('strategyCreation.step2Title'),
        tr('strategyCreation.step3Title')
    ];
    return titles[step - 1] || "";
}

// 获取步骤描述
function getStepDescription(step) {
    var descriptions = [
        tr('strategyCreation.step1Description'),
        tr('strategyCreation.step2Description'),
        tr('strategyCreation.step3Description')
    ];
    return descriptions[step - 1] || "";
}

// ============ 验证相关 ============

// 检查步骤是否有效
function isStepValid(step, context) {
    switch(step) {
        case 1:
            return context.selectedStrategyType !== "" && 
                   context.strategyName.trim() !== "" && 
                   context.strategyDescription.trim() !== "";
        case 2:
            return context.parametersValid && 
                   Object.keys(context.strategyParameters).length > 0;
        case 3:
            return true;  // 风险管理与回测步骤总是有效
        default:
            return false;
    }
}

// 验证当前步骤
function validateCurrentStep(step, context) {
    switch(step) {
        case 1:
            if (!context.selectedStrategyType || context.selectedStrategyType === "") {
                return {
                    valid: false,
                    message: tr('strategyCreation.selectStrategyTypeError')
                };
            }
            break;
            
        case 2:
            if (!context.strategyName || context.strategyName.trim() === "") {
                return {
                    valid: false,
                    message: tr('strategyCreation.strategyNameError')
                };
            }
            if (!context.strategyDescription || context.strategyDescription.trim() === "") {
                return {
                    valid: false,
                    message: tr('strategyCreation.strategyDescriptionError')
                };
            }
            break;
            
        case 3:
            if (!context.parametersValid || Object.keys(context.strategyParameters).length === 0) {
                return {
                    valid: false,
                    message: tr('strategyCreation.parameterError')
                };
            }
            break;
    }
    
    return {
        valid: true,
        message: tr('strategyCreation.validationPassedFull')
    };
}

// ============ 数据构建相关 ============

// 构建完整的策略数据
function buildCompleteStrategyData(context) {
        // 确保 slippage 写入 strategyParameters
        if (context.slippage !== undefined && context.strategyParameters) {
            context.strategyParameters.slippage = context.slippage;
        }
        if (context.commission !== undefined && context.strategyParameters) {
            context.strategyParameters.commission = context.commission;
        }
    var currentDate = new Date();
    var dateStr = currentDate.toISOString().split('T')[0];
    
    var strategyData = {
        // 基本信息
        name: context.strategyName,
        displayName: context.strategyName,
        strategyType: context.selectedStrategyType,
        typeName: getStrategyTypeName(context.selectedStrategyType),
        description: context.strategyDescription,
        
        // 基本属性
        assetType: context.assetType,
        timeFrame: context.timeFrame,
        riskLevel: context.riskLevel,
        optimizationMethod: context.optimizationMethod,
        
        // 回测设置
        backtestYears: context.backtestYears,
        backtestStartDate: context.backtestStartDate,
        backtestEndDate: context.backtestEndDate,
        benchmark: context.benchmark,
        transactionCost: context.transactionCost,
        
        // 风险管理
        maxDrawdownLimit: context.maxDrawdownLimit,
        positionSizingMethod: context.positionSizingMethod,
        maxPositionPercent: context.maxPositionPercent,
        stopLossPercent: context.stopLossPercent,
        takeProfitPercent: context.takeProfitPercent,
        
        // 高级选项
        enableAdvancedOptions: context.enableAdvancedOptions,
        enableWalkForward: context.enableWalkForward,
        enableMonteCarlo: context.enableMonteCarlo,
        monteCarloSamples: context.monteCarloSamples,
        enableOutOfSample: context.enableOutOfSample,
        outOfSampleRatio: context.outOfSampleRatio,
        
        // 元数据
        status: "stopped",
        createdDate: dateStr,
        returns: "+0.0%",
        maxDrawdown: "-0.0%",
        sharpeRatio: "0.0",
        winRate: "0.0%",
        tags: context.strategyTags,
        
        // 参数数据
        parameters: context.strategyParameters,
        parameterCount: Object.keys(context.strategyParameters).length
    };
    
    return strategyData;
}

// ============ 仓位管理相关 ============

// 获取仓位管理方法描述
function getPositionSizingDescription(method) {
    var descriptions = {
        1: "固定仓位：每次交易使用固定的资金比例，简单易用但不够灵活",
        2: "凯利公式：基于胜率和盈亏比计算最优仓位，理论最优但风险较高",
        3: "等权重：投资组合中每个标的权重相等，分散风险但可能不够高效"
    };
    return descriptions[method] || "";
}

    // ============ 参数配置相关 ============

    // 构建参数配置
    function buildParamConfigs(strategyType) {
        var configs = [];
        
        // ============ 通用回测参数 ============
        configs.push({
            id: "initialCapital",
            type: "slider",
            label: tr('strategyCreation.initialCapital'),
            description: tr('strategyCreation.initialCapitalDescription'),
            default: 1000000,
            min: 10000,
            max: 10000000,
            step: 10000,
            unit: tr('strategyCreation.currencyUnit'),
            category: tr('strategyCreation.commonParameters')
        });
        
        configs.push({
            id: "commission",
            type: "input",
            label: tr('strategyCreation.commission'),
            description: tr('strategyCreation.commissionDescription'),
            default: 0.03,
            placeholder: "0.03",
            maxLength:10,
            minLength:0,
            unit: "%",
            validator: "number",
            category: tr('strategyCreation.commonParameters')
        });
        
        configs.push({
            id: "slippage",
            type: "input",
            label: tr('strategyCreation.slippage'),
            description: tr('strategyCreation.slippageDescription'),
            default: 0.001,
            placeholder: "0.001",
            maxLength:10,
            minLength:0,
            unit: "%",
            validator: "number",
            category: tr('strategyCreation.commonParameters')
        });
        
        configs.push({
            id: "maxPosition",
            type: "slider",
            label: tr('strategyCreation.maxPosition'),
            description: tr('strategyCreation.maxPositionDescription'),
            default: 80,
            min: 10,
            max: 100,
            step: 5,
            unit: "%",
            category: tr('strategyCreation.commonParameters')
        });
        
        configs.push({
            id: "orderType",
            type: "select",
            label: tr('strategyCreation.orderType'),
            description: tr('strategyCreation.orderTypeDescription'),
            options: [tr('strategyCreation.limitOrder'), tr('strategyCreation.marketOrder')],
            default: tr('strategyCreation.limitOrder'),
            category: tr('strategyCreation.commonParameters')
        });
    
    // 如果请求的是通用参数，直接返回
    if (strategyType === "common") {
        return configs;
    }
    
    // ============ 策略特定参数 ============
    if (strategyType === "trend_following") {
        configs.push({
            id: "fastPeriod",
            type: "slider",
            label: tr('strategyCreation.fastPeriod'),
            description: tr('strategyCreation.fastPeriodDescription'),
            default: 5,
            min: 2,
            max: 50,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "slowPeriod",
            type: "slider",
            label: tr('strategyCreation.slowPeriod'),
            description: tr('strategyCreation.slowPeriodDescription'),
            default: 20,
            min: 5,
            max: 200,
            step: 5,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "stopLoss",
            type: "slider",
            label: tr('strategyCreation.stopLossPercent'),
            description: tr('strategyCreation.stopLossDescription'),
            default: 5,
            min: 1,
            max: 20,
            step: 0.5,
            unit: "%",
            category: tr('strategyCreation.coreParameters')
        });
    } else if (strategyType === "mean_reversion") {
        configs.push({
            id: "lookbackPeriod",
            type: "slider",
            label: tr('strategyCreation.lookbackPeriod'),
            description: tr('strategyCreation.lookbackPeriodDescription'),
            default: 20,
            min: 5,
            max: 100,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "entryThreshold",
            type: "slider",
            label: tr('strategyCreation.entryThreshold'),
            description: tr('strategyCreation.entryThresholdDescription'),
            default: 2.0,
            min: 1.0,
            max: 4.0,
            step: 0.1,
            unit: "",
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "exitThreshold",
            type: "slider",
            label: tr('strategyCreation.exitThreshold'),
            description: tr('strategyCreation.exitThresholdDescription'),
            default: 0.5,
            min: 0.1,
            max: 1.5,
            step: 0.1,
            unit: "",
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "gridLevels",
            type: "slider",
            label: tr('strategyCreation.gridLevels'),
            description: tr('strategyCreation.gridLevelsDescription'),
            default: 10,
            min: 3,
            max: 20,
            step: 1,
            unit: tr('strategyCreation.levelsUnit'),
            category: tr('strategyCreation.coreParameters')
        });
    } else if (strategyType === "momentum") {
        configs.push({
            id: "momentumPeriod",
            type: "slider",
            label: tr('strategyCreation.momentumPeriod'),
            description: tr('strategyCreation.momentumPeriodDescription'),
            default: 20,
            min: 5,
            max: 250,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "selectionRatio",
            type: "slider",
            label: tr('strategyCreation.selectionRatio'),
            description: tr('strategyCreation.selectionRatioDescription'),
            default: 20,
            min: 5,
            max: 50,
            step: 1,
            unit: "%",
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "rebalancingPeriod",
            type: "slider",
            label: tr('strategyCreation.rebalancingPeriod'),
            description: tr('strategyCreation.rebalancingPeriodDescription'),
            default: 5,
            min: 1,
            max: 30,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
    } else if (strategyType === "arbitrage") {
        configs.push({
            id: "lookbackDays",
            type: "slider",
            label: tr('strategyCreation.lookbackDays'),
            description: tr('strategyCreation.lookbackDaysDescription'),
            default: 60,
            min: 20,
            max: 200,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "entryZScore",
            type: "slider",
            label: tr('strategyCreation.entryZScore'),
            description: tr('strategyCreation.entryZScoreDescription'),
            default: 2.0,
            min: 1.0,
            max: 3.0,
            step: 0.1,
            unit: "",
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "exitZScore",
            type: "slider",
            label: tr('strategyCreation.exitZScore'),
            description: tr('strategyCreation.exitZScoreDescription'),
            default: 0.5,
            min: 0.1,
            max: 1.5,
            step: 0.1,
            unit: "",
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "hedgeRatio",
            type: "slider",
            label: tr('strategyCreation.hedgeRatio'),
            description: tr('strategyCreation.hedgeRatioDescription'),
            default: 1.0,
            min: 0.5,
            max: 2.0,
            step: 0.1,
            unit: "",
            category: tr('strategyCreation.coreParameters')
        });
    } else if (strategyType === "machine_learning") {
        configs.push({
            id: "featureWindow",
            type: "slider",
            label: tr('strategyCreation.featureWindow'),
            description: tr('strategyCreation.featureWindowDescription'),
            default: 60,
            min: 10,
            max: 250,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "predictionDays",
            type: "slider",
            label: tr('strategyCreation.predictionDays'),
            description: tr('strategyCreation.predictionDaysDescription'),
            default: 1,
            min: 1,
            max: 10,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "trainingDays",
            type: "slider",
            label: tr('strategyCreation.trainingDays'),
            description: tr('strategyCreation.trainingDaysDescription'),
            default: 1000,
            min: 500,
            max: 5000,
            step: 100,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "confidenceThreshold",
            type: "slider",
            label: tr('strategyCreation.confidenceThreshold'),
            description: tr('strategyCreation.confidenceThresholdDescription'),
            default: 60,
            min: 50,
            max: 90,
            step: 1,
            unit: "%",
            category: tr('strategyCreation.coreParameters')
        });
    } else if (strategyType === "multi_factor") {
        configs.push({
            id: "factorTypes",
            type: "select",
            label: tr('strategyCreation.factorTypes'),
            description: tr('strategyCreation.factorTypesDescription'),
            options: [tr('strategyCreation.value'), tr('strategyCreation.quality'), tr('strategyCreation.growth'), 
                     tr('strategyCreation.momentum'), tr('strategyCreation.size'), tr('strategyCreation.volatility'),
                     tr('strategyCreation.liquidity'), tr('strategyCreation.sentiment')],
            default: [tr('strategyCreation.value'), tr('strategyCreation.quality'), tr('strategyCreation.growth'), tr('strategyCreation.momentum')],
            multiple: true,
            category: tr('strategyCreation.coreParameters')
        });
        
        configs.push({
            id: "rebalancingPeriod",
            type: "slider",
            label: tr('strategyCreation.rebalancingPeriod'),
            description: tr('strategyCreation.rebalancingPeriodDescription'),
            default: 20,
            min: 5,
            max: 60,
            step: 5,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.coreParameters')
        });
    } else if (strategyType === "high_frequency") {
        configs.push({
            id: "timeframe",
            type: "select",
            label: tr('strategyCreation.timeframe'),
            description: tr('strategyCreation.timeframeDescription'),
            options: [tr('strategyCreation.oneMinute'), tr('strategyCreation.fiveMinutes'), 
                     tr('strategyCreation.fifteenMinutes'), tr('strategyCreation.thirtyMinutes'), 
                     tr('strategyCreation.oneHour')],
            default: tr('strategyCreation.fiveMinutes'),
            category: tr('strategyCreation.coreParameters')
        });
    } else if (strategyType === "event_driven") {
        configs.push({
            id: "eventTypes",
            type: "select",
            label: tr('strategyCreation.eventTypes'),
            description: tr('strategyCreation.eventTypesDescription'),
            options: [tr('strategyCreation.earningsRelease'), tr('strategyCreation.mergerAnnouncement'), 
                     tr('strategyCreation.dividendAnnouncement'), tr('strategyCreation.managementChange'),
                     tr('strategyCreation.policyRelease'), tr('strategyCreation.productLaunch')],
            default: [tr('strategyCreation.earningsRelease'), tr('strategyCreation.mergerAnnouncement')],
            multiple: true,
            category: tr('strategyCreation.coreParameters')
        });
    } else if (strategyType === "custom") {
        // 自定义策略不需要特殊参数，用户自己定义代码
        configs.push({
            id: "customCode",
            type: "input",
            label: tr('strategyCreation.customCode'),
            description: tr('strategyCreation.customCodeDescription'),
            default: "# " + tr('strategyCreation.customCode'),
            multiline: true,
            placeholder: tr('strategyCreation.customCodePlaceholder'),
            category: tr('strategyCreation.coreParameters')
        });
    }
    
    return configs;
}

// ============ 工具函数 ============

// 重置表单数据
function resetFormData() {
    return {
        strategyName: "",
        strategyDescription: "",
        selectedStrategyType: "trend_following",
        strategyTags: [],
        assetType: "stock",
        timeFrame: "daily",
        riskLevel: "medium",
        optimizationMethod: "genetic",
        backtestYears: 3,
        backtestStartDate: "",
        backtestEndDate: "",
        benchmark: tr('strategyCreation.defaultBenchmark'),
        transactionCost: 0.0015,
        maxDrawdownLimit: 0.2,
        positionSizingMethod: 1,
        maxPositionPercent: 80,
        stopLossPercent: 10,
        takeProfitPercent: 20,
        slippage: 0.001, // 新增slippage字段，默认值与参数配置一致
        commission: 0.0003, // 新增commission字段，默认值与参数配置一致
        enableAdvancedOptions: false,
        enableWalkForward: false,
        enableMonteCarlo: false,
        monteCarloSamples: 1000,
        enableOutOfSample: false,
        outOfSampleRatio: 0.3,
        strategyParameters: {},
        parametersValid: false,
        validationMessage: ""
    };
}

// 导出函数
var StrategyCreationUtils = {
    // 翻译相关
    tr: tr,
    getTranslation: getTranslation,
    
    // 策略类型相关
    getStrategyTypeName: getStrategyTypeName,
    getStrategyTypeDescription: getStrategyTypeDescription,
    getStrategyIcon: getStrategyIcon,
    getBriefDescription: getBriefDescription,
    
    // 风险等级相关
    getRiskLevelName: getRiskLevelName,
    getRiskLevelColor: getRiskLevelColor,
    
    // 步骤相关
    getStepLabel: getStepLabel,
    getStepTitle: getStepTitle,
    getStepDescription: getStepDescription,
    
    // 验证相关
    isStepValid: isStepValid,
    validateCurrentStep: validateCurrentStep,
    
    // 数据构建相关
    buildCompleteStrategyData: buildCompleteStrategyData,
    
    // 仓位管理相关
    getPositionSizingDescription: getPositionSizingDescription,
    
    // 参数配置相关
    buildParamConfigs: buildParamConfigs,
    
    // 工具函数
    resetFormData: resetFormData
};
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

function getDefaultStrategyDescription(typeId) {
    var descriptions = {
        "trend_following": "基于趋势识别与顺势持仓的交易策略，结合信号确认、仓位控制与止盈止损规则，在日线级别捕捉中期趋势。",
        "mean_reversion": "基于价格偏离均值后的回归特征构建交易信号，在超跌与超涨区间寻找反转机会，并配合风险阈值控制回撤。",
        "momentum": "基于强势标的延续性构建动量组合，关注价格与成交活跃度的同步增强，在趋势持续阶段获取超额收益。",
        "arbitrage": "通过识别价差偏离与相对定价失衡机会进行套利交易，强调入场纪律、对冲约束与收益回归验证。",
        "machine_learning": "结合历史行情与衍生特征训练预测模型，对未来收益或方向进行打分，并通过统一风控框架执行交易。",
        "multi_factor": "综合估值、质量、成长、动量等多维因子构建选股评分体系，通过分层排序与调仓机制形成组合。",
        "high_frequency": "围绕高频行情、盘口变化与短周期微结构信号进行快速决策，重点控制交易成本、滑点与执行效率。",
        "event_driven": "围绕公告、业绩、行业事件等催化因素建立交易规则，捕捉事件前后定价偏差带来的机会。",
        "custom": "自定义交易策略模板，可在此基础上补充选股逻辑、入场离场规则与风控条件。"
    };
    return descriptions[typeId] || descriptions.custom;
}

function getDefaultStrategyTags(typeId) {
    var tags = {
        "trend_following": ["趋势", "顺势", "技术分析"],
        "mean_reversion": ["均值回归", "反转", "波动"],
        "momentum": ["动量", "强势股", "趋势延续"],
        "arbitrage": ["套利", "价差", "对冲"],
        "machine_learning": ["机器学习", "预测", "模型驱动"],
        "multi_factor": ["多因子", "选股", "组合"],
        "high_frequency": ["高频", "短周期", "执行"],
        "event_driven": ["事件驱动", "公告", "催化"],
        "custom": ["自定义", "策略", "量化"]
    };
    return tags[typeId] || tags.custom;
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
            return true;  // 创建确认步骤总是有效
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
    var currentDate = new Date();
    var dateStr = currentDate.toISOString().split('T')[0];
    var normalizedParameters = normalizeStrategyParameters(context.selectedStrategyType, context.strategyParameters);
    
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
        
        // 高级选项
        enableAdvancedOptions: context.enableAdvancedOptions,
        
        // 元数据
        status: "stopped",
        createdDate: dateStr,
        returns: "+0.0%",
        maxDrawdown: "-0.0%",
        sharpeRatio: "0.0",
        winRate: "0.0%",
        tags: context.strategyTags,
        
        // 参数数据
        parameters: normalizedParameters,
        parameterCount: Object.keys(normalizedParameters).length
    };
    
    return strategyData;
}

function normalizePercentageToRatio(value) {
    var numericValue = Number(value)
    if (!isFinite(numericValue)) {
        return value
    }
    return numericValue > 1 ? numericValue / 100 : numericValue
}

function normalizeStrategyParameters(strategyType, rawParameters) {
    var source = rawParameters || ({})
    var normalized = ({})

    normalized.strategy_subtype = strategyType

    function assignIfPresent(targetKey, sourceKeys, transform) {
        for (var index = 0; index < sourceKeys.length; ++index) {
            var sourceKey = sourceKeys[index]
            if (source[sourceKey] === undefined || source[sourceKey] === null || source[sourceKey] === "") {
                continue
            }
            normalized[targetKey] = transform ? transform(source[sourceKey]) : source[sourceKey]
            return
        }
    }

    assignIfPresent("position_size", ["position_size", "positionSize"], normalizePercentageToRatio)
    assignIfPresent("stop_loss", ["stop_loss", "stopLoss"], normalizePercentageToRatio)
    assignIfPresent("take_profit", ["take_profit", "takeProfit"], normalizePercentageToRatio)
    assignIfPresent("rebalance_days", ["rebalance_days", "rebalanceDays", "rebalancingPeriod"], Number)

    if (strategyType === "trend_following") {
        assignIfPresent("fast_period", ["fast_period", "fastPeriod"], Number)
        assignIfPresent("slow_period", ["slow_period", "slowPeriod"], Number)
    } else if (strategyType === "mean_reversion") {
        assignIfPresent("boll_period", ["boll_period", "bollPeriod", "lookbackPeriod"], Number)
        assignIfPresent("boll_std", ["boll_std", "bollStd", "entryThreshold"], Number)
        assignIfPresent("reversion_threshold", ["reversion_threshold", "reversionThreshold", "exitThreshold"], Number)
    } else if (strategyType === "momentum") {
        assignIfPresent("top_n", ["top_n", "topN"], Number)
        assignIfPresent("momentum_period", ["momentum_period", "momentumPeriod"], Number)
    } else if (strategyType === "arbitrage") {
        assignIfPresent("spread_threshold", ["spread_threshold", "spreadThreshold"], Number)
        assignIfPresent("entry_z_score", ["entry_z_score", "entryZScore"], Number)
        assignIfPresent("exit_z_score", ["exit_z_score", "exitZScore"], Number)
    } else if (strategyType === "machine_learning") {
        assignIfPresent("feature_window", ["feature_window", "featureWindow"], Number)
        assignIfPresent("prediction_days", ["prediction_days", "predictionDays"], Number)
        assignIfPresent("training_days", ["training_days", "trainingDays"], Number)
        assignIfPresent("confidence_threshold", ["confidence_threshold", "confidenceThreshold"], normalizePercentageToRatio)
    } else if (strategyType === "multi_factor") {
        assignIfPresent("factor_types", ["factor_types", "factorTypes"])
    } else if (strategyType === "high_frequency") {
        assignIfPresent("execution_timeframe", ["execution_timeframe", "timeframe"])
    } else if (strategyType === "event_driven") {
        assignIfPresent("event_types", ["event_types", "eventTypes"])
    } else if (strategyType === "custom") {
        assignIfPresent("custom_code", ["custom_code", "customCode"])
    }

    for (var key in source) {
        if (normalized[key] === undefined) {
            normalized[key] = source[key]
        }
    }

    return normalized
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
        
        // ============ 通用策略参数 ============
        configs.push({
            id: "positionSize",
            type: "slider",
            label: tr('strategyCreation.positionSize'),
            description: tr('strategyCreation.positionSizeDescription'),
            default: 20,
            min: 5,
            max: 100,
            step: 5,
            unit: "%",
            category: tr('strategyCreation.commonParameters')
        });

        configs.push({
            id: "stopLoss",
            type: "slider",
            label: tr('strategyCreation.stopLossPercent'),
            description: tr('strategyCreation.stopLossDescription'),
            default: 5,
            min: 1,
            max: 50,
            step: 0.5,
            unit: "%",
            category: tr('strategyCreation.commonParameters')
        });

        configs.push({
            id: "takeProfit",
            type: "slider",
            label: tr('strategyCreation.takeProfitPercent'),
            description: tr('strategyCreation.takeProfitDescription'),
            default: 15,
            min: 5,
            max: 200,
            step: 1,
            unit: "%",
            category: tr('strategyCreation.commonParameters')
        });

        configs.push({
            id: "rebalanceDays",
            type: "slider",
            label: tr('strategyCreation.rebalanceDays'),
            description: tr('strategyCreation.rebalanceDaysDescription'),
            default: 5,
            min: 1,
            max: 60,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
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
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
        });
    } else if (strategyType === "mean_reversion") {
        configs.push({
            id: "bollPeriod",
            type: "slider",
            label: tr('strategyCreation.bollPeriod'),
            description: tr('strategyCreation.bollPeriodDescription'),
            default: 20,
            min: 5,
            max: 100,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.personalizedParameters')
        });
        
        configs.push({
            id: "bollStd",
            type: "slider",
            label: tr('strategyCreation.bollStd'),
            description: tr('strategyCreation.bollStdDescription'),
            default: 2.0,
            min: 1.0,
            max: 3.0,
            step: 0.1,
            unit: "",
            category: tr('strategyCreation.personalizedParameters')
        });
        
        configs.push({
            id: "reversionThreshold",
            type: "slider",
            label: tr('strategyCreation.reversionThreshold'),
            description: tr('strategyCreation.reversionThresholdDescription'),
            default: 0.5,
            min: 0.1,
            max: 2.0,
            step: 0.1,
            unit: "",
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
        });
        
        configs.push({
            id: "topN",
            type: "slider",
            label: tr('strategyCreation.topN'),
            description: tr('strategyCreation.topNDescription'),
            default: 10,
            min: 1,
            max: 50,
            step: 1,
            unit: "只",
            category: tr('strategyCreation.personalizedParameters')
        });
        
    } else if (strategyType === "arbitrage") {
        configs.push({
            id: "spreadThreshold",
            type: "slider",
            label: tr('strategyCreation.spreadThreshold'),
            description: tr('strategyCreation.spreadThresholdDescription'),
            default: 0.02,
            min: 0.001,
            max: 0.1,
            step: 0.001,
            decimals: 3,
            unit: "",
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
        });
    } else if (strategyType === "multi_factor") {
        configs.push({
            id: "factorTypes",
            type: "multiselect",
            label: tr('strategyCreation.factorTypes'),
            description: tr('strategyCreation.factorTypesDescription'),
            options: [tr('strategyCreation.value'), tr('strategyCreation.quality'), tr('strategyCreation.growth'), 
                     tr('strategyCreation.momentum'), tr('strategyCreation.size'), tr('strategyCreation.volatility'),
                     tr('strategyCreation.liquidity'), tr('strategyCreation.sentiment')],
            default: [tr('strategyCreation.value'), tr('strategyCreation.quality'), tr('strategyCreation.growth'), tr('strategyCreation.momentum')],
            multiple: true,
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
        });
    } else if (strategyType === "event_driven") {
        configs.push({
            id: "eventTypes",
            type: "multiselect",
            label: tr('strategyCreation.eventTypes'),
            description: tr('strategyCreation.eventTypesDescription'),
            options: [tr('strategyCreation.earningsRelease'), tr('strategyCreation.mergerAnnouncement'), 
                     tr('strategyCreation.dividendAnnouncement'), tr('strategyCreation.managementChange'),
                     tr('strategyCreation.policyRelease'), tr('strategyCreation.productLaunch')],
            default: [tr('strategyCreation.earningsRelease'), tr('strategyCreation.mergerAnnouncement')],
            multiple: true,
            category: tr('strategyCreation.personalizedParameters')
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
            category: tr('strategyCreation.personalizedParameters')
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
        enableAdvancedOptions: false,
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
    getDefaultStrategyDescription: getDefaultStrategyDescription,
    getDefaultStrategyTags: getDefaultStrategyTags,
    
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
    normalizeStrategyParameters: normalizeStrategyParameters,
    
    // 仓位管理相关
    getPositionSizingDescription: getPositionSizingDescription,
    
    // 参数配置相关
    buildParamConfigs: buildParamConfigs,
    
    // 工具函数
    resetFormData: resetFormData
};
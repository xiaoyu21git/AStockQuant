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

function normalizeStrategyTypeId(typeId) {
    return String(typeId || "").trim().toLowerCase();
}

var StrategyTypeIndex = {
    Invalid: -1,
    TrendFollowing: 0,
    TrendBreakout: 1,
    MeanReversion: 2,
    Momentum: 3,
    Arbitrage: 4,
    MachineLearning: 5,
    MultiFactor: 6,
    HighFrequency: 7,
    EventDriven: 8,
    Custom: 9,
    Common: 100
};

var StrategyBehaviorKind = {
    Invalid: -1,
    TrendFollowing: 0,
    MeanReversion: 1,
    Momentum: 2,
    Arbitrage: 3,
    MultiFactor: 4,
    MachineLearning: 5,
    EventDriven: 6,
    HighFrequency: 7,
    Custom: 8
};

var StrategyStoredTypeIndex = {
    Unknown: -1,
    TrendFollowing: 0,
    MeanReversion: 1,
    Alpha: 2,
    Arbitrage: 3,
    HighFrequency: 4,
    Portfolio: 5,
    Custom: 6
};

function normalizeStrategyTypeIndex(strategyTypeIndex) {
    switch (Math.floor(Number(strategyTypeIndex))) {
        case StrategyTypeIndex.TrendFollowing:
        case StrategyTypeIndex.TrendBreakout:
        case StrategyTypeIndex.MeanReversion:
        case StrategyTypeIndex.Momentum:
        case StrategyTypeIndex.Arbitrage:
        case StrategyTypeIndex.MachineLearning:
        case StrategyTypeIndex.MultiFactor:
        case StrategyTypeIndex.HighFrequency:
        case StrategyTypeIndex.EventDriven:
        case StrategyTypeIndex.Custom:
        case StrategyTypeIndex.Common:
            return Math.floor(Number(strategyTypeIndex));
        default:
            return StrategyTypeIndex.Invalid;
    }
}

function strategyTypeIndexFromBehaviorKind(behaviorKind) {
    switch (Math.floor(Number(behaviorKind))) {
        case StrategyBehaviorKind.TrendFollowing:
            return StrategyTypeIndex.TrendFollowing;
        case StrategyBehaviorKind.MeanReversion:
            return StrategyTypeIndex.MeanReversion;
        case StrategyBehaviorKind.Momentum:
            return StrategyTypeIndex.Momentum;
        case StrategyBehaviorKind.Arbitrage:
            return StrategyTypeIndex.Arbitrage;
        case StrategyBehaviorKind.MultiFactor:
            return StrategyTypeIndex.MultiFactor;
        case StrategyBehaviorKind.MachineLearning:
            return StrategyTypeIndex.MachineLearning;
        case StrategyBehaviorKind.EventDriven:
            return StrategyTypeIndex.EventDriven;
        case StrategyBehaviorKind.HighFrequency:
            return StrategyTypeIndex.HighFrequency;
        case StrategyBehaviorKind.Custom:
            return StrategyTypeIndex.Custom;
        default:
            return StrategyTypeIndex.Invalid;
    }
}

function resolveProfileStrategyTypeIndex(strategyProfile) {
    var explicitTypeIndex = normalizeStrategyTypeIndex(strategyProfile && strategyProfile.strategyTypeIndex)
    if (explicitTypeIndex !== StrategyTypeIndex.Invalid) {
        return explicitTypeIndex
    }

    var behaviorKind = Math.floor(Number(strategyProfile && strategyProfile.strategyBehaviorKind))
    var behaviorTypeIndex = strategyTypeIndexFromBehaviorKind(behaviorKind)
    if (behaviorTypeIndex !== StrategyTypeIndex.Invalid) {
        return behaviorTypeIndex
    }

    return StrategyTypeIndex.TrendFollowing
}

function isTrendStrategyTypeIndex(strategyTypeIndex) {
    var normalizedTypeIndex = normalizeStrategyTypeIndex(strategyTypeIndex)
    return normalizedTypeIndex === StrategyTypeIndex.TrendFollowing
        || normalizedTypeIndex === StrategyTypeIndex.TrendBreakout
}

function strategyTypeIndexFromTypeId(typeId) {
    var normalizedTypeId = normalizeStrategyTypeId(typeId);
    switch (normalizedTypeId) {
        case "trend_following":
            return StrategyTypeIndex.TrendFollowing;
        case "trend_breakout":
            return StrategyTypeIndex.TrendBreakout;
        case "mean_reversion":
            return StrategyTypeIndex.MeanReversion;
        case "momentum":
        case "alpha":
            return StrategyTypeIndex.Momentum;
        case "arbitrage":
            return StrategyTypeIndex.Arbitrage;
        case "machine_learning":
            return StrategyTypeIndex.MachineLearning;
        case "multi_factor":
            return StrategyTypeIndex.MultiFactor;
        case "high_frequency":
            return StrategyTypeIndex.HighFrequency;
        case "event_driven":
            return StrategyTypeIndex.EventDriven;
        case "custom":
            return StrategyTypeIndex.Custom;
        default:
            return StrategyTypeIndex.Invalid;
    }
}

function strategyTypeIdFromIndex(strategyTypeIndex) {
    switch (normalizeStrategyTypeIndex(strategyTypeIndex)) {
        case StrategyTypeIndex.TrendFollowing:
            return "trend_following";
        case StrategyTypeIndex.TrendBreakout:
            return "trend_breakout";
        case StrategyTypeIndex.MeanReversion:
            return "mean_reversion";
        case StrategyTypeIndex.Momentum:
            return "momentum";
        case StrategyTypeIndex.Arbitrage:
            return "arbitrage";
        case StrategyTypeIndex.MachineLearning:
            return "machine_learning";
        case StrategyTypeIndex.MultiFactor:
            return "multi_factor";
        case StrategyTypeIndex.HighFrequency:
            return "high_frequency";
        case StrategyTypeIndex.EventDriven:
            return "event_driven";
        case StrategyTypeIndex.Custom:
            return "custom";
        default:
            return "";
    }
}

function strategyBehaviorKindFromTypeId(typeId) {
    var normalizedTypeId = normalizeStrategyTypeId(typeId);
    switch (normalizedTypeId) {
        case "mean_reversion":
            return StrategyBehaviorKind.MeanReversion;
        case "momentum":
        case "alpha":
            return StrategyBehaviorKind.Momentum;
        case "arbitrage":
            return StrategyBehaviorKind.Arbitrage;
        case "multi_factor":
            return StrategyBehaviorKind.MultiFactor;
        case "machine_learning":
            return StrategyBehaviorKind.MachineLearning;
        case "event_driven":
            return StrategyBehaviorKind.EventDriven;
        case "high_frequency":
            return StrategyBehaviorKind.HighFrequency;
        case "custom":
            return StrategyBehaviorKind.Custom;
        case "trend_breakout":
        case "trend_following":
        default:
            return StrategyBehaviorKind.TrendFollowing;
    }
}

function strategyBehaviorKindFromTypeIndex(strategyTypeIndex) {
    switch (normalizeStrategyTypeIndex(strategyTypeIndex)) {
        case StrategyTypeIndex.TrendFollowing:
        case StrategyTypeIndex.TrendBreakout:
            return StrategyBehaviorKind.TrendFollowing;
        case StrategyTypeIndex.MeanReversion:
            return StrategyBehaviorKind.MeanReversion;
        case StrategyTypeIndex.Momentum:
            return StrategyBehaviorKind.Momentum;
        case StrategyTypeIndex.Arbitrage:
            return StrategyBehaviorKind.Arbitrage;
        case StrategyTypeIndex.MultiFactor:
            return StrategyBehaviorKind.MultiFactor;
        case StrategyTypeIndex.MachineLearning:
            return StrategyBehaviorKind.MachineLearning;
        case StrategyTypeIndex.EventDriven:
            return StrategyBehaviorKind.EventDriven;
        case StrategyTypeIndex.HighFrequency:
            return StrategyBehaviorKind.HighFrequency;
        case StrategyTypeIndex.Custom:
            return StrategyBehaviorKind.Custom;
        default:
            return StrategyBehaviorKind.Invalid;
    }
}

function strategyTypeIdFromBehaviorKind(behaviorKind) {
    return strategyTypeIdFromIndex(strategyTypeIndexFromBehaviorKind(behaviorKind));
}

function strategyBehaviorKindLabel(behaviorKind) {
    switch (Math.floor(Number(behaviorKind))) {
        case StrategyBehaviorKind.MeanReversion:
            return "均值回归";
        case StrategyBehaviorKind.Momentum:
            return "动量";
        case StrategyBehaviorKind.Arbitrage:
            return "套利";
        case StrategyBehaviorKind.MultiFactor:
            return "多因子";
        case StrategyBehaviorKind.MachineLearning:
            return "机器学习";
        case StrategyBehaviorKind.EventDriven:
            return "事件驱动";
        case StrategyBehaviorKind.HighFrequency:
            return "高频";
        case StrategyBehaviorKind.Custom:
            return "自定义";
        case StrategyBehaviorKind.TrendFollowing:
        default:
            return "趋势跟随";
    }
}

function strategyStoredTypeIndexFromBehaviorKind(behaviorKind) {
    switch (Math.floor(Number(behaviorKind))) {
        case StrategyBehaviorKind.TrendFollowing:
            return StrategyStoredTypeIndex.TrendFollowing;
        case StrategyBehaviorKind.MeanReversion:
            return StrategyStoredTypeIndex.MeanReversion;
        case StrategyBehaviorKind.Momentum:
        case StrategyBehaviorKind.MultiFactor:
        case StrategyBehaviorKind.MachineLearning:
            return StrategyStoredTypeIndex.Alpha;
        case StrategyBehaviorKind.Arbitrage:
            return StrategyStoredTypeIndex.Arbitrage;
        case StrategyBehaviorKind.HighFrequency:
            return StrategyStoredTypeIndex.HighFrequency;
        case StrategyBehaviorKind.EventDriven:
        case StrategyBehaviorKind.Custom:
            return StrategyStoredTypeIndex.Custom;
        default:
            return StrategyStoredTypeIndex.Unknown;
    }
}

function strategyStoredTypeIndexFromTypeIndex(strategyTypeIndex) {
    return strategyStoredTypeIndexFromBehaviorKind(strategyBehaviorKindFromTypeIndex(strategyTypeIndex));
}

function strategySupportsFactorOverlay(strategyTypeIndex) {
    return normalizeStrategyTypeIndex(strategyTypeIndex) !== StrategyTypeIndex.Invalid;
}

// 获取策略类型名称
function getStrategyTypeName(typeId) {
    var normalizedTypeId = normalizeStrategyTypeId(typeId);
    if (normalizedTypeId === "alpha") {
        normalizedTypeId = "momentum";
    }
    if (!normalizedTypeId) {
        return "";
    }
    return tr('strategyCreation.strategyTypes.' + normalizedTypeId) || normalizedTypeId;
}

function getStrategyTypeNameFromIndex(strategyTypeIndex) {
    return getStrategyTypeName(strategyTypeIdFromIndex(strategyTypeIndex));
}

// 获取策略类型描述
function getStrategyTypeDescription(typeId) {
    var normalizedTypeId = normalizeStrategyTypeId(typeId);
    if (!normalizedTypeId) {
        return "";
    }
    return tr('strategyCreation.strategyTypeDescriptions.' + normalizedTypeId) || tr('strategyCreation.strategyTypeDescriptions.custom');
}

function getStrategyTypeDescriptionFromIndex(strategyTypeIndex) {
    return getStrategyTypeDescription(strategyTypeIdFromIndex(strategyTypeIndex));
}

// 获取策略类型图标
function getStrategyIcon(typeId) {
    switch(typeId) {
        case "trend_following": return "📈";
        case "trend_breakout": return "🎯";
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

function getStrategyIconFromIndex(strategyTypeIndex) {
    return getStrategyIcon(strategyTypeIdFromIndex(strategyTypeIndex));
}

// 获取策略类型简要描述
function getBriefDescription(typeId) {
    var descriptions = {
        "trend_following": "跟随价格趋势交易",
        "trend_breakout": "突破近高并沿趋势持仓",
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

function getBriefDescriptionFromIndex(strategyTypeIndex) {
    return getBriefDescription(strategyTypeIdFromIndex(strategyTypeIndex));
}

function getDefaultStrategyDescription(strategyTypeIndex) {
    var typeId = strategyTypeIdFromIndex(strategyTypeIndex)
    var descriptions = {
        "trend_following": "基于趋势识别与顺势持仓的交易策略，结合信号确认、仓位控制与止盈止损规则，在日线级别捕捉中期趋势。",
        "trend_breakout": "基于长期均线、近高突破与趋势强度确认的交易策略，在价格接近阶段新高且趋势明确时入场，并结合均线失守与 ATR 跟踪止损退出。",
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

function getDefaultStrategyTags(strategyTypeIndex) {
    var typeId = strategyTypeIdFromIndex(strategyTypeIndex)
    var tags = {
        "trend_following": ["趋势", "顺势", "技术分析"],
        "trend_breakout": ["趋势突破", "年线", "ATR止损"],
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

var AssetTypeIndex = {
    Invalid: 0,
    Stock: 1,
    Futures: 2,
    Options: 3,
    Etf: 4,
    Index: 5,
    MultiAsset: 6
};

var TimeFrameIndex = {
    Invalid: 0,
    Tick: 1,
    OneMinute: 2,
    FiveMinutes: 3,
    FifteenMinutes: 4,
    ThirtyMinutes: 5,
    OneHour: 6,
    Daily: 7,
    Weekly: 8,
    Monthly: 9,
    Custom: 10
};

var RiskLevelIndex = {
    Invalid: 0,
    Low: 1,
    Medium: 2,
    High: 3,
    Aggressive: 4
};

function getAssetTypeNameFromIndex(assetTypeIndex) {
    switch (Math.floor(Number(assetTypeIndex))) {
        case AssetTypeIndex.Stock:
            return "股票";
        case AssetTypeIndex.Futures:
            return "期货";
        case AssetTypeIndex.Options:
            return "期权";
        case AssetTypeIndex.Etf:
            return "ETF";
        case AssetTypeIndex.Index:
            return "指数";
        case AssetTypeIndex.MultiAsset:
            return "多资产";
        default:
            return "未设置";
    }
}

function getTimeFrameNameFromIndex(timeFrameIndex) {
    switch (Math.floor(Number(timeFrameIndex))) {
        case TimeFrameIndex.Tick:
            return "Tick";
        case TimeFrameIndex.OneMinute:
            return "1 分钟";
        case TimeFrameIndex.FiveMinutes:
            return "5 分钟";
        case TimeFrameIndex.FifteenMinutes:
            return "15 分钟";
        case TimeFrameIndex.ThirtyMinutes:
            return "30 分钟";
        case TimeFrameIndex.OneHour:
            return "1 小时";
        case TimeFrameIndex.Daily:
            return "日线";
        case TimeFrameIndex.Weekly:
            return "周线";
        case TimeFrameIndex.Monthly:
            return "月线";
        case TimeFrameIndex.Custom:
            return "自定义";
        default:
            return "未设置";
    }
}

function riskLevelKeyFromIndex(riskLevelIndex) {
    switch (Math.floor(Number(riskLevelIndex))) {
        case RiskLevelIndex.Low:
            return "low";
        case RiskLevelIndex.Medium:
            return "medium";
        case RiskLevelIndex.High:
            return "high";
        case RiskLevelIndex.Aggressive:
            return "aggressive";
        default:
            return "";
    }
}

function getRiskLevelNameFromIndex(riskLevelIndex) {
    var levelKey = riskLevelKeyFromIndex(riskLevelIndex)
    return levelKey ? getRiskLevelName(levelKey) : "未设置"
}

function getRiskLevelColorFromIndex(riskLevelIndex) {
    var levelKey = riskLevelKeyFromIndex(riskLevelIndex)
    return levelKey ? getRiskLevelColor(levelKey) : "#94a3b8"
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
            return Number(context.selectedStrategyTypeIndex) >= 0 && 
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
            if (Number(context.selectedStrategyTypeIndex) < 0) {
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
    var selectedStrategyTypeIndex = normalizeStrategyTypeIndex(context.selectedStrategyTypeIndex);
    var strategyBehaviorKind = strategyBehaviorKindFromTypeIndex(selectedStrategyTypeIndex);
    var strategyTypeId = strategyTypeIdFromIndex(selectedStrategyTypeIndex);
    var normalizedParameters = normalizeStrategyParameters(selectedStrategyTypeIndex, context.strategyParameters);
    normalizedParameters.selectedStrategyTypeIndex = selectedStrategyTypeIndex;
    
    var strategyData = {
        // 基本信息
        name: context.strategyName,
        displayName: context.strategyName,
        selectedStrategyTypeIndex: selectedStrategyTypeIndex,
        strategyBehaviorKind: strategyBehaviorKind,
        strategyTypeIndex: strategyStoredTypeIndexFromTypeIndex(selectedStrategyTypeIndex),
        typeName: getStrategyTypeName(strategyTypeId),
        description: context.strategyDescription,
        
        // 基本属性
        assetTypeIndex: context.assetTypeIndex,
        timeFrameIndex: context.timeFrameIndex,
        riskLevelIndex: context.riskLevelIndex,
        optimizationMethod: context.optimizationMethod,
        
        // 高级选项
        enableAdvancedOptions: context.enableAdvancedOptions,
        
        // 元数据
        statusIndex: 2,
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

function normalizeStrategyParameters(strategyTypeIndex, rawParameters) {
    var source = rawParameters || ({})
    var normalized = ({})
    var normalizedStrategyTypeIndex = normalizeStrategyTypeIndex(strategyTypeIndex)

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

    assignIfPresent("positionSize", ["positionSize"], normalizePercentageToRatio)
    assignIfPresent("stopLoss", ["stopLoss"], normalizePercentageToRatio)
    assignIfPresent("takeProfit", ["takeProfit"], normalizePercentageToRatio)
    assignIfPresent("maxDrawdownLimit", ["maxDrawdownLimit"], Number)
    assignIfPresent("rebalanceDays", ["rebalanceDays"], Number)

    if (normalizedStrategyTypeIndex === StrategyTypeIndex.TrendFollowing) {
        assignIfPresent("fastPeriod", ["fastPeriod"], Number)
        assignIfPresent("slowPeriod", ["slowPeriod"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.TrendBreakout) {
        assignIfPresent("longTrendPeriod", ["longTrendPeriod"], Number)
        assignIfPresent("breakoutLookbackPeriod", ["breakoutLookbackPeriod"], Number)
        assignIfPresent("breakoutThreshold", ["breakoutThreshold"], normalizePercentageToRatio)
        assignIfPresent("adxPeriod", ["adxPeriod"], Number)
        assignIfPresent("adxThreshold", ["adxThreshold"], Number)
        assignIfPresent("exitMaPeriod", ["exitMaPeriod"], Number)
        assignIfPresent("atrPeriod", ["atrPeriod"], Number)
        assignIfPresent("atrMultiplier", ["atrMultiplier"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MeanReversion) {
        assignIfPresent("bollPeriod", ["bollPeriod"], Number)
        assignIfPresent("bollStd", ["bollStd"], Number)
        assignIfPresent("reversionThreshold", ["reversionThreshold"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.Momentum) {
        assignIfPresent("topN", ["topN"], Number)
        assignIfPresent("momentumPeriod", ["momentumPeriod"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.Arbitrage) {
        assignIfPresent("spreadThreshold", ["spreadThreshold"], Number)
        assignIfPresent("entryZScore", ["entryZScore"], Number)
        assignIfPresent("exitZScore", ["exitZScore"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MachineLearning) {
        assignIfPresent("featureWindow", ["featureWindow"], Number)
        assignIfPresent("predictionDays", ["predictionDays"], Number)
        assignIfPresent("trainingDays", ["trainingDays"], Number)
        assignIfPresent("confidenceThreshold", ["confidenceThreshold"], normalizePercentageToRatio)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MultiFactor) {
        assignIfPresent("factorTypes", ["factorTypes"])
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.HighFrequency) {
        assignIfPresent("timeframe", ["timeframe"])
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.EventDriven) {
        assignIfPresent("eventTypes", ["eventTypes"])
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.Custom) {
        assignIfPresent("customCode", ["customCode"])
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
    function buildParamConfigs(strategyTypeIndex) {
        var configs = [];
        var normalizedStrategyTypeIndex = normalizeStrategyTypeIndex(strategyTypeIndex)
        
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
            id: "maxDrawdownLimit",
            type: "slider",
            label: tr('strategyCreation.maxDrawdownLimit'),
            description: tr('strategyCreation.maxDrawdownDescription'),
            default: 20,
            min: 1,
            max: 80,
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
    if (normalizedStrategyTypeIndex === StrategyTypeIndex.Common) {
        return configs;
    }
    
    // ============ 策略特定参数 ============
    if (normalizedStrategyTypeIndex === StrategyTypeIndex.TrendFollowing) {
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
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.TrendBreakout) {
        configs.push({
            id: "longTrendPeriod",
            type: "slider",
            label: tr('strategyCreation.longTrendPeriod'),
            description: tr('strategyCreation.longTrendPeriodDescription'),
            default: 250,
            min: 120,
            max: 300,
            step: 5,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.personalizedParameters')
        });

        configs.push({
            id: "breakoutLookbackPeriod",
            type: "slider",
            label: tr('strategyCreation.breakoutLookbackPeriod'),
            description: tr('strategyCreation.breakoutLookbackPeriodDescription'),
            default: 60,
            min: 20,
            max: 250,
            step: 5,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.personalizedParameters')
        });

        configs.push({
            id: "breakoutThreshold",
            type: "slider",
            label: tr('strategyCreation.breakoutThreshold'),
            description: tr('strategyCreation.breakoutThresholdDescription'),
            default: 95,
            min: 85,
            max: 100,
            step: 1,
            unit: "%",
            category: tr('strategyCreation.personalizedParameters')
        });

        configs.push({
            id: "adxPeriod",
            type: "slider",
            label: tr('strategyCreation.adxPeriod'),
            description: tr('strategyCreation.adxPeriodDescription'),
            default: 14,
            min: 5,
            max: 50,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.personalizedParameters')
        });

        configs.push({
            id: "adxThreshold",
            type: "slider",
            label: tr('strategyCreation.adxThreshold'),
            description: tr('strategyCreation.adxThresholdDescription'),
            default: 25,
            min: 10,
            max: 50,
            step: 1,
            unit: "",
            category: tr('strategyCreation.personalizedParameters')
        });

        configs.push({
            id: "exitMaPeriod",
            type: "slider",
            label: tr('strategyCreation.exitMaPeriod'),
            description: tr('strategyCreation.exitMaPeriodDescription'),
            default: 50,
            min: 10,
            max: 200,
            step: 5,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.personalizedParameters')
        });

        configs.push({
            id: "atrPeriod",
            type: "slider",
            label: tr('strategyCreation.atrPeriod'),
            description: tr('strategyCreation.atrPeriodDescription'),
            default: 10,
            min: 5,
            max: 50,
            step: 1,
            unit: tr('strategyCreation.daysUnit'),
            category: tr('strategyCreation.personalizedParameters')
        });

        configs.push({
            id: "atrMultiplier",
            type: "slider",
            label: tr('strategyCreation.atrMultiplier'),
            description: tr('strategyCreation.atrMultiplierDescription'),
            default: 2.0,
            min: 0.5,
            max: 5.0,
            step: 0.1,
            decimals: 1,
            unit: "xATR",
            category: tr('strategyCreation.personalizedParameters')
        });
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MeanReversion) {
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
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.Momentum) {
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
        
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.Arbitrage) {
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
        
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MachineLearning) {
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
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MultiFactor) {
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
        
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.HighFrequency) {
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
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.EventDriven) {
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
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.Custom) {
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

function buildDefaultStrategyProfile(strategyTypeIndex) {
    var normalizedTypeIndex = normalizeStrategyTypeIndex(strategyTypeIndex)
    if (normalizedTypeIndex === StrategyTypeIndex.Invalid) {
        normalizedTypeIndex = StrategyTypeIndex.TrendFollowing
    }
    var profile = {
        strategyTypeIndex: normalizedTypeIndex,
        strategyBehaviorKind: strategyBehaviorKindFromTypeIndex(normalizedTypeIndex),
        horizon: "swing",
        tradingFrequency: "low_frequency",
        marketScope: "a_share",
        executionStyle: "close_confirmed"
    };

    if (isTrendStrategyTypeIndex(normalizedTypeIndex)) {
        profile.horizon = "swing";
        profile.tradingFrequency = "low_frequency";
        profile.executionStyle = "close_confirmed";
    } else if (normalizedTypeIndex === StrategyTypeIndex.MeanReversion) {
        profile.horizon = "swing";
        profile.tradingFrequency = "medium_frequency";
        profile.executionStyle = "intraday_confirmed";
    } else if (normalizedTypeIndex === StrategyTypeIndex.Momentum) {
        profile.horizon = "short_term";
        profile.tradingFrequency = "medium_frequency";
        profile.executionStyle = "open_followup";
    } else if (normalizedTypeIndex === StrategyTypeIndex.HighFrequency) {
        profile.horizon = "intraday";
        profile.tradingFrequency = "high_frequency";
        profile.executionStyle = "tick_driven";
    } else if (normalizedTypeIndex === StrategyTypeIndex.EventDriven) {
        profile.horizon = "short_term";
        profile.tradingFrequency = "event_driven";
        profile.executionStyle = "event_confirmed";
    }

    return profile;
}

function getRuleComposerStageDefinitions() {
    return [
        {
            stageId: "market",
            title: "市场环境",
            description: "先判断当前市场是否允许承担新增风险。",
            accentColor: "#2563eb"
        },
        {
            stageId: "eligibility",
            title: "标的准入",
            description: "过滤不满足流动性、风控和池子约束的标的。",
            accentColor: "#0ea5e9"
        },
        {
            stageId: "signal",
            title: "入场确认",
            description: "定义候选入场、观察信号和否决条件。",
            accentColor: "#22c55e"
        },
        {
            stageId: "portfolio",
            title: "仓位与组合",
            description: "约束单票、组合和行业暴露。",
            accentColor: "#14b8a6"
        },
        {
            stageId: "rebalance",
            title: "调仓退出",
            description: "定义减仓、止盈、止损和退出动作。",
            accentColor: "#f59e0b"
        },
        {
            stageId: "execution",
            title: "执行约束",
            description: "决定节流、拆单和成交推进约束。",
            accentColor: "#a855f7"
        },
        {
            stageId: "account_risk",
            title: "账户风控",
            description: "账户级回撤、熔断和停机边界。",
            accentColor: "#ef4444"
        }
    ];
}

function resolveRuleTemplateFileName(templateId) {
    var key = String(templateId || "").trim();
    var mapping = {
        template_eligibility_trend_participation_guard_v1: "eligibility_trend_participation_guard.yaml",
        template_watch_trend_structure_breakdown_v1: "watch_trend_structure_breakdown.yaml",
        template_entry_trend_support_near_ma_v1: "entry_trend_support_near_ma.yaml",
        template_risk_market_trend_neutral_allow_entry_v1: "risk_market_trend_neutral_allow_entry.yaml",
        template_entry_pullback_ma20_support_candidate_v1: "entry_pullback_ma20_support_candidate.yaml",
        template_entry_pullback_ma60_support_candidate_v1: "entry_pullback_ma60_support_candidate.yaml",
        template_entry_pullback_ma20_support_v1: "entry_pullback_ma20_support.yaml",
        template_entry_pullback_ma60_support_v1: "entry_pullback_ma60_support.yaml",
        template_entry_midterm_platform_breakout_v1: "entry_midterm_platform_breakout.yaml",
        template_entry_long_term_yearline_reclaim_v1: "entry_long_term_yearline_reclaim.yaml",
        template_entry_catch_up_breakout_v1: "entry_catch_up_breakout.yaml",
        template_entry_event_earnings_surprise_breakout_v1: "entry_event_earnings_surprise_breakout.yaml",
        template_entry_hft_orderflow_reclaim_v1: "entry_hft_orderflow_reclaim.yaml",
        template_entry_emotion_reflow_repair_v1: "entry_emotion_reflow_repair.yaml",
        template_exit_scale_out_take_profit_v1: "exit_scale_out_take_profit.yaml",
        template_exit_acceptance_breakdown_v1: "exit_acceptance_breakdown.yaml",
        template_risk_market_bull_trend_allow_entry_v1: "risk_market_bull_trend_allow_entry.yaml",
        template_risk_market_sideways_selective_entry_v1: "risk_market_sideways_selective_entry.yaml",
        template_risk_market_bear_freeze_entry_v1: "risk_market_bear_freeze_entry.yaml",
        template_risk_market_emotion_repair_allow_entry_v1: "risk_market_emotion_repair_allow_entry.yaml"
    };
    return mapping[key] || "";
}

function canonicalRulePackStageId(value) {
    var key = String(value || "").trim().toLowerCase();
    var validStages = {
        market: true,
        eligibility: true,
        signal: true,
        portfolio: true,
        rebalance: true,
        execution: true,
        account_risk: true
    };
    return validStages[key] ? key : "";
}

function createDefaultRulePackEntry(spec) {
    var stageId = canonicalRulePackStageId(spec && spec.stageId);
    if (stageId === "") {
        throw new Error("默认规则包缺少合法 stageId");
    }

    return {
        stageId: stageId,
        groupId: String((spec && spec.groupId) || "").trim(),
        groupTitle: String((spec && spec.groupTitle) || "").trim(),
        groupRole: String((spec && spec.groupRole) || "").trim().toLowerCase(),
        groupOperator: String((spec && spec.groupOperator) || "").trim().toLowerCase(),
        templateId: String((spec && spec.templateId) || "").trim(),
        templateDisplayName: String((spec && spec.templateDisplayName) || "").trim(),
        fileName: String((spec && spec.fileName) || resolveRuleTemplateFileName(spec && spec.templateId)).trim(),
        summary: String((spec && spec.summary) || "").trim(),
        category: String((spec && spec.category) || "").trim(),
        termId: String((spec && spec.termId) || "").trim(),
        termDisplayName: String((spec && spec.termDisplayName) || "").trim(),
        defaultInjected: true
    };
}

function buildDefaultBaseRuleBindings(strategyProfile) {
    var profile = strategyProfile || buildDefaultStrategyProfile(StrategyTypeIndex.TrendFollowing);
    var strategyTypeIndex = resolveProfileStrategyTypeIndex(profile);

    var specs = [];
    if (strategyTypeIndex === StrategyTypeIndex.TrendFollowing) {
        specs = [
            {
                stageId: "eligibility",
                groupId: "eligibility_core",
                groupTitle: "基础过滤组",
                groupRole: "must_pass",
                groupOperator: "all",
                templateId: "template_eligibility_trend_participation_guard_v1",
                templateDisplayName: "趋势参与资格过滤模板",
                summary: "先过滤流动性不足、趋势失速或偏离均线过大的候选，避免裸信号直接开仓。",
                category: "eligibility_filter",
                termId: "trend_participation_guard",
                termDisplayName: "趋势参与资格过滤"
            },
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_trend_support_near_ma_v1",
                templateDisplayName: "趋势支撑邻近候选模板",
                summary: "用于识别贴近 20/60 日线且趋势未明显走坏的趋势延续候选，降低强确认布尔条件导致的长期零命中。",
                category: "entry_pattern",
                termId: "entry_trend_support_near_ma",
                termDisplayName: "趋势支撑邻近候选"
            },
            {
                stageId: "signal",
                groupId: "signal_veto",
                groupTitle: "信号否决组",
                groupRole: "veto",
                groupOperator: "any",
                templateId: "template_watch_trend_structure_breakdown_v1",
                templateDisplayName: "趋势结构破坏阻断模板",
                summary: "价格失守趋势支撑、均线斜率转弱或活跃度塌陷时，阻断继续按趋势候选开仓。",
                category: "watch_invalidation",
                termId: "trend_structure_breakdown_watch",
                termDisplayName: "趋势结构破坏阻断"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_exit",
                groupTitle: "退出触发组",
                groupRole: "any_pass",
                groupOperator: "any",
                templateId: "template_exit_acceptance_breakdown_v1",
                templateDisplayName: "承接走弱退出模板",
                summary: "承接明显走弱后执行保护性退出。",
                category: "exit_pattern",
                termId: "exit_acceptance_breakdown",
                termDisplayName: "承接走弱退出"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_scale",
                groupTitle: "分批管理组",
                groupRole: "position_management",
                groupOperator: "all",
                templateId: "template_exit_scale_out_take_profit_v1",
                templateDisplayName: "分批止盈模板",
                summary: "趋势未完全破坏时先分批兑现利润。",
                category: "exit_management",
                termId: "exit_scale_out_take_profit",
                termDisplayName: "分批止盈"
            }
        ];
    } else if (strategyTypeIndex === StrategyTypeIndex.TrendBreakout) {
        specs = [
            {
                stageId: "eligibility",
                groupId: "eligibility_core",
                groupTitle: "基础过滤组",
                groupRole: "must_pass",
                groupOperator: "all",
                templateId: "template_eligibility_trend_participation_guard_v1",
                templateDisplayName: "趋势参与资格过滤模板",
                summary: "先过滤流动性不足、趋势失速或偏离均线过大的候选，避免裸突破信号直接开仓。",
                category: "eligibility_filter",
                termId: "trend_participation_guard",
                termDisplayName: "趋势参与资格过滤"
            },
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_midterm_platform_breakout_v1",
                templateDisplayName: "中期平台突破模板",
                summary: "用于识别中期平台放量突破。",
                category: "entry_pattern",
                termId: "entry_midterm_platform_breakout",
                termDisplayName: "中期平台突破"
            },
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_long_term_yearline_reclaim_v1",
                templateDisplayName: "年线收复回踩确认模板",
                summary: "用于识别年线收复后的回踩确认。",
                category: "entry_pattern",
                termId: "entry_long_term_yearline_reclaim",
                termDisplayName: "年线收复回踩确认"
            },
            {
                stageId: "signal",
                groupId: "signal_veto",
                groupTitle: "信号否决组",
                groupRole: "veto",
                groupOperator: "any",
                templateId: "template_watch_trend_structure_breakdown_v1",
                templateDisplayName: "趋势结构破坏阻断模板",
                summary: "价格失守趋势支撑、均线斜率转弱或活跃度塌陷时，阻断继续按趋势突破候选开仓。",
                category: "watch_invalidation",
                termId: "trend_structure_breakdown_watch",
                termDisplayName: "趋势结构破坏阻断"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_exit",
                groupTitle: "退出触发组",
                groupRole: "any_pass",
                groupOperator: "any",
                templateId: "template_exit_acceptance_breakdown_v1",
                templateDisplayName: "承接走弱退出模板",
                summary: "承接明显走弱后执行保护性退出。",
                category: "exit_pattern",
                termId: "exit_acceptance_breakdown",
                termDisplayName: "承接走弱退出"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_scale",
                groupTitle: "分批管理组",
                groupRole: "position_management",
                groupOperator: "all",
                templateId: "template_exit_scale_out_take_profit_v1",
                templateDisplayName: "分批止盈模板",
                summary: "趋势突破后分段兑现利润。",
                category: "exit_management",
                termId: "exit_scale_out_take_profit",
                termDisplayName: "分批止盈"
            }
        ];
    } else if (strategyTypeIndex === StrategyTypeIndex.Momentum) {
        specs = [
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_catch_up_breakout_v1",
                templateDisplayName: "补涨突破模板",
                summary: "用于识别补涨标的由跟随转主动的启动点。",
                category: "entry_pattern",
                termId: "entry_catch_up_breakout",
                termDisplayName: "补涨突破"
            },
            {
                stageId: "signal",
                groupId: "signal_boost",
                groupTitle: "评分增强组",
                groupRole: "score_boost",
                groupOperator: "score_sum",
                templateId: "template_entry_emotion_reflow_repair_v1",
                templateDisplayName: "情绪修复回流模板",
                summary: "用于识别市场情绪修复后的主线回流确认。",
                category: "entry_pattern",
                termId: "entry_emotion_reflow_repair",
                termDisplayName: "情绪修复回流"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_exit",
                groupTitle: "退出触发组",
                groupRole: "any_pass",
                groupOperator: "any",
                templateId: "template_exit_acceptance_breakdown_v1",
                templateDisplayName: "承接走弱退出模板",
                summary: "承接明显走弱后执行保护性退出。",
                category: "exit_pattern",
                termId: "exit_acceptance_breakdown",
                termDisplayName: "承接走弱退出"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_scale",
                groupTitle: "分批管理组",
                groupRole: "position_management",
                groupOperator: "all",
                templateId: "template_exit_scale_out_take_profit_v1",
                templateDisplayName: "分批止盈模板",
                summary: "动量延续仍在时先分段兑现利润。",
                category: "exit_management",
                termId: "exit_scale_out_take_profit",
                termDisplayName: "分批止盈"
            }
        ];
    } else if (strategyTypeIndex === StrategyTypeIndex.MeanReversion) {
        specs = [
            {
                stageId: "eligibility",
                groupId: "eligibility_core",
                groupTitle: "基础过滤组",
                groupRole: "must_pass",
                groupOperator: "all",
                templateId: "template_eligibility_trend_participation_guard_v1",
                templateDisplayName: "趋势参与资格过滤模板",
                summary: "先过滤流动性不足、趋势过度恶化或偏离均线过大的候选。",
                category: "eligibility_filter",
                termId: "trend_participation_guard",
                termDisplayName: "趋势参与资格过滤"
            },
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_pullback_ma20_support_v1",
                templateDisplayName: "回踩 20 日线确认模板",
                summary: "用于识别回踩 20 日线后的企稳确认。",
                category: "entry_pattern",
                termId: "entry_pullback_ma20_support",
                termDisplayName: "回踩20日线"
            },
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_pullback_ma60_support_v1",
                templateDisplayName: "回踩 60 日线确认模板",
                summary: "用于识别回踩 60 日线后的企稳确认。",
                category: "entry_pattern",
                termId: "entry_pullback_ma60_support",
                termDisplayName: "回踩60日线"
            },
            {
                stageId: "signal",
                groupId: "signal_veto",
                groupTitle: "信号否决组",
                groupRole: "veto",
                groupOperator: "any",
                templateId: "template_watch_trend_structure_breakdown_v1",
                templateDisplayName: "趋势结构破坏阻断模板",
                summary: "价格持续失守支撑时阻断继续做均值回归。",
                category: "watch_invalidation",
                termId: "trend_structure_breakdown_watch",
                termDisplayName: "趋势结构破坏阻断"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_exit",
                groupTitle: "退出触发组",
                groupRole: "any_pass",
                groupOperator: "any",
                templateId: "template_exit_acceptance_breakdown_v1",
                templateDisplayName: "承接走弱退出模板",
                summary: "反弹承接明显走弱后执行保护性退出。",
                category: "exit_pattern",
                termId: "exit_acceptance_breakdown",
                termDisplayName: "承接走弱退出"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_scale",
                groupTitle: "分批管理组",
                groupRole: "position_management",
                groupOperator: "all",
                templateId: "template_exit_scale_out_take_profit_v1",
                templateDisplayName: "分批止盈模板",
                summary: "均值回归修复后采用分批兑现利润。",
                category: "exit_management",
                termId: "exit_scale_out_take_profit",
                termDisplayName: "分批止盈"
            }
        ];
    } else if (strategyTypeIndex === StrategyTypeIndex.EventDriven) {
        specs = [
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_event_earnings_surprise_breakout_v1",
                templateDisplayName: "业绩超预期突破模板",
                summary: "用于识别业绩超预期后的放量突破。",
                category: "entry_pattern",
                termId: "entry_event_earnings_surprise_breakout",
                termDisplayName: "业绩超预期突破"
            },
            {
                stageId: "signal",
                groupId: "signal_boost",
                groupTitle: "评分增强组",
                groupRole: "score_boost",
                groupOperator: "score_sum",
                templateId: "template_entry_emotion_reflow_repair_v1",
                templateDisplayName: "情绪修复回流模板",
                summary: "情绪修复与主线回流时增强事件驱动候选优先级。",
                category: "entry_pattern",
                termId: "entry_emotion_reflow_repair",
                termDisplayName: "情绪修复回流"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_exit",
                groupTitle: "退出触发组",
                groupRole: "any_pass",
                groupOperator: "any",
                templateId: "template_exit_acceptance_breakdown_v1",
                templateDisplayName: "承接走弱退出模板",
                summary: "承接明显走弱后执行保护性退出。",
                category: "exit_pattern",
                termId: "exit_acceptance_breakdown",
                termDisplayName: "承接走弱退出"
            }
        ];
    } else if (strategyTypeIndex === StrategyTypeIndex.HighFrequency) {
        specs = [
            {
                stageId: "eligibility",
                groupId: "eligibility_core",
                groupTitle: "基础过滤组",
                groupRole: "must_pass",
                groupOperator: "all",
                templateId: "template_eligibility_trend_participation_guard_v1",
                templateDisplayName: "趋势参与资格过滤模板",
                summary: "先过滤流动性和参与资格明显不足的候选。",
                category: "eligibility_filter",
                termId: "trend_participation_guard",
                termDisplayName: "趋势参与资格过滤"
            },
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_hft_orderflow_reclaim_v1",
                templateDisplayName: "盘口扫单回补确认模板",
                summary: "用于识别盘口回补和订单流重新占优的短线确认。",
                category: "entry_pattern",
                termId: "entry_hft_orderflow_reclaim",
                termDisplayName: "盘口扫单回补确认"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_exit",
                groupTitle: "退出触发组",
                groupRole: "any_pass",
                groupOperator: "any",
                templateId: "template_exit_acceptance_breakdown_v1",
                templateDisplayName: "承接走弱退出模板",
                summary: "盘口承接明显衰减后执行保护性退出。",
                category: "exit_pattern",
                termId: "exit_acceptance_breakdown",
                termDisplayName: "承接走弱退出"
            }
        ];
    } else if (strategyTypeIndex === StrategyTypeIndex.MultiFactor
               || strategyTypeIndex === StrategyTypeIndex.MachineLearning
               || strategyTypeIndex === StrategyTypeIndex.Arbitrage) {
        specs = [
            {
                stageId: "eligibility",
                groupId: "eligibility_core",
                groupTitle: "基础过滤组",
                groupRole: "must_pass",
                groupOperator: "all",
                templateId: "template_eligibility_trend_participation_guard_v1",
                templateDisplayName: "趋势参与资格过滤模板",
                summary: "作为模型或组合策略的外层准入过滤，先挡掉明显不该参与的样本。",
                category: "eligibility_filter",
                termId: "trend_participation_guard",
                termDisplayName: "趋势参与资格过滤"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_exit",
                groupTitle: "退出触发组",
                groupRole: "any_pass",
                groupOperator: "any",
                templateId: "template_exit_acceptance_breakdown_v1",
                templateDisplayName: "承接走弱退出模板",
                summary: "外层门禁发现承接明显恶化时执行保护性退出。",
                category: "exit_pattern",
                termId: "exit_acceptance_breakdown",
                termDisplayName: "承接走弱退出"
            }
        ];
    } else {
        specs = [
            {
                stageId: "eligibility",
                groupId: "eligibility_core",
                groupTitle: "基础过滤组",
                groupRole: "must_pass",
                groupOperator: "all",
                templateId: "template_eligibility_trend_participation_guard_v1",
                templateDisplayName: "趋势参与资格过滤模板",
                summary: "默认先过滤明显不满足参与条件的候选。",
                category: "eligibility_filter",
                termId: "trend_participation_guard",
                termDisplayName: "趋势参与资格过滤"
            },
            {
                stageId: "signal",
                groupId: "signal_core",
                groupTitle: "核心确认组",
                groupRole: "must_pass",
                groupOperator: "any",
                templateId: "template_entry_trend_support_near_ma_v1",
                templateDisplayName: "趋势支撑邻近候选模板",
                summary: "作为自定义策略的通用趋势型默认入场候选。",
                category: "entry_pattern",
                termId: "entry_trend_support_near_ma",
                termDisplayName: "趋势支撑邻近候选"
            },
            {
                stageId: "signal",
                groupId: "signal_veto",
                groupTitle: "信号否决组",
                groupRole: "veto",
                groupOperator: "any",
                templateId: "template_watch_trend_structure_breakdown_v1",
                templateDisplayName: "趋势结构破坏阻断模板",
                summary: "默认阻断明显走坏的趋势样本。",
                category: "watch_invalidation",
                termId: "trend_structure_breakdown_watch",
                termDisplayName: "趋势结构破坏阻断"
            },
            {
                stageId: "rebalance",
                groupId: "rebalance_exit",
                groupTitle: "退出触发组",
                groupRole: "any_pass",
                groupOperator: "any",
                templateId: "template_exit_acceptance_breakdown_v1",
                templateDisplayName: "承接走弱退出模板",
                summary: "默认在承接明显走弱时执行保护性退出。",
                category: "exit_pattern",
                termId: "exit_acceptance_breakdown",
                termDisplayName: "承接走弱退出"
            }
        ];
    }

    return specs.map(createDefaultRulePackEntry);
}

function normalizeRuleComposerBindingStage(binding) {
    var stageId = String((binding && (binding.stageId || binding.phase)) || "signal").trim().toLowerCase();
    return canonicalRulePackStageId(stageId);
}

function injectRecommendedBaseBindings(bindings, recommendedBindings) {
    var existingBindings = Array.isArray(bindings) ? bindings.slice() : [];
    var recommended = Array.isArray(recommendedBindings) ? recommendedBindings : [];
    var existingGroupCounts = {};
    var existingTemplates = {};

    for (var index = 0; index < existingBindings.length; ++index) {
        var binding = existingBindings[index] || {};
        var templateId = String(binding.templateId || binding.template_id || "").trim();
        var groupKey = normalizeRuleComposerBindingStage(binding)
            + "::" + String(binding.group_id || binding.groupId || "").trim().toLowerCase();
        existingGroupCounts[groupKey] = (existingGroupCounts[groupKey] || 0) + 1;
        if (templateId) {
            existingTemplates[templateId] = true;
        }
    }

    for (var recommendedIndex = 0; recommendedIndex < recommended.length; ++recommendedIndex) {
        var recommendedBinding = recommended[recommendedIndex] || {};
        var recommendedTemplateId = String(recommendedBinding.templateId || recommendedBinding.template_id || "").trim();
        var recommendedGroupKey = normalizeRuleComposerBindingStage(recommendedBinding)
            + "::" + String(recommendedBinding.group_id || recommendedBinding.groupId || "").trim().toLowerCase();
        if (recommendedTemplateId && existingTemplates[recommendedTemplateId]) {
            continue;
        }
        if ((existingGroupCounts[recommendedGroupKey] || 0) > 0) {
            continue;
        }
        existingBindings.push(recommendedBinding);
    }

    return existingBindings;
}

function buildDefaultMarketRuleBindings(strategyProfile) {
    var profile = strategyProfile || buildDefaultStrategyProfile(StrategyTypeIndex.TrendFollowing);
    var strategyTypeIndex = resolveProfileStrategyTypeIndex(profile);

    var gateSpecs = [];
    var vetoSpec = {
        groupId: "market_veto",
        groupTitle: "风险否决组",
        groupRole: "veto",
        groupOperator: "any",
        templateId: "template_risk_market_bear_freeze_entry_v1",
        templateDisplayName: "熊市冻结新开仓模板",
        fileName: "risk_market_bear_freeze_entry.yaml",
        summary: "市场进入熊市或系统性退潮阶段时，优先冻结新增仓位。",
        category: "market_risk",
        termId: "market_bear_freeze_entry",
        termDisplayName: "熊市冻结新开仓"
    };

    if (isTrendStrategyTypeIndex(strategyTypeIndex) || strategyTypeIndex === StrategyTypeIndex.Momentum) {
        gateSpecs = [
            {
                groupId: "market_gate",
                groupTitle: "市场放行组",
                groupRole: "any_pass",
                groupOperator: "at_least",
                templateId: "template_risk_market_trend_neutral_allow_entry_v1",
                templateDisplayName: "趋势中性环境放行模板",
                fileName: "risk_market_trend_neutral_allow_entry.yaml",
                summary: "市场未进入熊市且广度、趋势强度与回撤压力仍在可承受区间时，放行日线趋势策略继续筛股。",
                category: "market_gate",
                termId: "market_trend_neutral_allow_entry",
                termDisplayName: "趋势中性环境放行"
            },
            {
                groupId: "market_gate",
                groupTitle: "市场放行组",
                groupRole: "any_pass",
                groupOperator: "at_least",
                templateId: "template_risk_market_bull_trend_allow_entry_v1",
                templateDisplayName: "牛市趋势放行模板",
                fileName: "risk_market_bull_trend_allow_entry.yaml",
                summary: "市场处于牛市趋势阶段且广度、趋势强度同步改善时，放行趋势类新增仓位。",
                category: "market_gate",
                termId: "market_bull_trend_allow_entry",
                termDisplayName: "牛市趋势放行新开仓"
            },
            {
                groupId: "market_gate",
                groupTitle: "市场放行组",
                groupRole: "any_pass",
                groupOperator: "at_least",
                templateId: "template_risk_market_sideways_selective_entry_v1",
                templateDisplayName: "震荡市精选放行模板",
                fileName: "risk_market_sideways_selective_entry.yaml",
                summary: "市场处于震荡阶段且波动受控时，放行精选确认类候选。",
                category: "market_gate",
                termId: "market_sideways_selective_entry",
                termDisplayName: "震荡市精选放行"
            }
        ];
    } else if (strategyTypeIndex === StrategyTypeIndex.MeanReversion || strategyTypeIndex === StrategyTypeIndex.HighFrequency) {
        gateSpecs = [{
            groupId: "market_gate",
            groupTitle: "市场放行组",
            groupRole: "must_pass",
            groupOperator: "all",
            templateId: "template_risk_market_sideways_selective_entry_v1",
            templateDisplayName: "震荡市精选放行模板",
            fileName: "risk_market_sideways_selective_entry.yaml",
            summary: "市场处于震荡阶段且波动受控时，仅放行精选确认类新增仓位。",
            category: "market_gate",
            termId: "market_sideways_selective_entry",
            termDisplayName: "震荡市精选放行"
        }];
    } else if (strategyTypeIndex === StrategyTypeIndex.EventDriven) {
        gateSpecs = [{
            groupId: "market_gate",
            groupTitle: "市场放行组",
            groupRole: "must_pass",
            groupOperator: "all",
            templateId: "template_risk_market_bull_trend_allow_entry_v1",
            templateDisplayName: "牛市趋势放行模板",
            fileName: "risk_market_bull_trend_allow_entry.yaml",
            summary: "市场处于牛市趋势阶段且广度、趋势强度同步改善时，放行趋势类新增仓位。",
            category: "market_gate",
            termId: "market_bull_trend_allow_entry",
            termDisplayName: "牛市趋势放行新开仓"
        }];
    } else if (strategyTypeIndex === StrategyTypeIndex.MultiFactor || strategyTypeIndex === StrategyTypeIndex.MachineLearning || strategyTypeIndex === StrategyTypeIndex.Arbitrage) {
        gateSpecs = [{
            groupId: "market_gate",
            groupTitle: "市场放行组",
            groupRole: "must_pass",
            groupOperator: "all",
            templateId: "template_risk_market_sideways_selective_entry_v1",
            templateDisplayName: "震荡市精选放行模板",
            fileName: "risk_market_sideways_selective_entry.yaml",
            summary: "市场处于震荡阶段且波动受控时，仅放行精选确认类新增仓位。",
            category: "market_gate",
            termId: "market_sideways_selective_entry",
            termDisplayName: "震荡市精选放行"
        }];
    } else {
        gateSpecs = [{
            groupId: "market_gate",
            groupTitle: "市场放行组",
            groupRole: "must_pass",
            groupOperator: "all",
            templateId: "template_risk_market_emotion_repair_allow_entry_v1",
            templateDisplayName: "情绪修复放行模板",
            fileName: "risk_market_emotion_repair_allow_entry.yaml",
            summary: "情绪修复且高位承接、回封率、题材宽度同步回暖时，放行新增仓位。",
            category: "market_gate",
            termId: "market_emotion_repair_allow_entry",
            termDisplayName: "情绪修复放行新开仓"
        }];
    }

    var bindings = [];
    for (var gateIndex = 0; gateIndex < gateSpecs.length; ++gateIndex) {
        bindings.push(createDefaultRulePackEntry(Object.assign({ stageId: "market" }, gateSpecs[gateIndex])));
    }
    bindings.push(createDefaultRulePackEntry(Object.assign({ stageId: "market" }, vetoSpec)));
    return bindings;
}

function buildDefaultRuleComposerSkeleton(strategyProfile, rawBindings) {
    var profile = strategyProfile || buildDefaultStrategyProfile(StrategyTypeIndex.TrendFollowing);
    var strategyTypeIndex = resolveProfileStrategyTypeIndex(profile);
    var stageDefinitions = getRuleComposerStageDefinitions();
    var groupsByStage = {
        market: [
            { groupId: "market_gate", title: "市场放行组", role: "must_pass", operator: "all", description: "先确认市场允许承担新增风险。", rules: [] },
            { groupId: "market_veto", title: "风险否决组", role: "veto", operator: "any", description: "命中任一风险边界就冻结新开仓。", rules: [] }
        ],
        eligibility: [
            { groupId: "eligibility_core", title: "基础过滤组", role: "must_pass", operator: "all", description: "满足池子、流动性和交易资格。", rules: [] }
        ],
        signal: [
            { groupId: "signal_core", title: "核心确认组", role: "must_pass", operator: "all", description: "决定是否具备入场确认条件。", rules: [] },
            { groupId: "signal_boost", title: "评分增强组", role: "score_boost", operator: "score_sum", description: "不改变能否入场，只影响优先级。", rules: [] },
            { groupId: "signal_veto", title: "信号否决组", role: "veto", operator: "any", description: "命中任一否决条件就阻断当前信号。", rules: [] }
        ],
        portfolio: [
            { groupId: "portfolio_budget", title: "风险预算组", role: "position_management", operator: "all", description: "约束单票和组合层级的风险预算。", rules: [] }
        ],
        rebalance: [
            { groupId: "rebalance_exit", title: "退出触发组", role: "any_pass", operator: "any", description: "任一退出条件命中即可执行减仓或退出。", rules: [] },
            { groupId: "rebalance_scale", title: "分批管理组", role: "position_management", operator: "all", description: "定义分批止盈、减仓和冷却逻辑。", rules: [] }
        ],
        execution: [
            { groupId: "execution_guard", title: "执行限制组", role: "execution_constraint", operator: "all", description: "限制节流、拆单和成交推进。", rules: [] }
        ],
        account_risk: [
            { groupId: "account_guard", title: "账户保护组", role: "account_guard", operator: "any", description: "账户级回撤或异常达到阈值时仲裁所有动作。", rules: [] }
        ]
    };

    if (isTrendStrategyTypeIndex(strategyTypeIndex)) {
        groupsByStage.market[0].description = "优先确认牛市趋势放行，其次看震荡市是否只允许精选参与。";
        groupsByStage.market[1].description = "优先把熊市冻结和系统性退潮放到这里，避免在弱市里追趋势。";
        groupsByStage.market[0].role = "any_pass";
        groupsByStage.market[0].operator = "at_least";
        groupsByStage.signal[0].operator = "any";
        groupsByStage.signal[0].description = "回踩、突破等主信号命中任一条即可继续推进，避免多个入场模板被要求同时成立。";
    } else if (strategyTypeIndex === StrategyTypeIndex.MeanReversion) {
        groupsByStage.market[0].description = "优先确认震荡市精选放行，再决定是否参与回踩修复和均值回归。";
        groupsByStage.market[1].description = "把熊市冻结和单边失配环境放到这里，避免逆势抄底。";
    } else if (strategyTypeIndex === StrategyTypeIndex.EventDriven) {
        groupsByStage.market[0].description = "先确认牛市或震荡市仍允许参与，再按催化强度放行事件驱动候选。";
        groupsByStage.market[1].description = "把熊市冻结和系统性退潮放到这里，避免催化失效时继续开仓。";
    } else if (strategyTypeIndex === StrategyTypeIndex.HighFrequency) {
        groupsByStage.market[0].description = "优先确认震荡市精选放行与微结构稳定，再决定是否进行日内试单。";
        groupsByStage.market[1].description = "把熊市冻结和波动冲击过高的时段放到这里，避免噪音市里高频误触发。";
    } else if (strategyTypeIndex === StrategyTypeIndex.MultiFactor || strategyTypeIndex === StrategyTypeIndex.MachineLearning || strategyTypeIndex === StrategyTypeIndex.Arbitrage) {
        groupsByStage.market[0].description = "先看市场是否允许组合继续承担新增风险，再决定是否恢复候选与调仓。";
        groupsByStage.market[1].description = "优先把熊市冻结、系统性回撤和风险扩散放到这里。";
    }

    if (profile.horizon === "intraday" || profile.tradingFrequency === "high_frequency") {
        groupsByStage.execution.push({
            groupId: "execution_batching",
            title: "批次推进组",
            role: "execution_constraint",
            operator: "all",
            description: "高频或日内策略通常需要更严格的批次和重试节奏。",
            rules: []
        });
    }

    if (strategyTypeIndex === StrategyTypeIndex.Momentum || strategyTypeIndex === StrategyTypeIndex.EventDriven) {
        groupsByStage.signal[0].operator = "any";
        groupsByStage.signal[0].description = "短线或事件驱动策略常由多个信号中的任一触发。";
    }

    var stages = [];
    for (var stageIndex = 0; stageIndex < stageDefinitions.length; ++stageIndex) {
        var definition = stageDefinitions[stageIndex];
        var stageGroups = groupsByStage[definition.stageId] || [];
        var clonedGroups = [];
        for (var groupIndex = 0; groupIndex < stageGroups.length; ++groupIndex) {
            var stageGroup = stageGroups[groupIndex];
            clonedGroups.push({
                groupId: stageGroup.groupId,
                title: stageGroup.title,
                role: stageGroup.role,
                operator: stageGroup.operator,
                description: stageGroup.description,
                rules: []
            });
        }

        stages.push({
            stageId: definition.stageId,
            title: definition.title,
            description: definition.description,
            accentColor: definition.accentColor,
            groups: clonedGroups
        });
    }

    var bindings = [];
    if (Array.isArray(rawBindings)) {
        bindings = rawBindings;
    } else if (rawBindings && typeof rawBindings === "object") {
        for (var bindingKey in rawBindings) {
            if (rawBindings[bindingKey]) {
                bindings.push(rawBindings[bindingKey]);
            }
        }
    }

    var hasMarketBindings = false;
    for (var rawBindingIndex = 0; rawBindingIndex < bindings.length; ++rawBindingIndex) {
        if (normalizeRuleComposerBindingStage(bindings[rawBindingIndex]) === "market") {
            hasMarketBindings = true;
            break;
        }
    }
    if (!hasMarketBindings) {
        bindings = buildDefaultMarketRuleBindings(profile).concat(bindings);
    }

    bindings = injectRecommendedBaseBindings(bindings, buildDefaultBaseRuleBindings(profile));

    function targetGroupIndex(stageId, binding, stageGroups) {
        var groups = Array.isArray(stageGroups) ? stageGroups : [];
        var groupId = String((binding && (binding.group_id || binding.groupId)) || "").trim().toLowerCase();
        var groupRole = String((binding && (binding.group_role || binding.groupRole)) || "").trim().toLowerCase();
        var category = String((binding && binding.category) || "").trim().toLowerCase();

        if (groupId !== "") {
            for (var groupIdIndex = 0; groupIdIndex < groups.length; ++groupIdIndex) {
                if (String(groups[groupIdIndex].groupId || "").trim().toLowerCase() === groupId) {
                    return groupIdIndex;
                }
            }
        }

        if (groupRole !== "") {
            for (var groupRoleIndex = 0; groupRoleIndex < groups.length; ++groupRoleIndex) {
                if (String(groups[groupRoleIndex].role || "").trim().toLowerCase() === groupRole) {
                    return groupRoleIndex;
                }
            }
        }

        if (stageId === "market") {
            if (category === "market_risk") {
                for (var marketRiskIndex = 0; marketRiskIndex < groups.length; ++marketRiskIndex) {
                    if (String(groups[marketRiskIndex].groupId || "") === "market_veto") {
                        return marketRiskIndex;
                    }
                }
            }
            return 0;
        }
        if (stageId === "signal") {
            if (category === "watch_invalidation") {
                for (var signalVetoIndex = 0; signalVetoIndex < groups.length; ++signalVetoIndex) {
                    if (String(groups[signalVetoIndex].groupId || "") === "signal_veto") {
                        return signalVetoIndex;
                    }
                }
            }
            return 0;
        }
        if (stageId === "rebalance") {
            if (category === "exit_management") {
                for (var rebalanceScaleIndex = 0; rebalanceScaleIndex < groups.length; ++rebalanceScaleIndex) {
                    if (String(groups[rebalanceScaleIndex].groupId || "") === "rebalance_scale") {
                        return rebalanceScaleIndex;
                    }
                }
            }
            return 0;
        }
        return 0;
    }

    for (var bindingIndex = 0; bindingIndex < bindings.length; ++bindingIndex) {
        var binding = bindings[bindingIndex] || {};
        var bindingStageId = normalizeRuleComposerBindingStage(binding);
        for (var stageMatchIndex = 0; stageMatchIndex < stages.length; ++stageMatchIndex) {
            var stage = stages[stageMatchIndex];
            if (stage.stageId !== bindingStageId || !stage.groups || stage.groups.length === 0) {
                continue;
            }
            var destinationGroup = stage.groups[targetGroupIndex(bindingStageId, binding, stage.groups)];
            destinationGroup.rules.push({
                instanceId: "binding_" + bindingStageId + "_" + bindingIndex,
                templateId: binding.template_id || binding.templateId || "",
                templateName: binding.template_display_name || binding.templateDisplayName || binding.template_id || binding.templateId || "未命名模板",
                summary: binding.summary || "",
                phase: bindingStageId,
                fileName: binding.file_name || binding.fileName || "",
                filePath: binding.file_path || binding.filePath || "",
                category: binding.category || "",
                termId: binding.term_id || binding.termId || "",
                termName: binding.term_display_name || binding.termDisplayName || "",
                defaultInjected: !!(binding.default_injected || binding.defaultInjected),
                ready: true
            });
            break;
        }
    }

    return stages;
}

function buildRuleComposerIssueKey(stageId, groupId) {
    return String(stageId || "").trim().toLowerCase() + "::" + String(groupId || "").trim().toLowerCase();
}

function ruleLikeTemplateText(rule) {
    return [
        rule && (rule.templateId || rule.template_id),
        rule && (rule.category || ""),
        rule && (rule.termId || rule.term_id),
        rule && (rule.templateName || rule.template_display_name)
    ].join(" ").toLowerCase();
}

function isWatchInvalidationRule(rule) {
    var text = ruleLikeTemplateText(rule);
    return text.indexOf("watch_invalidation") >= 0
        || text.indexOf("invalidated") >= 0
        || text.indexOf("template_watch_") >= 0;
}

function isCompositeBlockingEntryTemplate(rule) {
    var templateId = String(rule && (rule.templateId || rule.template_id) || "").trim();
    var fileName = String(rule && (rule.fileName || rule.file_name) || "").trim().toLowerCase();
    return templateId === "template_entry_pullback_ma20_support_v1"
        || templateId === "template_entry_pullback_ma60_support_v1"
        || fileName === "entry_pullback_ma20_support.yaml"
        || fileName === "entry_pullback_ma60_support.yaml";
}

function isPositiveEntryRule(rule) {
    var text = ruleLikeTemplateText(rule);
    return !isWatchInvalidationRule(rule)
        && (String((rule && rule.category) || "").trim().toLowerCase() === "entry_pattern"
            || text.indexOf("template_entry_") >= 0);
}

function isIntradayOrSpeculativeRule(rule) {
    var text = ruleLikeTemplateText(rule);
    var tokens = [
        "intraday", "orderflow", "hft", "afternoon", "tail_", "open_board",
        "one_word", "gap_up", "reseal", "instant_limit", "first_board"
    ];
    for (var index = 0; index < tokens.length; ++index) {
        if (text.indexOf(tokens[index]) >= 0) {
            return true;
        }
    }
    return false;
}

function isEventDrivenRule(rule) {
    var text = ruleLikeTemplateText(rule);
    return text.indexOf("event_") >= 0 || text.indexOf("earnings") >= 0;
}

function validateRuleComposerConfiguration(strategyProfile, stages) {
    var profile = strategyProfile || buildDefaultStrategyProfile(StrategyTypeIndex.TrendFollowing);
    var strategyTypeIndex = resolveProfileStrategyTypeIndex(profile);
    var stageList = Array.isArray(stages) ? stages : [];
    var errors = [];
    var warnings = [];
    var suggestions = [];
    var groupIssues = {};
    var seenIssueSignatures = {};
    var templateUsage = {};

    function pushIssue(severity, stageId, groupId, code, message) {
        var signature = [severity, stageId || "", groupId || "", code || "", message || ""].join("|");
        if (seenIssueSignatures[signature]) {
            return;
        }
        seenIssueSignatures[signature] = true;

        var issue = {
            severity: severity,
            stageId: stageId || "",
            groupId: groupId || "",
            code: code || "",
            message: message || ""
        };
        if (severity === "error") {
            errors.push(issue);
        } else {
            warnings.push(issue);
        }

        var groupKey = buildRuleComposerIssueKey(stageId, groupId);
        var currentGroupIssue = groupIssues[groupKey] || { level: severity, messages: [] };
        currentGroupIssue.level = currentGroupIssue.level === "error" || severity === "warning"
            ? currentGroupIssue.level
            : severity;
        if (severity === "error") {
            currentGroupIssue.level = "error";
        }
        currentGroupIssue.messages.push(message);
        groupIssues[groupKey] = currentGroupIssue;
    }

    function findGroup(stageId, groupId) {
        for (var stageIndex = 0; stageIndex < stageList.length; ++stageIndex) {
            var stage = stageList[stageIndex] || {};
            if (String(stage.stageId || "") !== stageId) {
                continue;
            }
            var groups = Array.isArray(stage.groups) ? stage.groups : [];
            for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
                if (String(groups[groupIndex].groupId || "") === groupId) {
                    return groups[groupIndex];
                }
            }
        }
        return null;
    }

    function groupRules(stageId, groupId) {
        var group = findGroup(stageId, groupId);
        return Array.isArray(group && group.rules) ? group.rules : [];
    }

    function countPositiveRules(stageId, groupId) {
        var rules = groupRules(stageId, groupId);
        var count = 0;
        for (var index = 0; index < rules.length; ++index) {
            if (isPositiveEntryRule(rules[index])) {
                count += 1;
            }
        }
        return count;
    }

    for (var stageIndex = 0; stageIndex < stageList.length; ++stageIndex) {
        var stage = stageList[stageIndex] || {};
        var stageId = String(stage.stageId || "").trim().toLowerCase();
        var groups = Array.isArray(stage.groups) ? stage.groups : [];
        for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            var group = groups[groupIndex] || {};
            var groupId = String(group.groupId || "").trim().toLowerCase();
            var groupRole = String(group.role || "").trim().toLowerCase();
            var groupOperator = String(group.operator || "").trim().toLowerCase();
            var rules = Array.isArray(group.rules) ? group.rules : [];

            for (var ruleIndex = 0; ruleIndex < rules.length; ++ruleIndex) {
                var rule = rules[ruleIndex] || {};
                var templateId = String(rule.templateId || rule.template_id || "").trim();
                if (templateId) {
                    templateUsage[templateId] = templateUsage[templateId] || [];
                    templateUsage[templateId].push({ stageId: stageId, groupId: groupId });
                }

                if ((stageId === "eligibility" || stageId === "signal")
                        && (groupRole === "must_pass" || groupRole === "any_pass" || groupRole === "score_boost")
                        && isWatchInvalidationRule(rule)) {
                    pushIssue(
                        "error",
                        stageId,
                        groupId,
                        "watch_rule_in_positive_group",
                        "当前规则组放入了失效/否决类模板，这类规则会把正向入场确认直接拦死，建议移到信号否决组。"
                    );
                }

                if ((stageId === "eligibility" || stageId === "signal")
                        && (groupRole === "must_pass" || groupRole === "any_pass")
                        && isCompositeBlockingEntryTemplate(rule)) {
                    pushIssue(
                        "error",
                        stageId,
                        groupId,
                        "composite_entry_template_in_positive_group",
                        "当前规则组使用的是带内置 block 分支的复合入场模板。它会在核心确认组里先触发失效阻断，建议改用纯候选模板，或把阻断逻辑拆到信号否决组。"
                    );
                }

                if (stageId === "signal" && groupId === "signal_boost" && isWatchInvalidationRule(rule)) {
                    pushIssue(
                        "error",
                        stageId,
                        groupId,
                        "boost_contains_block_rule",
                        "评分增强组不应放阻断类模板，否则会把加分组变成隐性否决组。"
                    );
                }

                if (stageId === "signal" && groupId === "signal_veto" && isPositiveEntryRule(rule)) {
                    pushIssue(
                        "warning",
                        stageId,
                        groupId,
                        "positive_rule_in_veto_group",
                        "信号否决组里出现了正向入场模板，语义相反，容易造成规则含义混乱。"
                    );
                }

                if (isTrendStrategyTypeIndex(strategyTypeIndex)
                        && profile.horizon !== "intraday"
                        && (stageId === "signal" || stageId === "eligibility")
                        && (isIntradayOrSpeculativeRule(rule) || isEventDrivenRule(rule))) {
                    pushIssue(
                        "warning",
                        stageId,
                        groupId,
                        "style_mismatch_rule",
                        "当前策略画像偏日线趋势，但这里混入了日内/事件驱动模板，容易让回测长期零成交或风格漂移。"
                    );
                }
            }

            if ((stageId === "signal" || stageId === "eligibility")
                    && groupRole === "must_pass"
                    && groupOperator === "all") {
                var positiveCount = 0;
                for (var positiveIndex = 0; positiveIndex < rules.length; ++positiveIndex) {
                    if (isPositiveEntryRule(rules[positiveIndex])) {
                        positiveCount += 1;
                    }
                }
                if (positiveCount >= 2) {
                    pushIssue(
                        "error",
                        stageId,
                        groupId,
                        "multiple_positive_rules_in_all_group",
                        "同一必须满足组里放了多条候选入场模板且组合方式是“全部满足”，这会要求多个入场形态同时成立，极易出现长期零成交。"
                    );
                }
            }
        }
    }

    var trendStrategy = isTrendStrategyTypeIndex(strategyTypeIndex);
    if (trendStrategy) {
        var positiveSignalRules = countPositiveRules("signal", "signal_core") + countPositiveRules("eligibility", "eligibility_core");
        if (positiveSignalRules === 0) {
            pushIssue(
                "error",
                "signal",
                "signal_core",
                "missing_positive_entry_rules",
                "趋势策略至少要有一条正向入场主规则，例如 20 日线回踩、60 日线回踩或中期平台突破。"
            );
        }

        if (groupRules("signal", "signal_boost").length === 0) {
            pushIssue(
                "warning",
                "signal",
                "signal_boost",
                "missing_signal_boost",
                "缺少评分增强规则。多个候选同时出现时只能按基础优先级处理，难以稳定区分强弱。"
            );
        }
    }

    if ((trendStrategy || strategyTypeIndex === StrategyTypeIndex.Momentum || strategyTypeIndex === StrategyTypeIndex.EventDriven)
            && groupRules("eligibility", "eligibility_core").length === 0) {
        pushIssue(
            "warning",
            "eligibility",
            "eligibility_core",
            "missing_eligibility_rules",
            "缺少基础过滤规则。当前信号会直接面对全市场候选，容易把流动性、上市时长或风格不匹配的标的一起放进来。"
        );
    }

    if ((trendStrategy || strategyTypeIndex === StrategyTypeIndex.Momentum || strategyTypeIndex === StrategyTypeIndex.EventDriven)
            && groupRules("signal", "signal_veto").length === 0) {
        pushIssue(
            "warning",
            "signal",
            "signal_veto",
            "missing_signal_veto",
            "缺少信号否决规则。入场确认一旦放宽，缺少反例阻断会让低质量候选直接进入交易。"
        );
    }

    if ((trendStrategy || strategyTypeIndex === StrategyTypeIndex.Momentum || strategyTypeIndex === StrategyTypeIndex.EventDriven)
            && countPositiveRules("signal", "signal_core") > 0
            && groupRules("eligibility", "eligibility_core").length === 0
            && groupRules("signal", "signal_veto").length === 0) {
        pushIssue(
            "warning",
            "signal",
            "signal_core",
            "entry_quality_controls_missing",
            "当前配置接近“裸信号直连开仓”：有正向入场模板，但缺少基础过滤和信号否决，放宽阈值后通常会迅速从零成交变成低质量高回撤。"
        );
    }

    if (groupRules("portfolio", "portfolio_budget").length === 0) {
        pushIssue(
            "warning",
            "portfolio",
            "portfolio_budget",
            "missing_portfolio_budget",
            "缺少风险预算规则。单票与组合层级的仓位约束只能依赖运行参数，难以把信号质量和仓位分配联动起来。"
        );
    }

    if (groupRules("market", "market_gate").length === 0) {
        pushIssue("warning", "market", "market_gate", "missing_market_gate", "缺少市场放行规则，策略会失去市场环境总开关。");
    }
    if (groupRules("market", "market_veto").length === 0) {
        pushIssue("warning", "market", "market_veto", "missing_market_veto", "缺少市场否决规则，弱市里可能继续尝试开仓。");
    }
    if (groupRules("rebalance", "rebalance_exit").length === 0) {
        pushIssue("warning", "rebalance", "rebalance_exit", "missing_exit_rules", "缺少退出触发规则，持仓保护会偏弱。");
    }
    if (groupRules("rebalance", "rebalance_scale").length === 0) {
        pushIssue("warning", "rebalance", "rebalance_scale", "missing_scale_rules", "缺少分批管理规则，止盈和减仓节奏可能不完整。");
    }

    for (var templateId in templateUsage) {
        var usages = templateUsage[templateId] || [];
        if (usages.length < 2) {
            continue;
        }
        var usageMap = {};
        for (var usageIndex = 0; usageIndex < usages.length; ++usageIndex) {
            usageMap[buildRuleComposerIssueKey(usages[usageIndex].stageId, usages[usageIndex].groupId)] = true;
        }
        var uniqueGroups = Object.keys(usageMap);
        if (uniqueGroups.length < 2) {
            continue;
        }
        for (var uniqueIndex = 0; uniqueIndex < usages.length; ++uniqueIndex) {
            pushIssue(
                "warning",
                usages[uniqueIndex].stageId,
                usages[uniqueIndex].groupId,
                "duplicate_template_across_groups",
                "同一模板被重复放进多个规则组，容易出现重复阻断、重复计分或语义冲突。"
            );
        }
    }

    if (errors.length === 0 && warnings.length === 0) {
        suggestions.push("当前规则组合未发现明显冲突，可以继续补充个性化规则。");
    } else if (errors.length === 0) {
        suggestions.push("当前以提醒为主，建议优先处理黄色提示，避免回测进入长期零成交状态。");
    } else {
        suggestions.push("当前存在会明显影响成交或语义的阻断问题，建议先修复后再创建或回测。");
    }

    return {
        valid: errors.length === 0,
        errorCount: errors.length,
        warningCount: warnings.length,
        errors: errors,
        warnings: warnings,
        suggestions: suggestions,
        groupIssues: groupIssues,
        summaryText: errors.length > 0
            ? ("发现 " + errors.length + " 个阻断问题，" + warnings.length + " 个提醒")
            : (warnings.length > 0 ? ("发现 " + warnings.length + " 个组合提醒") : "规则组合健康")
    };
}

// ============ 工具函数 ============

// 重置表单数据
function resetFormData() {
    return {
        strategyName: "",
        strategyDescription: "",
        selectedStrategyTypeIndex: 0,
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
    StrategyTypeIndex: StrategyTypeIndex,
    StrategyBehaviorKind: StrategyBehaviorKind,
    StrategyStoredTypeIndex: StrategyStoredTypeIndex,
    normalizeStrategyTypeIndex: normalizeStrategyTypeIndex,
    strategyTypeIndexFromBehaviorKind: strategyTypeIndexFromBehaviorKind,
    strategyBehaviorKindFromTypeIndex: strategyBehaviorKindFromTypeIndex,
    strategyBehaviorKindLabel: strategyBehaviorKindLabel,
    strategyStoredTypeIndexFromBehaviorKind: strategyStoredTypeIndexFromBehaviorKind,
    strategyStoredTypeIndexFromTypeIndex: strategyStoredTypeIndexFromTypeIndex,
    getStrategyTypeNameFromIndex: getStrategyTypeNameFromIndex,
    getStrategyTypeDescriptionFromIndex: getStrategyTypeDescriptionFromIndex,
    getStrategyIconFromIndex: getStrategyIconFromIndex,
    getBriefDescriptionFromIndex: getBriefDescriptionFromIndex,
    getDefaultStrategyDescription: getDefaultStrategyDescription,
    getDefaultStrategyTags: getDefaultStrategyTags,
    
    // 风险等级相关
    AssetTypeIndex: AssetTypeIndex,
    TimeFrameIndex: TimeFrameIndex,
    RiskLevelIndex: RiskLevelIndex,
    getAssetTypeNameFromIndex: getAssetTypeNameFromIndex,
    getTimeFrameNameFromIndex: getTimeFrameNameFromIndex,
    getRiskLevelName: getRiskLevelName,
    getRiskLevelNameFromIndex: getRiskLevelNameFromIndex,
    getRiskLevelColor: getRiskLevelColor,
    getRiskLevelColorFromIndex: getRiskLevelColorFromIndex,
    
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

    // 规则编排骨架
    buildDefaultStrategyProfile: buildDefaultStrategyProfile,
    getRuleComposerStageDefinitions: getRuleComposerStageDefinitions,
    resolveRuleTemplateFileName: resolveRuleTemplateFileName,
    buildDefaultBaseRuleBindings: buildDefaultBaseRuleBindings,
    buildDefaultRuleComposerSkeleton: buildDefaultRuleComposerSkeleton,
    validateRuleComposerConfiguration: validateRuleComposerConfiguration,
    
    // 工具函数
    resetFormData: resetFormData
};
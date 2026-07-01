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

var StrategyTypeIndex = {
    Invalid: -1,
    DoubleMovingAverage: 0,
    TurtleBreakout: 1,
    BollingerBandMeanReversion: 2,
    RsiMeanReversion: 3,
    MultiFactorSelection: 4,
    EarningsSurprise: 5,
    StatisticalPairTrading: 6,
    RiskParityAllocation: 7,
    MachineLearningSelection: 8,
    OrderFlowImbalance: 9,
    VolatilitySpread: 10,
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
        case StrategyTypeIndex.DoubleMovingAverage:
        case StrategyTypeIndex.TurtleBreakout:
        case StrategyTypeIndex.BollingerBandMeanReversion:
        case StrategyTypeIndex.RsiMeanReversion:
        case StrategyTypeIndex.MultiFactorSelection:
        case StrategyTypeIndex.EarningsSurprise:
        case StrategyTypeIndex.StatisticalPairTrading:
        case StrategyTypeIndex.RiskParityAllocation:
        case StrategyTypeIndex.MachineLearningSelection:
        case StrategyTypeIndex.OrderFlowImbalance:
        case StrategyTypeIndex.VolatilitySpread:
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
function getStrategyTypeName(strategyTypeIndex) {
    var normalizedTypeIndex = normalizeStrategyTypeIndex(strategyTypeIndex)
    var typeId = strategyTypeIdFromIndex(normalizedTypeIndex)
    if (!typeId) {
        return "";
    }
    return tr('strategyCreation.strategyTypes.' + typeId) || typeId;
}

function getStrategyTypeNameFromIndex(strategyTypeIndex) {
    return getStrategyTypeName(strategyTypeIndex);
}

// 获取策略类型描述
function getStrategyTypeDescription(strategyTypeIndex) {
    var normalizedTypeIndex = normalizeStrategyTypeIndex(strategyTypeIndex)
    var typeId = strategyTypeIdFromIndex(normalizedTypeIndex)
    if (!typeId) {
        return "";
    }
    return tr('strategyCreation.strategyTypeDescriptions.' + typeId) || tr('strategyCreation.strategyTypeDescriptions.custom');
}

function getStrategyTypeDescriptionFromIndex(strategyTypeIndex) {
    return getStrategyTypeDescription(strategyTypeIndex);
}

// 获取策略类型图标
function getStrategyIcon(strategyTypeIndex) {
    switch (normalizeStrategyTypeIndex(strategyTypeIndex)) {
        case StrategyTypeIndex.TrendFollowing: return "📈";
        case StrategyTypeIndex.TrendBreakout: return "🎯";
        case StrategyTypeIndex.MeanReversion: return "🔄";
        case StrategyTypeIndex.Momentum: return "🚀";
        case StrategyTypeIndex.Arbitrage: return "⚖️";
        case StrategyTypeIndex.MachineLearning: return "🤖";
        case StrategyTypeIndex.MultiFactor: return "🧩";
        case StrategyTypeIndex.HighFrequency: return "⚡";
        case StrategyTypeIndex.EventDriven: return "📰";
        case StrategyTypeIndex.Custom: return "🛠️";
        default: return "📊";
    }
}

function getStrategyIconFromIndex(strategyTypeIndex) {
    return getStrategyIcon(strategyTypeIndex);
}

// 获取策略类型简要描述
function getBriefDescription(strategyTypeIndex) {
    switch (normalizeStrategyTypeIndex(strategyTypeIndex)) {
        case StrategyTypeIndex.TrendFollowing: return "跟随价格趋势交易";
        case StrategyTypeIndex.TrendBreakout: return "突破近高并沿趋势持仓";
        case StrategyTypeIndex.MeanReversion: return "价格偏离均值后回归";
        case StrategyTypeIndex.Momentum: return "跟随强势股票动量";
        case StrategyTypeIndex.Arbitrage: return "利用价差套利交易";
        case StrategyTypeIndex.MachineLearning: return "AI预测价格走势";
        case StrategyTypeIndex.MultiFactor: return "多维度综合评分";
        case StrategyTypeIndex.HighFrequency: return "高频数据快速交易";
        case StrategyTypeIndex.EventDriven: return "事件驱动交易机会";
        case StrategyTypeIndex.Custom: return "用户自定义策略";
        default: return "策略类型";
    }
}

function getBriefDescriptionFromIndex(strategyTypeIndex) {
    return getBriefDescription(strategyTypeIndex);
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
    var normalizedParameters = normalizeStrategyParameters(selectedStrategyTypeIndex, context.strategyParameters);
    
    var strategyData = {
        // 基本信息
        name: context.strategyName,
        displayName: context.strategyName,
        strategyBehaviorKind: strategyBehaviorKind,
        strategyTypeIndex: selectedStrategyTypeIndex,
        typeName: getStrategyTypeName(selectedStrategyTypeIndex),
        description: context.strategyDescription,
        
        // 基本属性
        assetTypeIndex: context.assetTypeIndex,
        timeFrameIndex: context.timeFrameIndex,
        riskLevelIndex: context.riskLevelIndex,
        optimizationMethod: context.optimizationMethod,
        
        // 高级选项
        enableAdvancedOptions: context.enableAdvancedOptions,
        
        // 元数据
        statusIndex: false,
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
    // 深拷贝剥离 QML property var 造成的 QVariant 包装，
    // 确保嵌套的 rule_profile/rule_composer_state 到 C++ 侧能正确识别为 QVariantMap
    try { source = JSON.parse(JSON.stringify(source)) } catch (e) {}
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

    assignIfPresent("allowShort", ["allowShort"], Boolean)
    assignIfPresent("maxPositions", ["maxPositions"], Number)
    assignIfPresent("maxWeightPerStock", ["maxWeightPerStock"], Number)
    assignIfPresent("minWeightPerStock", ["minWeightPerStock"], Number)
    assignIfPresent("weightScheme", ["weightScheme"], Number)
    assignIfPresent("rebalanceFrequency", ["rebalanceFrequency"], Number)

    if (normalizedStrategyTypeIndex === StrategyTypeIndex.DoubleMovingAverage) {
        assignIfPresent("fastPeriod", ["fastPeriod"], Number)
        assignIfPresent("slowPeriod", ["slowPeriod"], Number)
        assignIfPresent("priceField", ["priceField"])
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.TurtleBreakout) {
        assignIfPresent("channelPeriod", ["channelPeriod"], Number)
        assignIfPresent("breakoutMultiplier", ["breakoutMultiplier"], Number)
        assignIfPresent("atrPeriod", ["atrPeriod"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.BollingerBandMeanReversion) {
        assignIfPresent("period", ["period"], Number)
        assignIfPresent("standardDeviationMultiplier", ["standardDeviationMultiplier"], Number)
        assignIfPresent("entryThreshold", ["entryThreshold"], Number)
        assignIfPresent("exitThreshold", ["exitThreshold"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.RsiMeanReversion) {
        assignIfPresent("period", ["period"], Number)
        assignIfPresent("oversoldLevel", ["oversoldLevel"], Number)
        assignIfPresent("overboughtLevel", ["overboughtLevel"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MultiFactorSelection) {
        assignIfPresent("factorWeights", ["factorWeights"])
        assignIfPresent("topN", ["topN"], Number)
        assignIfPresent("industryNeutral", ["industryNeutral"], Boolean)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.EarningsSurprise) {
        assignIfPresent("surpriseThreshold", ["surpriseThreshold"], Number)
        assignIfPresent("holdDays", ["holdDays"], Number)
        assignIfPresent("eventSources", ["eventSources"])
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.StatisticalPairTrading) {
        assignIfPresent("tradingPair", ["tradingPair"])
        assignIfPresent("hedgeRatio", ["hedgeRatio"], Number)
        assignIfPresent("lookback", ["lookback"], Number)
        assignIfPresent("entryZScore", ["entryZScore"], Number)
        assignIfPresent("exitZScore", ["exitZScore"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.RiskParityAllocation) {
        assignIfPresent("assets", ["assets"])
        assignIfPresent("volatilityLookback", ["volatilityLookback"], Number)
        assignIfPresent("targetVolatility", ["targetVolatility"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MachineLearningSelection) {
        assignIfPresent("modelId", ["modelId"], Number)
        assignIfPresent("featureIds", ["featureIds"])
        assignIfPresent("topN", ["topN"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.OrderFlowImbalance) {
        assignIfPresent("depthLevels", ["depthLevels"], Number)
        assignIfPresent("imbalanceThreshold", ["imbalanceThreshold"], Number)
        assignIfPresent("maxHoldSeconds", ["maxHoldSeconds"], Number)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.VolatilitySpread) {
        assignIfPresent("underlying", ["underlying"])
        assignIfPresent("optionChainFilter", ["optionChainFilter"])
        assignIfPresent("historicalVolatilityWindow", ["historicalVolatilityWindow"], Number)
        assignIfPresent("entrySpreadUpper", ["entrySpreadUpper"], Number)
        assignIfPresent("entrySpreadLower", ["entrySpreadLower"], Number)
        assignIfPresent("deltaNeutral", ["deltaNeutral"], Boolean)
    } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.Custom) {
        assignIfPresent("customCode", ["customCode"])
    }

    assignIfPresent("rule_profile", ["rule_profile"])
    assignIfPresent("rule_composer_state", ["rule_composer_state"])
    assignIfPresent("execution_policy", ["execution_policy"])
    assignIfPresent("backtest_assumptions", ["backtest_assumptions"])
    assignIfPresent("strategy_scope_context", ["strategy_scope_context"])
    assignIfPresent("factor_overlay", ["factor_overlay"])

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

        configs.push({
            id: "allowShort",
            type: "select",
            label: "允许做空",
            description: "是否允许空头仓位",
            options: [false, true],
            default: false,
            category: tr('strategyCreation.commonParameters')
        });
        configs.push({
            id: "maxPositions",
            type: "slider",
            label: "最大持仓数",
            description: "组合允许的最大持仓标的数",
            default: 100,
            min: 1,
            max: 500,
            step: 1,
            unit: "只",
            category: tr('strategyCreation.commonParameters')
        });
        configs.push({
            id: "maxWeightPerStock",
            type: "slider",
            label: "单票最大权重",
            description: "每个标的最大仓位权重",
            default: 0.1,
            min: 0.01,
            max: 1.0,
            step: 0.01,
            decimals: 2,
            unit: "",
            category: tr('strategyCreation.commonParameters')
        });
        configs.push({
            id: "minWeightPerStock",
            type: "slider",
            label: "单票最小权重",
            description: "每个标的最小仓位权重",
            default: 0.0,
            min: 0.0,
            max: 0.5,
            step: 0.01,
            decimals: 2,
            unit: "",
            category: tr('strategyCreation.commonParameters')
        });
        configs.push({
            id: "weightScheme",
            type: "select",
            label: "权重方案",
            description: "仓位权重分配方案",
            options: [0, 1, 2, 3],
            default: 0,
            category: tr('strategyCreation.commonParameters')
        });
        configs.push({
            id: "rebalanceFrequency",
            type: "select",
            label: "调仓频率",
            description: "策略调仓频率枚举",
            options: [0, 1, 2, 3, 4],
            default: 0,
            category: tr('strategyCreation.commonParameters')
        });

        if (normalizedStrategyTypeIndex === StrategyTypeIndex.Common) {
            return configs;
        }

        if (normalizedStrategyTypeIndex === StrategyTypeIndex.DoubleMovingAverage) {
            configs.push({id: "fastPeriod", type: "slider", label: "快线周期", default: 5, min: 2, max: 100, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "slowPeriod", type: "slider", label: "慢线周期", default: 20, min: 5, max: 250, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "priceField", type: "select", label: "价格字段", options: [0, 1, 2, 3, 4], default: 3, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.TurtleBreakout) {
            configs.push({id: "channelPeriod", type: "slider", label: "通道周期", default: 20, min: 5, max: 250, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "breakoutMultiplier", type: "slider", label: "突破倍数", default: 1.0, min: 0.1, max: 5.0, step: 0.1, decimals: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "atrPeriod", type: "slider", label: "ATR 周期", default: 20, min: 5, max: 100, step: 1, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.BollingerBandMeanReversion) {
            configs.push({id: "period", type: "slider", label: "周期", default: 20, min: 5, max: 200, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "standardDeviationMultiplier", type: "slider", label: "标准差倍数", default: 2.0, min: 0.5, max: 5.0, step: 0.1, decimals: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "entryThreshold", type: "slider", label: "入场阈值", default: 1.0, min: 0.1, max: 5.0, step: 0.1, decimals: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "exitThreshold", type: "slider", label: "离场阈值", default: 0.2, min: 0.05, max: 3.0, step: 0.05, decimals: 2, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.RsiMeanReversion) {
            configs.push({id: "period", type: "slider", label: "RSI 周期", default: 14, min: 5, max: 100, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "oversoldLevel", type: "slider", label: "超卖阈值", default: 30, min: 1, max: 50, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "overboughtLevel", type: "slider", label: "超买阈值", default: 70, min: 50, max: 99, step: 1, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MultiFactorSelection
                   || normalizedStrategyTypeIndex === StrategyTypeIndex.MultiFactor) {
            configs.push({id: "factorWeights", type: "input", label: "因子权重", placeholder: "[{\"factorId\":\"f1\",\"weight\":0.2}]", category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "topN", type: "slider", label: "TopN", default: 50, min: 1, max: 500, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "industryNeutral", type: "select", label: "行业中性", options: [false, true], default: false, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.EarningsSurprise) {
            configs.push({id: "surpriseThreshold", type: "slider", label: "惊喜阈值", default: 0.2, min: 0.01, max: 2.0, step: 0.01, decimals: 2, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "holdDays", type: "slider", label: "持有天数", default: 5, min: 1, max: 120, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "eventSources", type: "input", label: "事件源", placeholder: "[0,1]", category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.StatisticalPairTrading) {
            configs.push({id: "tradingPair", type: "input", label: "交易对", placeholder: "{\"first\":\"000001.SZ\",\"second\":\"000002.SZ\"}", category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "hedgeRatio", type: "slider", label: "对冲比", default: 1.0, min: 0.1, max: 5.0, step: 0.1, decimals: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "lookback", type: "slider", label: "回看窗口", default: 20, min: 5, max: 250, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "entryZScore", type: "slider", label: "入场Z分数", default: 2.0, min: 0.5, max: 5.0, step: 0.1, decimals: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "exitZScore", type: "slider", label: "离场Z分数", default: 0.5, min: 0.1, max: 3.0, step: 0.1, decimals: 1, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.RiskParityAllocation) {
            configs.push({id: "assets", type: "input", label: "资产列表", placeholder: "[\"000300.SH\",\"000905.SH\"]", category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "volatilityLookback", type: "slider", label: "波动率回看", default: 60, min: 5, max: 500, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "targetVolatility", type: "slider", label: "目标波动率", default: 0.0, min: 0.0, max: 1.0, step: 0.01, decimals: 2, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.MachineLearningSelection) {
            configs.push({id: "modelId", type: "input", label: "模型ID", default: 0, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "featureIds", type: "input", label: "特征ID列表", placeholder: "[1,2,3]", category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "topN", type: "slider", label: "TopN", default: 50, min: 1, max: 500, step: 1, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.OrderFlowImbalance) {
            configs.push({id: "depthLevels", type: "slider", label: "盘口层级", default: 5, min: 1, max: 20, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "imbalanceThreshold", type: "slider", label: "失衡阈值", default: 0.3, min: 0.01, max: 1.0, step: 0.01, decimals: 2, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "maxHoldSeconds", type: "slider", label: "最大持有秒数", default: 60, min: 1, max: 3600, step: 1, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.VolatilitySpread) {
            configs.push({id: "underlying", type: "input", label: "标的", placeholder: "510300.SH", category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "optionChainFilter", type: "input", label: "期权链过滤", placeholder: "{\"expiryDaysMin\":15}", category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "historicalVolatilityWindow", type: "slider", label: "历史波动率窗口", default: 20, min: 5, max: 250, step: 1, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "entrySpreadUpper", type: "slider", label: "入场上阈", default: 0.05, min: 0.0, max: 1.0, step: 0.01, decimals: 2, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "entrySpreadLower", type: "slider", label: "入场下阈", default: -0.05, min: -1.0, max: 0.0, step: 0.01, decimals: 2, category: tr('strategyCreation.personalizedParameters')});
            configs.push({id: "deltaNeutral", type: "select", label: "Delta 中性", options: [false, true], default: true, category: tr('strategyCreation.personalizedParameters')});
        } else if (normalizedStrategyTypeIndex === StrategyTypeIndex.Custom) {
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
    var stageId = String((binding && binding.stageId) || "").trim().toLowerCase();
    if (stageId === "") {
        throw new Error("规则绑定缺少 stageId")
    }
    return canonicalRulePackStageId(stageId);
}

function injectRecommendedBaseBindings(bindings, recommendedBindings) {
    var existingBindings = Array.isArray(bindings) ? bindings.slice() : [];
    var recommended = Array.isArray(recommendedBindings) ? recommendedBindings : [];
    var existingGroupCounts = {};
    var existingTemplates = {};

    for (var index = 0; index < existingBindings.length; ++index) {
        var binding = existingBindings[index] || {};
        var templateId = String(binding.templateId || "").trim();
        var groupKey = normalizeRuleComposerBindingStage(binding)
            + "::" + String(binding.groupId || "").trim().toLowerCase();
        existingGroupCounts[groupKey] = (existingGroupCounts[groupKey] || 0) + 1;
        if (templateId) {
            existingTemplates[templateId] = true;
        }
    }

    for (var recommendedIndex = 0; recommendedIndex < recommended.length; ++recommendedIndex) {
        var recommendedBinding = recommended[recommendedIndex] || {};
        var recommendedTemplateId = String(recommendedBinding.templateId || "").trim();
        var recommendedGroupKey = normalizeRuleComposerBindingStage(recommendedBinding)
            + "::" + String(recommendedBinding.groupId || "").trim().toLowerCase();
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
        var groupId = String((binding && binding.groupId) || "").trim().toLowerCase();
        var groupRole = String((binding && binding.groupRole) || "").trim().toLowerCase();
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
                templateId: binding.templateId || "",
                templateName: binding.templateDisplayName || binding.templateId || "未命名模板",
                summary: binding.summary || "",
                stageId: bindingStageId,
                fileName: binding.fileName || "",
                filePath: binding.filePath || "",
                category: binding.category || "",
                termId: binding.termId || "",
                termName: binding.termDisplayName || "",
                defaultInjected: !!binding.defaultInjected,
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
        rule && rule.templateId,
        rule && (rule.category || ""),
        rule && rule.termId,
        rule && rule.templateName
    ].join(" ").toLowerCase();
}

function isWatchInvalidationRule(rule) {
    var text = ruleLikeTemplateText(rule);
    return text.indexOf("watch_invalidation") >= 0
        || text.indexOf("invalidated") >= 0
        || text.indexOf("template_watch_") >= 0;
}

function isCompositeBlockingEntryTemplate(rule) {
    var templateId = String(rule && rule.templateId || "").trim();
    var fileName = String(rule && rule.fileName || "").trim().toLowerCase();
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
                var templateId = String(rule.templateId || "").trim();
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
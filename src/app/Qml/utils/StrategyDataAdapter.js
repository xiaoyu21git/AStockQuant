// StrategyDataAdapter.js
// 策略数据映射适配器，将策略数据转换为统一的卡片数据格式

/**
 * 策略数据适配器
 * 提供策略数据到统一卡片数据格式的转换功能
 */

// 颜色映射配置
var COLOR_MAP = {
    // 策略类型颜色
    "趋势策略": "#3B82F6",     // 蓝色
    "动量策略": "#3B82F6",     // 蓝色
    "价值策略": "#F59E0B",     // 橙色
    "均值回归": "#F59E0B",     // 橙色
    "质量策略": "#10B981",     // 绿色
    "基本面策略": "#10B981",   // 绿色
    "成长策略": "#8B5CF6",     // 紫色
    "高增长策略": "#8B5CF6",   // 紫色
    "情绪策略": "#EC4899",     // 粉色
    "市场情绪策略": "#EC4899", // 粉色
    "波动策略": "#EF4444",     // 红色
    "套利策略": "#EF4444",     // 红色
    "组合策略": "#06B6D4",     // 青色
    "多因子策略": "#06B6D4",   // 青色
    
    // 状态颜色
    "ACTIVE": "#10B981",     // 活跃 - 绿色
    "EXPERIMENTAL": "#F59E0B", // 实验 - 橙色
    "DEPRECATED": "#EF4444",  // 废弃 - 红色
    "PENDING": "#3B82F6",     // 待审核 - 蓝色
    "RUNNING": "#10B981",     // 运行中 - 绿色
    "STOPPED": "#EF4444",     // 已停止 - 红色
    "PAUSED": "#F59E0B"       // 已暂停 - 橙色
};

// 图标映射
var ICON_MAP = {
    "趋势策略": "📊",
    "动量策略": "📊",
    "价值策略": "💰", 
    "均值回归": "💰",
    "质量策略": "📈",
    "基本面策略": "📈",
    "成长策略": "🚀",
    "高增长策略": "🚀",
    "情绪策略": "🧠",
    "市场情绪策略": "🧠",
    "波动策略": "📉",
    "套利策略": "📉",
    "组合策略": "🎯",
    "多因子策略": "🎯"
};

function normalizeStrategyStatus(status) {
    return status ? status.toString().trim().toUpperCase() : "";
}

function strategyStatusFromIndex(statusIndex) {
    switch (Number(statusIndex)) {
    case 0:
        return "DRAFT";
    case 1:
        return "ACTIVE";
    case 2:
        return "INACTIVE";
    case 3:
        return "TESTING";
    case 4:
        return "ARCHIVED";
    case 5:
        return "RUNNING";
    case 6:
        return "PAUSED";
    case 7:
        return "STOPPED";
    default:
        return "";
    }
}

function resolveStrategyBusinessStatus(strategy) {
    if (!strategy) {
        return "";
    }

    return strategyStatusFromIndex(strategy.statusIndex)
}

function resolveStrategyIdentifier(strategy) {
    if (!strategy) {
        return "";
    }

    return strategy.strategyId || strategy.strategy_id || strategy.id || "";
}

function resolveBoundStrategyIdentifier(tradingConfiguration) {
    var identifiers = resolveBoundStrategyIdentifiers(tradingConfiguration)
    return identifiers.length > 0 ? identifiers[0] : ""
}

function resolveBoundStrategyIdentifiers(tradingConfiguration) {
    if (!tradingConfiguration) {
        return [];
    }

    var identifiers = []
    var appendIdentifier = function(value) {
        var identifier = String(value || "").trim()
        if (!identifier || identifiers.indexOf(identifier) !== -1) {
            return
        }
        identifiers.push(identifier)
    }

    var bindings = tradingConfiguration.boundStrategies || []
    if (Array.isArray(bindings)) {
        for (var index = 0; index < bindings.length; ++index) {
            var entry = bindings[index]
            if (!entry) {
                continue
            }
            if (typeof entry === "string") {
                appendIdentifier(entry)
                continue
            }
            appendIdentifier(entry.strategyId || entry.strategy_id || entry.id)
        }
    }

    appendIdentifier(tradingConfiguration.boundStrategyId || tradingConfiguration.strategyId)
    return identifiers;
}

function isStrategyBoundToTradingConfiguration(strategy, tradingConfiguration) {
    var strategyId = resolveStrategyIdentifier(strategy)
    if (!strategyId) {
        return false
    }

    return resolveBoundStrategyIdentifiers(tradingConfiguration).indexOf(strategyId) !== -1
}

function isChinaTradingSessionOpen(nowDate) {
    var now = nowDate || new Date();
    var dayOfWeek = now.getDay();
    if (dayOfWeek === 0 || dayOfWeek === 6) {
        return false;
    }

    var totalMinutes = now.getHours() * 60 + now.getMinutes();
    var morningOpen = 9 * 60 + 15;
    var morningClose = 11 * 60 + 30;
    var afternoonOpen = 13 * 60;
    var afternoonClose = 15 * 60;
    return (totalMinutes >= morningOpen && totalMinutes < morningClose)
        || (totalMinutes >= afternoonOpen && totalMinutes < afternoonClose);
}

function isTradingConfigurationEnabled(tradingConfiguration) {
    return !!(tradingConfiguration && tradingConfiguration.enabled) && !tradingConfiguration.readOnly;
}

function hasMarketCalendarSnapshot(marketCalendarSnapshot) {
    return !!(marketCalendarSnapshot && (marketCalendarSnapshot.source || marketCalendarSnapshot.sessionPhase || marketCalendarSnapshot.calendarDate));
}

function isMarketCalendarSessionOpen(marketCalendarSnapshot) {
    return !!(hasMarketCalendarSnapshot(marketCalendarSnapshot) && marketCalendarSnapshot.sessionOpen);
}

function resolveRuntimeSnapshotStatus(runtimeSnapshot) {
    var runtimeState = normalizeStrategyStatus(runtimeSnapshot && runtimeSnapshot.state);
    if (!runtimeState) {
        return "";
    }

    if (runtimeState === "RUNNING") {
        return "RUNNING";
    }
    if (runtimeState === "STARTING" || runtimeState === "INITIALIZED" || runtimeState === "CREATED") {
        return "STARTING";
    }
    if (runtimeState === "STOPPING") {
        return "STOPPING";
    }
    if (runtimeState === "ERROR" || (runtimeSnapshot && runtimeSnapshot.hasError)) {
        return "ERROR";
    }
    if (runtimeState === "STOPPED") {
        return "STOPPED";
    }

    return "";
}

function resolveStrategyRuntimeStatus(strategy, tradingConfiguration, runtimeSnapshot, marketCalendarSnapshot, nowDate) {
    var businessStatus = resolveStrategyBusinessStatus(strategy);
    if (!businessStatus) {
        return "STOPPED";
    }

    if (businessStatus === "INACTIVE") {
        return "STOPPED";
    }
    if (businessStatus === "ARCHIVED") {
        return "DEPRECATED";
    }
    if (businessStatus === "RUNNING" || businessStatus === "PAUSED" || businessStatus === "STOPPED"
        || businessStatus === "DEPRECATED" || businessStatus === "PENDING") {
        return businessStatus;
    }

    var runtimeStatus = resolveRuntimeSnapshotStatus(runtimeSnapshot);
    if (runtimeStatus) {
        return runtimeStatus;
    }

    if (businessStatus === "ACTIVE" || businessStatus === "TESTING") {
        if (!isStrategyBoundToTradingConfiguration(strategy, tradingConfiguration)) {
            return "STOPPED";
        }

        if (!isTradingConfigurationEnabled(tradingConfiguration)) {
            return "STOPPED";
        }

        if (hasMarketCalendarSnapshot(marketCalendarSnapshot)) {
            return isMarketCalendarSessionOpen(marketCalendarSnapshot) ? "RUNNING" : "WAIT_OPEN";
        }
        return isChinaTradingSessionOpen(nowDate) ? "RUNNING" : "WAIT_OPEN";
    }

    return businessStatus;
}

function isRunningDisplayStatus(status) {
    return normalizeStrategyStatus(status) === "RUNNING";
}

/**
 * 将策略数据映射到卡片数据格式
 * @param {Object} strategy - 原始策略数据
 * @returns {Object} 转换后的卡片数据
 */
function mapStrategyToCardData(strategy) {
    if (!strategy) {
        return getDefaultStrategyCardData();
    }
    
    return {
        // 实体基本信息
        entityId: strategy.strategyId || strategy.id || "",
        entityType: "strategy",
        displayName: strategy.displayName || strategy.strategyName || strategy.name || "未命名策略",
        strategyId: strategy.strategyId || strategy.id || "",
        strategyName: strategy.strategyName || strategy.name || "",
        
        // 类别信息
        category: strategy.strategyType || "趋势策略",
        subCategory: strategy.subType || "策略",
        categoryColor: getStrategyTypeColor(strategy.strategyType || "趋势策略"),
        
        // 描述信息
        description: strategy.description || "暂无描述",
        creator: strategy.creator || "系统",
        createDate: strategy.createDate || new Date().toISOString().split('T')[0],
        
        // 状态和标签
        status: resolveStrategyBusinessStatus(strategy) || (strategy.isRunning ? "RUNNING" : "STOPPED"),
        isFavorite: !!strategy.isFavorite,
        isRecommended: !!strategy.isRecommended,
        tags: strategy.tags || getDefaultTags(strategy.strategyType),
        
        // 性能指标
        returns: strategy.returns || strategy.totalReturn || 0.0,
        sharpeRatio: strategy.sharpeRatio || 0.0,
        maxDrawdown: strategy.maxDrawdown || 0.0,
        winRate: strategy.winRate || 0.0,
        
        // 实时状态
        runningDays: strategy.runningDays || 0,
        tradesCount: strategy.tradesCount || 0,
        dailyPnL: strategy.dailyPnL || 0,
        position: strategy.position || 0,
        
        // 控制参数
        controlParameters: strategy.parameters || getDefaultControlParameters(),
        
        // 图表数据
        chartData: strategy.chartData || [],
        showMiniChart: true,
        showGroupReturns: false,
        
        // 控制面板设置
        showParameterPanel: false
    };
}

/**
 * 批量转换策略数据列表
 * @param {Array} strategyList - 策略数据列表
 * @returns {Array} 转换后的卡片数据列表
 */
function mapStrategyListToCardData(strategyList) {
    if (!strategyList || !Array.isArray(strategyList)) {
        return [];
    }
    
    return strategyList.map(mapStrategyToCardData);
}

/**
 * 获取策略类型颜色
 * @param {string} strategyType - 策略类型
 * @returns {string} 颜色值
 */
function getStrategyTypeColor(strategyType) {
    return COLOR_MAP[strategyType] || COLOR_MAP["趋势策略"];
}

/**
 * 获取状态颜色
 * @param {string} status - 状态值
 * @returns {string} 颜色值
 */
function getStatusColor(status) {
    return COLOR_MAP[status] || COLOR_MAP["ACTIVE"];
}

/**
 * 获取类别图标
 * @param {string} category - 类别
 * @returns {string} 图标字符
 */
function getCategoryIcon(category) {
    return ICON_MAP[category] || "📊";
}

/**
 * 获取默认标签
 * @param {string} strategyType - 策略类型
 * @returns {Array} 默认标签数组
 */
function getDefaultTags(strategyType) {
    var tagMap = {
        "趋势策略": ["趋势", "技术分析", "均线"],
        "动量策略": ["动量", "突破", "技术指标"],
        "价值策略": ["价值", "基本面", "估值"],
        "均值回归": ["均值回归", "反转", "振荡"],
        "质量策略": ["质量", "盈利能力", "稳定性"],
        "基本面策略": ["基本面", "财务分析", "价值"],
        "成长策略": ["成长", "增长", "扩张"],
        "高增长策略": ["高增长", "扩张", "发展"],
        "情绪策略": ["情绪", "市场心理", "舆情"],
        "市场情绪策略": ["市场情绪", "心理", "舆论"],
        "波动策略": ["波动", "风险", "套利"],
        "套利策略": ["套利", "对冲", "无风险"],
        "组合策略": ["组合", "多策略", "分散"],
        "多因子策略": ["多因子", "因子组合", "量化"]
    };
    
    return tagMap[strategyType] || ["策略", "量化", "交易"];
}

/**
 * 获取默认控制参数
 * @returns {Array} 默认控制参数数组
 */
function getDefaultControlParameters() {
    return [
        {name: "短期均线周期", value: 20, min: 5, max: 200, unit: "天", color: "blue"},
        {name: "长期均线周期", value: 60, min: 10, max: 500, unit: "天", color: "blue"},
        {name: "止损比例", value: 5, min: 1, max: 20, unit: "%", color: "red"},
        {name: "止盈比例", value: 10, min: 1, max: 30, unit: "%", color: "green"}
    ];
}

/**
 * 获取默认策略卡片数据（用于空状态）
 * @returns {Object} 默认策略卡片数据
 */
function getDefaultStrategyCardData() {
    return {
        entityId: "",
        entityType: "strategy",
        displayName: "示例策略",
        strategyId: "",
        strategyName: "示例策略",
        category: "趋势策略",
        subCategory: "策略",
        categoryColor: COLOR_MAP["趋势策略"],
        description: "这是一个示例策略",
        creator: "系统",
        createDate: new Date().toISOString().split('T')[0],
        status: "STOPPED",
        isFavorite: false,
        isRecommended: false,
        tags: ["趋势", "技术分析", "均线"],
        returns: 15.5,
        sharpeRatio: 1.2,
        maxDrawdown: 8.3,
        winRate: 58.7,
        runningDays: 120,
        tradesCount: 245,
        dailyPnL: 1250,
        position: 50000,
        controlParameters: getDefaultControlParameters(),
        chartData: generateDefaultStrategyChartData(),
        showMiniChart: true,
        showGroupReturns: false,
        showParameterPanel: false
    };
}

/**
 * 生成默认策略图表数据
 * @returns {Array} 图表数据
 */
function generateDefaultStrategyChartData() {
    var data = [];
    for (var i = 0; i < 30; i++) {
        var dayOffset = i - 15;
        var value = 0.0015 * Math.exp(-dayOffset * dayOffset / 100) * (1 + Math.random() * 0.2 - 0.1);
        data.push(value);
    }
    return data;
}

/**
 * 提取卡片性能指标配置
 * @param {Object} cardData - 卡片数据
 * @returns {Array} 性能指标数组
 */
function getStrategyPerformanceMetrics(cardData) {
    return [
        {
            label: "收益率",
            value: cardData.returns || 0,
            format: "%.2f",
            unit: "%",
            color: (cardData.returns || 0) >= 0 ? "#EF4444" : "#10B981",
            tooltip: "累计收益率"
        },
        {
            label: "夏普比率",
            value: cardData.sharpeRatio || 0,
            format: "%.2f",
            color: cardData.categoryColor,
            tooltip: "风险调整后收益"
        },
        {
            label: "最大回撤",
            value: cardData.maxDrawdown || 0,
            format: "%.2f",
            unit: "%",
            color: cardData.categoryColor,
            tooltip: "最大历史亏损"
        },
        {
            label: "胜率",
            value: cardData.winRate || 0,
            format: "%.1f",
            unit: "%",
            color: cardData.categoryColor,
            tooltip: "交易胜率"
        }
    ];
}

/**
 * 提取附加指标配置
 * @param {Object} cardData - 卡片数据
 * @returns {Array} 附加指标数组
 */
function getStrategyAdditionalMetrics(cardData) {
    return [
        {
            label: "运行天数",
            value: cardData.runningDays || 0,
            format: "%d",
            unit: "天",
            color: cardData.categoryColor,
            tooltip: "策略已运行天数"
        },
        {
            label: "交易次数",
            value: cardData.tradesCount || 0,
            format: "%d",
            unit: "次",
            color: cardData.categoryColor,
            tooltip: "累计交易次数"
        },
        {
            label: "今日盈亏",
            value: cardData.dailyPnL || 0,
            format: cardData.dailyPnL >= 0 ? "+$.0f" : "-$0",
            color: (cardData.dailyPnL || 0) >= 0 ? "#EF4444" : "#10B981",
            tooltip: "当日盈亏金额"
        },
        {
            label: "持仓",
            value: cardData.position || 0,
            format: "$%.0f",
            color: cardData.categoryColor,
            tooltip: "当前持仓规模"
        }
    ];
}

/**
 * 根据收益获取趋势方向
 * @param {number} returns - 收益率
 * @returns {string} 趋势方向: "up", "down", "neutral"
 */
function getReturnsTrendDirection(returns) {
    if (returns > 20) return "up";
    if (returns < -5) return "down";
    return "neutral";
}

/**
 * 将策略数据转换为StrategyCard组件的属性对象
 * @param {Object} strategy - 原始策略数据
 * @returns {Object} StrategyCard属性对象
 */
function mapToStrategyCardProps(strategy) {
    var cardData = mapStrategyToCardData(strategy);
    
    return {
        // 策略特有属性
        strategyId: cardData.strategyId,
        strategyName: cardData.strategyName,
        strategyType: cardData.category,
        
        // 基本属性
        entityId: cardData.entityId,
        displayName: cardData.displayName,
        category: cardData.category,
        subCategory: cardData.subCategory,
        description: cardData.description,
        creator: cardData.creator,
        createDate: cardData.createDate,
        status: cardData.status,
        isFavorite: cardData.isFavorite,
        isRecommended: cardData.isRecommended,
        tags: cardData.tags,
        categoryColor: cardData.categoryColor,
        
        // 性能指标
        returns: cardData.returns,
        sharpeRatio: cardData.sharpeRatio,
        maxDrawdown: cardData.maxDrawdown,
        winRate: cardData.winRate,
        
        // 实时状态
        runningDays: cardData.runningDays,
        tradesCount: cardData.tradesCount,
        dailyPnL: cardData.dailyPnL,
        position: cardData.position,
        
        // 控制参数
        controlParameters: cardData.controlParameters,
        
        // 图表数据
        chartData: cardData.chartData,
        showMiniChart: cardData.showMiniChart,
        showGroupReturns: cardData.showGroupReturns,
        
        // 控制面板设置
        showParameterPanel: cardData.showParameterPanel
    };
}

/**
 * 统一卡片数据适配器（同时支持因子和策略）
 * @param {Object} entityData - 实体数据（因子或策略）
 * @returns {Object} 统一卡片数据
 */
function mapToUniversalCardData(entityData) {
    if (!entityData) {
        return null;
    }
    
    // 判断实体类型
    var entityType = entityData.entityType || 
                    (entityData.factorId || entityData.factorName ? "factor" : 
                     entityData.strategyId || entityData.strategyName ? "strategy" : "unknown");
    
    switch (entityType) {
        case "factor":
            // 简化的因子数据处理（避免跨文件依赖）
            return mapSimpleFactorToCardData(entityData);
        case "strategy":
            return mapStrategyToCardData(entityData);
        default:
            // 尝试自动检测
            if (entityData.icValue !== undefined || entityData.irValue !== undefined) {
                return mapSimpleFactorToCardData(entityData);
            } else if (entityData.returns !== undefined || entityData.sharpeRatio !== undefined) {
                return mapStrategyToCardData(entityData);
            }
            return null;
    }
}

/**
 * 简化的因子数据映射（避免依赖FactorDataAdapter.js）
 * @param {Object} factor - 原始因子数据
 * @returns {Object} 转换后的卡片数据
 */
function mapSimpleFactorToCardData(factor) {
    if (!factor) {
        return null;
    }
    
    return {
        entityId: factor.factorId || "",
        entityType: "factor",
        displayName: factor.displayName || factor.factorName || factor.name || "未命名因子",
        factorId: factor.factorId || "",
        factorName: factor.factorName || factor.name || "",
        category: factor.majorCategory || "动量类",
        subCategory: factor.subCategory || "趋势动量",
        categoryColor: COLOR_MAP[factor.majorCategory] || COLOR_MAP["动量类"],
        description: factor.description || "暂无描述",
        creator: factor.creator || "系统",
        createDate: factor.createDate || new Date().toISOString().split('T')[0],
        status: factor.status || "ACTIVE",
        isFavorite: !!factor.isFavorite,
        isRecommended: !!factor.isRecommended,
        tags: factor.tags || ["量化", "因子"],
        icValue: factor.icValue || factor.ic || 0.0,
        irValue: factor.irValue || factor.ir || 0.0,
        validityDays: factor.validityDays || 20,
        turnoverRate: factor.turnoverRate || 32,
        groupReturns: factor.groupReturns || [],
        chartData: factor.groupReturns || [],
        showMiniChart: true,
        showGroupReturns: true
    };
}

// 导出函数
var StrategyDataAdapter = {
    mapStrategyToCardData: mapStrategyToCardData,
    mapStrategyListToCardData: mapStrategyListToCardData,
    getStrategyTypeColor: getStrategyTypeColor,
    getStatusColor: getStatusColor,
    getCategoryIcon: getCategoryIcon,
    getStrategyPerformanceMetrics: getStrategyPerformanceMetrics,
    getStrategyAdditionalMetrics: getStrategyAdditionalMetrics,
    mapToStrategyCardProps: mapToStrategyCardProps,
    mapToUniversalCardData: mapToUniversalCardData
};
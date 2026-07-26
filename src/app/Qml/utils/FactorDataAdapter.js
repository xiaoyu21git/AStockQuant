// FactorDataAdapter.js
// 因子数据映射适配器，将因子数据转换为统一的卡片数据格式

/**
 * 因子数据适配器
 * 提供因子数据到统一卡片数据格式的转换功能
 */

function normalizeCategoryKey(category) {
    var rawCategory = String(category || "").trim()
    var normalized = rawCategory.toLowerCase()

    switch (normalized) {
    case "value":
    case "价值类":
    case "价值因子":
        return "价值因子"
    case "momentum":
    case "动量类":
    case "动量因子":
        return "动量因子"
    case "size":
    case "规模类":
    case "规模因子":
        return "规模因子"
    case "quality":
    case "质量类":
    case "质量因子":
        return "质量因子"
    case "growth":
    case "成长类":
    case "成长因子":
        return "成长因子"
    case "low_volatility":
    case "低波类":
    case "低波因子":
    case "波动类":
        return "低波因子"
    case "dividend":
    case "红利因子":
        return "红利因子"
    case "technical":
    case "技术类":
    case "技术因子":
        return "技术因子"
    case "macro":
    case "宏观因子":
        return "宏观因子"
    case "industry":
    case "行业因子":
        return "行业因子"
    case "liquidity":
    case "流动性类":
    case "流动性因子":
        return "流动性因子"
    case "sentiment":
    case "情绪类":
    case "情绪因子":
        return "情绪因子"
    case "custom":
    case "自定义":
    case "自定义因子":
        return "自定义因子"
    case "reversal":
    case "反转类":
    case "反转因子":
        return "反转因子"
    case "high_freq":
    case "高频类":
    case "高频因子":
        return "高频因子"
    case "dl":
    case "ai":
    case "ai因子":
    case "AI因子":
        return "AI因子"
    default:
        return rawCategory || "动量因子"
    }
}

// 颜色映射配置
var COLOR_MAP = {
    // 因子类别颜色
    "动量因子": "#3B82F6",     // 蓝色
    "价值因子": "#F59E0B",     // 橙色
    "规模因子": "#8B5CF6",     // 紫色
    "质量因子": "#10B981",     // 绿色
    "成长因子": "#8B5CF6",     // 紫色
    "低波因子": "#06B6D4",     // 青色
    "红利因子": "#EC4899",     // 粉色
    "技术因子": "#EF4444",     // 红色
    "流动性因子": "#06B6D4",   // 青色
    "宏观因子": "#F97316", // 橙色
    "行业因子": "#EA580C",   // 深橙色
    "情绪因子": "#EC4899",     // 粉色
    "自定义因子": "#94A3B8",   // 灰色
    "反转因子": "#EF4444",     // 红色
    "高频因子": "#F59E0B",     // 橙色
    "AI因子": "#8B5CF6",       // 紫色

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
    "动量因子": "📊",
    "价值因子": "💰",
    "规模因子": "📐",
    "质量因子": "📈",
    "成长因子": "🚀",
    "低波因子": "📉",
    "红利因子": "🎁",
    "技术因子": "🧮",
    "流动性因子": "💧",
    "宏观因子": "🌦️",
    "行业因子": "🏭",
    "情绪因子": "🧠",
    "自定义因子": "🧩",
    "反转因子": "🔄",
    "高频因子": "⚡",
    "AI因子": "🧠"
};

/**
 * 将因子数据映射到卡片数据格式
 * @param {Object} factor - 原始因子数据
 * @returns {Object} 转换后的卡片数据
 */
function mapFactorToCardData(factor) {
    if (!factor) {
        return getDefaultFactorCardData();
    }
    
    return {
        // 实体基本信息
        entityId: factor.factorId || "",
        entityType: "factor",
        displayName: factor.displayName || factor.factorName || factor.name || "未命名因子",
        factorId: factor.factorId || "",
        factorName: factor.factorName || factor.name || "",
        
        // 类别信息
        category: normalizeCategoryKey(factor.majorCategory),
        subCategory: factor.subCategory || "趋势动量",
        categoryColor: getFactorCategoryColor(factor.majorCategory),
        
        // 描述信息
        description: factor.description || "暂无描述",
        creator: factor.creator || "系统",
        createDate: factor.createDate || new Date().toISOString().split('T')[0],
        
        // 状态和标签
        status: factor.status || "ACTIVE",
        isFavorite: !!factor.isFavorite,
        isRecommended: !!factor.isRecommended,
        tags: factor.tags || getDefaultTags(factor.majorCategory),
        
        // 因子特有性能指标
        icValue: factor.icValue || factor.ic || 0.0,
        irValue: factor.irValue || factor.ir || 0.0,
        validityDays: factor.validityDays || 20,
        turnoverRate: factor.turnoverRate !== undefined && factor.turnoverRate !== null ? factor.turnoverRate : 32,
        
        // 图表数据
        groupReturns: factor.groupReturns || [],
        chartData: factor.groupReturns || [],
        showMiniChart: true,
        showGroupReturns: true
    };
}

/**
 * 批量转换因子数据列表
 * @param {Array} factorList - 因子数据列表
 * @returns {Array} 转换后的卡片数据列表
 */
function mapFactorListToCardData(factorList) {
    if (!factorList || !Array.isArray(factorList)) {
        return [];
    }
    
    return factorList.map(mapFactorToCardData);
}

/**
 * 获取因子类别颜色
 * @param {string} majorCategory - 因子大类
 * @returns {string} 颜色值
 */
function getFactorCategoryColor(majorCategory) {
    return COLOR_MAP[normalizeCategoryKey(majorCategory)] || COLOR_MAP["动量因子"];
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
    return ICON_MAP[normalizeCategoryKey(category)] || "📊";
}

/**
 * 获取默认标签
 * @param {string} majorCategory - 因子大类
 * @returns {Array} 默认标签数组
 */
function getDefaultTags(majorCategory) {
    var normalizedCategory = normalizeCategoryKey(majorCategory)
    var tagMap = {
        "动量因子": ["动量", "技术指标", "趋势"],
        "价值因子": ["价值", "基本面", "估值"],
        "规模因子": ["规模", "市值", "风格"],
        "质量因子": ["质量", "盈利能力", "稳定性"],
        "成长因子": ["成长", "增长", "扩张"],
        "低波因子": ["低波", "风险", "防御"],
        "红利因子": ["红利", "股息", "收益"],
        "技术因子": ["技术", "指标", "信号"],
        "流动性因子": ["流动性", "换手", "交易量"],
        "宏观因子": ["宏观", "周期", "利率"],
        "行业因子": ["行业", "景气度", "轮动"],
        "情绪因子": ["情绪", "市场心理", "舆情"],
        "自定义因子": ["量化", "自定义", "表达式"],
        "反转因子": ["反转", "均值回归", "W式切割"],
        "高频因子": ["高频", "微观结构", "聪明钱"],
        "AI因子": ["AI", "深度学习", "神经网络"]
    };
    
    return tagMap[normalizedCategory] || ["量化", "因子"];
}

/**
 * 获取默认因子卡片数据（用于空状态）
 * @returns {Object} 默认因子卡片数据
 */
function getDefaultFactorCardData() {
    return {
        entityId: "",
        entityType: "factor",
        displayName: "示例因子",
        factorId: "",
        factorName: "示例因子",
        category: "动量因子",
        subCategory: "趋势动量",
        categoryColor: COLOR_MAP["动量因子"],
        description: "这是一个示例因子",
        creator: "系统",
        createDate: new Date().toISOString().split('T')[0],
        status: "ACTIVE",
        isFavorite: false,
        isRecommended: false,
        tags: ["动量", "技术指标", "趋势"],
        icValue: 0.035,
        irValue: 0.8,
        validityDays: 20,
        turnoverRate: 32,
        groupReturns: [0.12, 0.08, 0.05, 0.02, 0.01, -0.01, -0.03, -0.05, -0.08, -0.12],
        chartData: [0.12, 0.08, 0.05, 0.02, 0.01, -0.01, -0.03, -0.05, -0.08, -0.12],
        showMiniChart: true,
        showGroupReturns: true
    };
}

/**
 * 提取卡片性能指标配置
 * @param {Object} cardData - 卡片数据
 * @returns {Array} 性能指标数组
 */
function getFactorPerformanceMetrics(cardData) {
    return [
        {
            label: "IC",
            value: cardData.icValue || 0,
            format: "%.3f",
            color: cardData.categoryColor,
            showTrend: true,
            trendDirection: getTrendDirection(cardData.icValue),
            tooltip: "信息系数，衡量因子预测能力"
        },
        {
            label: "IR",
            value: cardData.irValue || 0,
            format: "%.2f",
            color: cardData.categoryColor,
            tooltip: "信息比率，衡量因子稳定性"
        },
        {
            label: "换手率",
            value: cardData.turnoverRate || 0,
            format: "%.0f",
            unit: "%/年",
            color: cardData.categoryColor,
            tooltip: "年化换手率，衡量交易频率"
        },
        {
            label: "有效期",
            value: cardData.validityDays || 0,
            format: "%d",
            unit: "天",
            color: cardData.categoryColor,
            tooltip: "因子有效持续时间"
        }
    ];
}

/**
 * 根据值获取趋势方向
 * @param {number} value - 值
 * @returns {string} 趋势方向: "up", "down", "neutral"
 */
function getTrendDirection(value) {
    if (value > 0.03) return "up";
    if (value < 0.02) return "down";
    return "neutral";
}

/**
 * 将因子数据转换为FactorCard组件的属性对象
 * @param {Object} factor - 原始因子数据
 * @returns {Object} FactorCard属性对象
 */
function mapToFactorCardProps(factor) {
    var cardData = mapFactorToCardData(factor);
    
    return {
        // 因子特有属性
        factorId: cardData.factorId,
        factorName: cardData.factorName,
        majorCategory: cardData.category,
        subCategory: cardData.subCategory,
        
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
        icValue: cardData.icValue,
        irValue: cardData.irValue,
        validityDays: cardData.validityDays,
        turnoverRate: cardData.turnoverRate,
        
        // 图表数据
        groupReturns: cardData.groupReturns,
        chartData: cardData.chartData,
        showMiniChart: cardData.showMiniChart,
        showGroupReturns: cardData.showGroupReturns
    };
}

// 导出函数
var FactorDataAdapter = {
    mapFactorToCardData: mapFactorToCardData,
    mapFactorListToCardData: mapFactorListToCardData,
    getFactorCategoryColor: getFactorCategoryColor,
    getStatusColor: getStatusColor,
    getCategoryIcon: getCategoryIcon,
    getFactorPerformanceMetrics: getFactorPerformanceMetrics,
    mapToFactorCardProps: mapToFactorCardProps
};
// FactorDataAdapter.js
// 因子数据映射适配器，将因子数据转换为统一的卡片数据格式

/**
 * 因子数据适配器
 * 提供因子数据到统一卡片数据格式的转换功能
 */

// 颜色映射配置
var COLOR_MAP = {
    // 因子类别颜色
    "动量类": "#3B82F6",     // 蓝色
    "价值类": "#F59E0B",     // 橙色
    "质量类": "#10B981",     // 绿色
    "成长类": "#8B5CF6",     // 紫色
    "情绪类": "#EC4899",     // 粉色
    "波动类": "#EF4444",     // 红色
    "流动性类": "#06B6D4",   // 青色
    "预期类": "#F97316",     // 橙色
    "恐慌类": "#8B4513",     // 棕色
    
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
    "动量类": "📊",
    "价值类": "💰", 
    "质量类": "📈",
    "成长类": "🚀",
    "情绪类": "🧠",
    "波动类": "📉",
    "流动性类": "💧",
    "预期类": "🔮",
    "恐慌类": "🛡️"
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
        entityId: factor.factorId || factor.id || "",
        entityType: "factor",
        displayName: factor.displayName || factor.factorName || factor.name || "未命名因子",
        factorId: factor.factorId || factor.id || "",
        factorName: factor.factorName || factor.name || "",
        
        // 类别信息
        category: factor.majorCategory || "动量类",
        subCategory: factor.subCategory || "趋势动量",
        categoryColor: getFactorCategoryColor(factor.majorCategory || "动量类"),
        
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
        turnoverRate: factor.turnoverRate || 32,
        
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
    return COLOR_MAP[majorCategory] || COLOR_MAP["动量类"];
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
 * @param {string} majorCategory - 因子大类
 * @returns {Array} 默认标签数组
 */
function getDefaultTags(majorCategory) {
    var tagMap = {
        "动量类": ["动量", "技术指标", "趋势"],
        "价值类": ["价值", "基本面", "估值"],
        "质量类": ["质量", "盈利能力", "稳定性"],
        "成长类": ["成长", "增长", "扩张"],
        "情绪类": ["情绪", "市场心理", "舆情"],
        "波动类": ["波动", "风险", "套利"],
        "流动性类": ["流动性", "换手", "交易量"],
        "预期类": ["预期", "预测", "预期差"],
        "恐慌类": ["恐慌", "避险", "防御"]
    };
    
    return tagMap[majorCategory] || ["量化", "因子"];
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
        category: "动量类",
        subCategory: "趋势动量",
        categoryColor: COLOR_MAP["动量类"],
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
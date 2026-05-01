// FactorTypeParameterPage.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

/**
 * 因子类型参数配置页面
 * 根据因子类别显示对应的参数配置
 */
Item {
    id: root
    // ============ 公共属性 ============
    property string factorCategory: ""
    
    // 参数值变化信号
    signal parameterChanged(string paramName, var value)
    signal allParametersChanged(var parameters)
    
    // ============ 内部属性 ============
    
    // 当前参数值集合
    property var currentParameters: ({})
    
    // ============ 页面布局 ============
    
    Column {
        anchors.fill: parent
        spacing: 20
        
        // 如果没有选择因子类别
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16
            visible: !factorCategory
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "📋"
                font.pixelSize: 40
                color: "#64748B"
            }
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "未选择因子类别"
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#F1F5F9"
            }
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "请先选择一个因子类别"
                font.pixelSize: 12
                color: "#94A3B8"
            }
        }
        
        // 参数配置区域
        Column {
            width: parent.width
            spacing: 16
            visible: factorCategory
            
            // 类别标题
            Rectangle {
                width: parent.width
                height: 60
                radius: 8
                color: getCategoryColor(factorCategory)
                
                Row {
                    anchors.centerIn: parent
                    spacing: 12
                    
                    Text {
                        text: getCategoryIcon(factorCategory)
                        font.pixelSize: 24
                        color: "white"
                    }
                    
                    Text {
                        text: factorCategory + " 参数配置"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "white"
                    }
                }
            }
            
            // 参数列表
            Column {
                id: parametersColumn
                width: parent.width
                spacing: 20
                
                // 动态加载参数输入组件
            }
        }
    }
    
    // ============ 工具函数 ============

    // 统一类别标识，兼容旧入口、中文展示名和内部类型名
    function normalizeCategory(category) {
        switch (category) {
            case "红利因子":
            case "dividend":
                return "红利因子"
            case "低波因子":
            case "low_volatility":
                return "低波因子"
            default:
                return category
        }
    }
    
    // 获取类别颜色
    function getCategoryColor(category) {
        switch (normalizeCategory(category)) {
            case "动量类": return "#3B82F6"
            case "价值类": return "#F59E0B"
            case "质量类": return "#10B981"
            case "成长类": return "#8B5CF6"
            case "红利因子": return "#EC4899"
            case "情绪类": return "#EC4899"
            default: return "#334155"
        }
    }
    
    // 获取类别图标
    function getCategoryIcon(category) {
        switch (normalizeCategory(category)) {
            case "动量类": return "📊"
            case "价值类": return "💰"
            case "质量类": return "📈"
            case "成长类": return "🚀"
            case "红利因子": return "💵"
            case "情绪类": return "🧠"
            default: return "📋"
        }
    }
    
    // 根据因子类别获取参数配置
    function getParametersForCategory(category) {
        var parameters = []
        var normalizedCategory = normalizeCategory(category)
        
        switch (normalizedCategory) {
            case "动量类":
                parameters = [
                    {
                        paramName: "window",
                        displayName: "窗口期",
                        paramType: "integer",
                        description: "计算动量的窗口期（天数）",
                        currentValue: 20,
                        commonValues: [5, 10, 20, 30, 60, 120],
                        minValue: 1,
                        maxValue: 250,
                        stepValue: 1,
                        defaultValue: 20
                    },
                    {
                        paramName: "type",
                        displayName: "动量类型",
                        paramType: "enum",
                        description: "动量计算类型",
                        currentValue: "simple",
                        commonValues: ["simple", "exponential", "rank"],
                        defaultValue: "simple"
                    }
                ]
                break
                
            case "价值类":
                parameters = [
                    {
                        paramName: "valuationMetrics",
                        displayName: "价值指标",
                        paramType: "multiselect",
                        description: "选择价值因子代表指标",
                        currentValue: ["bp", "ep"],
                        commonValues: [
                            { value: "bp", label: "BP（市净率倒数）" },
                            { value: "ep", label: "EP（市盈率倒数）" },
                            { value: "dividend_yield", label: "股息率（TTM）" },
                            { value: "cf_p", label: "CF/P（现金流市值比）" }
                        ],
                        defaultValue: ["bp", "ep"]
                    }
                ]
                break
                
            case "质量类":
                parameters = [
                    {
                        paramName: "metric",
                        displayName: "质量指标",
                        paramType: "enum",
                        description: "使用的质量指标",
                        currentValue: "roe",
                        commonValues: ["roe", "roa", "roic", "gross_margin", "operating_margin"],
                        defaultValue: "roe"
                    }
                ]
                break

            case "红利因子":
                parameters = [
                    {
                        paramName: "dividendMetrics",
                        displayName: "红利核心指标",
                        paramType: "multiselect",
                        description: "红利策略核心指标，可多选",
                        currentValue: ["dividend_yield"],
                        commonValues: [
                            "dividend_yield",
                            "dividend_stability",
                            "payout_ratio"
                        ],
                        defaultValue: ["dividend_yield"]
                    },
                    {
                        paramName: "minDividendYield",
                        displayName: "最低股息率",
                        paramType: "number",
                        description: "股息率筛选阈值（%）",
                        currentValue: 2.0,
                        minValue: 0,
                        maxValue: 20,
                        stepValue: 0.1,
                        defaultValue: 2.0
                    }
                ]
                break
                
            case "成长类":
                parameters = [
                    {
                        paramName: "growthMetrics",
                        displayName: "成长指标",
                        paramType: "multiselect",
                        description: "选择参与成长组合的指标",
                        currentValue: ["revenue_growth", "net_profit_growth", "delta_roe", "sue"],
                        commonValues: ["revenue_growth", "net_profit_growth", "delta_roe", "sue"],
                        defaultValue: ["revenue_growth", "net_profit_growth", "delta_roe", "sue"]
                    },
                    {
                        paramName: "revenueGrowthWeight",
                        displayName: "营收增速权重",
                        paramType: "integer",
                        description: "四项合计为 100",
                        currentValue: 25,
                        minValue: 0,
                        maxValue: 100,
                        stepValue: 1,
                        defaultValue: 25
                    },
                    {
                        paramName: "netProfitGrowthWeight",
                        displayName: "单季净利同比增速权重",
                        paramType: "integer",
                        description: "四项合计为 100",
                        currentValue: 25,
                        minValue: 0,
                        maxValue: 100,
                        stepValue: 1,
                        defaultValue: 25
                    },
                    {
                        paramName: "deltaRoeWeight",
                        displayName: "DELTAROE权重",
                        paramType: "integer",
                        description: "四项合计为 100",
                        currentValue: 25,
                        minValue: 0,
                        maxValue: 100,
                        stepValue: 1,
                        defaultValue: 25
                    },
                    {
                        paramName: "sueWeight",
                        displayName: "SUE权重",
                        paramType: "integer",
                        description: "四项合计为 100",
                        currentValue: 25,
                        minValue: 0,
                        maxValue: 100,
                        stepValue: 1,
                        defaultValue: 25
                    }
                ]
                break
                
            case "情绪类":
                parameters = [
                    {
                        paramName: "sentiment_source",
                        displayName: "情绪来源",
                        paramType: "enum",
                        description: "情绪数据来源",
                        currentValue: "news_sentiment",
                        commonValues: ["news_sentiment", "social_media", "market_sentiment", "investor_sentiment"],
                        defaultValue: "news_sentiment"
                    },
                    {
                        paramName: "lookback_days",
                        displayName: "回溯天数",
                        paramType: "integer",
                        description: "情绪数据回溯天数",
                        currentValue: 7,
                        commonValues: [3, 7, 14, 30],
                        minValue: 1,
                        maxValue: 90,
                        stepValue: 1,
                        defaultValue: 7
                    }
                ]
                break
                
        }
        
        return parameters
    }
    
    // 更新参数输入控件
    function updateParameterInputs() {
        // 清空现有参数
        while (parametersColumn.children.length > 0) {
            parametersColumn.children[0].destroy()
        }
        
        if (!factorCategory) return
        
        var parameters = getParametersForCategory(factorCategory)
        currentParameters = {}
        
        // 创建参数输入控件
        for (var i = 0; i < parameters.length; i++) {
            var param = parameters[i]
            
            // 创建参数项容器
            var paramItem = Qt.createQmlObject(`
                import QtQuick 2.15
                import "../"
                
                Column {
                    width: parent.width
                    spacing: 8
                    
                    // 参数标题
                    Text {
                        text: "${param.displayName}"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                    }
                    
                    // 参数描述
                    Text {
                        text: "${param.description}"
                        font.pixelSize: 12
                        color: "#94A3B8"
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }
                    
                    // 参数输入组件
                    FactorParameterInput {
                        id: paramInput_${param.paramName}
                        width: parent.width
                        paramName: "${param.paramName}"
                        displayName: ""
                        paramType: "${param.paramType}"
                        description: ""
                        commonValues: ${JSON.stringify(param.commonValues || [])}
                        defaultValue: ${JSON.stringify(param.defaultValue)}
                        minValue: ${JSON.stringify(param.minValue)}
                        maxValue: ${JSON.stringify(param.maxValue)}
                        stepValue: ${JSON.stringify(param.stepValue)}
                        currentValue: ${JSON.stringify(param.currentValue || param.defaultValue)}
                        
                        onValueChanged: {
                            root.handleParameterChanged("${param.paramName}", value)
                        }
                    }
                }
            `, parametersColumn)
            
            // 存储初始值
            currentParameters[param.paramName] = param.currentValue || param.defaultValue
        }
        
        // 如果没有参数，显示提示
        if (parameters.length === 0) {
            var noParamsText = Qt.createQmlObject(`
                import QtQuick 2.15
                
                Column {
                    width: parent.width
                    spacing: 8
                    
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "🎯"
                        font.pixelSize: 32
                        color: "#64748B"
                    }
                    
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "该因子类别无需额外参数配置"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        width: Math.min(parent.width - 20, 300)
                    }
                    
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "所有参数都使用默认值"
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }
                }
            `, parametersColumn)
        }
    }
    
    // 处理参数变化
    function handleParameterChanged(paramName, value) {
        currentParameters[paramName] = value
        parameterChanged(paramName, value)
        allParametersChanged(currentParameters)
    }
    
    // 获取所有参数值
    function getAllParameters() {
        return currentParameters
    }
    
    // ============ 属性变化监听 ============
    
    onFactorCategoryChanged: {
        updateParameterInputs()
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        if (factorCategory) {
            updateParameterInputs()
        }
    }
}
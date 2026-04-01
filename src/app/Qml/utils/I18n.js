// I18n.js
// 国际化翻译工具
// 支持多语言动态切换

// 语言定义
var LANGUAGES = {
    'zh_CN': '简体中文',
    'en_US': 'English',
    'ja_JP': '日本語'
};

// 当前语言，默认为中文
var currentLanguage = 'zh_CN';

// 翻译字典
var translations = {
    'zh_CN': {
        // 通用
        'common': {
            'yes': '是',
            'no': '否',
            'save': '保存',
            'cancel': '取消',
            'confirm': '确认',
            'back': '返回',
            'next': '下一步',
            'previous': '上一步',
            'create': '创建',
            'edit': '编辑',
            'delete': '删除',
            'search': '搜索',
            'loading': '加载中...',
            'error': '错误',
            'success': '成功',
            'warning': '警告'
        },
        
        // 策略创建页面
        'strategyCreation': {
            'title': '专业策略创建向导',
            'subtitle': '创建并优化专业级量化交易策略',
            'step1': '选择类型与基本信息',
            'step2': '参数配置',
            'step3': '风险与回测',
            
            // 步骤1
            'step1Title': '选择策略类型与配置基本信息',
            'step1Description': '选择适合您的交易风格的策略类型并填写策略基本信息',
            'selectStrategyType': '选择策略类型',
            'strategyBasicInfo': '策略基本信息',
            'strategyName': '策略名称 *',
            'strategyNamePlaceholder': '请输入策略名称（如：双均线趋势跟踪策略）',
            'strategyDescription': '策略描述 *',
            'strategyDescriptionPlaceholder': '详细描述策略的核心逻辑、入场条件、出场条件等...',
            'assetType': '资产类型',
            'timeFrame': '时间框架',
            'riskLevel': '风险等级',
            'optimizationMethod': '优化方法',
            'tags': '策略标签',
            'tagsPlaceholder': '输入标签，用逗号分隔（如：趋势跟踪，技术分析，A股）',
            
            // 策略类型
            'strategyTypes': {
                'trend_following': '趋势跟踪策略',
                'mean_reversion': '均值回归策略',
                'momentum': '动量策略',
                'arbitrage': '套利策略',
                'machine_learning': '机器学习策略',
                'multi_factor': '多因子策略',
                'high_frequency': '高频策略',
                'event_driven': '事件驱动策略',
                'custom': '自定义策略'
            },
            
            // 策略类型描述
            'strategyTypeDescriptions': {
                'trend_following': '基于价格趋势判断的交易策略，在上升趋势中买入，下降趋势中卖出。适合趋势明显的市场环境',
                'mean_reversion': '基于价格偏离均值后回归的交易策略，在价格过低时买入，过高时卖出。适合震荡市场',
                'momentum': '基于价格动量的交易策略，跟随强势股票上涨，避开弱势股票。适合有明显趋势的市场',
                'arbitrage': '基于价格差异的套利交易，利用不同市场或品种间的价差获利。风险相对较低',
                'machine_learning': '基于机器学习模型预测的交易策略，使用算法识别市场模式和预测价格走势',
                'multi_factor': '基于多个因子综合评分的交易策略，综合考虑多个维度选择股票',
                'high_frequency': '基于高频数据的交易策略，要求低延迟和快速执行。适合机构投资者',
                'event_driven': '基于特定事件（如财报发布、并购公告）的交易策略，利用事件对价格的影响获利',
                'custom': '用户自定义代码的交易策略，灵活支持各种复杂逻辑和算法'
            },
            
            // 步骤2
            'step2Title': '参数配置与优化',
            'step2Description': '本页包含三类配置：通用参数、个性化参数和高级参数。回测周期、资金、手续费、滑点等运行参数统一放在回测页面设置。',
            'parameterConfiguration': '策略参数配置',
            'parameterConfigurationDescription': '配置策略的核心参数和算法设置',
            'parameterPanel': '参数配置面板',
            'advancedParameterOptions': '高级参数优化选项',
            'parameterOptimizationRange': '参数优化范围',
            'parameterOptimizationRangeOptions': ['无优化', '小范围优化', '中等范围优化', '大范围优化'],
            'sensitivityAnalysis': '参数敏感性分析',
            'sensitivityAnalysisOptions': ['无分析', '基础分析', '详细分析'],
            'parameterConstraints': '参数约束条件',
            'parameterConstraintOptions': ['无约束', '线性约束', '非线性约束'],
            'parameterInitializationMethod': '参数初始化方式',
            'parameterInitializationMethods': ['随机初始化', '均匀初始化', '经验初始化'],
            'customParameterScript': '自定义参数脚本',
            'customParameterScriptPlaceholder': '输入自定义参数初始化或优化脚本...',
            'advancedParameters': '高级参数选项',
            'personalizedParameters': '个性化参数',
            'configuredParameters': '已配置参数',
            'parameterValidationPassed': '参数验证通过',
            'parameterValidationRequired': '参数需要验证',
            
            // 步骤3
            'step3Title': '创建确认',
            'step3Description': '确认策略定义与参数完整性；回测周期、基准和高级回测选项将在策略回测页面配置',
            'strategySummary': '策略摘要',
            'strategyType': '策略类型',
            'riskLevelLabel': '风险等级',
            'backtestPeriod': '回测周期',
            'parameterCount': '参数数量',
            'basicRiskManagement': '基础风险管理',
            'basicBacktestSettings': '基础回测设置',
            'positionManagement': '仓位管理',
            'advancedBacktestOptions': '高级回测选项',
            
            // 风险管理
            'stopLossPercent': '止损比例',
            'stopLossDescription': '止损触发比例（百分比）',
            'takeProfitPercent': '止盈比例',
            'takeProfitDescription': '止盈触发比例（百分比）',
            'maxDrawdownLimit': '最大回撤限制',
            'maxDrawdownDescription': '最大回撤触发限制（百分比）',
            'maxPositionPercent': '最大仓位百分比',
            'maxPositionDescription': '最大持仓占资金比例（百分比）',
            
            // 回测设置
            'backtestYears': '回测周期',
            'benchmark': '基准指数',
            'transactionCost': '交易成本',
            'useCustomDateRange': '使用自定义日期范围',
            'startDate': '开始日期',
            'endDate': '结束日期',
            'startDatePlaceholder': 'YYYY-MM-DD',
            'endDatePlaceholder': 'YYYY-MM-DD',
            
            // 仓位管理
            'positionSizingMethod': '仓位管理方法',
            'positionSizingDescription': '选择仓位管理方法',
            'fixedPosition': '固定仓位',
            'kellyFormula': '凯利公式',
            'equalWeight': '等权重',
            
            // 高级选项
            'enableAdvancedOptions': '启用高级选项',
            'walkForwardOptimization': '滚动窗口优化',
            'monteCarloSimulation': '蒙特卡洛模拟',
            'outOfSampleTesting': '样本外测试',
            'windowLength': '窗口长度',
            'sampleCount': '样本数',
            'outOfSampleRatio': '样本外比例',
            
            // 验证消息
            'validationPassed': '✓ 当前步骤验证通过',
            'validationRequired': '⚠️ 请完成当前步骤的必填项',
            'selectStrategyTypeError': '请选择策略类型',
            'strategyNameError': '请输入策略名称',
            'strategyDescriptionError': '请输入策略描述',
            'parameterError': '请配置有效的策略参数',
            'validationPassedFull': '✓ 验证通过',
            'strategyCreatedSuccess': '✅ 策略创建成功！正在保存...',
            'strategyCreatedBacktest': '✅ 策略创建成功！',
            'backtestStarted': '策略创建成功，请前往回测页面启动回测。',
            'backtestControllerError': '⚠️ 回测控制器未初始化或策略名称为空',
            'immediateBacktest': '前往回测',
            
            // 对话框消息
            'strategyCreationSuccessDialogTitle': '策略创建成功',
            'strategyCreatedSuccessDialogMessage': '策略已成功创建并保存到策略库！',
            'strategyCreatedSuccessDialogSubtitle': '您可以在策略库中查看和编辑该策略，需要时再进入回测页面配置运行参数。',
            
            // 按钮
            'createAndBacktest': '创建后前往回测',
            'nextStep': '下一步',
            
            // 风险等级
            'riskLevels': {
                'low': '保守型',
                'medium': '稳健型',
                'high': '进取型',
                'aggressive': '激进型'
            },
            
            // 资产类型
            'assetTypes': ['股票', '期货', '加密货币', '外汇', '期权', 'ETF', '债券', '商品'],
            'assetTypeValues': ['stock', 'futures', 'crypto', 'forex', 'options', 'etf', 'bond', 'commodity'],
            
            // 时间框架
            'timeFrames': ['高频(1分钟)', '日内(5分钟)', '短期(15分钟)', '中期(1小时)', '长期(日线)', '超长期(周线)'],
            'timeFrameValues': ['1min', '5min', '15min', '1hour', 'daily', 'weekly'],
            
            // 优化方法
            'optimizationMethods': ['遗传算法', '网格搜索', '贝叶斯优化', '随机搜索'],
            'optimizationMethodValues': ['genetic', 'grid', 'bayesian', 'random'],
            
            // 基准指数
            'benchmarks': ['沪深300', '上证指数', '深证成指', '创业板指', '中证500', '中证1000', '自定义'],
            'defaultBenchmark': '沪深300',
            
            // 参数相关翻译（新增）
            'initialCapital': '初始资金',
            'initialCapitalDescription': '回测起始资金金额',
            'commission': '手续费率',
            'commissionDescription': '单边交易手续费率',
            'slippage': '滑点成本',
            'slippageDescription': '预期成交价格与报价的差距',
            'positionSize': '仓位大小',
            'positionSizeDescription': '策略每次建仓使用的目标仓位比例',
            'maxPosition': '最大持仓',
            'maxPositionDescription': '最大持仓占资金比例',
            'orderType': '订单类型',
            'orderTypeDescription': '策略使用的订单类型',
            'limitOrder': '限价单',
            'marketOrder': '市价单',
            'rebalanceDays': '调仓周期',
            'rebalanceDaysDescription': '策略重新筛选或调整仓位的周期',
            'currencyUnit': '元',
            'daysUnit': '天',
            'levelsUnit': '层',
            'commonParameters': '通用参数',
            'coreParameters': '核心参数',
            'personalizedParameters': '个性化参数',
            
            // 策略特定参数标签
            'fastPeriod': '快线周期',
            'fastPeriodDescription': '短期移动平均线周期',
            'slowPeriod': '慢线周期',
            'slowPeriodDescription': '长期移动平均线周期',
            'bollPeriod': '布林带周期',
            'bollPeriodDescription': '均值回归策略的布林带计算周期',
            'bollStd': '布林带标准差',
            'bollStdDescription': '均值回归策略的布林带标准差倍数',
            'reversionThreshold': '回归阈值',
            'reversionThresholdDescription': '触发均值回归交易的阈值',
            'lookbackPeriod': '回顾周期',
            'lookbackPeriodDescription': '计算均值和标准差的回顾周期',
            'entryThreshold': '入场阈值',
            'entryThresholdDescription': '价格偏离均值多少标准差时入场',
            'exitThreshold': '出场阈值',
            'exitThresholdDescription': '价格回归到均值多少标准差时出场',
            'gridLevels': '网格层数',
            'gridLevelsDescription': '网格交易的层数设置',
            'momentumPeriod': '动量周期',
            'momentumPeriodDescription': '计算动量的周期',
            'topN': 'Top N股票',
            'topNDescription': '选择得分最高的前N只股票',
            'selectionRatio': '选股比例',
            'selectionRatioDescription': '选择动量最强股票的百分比',
            'rebalancingPeriod': '调仓周期',
            'rebalancingPeriodDescription': '重新筛选和调整仓位的周期',
            'spreadThreshold': '价差阈值',
            'spreadThresholdDescription': '统计套利触发所需的最小价差阈值',
            'lookbackDays': '回看天数',
            'lookbackDaysDescription': '计算协整关系和历史标准差的天数',
            'entryZScore': '入场Z值',
            'entryZScoreDescription': '价差偏离多少标准差时入场',
            'exitZScore': '出场Z值',
            'exitZScoreDescription': '价差回归到多少标准差时出场',
            'hedgeRatio': '对冲比例',
            'hedgeRatioDescription': '配对中对冲头寸的比例',
            'featureWindow': '特征窗口',
            'featureWindowDescription': '特征提取的时间窗口',
            'predictionDays': '预测天数',
            'predictionDaysDescription': '预测未来价格的天数',
            'trainingDays': '训练天数',
            'trainingDaysDescription': '模型训练使用的历史数据天数',
            'confidenceThreshold': '置信阈值',
            'confidenceThresholdDescription': '模型预测置信度阈值',
            'factorTypes': '因子类型',
            'factorTypesDescription': '使用的因子类型',
            'value': '价值',
            'quality': '质量',
            'growth': '成长',
            'momentum': '动量',
            'size': '规模',
            'volatility': '波动率',
            'liquidity': '流动性',
            'sentiment': '情绪',
            'timeframe': '时间框架',
            'timeframeDescription': '高频交易的时间框架',
            'oneMinute': '1分钟',
            'fiveMinutes': '5分钟',
            'fifteenMinutes': '15分钟',
            'thirtyMinutes': '30分钟',
            'oneHour': '1小时',
            'eventTypes': '事件类型',
            'eventTypesDescription': '关注的事件类型',
            'earningsRelease': '财报发布',
            'mergerAnnouncement': '并购公告',
            'dividendAnnouncement': '分红公告',
            'managementChange': '高管变动',
            'policyRelease': '政策发布',
            'productLaunch': '产品发布',
            'customCode': '自定义代码',
            'customCodeDescription': '请输入自定义策略代码',
            'customCodePlaceholder': '在这里编写您的自定义策略代码...'
        },
        
        // 参数类型
        'parameterTypes': {
            'slider': '滑块',
            'select': '选择',
            'toggle': '开关',
            'input': '输入框',
            'group': '分组'
        }
    },
    
    'en_US': {
        'common': {
            'yes': 'Yes',
            'no': 'No',
            'save': 'Save',
            'cancel': 'Cancel',
            'confirm': 'Confirm',
            'back': 'Back',
            'next': 'Next',
            'previous': 'Previous',
            'create': 'Create',
            'edit': 'Edit',
            'delete': 'Delete',
            'search': 'Search',
            'loading': 'Loading...',
            'error': 'Error',
            'success': 'Success',
            'warning': 'Warning'
        },
        
        'strategyCreation': {
            'title': 'Professional Strategy Creation Wizard',
            'subtitle': 'Create and optimize professional quantitative trading strategies',
            'step1': 'Type Selection & Basic Info',
            'step2': 'Parameter Configuration',
            'step3': 'Risk & Backtest',
            
            'step1Title': 'Select Strategy Type and Configure Basic Information',
            'step1Description': 'Select a strategy type that fits your trading style and fill in basic information',
            'selectStrategyType': 'Select Strategy Type',
            'strategyBasicInfo': 'Strategy Basic Information',
            'strategyName': 'Strategy Name *',
            'strategyNamePlaceholder': 'Enter strategy name (e.g., Dual Moving Average Trend Following)',
            'strategyDescription': 'Strategy Description *',
            'strategyDescriptionPlaceholder': 'Describe the core logic, entry conditions, exit conditions, etc...',
            'assetType': 'Asset Type',
            'timeFrame': 'Time Frame',
            'riskLevel': 'Risk Level',
            'optimizationMethod': 'Optimization Method',
            'tags': 'Strategy Tags',
            'tagsPlaceholder': 'Enter tags separated by commas (e.g., trend following, technical analysis, A-share)',
            
            'strategyTypes': {
                'trend_following': 'Trend Following Strategy',
                'mean_reversion': 'Mean Reversion Strategy',
                'momentum': 'Momentum Strategy',
                'arbitrage': 'Arbitrage Strategy',
                'machine_learning': 'Machine Learning Strategy',
                'multi_factor': 'Multi-Factor Strategy',
                'high_frequency': 'High Frequency Strategy',
                'event_driven': 'Event Driven Strategy',
                'custom': 'Custom Strategy'
            },
            
            'strategyTypeDescriptions': {
                'trend_following': 'Trading strategy based on price trends, buying in uptrends and selling in downtrends. Suitable for trending markets',
                'mean_reversion': 'Trading strategy based on price deviations from the mean, buying when too low and selling when too high. Suitable for ranging markets',
                'momentum': 'Trading strategy based on price momentum, following strong stocks and avoiding weak ones. Suitable for markets with clear trends',
                'arbitrage': 'Arbitrage trading based on price differences between markets or instruments. Relatively low risk',
                'machine_learning': 'Trading strategy based on machine learning models, using algorithms to identify market patterns and predict price movements',
                'multi_factor': 'Trading strategy based on multi-factor scoring, selecting stocks based on multiple dimensions',
                'high_frequency': 'High-frequency trading strategy requiring low latency and fast execution. Suitable for institutional investors',
                'event_driven': 'Trading strategy based on specific events (e.g., earnings releases, M&A announcements), profiting from event impacts',
                'custom': 'User-defined strategy with custom code, supporting complex logic and algorithms'
            },
            
            'step2Title': 'Parameter Configuration and Optimization',
            'step2Description': 'This step contains three sections: shared parameters, personalized parameters, and advanced parameters. Backtest period, capital, commission, and slippage are configured on the backtest page.',
            'parameterConfiguration': 'Strategy Parameter Configuration',
            'parameterConfigurationDescription': 'Configure core parameters and algorithm settings',
            'parameterPanel': 'Parameter Configuration Panel',
            'parameterConfigPanel': 'Parameter Configuration Panel',
            'advancedParameters': 'Advanced Parameters',
            'personalizedParameters': 'Personalized Parameters',
            'configuredParameters': 'Configured Parameters',
            'parameterValidationPassed': 'Parameter validation passed',
            'parameterValidationRequired': 'Parameter validation required',
            
            'step3Title': 'Creation Review',
            'step3Description': 'Review the strategy definition before creation; session backtest options are configured on the strategy backtest page',
            'strategySummary': 'Strategy Summary',
            'strategyType': 'Strategy Type',
            'riskLevelLabel': 'Risk Level',
            'backtestPeriod': 'Backtest Period',
            'parameterCount': 'Parameter Count',
            'basicRiskManagement': 'Basic Risk Management',
            'basicBacktestSettings': 'Basic Backtest Settings',
            'positionManagement': 'Position Management',
            'advancedBacktestOptions': 'Advanced Backtest Options',
            
            'stopLossPercent': 'Stop Loss Percentage',
            'stopLossDescription': 'Stop loss trigger percentage',
            'takeProfitPercent': 'Take Profit Percentage',
            'takeProfitDescription': 'Take profit trigger percentage',
            'positionSize': 'Position Size',
            'positionSizeDescription': 'Target position ratio used for each strategy entry',
            'rebalanceDays': 'Rebalance Days',
            'rebalanceDaysDescription': 'Interval for the strategy to rebalance or refresh holdings',
            'maxDrawdownLimit': 'Maximum Drawdown Limit',
            'maxDrawdownDescription': 'Maximum drawdown trigger limit',
            'maxPositionPercent': 'Maximum Position Percentage',
            'maxPositionDescription': 'Maximum holding percentage of capital',
            
            'backtestYears': 'Backtest Period',
            'benchmark': 'Benchmark',
            'transactionCost': 'Transaction Cost',
            'useCustomDateRange': 'Use Custom Date Range',
            'startDate': 'Start Date',
            'endDate': 'End Date',
            'startDatePlaceholder': 'YYYY-MM-DD',
            'endDatePlaceholder': 'YYYY-MM-DD',
            
            'positionSizingMethod': 'Position Sizing Method',
            'positionSizingDescription': 'Select position sizing method',
            'fixedPosition': 'Fixed Position',
            'kellyFormula': 'Kelly Formula',
            'equalWeight': 'Equal Weight',
            
            'enableAdvancedOptions': 'Enable Advanced Options',
            'walkForwardOptimization': 'Walk-Forward Optimization',
            'monteCarloSimulation': 'Monte Carlo Simulation',
            'outOfSampleTesting': 'Out-of-Sample Testing',
            'windowLength': 'Window Length',
            'sampleCount': 'Sample Count',
            'outOfSampleRatio': 'Out-of-Sample Ratio',
            
            'validationPassed': '✓ Current step validation passed',
            'validationRequired': '⚠️ Please complete required fields for current step',
            'selectStrategyTypeError': 'Please select strategy type',
            'strategyNameError': 'Please enter strategy name',
            'strategyDescriptionError': 'Please enter strategy description',
            'parameterError': 'Please configure valid strategy parameters',
            'validationPassedFull': '✓ Validation passed',
            'strategyCreatedSuccess': '✅ Strategy created successfully! Saving...',
            'strategyCreatedBacktest': '✅ Strategy created successfully!',
            'backtestStarted': 'Strategy created successfully. Open the backtest page when you are ready to run it.',
            'backtestControllerError': '⚠️ Backtest controller not initialized or strategy name empty',
            'immediateBacktest': 'Open Backtest',
            
            'createAndBacktest': 'Create Then Open Backtest',
            'nextStep': 'Next Step',
            
            'riskLevels': {
                'low': 'Conservative',
                'medium': 'Moderate',
                'high': 'Aggressive',
                'aggressive': 'Very Aggressive'
            },
            
            'assetTypes': ['Stock', 'Futures', 'Cryptocurrency', 'Forex', 'Options', 'ETF', 'Bond', 'Commodity'],
            'assetTypeValues': ['stock', 'futures', 'crypto', 'forex', 'options', 'etf', 'bond', 'commodity'],
            
            'timeFrames': ['High-Frequency(1min)', 'Intraday(5min)', 'Short-Term(15min)', 'Medium-Term(1hour)', 'Long-Term(Daily)', 'Very Long-Term(Weekly)'],
            'timeFrameValues': ['1min', '5min', '15min', '1hour', 'daily', 'weekly'],
            
            'optimizationMethods': ['Genetic Algorithm', 'Grid Search', 'Bayesian Optimization', 'Random Search'],
            'optimizationMethodValues': ['genetic', 'grid', 'bayesian', 'random'],
            
            'benchmarks': ['CSI 300', 'Shanghai Composite', 'Shenzhen Component', 'ChiNext', 'CSI 500', 'CSI 1000', 'Custom'],
            'defaultBenchmark': 'CSI 300',
            
            // 对话框消息
            'strategyCreationSuccessDialogTitle': 'Strategy Creation Successful',
            'strategyCreatedSuccessDialogMessage': 'Strategy has been successfully created and saved to the strategy library!',
            'strategyCreatedSuccessDialogSubtitle': 'You can view and edit this strategy in the strategy library, then configure runtime backtest settings separately when needed.'
        },
        
        'parameterTypes': {
            'slider': 'Slider',
            'select': 'Select',
            'toggle': 'Toggle',
            'input': 'Input',
            'group': 'Group'
        }
    }
};

// 获取翻译
function tr(key, language) {
    var lang = language || currentLanguage;
    
    // 分割键路径
    var parts = key.split('.');
    var result = translations[lang];
    
    // 遍历路径
    for (var i = 0; i < parts.length; i++) {
        if (result && result.hasOwnProperty(parts[i])) {
            result = result[parts[i]];
        } else {
            // 如果找不到翻译，返回键本身
            console.warn('Translation not found for key:', key, 'in language:', lang);
            return key;
        }
    }
    
    return result;
}

// 设置当前语言
function setLanguage(lang) {
    if (LANGUAGES.hasOwnProperty(lang)) {
        currentLanguage = lang;
        console.log('Language set to:', lang);
        return true;
    } else {
        console.error('Unsupported language:', lang);
        return false;
    }
}

// 获取当前语言
function getCurrentLanguage() {
    return currentLanguage;
}

// 获取所有支持的语言
function getSupportedLanguages() {
    return LANGUAGES;
}

// 格式化带参数的翻译
function trf(key, params, language) {
    var text = tr(key, language);
    
    // 替换参数
    for (var param in params) {
        if (params.hasOwnProperty(param)) {
            var placeholder = '{' + param + '}';
            text = text.replace(placeholder, params[param]);
        }
    }
    
    return text;
}

// 快捷函数
function qsTr(key) {
    return tr(key);
}
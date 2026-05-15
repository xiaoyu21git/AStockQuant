// FactorSchemaLoader.js
// 因子参数配置加载器 - 基于 JSON Schema 的动态表单系统

/**
 * 因子参数配置加载器
 * 提供统一的 JSON Schema 配置加载和管理功能
 */

var cachedSchemas = null;
var schemaLoadInProgress = false;
var pendingSchemaCallbacks = [];

var enumIds = {
  commonFrequency: {
    daily: 0,
    weekly: 1,
    monthly: 2,
    quarterly: 3
  },
  commonStandardization: {
    none: 0,
    zscore: 1,
    minmax: 2,
    percentile: 3
  },
  standardizationMethod: {
    none: 0,
    zscore: 1,
    minmax: 2,
    rank: 3,
    percentile: 4
  },
  momentumCalculationType: {
    simple: 0,
    rank: 1,
    exponential: 3
  },
  valuationMetric: {
    bp: 0,
    ep: 1,
    dividendYield: 2,
    cfp: 3
  },
  qualityMetric: {
    roe: 0,
    roa: 1,
    grossMargin: 2,
    operatingMargin: 3
  },
  growthMetric: {
    revenueGrowth: 0,
    netProfitGrowth: 1,
    deltaRoe: 2,
    sue: 3
  },
  sizeMetric: {
    marketCap: 0,
    circulatingMarketCap: 1,
    totalAssets: 2
  },
  lowVolComponent: {
    volatility: 0,
    drawdown: 1,
    beta: 2
  },
  dividendMetric: {
    dividendYield: 0,
    payoutRatio: 1,
    dividendStability: 2
  },
  sentimentSource: {
    news: 0,
    socialMedia: 1,
    analystRating: 2,
    market: 3
  },
  technicalIndicator: {
    rsi: 0,
    macd: 1,
    ma: 2,
    ema: 3,
    boll: 4,
    kdj: 5,
    atr: 6,
    obv: 7,
    vwap: 8,
    volumeRatio: 9,
    turnoverStability: 10
  },
  technicalPriceType: {
    close: 0,
    open: 1,
    high: 2,
    low: 3
  },
  liquidityMetric: {
    turnoverRate: 0,
    volume: 1,
    amihudIlliquidity: 2,
    amplitude: 3
  },
  technicalCombinationMode: {
    equalWeight: 0,
    normalizedAverage: 1
  },
  macroDimension: {
    growth: 0,
    inflation: 1,
    credit: 2,
    rates: 3,
    policy: 4,
    riskAppetite: 5
  },
  macroIndicator: {
    industrialAddedValueYoy: 0,
    manufacturingPmi: 1,
    gdpYoy: 2,
    cpiYoy: 3,
    ppiYoy: 4,
    m2Yoy: 5,
    socialFinancingStockYoy: 6,
    m1M2Spread: 7,
    tenYearBondYield: 8,
    shibor3m: 9,
    lpr1y: 10,
    reserveRequirementRatio: 11,
    aaCreditSpread: 12,
    vixProxy: 13
  },
  sectorType: {
    swL1: 0,
    swL2: 1,
    citicL1: 2,
    citicL2: 3
  },
  industryMetric: {
    industryProsperity: 0,
    industryMomentum: 1,
    industryConcentration: 2
  }
};

function enumOption(value, label) {
  return {
    value: value,
    label: label
  };
}

function createStandardizationProperty(useConfigurableEnum) {
  var standardizationIds = useConfigurableEnum ? enumIds.standardizationMethod : enumIds.commonStandardization;
  return {
    "type": "string",
    "label": "标准化方法",
    "description": "因子值的标准化处理方法",
    "enum": [
      enumOption(standardizationIds.zscore, "标准分标准化（Z-Score）", ["zscore"]),
      enumOption(standardizationIds.minmax, "区间缩放标准化（Min-Max）", ["minmax"]),
      enumOption(standardizationIds.percentile, "分位数", ["percentile"]),
      enumOption(standardizationIds.none, "不处理", ["none"])
    ],
    "default": standardizationIds.zscore
  };
}

function cloneSchemasForCallback(schemas) {
  return schemas || defaultSchemas;
}

function flushSchemaCallbacks(schemas) {
  var resolvedSchemas = cloneSchemasForCallback(schemas);
  cachedSchemas = resolvedSchemas;
  schemaLoadInProgress = false;

  var callbacks = pendingSchemaCallbacks.slice(0);
  pendingSchemaCallbacks = [];
  for (var index = 0; index < callbacks.length; ++index) {
    callbacks[index](resolvedSchemas);
  }
}

// 默认配置（当外部文件加载失败时使用）
var defaultSchemas = {
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "因子参数配置",
  "description": "所有因子类型的参数定义，使用 JSON Schema 格式",
  "version": "1.0.0",
  
  "commonParameters": {
    "title": "通用参数",
    "description": "所有因子共享的通用参数",
    "properties": {
      "frequency": {
        "type": "string",
        "label": "数据频率",
        "description": "因子计算的数据频率",
        "enum": [
          enumOption(enumIds.commonFrequency.daily, "日频", ["daily"]),
          enumOption(enumIds.commonFrequency.weekly, "周频", ["weekly"]),
          enumOption(enumIds.commonFrequency.monthly, "月频", ["monthly"])
        ],
        "default": enumIds.commonFrequency.daily
      },
      "lookbackPeriod": {
        "type": "integer",
        "label": "回溯窗口",
        "description": "计算因子值所需的通用历史数据长度（与各因子专属观察窗口独立）",
        "default": 252,
        "minimum": 1,
        "maximum": 1000,
        "step": 1,
        "unit": "天",
        "commonValues": [20, 60, 120, 252]
      },
      "laggedEnabled": {
        "type": "boolean",
        "label": "滞后处理开关",
        "description": "是否启用滞后处理（防止未来函数）",
        "default": true
      },
      "standardization": createStandardizationProperty(false),
      "neutralizationEnabled": {
        "type": "boolean",
        "label": "中性化开关",
        "description": "是否消除行业/市值影响",
        "default": true
      }
    }
  },

  "factorSchemas": {
    "momentum": {
      "title": "动量因子",
      "description": "动量因子参数配置",
      "properties": {
        "window": {
          "type": "integer",
          "label": "动量窗口",
          "description": "计算动量的时间窗口（天数）",
          "default": 60,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [20, 60, 120, 250]
        },
        "type": {
          "type": "string",
          "label": "计算方法",
          "description": "动量计算方法",
          "enum": [
            enumOption(enumIds.momentumCalculationType.simple, "简单动量", ["simple"]),
            enumOption(enumIds.momentumCalculationType.exponential, "指数加权动量", ["exponential"]),
            enumOption(enumIds.momentumCalculationType.rank, "排序动量", ["rank"])
          ],
          "default": enumIds.momentumCalculationType.simple
        }
      }
    },

    "value": {
      "title": "价值因子",
      "description": "价值因子参数配置",
      "properties": {
        "valuationMetrics": {
          "type": "array",
          "label": "价值指标",
          "description": "选择价值因子代表指标",
          "default": [enumIds.valuationMetric.bp, enumIds.valuationMetric.ep],
          "items": {
            "type": "string",
            "enum": [
              enumOption(enumIds.valuationMetric.bp, "市净率倒数（BP）", ["bp"]),
              enumOption(enumIds.valuationMetric.ep, "市盈率倒数（EP）", ["ep"]),
              enumOption(enumIds.valuationMetric.dividendYield, "股息率（过去12个月）", ["dividend_yield"]),
              enumOption(enumIds.valuationMetric.cfp, "现金流市值比（CF/P）", ["cf_p"])
            ]
          }
        },
        "bpWeight": {
          "type": "number",
          "label": "市净率倒数权重（BP）",
          "description": "市净率倒数（BP）在价值组合中的权重",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0,
          "linkedWeightGroup": "value",
          "linkedWeightTotal": 100,
          "linkedWeightDecimals": 0
        },
        "epWeight": {
          "type": "number",
          "label": "市盈率倒数权重（EP）",
          "description": "市盈率倒数（EP）在价值组合中的权重",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0,
          "linkedWeightGroup": "value",
          "linkedWeightTotal": 100,
          "linkedWeightDecimals": 0
        },
        "dividendYieldWeight": {
          "type": "number",
          "label": "股息率权重",
          "description": "股息率（过去12个月）在价值组合中的权重",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0,
          "linkedWeightGroup": "value",
          "linkedWeightTotal": 100,
          "linkedWeightDecimals": 0
        },
        "cfPWeight": {
          "type": "number",
          "label": "现金流市值比权重（CF/P）",
          "description": "现金流市值比（CF/P）在价值组合中的权重",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0,
          "linkedWeightGroup": "value",
          "linkedWeightTotal": 100,
          "linkedWeightDecimals": 0
        }
      }
    },
    
    "quality": {
      "title": "质量因子",
      "description": "质量因子参数配置",
      "properties": {
        "metric": {
          "type": "string",
          "label": "质量指标",
          "description": "使用的质量指标",
          "enum": [
            enumOption(enumIds.qualityMetric.roe, "净资产收益率（ROE）", ["roe"]),
            enumOption(enumIds.qualityMetric.roa, "总资产收益率（ROA）", ["roa"]),
            enumOption(enumIds.qualityMetric.grossMargin, "毛利率", ["gross_margin"]),
            enumOption(enumIds.qualityMetric.operatingMargin, "营业利润率", ["operating_margin"])
          ],
          "default": enumIds.qualityMetric.roe
        }
      }
    },
    
    "growth": {
      "title": "成长因子",
      "description": "成长因子参数配置",
      "properties": {
        "standardization": createStandardizationProperty(true),
        "growthMetrics": {
          "type": "array",
          "label": "成长指标",
          "description": "使用的成长指标",
          "default": [
            enumIds.growthMetric.revenueGrowth,
            enumIds.growthMetric.netProfitGrowth,
            enumIds.growthMetric.deltaRoe,
            enumIds.growthMetric.sue
          ],
          "items": {
            "type": "string",
            "enum": [
              enumOption(enumIds.growthMetric.revenueGrowth, "营收增速", ["revenue_growth"]),
              enumOption(enumIds.growthMetric.netProfitGrowth, "单季净利同比增速", ["net_profit_growth"]),
              enumOption(enumIds.growthMetric.deltaRoe, "ROE同比变化（DELTAROE）", ["delta_roe"]),
              enumOption(enumIds.growthMetric.sue, "标准化预期外盈利（SUE）", ["sue"])
            ]
          }
        },
        "revenueGrowthWeight": {
          "type": "number",
          "label": "营收增速权重",
          "description": "营收增速在成长组合中的权重，四项合计为 100",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0
        },
        "netProfitGrowthWeight": {
          "type": "number",
          "label": "单季净利同比增速权重",
          "description": "单季净利同比增速在成长组合中的权重，四项合计为 100",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0
        },
        "deltaRoeWeight": {
          "type": "number",
          "label": "ROE同比变化权重（DELTAROE）",
          "description": "ROE同比变化（DELTAROE）在成长组合中的权重，四项合计为 100",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0
        },
        "sueWeight": {
          "type": "number",
          "label": "标准化预期外盈利权重（SUE）",
          "description": "标准化预期外盈利（SUE）在成长组合中的权重，四项合计为 100",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0
        }
      }
    },
    
    "size": {
      "title": "规模因子",
      "description": "规模因子参数配置",
      "properties": {
        "sizeMetric": {
          "type": "string",
          "label": "规模指标",
          "description": "使用的规模指标",
          "enum": [
            enumOption(enumIds.sizeMetric.marketCap, "总市值", ["market_cap"]),
            enumOption(enumIds.sizeMetric.circulatingMarketCap, "流通市值", ["circulating_market_cap"]),
            enumOption(enumIds.sizeMetric.totalAssets, "总资产", ["total_assets"])
          ],
          "default": enumIds.sizeMetric.circulatingMarketCap
        }
      }
    },
    
    "low_volatility": {
      "title": "低波因子",
      "description": "低波因子参数配置",
      "properties": {
        "window": {
          "type": "integer",
          "label": "波动率窗口",
          "description": "计算波动率的时间窗口（天数）",
          "default": 60,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [20, 60, 120, 250]
        },
        "components": {
          "type": "array",
          "label": "低波构成",
          "description": "选择参与排序的低波信号，可多选",
          "required": true,
          "default": [
            enumIds.lowVolComponent.volatility,
            enumIds.lowVolComponent.drawdown,
            enumIds.lowVolComponent.beta
          ],
          "items": {
            "type": "string",
            "enum": [
              enumOption(enumIds.lowVolComponent.volatility, "波动率倒数", ["volatility"]),
              enumOption(enumIds.lowVolComponent.drawdown, "最大回撤倒数", ["drawdown"]),
              enumOption(enumIds.lowVolComponent.beta, "贝塔倒数（Beta）", ["beta"])
            ]
          }
        },
        "volatilityWeight": {
          "type": "number",
          "label": "波动率权重",
          "description": "波动率倒数在低波组合中的权重，三项合计为 100",
          "default": 33.4,
          "minimum": 0.0,
          "maximum": 100.0,
          "step": 0.1,
          "unit": "%",
          "decimals": 1
        },
        "drawdownWeight": {
          "type": "number",
          "label": "最大回撤权重",
          "description": "最大回撤倒数在低波组合中的权重，三项合计为 100",
          "default": 33.3,
          "minimum": 0.0,
          "maximum": 100.0,
          "step": 0.1,
          "unit": "%",
          "decimals": 1
        },
        "betaWeight": {
          "type": "number",
          "label": "贝塔权重（Beta）",
          "description": "贝塔倒数（Beta）在低波组合中的权重，三项合计为 100",
          "default": 33.3,
          "minimum": 0.0,
          "maximum": 100.0,
          "step": 0.1,
          "unit": "%",
          "decimals": 1
        }
      }
    },
    
    "dividend": {
      "title": "红利因子",
      "description": "红利因子参数配置",
      "properties": {
        "standardization": createStandardizationProperty(true),
        "dividendMetrics": {
          "type": "array",
          "label": "红利核心指标",
          "description": "红利策略核心指标，支持多选",
          "required": true,
          "default": [enumIds.dividendMetric.dividendYield],
          "items": {
            "type": "string",
            "enum": [
              enumOption(enumIds.dividendMetric.dividendYield, "股息率", ["dividend_yield"]),
              enumOption(enumIds.dividendMetric.dividendStability, "分红稳定性", ["dividend_stability"]),
              enumOption(enumIds.dividendMetric.payoutRatio, "股利支付率", ["payout_ratio"])
            ]
          }
        },
        "minDividendYield": {
          "type": "number",
          "label": "最低股息率",
          "description": "最低股息率要求（%）",
          "default": 2.0,
          "minimum": 0.0,
          "maximum": 20.0,
          "step": 0.1,
          "unit": "%"
        }
      }
    },
    
    "sentiment": {
      "title": "情绪因子",
      "description": "情绪因子参数配置",
      "properties": {
        "standardization": createStandardizationProperty(true),
        "sentimentSource": {
          "type": "string",
          "label": "情绪数据源",
          "description": "情绪数据来源",
          "enum": [
            enumOption(enumIds.sentimentSource.news, "新闻情绪", ["news_sentiment"]),
            enumOption(enumIds.sentimentSource.socialMedia, "社交媒体", ["social_media"]),
            enumOption(enumIds.sentimentSource.analystRating, "分析师评级", ["analyst_rating"]),
            enumOption(enumIds.sentimentSource.market, "市场情绪", ["market_sentiment"])
          ],
          "default": enumIds.sentimentSource.news
        },
        "window": {
          "type": "integer",
          "label": "情绪窗口",
          "description": "情绪数据计算窗口（天数）",
          "default": 20,
          "minimum": 1,
          "maximum": 60,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 10, 20, 30]
        }
      }
    },
    
    "technical": {
      "title": "技术因子",
      "description": "技术因子参数配置",
      "properties": {
        "standardization": createStandardizationProperty(true),
        "technicalIndicators": {
          "type": "array",
          "label": "技术指标组合",
          "description": "选择一个或多个技术指标进行组合计算",
          "required": true,
          "default": [enumIds.technicalIndicator.rsi],
          "items": {
            "type": "string",
            "enum": [
              enumOption(enumIds.technicalIndicator.rsi, "相对强弱指数（RSI）", ["rsi"]),
              enumOption(enumIds.technicalIndicator.macd, "指数平滑异同平均线（MACD）", ["macd"]),
              enumOption(enumIds.technicalIndicator.ma, "移动平均线（MA）", ["ma"]),
              enumOption(enumIds.technicalIndicator.ema, "指数移动平均（EMA）", ["ema"]),
              enumOption(enumIds.technicalIndicator.boll, "布林带（BOLL）", ["boll"]),
              enumOption(enumIds.technicalIndicator.kdj, "随机指标（KDJ）", ["kdj"]),
              enumOption(enumIds.technicalIndicator.atr, "真实波幅（ATR）", ["atr"]),
              enumOption(enumIds.technicalIndicator.obv, "能量潮（OBV）", ["obv"]),
              enumOption(enumIds.technicalIndicator.vwap, "成交量加权平均价（VWAP）", ["vwap"]),
              enumOption(enumIds.technicalIndicator.volumeRatio, "量比", ["volume_ratio"]),
              enumOption(enumIds.technicalIndicator.turnoverStability, "换手率稳定性", ["turnover_stability"])
            ]
          }
        },
        "technicalPriceType": {
          "type": "string",
          "label": "价格字段",
          "description": "RSI、MACD、OBV 参考的价格字段",
          "enum": [
            enumOption(enumIds.technicalPriceType.close, "收盘价", ["close"]),
            enumOption(enumIds.technicalPriceType.open, "开盘价", ["open"]),
            enumOption(enumIds.technicalPriceType.high, "最高价", ["high"]),
            enumOption(enumIds.technicalPriceType.low, "最低价", ["low"])
          ],
          "default": enumIds.technicalPriceType.close
        },
        "rsiWindow": {
          "type": "integer",
          "label": "RSI窗口",
          "description": "RSI 计算窗口（天数）",
          "default": 14,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [6, 9, 14, 21]
        },
        "maWindow": {
          "type": "integer",
          "label": "MA窗口",
          "description": "MA 计算窗口（天数）",
          "default": 20,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 10, 20, 60, 120]
        },
        "emaWindow": {
          "type": "integer",
          "label": "EMA窗口",
          "description": "EMA 计算窗口（天数）",
          "default": 20,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 10, 20, 60, 120]
        },
        "bollWindow": {
          "type": "integer",
          "label": "BOLL窗口",
          "description": "布林带计算窗口",
          "default": 20,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [10, 20, 26, 60]
        },
        "bollStdDev": {
          "type": "number",
          "label": "BOLL标准差倍数",
          "description": "布林带上下轨标准差倍数",
          "default": 2.0,
          "minimum": 1.0,
          "maximum": 4.0,
          "step": 0.1,
          "unit": "倍",
          "decimals": 1
        },
        "kdjWindow": {
          "type": "integer",
          "label": "KDJ窗口",
          "description": "KDJ 计算窗口",
          "default": 9,
          "minimum": 5,
          "maximum": 120,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 9, 14, 21]
        },
        "kdjKPeriod": {
          "type": "integer",
          "label": "K值平滑周期",
          "description": "KDJ 中 K 值平滑周期",
          "default": 3,
          "minimum": 2,
          "maximum": 10,
          "step": 1,
          "unit": "天",
          "commonValues": [2, 3, 5]
        },
        "kdjDPeriod": {
          "type": "integer",
          "label": "D值平滑周期",
          "description": "KDJ 中 D 值平滑周期",
          "default": 3,
          "minimum": 2,
          "maximum": 10,
          "step": 1,
          "unit": "天",
          "commonValues": [2, 3, 5]
        },
        "atrWindow": {
          "type": "integer",
          "label": "ATR窗口",
          "description": "ATR 计算窗口",
          "default": 14,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [7, 14, 20, 26]
        },
        "macdFastPeriod": {
          "type": "integer",
          "label": "MACD快线周期",
          "description": "MACD 快线 EMA 周期",
          "default": 12,
          "minimum": 2,
          "maximum": 120,
          "step": 1,
          "unit": "天",
          "commonValues": [8, 12, 13]
        },
        "macdSlowPeriod": {
          "type": "integer",
          "label": "MACD慢线周期",
          "description": "MACD 慢线 EMA 周期",
          "default": 26,
          "minimum": 3,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [20, 26, 30]
        },
        "macdSignalPeriod": {
          "type": "integer",
          "label": "MACD信号线周期",
          "description": "MACD 信号线 EMA 周期",
          "default": 9,
          "minimum": 2,
          "maximum": 120,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 9]
        },
        "obvWindow": {
          "type": "integer",
          "label": "OBV窗口",
          "description": "OBV 斜率/变化率计算窗口",
          "default": 20,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [10, 20, 60]
        },
        "vwapWindow": {
          "type": "integer",
          "label": "VWAP窗口",
          "description": "VWAP 计算窗口",
          "default": 20,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 10, 20, 60]
        },
        "volumeRatioWindow": {
          "type": "integer",
          "label": "量比窗口",
          "description": "量比计算窗口",
          "default": 20,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 10, 20, 60]
        },
        "turnoverStabilityWindow": {
          "type": "integer",
          "label": "换手率稳定性窗口",
          "description": "换手率稳定性计算窗口",
          "default": 60,
          "minimum": 20,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [20, 60, 120, 250]
        },
        "turnoverStabilityMetric": {
          "type": "string",
          "label": "稳定性参考值",
          "description": "换手率稳定性参考字段",
          "enum": [
            enumOption(enumIds.liquidityMetric.turnoverRate, "换手率", ["turnover_rate"]),
            enumOption(enumIds.liquidityMetric.volume, "成交量", ["volume"]),
            enumOption(enumIds.liquidityMetric.amplitude, "振幅", ["amplitude"])
          ],
          "default": enumIds.liquidityMetric.turnoverRate
        },
        "technicalCombinationMode": {
          "type": "string",
          "label": "组合方式",
          "description": "多个技术指标的组合方式",
          "enum": [
            enumOption(enumIds.technicalCombinationMode.equalWeight, "等权平均", ["equal_weight"]),
            enumOption(enumIds.technicalCombinationMode.normalizedAverage, "标准化平均", ["normalized_average"])
          ],
          "default": enumIds.technicalCombinationMode.equalWeight
        }
      }
    },
    
    "macro": {
      "title": "宏观因子",
      "description": "宏观因子参数配置",
      "properties": {
        "standardization": createStandardizationProperty(true),
        "macroDimensions": {
          "type": "array",
          "label": "因子维度",
          "description": "选择宏观维度",
          "required": true,
          "default": [
            enumIds.macroDimension.growth,
            enumIds.macroDimension.inflation,
            enumIds.macroDimension.credit,
            enumIds.macroDimension.rates,
            enumIds.macroDimension.policy,
            enumIds.macroDimension.riskAppetite
          ],
          "items": {
            "type": "string",
            "enum": [
              enumOption(enumIds.macroDimension.growth, "经济增长", ["growth"]),
              enumOption(enumIds.macroDimension.inflation, "通货膨胀", ["inflation"]),
              enumOption(enumIds.macroDimension.credit, "货币信用", ["credit"]),
              enumOption(enumIds.macroDimension.rates, "利率水平", ["rates"]),
              enumOption(enumIds.macroDimension.policy, "政策环境", ["policy"]),
              enumOption(enumIds.macroDimension.riskAppetite, "风险偏好", ["risk_appetite"])
            ]
          }
        },
        "macroIndicators": {
          "type": "array",
          "label": "核心指标（推荐）",
          "description": "选择核心宏观指标",
          "required": true,
          "default": [
            enumIds.macroIndicator.industrialAddedValueYoy,
            enumIds.macroIndicator.cpiYoy,
            enumIds.macroIndicator.m2Yoy,
            enumIds.macroIndicator.tenYearBondYield,
            enumIds.macroIndicator.lpr1y,
            enumIds.macroIndicator.aaCreditSpread
          ],
          "items": {
            "type": "string",
            "enum": [
              enumOption(enumIds.macroIndicator.industrialAddedValueYoy, "工业增加值同比", ["industrial_added_value_yoy"]),
              enumOption(enumIds.macroIndicator.manufacturingPmi, "制造业采购经理指数（PMI）", ["manufacturing_pmi"]),
              enumOption(enumIds.macroIndicator.gdpYoy, "国内生产总值同比（GDP）", ["gdp_yoy"]),
              enumOption(enumIds.macroIndicator.cpiYoy, "居民消费价格指数同比（CPI）", ["cpi_yoy"]),
              enumOption(enumIds.macroIndicator.ppiYoy, "工业生产者出厂价格指数同比（PPI）", ["ppi_yoy"]),
              enumOption(enumIds.macroIndicator.m2Yoy, "广义货币同比（M2）", ["m2_yoy"]),
              enumOption(enumIds.macroIndicator.socialFinancingStockYoy, "社融存量同比", ["social_financing_stock_yoy"]),
              enumOption(enumIds.macroIndicator.m1M2Spread, "M1-M2剪刀差", ["m1_m2_spread"]),
              enumOption(enumIds.macroIndicator.tenYearBondYield, "10年国债收益率", ["ten_year_bond_yield"]),
              enumOption(enumIds.macroIndicator.shibor3m, "3个月上海银行间同业拆放利率（SHIBOR）", ["shibor_3m"]),
              enumOption(enumIds.macroIndicator.lpr1y, "1年期贷款市场报价利率（LPR）", ["lpr_1y"]),
              enumOption(enumIds.macroIndicator.reserveRequirementRatio, "存款准备金率", ["reserve_requirement_ratio"]),
              enumOption(enumIds.macroIndicator.aaCreditSpread, "AA信用利差", ["aa_credit_spread"]),
              enumOption(enumIds.macroIndicator.vixProxy, "波动率指数代理（VIX）", ["vix_proxy"])
            ]
          }
        },
        "macroFrequency": {
          "type": "string",
          "label": "宏观对齐频率",
          "description": "宏观指标对齐频率",
          "enum": [
            enumOption(enumIds.commonFrequency.daily, "日频", ["daily"]),
            enumOption(enumIds.commonFrequency.weekly, "周频", ["weekly"]),
            enumOption(enumIds.commonFrequency.monthly, "月频", ["monthly"]),
            enumOption(enumIds.commonFrequency.quarterly, "季频", ["quarterly"])
          ],
          "default": enumIds.commonFrequency.monthly
        },
        "macroWindow": {
          "type": "integer",
          "label": "观察周期",
          "description": "宏观观察周期",
          "default": 12,
          "minimum": 3,
          "maximum": 60,
          "step": 1,
          "unit": "期",
          "commonValues": [3, 6, 12, 24, 36]
        }
      }
    },

    "industry": {
      "title": "行业因子",
      "description": "行业因子参数配置",
      "properties": {
        "standardization": createStandardizationProperty(true),
        "sectorType": {
          "type": "string",
          "label": "行业分类标准",
          "description": "行业分类标准",
          "enum": [
            enumOption(enumIds.sectorType.swL1, "申万一级", ["sw_l1"]),
            enumOption(enumIds.sectorType.swL2, "申万二级", ["sw_l2"]),
            enumOption(enumIds.sectorType.citicL1, "中信一级", ["citic_l1"]),
            enumOption(enumIds.sectorType.citicL2, "中信二级", ["citic_l2"])
          ],
          "default": enumIds.sectorType.swL1
        },
        "industryMetric": {
          "type": "string",
          "label": "行业指标",
          "description": "行业因子类型",
          "enum": [
            enumOption(enumIds.industryMetric.industryProsperity, "行业景气度", ["industry_prosperity"]),
            enumOption(enumIds.industryMetric.industryMomentum, "行业动量", ["industry_momentum"]),
            enumOption(enumIds.industryMetric.industryConcentration, "行业集中度", ["industry_concentration"])
          ],
          "default": enumIds.industryMetric.industryMomentum
        },
        "window": {
          "type": "integer",
          "label": "观察窗口",
          "description": "行业因子回看窗口（天数）",
          "default": 60,
          "minimum": 20,
          "maximum": 750,
          "step": 1,
          "unit": "天",
          "commonValues": [20, 60, 120, 250, 750]
        }
      }
    },
    
    "liquidity": {
      "title": "流动性因子",
      "description": "流动性因子参数配置",
      "properties": {
        "standardization": createStandardizationProperty(true),
        "metric": {
          "type": "string",
          "label": "流动性指标",
          "description": "使用的流动性指标",
          "enum": [
            enumOption(enumIds.liquidityMetric.turnoverRate, "换手率", ["turnover_rate"]),
            enumOption(enumIds.liquidityMetric.amihudIlliquidity, "Amihud 非流动性指标", ["amihud_illiquidity"]),
            enumOption(enumIds.liquidityMetric.amplitude, "振幅", ["amplitude"]),
            enumOption(enumIds.liquidityMetric.volume, "成交量", ["volume"])
          ],
          "default": enumIds.liquidityMetric.turnoverRate
        },
        "window": {
          "type": "integer",
          "label": "流动性窗口",
          "description": "计算流动性的时间窗口（天数）",
          "default": 20,
          "minimum": 5,
          "maximum": 120,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 10, 20, 60]
        }
      }
    },
    
    "custom": {
      "title": "自定义因子",
      "description": "自定义因子参数配置",
      "properties": {
        "standardization": createStandardizationProperty(true),
        "expression": {
          "type": "string",
          "label": "表达式",
          "description": "因子计算表达式",
          "default": "",
          "placeholder": "例如: close / open - 1"
        },
        "variables": {
          "type": "array",
          "label": "变量定义",
          "description": "表达式变量绑定。可指定 field 映射真实数据字段，或仅指定 defaultValue 作为常量/缺失回退值",
          "default": [],
          "items": {
            "type": "object",
            "properties": {
              "name": {"type": "string"},
              "field": {"type": "string"},
              "description": {"type": "string"},
              "defaultValue": {"type": "number"}
            }
          }
        }
      }
    }
  }
};

// 加载因子参数配置
function loadFactorSchemas(callback) {
  if (cachedSchemas) {
    callback(cloneSchemasForCallback(cachedSchemas));
    return;
  }

  pendingSchemaCallbacks.push(callback);
  if (schemaLoadInProgress) {
    return;
  }

  schemaLoadInProgress = true;
  console.log("开始加载因子参数配置...");
  
  // 尝试多个可能的路径（优先使用qrc资源路径）
  var paths = [
    "qrc:/config/views/factor_schemas.json",
    "qrc:/config/views/factor_unified.json",
    "qrc:/config/views/factor_common_params.json"
  ];
  
  // 如果存在XMLHttpRequest对象，则尝试远程加载
  function tryLoadRemote(index) {
    if (index >= paths.length) {
      console.warn("所有远程路径都尝试失败，使用内置默认配置");
      flushSchemaCallbacks(defaultSchemas);
      return;
    }
    
    console.log("尝试加载远程路径:", paths[index]);
    
    try {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", paths[index], true);
      xhr.onreadystatechange = function() {
        if (xhr.readyState === XMLHttpRequest.DONE) {
          if (xhr.status === 200) {
            try {
              var schemas = JSON.parse(xhr.responseText);
              console.log("因子参数配置加载成功，路径:", paths[index]);
              console.log("包含因子类型:", Object.keys(schemas.factorSchemas || schemas.factorTypeSchemas || {}).length);
              flushSchemaCallbacks(schemas);
            } catch (e) {
              console.error("JSON解析失败:", e);
              tryLoadRemote(index + 1);
            }
          } else {
            console.log("远程路径加载失败，尝试下一个:", paths[index]);
            tryLoadRemote(index + 1);
          }
        }
      };
      xhr.onerror = function() {
        console.log("网络错误，尝试下一个:", paths[index]);
        tryLoadRemote(index + 1);
      };
      xhr.send();
    } catch (e) {
      console.error("XMLHttpRequest错误:", e);
      tryLoadRemote(index + 1);
    }
  }
  
  // 如果存在Qt对象，使用Qt的资源加载机制
  if (typeof Qt !== 'undefined' && Qt && Qt.include) {
    console.log("检测到Qt环境，尝试使用Qt资源加载...");
    // 在Qt/QML环境中，资源文件已经内置，直接使用默认配置
    flushSchemaCallbacks(defaultSchemas);
  } else {
    // 非Qt环境，尝试远程加载
    tryLoadRemote(0);
  }
}

// 获取通用参数配置
function getCommonParameters(schemas) {
  if (!schemas || !schemas.commonParameters) {
    console.warn("没有找到通用参数配置，返回空对象");
    return { properties: {} };
  }
  return schemas.commonParameters;
}

// 获取特定因子类型的参数配置
function getFactorSchema(schemas, factorType) {
  if (!schemas) {
    console.warn("没有找到因子类型配置:", factorType, "，返回空配置");
    return { properties: {} };
  }
  
  // 支持两种键名: factorSchemas 和 factorTypeSchemas
  var factorSchemasObj = schemas.factorSchemas || schemas.factorTypeSchemas;
  
  if (factorSchemasObj && factorSchemasObj[factorType]) {
    return factorSchemasObj[factorType];
  }

  if (schemas[factorType] && schemas[factorType].properties) {
    return schemas[factorType];
  }

  console.warn("没有找到因子类型配置:", factorType, "，返回空配置");
  console.log("可用的键:", Object.keys(schemas));
  return { properties: {} };
}

// 合并通用参数和特定参数
function getMergedSchema(schemas, factorType) {
  var commonSchema = getCommonParameters(schemas);
  var factorSchema = getFactorSchema(schemas, factorType);
  
  // 创建合并后的schema
  var mergedSchema = {
    title: factorSchema.title || factorType + "因子",
    description: factorSchema.description || "",
    properties: {}
  };
  
  // 先添加通用参数
  if (commonSchema.properties) {
    for (var key in commonSchema.properties) {
      mergedSchema.properties[key] = commonSchema.properties[key];
    }
  }
  
  // 再添加特定参数（会覆盖同名的通用参数）
  if (factorSchema.properties) {
    for (var key in factorSchema.properties) {
      mergedSchema.properties[key] = factorSchema.properties[key];
    }
  }
  
  console.log("合并后的schema - 类型:", factorType, "参数数量:", Object.keys(mergedSchema.properties).length);
  return mergedSchema;
}

// 获取所有可用的因子类型
function getAvailableFactorTypes(schemas) {
  if (!schemas) {
    console.warn("没有找到因子类型配置，返回空数组");
    return [];
  }
  
  // 支持两种键名: factorSchemas 和 factorTypeSchemas
  var factorSchemasObj = schemas.factorSchemas || schemas.factorTypeSchemas;
  
  if (!factorSchemasObj) {
    var fallbackTypes = [];
    for (var topLevelKey in schemas) {
      if (topLevelKey === "$schema" || topLevelKey === "title" || topLevelKey === "description" || topLevelKey === "version" || topLevelKey === "commonParameters") {
        continue;
      }

      var topLevelSchema = schemas[topLevelKey]
      if (topLevelSchema && topLevelSchema.properties) {
        fallbackTypes.push({
          id: topLevelKey,
          name: topLevelSchema.title || topLevelKey,
          description: topLevelSchema.description || ""
        })
      }
    }

    if (fallbackTypes.length > 0) {
      return fallbackTypes;
    }

    console.warn("没有找到因子类型配置，返回空数组");
    return [];
  }
  
  var types = [];
  for (var type in factorSchemasObj) {
    types.push({
      id: type,
      name: factorSchemasObj[type].title || type,
      description: factorSchemasObj[type].description || ""
    });
  }
  
  return types;
}

// 获取参数的默认值
function getDefaultValues(schema) {
  if (!schema || !schema.properties) {
    return {};
  }
  
  var defaults = {};
  for (var key in schema.properties) {
    var param = schema.properties[key];
    if (param.default !== undefined) {
      defaults[key] = param.default;
    }
  }
  
  return defaults;
}

// 验证参数值
function validateParameter(schema, key, value) {
  if (!schema || !schema.properties || !schema.properties[key]) {
    return { valid: false, message: "参数不存在" };
  }
  
  var param = schema.properties[key];
  
  // 必填验证
  if (param.required && (value === undefined || value === null || value === "")) {
    return { valid: false, message: param.label + " 不能为空" };
  }
  
  // 数值范围验证
  if ((param.type === "number" || param.type === "integer") && value !== undefined) {
    if (param.minimum !== undefined && value < param.minimum) {
      return { valid: false, message: param.label + " 不能小于 " + param.minimum };
    }
    if (param.maximum !== undefined && value > param.maximum) {
      return { valid: false, message: param.label + " 不能大于 " + param.maximum };
    }
  }
  
  // 枚举值验证
  if (param.enum && value !== undefined) {
    var enumValues = param.enum.map(function(option) {
      if (option && typeof option === "object") {
        return option.value !== undefined ? option.value : option.label
      }
      return option
    })
    if (!enumValues.includes(value)) {
      return { valid: false, message: param.label + " 必须是有效选项" };
    }
  }
  
  return { valid: true, message: "" };
}

// 验证所有参数
function validateAllParameters(schema, values) {
  if (!schema || !schema.properties) {
    return { valid: true, message: "没有参数需要验证" };
  }
  
  var errors = [];
  for (var key in schema.properties) {
    var validation = validateParameter(schema, key, values[key]);
    if (!validation.valid) {
      errors.push(validation.message);
    }
  }
  
  if (errors.length > 0) {
    return { valid: false, message: errors.join("; ") };
  }
  
  return { valid: true, message: "" };
}

// 导出函数
var FactorSchemaLoader = {
  loadFactorSchemas: loadFactorSchemas,
  getCommonParameters: getCommonParameters,
  getFactorSchema: getFactorSchema,
  getMergedSchema: getMergedSchema,
  getAvailableFactorTypes: getAvailableFactorTypes,
  getDefaultValues: getDefaultValues,
  validateParameter: validateParameter,
  validateAllParameters: validateAllParameters,
  enumIds: enumIds,
  defaultSchemas: defaultSchemas
};

// QML环境中，顶层变量自动可用，无需额外导出
// 只在不使用QML的浏览器环境中尝试导出到window对象
// QML环境中没有window对象（只有Qt对象），浏览器环境中有window对象
// 简单判断：有window对象且有document对象就是浏览器环境
var isBrowserEnvironment = typeof window !== 'undefined' && window && 
                          typeof document !== 'undefined' && 
                          typeof XMLHttpRequest !== 'undefined';

if (isBrowserEnvironment) {
  try {
    window.FactorSchemaLoader = FactorSchemaLoader;
  } catch (e) {
    // 忽略导出错误
  }
}

// Node.js/CommonJS 模块导出
if (typeof module !== 'undefined' && module.exports) {
  module.exports = FactorSchemaLoader;
}
 
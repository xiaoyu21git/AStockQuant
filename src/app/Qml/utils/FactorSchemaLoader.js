// FactorSchemaLoader.js
// 因子参数配置加载器 - 基于 JSON Schema 的动态表单系统

/**
 * 因子参数配置加载器
 * 提供统一的 JSON Schema 配置加载和管理功能
 */

var cachedSchemas = null;
var schemaLoadInProgress = false;
var pendingSchemaCallbacks = [];

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
        "enum": ["日频", "周频", "月频", "季频", "年频"],
        "default": "日频"
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
      "standardization": {
        "type": "string",
        "label": "标准化方法",
        "description": "因子值的标准化处理方法",
        "enum": ["Z-Score", "Min-Max", "Rank", "None"],
        "default": "Z-Score"
      },
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
        "lookbackWindow": {
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
        "method": {
          "type": "string",
          "label": "计算方法",
          "description": "动量计算方法",
          "enum": ["简单动量", "加权动量", "残差动量"],
          "default": "简单动量"
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
          "default": ["bp", "ep"],
          "items": {
            "type": "string",
            "enum": [
              {"value": "bp", "label": "BP（市净率倒数）"},
              {"value": "ep", "label": "EP（市盈率倒数）"},
              {"value": "dividend_yield", "label": "股息率（TTM）"},
              {"value": "cf_p", "label": "CF/P（现金流市值比）"}
            ]
          }
        },
        "bpWeight": {
          "type": "number",
          "label": "BP权重",
          "description": "BP（市净率倒数）在价值组合中的权重",
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
          "label": "EP权重",
          "description": "EP（市盈率倒数）在价值组合中的权重",
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
          "description": "股息率在价值组合中的权重",
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
          "label": "CF/P权重",
          "description": "CF/P 在价值组合中的权重",
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
        "qualityMetrics": {
          "type": "array",
          "label": "质量指标",
          "description": "使用的质量指标",
          "default": ["净资产收益率", "总资产收益率"],
          "items": {
            "type": "string",
            "enum": ["净资产收益率", "总资产收益率", "毛利率", "营业利润率"]
          }
        }
      }
    },
    
    "growth": {
      "title": "成长因子",
      "description": "成长因子参数配置",
      "properties": {
        "growthMetrics": {
          "type": "array",
          "label": "成长指标",
          "description": "使用的成长指标",
          "default": ["revenue_growth", "net_profit_growth", "delta_roe", "sue"],
          "items": {
            "type": "string",
            "enum": [
              {"value": "revenue_growth", "label": "营收增速"},
              {"value": "net_profit_growth", "label": "单季净利同比增速"},
              {"value": "delta_roe", "label": "DELTAROE（ROE同比变化）"},
              {"value": "sue", "label": "SUE（标准化预期外盈利）"}
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
          "label": "DELTAROE权重",
          "description": "DELTAROE（ROE同比变化）在成长组合中的权重，四项合计为 100",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0
        },
        "sueWeight": {
          "type": "number",
          "label": "SUE权重",
          "description": "SUE（标准化预期外盈利）在成长组合中的权重，四项合计为 100",
          "default": 25,
          "minimum": 0,
          "maximum": 100,
          "step": 1,
          "unit": "%",
          "decimals": 0
        }
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
          "enum": ["总市值", "流通市值", "总资产"],
          "default": "流通市值"
        }
      }
    },
    
    "low_volatility": {
      "title": "低波因子",
      "description": "低波因子参数配置",
      "properties": {
        "volatilityWindow": {
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
          "default": ["volatility", "drawdown", "beta"],
          "items": {
            "type": "string",
            "enum": [
              {"value": "volatility", "label": "波动率倒数"},
              {"value": "drawdown", "label": "最大回撤倒数"},
              {"value": "beta", "label": "Beta倒数"}
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
          "label": "Beta权重",
          "description": "Beta倒数在低波组合中的权重，三项合计为 100",
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
        "dividendMetrics": {
          "type": "array",
          "label": "红利核心指标",
          "description": "红利策略核心指标，支持多选",
          "required": true,
          "default": ["dividend_yield"],
          "items": {
            "type": "string",
            "enum": [
              {"value": "dividend_yield", "label": "股息率"},
              {"value": "dividend_stability", "label": "分红稳定性"},
              {"value": "payout_ratio", "label": "股利支付率"}
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
        "sentimentSource": {
          "type": "string",
          "label": "情绪数据源",
          "description": "情绪数据来源",
          "enum": ["新闻情绪", "社交媒体", "分析师评级", "市场情绪"],
          "default": "新闻情绪"
        },
        "sentimentWindow": {
          "type": "integer",
          "label": "情绪窗口",
          "description": "情绪数据计算窗口（天数）",
          "default": 20,
          "minimum": 1,
          "maximum": 60,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 10, 20, 30]
        },
        "sentimentWeight": {
          "type": "number",
          "label": "情绪权重",
          "description": "情绪因子在综合评分中的权重",
          "default": 0.3,
          "minimum": 0.0,
          "maximum": 1.0,
          "step": 0.05,
          "unit": "%"
        }
      }
    },
    
    "technical": {
      "title": "技术因子",
      "description": "技术因子参数配置",
      "properties": {
        "technicalIndicators": {
          "type": "array",
          "label": "技术指标组合",
          "description": "选择一个或多个技术指标进行组合计算",
          "required": true,
          "default": ["rsi"],
          "items": {
            "type": "string",
            "enum": [
              {"value": "rsi", "label": "RSI（相对强弱指数）"},
              {"value": "macd", "label": "MACD（指数平滑异同平均线）"},
              {"value": "ma", "label": "MA（移动平均）"},
              {"value": "ema", "label": "EMA（指数移动平均）"},
              {"value": "boll", "label": "BOLL（布林带）"},
              {"value": "kdj", "label": "KDJ（随机指标）"},
              {"value": "atr", "label": "ATR（真实波幅）"},
              {"value": "obv", "label": "OBV（能量潮）"},
              {"value": "vwap", "label": "VWAP（成交量加权平均价）"},
              {"value": "volume_ratio", "label": "量比"},
              {"value": "turnover_stability", "label": "换手率稳定性"}
            ]
          }
        },
        "technicalPriceType": {
          "type": "string",
          "label": "价格字段",
          "description": "RSI、MACD、OBV 参考的价格字段",
          "enum": [
            {"value": "close", "label": "收盘价"},
            {"value": "open", "label": "开盘价"},
            {"value": "high", "label": "最高价"},
            {"value": "low", "label": "最低价"}
          ],
          "default": "close"
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
            {"value": "turnover_rate", "label": "换手率"},
            {"value": "turnover", "label": "成交额"},
            {"value": "volume", "label": "成交量"}
          ],
          "default": "turnover_rate"
        },
        "technicalCombinationMode": {
          "type": "string",
          "label": "组合方式",
          "description": "多个技术指标的组合方式",
          "enum": [
            {"value": "equal_weight", "label": "等权平均"},
            {"value": "normalized_average", "label": "标准化平均"}
          ],
          "default": "equal_weight"
        }
      }
    },
    
    "macro": {
      "title": "宏观因子",
      "description": "宏观因子参数配置",
      "properties": {
        "macroDimensions": {
          "type": "array",
          "label": "因子维度",
          "description": "选择宏观维度",
          "required": true,
          "default": ["growth", "inflation", "credit", "rates", "policy", "risk_appetite"],
          "items": {
            "type": "string",
            "enum": [
              {"value": "growth", "label": "经济增长"},
              {"value": "inflation", "label": "通货膨胀"},
              {"value": "credit", "label": "货币信用"},
              {"value": "rates", "label": "利率水平"},
              {"value": "policy", "label": "政策环境"},
              {"value": "risk_appetite", "label": "风险偏好"}
            ]
          }
        },
        "macroIndicators": {
          "type": "array",
          "label": "核心指标（推荐）",
          "description": "选择核心宏观指标",
          "required": true,
          "default": ["industrial_added_value_yoy", "cpi_yoy", "m2_yoy", "ten_year_bond_yield", "lpr_1y", "aa_credit_spread"],
          "items": {
            "type": "string",
            "enum": [
              {"value": "industrial_added_value_yoy", "label": "工业增加值同比"},
              {"value": "manufacturing_pmi", "label": "制造业PMI"},
              {"value": "gdp_yoy", "label": "GDP同比"},
              {"value": "cpi_yoy", "label": "CPI同比"},
              {"value": "ppi_yoy", "label": "PPI同比"},
              {"value": "m2_yoy", "label": "M2同比"},
              {"value": "social_financing_stock_yoy", "label": "社融存量同比"},
              {"value": "m1_m2_spread", "label": "M1-M2剪刀差"},
              {"value": "ten_year_bond_yield", "label": "10年国债收益率"},
              {"value": "shibor_3m", "label": "3M SHIBOR"},
              {"value": "lpr_1y", "label": "1年期LPR"},
              {"value": "reserve_requirement_ratio", "label": "存款准备金率"},
              {"value": "aa_credit_spread", "label": "AA信用利差"},
              {"value": "vix_proxy", "label": "VIX代理"}
            ]
          }
        },
        "macroFrequency": {
          "type": "string",
          "label": "宏观对齐频率",
          "description": "宏观指标对齐频率",
          "enum": [
            {"value": "daily", "label": "日频"},
            {"value": "weekly", "label": "周频"},
            {"value": "monthly", "label": "月频"},
            {"value": "quarterly", "label": "季频"}
          ],
          "default": "monthly"
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
        "sectorType": {
          "type": "string",
          "label": "行业分类标准",
          "description": "行业分类标准",
          "enum": ["申万一级", "申万二级", "中信一级", "中信二级"],
          "default": "申万一级"
        },
        "industryMetric": {
          "type": "string",
          "label": "行业指标",
          "description": "行业因子类型",
          "enum": [
            {"value": "industry_prosperity", "label": "行业景气度"},
            {"value": "industry_momentum", "label": "行业动量"},
            {"value": "industry_concentration", "label": "行业集中度"}
          ],
          "default": "industry_momentum"
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
        "liquidityMetric": {
          "type": "string",
          "label": "流动性指标",
          "description": "使用的流动性指标",
          "enum": ["换手率", "Amihud非流动性", "买卖价差", "成交量"],
          "default": "换手率"
        },
        "liquidityWindow": {
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
    if (!param.enum.includes(value)) {
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
 
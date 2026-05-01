// FactorDefaultParams.js
// 因子参数的内置默认配置
// 当外部JSON配置加载失败时使用

function getDefaultMeta() {
    return {
        "id": "factor_common_params",
        "title": "因子通用参数配置（内置）",
        "description": "因子构建的通用核心参数配置定义",
        
        "commonParams": {
            "lookbackPeriod": {
                "name": "lookback_period",
                "displayName": "回溯窗口",
                "description": "计算因子值所需的通用历史数据长度（与各因子专属观察周期独立）",
                "type": "object",
                "properties": {
                    "value": {
                        "name": "value",
                        "description": "回溯窗口的长度（天数）",
                        "type": "integer",
                        "default": 252,
                        "min": 1,
                        "max": 1000,
                        "step": 1,
                        "commonValues": [20, 60, 120, 252]
                    },
                    "unit": {
                        "name": "unit",
                        "displayName": "时间单位",
                        "description": "回溯窗口的时间单位",
                        "type": "enum",
                        "default": "days",
                        "options": [
                            {"value": "days", "label": "交易日"},
                            {"value": "weeks", "label": "周"},
                            {"value": "months", "label": "月"},
                            {"value": "years", "label": "年"}
                        ]
                    }
                }
            },
            
            "standardization": {
                "name": "standardization",
                "displayName": "标准化/去极值",
                "description": "消除量纲影响和极端异常值对模型的影响",
                "type": "object",
                "properties": {
                    "enabled": {
                        "name": "enabled",
                        "displayName": "启用标准化",
                        "description": "是否对因子值进行标准化处理",
                        "type": "boolean",
                        "default": true
                    },
                    "method": {
                        "name": "method",
                        "displayName": "标准化方法",
                        "description": "使用的标准化方法",
                        "type": "enum",
                        "default": "zscore",
                        "options": [
                            {"value": "zscore", "label": "Z-Score标准化"},
                            {"value": "median_winsorize", "label": "中位数去极值"},
                            {"value": "winsorize", "label": "缩尾处理"},
                            {"value": "rank", "label": "排序标准化"},
                            {"value": "none", "label": "不进行标准化"}
                        ]
                    },
                    "winsorizePercentile": {
                        "name": "winsorize_percentile",
                        "displayName": "缩尾百分位",
                        "description": "缩尾处理的百分位阈值",
                        "type": "float",
                        "default": 0.01,
                        "min": 0.0,
                        "max": 0.5,
                        "step": 0.005
                    },
                    "crossSectional": {
                        "name": "cross_sectional",
                        "displayName": "横截面标准化",
                        "description": "是否在每个时间点进行横截面标准化",
                        "type": "boolean",
                        "default": true
                    }
                }
            },
            
            "neutralization": {
                "name": "neutralization",
                "displayName": "中性化处理",
                "description": "剔除行业、市值等风格对因子的干扰",
                "type": "object",
                "properties": {
                    "enabled": {
                        "name": "enabled",
                        "displayName": "启用中性化",
                        "description": "是否对因子进行中性化处理",
                        "type": "boolean",
                        "default": true
                    },
                    "factors": {
                        "name": "factors",
                        "displayName": "中性化因子",
                        "description": "需要中性化的风格因子",
                        "type": "array",
                        "default": ["industry", "market_cap"],
                        "options": [
                            {"value": "industry", "label": "行业"},
                            {"value": "market_cap", "label": "市值"},
                            {"value": "momentum", "label": "动量"},
                            {"value": "value", "label": "价值"}
                        ]
                    },
                    "method": {
                        "name": "method",
                        "displayName": "中性化方法",
                        "description": "中性化处理的方法",
                        "type": "enum",
                        "default": "regression",
                        "options": [
                            {"value": "regression", "label": "回归法"},
                            {"value": "group_mean", "label": "组内均值调整"},
                            {"value": "rank_neutral", "label": "排序中性化"}
                        ]
                    }
                }
            },
            
            "rebalancing": {
                "name": "rebalancing",
                "displayName": "调仓频率",
                "description": "多久更新一次因子值和持仓",
                "type": "object",
                "properties": {
                    "frequency": {
                        "name": "frequency",
                        "displayName": "调仓频率",
                        "description": "因子调仓的频率",
                        "type": "enum",
                        "default": "monthly",
                        "options": [
                            {"value": "daily", "label": "日频"},
                            {"value": "weekly", "label": "周频"},
                            {"value": "monthly", "label": "月频"},
                            {"value": "quarterly", "label": "季度频"}
                        ]
                    }
                }
            }
        },
        
        "factorTypeSpecificParams": {
            "value": {
                "description": "价值因子特定参数",
                "params": {
                    "valuationMetrics": {
                        "name": "valuation_metrics",
                        "displayName": "价值指标",
                        "description": "选择价值因子代表指标",
                        "type": "array",
                        "default": ["bp", "ep"],
                        "options": [
                            {"value": "bp", "label": "BP（市净率倒数）"},
                            {"value": "ep", "label": "EP（市盈率倒数）"},
                            {"value": "dividend_yield", "label": "股息率（TTM）"},
                            {"value": "cf_p", "label": "CF/P（现金流市值比）"}
                        ]
                    }
                }
            },
            
            "dividend": {
                "description": "红利因子特定参数",
                "params": {
                    "dividendMetrics": {
                        "name": "dividend_metrics",
                        "displayName": "红利核心指标",
                        "description": "选择红利策略核心指标，可多选",
                        "type": "array",
                        "default": ["dividend_yield"],
                        "options": [
                            {"value": "dividend_yield", "label": "股息率（TTM 250天或3年平均）"},
                            {"value": "dividend_stability", "label": "分红稳定性（过去3-5年，750-1250天）"},
                            {"value": "payout_ratio", "label": "股利支付率（TTM 250天）"}
                        ]
                    },
                    "minDividendYield": {
                        "name": "min_dividend_yield",
                        "displayName": "最低股息率",
                        "description": "股息率筛选阈值（%）",
                        "type": "number",
                        "default": 2.0,
                        "minValue": 0,
                        "maxValue": 20,
                        "stepValue": 0.1
                    }
                }
            },
            
            "momentum": {
                "description": "动量因子特定参数",
                "params": {
                    "window": {
                        "name": "window",
                        "displayName": "动量窗口",
                        "description": "计算动量的时间窗口（天数）",
                        "type": "integer",
                        "default": 60,
                        "min": 5,
                        "max": 250,
                        "step": 1,
                        "commonValues": [20, 60, 120, 250]
                    },
                    "skipRecent": {
                        "name": "skip_recent",
                        "displayName": "跳过近期",
                        "description": "跳过最近N天的数据（避免反转效应）",
                        "type": "integer",
                        "default": 20,
                        "min": 0,
                        "max": 60,
                        "step": 1
                    },
                    "type": {
                        "name": "type",
                        "displayName": "动量类型",
                        "description": "动量计算类型",
                        "type": "enum",
                        "default": "simple",
                        "options": [
                            {"value": "simple", "label": "简单动量"},
                            {"value": "exponential", "label": "指数加权动量"},
                            {"value": "rank", "label": "排序动量"}
                        ]
                    }
                }
            },
            
            "quality": {
                "description": "质量因子特定参数",
                "params": {
                    "metrics": {
                        "name": "metrics",
                        "displayName": "质量指标",
                        "description": "使用的质量指标",
                        "type": "array",
                        "default": ["roe", "roa"],
                        "options": [
                            {"value": "roe", "label": "净资产收益率（ROE）"},
                            {"value": "roa", "label": "总资产收益率（ROA）"},
                            {"value": "gross_margin", "label": "毛利率"},
                            {"value": "operating_margin", "label": "营业利润率"}
                        ]
                    }
                }
            },
            
            "growth": {
                "description": "成长因子特定参数",
                "params": {
                    "growthMetrics": {
                        "name": "growth_metrics",
                        "displayName": "成长指标",
                        "description": "使用的成长指标",
                        "type": "array",
                        "default": ["revenue_growth", "net_profit_growth", "delta_roe", "sue"],
                        "options": [
                            {"value": "revenue_growth", "label": "营收增速"},
                            {"value": "net_profit_growth", "label": "单季净利同比增速"},
                            {"value": "delta_roe", "label": "DELTAROE（ROE同比变化）"},
                            {"value": "sue", "label": "SUE（标准化预期外盈利）"}
                        ]
                    },
                    "revenueGrowthWeight": {
                        "name": "revenue_growth_weight",
                        "displayName": "营收增速权重",
                        "description": "四项合计为 100",
                        "type": "integer",
                        "default": 25,
                        "minValue": 0,
                        "maxValue": 100,
                        "stepValue": 1
                    },
                    "netProfitGrowthWeight": {
                        "name": "net_profit_growth_weight",
                        "displayName": "单季净利同比增速权重",
                        "description": "四项合计为 100",
                        "type": "integer",
                        "default": 25,
                        "minValue": 0,
                        "maxValue": 100,
                        "stepValue": 1
                    },
                    "deltaRoeWeight": {
                        "name": "delta_roe_weight",
                        "displayName": "DELTAROE权重",
                        "description": "四项合计为 100",
                        "type": "integer",
                        "default": 25,
                        "minValue": 0,
                        "maxValue": 100,
                        "stepValue": 1
                    },
                    "sueWeight": {
                        "name": "sue_weight",
                        "displayName": "SUE权重",
                        "description": "四项合计为 100",
                        "type": "integer",
                        "default": 25,
                        "minValue": 0,
                        "maxValue": 100,
                        "stepValue": 1
                    }
                }
            },

            "macro": {
                "description": "宏观因子特定参数",
                "params": {
                    "macroDimensions": {
                        "name": "macro_dimensions",
                        "displayName": "因子维度",
                        "description": "选择参与宏观组合的维度",
                        "type": "array",
                        "default": ["growth", "inflation", "credit", "rates", "policy", "risk_appetite"],
                        "options": [
                            {"value": "growth", "label": "经济增长"},
                            {"value": "inflation", "label": "通货膨胀"},
                            {"value": "credit", "label": "货币信用"},
                            {"value": "rates", "label": "利率水平"},
                            {"value": "policy", "label": "政策环境"},
                            {"value": "risk_appetite", "label": "风险偏好"}
                        ]
                    },
                    "macroIndicators": {
                        "name": "macro_indicators",
                        "displayName": "核心指标（推荐）",
                        "description": "建议优先选择与所选维度匹配的宏观指标",
                        "type": "array",
                        "default": ["industrial_added_value_yoy", "cpi_yoy", "m2_yoy", "ten_year_bond_yield", "lpr_1y", "aa_credit_spread"],
                        "options": [
                            {"value": "industrial_added_value_yoy", "label": "经济增长 - 工业增加值同比 | 月频 | Z-Score/同比变化"},
                            {"value": "manufacturing_pmi", "label": "经济增长 - 制造业PMI | 月频 | 水平值，50为扩张阈值"},
                            {"value": "gdp_yoy", "label": "经济增长 - GDP同比 | 季频 | 同比变化/对齐插值"},
                            {"value": "cpi_yoy", "label": "通货膨胀 - CPI同比 | 月频 | 同比变化率"},
                            {"value": "ppi_yoy", "label": "通货膨胀 - PPI同比 | 月频 | 同比变化率"},
                            {"value": "m2_yoy", "label": "货币信用 - M2同比增速 | 月频 | 一阶差分/加速度"},
                            {"value": "social_financing_stock_yoy", "label": "货币信用 - 社融存量同比 | 月频 | 同比/加速度"},
                            {"value": "m1_m2_spread", "label": "货币信用 - M1-M2剪刀差 | 月频 | 差值/扩散度"},
                            {"value": "ten_year_bond_yield", "label": "利率水平 - 10年期国债收益率 | 日/月频 | 水平值/月均值"},
                            {"value": "shibor_3m", "label": "利率水平 - SHIBOR(3M) | 日频 | 水平值/移动均值"},
                            {"value": "lpr_1y", "label": "政策环境 - LPR(1Y) | 月频 | 变化点/事件驱动"},
                            {"value": "reserve_requirement_ratio", "label": "政策环境 - 存款准备金率 | 不定期 | 变化点/事件驱动"},
                            {"value": "aa_credit_spread", "label": "风险偏好 - 信用利差(AA-国债) | 日/月频 | 收窄代表风险偏好提升"},
                            {"value": "vix_proxy", "label": "风险偏好 - VIX/波动率代理 | 日频 | 标准化后反向处理"}
                        ]
                    },
                    "macroFrequency": {
                        "name": "macro_frequency",
                        "displayName": "宏观对齐频率",
                        "description": "仅用于宏观指标与市场序列的对齐，和通用频率共享同一枚举值，不影响通用回溯窗口",
                        "type": "enum",
                        "default": "monthly",
                        "options": [
                            {"value": "daily", "label": "日频"},
                            {"value": "weekly", "label": "周频"},
                            {"value": "monthly", "label": "月频"},
                            {"value": "quarterly", "label": "季频"}
                        ]
                    },
                    "macroWindow": {
                        "name": "macro_window",
                        "displayName": "观察周期",
                        "description": "宏观指标平滑/对齐周期，不与通用回溯窗口共用",
                        "type": "integer",
                        "default": 12,
                        "min": 3,
                        "max": 60,
                        "step": 1,
                        "commonValues": [3, 6, 12, 24, 36]
                    }
                }
            },
            
            "size": {
                "description": "规模因子特定参数",
                "params": {
                    "sizeMetric": {
                        "name": "size_metric",
                        "displayName": "规模指标",
                        "description": "使用的规模指标",
                        "type": "enum",
                        "default": "float_cap",
                        "options": [
                            {"value": "market_cap", "label": "总市值"},
                            {"value": "float_cap", "label": "流通市值"},
                            {"value": "total_assets", "label": "总资产"}
                        ]
                    }
                }
            }
        }
    }
}

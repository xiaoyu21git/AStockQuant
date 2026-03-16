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
                "description": "计算因子值所需的历史数据长度",
                "type": "object",
                "properties": {
                    "value": {
                        "name": "value",
                        "displayName": "窗口长度",
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
                        "displayName": "估值指标",
                        "description": "使用的估值指标",
                        "type": "array",
                        "default": ["pe_ttm", "pb"],
                        "options": [
                            {"value": "pe_ttm", "label": "市盈率（TTM）"},
                            {"value": "pb", "label": "市净率"},
                            {"value": "ps", "label": "市销率"},
                            {"value": "dividend_yield", "label": "股息率"}
                        ]
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
                        "default": ["revenue_growth", "earnings_growth"],
                        "options": [
                            {"value": "revenue_growth", "label": "营收增长率"},
                            {"value": "earnings_growth", "label": "盈利增长率"},
                            {"value": "eps_growth", "label": "每股收益增长率"}
                        ]
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
                        "default": "market_cap",
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

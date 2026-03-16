// FactorSchemaLoader.js
// 因子参数配置加载器 - 基于 JSON Schema 的动态表单系统

/**
 * 因子参数配置加载器
 * 提供统一的 JSON Schema 配置加载和管理功能
 */

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
      "lookbackPeriod": {
        "type": "integer",
        "label": "回溯窗口",
        "description": "计算因子值所需的历史数据长度",
        "default": 252,
        "minimum": 1,
        "maximum": 1000,
        "step": 1,
        "unit": "天",
        "commonValues": [20, 60, 120, 252]
      },
      "frequency": {
        "type": "string",
        "label": "数据频率",
        "description": "因子计算的数据频率",
        "enum": ["日频", "周频", "月频", "季频", "年频"],
        "default": "日频"
      },
      "standardization": {
        "type": "string",
        "label": "标准化方法",
        "description": "因子值的标准化处理方法",
        "enum": ["Z-Score", "Min-Max", "Rank", "None"],
        "default": "Z-Score"
      },
      "neutralization": {
        "type": "boolean",
        "label": "中性化处理",
        "description": "是否进行行业中性化处理",
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
          "label": "估值指标",
          "description": "使用的估值指标",
          "default": ["pe_ttm", "pb"],
          "items": {
            "type": "string",
            "enum": ["pe_ttm", "pb", "ps", "dividend_yield"]
          }
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
          "default": ["roe", "roa"],
          "items": {
            "type": "string",
            "enum": ["roe", "roa", "gross_margin", "operating_margin"]
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
          "default": ["revenue_growth", "earnings_growth"],
          "items": {
            "type": "string",
            "enum": ["revenue_growth", "earnings_growth", "eps_growth"]
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
          "enum": ["market_cap", "float_cap", "total_assets"],
          "default": "market_cap"
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
        "volatilityType": {
          "type": "string",
          "label": "波动率类型",
          "description": "波动率计算方法",
          "enum": ["历史波动率", "下行波动率", "已实现波动率"],
          "default": "历史波动率"
        }
      }
    },
    
    "dividend": {
      "title": "红利因子",
      "description": "红利因子参数配置",
      "properties": {
        "dividendType": {
          "type": "string",
          "label": "红利类型",
          "description": "红利计算类型",
          "enum": ["股息率", "股息支付率", "股息稳定性"],
          "default": "股息率"
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
        "indicatorType": {
          "type": "string",
          "label": "指标类型",
          "description": "技术指标类型",
          "enum": ["趋势指标", "动量指标", "波动率指标", "成交量指标"],
          "default": "趋势指标"
        },
        "indicatorWindow": {
          "type": "integer",
          "label": "指标窗口",
          "description": "技术指标计算窗口（天数）",
          "default": 20,
          "minimum": 5,
          "maximum": 250,
          "step": 1,
          "unit": "天",
          "commonValues": [5, 10, 20, 60, 120]
        }
      }
    },
    
    "macro_sector": {
      "title": "宏观/行业因子",
      "description": "宏观/行业因子参数配置",
      "properties": {
        "sectorType": {
          "type": "string",
          "label": "行业类型",
          "description": "行业分类标准",
          "enum": ["申万一级", "申万二级", "中信一级", "中信二级"],
          "default": "申万一级"
        },
        "macroFactor": {
          "type": "string",
          "label": "宏观因子",
          "description": "宏观因子类型",
          "enum": ["利率敏感度", "通胀敏感度", "经济增长敏感度"],
          "default": "利率敏感度"
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
          "description": "表达式中使用的变量定义",
          "default": [],
          "items": {
            "type": "object",
            "properties": {
              "name": {"type": "string"},
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
  console.log("开始加载因子参数配置...");
  
  // 尝试多个可能的路径
  var paths = [
    "qrc:/config/views/factor_schemas.json",
    "file:///config/views/factor_schemas.json",
    "../../../../config/views/factor_schemas.json",
    "../../../config/views/factor_schemas.json",
    "../../config/views/factor_schemas.json"
  ];
  
  function tryLoad(index) {
    if (index >= paths.length) {
      console.warn("所有路径都尝试失败，使用内置默认配置");
      callback(defaultSchemas);
      return;
    }
    
    console.log("尝试加载路径:", paths[index]);
    
    var xhr = new XMLHttpRequest();
    xhr.open("GET", paths[index], true);
    xhr.onreadystatechange = function() {
      if (xhr.readyState === XMLHttpRequest.DONE) {
        if (xhr.status === 200) {
          try {
            var schemas = JSON.parse(xhr.responseText);
            console.log("因子参数配置加载成功，路径:", paths[index]);
            console.log("包含因子类型:", Object.keys(schemas.factorSchemas || schemas.factorTypeSchemas || {}).length);
            callback(schemas);
          } catch (e) {
            console.error("JSON解析失败:", e);
            tryLoad(index + 1);
          }
        } else {
          console.log("路径加载失败，尝试下一个:", paths[index]);
          tryLoad(index + 1);
        }
      }
    };
    xhr.onerror = function() {
      console.log("网络错误，尝试下一个:", paths[index]);
      tryLoad(index + 1);
    };
    xhr.send();
  }
  
  tryLoad(0);
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
  
  if (!factorSchemasObj || !factorSchemasObj[factorType]) {
    console.warn("没有找到因子类型配置:", factorType, "，返回空配置");
    console.log("可用的键:", Object.keys(schemas));
    return { properties: {} };
  }
  return factorSchemasObj[factorType];
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

// 导出为全局对象
if (typeof window !== 'undefined') {
  window.FactorSchemaLoader = FactorSchemaLoader;
}

// 导出为模块
if (typeof module !== 'undefined' && module.exports) {
  module.exports = FactorSchemaLoader;
}
 
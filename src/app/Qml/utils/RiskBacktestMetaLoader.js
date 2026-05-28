// RiskBacktestMetaLoader.js
// 用于动态加载共享风险/回测元数据与回测页专属元数据
// 支持缓存、错误处理和加载状态管理

.pragma library

var sharedMetaPath = "qrc:/config/views/risk_backtest_params.json";
var metaCache = {};
var loadingCallbacks = {};
var embeddedMetaByPath = {
    "qrc:/config/views/risk_backtest_params.json": {
        riskManagement: {
            properties: {
                stopLossPercent: {
                    label: "止损比例",
                    description: "单个头寸的最大亏损比例，达到阈值后执行止损。",
                    type: "number",
                    min: 1,
                    max: 50,
                    step: 0.5,
                    default: 10,
                    unit: "%",
                    group: "基础风险控制"
                },
                takeProfitPercent: {
                    label: "止盈比例",
                    description: "单个头寸的目标盈利比例，达到阈值后执行止盈。",
                    type: "number",
                    min: 5,
                    max: 200,
                    step: 1,
                    default: 20,
                    unit: "%",
                    group: "基础风险控制"
                },
                maxDrawdownLimit: {
                    label: "最大回撤限制",
                    description: "组合回测允许承受的最大回撤比例。",
                    type: "number",
                    min: 1,
                    max: 50,
                    step: 1,
                    default: 20,
                    unit: "%",
                    group: "基础风险控制"
                },
                maxTotalExposure: {
                    label: "最大总仓位",
                    description: "限制组合整体风险暴露，控制总持仓占资金比例。",
                    type: "number",
                    min: 10,
                    max: 100,
                    step: 1,
                    default: 67,
                    unit: "%",
                    group: "组合层风险"
                },
                maxPositionPercent: {
                    label: "单票集中度上限",
                    description: "限制单一个股最大持仓比例。",
                    type: "number",
                    min: 5,
                    max: 25,
                    step: 1,
                    default: 15,
                    unit: "%",
                    group: "持仓层风险"
                },
                autoStopEnabled: {
                    label: "启用自动止损",
                    description: "关闭后回测不会按止损比例自动触发止损。",
                    type: "boolean",
                    default: true,
                    group: "基础风险控制"
                },
                positionSizingMethod: {
                    label: "仓位管理方法",
                    description: "回测中使用的仓位分配方式。",
                    type: "select",
                    default: "fixed",
                    options: [
                        { value: "fixed", label: "固定比例" },
                        { value: "kelly", label: "凯利公式" },
                        { value: "equalWeight", label: "等权重" },
                        { value: "riskParity", label: "风险平价" }
                    ],
                    group: "仓位管理"
                },
                varWarningPercent: {
                    label: "VaR 预警阈值",
                    description: "当组合风险利用率接近阈值时给出预警。",
                    type: "number",
                    min: 50,
                    max: 95,
                    step: 1,
                    default: 80,
                    unit: "%",
                    group: "高级风险控制"
                },
                orderSizeLimit: {
                    label: "单笔最大委托",
                    description: "限制单笔委托规模，避免误操作导致大额下单。",
                    type: "number",
                    min: 10,
                    max: 500,
                    step: 10,
                    default: 100,
                    unit: "万",
                    group: "高级风险控制"
                },
                turnoverLimit: {
                    label: "日累计成交上限",
                    description: "限制日内累计换手规模，降低冲击成本。",
                    type: "number",
                    min: 100,
                    max: 20000,
                    step: 100,
                    default: 5000,
                    unit: "万",
                    group: "高级风险控制"
                },
                slippageLimit: {
                    label: "滑点容忍度",
                    description: "超出该阈值时视为异常成交环境。",
                    type: "number",
                    min: 0.05,
                    max: 1,
                    step: 0.05,
                    default: 0.2,
                    unit: "%",
                    group: "高级风险控制"
                },
                level1Breaker: {
                    label: "一级熔断阈值",
                    description: "达到阈值时发出预警并停止新增仓位。",
                    type: "number",
                    min: 1,
                    max: 5,
                    step: 0.5,
                    default: 2,
                    unit: "%",
                    group: "高级风险控制"
                },
                level2Breaker: {
                    label: "二级熔断阈值",
                    description: "达到阈值时视为减仓级别风控。",
                    type: "number",
                    min: 3,
                    max: 10,
                    step: 0.5,
                    default: 5,
                    unit: "%",
                    group: "高级风险控制"
                },
                level3Breaker: {
                    label: "三级熔断阈值",
                    description: "达到阈值时视为清仓级别风控。",
                    type: "number",
                    min: 6,
                    max: 15,
                    step: 0.5,
                    default: 8,
                    unit: "%",
                    group: "高级风险控制"
                }
            }
        },
        backtestSettings: {
            properties: {
                backtestPeriod: {
                    label: "回测周期",
                    description: "选择回测的历史时间范围。",
                    type: "select",
                    default: "3year",
                    options: [
                        { value: "1year", label: "最近1年" },
                        { value: "3year", label: "最近3年" },
                        { value: "5year", label: "最近5年" },
                        { value: "full", label: "全周期" }
                    ],
                    group: "时间周期配置"
                },
                initialCapital: {
                    label: "初始资金",
                    description: "回测使用的初始资金规模。",
                    type: "number",
                    min: 100000,
                    max: 10000000,
                    step: 100000,
                    default: 1000000,
                    unit: "元",
                    group: "资金管理"
                },
                commissionRate: {
                    label: "交易佣金",
                    description: "每笔交易的佣金费率。",
                    type: "number",
                    min: 0.0001,
                    max: 0.005,
                    step: 0.0001,
                    default: 0.0015,
                    decimals: 4,
                    displayMultiplier: 100,
                    displayDecimals: 3,
                    displayUnit: "%",
                    group: "交易成本"
                },
                slippageRate: {
                    label: "滑点率",
                    description: "成交时预估的滑点比例。",
                    type: "number",
                    min: 0,
                    max: 0.01,
                    step: 0.0001,
                    default: 0.001,
                    decimals: 4,
                    displayMultiplier: 100,
                    displayDecimals: 3,
                    displayUnit: "%",
                    group: "交易成本"
                }
            }
        },
        uiGroups: [
            {
                id: "backtestCore",
                name: "回测核心参数",
                params: ["backtestPeriod", "initialCapital", "commissionRate", "slippageRate"]
            },
            {
                id: "riskCore",
                name: "风险控制参数",
                params: ["stopLossPercent", "takeProfitPercent", "maxDrawdownLimit", "autoStopEnabled", "positionSizingMethod"]
            },
            {
                id: "positionRisk",
                name: "仓位与暴露控制",
                params: ["maxTotalExposure", "maxPositionPercent"]
            },
            {
                id: "advancedRisk",
                name: "高级风险控制",
                params: ["varWarningPercent", "orderSizeLimit", "turnoverLimit", "slippageLimit", "level1Breaker", "level2Breaker", "level3Breaker"]
            }
        ],
        defaultValues: {
            backtestPeriod: "3year",
            initialCapital: 1000000,
            commissionRate: 0.0015,
            slippageRate: 0.001,
            stopLossPercent: 10,
            takeProfitPercent: 20,
            maxDrawdownLimit: 20,
            maxTotalExposure: 67,
            maxPositionPercent: 15,
            autoStopEnabled: true,
            positionSizingMethod: "fixed",
            varWarningPercent: 80,
            orderSizeLimit: 100,
            turnoverLimit: 5000,
            slippageLimit: 0.2,
            level1Breaker: 2,
            level2Breaker: 5,
            level3Breaker: 8
        }
    }
};

function cloneMeta(value) {
    return JSON.parse(JSON.stringify(value || null));
}

function resolveMetaPath(path) {
    var resolvedPath = String(path || "").trim();
    return resolvedPath.length > 0 ? resolvedPath : sharedMetaPath;
}

function cachedMetaForPath(path) {
    var resolvedPath = resolveMetaPath(path);
    return metaCache[resolvedPath] || null;
}

function resolveAvailableMeta(path) {
    var resolvedPath = resolveMetaPath(path);
    var currentMeta = cachedMetaForPath(resolvedPath);
    if (currentMeta) {
        return currentMeta;
    }

    var embeddedMeta = resolveEmbeddedMeta(resolvedPath);
    if (embeddedMeta) {
        metaCache[resolvedPath] = embeddedMeta;
        return metaCache[resolvedPath];
    }

    return null;
}

function resolveEmbeddedMeta(path) {
    if (!path) {
        return null;
    }
    return embeddedMetaByPath[path] ? cloneMeta(embeddedMetaByPath[path]) : null;
}

function isSuccessfulResponse(xhr) {
    if (!xhr) {
        return false;
    }

    if (xhr.status === 200) {
        return true;
    }

    // QML 通过 qrc 读取本地资源时，XMLHttpRequest 可能返回 status=0。
    return xhr.status === 0 && xhr.responseText && xhr.responseText.length > 0;
}

function loadMetaFile(path, callback) {
    var resolvedPath = resolveMetaPath(path);
    console.log("开始加载参数元数据文件:", resolvedPath);
    
    // 检查缓存
    if (cachedMetaForPath(resolvedPath)) {
        console.log("使用缓存的参数元数据");
        callback(cloneMeta(cachedMetaForPath(resolvedPath)));
        return;
    }

    var embeddedMeta = resolveEmbeddedMeta(resolvedPath);
    if (embeddedMeta) {
        console.log("使用内置参数元数据:", resolvedPath);
        metaCache[resolvedPath] = embeddedMeta;
        callback(cloneMeta(metaCache[resolvedPath]));
        return;
    }
    
    // 如果已经在加载中，将回调加入队列
    if (loadingCallbacks[resolvedPath]) {
        loadingCallbacks[resolvedPath].push(callback);
        console.log("文件已在加载中，加入回调队列");
        return;
    }
    
    // 初始化回调队列
    loadingCallbacks[resolvedPath] = [callback];
    
    var xhr = new XMLHttpRequest();
    xhr.open("GET", resolvedPath);
    xhr.timeout = 10000; // 10秒超时
    
    xhr.onreadystatechange = function() {
        if (xhr.readyState === XMLHttpRequest.DONE) {
            var callbacks = loadingCallbacks[resolvedPath];
            delete loadingCallbacks[resolvedPath];
            
            if (isSuccessfulResponse(xhr)) {
                try {
                    var meta = JSON.parse(xhr.responseText);
                    console.log("成功加载参数元数据文件:", resolvedPath, "状态:", xhr.status, "大小:", xhr.responseText.length, "字节");
                    
                    // 缓存结果
                    metaCache[resolvedPath] = meta;
                    
                    // 执行所有回调
                    for (var i = 0; i < callbacks.length; i++) {
                        try {
                            callbacks[i](cloneMeta(meta));
                        } catch (e) {
                            console.error("回调执行失败:", e);
                        }
                    }
                } catch (e) {
                    console.error("解析JSON失败:", e, "路径:", resolvedPath);
                    for (var i = 0; i < callbacks.length; i++) {
                        try {
                            callbacks[i](null);
                        } catch (e2) {
                            console.error("错误回调执行失败:", e2);
                        }
                    }
                }
            } else {
                console.error("加载参数元数据失败:", resolvedPath, "状态:", xhr.status, "响应:", xhr.statusText);
                for (var i = 0; i < callbacks.length; i++) {
                    try {
                        callbacks[i](null);
                    } catch (e) {
                        console.error("错误回调执行失败:", e);
                    }
                }
            }
        }
    };
    
    xhr.ontimeout = function() {
        console.error("加载参数元数据超时:", resolvedPath);
        var callbacks = loadingCallbacks[resolvedPath];
        delete loadingCallbacks[resolvedPath];
        
        for (var i = 0; i < callbacks.length; i++) {
            try {
                callbacks[i](null);
            } catch (e) {
                console.error("超时回调执行失败:", e);
            }
        }
    };
    
    xhr.onerror = function() {
        console.error("加载参数元数据网络错误:", resolvedPath);
        var callbacks = loadingCallbacks[resolvedPath];
        delete loadingCallbacks[resolvedPath];
        
        for (var i = 0; i < callbacks.length; i++) {
            try {
                callbacks[i](null);
            } catch (e) {
                console.error("错误回调执行失败:", e);
            }
        }
    };
    
    try {
        xhr.send();
    } catch (e) {
        console.error("发送XHR请求失败:", e);
        delete loadingCallbacks[resolvedPath];
        callback(null);
    }
}

function getRiskConfig(path) {
    var resolvedPath = resolveMetaPath(path);
    var currentMeta = resolveAvailableMeta(resolvedPath);
    console.log("getRiskConfig called, metaCache exists:", !!currentMeta, "path:", resolvedPath);
    if (!currentMeta) {
        console.warn("风险和回测参数未加载，无法获取风险配置");
        return { 
            riskManagement: { properties: {} },
            backtestSettings: { properties: {} },
            allParameters: {}
        };
    }
    
    console.log("metaCache keys:", Object.keys(currentMeta));
    console.log("riskManagement exists:", "riskManagement" in currentMeta);
    console.log("backtestSettings exists:", "backtestSettings" in currentMeta);
    
    var result = {
        riskManagement: currentMeta.riskManagement || { properties: {} },
        backtestSettings: currentMeta.backtestSettings || { properties: {} },
        uiGroups: currentMeta.uiGroups || [],
        defaultValues: currentMeta.defaultValues || {},
        allParameters: {}
    };
    
    // 合并风险管理参数
    if (currentMeta.riskManagement && currentMeta.riskManagement.properties) {
        console.log("riskManagement.properties keys:", Object.keys(currentMeta.riskManagement.properties));
        for (var key in currentMeta.riskManagement.properties) {
            if (currentMeta.riskManagement.properties.hasOwnProperty(key)) {
                result.allParameters[key] = currentMeta.riskManagement.properties[key];
                result.allParameters[key].category = "风险管理";
                console.log("Added risk parameter:", key, currentMeta.riskManagement.properties[key].type);
            }
        }
    } else {
        console.log("No riskManagement.properties found");
    }
    
    // 合并回测设置参数
    if (currentMeta.backtestSettings && currentMeta.backtestSettings.properties) {
        console.log("backtestSettings.properties keys:", Object.keys(currentMeta.backtestSettings.properties));
        for (var key in currentMeta.backtestSettings.properties) {
            if (currentMeta.backtestSettings.properties.hasOwnProperty(key)) {
                result.allParameters[key] = currentMeta.backtestSettings.properties[key];
                result.allParameters[key].category = "回测设置";
                console.log("Added backtest parameter:", key, currentMeta.backtestSettings.properties[key].type);
            }
        }
    } else {
        console.log("No backtestSettings.properties found");
    }
    
    console.log("获取风险和回测配置 - 风险管理参数:", Object.keys(currentMeta.riskManagement && currentMeta.riskManagement.properties || {}).length,
                "回测设置参数:", Object.keys(currentMeta.backtestSettings && currentMeta.backtestSettings.properties || {}).length,
                "总参数:", Object.keys(result.allParameters).length);
    
    return result;
}

function getParameterConfigs(category, path) {
    var config = getRiskConfig(path);
    var configs = [];
    
    // 如果没有指定类别，返回所有参数
    if (!category || category === "all") {
        // 转换风险管理参数
        if (config.riskManagement.properties) {
            for (var key in config.riskManagement.properties) {
                if (config.riskManagement.properties.hasOwnProperty(key)) {
                    var param = config.riskManagement.properties[key];
                    var paramConfig = convertToParamConfig(key, param, "风险管理", path);
                    configs.push(paramConfig);
                }
            }
        }
        
        // 转换回测设置参数
        if (config.backtestSettings.properties) {
            for (var key in config.backtestSettings.properties) {
                if (config.backtestSettings.properties.hasOwnProperty(key)) {
                    var param = config.backtestSettings.properties[key];
                    var paramConfig = convertToParamConfig(key, param, "回测设置", path);
                    configs.push(paramConfig);
                }
            }
        }
    } 
    // 只返回风险管理参数
    else if (category === "risk") {
        if (config.riskManagement.properties) {
            for (var key in config.riskManagement.properties) {
                if (config.riskManagement.properties.hasOwnProperty(key)) {
                    var param = config.riskManagement.properties[key];
                    var paramConfig = convertToParamConfig(key, param, "风险管理", path);
                    configs.push(paramConfig);
                }
            }
        }
    } 
    // 只返回回测设置参数
    else if (category === "backtest") {
        if (config.backtestSettings.properties) {
            for (var key in config.backtestSettings.properties) {
                if (config.backtestSettings.properties.hasOwnProperty(key)) {
                    var param = config.backtestSettings.properties[key];
                    var paramConfig = convertToParamConfig(key, param, "回测设置", path);
                    configs.push(paramConfig);
                }
            }
        }
    }
    
    console.log("转换风险和回测参数配置 - 类别:", category, "数量:", configs.length);
    return configs;
}

function getUiGroups(path) {
    var currentMeta = resolveAvailableMeta(path);
    if (!currentMeta) {
        console.warn("风险和回测参数未加载，无法获取UI分组");
        return [];
    }
    
    return currentMeta.uiGroups || [];
}

function getDefaultValues(path) {
    var currentMeta = resolveAvailableMeta(path);
    if (!currentMeta) {
        console.warn("风险和回测参数未加载，无法获取默认值");
        return {};
    }
    
    return currentMeta.defaultValues || {};
}

function getDefaultValue(paramId, path) {
    var defaults = getDefaultValues(path);
    return defaults[paramId];
}

function convertToParamConfig(id, param, category, path) {
    var config = {
        id: id,
        label: param.label || param.title || id,
        description: param.description || "",
        type: mapParamType(param.type),
        default: param.default !== undefined ? param.default : getDefaultValue(id, path),
        required: param.required || false,
        category: category
    };
    
    // 处理条件可见性
    if (param.visibleWhen) {
        config.visibleWhen = param.visibleWhen;
        config.dependencies = []; // 可以从visibleWhen中解析依赖
    }
    
    // 类型特定配置
    switch (param.type) {
        case "number":
            config.min = param.min;
            config.max = param.max;
            config.step = param.step || 0.01;
            config.unit = param.unit || "";
            config.decimals = param.decimals || 2;
            break;
            
        case "select":
            config.type = param.multiple ? "multiselect" : "select";
            config.options = param.options || [];
            config.multiple = param.multiple || false;
            break;

        case "string":
            config.type = "input";
            config.multiline = param.multiline || false;
            config.placeholder = param.placeholder || "";
            config.maxLength = param.maxLength;
            break;
            
        case "boolean":
            config.type = "toggle";
            config.trueLabel = param.trueLabel || "是";
            config.falseLabel = param.falseLabel || "否";
            break;
    }
    
    // 复制其他属性
    if (param.category) config.category = param.category;
    if (param.group) config.group = param.group;
    if (param.unit) config.unit = param.unit;
    
    return config;
}

function mapParamType(schemaType) {
    var typeMap = {
        "integer": "slider",
        "number": "slider", 
        "string": "input",
        "boolean": "toggle",
        "select": "select",
        "array": "select"
    };
    return typeMap[schemaType] || "input";
}

// 根据UI分组获取参数配置
function getParameterConfigsByGroup(groupId, path) {
    var config = getRiskConfig(path);
    var group = null;
    
    // 查找分组
    for (var i = 0; i < config.uiGroups.length; i++) {
        if (config.uiGroups[i].id === groupId) {
            group = config.uiGroups[i];
            break;
        }
    }
    
    if (!group) {
        console.warn("未找到分组:", groupId);
        return [];
    }
    
    var configs = [];
    var paramIds = group.params || [];
    
    for (var i = 0; i < paramIds.length; i++) {
        var paramId = paramIds[i];
        var param = config.allParameters[paramId];
        if (param) {
            var paramConfig = convertToParamConfig(paramId, param, group.name, path);
            configs.push(paramConfig);
        }
    }
    
    console.log("获取分组参数配置 - 分组:", groupId, "名称:", group.name, "数量:", configs.length);
    return configs;
}

// 获取所有参数组的配置
function getAllGroupedConfigs(path) {
    var config = getRiskConfig(path);
    var groupedConfigs = [];
    
    for (var i = 0; i < config.uiGroups.length; i++) {
        var group = config.uiGroups[i];
        var groupConfigs = getParameterConfigsByGroup(group.id, path);
        
        groupedConfigs.push({
            id: group.id,
            name: group.name,
            description: group.description,
            configs: groupConfigs
        });
    }
    
    return groupedConfigs;
}

// 获取缓存的元数据
function getCachedMeta() {
    return cloneMeta(resolveAvailableMeta(sharedMetaPath));
}

// 清除缓存
function clearCache(path) {
    var resolvedPath = resolveMetaPath(path);
    delete metaCache[resolvedPath];
    console.log("风险和回测参数缓存已清除");
}

// 检查是否正在加载
function isLoading(path) {
    return !!loadingCallbacks[path];
}

// 获取验证规则
function getValidationRules(path) {
    var currentMeta = resolveAvailableMeta(path);
    if (!currentMeta) {
        console.warn("风险和回测参数未加载，无法获取验证规则");
        return {};
    }
    
    return currentMeta.validationRules || {};
}

// 验证参数值
function validateParameter(paramId, value) {
    var rules = getValidationRules();
    var paramRules = rules[paramId];
    
    if (!paramRules) {
        return { valid: true, message: "" };
    }
    
    var validation = { valid: true, message: "" };
    
    // 检查最小值
    if (paramRules.min !== undefined && value < paramRules.min) {
        validation.valid = false;
        validation.message = "值不能小于 " + paramRules.min;
    }
    
    // 检查最大值
    if (paramRules.max !== undefined && value > paramRules.max) {
        validation.valid = false;
        validation.message = "值不能大于 " + paramRules.max;
    }
    
    // 使用自定义消息
    if (!validation.valid && paramRules.message) {
        validation.message = paramRules.message;
    }
    
    return validation;
}
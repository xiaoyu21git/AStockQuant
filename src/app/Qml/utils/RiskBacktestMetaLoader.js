// RiskBacktestMetaLoader.js
// 用于动态加载和合并风险和回测参数metadata
// 支持缓存、错误处理和加载状态管理

.pragma library

var metaCache = null;
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
                params: ["stopLossPercent", "takeProfitPercent", "maxDrawdownLimit", "positionSizingMethod"]
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
            positionSizingMethod: "fixed"
        }
    }
};

function cloneMeta(value) {
    return JSON.parse(JSON.stringify(value || null));
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
    console.log("开始加载风险和回测参数文件:", path);
    
    // 检查缓存
    if (metaCache) {
        console.log("使用缓存的风险和回测参数");
        callback(metaCache);
        return;
    }

    var embeddedMeta = resolveEmbeddedMeta(path);
    if (embeddedMeta) {
        console.log("使用内置风险和回测参数:", path);
        metaCache = embeddedMeta;
        callback(cloneMeta(metaCache));
        return;
    }
    
    // 如果已经在加载中，将回调加入队列
    if (loadingCallbacks[path]) {
        loadingCallbacks[path].push(callback);
        console.log("文件已在加载中，加入回调队列");
        return;
    }
    
    // 初始化回调队列
    loadingCallbacks[path] = [callback];
    
    var xhr = new XMLHttpRequest();
    xhr.open("GET", path);
    xhr.timeout = 10000; // 10秒超时
    
    xhr.onreadystatechange = function() {
        if (xhr.readyState === XMLHttpRequest.DONE) {
            var callbacks = loadingCallbacks[path];
            delete loadingCallbacks[path];
            
            if (isSuccessfulResponse(xhr)) {
                try {
                    var meta = JSON.parse(xhr.responseText);
                    console.log("成功加载风险和回测参数文件:", path, "状态:", xhr.status, "大小:", xhr.responseText.length, "字节");
                    
                    // 缓存结果
                    metaCache = meta;
                    
                    // 执行所有回调
                    for (var i = 0; i < callbacks.length; i++) {
                        try {
                            callbacks[i](cloneMeta(meta));
                        } catch (e) {
                            console.error("回调执行失败:", e);
                        }
                    }
                } catch (e) {
                    console.error("解析JSON失败:", e, "路径:", path);
                    for (var i = 0; i < callbacks.length; i++) {
                        try {
                            callbacks[i](null);
                        } catch (e2) {
                            console.error("错误回调执行失败:", e2);
                        }
                    }
                }
            } else {
                console.error("加载风险和回测参数失败:", path, "状态:", xhr.status, "响应:", xhr.statusText);
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
        console.error("加载风险和回测参数超时:", path);
        var callbacks = loadingCallbacks[path];
        delete loadingCallbacks[path];
        
        for (var i = 0; i < callbacks.length; i++) {
            try {
                callbacks[i](null);
            } catch (e) {
                console.error("超时回调执行失败:", e);
            }
        }
    };
    
    xhr.onerror = function() {
        console.error("加载风险和回测参数网络错误:", path);
        var callbacks = loadingCallbacks[path];
        delete loadingCallbacks[path];
        
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
        delete loadingCallbacks[path];
        callback(null);
    }
}

function getRiskConfig() {
    console.log("getRiskConfig called, metaCache exists:", !!metaCache);
    if (!metaCache) {
        console.warn("风险和回测参数未加载，无法获取风险配置");
        return { 
            riskManagement: { properties: {} },
            backtestSettings: { properties: {} },
            allParameters: {}
        };
    }
    
    console.log("metaCache keys:", Object.keys(metaCache));
    console.log("riskManagement exists:", "riskManagement" in metaCache);
    console.log("backtestSettings exists:", "backtestSettings" in metaCache);
    
    var result = {
        riskManagement: metaCache.riskManagement || { properties: {} },
        backtestSettings: metaCache.backtestSettings || { properties: {} },
        uiGroups: metaCache.uiGroups || [],
        defaultValues: metaCache.defaultValues || {},
        allParameters: {}
    };
    
    // 合并风险管理参数
    if (metaCache.riskManagement && metaCache.riskManagement.properties) {
        console.log("riskManagement.properties keys:", Object.keys(metaCache.riskManagement.properties));
        for (var key in metaCache.riskManagement.properties) {
            if (metaCache.riskManagement.properties.hasOwnProperty(key)) {
                result.allParameters[key] = metaCache.riskManagement.properties[key];
                result.allParameters[key].category = "风险管理";
                console.log("Added risk parameter:", key, metaCache.riskManagement.properties[key].type);
            }
        }
    } else {
        console.log("No riskManagement.properties found");
    }
    
    // 合并回测设置参数
    if (metaCache.backtestSettings && metaCache.backtestSettings.properties) {
        console.log("backtestSettings.properties keys:", Object.keys(metaCache.backtestSettings.properties));
        for (var key in metaCache.backtestSettings.properties) {
            if (metaCache.backtestSettings.properties.hasOwnProperty(key)) {
                result.allParameters[key] = metaCache.backtestSettings.properties[key];
                result.allParameters[key].category = "回测设置";
                console.log("Added backtest parameter:", key, metaCache.backtestSettings.properties[key].type);
            }
        }
    } else {
        console.log("No backtestSettings.properties found");
    }
    
    console.log("获取风险和回测配置 - 风险管理参数:", Object.keys(metaCache.riskManagement && metaCache.riskManagement.properties || {}).length,
                "回测设置参数:", Object.keys(metaCache.backtestSettings && metaCache.backtestSettings.properties || {}).length,
                "总参数:", Object.keys(result.allParameters).length);
    
    return result;
}

function getParameterConfigs(category) {
    var config = getRiskConfig();
    var configs = [];
    
    // 如果没有指定类别，返回所有参数
    if (!category || category === "all") {
        // 转换风险管理参数
        if (config.riskManagement.properties) {
            for (var key in config.riskManagement.properties) {
                if (config.riskManagement.properties.hasOwnProperty(key)) {
                    var param = config.riskManagement.properties[key];
                    var paramConfig = convertToParamConfig(key, param, "风险管理");
                    configs.push(paramConfig);
                }
            }
        }
        
        // 转换回测设置参数
        if (config.backtestSettings.properties) {
            for (var key in config.backtestSettings.properties) {
                if (config.backtestSettings.properties.hasOwnProperty(key)) {
                    var param = config.backtestSettings.properties[key];
                    var paramConfig = convertToParamConfig(key, param, "回测设置");
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
                    var paramConfig = convertToParamConfig(key, param, "风险管理");
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
                    var paramConfig = convertToParamConfig(key, param, "回测设置");
                    configs.push(paramConfig);
                }
            }
        }
    }
    
    console.log("转换风险和回测参数配置 - 类别:", category, "数量:", configs.length);
    return configs;
}

function getUiGroups() {
    if (!metaCache) {
        console.warn("风险和回测参数未加载，无法获取UI分组");
        return [];
    }
    
    return metaCache.uiGroups || [];
}

function getDefaultValues() {
    if (!metaCache) {
        console.warn("风险和回测参数未加载，无法获取默认值");
        return {};
    }
    
    return metaCache.defaultValues || {};
}

function getDefaultValue(paramId) {
    var defaults = getDefaultValues();
    return defaults[paramId];
}

function convertToParamConfig(id, param, category) {
    var config = {
        id: id,
        label: param.label || param.title || id,
        description: param.description || "",
        type: mapParamType(param.type),
        default: param.default !== undefined ? param.default : getDefaultValue(id),
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
            config.type = "select";
            config.options = param.options || [];
            config.multiple = param.multiple || false;
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
function getParameterConfigsByGroup(groupId) {
    var config = getRiskConfig();
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
            var paramConfig = convertToParamConfig(paramId, param, group.name);
            configs.push(paramConfig);
        }
    }
    
    console.log("获取分组参数配置 - 分组:", groupId, "名称:", group.name, "数量:", configs.length);
    return configs;
}

// 获取所有参数组的配置
function getAllGroupedConfigs() {
    var config = getRiskConfig();
    var groupedConfigs = [];
    
    for (var i = 0; i < config.uiGroups.length; i++) {
        var group = config.uiGroups[i];
        var groupConfigs = getParameterConfigsByGroup(group.id);
        
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
    return metaCache;
}

// 清除缓存
function clearCache() {
    metaCache = null;
    console.log("风险和回测参数缓存已清除");
}

// 检查是否正在加载
function isLoading(path) {
    return !!loadingCallbacks[path];
}

// 获取验证规则
function getValidationRules() {
    if (!metaCache) {
        console.warn("风险和回测参数未加载，无法获取验证规则");
        return {};
    }
    
    return metaCache.validationRules || {};
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
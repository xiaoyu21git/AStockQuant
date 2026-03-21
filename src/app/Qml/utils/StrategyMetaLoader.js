// StrategyMetaLoader.js
// 用于动态加载和合并策略metadata
// 支持缓存、错误处理和加载状态管理

.pragma library

var metaCache = null;
var loadingCallbacks = {};

function loadMetaFile(path, callback) {
    console.log("开始加载策略元数据文件:", path);
    
    // 检查缓存
    if (metaCache) {
        console.log("使用缓存的策略元数据");
        callback(metaCache);
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
            
            if (xhr.status === 200) {
                try {
                    var meta = JSON.parse(xhr.responseText);
                    console.log("成功加载策略元数据文件:", path, "大小:", xhr.responseText.length, "字节");
                    
                    // 缓存结果
                    metaCache = meta;
                    
                    // 执行所有回调
                    for (var i = 0; i < callbacks.length; i++) {
                        try {
                            callbacks[i](meta);
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
                console.error("加载策略元数据失败:", path, "状态:", xhr.status, "响应:", xhr.statusText);
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
        console.error("加载策略元数据超时:", path);
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
        console.error("加载策略元数据网络错误:", path);
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

function getStrategyConfig(strategyType) {
    if (!metaCache) {
        console.warn("策略元数据未加载，无法获取策略配置");
        return { 
            commonParameters: {},
            strategyParameters: {},
            allParameters: {}
        };
    }
    
    var result = {
        commonParameters: metaCache.commonParameters || {},
        strategyParameters: metaCache.strategySchemas && metaCache.strategySchemas[strategyType] ? metaCache.strategySchemas[strategyType] : {},
        allParameters: {}
    };
    
    // 合并通用参数和策略特定参数
    if (result.commonParameters.properties) {
        for (var key in result.commonParameters.properties) {
            if (result.commonParameters.properties.hasOwnProperty(key)) {
                result.allParameters[key] = result.commonParameters.properties[key];
                result.allParameters[key].category = "通用参数";
            }
        }
    }
    
    if (result.strategyParameters.properties) {
        for (var key in result.strategyParameters.properties) {
            if (result.strategyParameters.properties.hasOwnProperty(key)) {
                result.allParameters[key] = result.strategyParameters.properties[key];
                result.allParameters[key].category = "策略参数";
            }
        }
    }
    
    console.log("获取策略配置 - 类型:", strategyType, 
                "通用参数:", Object.keys(result.commonParameters.properties || {}).length,
                "策略参数:", Object.keys(result.strategyParameters.properties || {}).length,
                "总参数:", Object.keys(result.allParameters).length);
    
    return result;
}

function getStrategyTypes() {
    if (!metaCache || !metaCache.strategySchemas) {
        console.warn("策略元数据未加载，无法获取策略类型");
        return [];
    }
    
    var types = [];
    for (var key in metaCache.strategySchemas) {
        if (metaCache.strategySchemas.hasOwnProperty(key)) {
            var schema = metaCache.strategySchemas[key];
            types.push({
                id: key,
                title: schema.title || key,
                description: schema.description || "",
                category: schema.category || "其他"
            });
        }
    }
    
    console.log("获取策略类型:", types.length, "种");
    return types;
}

function getParameterConfigs(strategyType) {
    var config = getStrategyConfig(strategyType);
    var configs = [];
    
    // 转换通用参数
    if (config.commonParameters.properties) {
        for (var key in config.commonParameters.properties) {
            if (config.commonParameters.properties.hasOwnProperty(key)) {
                var param = config.commonParameters.properties[key];
                var paramConfig = convertToParamConfig(key, param, "通用参数");
                configs.push(paramConfig);
            }
        }
    }
    
    // 转换策略特定参数
    if (config.strategyParameters.properties) {
        for (var key in config.strategyParameters.properties) {
            if (config.strategyParameters.properties.hasOwnProperty(key)) {
                var param = config.strategyParameters.properties[key];
                var paramConfig = convertToParamConfig(key, param, "策略参数");
                configs.push(paramConfig);
            }
        }
    }
    
    console.log("转换策略参数配置 - 类型:", strategyType, "数量:", configs.length);
    return configs;
}

function convertToParamConfig(id, param, category) {
    var config = {
        id: id,
        label: param.label || param.title || id,
        description: param.description || "",
        type: mapParamType(param.type),
        default: param.default,
        required: param.required || false,
        category: category
    };
    
    // 处理条件可见性
    if (param.visibleWhen) {
        config.visibleWhen = param.visibleWhen;
        config.dependencies = Object.keys(param.visibleWhen);
    }
    
    // 类型特定配置
    switch (param.type) {
        case "integer":
        case "number":
            config.min = param.minimum;
            config.max = param.maximum;
            config.step = param.step || (param.type === "integer" ? 1 : 0.01);
            config.unit = param.unit || "";
            config.decimals = param.decimals || (param.type === "integer" ? 0 : 2);
            config.showPresets = param.commonValues !== undefined;
            config.presets = param.commonValues;
            break;
            
        case "string":
            config.multiline = param.multiline || false;
            config.maxLength = param.maxLength;
            config.minLength = param.minLength;
            config.pattern = param.pattern;
            config.patternMessage = param.patternMessage;
            
            if (param.enum) {
                config.type = "select";
                config.options = param.enum.map(function(item) {
                    return { value: item, label: item };
                });
            }
            break;
            
        case "boolean":
            config.type = "toggle";
            config.trueLabel = param.trueLabel || "是";
            config.falseLabel = param.falseLabel || "否";
            break;
            
        case "array":
            config.type = "select";
            if (param.items && param.items.enum) {
                config.options = param.items.enum.map(function(item) {
                    return { value: item, label: item };
                });
            }
            config.multiple = true;
            break;
    }
    
    return config;
}

function mapParamType(schemaType) {
    var typeMap = {
        "integer": "slider",
        "number": "slider", 
        "string": "input",
        "boolean": "toggle",
        "array": "select",
        "object": "group"
    };
    return typeMap[schemaType] || "input";
}

// 获取缓存的元数据
function getCachedMeta() {
    return metaCache;
}

// 清除缓存
function clearCache() {
    metaCache = null;
    console.log("策略元数据缓存已清除");
}

// 检查是否正在加载
function isLoading(path) {
    return !!loadingCallbacks[path];
}
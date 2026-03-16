// FactorMetaLoader.js
// 用于动态加载和合并因子metadata
// 支持缓存、错误处理和加载状态管理

.pragma library

var metaCache = null;
var loadingCallbacks = {};

function loadMetaFile(path, callback) {
    console.log("开始加载元数据文件:", path);
    
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
                    console.log("成功加载元数据文件:", path, "大小:", xhr.responseText.length, "字节");
                    
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
                console.error("加载因子元数据失败:", path, "状态:", xhr.status, "响应:", xhr.statusText);
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
        console.error("加载元数据超时:", path);
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
        console.error("加载元数据网络错误:", path);
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

function getMergedMeta(meta, type) {
    if (!meta) {
        console.warn("获取合并元数据失败: meta为空");
        return { parameters: {} };
    }
    
    var merged = { parameters: {} };
    
    // 处理 factor_common_params.json 结构
    if (meta.commonParams) {
        for (var k in meta.commonParams) {
            if (meta.commonParams.hasOwnProperty(k)) {
                merged.parameters[k] = meta.commonParams[k];
            }
        }
        console.log("加载通用参数:", Object.keys(meta.commonParams).length, "个");
    }
    
    // 处理特定类型参数
    if (meta.factorTypeSpecificParams && meta.factorTypeSpecificParams[type]) {
        var specificParams = meta.factorTypeSpecificParams[type].params;
        if (specificParams) {
            for (var k in specificParams) {
                if (specificParams.hasOwnProperty(k)) {
                    merged.parameters[k] = specificParams[k];
                }
            }
            console.log("加载类型特定参数 [" + type + "]:", Object.keys(specificParams).length, "个");
        }
    }
    
    console.log("合并后的元数据 - 类型:", type, "总参数数量:", Object.keys(merged.parameters).length);
    
    // 验证参数结构
    for (var paramName in merged.parameters) {
        if (merged.parameters.hasOwnProperty(paramName)) {
            var param = merged.parameters[paramName];
            if (!param.type) {
                console.warn("参数缺少type字段:", paramName);
                param.type = "string"; // 默认类型
            }
        }
    }
    
    return merged;
}

// 获取缓存的元数据
function getCachedMeta() {
    return metaCache;
}

// 清除缓存
function clearCache() {
    metaCache = null;
    console.log("元数据缓存已清除");
}

// 检查是否正在加载
function isLoading(path) {
    return !!loadingCallbacks[path];
}

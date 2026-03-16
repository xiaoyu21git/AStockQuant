// ParamRegistry.qml
// 参数组件注册表 - 方案2插件化组件注册的核心
// 负责管理和创建各种类型的参数组件

pragma Singleton
import QtQuick 2.15

/**
 * 参数组件注册表（单例模式）
 * 
 * 功能：
 * 1. 注册各种参数类型的组件
 * 2. 根据参数类型获取对应的组件
 * 3. 动态创建参数实例
 * 4. 支持运行时扩展新的参数类型
 * 
 * 使用方式：
 * ParamRegistry.registerParam("slider", sliderComponent)
 * var component = ParamRegistry.getComponent("slider")
 * var instance = ParamRegistry.createParam("slider", config, parent)
 */
QtObject {
    id: registry
    
    // ============ 组件注册表 ============
    
    // 参数组件映射表 { type: Component }
    property var paramComponents: ({})
    
    // 参数验证器映射表 { type: Function }
    property var paramValidators: ({})
    
    // 参数默认配置表 { type: Object }
    property var paramDefaults: ({})
    
    // ============ 注册方法 ============
    
    /**
     * 注册参数组件
     * @param {string} type - 参数类型标识 (如 "slider", "select", "input")
     * @param {Component} component - QML组件
     * @param {Object} options - 可选配置
     *   - validator: 验证函数
     *   - defaults: 默认配置
     */
    function registerParam(type, component, options) {
        options = options || {}
        
        paramComponents[type] = component
        
        if (options.validator) {
            paramValidators[type] = options.validator
        }
        
        if (options.defaults) {
            paramDefaults[type] = options.defaults
        }
        
        console.log("[ParamRegistry] 已注册参数类型:", type)
    }
    
    /**
     * 批量注册参数组件
     * @param {Array} registrations - 注册信息数组
     *   每项格式: { type: string, component: Component, options?: Object }
     */
    function registerParams(registrations) {
        for (var i = 0; i < registrations.length; i++) {
            var reg = registrations[i]
            registerParam(reg.type, reg.component, reg.options)
        }
    }
    
    /**
     * 注销参数类型
     * @param {string} type - 参数类型标识
     */
    function unregisterParam(type) {
        delete paramComponents[type]
        delete paramValidators[type]
        delete paramDefaults[type]
        console.log("[ParamRegistry] 已注销参数类型:", type)
    }
    
    // ============ 获取方法 ============
    
    /**
     * 获取参数组件
     * @param {string} type - 参数类型
     * @return {Component|null} QML组件或null
     */
    function getComponent(type) {
        // 尝试精确匹配
        if (paramComponents[type]) {
            return paramComponents[type]
        }
        
        // 类型映射（支持别名）
        var typeMapping = {
            "integer": "slider",
            "number": "slider",
            "float": "slider",
            "int": "slider",
            "range": "slider",
            "string": "input",
            "text": "input",
            "boolean": "toggle",
            "bool": "toggle",
            "checkbox": "toggle",
            "enum": "select",
            "dropdown": "select",
            "combo": "select",
            "array": "multiselect",
            "list": "multiselect",
            "object": "group",
            "nested": "group"
        }
        
        var mappedType = typeMapping[type]
        if (mappedType && paramComponents[mappedType]) {
            return paramComponents[mappedType]
        }
        
        // 回退到默认输入组件
        console.warn("[ParamRegistry] 未找到类型 '" + type + "' 的组件，使用默认输入组件")
        return paramComponents["input"] || null
    }
    
    /**
     * 获取所有已注册的参数类型
     * @return {Array} 类型标识数组
     */
    function getRegisteredTypes() {
        return Object.keys(paramComponents)
    }
    
    /**
     * 检查参数类型是否已注册
     * @param {string} type - 参数类型
     * @return {boolean}
     */
    function isTypeRegistered(type) {
        return paramComponents.hasOwnProperty(type)
    }
    
    // ============ 创建方法 ============
    
    /**
     * 创建参数组件实例
     * @param {string} type - 参数类型
     * @param {Object} config - 参数配置
     * @param {Item} parent - 父级组件
     * @return {Item|null} 创建的组件实例
     */
    function createParam(type, config, parent) {
        var component = getComponent(type)
        if (!component) {
            console.error("[ParamRegistry] 无法创建参数，类型未注册:", type)
            return null
        }
        
        // 合并默认配置
        var mergedConfig = mergeConfig(type, config)
        
        try {
            var instance = component.createObject(parent, {
                config: mergedConfig
            })
            
            if (!instance) {
                console.error("[ParamRegistry] 组件创建失败:", type)
                return null
            }
            
            console.log("[ParamRegistry] 已创建参数组件:", type, "ID:", config.id || "unknown")
            return instance
            
        } catch (e) {
            console.error("[ParamRegistry] 创建参数组件异常:", type, e)
            return null
        }
    }
    
    /**
     * 批量创建参数组件
     * @param {Array} configs - 参数配置数组
     * @param {Item} parent - 父级组件
     * @return {Array} 创建的组件实例数组
     */
    function createParams(configs, parent) {
        var instances = []
        for (var i = 0; i < configs.length; i++) {
            var config = configs[i]
            var type = config.type || "input"
            var instance = createParam(type, config, parent)
            if (instance) {
                instances.push(instance)
            }
        }
        return instances
    }
    
    // ============ 验证方法 ============
    
    /**
     * 验证参数值
     * @param {string} type - 参数类型
     * @param {*} value - 参数值
     * @param {Object} config - 参数配置
     * @return {Object} { valid: boolean, message: string }
     */
    function validateValue(type, value, config) {
        var validator = paramValidators[type]
        if (validator) {
            return validator(value, config)
        }
        
        // 通用验证
        return defaultValidate(value, config)
    }
    
    /**
     * 默认验证函数
     */
    function defaultValidate(value, config) {
        config = config || {}
        
        // 必填验证
        if (config.required && (value === undefined || value === null || value === "")) {
            return {
                valid: false,
                message: (config.label || config.id || "参数") + " 不能为空"
            }
        }
        
        // 数值范围验证
        if (typeof value === "number") {
            if (config.min !== undefined && value < config.min) {
                return {
                    valid: false,
                    message: (config.label || config.id) + " 不能小于 " + config.min
                }
            }
            if (config.max !== undefined && value > config.max) {
                return {
                    valid: false,
                    message: (config.label || config.id) + " 不能大于 " + config.max
                }
            }
        }
        
        // 字符串长度验证
        if (typeof value === "string") {
            if (config.minLength !== undefined && value.length < config.minLength) {
                return {
                    valid: false,
                    message: (config.label || config.id) + " 长度不能少于 " + config.minLength + " 个字符"
                }
            }
            if (config.maxLength !== undefined && value.length > config.maxLength) {
                return {
                    valid: false,
                    message: (config.label || config.id) + " 长度不能超过 " + config.maxLength + " 个字符"
                }
            }
        }
        
        // 枚举值验证
        if (config.options && Array.isArray(config.options)) {
            var validValues = config.options.map(function(opt) {
                return typeof opt === "object" ? opt.value : opt
            })
            if (!validValues.includes(value)) {
                return {
                    valid: false,
                    message: (config.label || config.id) + " 必须是有效选项"
                }
            }
        }
        
        return { valid: true, message: "" }
    }
    
    // ============ 工具方法 ============
    
    /**
     * 合并默认配置
     */
    function mergeConfig(type, config) {
        var defaults = paramDefaults[type] || {}
        var result = {}
        
        // 首先应用默认值
        for (var key in defaults) {
            result[key] = defaults[key]
        }
        
        // 然后应用传入的配置
        for (var key2 in config) {
            result[key2] = config[key2]
        }
        
        return result
    }
    
    /**
     * 获取参数的默认值
     * @param {Object} config - 参数配置
     * @return {*} 默认值
     */
    function getDefaultValue(config) {
        if (config.default !== undefined) {
            return config.default
        }
        
        // 根据类型返回默认值
        switch (config.type) {
            case "integer":
            case "number":
            case "float":
            case "slider":
            case "range":
                return config.min !== undefined ? config.min : 0
            case "boolean":
            case "bool":
            case "toggle":
                return false
            case "string":
            case "text":
            case "input":
                return ""
            case "enum":
            case "select":
            case "dropdown":
                if (config.options && config.options.length > 0) {
                    var firstOpt = config.options[0]
                    return typeof firstOpt === "object" ? firstOpt.value : firstOpt
                }
                return ""
            case "array":
            case "multiselect":
                return []
            case "object":
            case "group":
                return {}
            default:
                return null
        }
    }
    
    /**
     * 清空所有注册
     */
    function clear() {
        paramComponents = {}
        paramValidators = {}
        paramDefaults = {}
        console.log("[ParamRegistry] 已清空所有注册")
    }
    
    /**
     * 获取注册统计信息
     */
    function getStats() {
        return {
            registeredTypes: Object.keys(paramComponents).length,
            types: Object.keys(paramComponents),
            hasValidators: Object.keys(paramValidators).length
        }
    }
}

// ParamComponents.qml
// 参数组件库 - 注册所有参数组件到注册表
// 这是方案2插件化组件注册的初始化文件

import QtQuick 2.15
import QtQuick.Controls 2.15

/**
 * 参数组件库
 * 
 * 功能：
 * 1. 定义所有参数类型的组件
 * 2. 注册组件到参数注册表
 * 3. 提供组件验证器和默认配置
 * 
 * 使用方式：
 * 在应用启动时创建此组件，自动注册所有参数类型
 */
Item {
    id: root
    
    // ============ 内联参数注册表 ============
    
    // 参数组件映射表 { type: Component }
    property var paramComponents: ({})
    property int registryVersion: 0
    
    // 参数验证器映射表 { type: Function }
    property var paramValidators: ({})
    
    // 参数默认配置表 { type: Object }
    property var paramDefaults: ({})
    
    // ============ 组件定义 ============
    
    // 滑块参数组件
    Component {
        id: sliderComponent
        SliderParam {}
    }
    
    // 下拉选择参数组件
    Component {
        id: selectComponent
        SelectParam {}
    }

    // 多选参数组件
    Component {
        id: multiselectComponent
        MultiSelectParam {}
    }
    
    // 文本输入参数组件
    Component {
        id: inputComponent
        InputParam {}
    }
    
    // 开关切换参数组件
    Component {
        id: toggleComponent
        ToggleParam {}
    }
    
    // ============ 验证器函数 ============
    
    // 滑块参数验证器
    function sliderValidator(value, config) {
        config = config || {}
        
        if (config.required && (value === undefined || value === null)) {
            return {
                valid: false,
                message: (config.label || config.id) + " 不能为空"
            }
        }
        
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
        
        return { valid: true, message: "" }
    }
    
    // 下拉选择验证器
    function selectValidator(value, config) {
        config = config || {}
        
        if (config.required && (value === undefined || value === null || value === "")) {
            return {
                valid: false,
                message: (config.label || config.id) + " 不能为空"
            }
        }
        
        if (value && config.options && Array.isArray(config.options)) {
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

    // 多选验证器
    function multiselectValidator(value, config) {
        config = config || {}

        var values = Array.isArray(value) ? value : (value === undefined || value === null || value === "" ? [] : [value])
        if (config.required && values.length === 0) {
            return {
                valid: false,
                message: (config.label || config.id) + " 至少选择一个选项"
            }
        }

        if (values.length > 0 && config.options && Array.isArray(config.options)) {
            var validValues = config.options.map(function(opt) {
                return typeof opt === "object" ? opt.value : opt
            })
            for (var i = 0; i < values.length; ++i) {
                if (!validValues.includes(values[i])) {
                    return {
                        valid: false,
                        message: (config.label || config.id) + " 包含无效选项"
                    }
                }
            }
        }

        return { valid: true, message: "" }
    }
    
    // 文本输入验证器
    function inputValidator(value, config) {
        config = config || {}
        
        if (config.required && (value === undefined || value === null || value === "")) {
            return {
                valid: false,
                message: (config.label || config.id) + " 不能为空"
            }
        }
        
        if (value !== undefined && value !== null) {
            var strValue = String(value)
            
            if (config.minLength !== undefined && strValue.length < config.minLength) {
                return {
                    valid: false,
                    message: (config.label || config.id) + " 长度不能少于 " + config.minLength + " 个字符"
                }
            }
            
            if (config.maxLength !== undefined && strValue.length > config.maxLength) {
                return {
                    valid: false,
                    message: (config.label || config.id) + " 长度不能超过 " + config.maxLength + " 个字符"
                }
            }
            
            if (config.pattern && strValue !== "") {
                var regex = new RegExp(config.pattern)
                if (!regex.test(strValue)) {
                    return {
                        valid: false,
                        message: (config.label || config.id) + " " + (config.patternMessage || "格式不正确")
                    }
                }
            }
        }
        
        return { valid: true, message: "" }
    }
    
    // 开关切换验证器
    function toggleValidator(value, config) {
        config = config || {}
        
        if (config.required && value === undefined) {
            return {
                valid: false,
                message: (config.label || config.id) + " 必须选择"
            }
        }
        
        return { valid: true, message: "" }
    }
    
    // ============ 默认配置 ============
    
    // 滑块参数默认配置
    property var sliderDefaults: function() {
        return {
            min: 0,
            max: 100,
            step: 1,
            decimals: 0,
            unit: "",
            showPresets: false
        }
    }

    // 下拉选择默认配置
    property var selectDefaults: function() {
        return {
            placeholder: "请选择...",
            searchable: false,
            showChips: true
        }
    }

    property var multiselectDefaults: function() {
        return {
            showChips: true,
            default: []
        }
    }

    // 文本输入默认配置
    property var inputDefaults: function() {
        return {
            placeholder: "请输入...",
            multiline: false,
            password: false,
            maxLength: 255,
            minLength: 0
        }
    }

    // 开关切换默认配置
    property var toggleDefaults: function() {
        return {
            style: "switch",
            trueLabel: "开启",
            falseLabel: "关闭",
            trueIcon: "✓",
            falseIcon: "✗"
        }
    }
    
    // ============ 注册表函数 ============
    
    // 注册参数组件
    function registerParam(type, component, options) {
        options = options || {}

        var nextComponents = {}
        for (var existingType in paramComponents) {
            nextComponents[existingType] = paramComponents[existingType]
        }
        nextComponents[type] = component
        paramComponents = nextComponents

        if (options.validator) {
            var nextValidators = {}
            for (var validatorType in paramValidators) {
                nextValidators[validatorType] = paramValidators[validatorType]
            }
            nextValidators[type] = options.validator
            paramValidators = nextValidators
        }

        if (options.defaults) {
            var nextDefaults = {}
            for (var defaultType in paramDefaults) {
                nextDefaults[defaultType] = paramDefaults[defaultType]
            }
            nextDefaults[type] = options.defaults
            paramDefaults = nextDefaults
        }

        registryVersion += 1
    }
    
    // 获取所有已注册的参数类型
    function getRegisteredTypes() {
        return Object.keys(paramComponents)
    }
    
    // 检查参数类型是否已注册
    function isTypeRegistered(type) {
        return paramComponents.hasOwnProperty(type)
    }
    
    // 注销参数类型
    function unregisterParam(type) {
        var nextComponents = {}
        for (var existingType in paramComponents) {
            if (existingType !== type) {
                nextComponents[existingType] = paramComponents[existingType]
            }
        }
        paramComponents = nextComponents

        var nextValidators = {}
        for (var validatorType in paramValidators) {
            if (validatorType !== type) {
                nextValidators[validatorType] = paramValidators[validatorType]
            }
        }
        paramValidators = nextValidators

        var nextDefaults = {}
        for (var defaultType in paramDefaults) {
            if (defaultType !== type) {
                nextDefaults[defaultType] = paramDefaults[defaultType]
            }
        }
        paramDefaults = nextDefaults
        registryVersion += 1
    }
    
    // 获取注册统计信息
    function getStats() {
        return {
            registeredTypes: Object.keys(paramComponents).length,
            types: Object.keys(paramComponents),
            hasValidators: Object.keys(paramValidators).length
        }
    }
    
    // 获取参数组件
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
        console.warn("[ParamComponents] 未找到类型 '" + type + "' 的组件，使用默认输入组件")
        return paramComponents["input"] || null
    }
    
    // 创建参数组件实例
    function createParam(type, config, parent) {
        var component = getComponent(type)
        if (!component) {
            console.error("[ParamComponents] 无法创建参数，类型未注册:", type)
            return null
        }
        
        // 合并默认配置
        var mergedConfig = mergeConfig(type, config)
        
        try {
            var instance = component.createObject(parent, {
                config: mergedConfig
            })
            
            if (!instance) {
                console.error("[ParamComponents] 组件创建失败:", type)
                return null
            }
            
            console.log("[ParamComponents] 已创建参数组件:", type, "ID:", config.id || "unknown")
            return instance
            
        } catch (e) {
            console.error("[ParamComponents] 创建参数组件异常:", type, e)
            return null
        }
    }
    
    // 合并默认配置
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
    
    // 验证参数值
    function validateValue(type, value, config) {
        var validator = paramValidators[type]
        if (validator) {
            return validator(value, config)
        }
        
        // 通用验证
        return defaultValidate(value, config)
    }
    
    // 默认验证函数
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
    
    // ============ 注册方法 ============
    
    // 注册所有参数组件
    function registerAllComponents() {
        registerParam("slider", sliderComponent, {
            validator: sliderValidator,
            defaults: sliderDefaults()
        })

        registerParam("select", selectComponent, {
            validator: selectValidator,
            defaults: selectDefaults()
        })

        registerParam("multiselect", multiselectComponent, {
            validator: multiselectValidator,
            defaults: multiselectDefaults()
        })

        registerParam("input", inputComponent, {
            validator: inputValidator,
            defaults: inputDefaults()
        })

        registerParam("toggle", toggleComponent, {
            validator: toggleValidator,
            defaults: toggleDefaults()
        })
        
        // 注册别名
        registerAliases()
    }
    
    // 注册类型别名
    function registerAliases() {
        // 数值类型别名
        registerParam("integer", sliderComponent, {
            validator: sliderValidator,
            defaults: (function() {
                var d = sliderDefaults();
                d.decimals = 0;
                return d;
            })()
        })

        registerParam("number", sliderComponent, {
            validator: sliderValidator,
            defaults: (function() {
                var d = sliderDefaults();
                d.decimals = 2;
                return d;
            })()
        })

        registerParam("float", sliderComponent, {
            validator: sliderValidator,
            defaults: (function() {
                var d = sliderDefaults();
                d.decimals = 2;
                return d;
            })()
        })

        registerParam("range", sliderComponent, {
            validator: sliderValidator,
            defaults: sliderDefaults()
        })

        // 字符串类型别名
        registerParam("string", inputComponent, {
            validator: inputValidator,
            defaults: inputDefaults()
        })

        registerParam("text", inputComponent, {
            validator: inputValidator,
            defaults: (function() {
                var d = inputDefaults();
                d.multiline = true;
                return d;
            })()
        })

        // 布尔类型别名
        registerParam("boolean", toggleComponent, {
            validator: toggleValidator,
            defaults: toggleDefaults()
        })

        registerParam("bool", toggleComponent, {
            validator: toggleValidator,
            defaults: toggleDefaults()
        })

        registerParam("checkbox", toggleComponent, {
            validator: toggleValidator,
            defaults: (function() {
                var d = toggleDefaults();
                d.style = "checkbox";
                return d;
            })()
        })

        // 枚举类型别名
        registerParam("enum", selectComponent, {
            validator: selectValidator,
            defaults: selectDefaults()
        })

        registerParam("dropdown", selectComponent, {
            validator: selectValidator,
            defaults: selectDefaults()
        })

        registerParam("combo", selectComponent, {
            validator: selectValidator,
            defaults: selectDefaults()
        })

        registerParam("array", multiselectComponent, {
            validator: multiselectValidator,
            defaults: multiselectDefaults()
        })

        registerParam("list", multiselectComponent, {
            validator: multiselectValidator,
            defaults: multiselectDefaults()
        })
        
        console.log("类型别名注册完成")
    }
    
    // 注销所有组件
    function unregisterAllComponents() {
        var types = getRegisteredTypes()
        for (var i = 0; i < types.length; i++) {
            unregisterParam(types[i])
        }
        console.log("已注销所有参数组件")
    }
    
    // 获取组件注册状态
    function getRegistrationStatus() {
        return {
            registered: getRegisteredTypes(),
            stats: getStats(),
            hasSlider: isTypeRegistered("slider"),
            hasSelect: isTypeRegistered("select"),
            hasMultiSelect: isTypeRegistered("multiselect"),
            hasInput: isTypeRegistered("input"),
            hasToggle: isTypeRegistered("toggle")
        }
    }
    
    // ============ 工具方法 ============

    function resolveNumericBound(prop, minKey, maxKey, fallbackMin, fallbackMax) {
        prop = prop || {}

        function firstDefinedValue(keys, fallbackValue) {
            for (var keyIndex = 0; keyIndex < keys.length; keyIndex++) {
                var key = keys[keyIndex]
                if (key && prop[key] !== undefined && prop[key] !== null && prop[key] !== "") {
                    var numericValue = Number(prop[key])
                    if (!isNaN(numericValue)) {
                        return numericValue
                    }
                }
            }
            return fallbackValue
        }

        var minValue = firstDefinedValue([minKey, "minimum", "min"], fallbackMin)
        var maxValue = firstDefinedValue([maxKey, "maximum", "max"], fallbackMax)

        var presetValues = []
        if (Array.isArray(prop.commonValues)) {
            presetValues = prop.commonValues
        } else if (prop.commonValues !== undefined && prop.commonValues !== null) {
            presetValues = [prop.commonValues]
        }

        for (var i = 0; i < presetValues.length; i++) {
            var preset = Number(presetValues[i])
            if (isNaN(preset)) {
                continue
            }
            if (minValue === undefined || preset < minValue) {
                minValue = preset
            }
            if (maxValue === undefined || preset > maxValue) {
                maxValue = preset
            }
        }

        if (prop.default !== undefined) {
            var defaultValue = Number(prop.default)
            if (!isNaN(defaultValue)) {
                if (minValue === undefined || defaultValue < minValue) {
                    minValue = defaultValue
                }
                if (maxValue !== undefined && defaultValue > maxValue) {
                    maxValue = defaultValue
                }
            }
        }

        if (minValue !== undefined && maxValue !== undefined && maxValue < minValue) {
            maxValue = minValue
        }

        return {
            min: minValue,
            max: maxValue
        }
    }

    function formatStructuredDefaultValue(value, fallbackValue) {
        if (value === undefined || value === null) {
            return fallbackValue
        }

        if (typeof value === "string") {
            return value
        }

        try {
            return JSON.stringify(value, null, 2)
        } catch (error) {
            console.warn("结构化参数默认值格式化失败:", error)
            return fallbackValue
        }
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("ParamComponents 初始化")
        registerAllComponents()
    }
    
    Component.onDestruction: {
        console.log("ParamComponents 销毁")
    }
}

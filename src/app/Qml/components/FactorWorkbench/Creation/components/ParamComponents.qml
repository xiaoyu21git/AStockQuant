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
        
        paramComponents[type] = component
        
        if (options.validator) {
            paramValidators[type] = options.validator
        }
        
        if (options.defaults) {
            paramDefaults[type] = options.defaults
        }
        
        console.log("[ParamComponents] 已注册参数类型:", type)
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
        delete paramComponents[type]
        delete paramValidators[type]
        delete paramDefaults[type]
        console.log("[ParamComponents] 已注销参数类型:", type)
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
        console.log("开始注册参数组件...")
        
        registerParam("slider", sliderComponent, {
            validator: sliderValidator,
            defaults: sliderDefaults()
        })

        registerParam("select", selectComponent, {
            validator: selectValidator,
            defaults: selectDefaults()
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
        
        console.log("参数组件注册完成，已注册类型:", getRegisteredTypes())
        console.log("注册统计:", JSON.stringify(getStats()))
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
            hasInput: isTypeRegistered("input"),
            hasToggle: isTypeRegistered("toggle")
        }
    }
    
    // ============ 工具方法 ============
    
    // 将JSON Schema转换为参数配置
    function schemaToConfigs(schema) {
        if (!schema || !schema.properties) {
            console.warn("无效的JSON Schema")
            return []
        }
        
        var configs = []
        for (var key in schema.properties) {
            var prop = schema.properties[key]
            var config = {
                id: key,
                label: prop.label || prop.title || key,
                description: prop.description || "",
                type: mapSchemaType(prop.type),
                default: prop.default,
                required: prop.required || false
            }
            
            // 类型特定配置
            switch (prop.type) {
                case "integer":
                case "number":
                    config.min = prop.minimum
                    config.max = prop.maximum
                    config.step = prop.step || 1
                    config.unit = prop.unit || ""
                    config.decimals = prop.decimals || (prop.type === "integer" ? 0 : 2)
                    config.showPresets = prop.commonValues !== undefined
                    config.presets = prop.commonValues
                    break
                    
                case "string":
                    config.multiline = prop.multiline || false
                    config.maxLength = prop.maxLength
                    config.minLength = prop.minLength
                    config.pattern = prop.pattern
                    config.patternMessage = prop.patternMessage
                    
                    if (prop.enum) {
                        config.type = "select"
                        config.options = prop.enum
                    }
                    break
                    
                case "boolean":
                    config.type = "toggle"
                    config.trueLabel = prop.trueLabel || "是"
                    config.falseLabel = prop.falseLabel || "否"
                    break
                    
                case "array":
                    config.type = "select"
                    config.options = prop.items ? prop.items.enum : []
                    config.multiple = true
                    break
                    
                case "object":
                    config.type = "group"
                    config.properties = prop.properties
                    break
            }
            
            configs.push(config)
        }
        
        console.log("JSON Schema转换完成，生成", configs.length, "个参数配置")
        return configs
    }
    
    // 映射Schema类型到参数类型
    function mapSchemaType(schemaType) {
        var typeMap = {
            "integer": "slider",
            "number": "slider",
            "string": "input",
            "boolean": "toggle",
            "array": "select",
            "object": "group"
        }
        return typeMap[schemaType] || "input"
    }
    
    // 创建参数配置示例
    function createExampleConfigs() {
        return [
            {
                id: "lookback",
                type: "slider",
                label: "回看周期",
                description: "计算因子值所需的历史数据长度",
                min: 5,
                max: 250,
                step: 5,
                default: 20,
                unit: "天",
                showPresets: true,
                presets: [20, 60, 120, 250],
                required: true
            },
            {
                id: "method",
                type: "select",
                label: "计算方法",
                description: "使用的计算方法",
                options: [
                    { value: "simple", label: "简单动量" },
                    { value: "weighted", label: "加权动量" },
                    { value: "residual", label: "残差动量" }
                ],
                default: "simple",
                required: true
            },
            {
                id: "neutralize",
                type: "toggle",
                label: "中性化处理",
                description: "是否进行行业中性化",
                default: false,
                trueLabel: "启用",
                falseLabel: "禁用"
            },
            {
                id: "factorName",
                type: "input",
                label: "因子名称",
                description: "请输入因子名称",
                default: "",
                placeholder: "例如：动量因子_60日",
                maxLength: 50,
                required: true
            }
        ]
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

// DynamicParamGenerator.qml
// 动态参数生成器 - 方案2插件化组件注册的核心
// 根据参数配置动态生成参数表单

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 动态参数生成器
 * 
 * 功能：
 * 1. 根据参数配置数组动态生成参数组件
 * 2. 管理参数值的收集和验证
 * 3. 支持参数分组和条件显示
 * 4. 提供统一的参数访问接口
 * 5. 支持水平网格布局，充分利用宽度
 * 
 * 使用方式：
 * DynamicParamGenerator {
 *     configs: [
 *         { id: "lookback", type: "slider", label: "回看周期", min: 5, max: 250, default: 20 },
 *         { id: "method", type: "select", label: "计算方法", options: ["简单动量", "加权动量"] }
 *     ]
 *     onValuesChanged: console.log("参数值变化:", values)
 * }
 */
Item {
    id: root
    
    // ============ 公共属性 ============
    
    // 参数配置数组
    property var configs: []
    
    // 当前参数值 { id: value }
    property var values: ({})
    
    // 参数验证状态
    property bool allValid: true
    property var validationErrors: ({})
    
    // 参数分组配置
    property var groups: []
    property bool showGroups: false
    
    // 参数组件间距
    property int itemSpacing: 16
    property int minColumnWidth: 320
    property int maxColumns: 3
    readonly property int responsiveColumns: {
        return root.calculateResponsiveColumns(flickable.width, root.maxColumns)
    }
    
    // 参数组件注册表实例（使用 var 类型避免绑定循环）
    property var paramRegistry: null
    readonly property bool registryReady: {
        if (!paramRegistry) {
            return false
        }

        if (typeof paramRegistry.isTypeRegistered !== "function") {
            return false
        }

        return paramRegistry.isTypeRegistered("slider")
            && paramRegistry.isTypeRegistered("select")
            && paramRegistry.isTypeRegistered("toggle")
            && paramRegistry.isTypeRegistered("input")
    }
    
    // 信号
    signal paramsChanged(var newValues)
    signal validationChanged(bool allValid, var errors)
    signal paramValueChanged(string paramId, var value)
    
    // ============ 内部状态 ============
    
    // 参数组件实例映射 { id: component }
    property var paramInstances: ({})
    property bool suppressParamValuePropagation: false
    
    // 参数分组映射 { groupId: [paramIds] }
    property var groupParams: ({})
    
    // 内部配置列表模型
    property var configsList: []
    property var groupedConfigsList: []
    readonly property bool useGroupedLayout: root.showGroups && root.groupedConfigsList.length > 0
    
    // ============ UI 布局 ============
    
    // 隐式高度，用于外部布局计算
    implicitHeight: flickable.contentHeight
    
    // 性能优化：添加缓存和懒加载标志
    property bool enableLazyLoading: true
    property int visibleItemCount: 0
    property int batchSize: 6 // 每次加载的组件数量
    
    // 主布局容器
    Flickable {
        id: flickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        
        // 懒加载检查器
        property bool isAtBottom: contentY + height >= contentHeight - 50
        onIsAtBottomChanged: {
            if (isAtBottom && root.enableLazyLoading && visibleItemCount < root.configsList.length) {
                loadMoreItems()
            }
        }
        
        // 主内容列
        Column {
            id: contentColumn
            width: flickable.width
            spacing: root.itemSpacing
            
            // 无参数时显示提示
            Text {
                width: parent.width
                text: "暂无参数配置"
                font.pixelSize: 14
                color: "#64748B"
                visible: root.configsList.length === 0
                horizontalAlignment: Text.AlignHCenter
                padding: 20
            }
            
            Column {
                width: parent.width
                spacing: root.itemSpacing
                visible: root.useGroupedLayout

                Repeater {
                    model: root.useGroupedLayout ? root.groupedConfigsList : []

                    delegate: Column {
                        width: contentColumn.width
                        spacing: 10

                        Column {
                            width: parent.width
                            spacing: 4

                            Row {
                                width: parent.width
                                spacing: 10

                                Rectangle {
                                    id: groupChip
                                    width: groupTitle.implicitWidth + 18
                                    height: 28
                                    radius: 14
                                    color: "#172554"
                                    border.width: 1
                                    border.color: "#31539A"

                                    Text {
                                        id: groupTitle
                                        anchors.centerIn: parent
                                        text: modelData.name || "参数分组"
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: "#BFDBFE"
                                    }
                                }

                                Rectangle {
                                    width: Math.max(0, parent.width - groupChip.width - 10)
                                    height: 1
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: "#23324A"
                                }
                            }

                            Text {
                                width: parent.width
                                visible: !!modelData.description
                                text: modelData.description || ""
                                font.pixelSize: 11
                                color: "#94A3B8"
                                wrapMode: Text.WordWrap
                            }
                        }

                        GridLayout {
                            width: parent.width
                            columns: root.resolveGroupColumns(modelData)
                            property real columnWidth: root.calculateColumnWidth(width, columns)
                            columnSpacing: 16
                            rowSpacing: 16

                            Repeater {
                                model: modelData.configs || []
                                delegate: paramLoaderDelegate
                            }
                        }
                    }
                }
            }

            // 参数网格布局 - 2列布局充分利用宽度
            GridLayout {
                id: paramsGrid
                width: parent.width
                columns: root.responsiveColumns
                property real columnWidth: root.calculateColumnWidth(width, columns)
                columnSpacing: 16
                rowSpacing: 16
                visible: root.configsList.length > 0 && !root.useGroupedLayout
                
                // 直接遍历配置列表生成参数组件
                Repeater {
                    id: paramsRepeater
                    model: root.useGroupedLayout ? [] : root.configsList

                    delegate: paramLoaderDelegate
                }
            }
        }
    }

    Component {
        id: paramLoaderDelegate

        Loader {
            id: paramLoader
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: parent && parent.columnWidth ? parent.columnWidth : -1
            Layout.minimumWidth: parent && parent.columnWidth ? parent.columnWidth : -1
            Layout.maximumWidth: parent && parent.columnWidth ? parent.columnWidth : Number.POSITIVE_INFINITY
            Layout.preferredHeight: visible && item && item.implicitHeight > 0 ? item.implicitHeight : 0
            Layout.minimumHeight: visible ? 96 : 0
            visible: currentConfig ? isConfigVisible(currentConfig, root.values) : true

            // 当前参数配置
            property var currentConfig: modelData
            property string paramType: currentConfig ? (currentConfig.type || "input") : "input"
            property int registryVersion: root.paramRegistry && root.paramRegistry.registryVersion !== undefined
                ? root.paramRegistry.registryVersion
                : 0

            // 动态获取组件
            sourceComponent: getComponentForType(paramType)

            // 获取参数类型对应的组件
            function getComponentForType(type) {
                var currentRegistryVersion = registryVersion
                if (!root.paramRegistry) {
                    console.warn("参数组件注册表未设置")
                    return null
                }
                if (!root.registryReady) {
                    return null
                }
                var comp = root.paramRegistry.getComponent(type)
                if (!comp) {
                    console.warn("未找到类型组件:", type)
                }
                return comp
            }

            onLoaded: {
                if (!item || !currentConfig) return

                console.log("加载参数组件:", currentConfig.id, "类型:", paramType)

                // 设置配置
                item.config = currentConfig

                // 设置初始值
                var initialValue = root.values[currentConfig.id]
                if (initialValue !== undefined) {
                    root.suppressParamValuePropagation = true
                    if (typeof item.setValue === "function") {
                        item.setValue(initialValue)
                    } else {
                        item.value = initialValue
                    }
                    root.suppressParamValuePropagation = false
                } else if (currentConfig.default !== undefined) {
                    root.suppressParamValuePropagation = true
                    if (typeof item.setValue === "function") {
                        item.setValue(currentConfig.default)
                    } else {
                        item.value = currentConfig.default
                    }
                    root.suppressParamValuePropagation = false
                    // 更新值到父组件
                    root.values[currentConfig.id] = currentConfig.default
                }

                // 连接信号
                if (item.paramValueChanged) {
                    item.paramValueChanged.connect(function(paramId, newValue) {
                        handleParamValueChanged(paramId, newValue)
                    })
                }

                if (item.validationChanged) {
                    item.validationChanged.connect(function(paramId, valid, message) {
                        handleParamValidationChanged(paramId, valid, message)
                    })
                }

                if (item.paramValidationChanged) {
                    item.paramValidationChanged.connect(function(paramId, valid, message) {
                        handleParamValidationChanged(paramId, valid, message)
                    })
                }

                // 保存组件实例
                root.paramInstances[currentConfig.id] = item
            }
        }
    }
    
    // 懒加载更多项目
    function loadMoreItems() {
        var remaining = root.configsList.length - root.visibleItemCount
        var toLoad = Math.min(root.batchSize, remaining)
        
        if (toLoad <= 0) return
        
        // 更新可见项目数量
        root.visibleItemCount += toLoad
        
        // 在真实实现中，这里会更新配置列表以显示更多项目
        // 为了简化，我们假设所有项目都已加载
        // 实际实现可能需要一个可见配置的子集
    }

    function calculateResponsiveColumns(availableWidth, maxAllowedColumns) {
        var resolvedWidth = Math.max(0, availableWidth)
        if (resolvedWidth <= 0) {
            return 1
        }

        var resolvedMaxColumns = Math.max(1, Number(maxAllowedColumns) || root.maxColumns)
        var estimatedColumns = 1
        if (resolvedWidth >= 1180) {
            estimatedColumns = 3
        } else if (resolvedWidth >= 760) {
            estimatedColumns = 2
        }
        return Math.min(resolvedMaxColumns, estimatedColumns)
    }

    function calculateColumnWidth(availableWidth, columnCount) {
        var resolvedWidth = Math.max(0, Number(availableWidth) || 0)
        var resolvedColumns = Math.max(1, Number(columnCount) || 1)
        var spacingTotal = Math.max(0, resolvedColumns - 1) * 16
        return Math.max(0, Math.floor((resolvedWidth - spacingTotal) / resolvedColumns))
    }

    function resolveGroupMinColumnWidth(group) {
        if (group && group.minColumnWidth !== undefined && group.minColumnWidth !== null) {
            return Number(group.minColumnWidth)
        }
        return root.minColumnWidth
    }

    function resolveGroupMaxColumns(group) {
        if (group && group.maxColumns !== undefined && group.maxColumns !== null) {
            return Number(group.maxColumns)
        }
        return root.maxColumns
    }

    function resolveGroupColumns(group) {
        return calculateResponsiveColumns(
            flickable.width,
            resolveGroupMaxColumns(group))
    }
    
    // 获取参数配置
    function getParamConfig(paramId) {
        for (var i = 0; i < root.configs.length; i++) {
            if (root.configs[i].id === paramId) {
                return root.configs[i]
            }
        }
        return null
    }

    function getLinkedWeightSelectionField(groupId) {
        if (groupId === "low_volatility") {
            return "components"
        }
        if (groupId === "growth") {
            return "growthMetrics"
        }
        if (groupId === "value") {
            return "valuationMetrics"
        }
        return ""
    }

    function getLinkedWeightComponentId(groupId, paramId) {
        if (groupId === "low_volatility") {
            var lowVolMapping = {
                "volatilityWeight": "volatility",
                "drawdownWeight": "drawdown",
                "betaWeight": "beta"
            }
            return lowVolMapping[paramId] || ""
        }

        if (groupId === "growth") {
            var growthMapping = {
                "revenueGrowthWeight": "revenue_growth",
                "netProfitGrowthWeight": "net_profit_growth",
                "deltaRoeWeight": "delta_roe",
                "sueWeight": "sue"
            }
            return growthMapping[paramId] || ""
        }

        if (groupId === "value") {
            var valueMapping = {
                "bpWeight": "bp",
                "epWeight": "ep",
                "dividendYieldWeight": "dividend_yield",
                "cfPWeight": "cf_p"
            }
            return valueMapping[paramId] || ""
        }

        return ""
    }

    function getLinkedWeightSelectionValues(groupId, values) {
        var sourceValues = values && typeof values === "object" ? values : root.values
        if (!sourceValues) {
            return []
        }

        var selectionField = getLinkedWeightSelectionField(groupId)
        if (!selectionField || sourceValues[selectionField] === undefined || sourceValues[selectionField] === null) {
            return []
        }

        var selected = Array.isArray(sourceValues[selectionField]) ? sourceValues[selectionField] : [sourceValues[selectionField]]
        return selected.slice()
    }

    function getTechnicalSelectedIndicators(values) {
        var sourceValues = values && typeof values === "object" ? values : root.values
        if (!sourceValues) {
            return []
        }

        var rawIndicators = []
        if (sourceValues.technicalIndicators !== undefined && sourceValues.technicalIndicators !== null) {
            rawIndicators = Array.isArray(sourceValues.technicalIndicators)
                ? sourceValues.technicalIndicators
                : [sourceValues.technicalIndicators]
        } else if (sourceValues.indicatorTypes !== undefined && sourceValues.indicatorTypes !== null) {
            rawIndicators = Array.isArray(sourceValues.indicatorTypes)
                ? sourceValues.indicatorTypes
                : [sourceValues.indicatorTypes]
        } else if (sourceValues.indicatorType !== undefined && sourceValues.indicatorType !== null) {
            rawIndicators = [sourceValues.indicatorType]
        } else if (sourceValues.indicator_type !== undefined && sourceValues.indicator_type !== null) {
            rawIndicators = [sourceValues.indicator_type]
        }

        var selected = []
        for (var i = 0; i < rawIndicators.length; i++) {
            var indicator = String(rawIndicators[i] || "").trim().toLowerCase()
            if (!indicator || selected.indexOf(indicator) >= 0) {
                continue
            }
            selected.push(indicator)
        }
        return selected
    }

    function isTechnicalParameterVisible(paramId, values) {
        var selectedIndicators = getTechnicalSelectedIndicators(values)

        if (paramId === "technicalIndicators") {
            return true
        }

        if (selectedIndicators.length === 0) {
            return false
        }

        if (paramId === "technicalCombinationMode") {
            return selectedIndicators.length > 1
        }

        var indicatorVisibilityMap = {
            "technicalPriceType": ["rsi", "macd", "ma", "ema", "boll", "kdj", "atr", "obv", "vwap"],
            "rsiWindow": ["rsi"],
            "maWindow": ["ma"],
            "emaWindow": ["ema"],
            "bollWindow": ["boll"],
            "bollStdDev": ["boll"],
            "kdjWindow": ["kdj"],
            "kdjKPeriod": ["kdj"],
            "kdjDPeriod": ["kdj"],
            "atrWindow": ["atr"],
            "macdFastPeriod": ["macd"],
            "macdSlowPeriod": ["macd"],
            "macdSignalPeriod": ["macd"],
            "obvWindow": ["obv"],
            "vwapWindow": ["vwap"],
            "volumeRatioWindow": ["volume_ratio"],
            "turnoverStabilityWindow": ["turnover_stability"],
            "turnoverStabilityMetric": ["turnover_stability"]
        }

        var requiredIndicators = indicatorVisibilityMap[paramId]
        if (!requiredIndicators) {
            return true
        }

        for (var index = 0; index < requiredIndicators.length; index++) {
            if (selectedIndicators.indexOf(requiredIndicators[index]) >= 0) {
                return true
            }
        }

        return false
    }

    function isConfigVisible(config, values) {
        if (!config) {
            return true
        }

        if (config.linkedWeightGroup) {
            return isLinkedWeightVisible(config.id, values)
        }

        if (config.id === "technicalIndicators" || config.id === "technicalPriceType"
                || config.id === "rsiWindow" || config.id === "maWindow" || config.id === "emaWindow"
                || config.id === "bollWindow" || config.id === "bollStdDev" || config.id === "kdjWindow"
                || config.id === "kdjKPeriod" || config.id === "kdjDPeriod" || config.id === "atrWindow"
                || config.id === "macdFastPeriod" || config.id === "macdSlowPeriod" || config.id === "macdSignalPeriod"
                || config.id === "obvWindow" || config.id === "vwapWindow" || config.id === "volumeRatioWindow"
                || config.id === "turnoverStabilityWindow" || config.id === "turnoverStabilityMetric"
                || config.id === "technicalCombinationMode") {
            return isTechnicalParameterVisible(config.id, values)
        }

        return true
    }

    function isLinkedWeightVisible(paramId, values) {
        var config = getParamConfig(paramId)
        if (!config || !config.linkedWeightGroup) {
            return true
        }

        var selectedValues = getLinkedWeightSelectionValues(config.linkedWeightGroup, values)

        var componentId = getLinkedWeightComponentId(config.linkedWeightGroup, paramId)
        if (!componentId) {
            return true
        }

        return selectedValues.indexOf(componentId) >= 0
    }

    function getLinkedWeightConfigs(groupId) {
        var configs = []
        for (var i = 0; i < root.configs.length; i++) {
            var config = root.configs[i]
            if (config && config.linkedWeightGroup === groupId) {
                configs.push(config)
            }
        }
        return configs
    }

    function getLinkedWeightDecimals(config) {
        if (config && config.linkedWeightDecimals !== undefined && config.linkedWeightDecimals !== null) {
            return Math.max(0, Number(config.linkedWeightDecimals) || 0)
        }
        if (config && config.decimals !== undefined && config.decimals !== null) {
            return Math.max(0, Number(config.decimals) || 0)
        }
        return 1
    }

    function roundToDecimals(value, decimals) {
        var precision = Math.max(0, Number(decimals) || 0)
        var factor = Math.pow(10, precision)
        return Math.round(Number(value) * factor) / factor
    }

    function clampLinkedWeightValue(value, decimals) {
        var numericValue = Number(value)
        if (!isFinite(numericValue)) {
            numericValue = 0
        }
        if (numericValue < 0) {
            numericValue = 0
        }
        if (numericValue > 100) {
            numericValue = 100
        }
        return roundToDecimals(numericValue, decimals !== undefined ? decimals : 1)
    }

    function normalizeLinkedWeightValues(paramId, value, sourceValues) {
        var changedConfig = getParamConfig(paramId)
        if (!changedConfig || !changedConfig.linkedWeightGroup) {
            return null
        }

        var proposedValues = {}
        for (var key in sourceValues) {
            proposedValues[key] = sourceValues[key]
        }
        proposedValues[paramId] = value

        return normalizeLinkedWeightGroupValues(changedConfig.linkedWeightGroup, proposedValues, sourceValues)
    }

    function normalizeLinkedWeightGroupValues(groupId, sourceValues, previousValues) {
        var values = sourceValues && typeof sourceValues === "object" ? sourceValues : ({})
        var normalizedValues = {}

        for (var key in values) {
            normalizedValues[key] = values[key]
        }

        var selectedComponents = getLinkedWeightSelectionValues(groupId, values)
        var previousSelectedComponents = getLinkedWeightSelectionValues(groupId, previousValues)
        var weightConfigs = getLinkedWeightConfigs(groupId)
        if (weightConfigs.length === 0) {
            return normalizedValues
        }

        if (selectedComponents.length === 0) {
            for (var emptyIndex = 0; emptyIndex < weightConfigs.length; emptyIndex++) {
                normalizedValues[weightConfigs[emptyIndex].id] = 0
            }
            return normalizedValues
        }

        var activeConfigs = []
        var activeSourceSum = 0
        for (var i = 0; i < weightConfigs.length; i++) {
            var config = weightConfigs[i]
            if (!config) {
                continue
            }

            var componentId = getLinkedWeightComponentId(groupId, config.id)

            if (!componentId || selectedComponents.indexOf(componentId) < 0) {
                normalizedValues[config.id] = 0
                continue
            }

            activeConfigs.push(config)

            var sourceValue = values[config.id]
            if (sourceValue === undefined || sourceValue === null || sourceValue === "") {
                sourceValue = config.default !== undefined ? config.default : 0
            } else if (previousSelectedComponents.indexOf(componentId) < 0 && config.default !== undefined) {
                sourceValue = config.default
            }

            sourceValue = clampLinkedWeightValue(sourceValue, getLinkedWeightDecimals(config))
            normalizedValues[config.id] = sourceValue
            activeSourceSum += sourceValue
        }

        if (activeConfigs.length === 0) {
            return normalizedValues
        }

        var totalTarget = 100
        var precision = getLinkedWeightDecimals(activeConfigs[0])
        if (activeSourceSum > 0) {
            var allocated = 0
            for (var j = 0; j < activeConfigs.length; j++) {
                var activeConfig = activeConfigs[j]
                if (j === activeConfigs.length - 1) {
                    normalizedValues[activeConfig.id] = roundToDecimals(totalTarget - allocated, precision)
                    continue
                }

                var currentSource = normalizedValues[activeConfig.id]
                var nextValue = totalTarget * currentSource / activeSourceSum
                nextValue = roundToDecimals(nextValue, precision)
                allocated += nextValue
                normalizedValues[activeConfig.id] = nextValue
            }
        } else {
            var equalValue = roundToDecimals(totalTarget / activeConfigs.length, precision)
            var equalAllocated = 0
            for (var k = 0; k < activeConfigs.length; k++) {
                var equalConfig = activeConfigs[k]
                if (k === activeConfigs.length - 1) {
                    normalizedValues[equalConfig.id] = roundToDecimals(totalTarget - equalAllocated, precision)
                    continue
                }

                normalizedValues[equalConfig.id] = equalValue
                equalAllocated += equalValue
            }
        }

        return normalizedValues
    }

    function normalizeLowVolWeights(sourceValues, previousValues) {
        return normalizeLinkedWeightGroupValues("low_volatility", sourceValues, previousValues)
    }

    function applyLinkedWeightValues(paramId, updates) {
        if (!updates) {
            return
        }

        root.values = updates
        root.suppressParamValuePropagation = true
        for (var i = 0; i < root.configs.length; i++) {
            var config = root.configs[i]
            if (!config || updates[config.id] === undefined) {
                continue
            }

            var instance = root.paramInstances[config.id]
            if (instance && typeof instance.setValue === "function") {
                instance.setValue(updates[config.id])
            } else if (instance) {
                instance.value = updates[config.id]
            }
        }
        root.suppressParamValuePropagation = false
        root.paramValueChanged(paramId, updates[paramId])
        root.paramsChanged(root.values)
        validateAll()
        updateValidationState()
    }
    
    // 处理参数值变化
    function handleParamValueChanged(paramId, newValue) {
        console.log("参数值变化:", paramId, "=", newValue)

        if (paramId === "components" || paramId === "growthMetrics" || paramId === "valuationMetrics") {
            var previousValues = {}
            for (var previousKey in root.values) {
                previousValues[previousKey] = root.values[previousKey]
            }

            var mergedValues = {}
            for (var mergedKey in root.values) {
                mergedValues[mergedKey] = root.values[mergedKey]
            }
            mergedValues[paramId] = newValue

            syncValues(mergedValues, previousValues)
            root.paramValueChanged(paramId, root.values[paramId])
            return
        }

        var linkedUpdates = normalizeLinkedWeightValues(paramId, newValue, root.values)
        if (linkedUpdates) {
            applyLinkedWeightValues(paramId, linkedUpdates)
            return
        }

        if (root.suppressParamValuePropagation) {
            var suppressedValues = {}
            for (var suppressedKey in root.values) {
                suppressedValues[suppressedKey] = root.values[suppressedKey]
            }
            suppressedValues[paramId] = newValue
            root.values = suppressedValues
            validateParam(paramId)
            return
        }
        
        // 更新值
        var newValues = {}
        for (var key in root.values) {
            newValues[key] = root.values[key]
        }
        newValues[paramId] = newValue
        root.values = newValues
        
        // 更新对应的组件实例，确保UI同步
        var instance = root.paramInstances[paramId]
        if (instance && typeof instance.setValue === "function") {
            // 防止循环调用，检查值是否已经相同
            if (instance.value !== newValue) {
                instance.setValue(newValue)
            }
        }
        
        // 发出信号
        root.paramValueChanged(paramId, newValue)
        root.paramsChanged(root.values)
        
        // 验证参数
        validateParam(paramId)
    }
    
    // 处理参数验证变化
    function handleParamValidationChanged(paramId, valid, message) {
        var newErrors = {}
        for (var key in root.validationErrors) {
            newErrors[key] = root.validationErrors[key]
        }
        
        if (valid) {
            delete newErrors[paramId]
        } else {
            newErrors[paramId] = message
        }
        root.validationErrors = newErrors
        
        // 更新整体验证状态
        updateValidationState()
    }
    
    // 验证单个参数
    function validateParam(paramId) {
        var instance = root.paramInstances[paramId]
        if (instance && typeof instance.validate === "function") {
            return instance.validate()
        }
        return true
    }
    
    // 验证所有参数
    function validateAll() {
        var valid = true
        
        for (var paramId in root.paramInstances) {
            var paramValid = validateParam(paramId)
            if (!paramValid) {
                valid = false
            }
        }
        
        root.allValid = valid
        return valid
    }
    
    // 更新验证状态
    function updateValidationState() {
        var errorCount = Object.keys(root.validationErrors).length
        root.allValid = errorCount === 0
        
        root.validationChanged(root.allValid, root.validationErrors)
    }
    
    // 获取所有参数值
    function getValues() {
        return root.values
    }
    
    // 设置参数值
    function setValue(paramId, value) {
        var instance = root.paramInstances[paramId]
        if (instance && typeof instance.setValue === "function") {
            instance.setValue(value)
        } else {
            handleParamValueChanged(paramId, value)
        }
    }
    
    // 批量设置参数值
    function setValues(newValues) {
        for (var paramId in newValues) {
            setValue(paramId, newValues[paramId])
        }
    }

    function syncValues(newValues, previousValues) {
        var sourceValues = newValues && typeof newValues === "object" ? newValues : ({})
        var mergedValues = {}

        for (var index = 0; index < root.configs.length; index++) {
            var config = root.configs[index]
            if (!config || !config.id) {
                continue
            }

            if (sourceValues[config.id] !== undefined) {
                mergedValues[config.id] = sourceValues[config.id]
            } else if (config.default !== undefined) {
                mergedValues[config.id] = config.default
            }
        }

        var linkedGroups = ({})
        for (var linkedIndex = 0; linkedIndex < root.configs.length; linkedIndex++) {
            var linkedConfig = root.configs[linkedIndex]
            if (linkedConfig && linkedConfig.linkedWeightGroup) {
                linkedGroups[linkedConfig.linkedWeightGroup] = true
            }
        }

        for (var linkedGroupId in linkedGroups) {
            var normalized = normalizeLinkedWeightGroupValues(linkedGroupId, mergedValues, previousValues)
            if (normalized) {
                mergedValues = normalized
            }
        }

        root.values = mergedValues
        root.validationErrors = {}
        root.suppressParamValuePropagation = true

        for (var paramId in mergedValues) {
            var instance = root.paramInstances[paramId]
            if (!instance) {
                continue
            }

            if (typeof instance.setValue === "function") {
                instance.setValue(mergedValues[paramId])
            } else {
                instance.value = mergedValues[paramId]
            }
        }

        root.suppressParamValuePropagation = false
        validateAll()
        root.paramsChanged(root.values)
        updateValidationState()
    }
    
    // 重置所有参数为默认值
    function resetToDefaults() {
        for (var paramId in root.paramInstances) {
            var instance = root.paramInstances[paramId]
            if (instance && typeof instance.reset === "function") {
                instance.reset()
            }
        }
        
        // 重置值对象
        var newValues = {}
        root.validationErrors = {}
        
        // 重新初始化默认值
        for (var i = 0; i < root.configs.length; i++) {
            var config = root.configs[i]
            if (config.default !== undefined) {
                newValues[config.id] = config.default
            }
        }
        root.values = newValues
        
        root.paramsChanged(root.values)
        updateValidationState()
    }
    
    // 获取参数组件实例
    function getParamInstance(paramId) {
        return root.paramInstances[paramId]
    }
    
    // 获取参数验证错误
    function getValidationErrors() {
        return root.validationErrors
    }
    
    // 检查参数是否有效
    function isParamValid(paramId) {
        return !root.validationErrors[paramId]
    }
    
    // 获取无效参数列表
    function getInvalidParams() {
        return Object.keys(root.validationErrors)
    }
    
    // 根据条件筛选参数
    function filterParams(conditionFunc) {
        return root.configs.filter(conditionFunc)
    }
    
    // 重新加载参数配置
    function reloadConfigs(newConfigs, newGroups, initialValues) {
        // 清除现有组件实例引用
        root.paramInstances = {}
        root.validationErrors = {}
        
        // 更新配置
        root.configs = newConfigs || []
        root.groups = newGroups || []
        
        // 更新配置列表
        updateConfigsList()
        
        // 使用现有值或默认值重新初始化，避免 schema 重建时丢失编辑中的参数
        var sourceValues = initialValues && typeof initialValues === "object"
            ? initialValues
            : ({})
        var newValues = {}
        for (var i = 0; i < root.configs.length; i++) {
            var config = root.configs[i]
            if (sourceValues[config.id] !== undefined) {
                newValues[config.id] = sourceValues[config.id]
            } else if (config.default !== undefined) {
                newValues[config.id] = config.default
            }
        }
        root.values = newValues

        Qt.callLater(function() {
            root.syncValues(root.values)
        })
    }
    
    // 更新配置列表
    function updateConfigsList() {
        var list = []
        var configMap = ({})
        var grouped = []
        var assigned = ({})
        var groupedParamIds = ({})

        for (var i = 0; i < root.configs.length; i++) {
            var config = root.configs[i]
            list.push(config)
            if (config && config.id) {
                configMap[config.id] = config
            }
        }

        for (var groupIndex = 0; groupIndex < root.groups.length; groupIndex++) {
            var group = root.groups[groupIndex]
            if (!group) {
                continue
            }

            var groupConfigs = []
            var groupIds = []
            var paramIds = group.params || []
            for (var paramIndex = 0; paramIndex < paramIds.length; paramIndex++) {
                var paramId = paramIds[paramIndex]
                var groupedConfig = configMap[paramId]
                if (!groupedConfig || assigned[paramId]) {
                    continue
                }

                assigned[paramId] = true
                groupConfigs.push(groupedConfig)
                groupIds.push(paramId)
            }

            if (groupConfigs.length > 0) {
                var groupKey = group.id || group.name || ("group_" + groupIndex)
                groupedParamIds[groupKey] = groupIds
                grouped.push({
                    id: groupKey,
                    name: group.name || groupKey,
                    description: group.description || "",
                    minColumnWidth: group.minColumnWidth,
                    maxColumns: group.maxColumns,
                    configs: groupConfigs
                })
            }
        }

        var ungroupedConfigs = []
        var ungroupedIds = []
        for (var configIndex = 0; configIndex < list.length; configIndex++) {
            var currentConfig = list[configIndex]
            if (!currentConfig || !currentConfig.id || assigned[currentConfig.id]) {
                continue
            }

            ungroupedConfigs.push(currentConfig)
            ungroupedIds.push(currentConfig.id)
        }

        if (ungroupedConfigs.length > 0) {
            groupedParamIds.ungrouped = ungroupedIds
            grouped.push({
                id: "ungrouped",
                name: "其他参数",
                description: "",
                configs: ungroupedConfigs
            })
        }

        root.configsList = list
        root.groupParams = groupedParamIds
        root.groupedConfigsList = grouped
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化配置列表
        updateConfigsList()
        
        // 初始化默认值
        var newValues = {}
        for (var i = 0; i < root.configs.length; i++) {
            var config = root.configs[i]
            if (config.default !== undefined) {
                newValues[config.id] = config.default
            }
        }
        root.values = newValues
    }
    
    onConfigsChanged: {
        updateConfigsList()
    }

    onGroupsChanged: {
        updateConfigsList()
    }
}

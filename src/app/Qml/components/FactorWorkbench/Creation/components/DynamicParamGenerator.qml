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
            Layout.preferredHeight: item && item.implicitHeight > 0 ? item.implicitHeight : 112
            Layout.minimumHeight: 96

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
    
    // 处理参数值变化
    function handleParamValueChanged(paramId, newValue) {
        console.log("参数值变化:", paramId, "=", newValue)

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

    function syncValues(newValues) {
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

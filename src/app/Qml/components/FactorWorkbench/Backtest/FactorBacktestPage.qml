// FactorBacktestPage.qml
// 因子回测页面 - 重新设计版本
// 专注于回测进度监控和分组内容展示
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

/**
 * 因子回测页面组件 - 重新设计版本
 * 专注于回测进度监控和分组内容展示
 */
Item {
    id: root

    signal analysisReportRequested(var result)

    function normalizeStringList(value) {
        var normalized = []
        var seen = {}

        if (value === undefined || value === null) {
            return normalized
        }

        var values = []
        if (Array.isArray(value)) {
            values = value
        } else if (typeof value === "string") {
            values = [value]
        } else if (value.length !== undefined) {
            for (var i = 0; i < value.length; i++) {
                values.push(value[i])
            }
        } else {
            values = [value]
        }

        for (var j = 0; j < values.length; j++) {
            var item = values[j]
            if (item === undefined || item === null) {
                continue
            }

            var normalizedItem = String(item).trim().toLowerCase()
            if (!normalizedItem || seen[normalizedItem]) {
                continue
            }

            seen[normalizedItem] = true
            normalized.push(normalizedItem)
        }

        return normalized
    }

    function currentCacheDatasetInfo() {
        if (selectedDatasetId > 0 && cacheDatasetOptions) {
            for (var i = 0; i < cacheDatasetOptions.length; i++) {
                var option = cacheDatasetOptions[i]
                if (option && option.value === selectedDatasetId && option.raw) {
                    return option.raw
                }
            }
        }

        if (cleanedDataController && cleanedDataController.selectedDatasetInfo
                && cleanedDataController.selectedDatasetInfo.id !== undefined
                && cleanedDataController.selectedDatasetInfo.id === selectedDatasetId) {
            return cleanedDataController.selectedDatasetInfo
        }

        return null
    }

    function currentCacheAvailableFields() {
        var datasetInfo = currentCacheDatasetInfo()
        if (!datasetInfo || !datasetInfo.availableFields) {
            return []
        }

        return normalizeStringList(datasetInfo.availableFields)
    }

    function currentCacheFieldDiagnostics() {
        if (cleanedDataController && cleanedDataController.selectedDatasetFieldDiagnostics) {
            return cleanedDataController.selectedDatasetFieldDiagnostics
        }

        var datasetInfo = currentCacheDatasetInfo()
        if (datasetInfo && datasetInfo.fieldDiagnostics) {
            return datasetInfo.fieldDiagnostics
        }

        return ({})
    }

    function extractFactorRequiredFields(factorDefinition) {
        if (!factorDefinition || !factorDefinition.config) {
            return []
        }

        var config = factorDefinition.config
        var dataRequirements = config.data_requirements
        if (!dataRequirements || !dataRequirements.required) {
            return []
        }

        return normalizeStringList(dataRequirements.required)
    }

    function currentCacheFactorSupportMap() {
        var supportMap = ({})
        if (!factorService || !factorService.getAllFactors) {
            return supportMap
        }

        var factorDefinitions = factorService.getAllFactors()
        var cacheMode = selectedDataSourceMode === "cache"
        var availableFields = currentCacheAvailableFields()
        var fieldDiagnostics = currentCacheFieldDiagnostics()
        var fieldSet = ({})
        var positiveRequiredFields = ({
            "open": true,
            "high": true,
            "low": true,
            "close": true,
            "volume": true,
            "turnover": true,
            "pe_ratio": true,
            "pb_ratio": true,
            "market_cap": true,
            "circulating_market_cap": true
        })

        for (var i = 0; i < availableFields.length; i++) {
            fieldSet[availableFields[i]] = true
        }

        for (var index = 0; index < factorDefinitions.length; index++) {
            var factorDefinition = factorDefinitions[index]
            if (!factorDefinition || !factorDefinition.factorId) {
                continue
            }

            var factorId = String(factorDefinition.factorId)
            var requiredFields = extractFactorRequiredFields(factorDefinition)
            var missingFields = []
            var supported = true
            var reason = ""

            if (cacheMode) {
                if (availableFields.length === 0) {
                    supported = false
                    reason = selectedDatasetId > 0
                        ? "当前缓存集缺少字段信息"
                        : "请先选择缓存集"
                } else {
                    for (var fieldIndex = 0; fieldIndex < requiredFields.length; fieldIndex++) {
                        var requiredField = requiredFields[fieldIndex]
                        if (!fieldSet[requiredField]) {
                            missingFields.push(requiredField)
                        }
                    }

                    supported = missingFields.length === 0
                    if (!supported) {
                        reason = "当前缓存缺少字段: " + missingFields.join(", ")
                    } else {
                        for (var diagIndex = 0; diagIndex < requiredFields.length; diagIndex++) {
                            var fieldName = requiredFields[diagIndex]
                            var diagnostic = fieldDiagnostics[fieldName]
                            if (!diagnostic) {
                                continue
                            }

                            var latestTradeDate = String(diagnostic.latestTradeDate || "")
                            var latestNonNullCount = Number(diagnostic.latestDateNonNullCount || 0)
                            var latestPositiveCount = Number(diagnostic.latestDatePositiveCount || 0)

                            if (latestNonNullCount === 0) {
                                supported = false
                                reason = latestTradeDate
                                    ? ("当前缓存在最近交易日 " + latestTradeDate + " 没有可用字段值: " + fieldName)
                                    : ("当前缓存没有可用字段值: " + fieldName)
                                break
                            }

                            if (positiveRequiredFields[fieldName] && latestPositiveCount === 0) {
                                supported = false
                                reason = latestTradeDate
                                    ? ("当前缓存在最近交易日 " + latestTradeDate + " 的 " + fieldName + " 全部为 0 或非正数")
                                    : ("当前缓存中的 " + fieldName + " 全部为 0 或非正数")
                                break
                            }
                        }
                    }
                }
            }

            supportMap[factorId] = {
                supported: supported,
                requiredFields: requiredFields,
                missingFields: missingFields,
                reason: reason
            }
        }

        return supportMap
    }

    function filterSelectedFactorsByCurrentCache() {
        if (selectedDataSourceMode !== "cache" || !selectedFactorIds || selectedFactorIds.length === 0) {
            return
        }

        var supportMap = currentCacheFactorSupportMap()
        var filteredFactorIds = []
        var removedFactorNames = []

        for (var i = 0; i < selectedFactorIds.length; i++) {
            var factorId = selectedFactorIds[i]
            var supportInfo = supportMap[String(factorId)]
            if (supportInfo && supportInfo.supported === false) {
                removedFactorNames.push(resolveFactorDisplayName(factorId))
                continue
            }

            filteredFactorIds.push(factorId)
        }

        if (removedFactorNames.length > 0) {
            console.log("当前缓存不支持以下已选因子，已从选择中移除:", removedFactorNames.join(", "))
            selectedFactorIds = filteredFactorIds
        }
    }

    function resolveFactorDisplayName(factorId) {
        if (!factorId) {
            return ""
        }

        if (factorService && factorService.getViewModel) {
            var viewModel = factorService.getViewModel()
            if (viewModel && viewModel.getFactorById) {
                var factorInfo = viewModel.getFactorById(String(factorId))
                if (factorInfo) {
                    return factorInfo.displayName || factorInfo.factorName || factorInfo.name || String(factorId)
                }
            }
        }

        return String(factorId)
    }

    function selectedFactorDisplayText() {
        if (!selectedFactorIds || selectedFactorIds.length === 0) {
            return "未选择因子"
        }

        var names = []
        for (var i = 0; i < selectedFactorIds.length; i++) {
            names.push(resolveFactorDisplayName(selectedFactorIds[i]))
        }

        if (names.length <= 3) {
            return names.join("、")
        }

        return names.slice(0, 3).join("、") + " 等 " + names.length + " 个因子"
    }

    function hasMetricValue(value) {
        return value !== undefined && value !== null
    }

    function formatMetric(value, digits) {
        return hasMetricValue(value) ? Number(value).toFixed(digits) : Number(0).toFixed(digits)
    }

    function formatPercentMetric(value, digits) {
        return hasMetricValue(value) ? (Number(value) * 100).toFixed(digits) + "%" : (Number(0) * 100).toFixed(digits) + "%"
    }

    function formatTextMetric(value, fallback) {
        return hasMetricValue(value) && String(value).length > 0 ? String(value) : fallback
    }

    function returnMetricColor(value) {
        var numericValue = hasMetricValue(value) ? Number(value) : 0
        if (numericValue > 0) {
            return "#EF4444"
        }
        if (numericValue < 0) {
            return "#10B981"
        }
        return "#94A3B8"
    }

    function returnMetricTrend(value) {
        var numericValue = hasMetricValue(value) ? Number(value) : 0
        if (numericValue > 0) {
            return "up"
        }
        if (numericValue < 0) {
            return "down"
        }
        return "neutral"
    }

    function returnTrendColor(trend) {
        return trend === "up" ? "#EF4444" : (trend === "down" ? "#10B981" : "#94A3B8")
    }

    function factorDefinitionForValidation(factorId) {
        if (!factorService || !factorService.getFactorById || !factorId) {
            return null
        }

        var factorDefinition = factorService.getFactorById(String(factorId))
        return factorDefinition ? factorDefinition : null
    }

    function normalizedFactorTypeForValidation(factorDefinition) {
        if (!factorDefinition) {
            return ""
        }

        if (factorDefinition.factorType) {
            return String(factorDefinition.factorType).trim().toLowerCase()
        }

        if (factorDefinition.config) {
            if (factorDefinition.config.factorType) {
                return String(factorDefinition.config.factorType).trim().toLowerCase()
            }
            if (factorDefinition.config.factor_type) {
                return String(factorDefinition.config.factor_type).trim().toLowerCase()
            }
        }

        return ""
    }

    function displayedBacktestResultForFactor(factorId) {
        if (!factorId || !backtestResult) {
            return null
        }

        var targetFactorId = String(factorId)
        if (backtestResult.results && Array.isArray(backtestResult.results)) {
            for (var i = 0; i < backtestResult.results.length; i++) {
                var item = backtestResult.results[i]
                if (!item) {
                    continue
                }

                var itemConfig = item.config || {}
                if (String(item.factorId || itemConfig.factorId || "") === targetFactorId) {
                    return item
                }
            }
        }

        if (backtestResult.config && String(backtestResult.config.factorId || "") === targetFactorId) {
            return backtestResult
        }

        return null
    }

    function buildValidationState(statusKey, statusText, reason, detail, accentColor) {
        return {
            statusKey: statusKey,
            statusText: statusText,
            reason: reason,
            detail: detail,
            accentColor: accentColor
        }
    }

    function factorValidationState(factorId) {
        var factorName = resolveFactorDisplayName(factorId)
        var factorDefinition = factorDefinitionForValidation(factorId)
        if (!factorDefinition) {
            return buildValidationState(
                "config-missing",
                "配置缺失",
                "未能读取因子定义",
                factorName + " 当前缺少完整配置，无法判断可执行性。",
                "#F59E0B"
            )
        }

        var factorType = normalizedFactorTypeForValidation(factorDefinition)
        var majorCategory = String(factorDefinition.majorCategory || "")
        if (factorType === "dividend" || majorCategory === "红利因子") {
            return buildValidationState(
                "implementation-missing",
                "实现未接入",
                "红利因子当前未接入实际计算",
                "当前实现层仍缺少 dividend_yield / payout_ratio 的实际计算接入。",
                "#F59E0B"
            )
        }

        if (selectedDataSourceMode === "cache") {
            if (!hasAvailableCacheDataset()) {
                return buildValidationState(
                    "waiting-cache",
                    "待选择缓存集",
                    "当前没有可用缓存集",
                    "请先生成并选择缓存集，之后再验证该因子。",
                    "#64748B"
                )
            }

            if (selectedDatasetId <= 0) {
                return buildValidationState(
                    "waiting-cache",
                    "待选择缓存集",
                    "尚未选择缓存集",
                    "请选择一个缓存集后，系统才能校验字段支持情况。",
                    "#64748B"
                )
            }

            var supportInfo = currentCacheFactorSupportMap()[String(factorId)]
            if (supportInfo && supportInfo.supported === false) {
                return buildValidationState(
                    "data-missing",
                    "数据不足",
                    supportInfo.reason || "当前缓存不支持该因子",
                    "该因子依赖字段与当前缓存集不匹配，不能进入回测执行阶段。",
                    "#F59E0B"
                )
            }
        }

        if (lastBacktestError && selectedFactorIds.length === 1 && String(selectedFactorIds[0]) === String(factorId)) {
            return buildValidationState(
                "backtest-failed",
                "回测失败",
                lastBacktestError,
                "该因子已经进入执行阶段，但最近一次回测未成功完成。",
                "#EF4444"
            )
        }

        var resultEntry = displayedBacktestResultForFactor(factorId)
        if (!resultEntry) {
            return buildValidationState(
                "ready",
                "可执行待验证",
                "已通过执行前校验",
                "该因子当前已满足配置与数据前置条件，下一步需要通过回测结果验证效果。",
                "#3B82F6"
            )
        }

        var resultSummary = resultEntry.summary || {}
        var resultIcir = resultEntry.icirResult || {}
        var dataCoverage = Number(resultSummary.dataCoverage || 0)
        var icValue = Number(resultIcir.icValue || 0)
        var irValue = Number(resultIcir.irValue || 0)
        var icPositiveRate = Number(resultIcir.icPositiveRate || 0)
        var spreadReturn = Number(resultSummary.spreadReturn || 0)
        var meetsTarget = dataCoverage >= 0.9
            && Math.abs(icValue) >= 0.02
            && irValue >= 0.3
            && icPositiveRate >= 0.5
            && spreadReturn > 0

        if (meetsTarget) {
            return buildValidationState(
                "effective",
                "有效",
                "已满足当前目标阈值",
                "覆盖率、IC、IR、IC正率和多空收益差均达到当前设定目标。",
                "#EF4444"
            )
        }

        return buildValidationState(
            "weak",
            "效果偏弱",
            "已可执行，但未完全达到目标阈值",
            "建议继续观察数据覆盖率、IC/IR、IC正率和多空收益差，判断是否需要调整或下线。",
            "#F59E0B"
        )
    }

    function clearDisplayedBacktestState() {
        root.applyDisplayedBacktestResult(null)
        root.currentGroup = 0
        root.totalGroups = 0
    }

    function applyDisplayedBacktestResult(result) {
        if (!result) {
            root.backtestResult = ({})
            root.groupResults = []
            root.icirResult = ({})
            root.summaryStats = ({})
            return
        }

        root.backtestResult = result

        if (result.results && Array.isArray(result.results)) {
            if (result.results.length > 0) {
                var firstResult = result.results[0]
                root.groupResults = firstResult && firstResult.groups && Array.isArray(firstResult.groups) ? firstResult.groups : []
                root.icirResult = firstResult && firstResult.icirResult ? firstResult.icirResult : ({})
                root.summaryStats = firstResult && firstResult.summary ? firstResult.summary : ({})
            } else {
                root.groupResults = []
                root.icirResult = ({})
                root.summaryStats = ({})
            }
            return
        }

        root.groupResults = result.groups && Array.isArray(result.groups) ? result.groups : []
        root.icirResult = result.icirResult ? result.icirResult : ({})
        root.summaryStats = result.summary ? result.summary : ({})
    }
    
    // ============ 属性 ============
    
    property Bridge.FactorService factorService: null
    property Bridge.CleanedDataController cleanedDataController: null
    
    // 因子选择相关属性 - 现在由C++控制器管理
    property var selectedFactorIds: []  // 支持多因子选择，与控制器同步
    property string selectedFactorId: ""  // 向后兼容，取第一个选中的因子

    onSelectedFactorIdsChanged: {
        selectedFactorId = selectedFactorIds && selectedFactorIds.length > 0 ? String(selectedFactorIds[0]) : ""
    }
    
    // 因子选择对话框
    property var factorSelectorDialog: null
    
    // 数据集模型 - 不再使用，由C++控制器自动处理缓存
    
    // 回测控制器 - 使用属性绑定
    Bridge.FactorBacktestController {
        id: factorBacktestController
        
        // 绑定回测状态到QML属性
        onIsRunningChanged: {
            root.isBacktesting = factorBacktestController.isRunning
        }
        onProgressChanged: {
            root.backtestProgress = factorBacktestController.progress
        }
        onStatusChanged: {
            root.backtestStatus = factorBacktestController.status
        }
        
        // 绑定回测结果到QML属性
        onGroupResultsChanged: {
            root.groupResults = factorBacktestController.groupResults
        }
        onIcirResultChanged: {
            root.icirResult = factorBacktestController.icirResult
        }
        onSummaryStatsChanged: {
            root.summaryStats = factorBacktestController.summaryStats
        }
        onBacktestResultChanged: {
            if (!root.isBacktesting) {
                root.applyDisplayedBacktestResult(factorBacktestController.backtestResult)
            }
        }
        
        onBacktestStarted: function(factorId) {
            console.log("回测开始:", factorId)
            root.clearDisplayedBacktestState()
            root.lastBacktestError = ""
            showToast("▶️ 回测开始")
        }
        onBacktestProgress: function(progress, status) {
            // 进度信息已通过属性绑定更新
        }
        onBacktestProgressDetailed: function(progress, status, currentGroupNum, totalGroupsNum) {
            root.currentGroup = currentGroupNum
            root.totalGroups = totalGroupsNum
        }
        onBacktestCompleted: function(result) {
            console.log("📊 回测完成信号收到!")
            console.log("📊 result keys:", result ? Object.keys(result) : "null")
            root.lastBacktestError = ""
            root.applyDisplayedBacktestResult(result)
            
            root.currentGroup = 0
            root.totalGroups = 0
            showToast("✅ 因子回测完成")
            
            // 打印最终状态
            console.log("📊 最终 groupResults 数量:", root.groupResults.length)
            console.log("📊 最终 icirResult:", root.icirResult)
            console.log("📊 最终 summaryStats:", root.summaryStats)

            root.analysisReportRequested(result)
        }
        onBacktestFailed: function(error) {
            console.error("回测失败:", error)
            root.currentGroup = 0
            root.totalGroups = 0
            root.backtestResult = ({})
            root.groupResults = []
            root.icirResult = ({})
            root.summaryStats = ({})
            root.lastBacktestError = error
            showToast("❌ 回测失败: " + error)
        }
        onBacktestCancelled: function() {
            console.log("回测已取消")
            root.currentGroup = 0
            root.totalGroups = 0
            root.lastBacktestError = ""
            showToast("⏸️ 回测已取消")
        }
    }
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    property int currentGroup: 0
    property int totalGroups: 0
    
    // 回测结果
    property var backtestResult: ({})
    property var groupResults: []
    property var icirResult: ({})
    property var summaryStats: ({})
    property string lastBacktestError: ""
    
    // 分组配置
    property var groupConfig: ({})
    
    // 数据源属性
    property int selectedDatasetId: -1
    property string selectedDataSourceMode: "cache"
    property var cacheDatasetOptions: [{ text: "请选择缓存集", value: -1, raw: null }]

    onSelectedDataSourceModeChanged: {
        if (selectedDataSourceMode === "cache") {
            filterSelectedFactorsByCurrentCache()
        }
    }

    function hasAvailableCacheDataset() {
        return cacheDatasetOptions && cacheDatasetOptions.length > 1
    }

    function cacheDatasetOptionText(index) {
        if (!cacheDatasetOptions || index < 0 || index >= cacheDatasetOptions.length) {
            return ""
        }

        var option = cacheDatasetOptions[index]
        return option && option.text ? option.text : ""
    }

    function rebuildCacheDatasetOptions() {
        var options = [{ text: "请选择缓存集", value: -1, raw: null }]

        if (cleanedDataController && cleanedDataController.datasetList) {
            var datasets = cleanedDataController.datasetList
            for (var i = 0; i < datasets.length; i++) {
                var dataset = datasets[i]
                if (!dataset || dataset.id === undefined) {
                    continue
                }
                if (!dataset.isBacktestReady) {
                    continue
                }

                var parts = []
                parts.push("#" + dataset.id)
                parts.push(dataset.displayName || dataset.name || "未命名缓存集")
                if (dataset.startDate && dataset.endDate) {
                    parts.push("(" + dataset.startDate + "~" + dataset.endDate + ")")
                }

                options.push({
                    text: parts.join(" "),
                    value: dataset.id,
                    raw: dataset
                })
            }
        }

        cacheDatasetOptions = options
        syncSelectedDatasetIndex()
        console.log("回测页可回测缓存集选项已刷新，数量:", Math.max(0, cacheDatasetOptions.length - 1))
    }

    function syncSelectedDatasetIndex() {
        if (!datasetComboBox) {
            return
        }

        var options = cacheDatasetOptions
        if (!options || options.length === 0) {
            return
        }

        var targetId = selectedDatasetId
        if (targetId < 0 && cleanedDataController.selectedDatasetInfo && cleanedDataController.selectedDatasetInfo.id !== undefined) {
            targetId = cleanedDataController.selectedDatasetInfo.id
        }

        for (var index = 0; index < options.length; index++) {
            if (options[index].value === targetId) {
                if (datasetComboBox.currentIndex !== index) {
                    datasetComboBox.currentIndex = index
                }
                return
            }
        }

        if (targetId > 0) {
            selectedDatasetId = -1
            factorBacktestController.selectedDatasetId = -1
        }

        if (options.length > 0 && datasetComboBox.currentIndex !== 0) {
            datasetComboBox.currentIndex = 0
        }
    }

    function selectCacheDatasetAt(index) {
        if (selectedDataSourceMode !== "cache") {
            return
        }

        if (!cleanedDataController || !cacheDatasetOptions || index < 0 || index >= cacheDatasetOptions.length) {
            return
        }

        var selected = cacheDatasetOptions[index]
        if (!selected) {
            return
        }

        if (datasetComboBox.currentIndex !== index) {
            datasetComboBox.currentIndex = index
        }

        if (selected.value === undefined || selected.value <= 0) {
            selectedDatasetId = -1
            factorBacktestController.selectedDatasetId = -1
            return
        }

        if (selectedDatasetId !== selected.value) {
            selectedDatasetId = selected.value
        }

        factorBacktestController.selectedDatasetId = selected.value
        cleanedDataController.loadDatasetById(selected.value)
        console.log("回测页选择缓存集:", selected.value, selected.text)
    }

    onVisibleChanged: {
        if (visible && cleanedDataController) {
            cleanedDataController.refreshDatasets()
            rebuildCacheDatasetOptions()
        }
    }
    
    // ============ UI ============
    
    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
        
        Flickable {
            id: scrollView
            anchors.fill: parent
            anchors.margins: 16
            clip: true
            contentWidth: contentColumn.width
            contentHeight: contentColumn.height
            boundsBehavior: Flickable.StopAtBounds
            
            // 滚动条
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 8
            }
            
            ColumnLayout {
                id: contentColumn
                width: scrollView.width
                spacing: 16
            
                // 标题区域
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    
                    Text {
                        text: "🧪 因子回测"
                        font.pixelSize: 24
                        font.weight: Font.Bold
                        color: "#F1F5F9"
                    }
                    
                    Text {
                        text: "验证因子预测能力，监控回测进度"
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }
                    
                    Item { Layout.fillWidth: true }
                }
                
                // 回测控制面板
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        
                        // 因子选择区域
                        RowLayout {
                            spacing: 12
                            
                            // 选择因子按钮
                            Rectangle {
                                Layout.preferredWidth: 140
                                Layout.preferredHeight: 40
                                radius: 8
                                color: "#3B82F6"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: "📊"
                                        font.pixelSize: 14
                                        color: "white"
                                    }
                                    
                                    Text {
                                        text: "选择因子"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: openFactorSelector()
                                }
                            }
                            
                            // 占位空间
                            Item {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                            }
                            
                            // 已选因子显示
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Repeater {
                                    model: selectedFactorIds
                                    
                                    delegate: Rectangle {
                                        height: 28
                                        radius: 14
                                        color: "#3B82F620"
                                        
                                        Row {
                                            spacing: 6
                                            anchors.centerIn: parent
                                            
                                            Text {
                                                text: root.resolveFactorDisplayName(modelData)
                                                font.pixelSize: 11
                                                color: "#3B82F6"
                                                leftPadding: 10
                                            }
                                            
                                            Text {
                                                text: "×"
                                                font.pixelSize: 12
                                                color: "#3B82F6"
                                                rightPadding: 10
                                                
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        var index = selectedFactorIds.indexOf(modelData)
                                                        if (index !== -1) {
                                                            selectedFactorIds.splice(index, 1)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                // 空状态提示
                                Text {
                                    text: selectedFactorIds.length === 0 ? "请选择要回测的因子" : ""
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                    visible: selectedFactorIds.length === 0
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: selectedFactorIds.length > 0 ? 96 : 72
                            radius: 10
                            color: "#0F172A"
                            border.width: 1
                            border.color: "#1E293B"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: "验证状态"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "#F1F5F9"
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: "流程: 可执行性校验 -> 回测效果校验"
                                        font.pixelSize: 10
                                        color: "#64748B"
                                    }
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Repeater {
                                        model: selectedFactorIds

                                        delegate: Rectangle {
                                            height: 42
                                            radius: 8
                                            color: "#111827"
                                            border.width: 1
                                            border.color: validationState.accentColor
                                            width: Math.min(320, Math.max(220, validationColumn.implicitWidth + 24))

                                            property var validationState: root.factorValidationState(modelData)

                                            Column {
                                                id: validationColumn
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.left: parent.left
                                                anchors.leftMargin: 12
                                                anchors.right: parent.right
                                                anchors.rightMargin: 12
                                                spacing: 2

                                                Row {
                                                    spacing: 8

                                                    Text {
                                                        text: root.resolveFactorDisplayName(modelData)
                                                        font.pixelSize: 11
                                                        font.weight: Font.Medium
                                                        color: "#F1F5F9"
                                                    }

                                                    Text {
                                                        text: validationState.statusText
                                                        font.pixelSize: 10
                                                        color: validationState.accentColor
                                                    }
                                                }

                                                Text {
                                                    text: validationState.reason
                                                    font.pixelSize: 10
                                                    color: "#94A3B8"
                                                    elide: Text.ElideRight
                                                    width: parent.width
                                                }
                                            }
                                        }
                                    }

                                    Text {
                                        text: "请选择因子后查看统一验证状态"
                                        font.pixelSize: 11
                                        color: "#64748B"
                                        visible: selectedFactorIds.length === 0
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "目标阈值: 数据覆盖率 >= 90%, |IC| >= 0.02, IR >= 0.30, IC正率 >= 50%, 多空收益差 > 0"
                                    font.pixelSize: 10
                                    color: "#64748B"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        
                        // 回测配置
                        RowLayout {
                            spacing: 16
                            
                            // 分组数量
                            ColumnLayout {
                                spacing: 4
                                
                                Text {
                                    text: "分组数量"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                ComboBox {
                                    id: groupComboBox
                                    Layout.preferredWidth: 80
                                    model: ["5组", "10组", "20组"]
                                    currentIndex: 1
                                    
                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }
                                    
                                    contentItem: Text {
                                        text: parent.displayText
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        horizontalAlignment: Text.AlignLeft
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                            
                            // 数据源选择
                            ColumnLayout {
                                spacing: 4
                                
                                Text {
                                    text: "数据源"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                ComboBox {
                                    id: dataSourceComboBox
                                    Layout.preferredWidth: 140
                                    model: [
                                        { text: "缓存集", value: "cache" },
                                        { text: "数据库", value: "database" }
                                    ]
                                    textRole: "text"

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    contentItem: Text {
                                        text: dataSourceComboBox.displayText
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    onCurrentIndexChanged: {
                                        if (currentIndex < 0 || currentIndex >= model.length) {
                                            return
                                        }

                                        selectedDataSourceMode = model[currentIndex].value
                                        factorBacktestController.dataSourceMode = selectedDataSourceMode
                                    }
                                }

                                Text {
                                    text: selectedDataSourceMode === "cache"
                                          ? "使用用户选择的缓存集范围和股票池"
                                          : "直接从数据库读取全量回测数据"
                                    font.pixelSize: 10
                                    color: "#64748B"
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "缓存集"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }

                                ComboBox {
                                    id: datasetComboBox
                                    Layout.preferredWidth: 320
                                    model: cacheDatasetOptions
                                    textRole: "text"
                                    enabled: selectedDataSourceMode === "cache"
                                    opacity: enabled ? 1.0 : 0.45

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: enabled ? "#334155" : "#1E293B"
                                    }

                                    contentItem: Text {
                                        text: root.cacheDatasetOptionText(datasetComboBox.currentIndex)
                                              || root.cacheDatasetOptionText(0)
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        leftPadding: 8
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }

                                    indicator: Item {
                                        x: datasetComboBox.width - width - 12
                                        y: datasetComboBox.topPadding + (datasetComboBox.availableHeight - height) / 2
                                        width: 16
                                        height: 16

                                        Text {
                                            anchors.centerIn: parent
                                            text: datasetComboBox.popup.visible ? "▲" : "▼"
                                            font.pixelSize: 10
                                            color: "#94A3B8"
                                        }
                                    }

                                    popup: Popup {
                                        y: datasetComboBox.height + 4
                                        width: datasetComboBox.width
                                        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
                                        padding: 4

                                        background: Rectangle {
                                            radius: 8
                                            color: "#1E293B"
                                            border.width: 1
                                            border.color: "#334155"
                                        }

                                        contentItem: ListView {
                                            clip: true
                                            implicitHeight: contentHeight
                                            model: datasetComboBox.popup.visible ? datasetComboBox.delegateModel : null
                                            currentIndex: datasetComboBox.highlightedIndex

                                            ScrollIndicator.vertical: ScrollIndicator {}
                                        }
                                    }

                                    delegate: ItemDelegate {
                                        width: datasetComboBox.width - 8
                                        height: 40
                                        highlighted: datasetComboBox.highlightedIndex === index

                                        background: Rectangle {
                                            radius: 6
                                            color: parent.highlighted ? "#334155"
                                                   : parent.hovered ? "#2D3748" : "transparent"
                                        }

                                        contentItem: Text {
                                            text: root.cacheDatasetOptionText(index)
                                            font.pixelSize: 12
                                            color: "#F1F5F9"
                                            leftPadding: 8
                                            rightPadding: 8
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }

                                        onClicked: {
                                            root.selectCacheDatasetAt(index)
                                            datasetComboBox.popup.close()
                                        }
                                    }

                                    onActivated: function(index) {
                                        root.selectCacheDatasetAt(index)
                                    }
                                }

                                Text {
                                    text: selectedDatasetId > 0
                                          ? "当前缓存集 key: " + selectedDatasetId
                                          : (cacheDatasetOptions.length > 1 ? "请先选择一个缓存集" : "当前没有可用缓存集")
                                    font.pixelSize: 10
                                    color: selectedDatasetId > 0 ? "#93C5FD" : "#64748B"
                                }

                                Text {
                                    text: {
                                        for (var i = 0; i < cacheDatasetOptions.length; i++) {
                                            if (cacheDatasetOptions[i].value === selectedDatasetId && cacheDatasetOptions[i].raw) {
                                                return "当前缓存集 value: " + (cacheDatasetOptions[i].raw.displayName || cacheDatasetOptions[i].raw.name || "")
                                            }
                                        }
                                        return ""
                                    }
                                    visible: text.length > 0
                                    font.pixelSize: 10
                                    color: "#64748B"
                                }
                            }
                            
                            
                            Item { Layout.fillWidth: true }
                            
                            // 回测按钮
                            Rectangle {
                                id: backtestButton
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 40
                                radius: 8
                                color: isBacktesting ? "#334155" : (selectedFactorIds.length > 0 ? "#3B82F6" : "#475569")
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: isBacktesting ? "⏸️" : "▶️"
                                        font.pixelSize: 14
                                        color: isBacktesting ? "#94A3B8" : (selectedFactorIds.length > 0 ? "white" : "#94A3B8")
                                    }
                                    
                                    Text {
                                        text: isBacktesting ? "回测中..." : "开始回测"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: isBacktesting ? "#94A3B8" : (selectedFactorIds.length > 0 ? "white" : "#94A3B8")
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: !isBacktesting && selectedFactorIds.length > 0
                                    onClicked: startBacktest()
                                }
                            }
                            
                            // 取消按钮
                            Rectangle {
                                Layout.preferredWidth: 80
                                Layout.preferredHeight: 40
                                radius: 8
                                color: "#334155"
                                visible: isBacktesting
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: "✕"
                                        font.pixelSize: 14
                                        color: "#EF4444"
                                    }
                                    
                                    Text {
                                        text: "取消"
                                        font.pixelSize: 14
                                        color: "#EF4444"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: factorBacktestController.cancelBacktest()
                                }
                            }
                        }
                        
                        // 进度区域
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            visible: isBacktesting
                            
                            // 进度条
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 8
                                radius: 4
                                color: "#334155"
                                
                                Rectangle {
                                    width: parent.width * (backtestProgress / 100)
                                    height: parent.height
                                    radius: 4
                                    color: "#3B82F6"
                                }
                            }
                            
                            // 进度信息
                            RowLayout {
                                Layout.fillWidth: true
                                
                                Text {
                                    text: backtestStatus
                                    font.pixelSize: 12
                                    color: "#F59E0B"
                                }
                                
                                Text {
                                    text: backtestProgress + "%"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                Item { Layout.fillWidth: true }
                                
                                Text {
                                    text: currentGroup > 0 ? "分组: " + currentGroup + "/" + totalGroups : ""
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                    visible: currentGroup > 0
                                }
                            }
                        }
                    }
                }
            
                // 主要内容区域
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 500  // 使用固定高度让Flickable可以滚动
                    spacing: 16
                    
                    // 分组内容展示
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 12
                        color: "#1E293B"
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12
                            
                            // 标题
                            RowLayout {
                                Layout.fillWidth: true
                                
                                Text {
                                    text: "📊 分组内容"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }
                                
                                Item { Layout.fillWidth: true }
                                
                                Text {
                                    text: groupResults.length > 0 ? "共 " + groupResults.length + " 个分组" : "等待回测结果"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                            }
                            
                            // 分组列表
                            ListView {
                                id: groupListView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: groupResults
                                clip: true
                                spacing: 8
                                
                                delegate: Rectangle {
                                    width: groupListView.width
                                    height: 60
                                    radius: 8
                                    color: "#1E293B"
                                    
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 12
                                        
                                        // 分组编号
                                        Rectangle {
                                            Layout.preferredWidth: 32
                                            Layout.preferredHeight: 32
                                            radius: 16
                                            color: "#0F172A"
                                            
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.groupId || (index + 1)
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                                color: "#F1F5F9"
                                            }
                                        }
                                        
                                        // 分组信息
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            
                                            Text {
                                                text: modelData.groupName || ("第 " + (index + 1) + " 组")
                                                font.pixelSize: 14
                                                font.weight: Font.Medium
                                                color: "#F1F5F9"
                                            }
                                            
                                            RowLayout {
                                                spacing: 16
                                                
                                                Text {
                                                    text: "股票: " + (modelData.stockCount || 0)
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                }
                                                
                                                Text {
                                                    text: "因子值: " + (modelData.minFactorValue || 0).toFixed(2) + " - " + (modelData.maxFactorValue || 0).toFixed(2)
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                }
                                            }
                                        }
                                        
                                        // 收益信息
                                        ColumnLayout {
                                            Layout.alignment: Qt.AlignRight
                                            spacing: 2
                                            
                                            Text {
                                                text: (((modelData.return || 0) * 100)).toFixed(2) + "%"
                                                font.pixelSize: 16
                                                font.weight: Font.Bold
                                                color: root.returnMetricColor(modelData.return || 0)
                                            }
                                            
                                            Text {
                                                text: "收益"
                                                font.pixelSize: 10
                                                color: "#94A3B8"
                                            }
                                        }
                                    }
                                    
                                    // 当前分组高亮
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8
                                        color: "#3B82F620"
                                        border.width: 2
                                        border.color: "#3B82F6"
                                        visible: isBacktesting && currentGroup === (index + 1)
                                    }
                                }
                                
                                // 空状态
                                Text {
                                    anchors.centerIn: parent
                                    text: isBacktesting ? "正在计算分组..." : "请开始回测查看分组内容"
                                    font.pixelSize: 14
                                    color: "#94A3B8"
                                    visible: groupResults.length === 0
                                }
                            }
                        }
                    }
                }
            } // 这里应该是ColumnLayout的结束
        } // 这里应该是Flickable的结束，这是修复的关键位置
    } // 这是最外层Rectangle的结束
    
    // ============ 组件定义 ============
    
    // 关键指标卡片组件
    component KeyMetricCard: Item {
        property string title: ""
        property string value: ""
        property string description: ""
        property string color: "#F1F5F9"
        property string trend: "neutral"
        property string trendColor: trend === "up" ? "#10B981" : (trend === "down" ? "#EF4444" : "#94A3B8")
        
        Layout.fillWidth: true
        Layout.preferredHeight: 70
        
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#0F172A"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                
                Text {
                    text: title
                    font.pixelSize: 10
                    color: "#94A3B8"
                }
                
                Row {
                    spacing: 4
                    
                    Text {
                        text: value
                        font.pixelSize: 16
                        font.weight: Font.Bold
                        color: parent.parent.parent.color
                    }
                    
                    // 趋势指示器
                    Text {
                        visible: trend !== "neutral"
                        text: trend === "up" ? "↑" : "↓"
                        font.pixelSize: 12
                        color: trendColor
                    }
                }
                
                Text {
                    text: description
                    font.pixelSize: 9
                    color: "#64748B"
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    // 所有复杂逻辑已移至C++控制器，QML只负责UI显示和信号处理
    
    // 开始回测 - 简化版本，只调用C++控制器
    function startBacktest() {
        console.log("开始回测，因子数量:", selectedFactorIds.length)
        
        if (selectedFactorIds.length === 0) {
            console.log("请先选择要回测的因子")
            return
        }

        if (selectedDataSourceMode === "cache") {
            if (!hasAvailableCacheDataset()) {
                console.log("当前没有可用缓存集，请先生成并选择缓存集，或手动切换到数据库模式")
                return
            }

            if (selectedDatasetId <= 0) {
                console.log("请先选择一个缓存集后再开始回测")
                return
            }

            var supportMap = currentCacheFactorSupportMap()
            var unsupportedFactors = []
            for (var factorIndex = 0; factorIndex < selectedFactorIds.length; factorIndex++) {
                var selectedId = selectedFactorIds[factorIndex]
                var factorSupportInfo = supportMap[String(selectedId)]
                if (factorSupportInfo && factorSupportInfo.supported === false) {
                    unsupportedFactors.push(resolveFactorDisplayName(selectedId) + " (" + factorSupportInfo.reason + ")")
                }
            }

            if (unsupportedFactors.length > 0) {
                console.log("当前缓存不支持以下因子，请重新选择:", unsupportedFactors.join("; "))
                return
            }
        }
        
        // 首先将选择的因子ID传递给控制器
        if (factorBacktestController) {
            var selectedStartDate = ""
            var selectedEndDate = ""

            if (cleanedDataController) {
                if (cleanedDataController.currentStartDate && cleanedDataController.currentEndDate) {
                    selectedStartDate = cleanedDataController.currentStartDate
                    selectedEndDate = cleanedDataController.currentEndDate
                } else {
                    var dateRange = cleanedDataController.getDataDateRange()
                    if (dateRange && dateRange.startDate && dateRange.endDate) {
                        selectedStartDate = dateRange.startDate
                        selectedEndDate = dateRange.endDate
                    }
                }

                console.log("回测使用的数据集:", JSON.stringify(cleanedDataController.selectedDatasetInfo))
            }

            console.log("回测日期范围:", selectedStartDate, "至", selectedEndDate)

            // 将JavaScript数组转换为QVariantList
            var factorIdList = []
            for (var i = 0; i < selectedFactorIds.length; i++) {
                factorIdList.push(selectedFactorIds[i])
            }
            
            // 直接设置控制器的selectedFactorIds属性（而不是调用方法）
            factorBacktestController.selectedFactorIds = factorIdList
            factorBacktestController.selectedDatasetId = selectedDatasetId
            factorBacktestController.dataSourceMode = selectedDataSourceMode
            
            // 调用C++控制器开始回测，传递当前选中数据集对应的日期范围
            factorBacktestController.startBacktest(groupComboBox.currentText, selectedStartDate, selectedEndDate)
        }
    }
    
    // 打开因子选择对话框 - 简化版本
    function openFactorSelector() {
        console.log("打开因子选择对话框")
        
        // 创建对话框组件
        var component = Qt.createComponent("FactorSelectorDialog.qml")
        if (component.status === Component.Ready) {
            factorSelectorDialog = component.createObject(root, {
                factorService: factorService,
                factorViewModel: factorService ? factorService.getViewModel() : null,
                selectedFactorIds: selectedFactorIds.slice(),
                dataSourceMode: selectedDataSourceMode,
                selectedDatasetId: selectedDatasetId,
                cacheAvailableFields: currentCacheAvailableFields(),
                factorSupportMap: currentCacheFactorSupportMap()
            })
            
            // 连接信号
            factorSelectorDialog.factorsSelected.connect(handleFactorsSelected)
            factorSelectorDialog.dialogClosed.connect(handleDialogClosed)
            
            factorSelectorDialog.open()
        } else {
            console.error("无法创建因子选择对话框组件:", component.errorString())
        }
    }
    
    // 处理因子选择结果 - 简化版本
    function handleFactorsSelected(factorIds) {
        console.log("因子选择结果:", factorIds)
        selectedFactorIds = factorIds
    }
    
    // 处理对话框关闭 - 简化版本
    function handleDialogClosed() {
        console.log("因子选择对话框已关闭")
        if (factorSelectorDialog) {
            factorSelectorDialog.destroy()
            factorSelectorDialog = null
        }
    }
    
    // 移除已选择的因子 - 简化版本
    function removeSelectedFactor(factorId) {
        var index = selectedFactorIds.indexOf(factorId)
        if (index !== -1) {
            selectedFactorIds.splice(index, 1)
        }
    }
    
    // ============ 数据日期范围获取 ============
    // 日期范围由用户通过UI选择（如"最近1年"、"最近3年"等），不再自动获取
    
    // ============ 数据源相关函数 ============
    // 数据源处理已移至C++控制器，QML不再处理缓存选择逻辑
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("因子回测页面初始化完成")
        console.log("因子服务:", factorService)
        console.log("当前选择因子:", selectedFactorId)
        console.log("当前选择因子列表:", selectedFactorIds)
        factorBacktestController.dataSourceMode = selectedDataSourceMode

        if (cleanedDataController) {
            if (!cleanedDataController.isAvailable) {
                cleanedDataController.initialize()
            }
            cleanedDataController.refreshDatasets()
        }

        rebuildCacheDatasetOptions()
        if (factorBacktestController.backtestResult) {
            root.applyDisplayedBacktestResult(factorBacktestController.backtestResult)
        }
        
        // 数据源和日期范围处理已移至C++控制器，QML只负责UI显示
        console.log("因子回测页面初始化完成，等待用户操作")
    }

    Connections {
        target: cleanedDataController

        function onDatasetListChanged() {
            rebuildCacheDatasetOptions()
            syncSelectedDatasetIndex()
            filterSelectedFactorsByCurrentCache()
        }

        function onSelectedDatasetChanged() {
            if (cleanedDataController && cleanedDataController.selectedDatasetInfo && cleanedDataController.selectedDatasetInfo.id !== undefined) {
                if (cleanedDataController.selectedDatasetInfo.isBacktestReady) {
                    selectedDatasetId = cleanedDataController.selectedDatasetInfo.id
                    factorBacktestController.selectedDatasetId = selectedDatasetId
                }
            }
            rebuildCacheDatasetOptions()
            syncSelectedDatasetIndex()
            filterSelectedFactorsByCurrentCache()
        }

        function onSelectedDatasetDiagnosticsChanged() {
            rebuildCacheDatasetOptions()
            syncSelectedDatasetIndex()
            filterSelectedFactorsByCurrentCache()
        }
    }
}

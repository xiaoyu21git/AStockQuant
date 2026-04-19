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
    property var previousBacktestReport: ({})
    property int factorDefinitionRevision: 0

    function normalizePreflightFailures(value) {
        var normalized = []
        if (value === undefined || value === null) {
            return normalized
        }

        var values = Array.isArray(value) ? value : []
        for (var i = 0; i < values.length; i++) {
            var item = values[i]
            if (!item) {
                continue
            }

            normalized.push({
                factorId: item.factorId !== undefined && item.factorId !== null ? String(item.factorId) : "",
                instanceId: item.instanceId !== undefined && item.instanceId !== null ? String(item.instanceId) : "",
                reason: item.reason !== undefined && item.reason !== null ? String(item.reason) : "",
                category: item.category !== undefined && item.category !== null ? String(item.category) : ""
            })
        }

        return normalized
    }

    function normalizePreflightCategory(category) {
        return category !== undefined && category !== null ? String(category).trim().toLowerCase() : ""
    }

    function preflightCategoryMeta(category) {
        var normalizedCategory = normalizePreflightCategory(category)
        var meta = {
            key: normalizedCategory || "precheck-failed",
            statusText: "预检失败",
            shortText: "预检失败",
            detail: "当前未通过统一支持校验，暂时不能进入回测执行阶段。",
            accentColor: "#F59E0B",
            chipBackground: "#3F2D16",
            chipBorder: "#D97706",
            chipText: "#FDE68A"
        }

        switch (normalizedCategory) {
        case "runtime-init-failed":
            meta.statusText = "运行时初始化失败"
            meta.shortText = "运行时异常"
            meta.detail = "回测运行时没有初始化成功，本次无法判断因子支持性。"
            meta.accentColor = "#F87171"
            meta.chipBackground = "#3F1D24"
            meta.chipBorder = "#DC2626"
            meta.chipText = "#FECACA"
            break
        case "instance-missing":
            meta.statusText = "实例未解析"
            meta.shortText = "实例缺失"
            meta.detail = "没有找到可执行实例，请先检查 factor_instance 同步状态和实例绑定。"
            meta.accentColor = "#F87171"
            meta.chipBackground = "#3F1D24"
            meta.chipBorder = "#DC2626"
            meta.chipText = "#FECACA"
            break
        case "instance-create-failed":
            meta.statusText = "实例创建失败"
            meta.shortText = "实例异常"
            meta.detail = "实例创建阶段失败，通常是实例配置、注册信息或参数不完整。"
            meta.accentColor = "#F87171"
            meta.chipBackground = "#3F1D24"
            meta.chipBorder = "#DC2626"
            meta.chipText = "#FECACA"
            break
        case "unsupported-type":
            meta.statusText = "因子类型未接入"
            meta.shortText = "类型未接入"
            meta.detail = "当前运行时还没有接入该因子类型的回测执行链路。"
            meta.accentColor = "#FB923C"
            meta.chipBackground = "#3F2A17"
            meta.chipBorder = "#EA580C"
            meta.chipText = "#FED7AA"
            break
        case "unsupported-metric":
            meta.statusText = "指标未接入"
            meta.shortText = "指标未接入"
            meta.detail = "该因子当前选择的指标没有对应的回测实现。"
            meta.accentColor = "#FB923C"
            meta.chipBackground = "#3F2A17"
            meta.chipBorder = "#EA580C"
            meta.chipText = "#FED7AA"
            break
        case "dataset-missing":
            meta.statusText = "未选择缓存集"
            meta.shortText = "未选缓存集"
            meta.detail = "当前是缓存模式，但还没有选中可回测缓存集。"
            meta.accentColor = "#94A3B8"
            meta.chipBackground = "#1E293B"
            meta.chipBorder = "#475569"
            meta.chipText = "#CBD5E1"
            break
        case "dataset-invalid":
            meta.statusText = "缓存集无效"
            meta.shortText = "缓存集无效"
            meta.detail = "选中的缓存集缺少必要元数据，或者时间范围与内容不完整。"
            meta.accentColor = "#F59E0B"
            meta.chipBackground = "#3F2D16"
            meta.chipBorder = "#D97706"
            meta.chipText = "#FDE68A"
            break
        case "dataset-empty":
            meta.statusText = "缓存集为空"
            meta.shortText = "缓存为空"
            meta.detail = "选中的缓存集没有可用于回测的股票或交易日样本。"
            meta.accentColor = "#F59E0B"
            meta.chipBackground = "#3F2D16"
            meta.chipBorder = "#D97706"
            meta.chipText = "#FDE68A"
            break
        case "stock-pool-mismatch":
            meta.statusText = "股票池不匹配"
            meta.shortText = "股票池不匹配"
            meta.detail = "缓存集股票池与当前回测股票池没有有效重合，无法计算该因子。"
            meta.accentColor = "#FB7185"
            meta.chipBackground = "#3F1D24"
            meta.chipBorder = "#E11D48"
            meta.chipText = "#FECDD3"
            break
        case "dataset-fields-missing":
        case "missing-field":
            meta.statusText = "缓存字段缺失"
            meta.shortText = "字段缺失"
            meta.detail = "缓存集中没有提供该因子计算所需的基础字段。"
            meta.accentColor = "#F59E0B"
            meta.chipBackground = "#3F2D16"
            meta.chipBorder = "#D97706"
            meta.chipText = "#FDE68A"
            break
        case "missing-field-value":
            meta.statusText = "字段值为空"
            meta.shortText = "字段值为空"
            meta.detail = "字段本身存在，但最近交易日没有可用的非空值。"
            meta.accentColor = "#FBBF24"
            meta.chipBackground = "#3F3518"
            meta.chipBorder = "#CA8A04"
            meta.chipText = "#FEF08A"
            break
        case "invalid-field-value":
            meta.statusText = "字段值无效"
            meta.shortText = "字段值无效"
            meta.detail = "字段存在，但最近交易日的值全部为 0 或非正数，无法参与计算。"
            meta.accentColor = "#FBBF24"
            meta.chipBackground = "#3F3518"
            meta.chipBorder = "#CA8A04"
            meta.chipText = "#FEF08A"
            break
        case "insufficient-history":
            meta.statusText = "历史样本不足"
            meta.shortText = "样本不足"
            meta.detail = "结合预热窗口后，可用交易日仍不足以稳定计算该因子。"
            meta.accentColor = "#FACC15"
            meta.chipBackground = "#3F3518"
            meta.chipBorder = "#CA8A04"
            meta.chipText = "#FEF08A"
            break
        case "data-unavailable":
            meta.statusText = "底层数据不可用"
            meta.shortText = "底层数据不可用"
            meta.detail = "底层数据库中该因子所需数据不可用，或者数据校验没有通过。"
            meta.accentColor = "#F59E0B"
            meta.chipBackground = "#3F2D16"
            meta.chipBorder = "#D97706"
            meta.chipText = "#FDE68A"
            break
        case "supported":
            meta.statusText = "可执行"
            meta.shortText = "可执行"
            meta.detail = "当前已经通过统一支持校验，可以进入回测执行阶段。"
            meta.accentColor = "#22C55E"
            meta.chipBackground = "#133226"
            meta.chipBorder = "#16A34A"
            meta.chipText = "#BBF7D0"
            break
        default:
            break
        }

        return meta
    }

    function preflightFailureDetailText(failure) {
        var meta = preflightCategoryMeta(failure && failure.category)
        var factorName = resolveFactorDisplayName(failure && failure.factorId ? failure.factorId : "")
        if (!factorName) {
            factorName = "该因子"
        }
        return factorName + "：" + meta.detail
    }

    function preflightFailureForFactor(factorId) {
        var normalizedFactorId = factorId !== undefined && factorId !== null ? String(factorId) : ""
        for (var i = 0; i < lastPreflightFailures.length; i++) {
            var failure = lastPreflightFailures[i]
            if (failure && String(failure.factorId) === normalizedFactorId) {
                return failure
            }
        }
        return null
    }

    function formatPreflightFailureSummary(failure) {
        if (!failure) {
            return "未知预检失败"
        }

        var factorName = resolveFactorDisplayName(failure.factorId || "")
        var meta = preflightCategoryMeta(failure.category)
        var reason = failure.reason ? String(failure.reason) : "未知预检失败"
        if (failure.instanceId) {
            return factorName + " · " + meta.shortText + " · instanceId=" + failure.instanceId + " · " + reason
        }
        return factorName + " · " + meta.shortText + " · " + reason
    }

    function buildPreflightFailureExportText(failures) {
        var lines = []
        lines.push("AStockQuantEngine 因子组合回测预检失败诊断")
        lines.push("数据源模式: " + selectedDataSourceMode)

        var datasetInfo = currentCacheDatasetInfo()
        if (datasetInfo && datasetInfo.id !== undefined) {
            lines.push("缓存集: #" + datasetInfo.id + " " + (datasetInfo.displayName || datasetInfo.name || "未命名缓存集"))
        } else if (selectedDataSourceMode === "cache") {
            lines.push("缓存集: 未选择")
        }

        lines.push("选中因子数: " + selectedFactorIds.length)
        lines.push("失败因子数: " + failures.length)
        lines.push("")

        for (var i = 0; i < failures.length; i++) {
            var failure = failures[i]
            if (!failure) {
                continue
            }

            var meta = preflightCategoryMeta(failure.category)

            lines.push("- factorId: " + (failure.factorId || ""))
            lines.push("  factorName: " + resolveFactorDisplayName(failure.factorId || ""))
            lines.push("  instanceId: " + (failure.instanceId || "未解析"))
            lines.push("  category: " + meta.statusText + " (" + (failure.category || "unknown") + ")")
            lines.push("  reason: " + (failure.reason || "未知预检失败"))
            lines.push("  detail: " + preflightFailureDetailText(failure))
            lines.push("")
        }

        return lines.join("\n").trim()
    }

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

    function normalizeStockPoolSymbols(value) {
        var normalized = []
        var seen = ({})
        var values = []

        if (value === undefined || value === null) {
            return normalized
        }

        if (Array.isArray(value)) {
            values = value
        } else if (typeof value === "string") {
            values = String(value).split(/[,;\s，；]+/)
        } else if (value.length !== undefined) {
            for (var index = 0; index < value.length; index++) {
                values.push(value[index])
            }
        } else {
            values = [value]
        }

        for (var i = 0; i < values.length; i++) {
            var symbol = String(values[i] || "").trim().toUpperCase()
            if (!symbol || seen[symbol]) {
                continue
            }

            seen[symbol] = true
            normalized.push(symbol)
        }

        return normalized
    }

    function currentCacheDatasetStockCodes() {
        var datasetInfo = currentCacheDatasetInfo()
        if (!datasetInfo || !datasetInfo.stockCodes) {
            return []
        }

        return normalizeStockPoolSymbols(datasetInfo.stockCodes)
    }

    function intersectStockPoolSymbols(primarySymbols, compareSymbols) {
        var compareSet = ({})
        var intersection = []
        for (var index = 0; index < compareSymbols.length; index++) {
            compareSet[String(compareSymbols[index] || "")] = true
        }

        for (var primaryIndex = 0; primaryIndex < primarySymbols.length; primaryIndex++) {
            var symbol = String(primarySymbols[primaryIndex] || "")
            if (symbol && compareSet[symbol] && intersection.indexOf(symbol) === -1) {
                intersection.push(symbol)
            }
        }

        return intersection
    }

    function subtractStockPoolSymbols(primarySymbols, compareSymbols) {
        var compareSet = ({})
        var difference = []
        for (var index = 0; index < compareSymbols.length; index++) {
            compareSet[String(compareSymbols[index] || "")] = true
        }

        for (var primaryIndex = 0; primaryIndex < primarySymbols.length; primaryIndex++) {
            var symbol = String(primarySymbols[primaryIndex] || "")
            if (symbol && !compareSet[symbol] && difference.indexOf(symbol) === -1) {
                difference.push(symbol)
            }
        }

        return difference
    }

    function resolveFactorBacktestStockPoolComparison() {
        var previousSymbols = normalizeStockPoolSymbols(
            previousBacktestReport && previousBacktestReport.config
                ? (previousBacktestReport.config.symbol_pool
                    || previousBacktestReport.config.symbolPool
                    || previousBacktestReport.config.selectedSymbols
                    || [])
                : [])
        var currentSymbols = currentCacheDatasetStockCodes()
        var intersectionSymbols = intersectStockPoolSymbols(previousSymbols, currentSymbols)

        return {
            previousSymbols: previousSymbols,
            currentSymbols: currentSymbols,
            intersectionSymbols: intersectionSymbols,
            previousOnlySymbols: subtractStockPoolSymbols(previousSymbols, currentSymbols),
            currentOnlySymbols: subtractStockPoolSymbols(currentSymbols, previousSymbols)
        }
    }

    function buildFactorStockPoolComparisonText() {
        var comparison = resolveFactorBacktestStockPoolComparison()
        if (selectedFactorIds.length === 0) {
            return "先选择因子后再进入二次回测比较。"
        }

        if (selectedFactorIds.length > 1) {
            return "当前是多因子组合回测。回测完成后，系统会针对每个因子分别与它自己的上一轮基线比较，再决定自动覆盖还是提示确认。"
        }

        if (comparison.previousSymbols.length === 0) {
            return "当前没有上一轮同因子回测基线，本次完成后会直接建立新的股票池基线。"
        }

        return "上一轮股票池 " + comparison.previousSymbols.length
            + " 只，本次候选股票池 " + comparison.currentSymbols.length
            + " 只，交集 " + comparison.intersectionSymbols.length
            + " 只，上轮独有 " + comparison.previousOnlySymbols.length
            + " 只，本轮新增 " + comparison.currentOnlySymbols.length + " 只。"
    }

    function allFactorIdsForSupportCheck() {
        var factorIds = []
        if (factorService && factorService.getAllFactors) {
            var factors = factorService.getAllFactors()
            for (var index = 0; index < factors.length; index++) {
                var factor = factors[index]
                if (factor && factor.factorId !== undefined && factor.factorId !== null) {
                    factorIds.push(String(factor.factorId))
                }
            }
            return normalizeSelectedFactorIds(factorIds)
        }

        var factorViewModel = factorService && factorService.getViewModel ? factorService.getViewModel() : null
        if (!factorViewModel) {
            return factorIds
        }

        for (var rowIndex = 0; rowIndex < factorViewModel.rowCount(); rowIndex++) {
            var factorId = factorViewModel.data(factorViewModel.index(rowIndex, 0), 257)
            if (factorId !== undefined && factorId !== null) {
                factorIds.push(String(factorId))
            }
        }

        return normalizeSelectedFactorIds(factorIds)
    }

    function refreshFactorSupportMap() {
        supportMapRefreshTimer.restart()
    }

    function runSupportMapRefresh() {
        if (!factorBacktestController) {
            factorSupportMapCache = ({})
            supportMapRequestInFlight = false
            return
        }

        factorBacktestController.selectedDatasetId = selectedDatasetId
        factorBacktestController.dataSourceMode = selectedDataSourceMode

        var factorIds = allFactorIdsForSupportCheck()
        if (!factorIds || factorIds.length === 0) {
            factorSupportMapCache = ({})
            supportMapRequestInFlight = false
            if (pendingFilterAfterSupportMap) {
                pendingFilterAfterSupportMap = false
                filterSelectedFactorsByCurrentCache()
            }
            return
        }

        if (factorBacktestController.requestFactorSupportMapAsync) {
            supportMapRequestInFlight = true
            supportMapRequestSeq = supportMapRequestSeq + 1
            factorBacktestController.requestFactorSupportMapAsync(factorIds, "", "", supportMapRequestSeq)
            return
        }

        // 兼容旧控制器接口
        if (factorBacktestController.buildFactorSupportMap) {
            factorSupportMapCache = factorBacktestController.buildFactorSupportMap(factorIds)
        } else {
            factorSupportMapCache = ({})
        }
        supportMapRequestInFlight = false
        if (pendingFilterAfterSupportMap) {
            pendingFilterAfterSupportMap = false
            filterSelectedFactorsByCurrentCache()
        }
    }

    function currentCacheFactorSupportMap() {
        return factorSupportMapCache || ({})
    }

    function filterSelectedFactorsByCurrentCache() {
        if (selectedDataSourceMode !== "cache" || !selectedFactorIds || selectedFactorIds.length === 0) {
            return
        }

        if (supportMapRequestInFlight) {
            pendingFilterAfterSupportMap = true
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
        var revision = factorDefinitionRevision
        if (!factorId) {
            return ""
        }

        if (factorService && factorService.getFactorById) {
            var factorInfo = factorService.getFactorById(String(factorId))
            if (factorInfo) {
                return factorInfo.displayName || factorInfo.factorName || factorInfo.name || String(factorId)
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

    function activeRunFactorDisplayText() {
        if (!activeRunFactorIds || activeRunFactorIds.length === 0) {
            return ""
        }

        var names = []
        for (var i = 0; i < activeRunFactorIds.length; i++) {
            names.push(resolveFactorDisplayName(activeRunFactorIds[i]))
        }

        if (names.length <= 3) {
            return names.join("、")
        }

        return names.slice(0, 3).join("、") + " 等 " + names.length + " 个因子"
    }

    function normalizeSelectedFactorIds(factorIds) {
        var normalized = []
        var seen = {}

        if (!factorIds) {
            return normalized
        }

        for (var i = 0; i < factorIds.length; i++) {
            var factorId = factorIds[i]
            if (factorId === undefined || factorId === null) {
                continue
            }

            var normalizedFactorId = String(factorId).trim()
            if (!normalizedFactorId || seen[normalizedFactorId]) {
                continue
            }

            seen[normalizedFactorId] = true
            normalized.push(normalizedFactorId)
        }

        return normalized
    }

    function setSelectedFactors(factorIds) {
        selectedFactorIds = normalizeSelectedFactorIds(factorIds)
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

    function normalizedWinRate(value) {
        if (!hasMetricValue(value)) {
            return 0
        }

        var numeric = Number(value)
        if (!isFinite(numeric)) {
            return 0
        }

        // 兼容历史百分比口径（0~100）
        if (Math.abs(numeric) > 1) {
            numeric = numeric / 100
        }

        if (numeric < 0) {
            return 0
        }
        if (numeric > 1) {
            return 1
        }
        return numeric
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
        var revision = factorDefinitionRevision
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

        var supportInfo = currentCacheFactorSupportMap()[String(factorId)]
        if (supportInfo && supportInfo.supported === false) {
            var supportMeta = preflightCategoryMeta(supportInfo.category)
            return buildValidationState(
                supportMeta.key,
                supportMeta.statusText,
                supportInfo.reason || "当前不支持该因子回测",
                factorName + " 当前处于“" + supportMeta.statusText + "”状态。" + supportMeta.detail,
                supportMeta.accentColor
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
        }

        var preflightFailure = preflightFailureForFactor(factorId)
        if (preflightFailure) {
            var failureMeta = preflightCategoryMeta(preflightFailure.category)
            return buildValidationState(
                failureMeta.key || "preflight-failed",
                failureMeta.statusText,
                preflightFailure.reason || "组合回测预检失败",
                preflightFailure.instanceId
                    ? ("实例 " + preflightFailure.instanceId + " 未通过组合回测预检。" + failureMeta.detail)
                    : ("该因子未通过组合回测预检。" + failureMeta.detail),
                failureMeta.accentColor
            )
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
        root.selectedBacktestResultIndex = 0
    }

    function displayedBacktestResults() {
        if (!backtestResult) {
            return []
        }

        if (backtestResult.results && Array.isArray(backtestResult.results)) {
            return backtestResult.results
        }

        if (Object.keys(backtestResult).length > 0) {
            return [backtestResult]
        }

        return []
    }

    function displayedBacktestResultName(entry) {
        if (!entry) {
            return "未命名结果"
        }

        var config = entry.config || {}
        return String(config.factorName || config.factorId || entry.factorId || "未命名结果")
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
                if (root.selectedBacktestResultIndex < 0 || root.selectedBacktestResultIndex >= result.results.length) {
                    root.selectedBacktestResultIndex = 0
                }

                var displayedResult = result.results[root.selectedBacktestResultIndex]
                root.groupResults = displayedResult && displayedResult.groups && Array.isArray(displayedResult.groups) ? displayedResult.groups : []
                root.icirResult = displayedResult && displayedResult.icirResult ? displayedResult.icirResult : ({})
                root.summaryStats = displayedResult && displayedResult.summary ? displayedResult.summary : ({})
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

    function buildSingleFactorRunEntry(result) {
        if (!result) {
            return null
        }

        var entry = result
        if (result.results && Array.isArray(result.results) && result.results.length > 0) {
            entry = result.results[0]
        }

        if (!entry) {
            return null
        }

        var config = entry.config || {}
        var summary = entry.summary || {}
        var icir = entry.icirResult || {}

        var forward = Number(config.forwardDays || runtimeForwardDays || 1)
        var rebalance = Number(config.rebalanceDays || runtimeRebalanceDays || 1)

        return {
            runId: String(entry.taskId || (Date.now() + "_" + Math.random())),
            factorName: String(config.factorName || config.factorId || entry.factorId || selectedFactorDisplayText() || "单因子"),
            factorId: String(config.factorId || entry.factorId || ""),
            horizonTag: String(forward) + "/" + String(rebalance),
            forwardDays: forward,
            rebalanceDays: rebalance,
            annualReturn: Number(summary.annualReturn || 0),
            informationRatio: Number(summary.informationRatio || 0),
            sharpeRatio: Number(summary.sharpeRatio || 0),
            maxDrawdown: Number(summary.maxDrawdown || 0),
            turnoverRate: Number(summary.turnoverRate || 0),
            icValue: Number(icir.icValue || 0),
            irValue: Number(icir.irValue || 0),
            timestamp: Date.now()
        }
    }

    function pushSingleFactorRunHistory(result) {
        var newEntry = buildSingleFactorRunEntry(result)
        if (!newEntry) {
            return
        }

        var history = []
        for (var i = 0; i < singleFactorRunHistory.length; i++) {
            var existing = singleFactorRunHistory[i]
            if (!existing) {
                continue
            }

            // 同一因子同一周期只保留最近一次，避免重复刷屏
            if (existing.factorId === newEntry.factorId && existing.horizonTag === newEntry.horizonTag) {
                continue
            }
            history.push(existing)
        }

        history.unshift(newEntry)
        if (history.length > singleFactorRunHistoryLimit) {
            history = history.slice(0, singleFactorRunHistoryLimit)
        }

        singleFactorRunHistory = history
    }

    function formatRunTimestamp(value) {
        if (!value) {
            return "--"
        }
        return Qt.formatDateTime(new Date(value), "MM-dd hh:mm")
    }
    
    // ============ 属性 ============
    
    property Bridge.FactorService factorService: null
    property Bridge.CleanedDataController cleanedDataController: null
    
    // 因子选择相关属性 - 现在由C++控制器管理
    property var selectedFactorIds: []  // 支持多因子选择，与控制器同步
    property string selectedFactorId: ""  // 向后兼容，取第一个选中的因子
    property bool syncingSelectedFactorState: false
    property var factorSupportMapCache: ({})
    property bool supportMapRequestInFlight: false
    property bool pendingFilterAfterSupportMap: false
    property int supportMapRequestSeq: 0
    property int supportMapAppliedSeq: 0
    property double lastDatasetRefreshAtMs: 0

    onSelectedFactorIdsChanged: {
        syncingSelectedFactorState = true
        selectedFactorId = selectedFactorIds && selectedFactorIds.length > 0 ? String(selectedFactorIds[0]) : ""
        syncingSelectedFactorState = false
        if (!root.isBacktesting) {
            root.clearDisplayedBacktestState()
            root.lastBacktestError = ""
            root.lastPreflightFailures = []
            root.activeRunFactorIds = []
        }
    }

    onFactorDefinitionRevisionChanged: {
        refreshFactorSupportMap()
    }

    onSelectedFactorIdChanged: {
        if (syncingSelectedFactorState) {
            return
        }

        var normalizedFactorId = selectedFactorId ? String(selectedFactorId) : ""
        if (normalizedFactorId && (!selectedFactorIds || selectedFactorIds.length === 0)) {
            setSelectedFactors([normalizedFactorId])
            return
        }

        if (!root.isBacktesting && !normalizedFactorId && (!selectedFactorIds || selectedFactorIds.length === 0)) {
            root.clearDisplayedBacktestState()
            root.lastBacktestError = ""
            root.lastPreflightFailures = []
            root.activeRunFactorIds = []
        }
    }
    
    // 因子选择对话框
    property var factorSelectorDialog: null

    Timer {
        id: supportMapRefreshTimer
        interval: 40
        repeat: false
        onTriggered: runSupportMapRefresh()
    }

    Timer {
        id: cacheDatasetSyncTimer
        interval: 50
        repeat: false
        onTriggered: {
            rebuildCacheDatasetOptions()
            syncSelectedDatasetIndex()
            refreshFactorSupportMap()
            pendingFilterAfterSupportMap = true
            filterSelectedFactorsByCurrentCache()
        }
    }
    
    // 数据集模型 - 不再使用，由C++控制器自动处理缓存
    
    // 回测控制器 - 使用属性绑定
    Bridge.FactorBacktestController {
        id: factorBacktestController

        function controllerHasAggregatedResults() {
            return factorBacktestController.backtestResult
                    && factorBacktestController.backtestResult.results
                    && Array.isArray(factorBacktestController.backtestResult.results)
                    && factorBacktestController.backtestResult.results.length > 0
        }
        
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
            if (!controllerHasAggregatedResults()) {
                root.groupResults = factorBacktestController.groupResults
            }
        }
        onIcirResultChanged: {
            if (!controllerHasAggregatedResults()) {
                root.icirResult = factorBacktestController.icirResult
            }
        }
        onSummaryStatsChanged: {
            if (!controllerHasAggregatedResults()) {
                root.summaryStats = factorBacktestController.summaryStats
            }
        }
        onBacktestResultChanged: {
            if (!root.isBacktesting) {
                root.applyDisplayedBacktestResult(factorBacktestController.backtestResult)
            }
        }
        onLastPreflightFailuresChanged: {
            root.lastPreflightFailures = root.normalizePreflightFailures(factorBacktestController.lastPreflightFailures)
        }
        onFactorSupportMapReady: function(requestId, supportMap) {
            if (requestId < root.supportMapRequestSeq || requestId <= root.supportMapAppliedSeq) {
                return
            }

            root.supportMapAppliedSeq = requestId
            root.supportMapRequestInFlight = false
            root.factorSupportMapCache = supportMap || ({})

            if (root.pendingFilterAfterSupportMap) {
                root.pendingFilterAfterSupportMap = false
                root.filterSelectedFactorsByCurrentCache()
            }
        }
        
        onBacktestStarted: function(factorId) {
            console.log("回测开始:", factorId)
            root.clearDisplayedBacktestState()
            root.lastBacktestError = ""
            root.lastPreflightFailures = []
            if (preflightFailureDialog.visible) {
                preflightFailureDialog.close()
            }
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
            root.lastPreflightFailures = []
            if (preflightFailureDialog.visible) {
                preflightFailureDialog.close()
            }
            root.applyDisplayedBacktestResult(result)
            root.pushSingleFactorRunHistory(result)
            
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
            root.lastPreflightFailures = root.normalizePreflightFailures(factorBacktestController.lastPreflightFailures)
            if (root.lastPreflightFailures.length > 0) {
                preflightFailureDialog.failures = root.lastPreflightFailures
                preflightFailureDialog.open()
                showToast("❌ 组合回测预检失败，请查看明细")
            } else {
                showToast("❌ 回测失败: " + error)
            }
        }
        onBacktestCancelled: function() {
            console.log("回测已取消")
            root.currentGroup = 0
            root.totalGroups = 0
            root.lastBacktestError = ""
            root.lastPreflightFailures = []
            if (preflightFailureDialog.visible) {
                preflightFailureDialog.close()
            }
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
    property var lastPreflightFailures: []
    property int selectedBacktestResultIndex: 0
    property var activeRunFactorIds: []
    property var singleFactorRunHistory: []
    property int singleFactorRunHistoryLimit: 3
    
    // 分组配置
    property var groupConfig: ({})
    
    // 数据源属性
    property int selectedDatasetId: -1
    property string selectedDataSourceMode: "cache"
    property var cacheDatasetOptions: [{ text: "请选择缓存集", value: -1, raw: null }]
    property int runtimeForwardDays: 1
    property int runtimeRebalanceDays: 1
    property real runtimeSlippageRate: 0.0
    property real runtimeRiskFreeRate: 0.0
    property string runtimeBenchmarkSymbol: "000300.SH"

    onSelectedDatasetIdChanged: {
        refreshFactorSupportMap()
        pendingFilterAfterSupportMap = true
        filterSelectedFactorsByCurrentCache()
    }

    onSelectedDataSourceModeChanged: {
        if (selectedDataSourceMode === "cache") {
            if (!hasAvailableCacheDataset()) {
                console.log("当前没有可用缓存集，保持缓存模式等待数据集恢复")
                return
            }
        }

        refreshFactorSupportMap()
        pendingFilterAfterSupportMap = true
        filterSelectedFactorsByCurrentCache()
    }

    function hasAvailableCacheDataset() {
        return cacheDatasetOptions && cacheDatasetOptions.length > 1
    }

    function setDataSourceMode(mode) {
        var normalizedMode = mode === "cache" ? "cache" : "database"

        if (selectedDataSourceMode !== normalizedMode) {
            selectedDataSourceMode = normalizedMode
        }

        if (factorBacktestController) {
            factorBacktestController.dataSourceMode = normalizedMode
        }

        if (dataSourceComboBox && dataSourceComboBox.model) {
            for (var index = 0; index < dataSourceComboBox.model.length; index++) {
                if (dataSourceComboBox.model[index].value === normalizedMode) {
                    if (dataSourceComboBox.currentIndex !== index) {
                        dataSourceComboBox.currentIndex = index
                    }
                    break
                }
            }
        }
    }

    function syncBacktestRuntimeParamsToController() {
        if (!factorBacktestController) {
            return
        }

        factorBacktestController.backtestRuntimeParams = {
            forwardDays: Math.max(1, Number(runtimeForwardDays) || 1),
            rebalanceDays: Math.max(1, Number(runtimeRebalanceDays) || 1),
            slippageRate: Math.max(0, Number(runtimeSlippageRate) || 0),
            riskFreeRate: Math.max(0, Number(runtimeRiskFreeRate) || 0),
            benchmarkSymbol: String(runtimeBenchmarkSymbol || "000300.SH").trim().toUpperCase()
        }
    }

    function ensureUsableDataSourceMode() {
        if (selectedDataSourceMode === "cache" && !hasAvailableCacheDataset()) {
            console.log("当前没有可用缓存集，保持用户选择的缓存模式")
        }
    }

    function appendCacheDatasetOption(options, dataset) {
        if (!datasetSelectableForBacktest(dataset)) {
            return
        }

        for (var index = 0; index < options.length; index++) {
            if (options[index].value === dataset.id) {
                return
            }
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

    function cacheDatasetOptionText(index) {
        if (!cacheDatasetOptions || index < 0 || index >= cacheDatasetOptions.length) {
            return ""
        }

        var option = cacheDatasetOptions[index]
        return option && option.text ? option.text : ""
    }

    function datasetSelectableForBacktest(dataset) {
        if (!dataset || dataset.id === undefined) {
            return false
        }

        if (dataset.isBacktestReady) {
            return true
        }

        var fields = normalizeStringList(dataset.availableFields)
        var stockCodes = normalizeStockPoolSymbols(dataset.stockCodes)
        return fields.length > 0 && stockCodes.length > 0
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
                appendCacheDatasetOption(options, dataset)
            }
        }

        if (cleanedDataController
                && cleanedDataController.selectedDatasetInfo
                && cleanedDataController.selectedDatasetInfo.id !== undefined) {
            appendCacheDatasetOption(options, cleanedDataController.selectedDatasetInfo)
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

    function refreshDatasetsThrottled(forceRefresh) {
        if (!cleanedDataController || typeof cleanedDataController.refreshDatasets !== "function") {
            return
        }

        var nowMs = Date.now()
        if (!forceRefresh && lastDatasetRefreshAtMs > 0 && (nowMs - lastDatasetRefreshAtMs) < 1500) {
            return
        }

        lastDatasetRefreshAtMs = nowMs
        cleanedDataController.refreshDatasets()
    }

    onVisibleChanged: {
        if (visible && cleanedDataController) {
            refreshDatasetsThrottled(false)
            cacheDatasetSyncTimer.restart()
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
                    id: backtestControlPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(460, controlPanelContent.implicitHeight + 32)
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        id: controlPanelContent
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

                            Text {
                                Layout.fillWidth: true
                                text: selectedFactorIds.length > 0
                                    ? ("已选 " + selectedFactorIds.length + " 个因子，支持多选")
                                    : "请选择要回测的因子"
                                font.pixelSize: 12
                                color: selectedFactorIds.length > 0 ? "#38BDF8" : "#94A3B8"
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
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

                                Text {
                                    Layout.fillWidth: true
                                    text: selectedFactorIds.length > 0
                                        ? ("当前已选择 " + selectedFactorIds.length + " 个因子: " + selectedFactorDisplayText())
                                        : "当前未选择因子"
                                    font.pixelSize: 11
                                    color: selectedFactorIds.length > 0 ? "#38BDF8" : "#64748B"
                                    wrapMode: Text.WordWrap
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
                                                anchors.right: removeFactorText.left
                                                anchors.rightMargin: 8
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

                                            Text {
                                                id: removeFactorText
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.right: parent.right
                                                anchors.rightMargin: 12
                                                text: "×"
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: "#94A3B8"

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.removeSelectedFactor(modelData)
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

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "持有期(天)"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }

                                ComboBox {
                                    id: forwardDaysComboBox
                                    Layout.preferredWidth: 92
                                    model: [1, 3, 5, 10, 20]

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    contentItem: Text {
                                        text: forwardDaysComboBox.displayText
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        horizontalAlignment: Text.AlignLeft
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    onActivated: function(index) {
                                        runtimeForwardDays = Number(model[index])
                                        if (runtimeRebalanceDays <= 0) {
                                            runtimeRebalanceDays = runtimeForwardDays
                                        }
                                        root.syncBacktestRuntimeParamsToController()
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "调仓周期(天)"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }

                                ComboBox {
                                    id: rebalanceDaysComboBox
                                    Layout.preferredWidth: 104
                                    model: [1, 3, 5, 10, 20]

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    contentItem: Text {
                                        text: rebalanceDaysComboBox.displayText
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        horizontalAlignment: Text.AlignLeft
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    onActivated: function(index) {
                                        runtimeRebalanceDays = Number(model[index])
                                        root.syncBacktestRuntimeParamsToController()
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "滑点(%)"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }

                                TextField {
                                    id: slippageRateField
                                    Layout.preferredWidth: 90
                                    text: String((runtimeSlippageRate * 100).toFixed(3))
                                    color: "#F1F5F9"
                                    validator: DoubleValidator { bottom: 0.0; top: 100.0; decimals: 4 }

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    onEditingFinished: {
                                        runtimeSlippageRate = Math.max(0, Number(text) || 0) / 100.0
                                        text = String((runtimeSlippageRate * 100).toFixed(3))
                                        root.syncBacktestRuntimeParamsToController()
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "无风险(%)"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }

                                TextField {
                                    id: riskFreeRateField
                                    Layout.preferredWidth: 90
                                    text: String((runtimeRiskFreeRate * 100).toFixed(2))
                                    color: "#F1F5F9"
                                    validator: DoubleValidator { bottom: 0.0; top: 100.0; decimals: 4 }

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    onEditingFinished: {
                                        runtimeRiskFreeRate = Math.max(0, Number(text) || 0) / 100.0
                                        text = String((runtimeRiskFreeRate * 100).toFixed(2))
                                        root.syncBacktestRuntimeParamsToController()
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "基准"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }

                                TextField {
                                    id: benchmarkSymbolField
                                    Layout.preferredWidth: 120
                                    text: runtimeBenchmarkSymbol
                                    color: "#F1F5F9"

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    onEditingFinished: {
                                        runtimeBenchmarkSymbol = String(text || "000300.SH").trim().toUpperCase()
                                        if (!runtimeBenchmarkSymbol) {
                                            runtimeBenchmarkSymbol = "000300.SH"
                                        }
                                        text = runtimeBenchmarkSymbol
                                        root.syncBacktestRuntimeParamsToController()
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

                                        root.setDataSourceMode(model[currentIndex].value)
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
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Item { Layout.fillWidth: true }

                            // 回测按钮
                            Rectangle {
                                id: backtestButton
                                Layout.preferredWidth: 120
                                Layout.minimumWidth: 120
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
                                Layout.minimumWidth: 80
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

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 122
                            radius: 10
                            color: "#0F172A"
                            border.width: 1
                            border.color: "#2B3A55"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: "股票池覆盖与对比"
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: previousBacktestReport && Object.keys(previousBacktestReport).length > 0
                                            ? ("上一轮基线: " + ((previousBacktestReport.config && previousBacktestReport.config.factorName) || previousBacktestReport.factorId || "当前因子"))
                                            : "上一轮基线: 暂无"
                                        font.pixelSize: 11
                                        color: "#93C5FD"
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Repeater {
                                        model: [
                                            { title: "上一轮股票池", count: root.resolveFactorBacktestStockPoolComparison().previousSymbols.length, accent: "#38BDF8" },
                                            { title: "本次候选池", count: root.resolveFactorBacktestStockPoolComparison().currentSymbols.length, accent: "#34D399" },
                                            { title: "交集", count: root.resolveFactorBacktestStockPoolComparison().intersectionSymbols.length, accent: "#F59E0B" }
                                        ]

                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 42
                                            radius: 8
                                            color: "#111827"
                                            border.width: 1
                                            border.color: modelData.accent

                                            Row {
                                                anchors.centerIn: parent
                                                spacing: 8

                                                Text {
                                                    text: modelData.title
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                }

                                                Text {
                                                    text: modelData.count + " 只"
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    color: modelData.accent
                                                }
                                            }
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Text {
                                        Layout.fillWidth: true
                                        text: root.buildFactorStockPoolComparisonText() + " 回测完成后，系统会自动比较上一轮和本轮结果；结果明显时自动覆盖，结果接近时再让你确认。"
                                        font.pixelSize: 10
                                        color: "#94A3B8"
                                        wrapMode: Text.WordWrap
                                    }
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
                                    text: activeRunFactorIds.length > 0
                                        ? ("本次回测: " + activeRunFactorIds.length + " 个因子")
                                        : (selectedFactorIds.length > 0 ? ("待回测: " + selectedFactorIds.length + " 个因子") : "")
                                    font.pixelSize: 12
                                    color: "#38BDF8"
                                    visible: activeRunFactorIds.length > 0 || selectedFactorIds.length > 0
                                }

                                Text {
                                    text: activeRunFactorIds.length > 0 ? activeRunFactorDisplayText() : selectedFactorDisplayText()
                                    font.pixelSize: 11
                                    color: "#94A3B8"
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    visible: activeRunFactorIds.length > 0 || selectedFactorIds.length > 0
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

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: preflightSummaryColumn.implicitHeight + 24
                            radius: 10
                            color: "#3F1D24"
                            border.width: 1
                            border.color: "#F87171"
                            visible: !isBacktesting && lastPreflightFailures.length > 0

                            ColumnLayout {
                                id: preflightSummaryColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: "⚠️ 组合回测预检未通过"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: "#FECACA"
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: "失败因子: " + lastPreflightFailures.length + " 个"
                                        font.pixelSize: 11
                                        color: "#FCA5A5"
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 92
                                        Layout.preferredHeight: 28
                                        radius: 6
                                        color: "#7F1D1D"

                                        Text {
                                            anchors.centerIn: parent
                                            text: "查看明细"
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            color: "#FEE2E2"
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                preflightFailureDialog.failures = lastPreflightFailures
                                                preflightFailureDialog.open()
                                            }
                                        }
                                    }
                                }

                                Repeater {
                                    model: Math.min(lastPreflightFailures.length, 2)

                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        radius: 8
                                        color: "#2A1520"
                                        border.width: 1
                                        border.color: failureMeta.accentColor
                                        implicitHeight: failureSummaryColumn.implicitHeight + 14

                                        property var failure: lastPreflightFailures[index]
                                        property var failureMeta: root.preflightCategoryMeta(failure && failure.category)

                                        ColumnLayout {
                                            id: failureSummaryColumn
                                            anchors.fill: parent
                                            anchors.margins: 7
                                            spacing: 4

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: root.resolveFactorDisplayName((failure && failure.factorId) || "")
                                                    font.pixelSize: 11
                                                    font.weight: Font.DemiBold
                                                    color: "#FEE2E2"
                                                    elide: Text.ElideRight
                                                }

                                                Rectangle {
                                                    radius: 9
                                                    color: failureMeta.chipBackground
                                                    border.width: 1
                                                    border.color: failureMeta.chipBorder
                                                    implicitWidth: failureChipText.implicitWidth + 12
                                                    implicitHeight: failureChipText.implicitHeight + 8

                                                    Text {
                                                        id: failureChipText
                                                        anchors.centerIn: parent
                                                        text: failureMeta.shortText
                                                        font.pixelSize: 10
                                                        font.weight: Font.Medium
                                                        color: failureMeta.chipText
                                                    }
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: (failure && failure.reason) || "未知预检失败"
                                                font.pixelSize: 11
                                                color: "#FECACA"
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }

                                Text {
                                    visible: lastPreflightFailures.length > 2
                                    text: "其余 " + (lastPreflightFailures.length - 2) + " 个失败项请在明细中查看"
                                    font.pixelSize: 10
                                    color: "#FCA5A5"
                                }
                            }
                        }
                    }
                }
            
                // 主要内容区域
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: singleFactorRunHistory.length > 0 ? 180 : 0
                    visible: singleFactorRunHistory.length > 0
                    radius: 12
                    color: "#1E293B"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "🧭 A/B/C 单因子三组对照"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: "#F8FAFC"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "最近 " + singleFactorRunHistory.length + " 组"
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: Math.min(3, singleFactorRunHistory.length)
                            columnSpacing: 10
                            rowSpacing: 8

                            Repeater {
                                model: singleFactorRunHistory

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 118
                                    radius: 10
                                    color: "#111827"
                                    border.width: 1
                                    border.color: "#334155"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 4

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Text {
                                                text: (modelData.factorName || "单因子") + "  ·  " + modelData.horizonTag
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                color: "#E2E8F0"
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: root.formatRunTimestamp(modelData.timestamp)
                                                font.pixelSize: 10
                                                color: "#64748B"
                                            }
                                        }

                                        Text {
                                            text: "年化 " + root.formatPercentMetric(modelData.annualReturn, 2)
                                                + "  IR " + root.formatMetric(modelData.informationRatio, 2)
                                                + "  夏普 " + root.formatMetric(modelData.sharpeRatio, 2)
                                            font.pixelSize: 11
                                            color: "#CBD5E1"
                                            wrapMode: Text.NoWrap
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: "IC " + root.formatMetric(modelData.icValue, 3)
                                                + "  因子IR " + root.formatMetric(modelData.irValue, 2)
                                                + "  换手 " + root.formatMetric(modelData.turnoverRate, 2)
                                            font.pixelSize: 11
                                            color: "#94A3B8"
                                            wrapMode: Text.NoWrap
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: "最大回撤 " + root.formatPercentMetric(modelData.maxDrawdown, 2)
                                            font.pixelSize: 11
                                            color: "#F59E0B"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 640
                    spacing: 16

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 12
                        color: "#1E293B"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: "📊 分组内容"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }

                                Item { Layout.fillWidth: true }

                                ComboBox {
                                    id: resultSelector
                                    Layout.preferredWidth: 220
                                    visible: displayedBacktestResults().length > 1
                                    model: displayedBacktestResults()
                                    currentIndex: selectedBacktestResultIndex

                                    delegate: ItemDelegate {
                                        width: resultSelector.width
                                        text: root.displayedBacktestResultName(modelData)
                                    }

                                    contentItem: Text {
                                        text: resultSelector.currentIndex >= 0 && resultSelector.currentIndex < displayedBacktestResults().length
                                            ? root.displayedBacktestResultName(displayedBacktestResults()[resultSelector.currentIndex])
                                            : "选择回测结果"
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }

                                    background: Rectangle {
                                        radius: 8
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    onActivated: function(index) {
                                        root.selectedBacktestResultIndex = index
                                        root.applyDisplayedBacktestResult(root.backtestResult)
                                    }
                                }

                                Text {
                                    text: groupResults.length > 0 ? "共 " + groupResults.length + " 个分组" : "等待回测结果"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(320, riskSummaryColumn.implicitHeight + 20)
                                Layout.maximumHeight: 320
                                radius: 10
                                color: "#111827"
                                border.width: 1
                                border.color: "#334155"
                                visible: Object.keys(summaryStats).length > 0

                                ScrollView {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    clip: true

                                    ColumnLayout {
                                        id: riskSummaryColumn
                                        width: parent.width
                                        spacing: 8

                                        Text {
                                            text: "核心回测指标"
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            color: "#E2E8F0"
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: width < 560 ? 2 : 4
                                            columnSpacing: 8
                                            rowSpacing: 8

                                            Repeater {
                                                model: [
                                                    { title: "年化收益", value: root.formatPercentMetric(summaryStats.annualReturn, 2), description: "多空组合年化", trend: root.returnMetricTrend(summaryStats.annualReturn), color: root.returnMetricColor(summaryStats.annualReturn) },
                                                    { title: "夏普比率", value: root.formatMetric(summaryStats.sharpeRatio, 2), description: "收益/波动", trend: root.returnMetricTrend(summaryStats.sharpeRatio), color: root.returnMetricColor(summaryStats.sharpeRatio) },
                                                    { title: "最大回撤", value: root.formatPercentMetric(summaryStats.maxDrawdown, 2), description: "越低越稳健", trend: Number(summaryStats.maxDrawdown || 0) > 0 ? "down" : "neutral", color: "#F59E0B" },
                                                    { title: "胜率", value: root.formatPercentMetric(root.normalizedWinRate(summaryStats.winRate), 2), description: "正收益周期占比", trend: root.returnMetricTrend(root.normalizedWinRate(summaryStats.winRate) - 0.5), color: "#22C55E" }
                                                ]

                                                delegate: KeyMetricCard {
                                                    Layout.fillWidth: true
                                                    title: modelData.title
                                                    value: modelData.value
                                                    description: modelData.description
                                                    trend: modelData.trend
                                                    color: modelData.color
                                                }
                                            }
                                        }

                                        Text {
                                            text: "风控摘要"
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            color: "#E2E8F0"
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            implicitHeight: riskSummaryText.implicitHeight + 14
                                            radius: 8
                                            color: "#0F172A"
                                            border.width: 1
                                            border.color: "#1E293B"

                                            Text {
                                                id: riskSummaryText
                                                anchors.fill: parent
                                                anchors.margins: 7
                                                text: "风控摘要: " + root.formatTextMetric((summaryStats.riskMetrics || {}).riskControlSummary, "未触发风控")
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                    }
                                }
                            }

                            ListView {
                                id: groupListView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumHeight: 220
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

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8
                                        color: "#3B82F620"
                                        border.width: 2
                                        border.color: "#3B82F6"
                                        visible: isBacktesting && currentGroup === (index + 1)
                                    }
                                }

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

    Dialog {
        id: preflightFailureDialog
        modal: true
        title: "组合回测预检失败明细"
        standardButtons: Dialog.Ok
        width: Math.min(root.width - 48, 760)
        property var failures: []

        background: Rectangle {
            radius: 12
            color: "#111827"
            border.width: 1
            border.color: "#334155"
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "以下因子未通过本次组合回测预检。每条记录都会明确显示失败类别，便于区分实例异常、实现未接入、缓存字段缺失、字段值异常或样本不足。"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#CBD5E1"
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 320
                radius: 8
                color: "#0F172A"
                border.width: 1
                border.color: "#1E293B"

                ListView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 8
                    model: preflightFailureDialog.failures

                    delegate: Rectangle {
                        width: ListView.view.width
                        implicitHeight: failureDetailColumn.implicitHeight + 20
                        radius: 8
                        color: "#131C2E"
                        border.width: 1
                        border.color: failureMeta.accentColor

                        property var failureMeta: root.preflightCategoryMeta(modelData && modelData.category)

                        ColumnLayout {
                            id: failureDetailColumn
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: root.resolveFactorDisplayName(modelData.factorId || "")
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: "#FEE2E2"
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    radius: 9
                                    color: failureMeta.chipBackground
                                    border.width: 1
                                    border.color: failureMeta.chipBorder
                                    implicitWidth: detailChipText.implicitWidth + 12
                                    implicitHeight: detailChipText.implicitHeight + 8

                                    Text {
                                        id: detailChipText
                                        anchors.centerIn: parent
                                        text: failureMeta.shortText
                                        font.pixelSize: 10
                                        font.weight: Font.Medium
                                        color: failureMeta.chipText
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.instanceId ? ("instanceId: " + modelData.instanceId) : "instanceId: 未解析"
                                font.pixelSize: 11
                                color: "#FCA5A5"
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.reason || "未知预检失败"
                                font.pixelSize: 11
                                color: "#CBD5E1"
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.preflightFailureDetailText(modelData)
                                font.pixelSize: 10
                                color: "#94A3B8"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {}
                }
            }

            Text {
                Layout.fillWidth: true
                text: "诊断文本支持手动全选复制，可直接用于问题排查或反馈。"
                font.pixelSize: 11
                color: "#94A3B8"
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 132
                    Layout.preferredHeight: 30
                    radius: 6
                    color: "#1D4ED8"

                    Text {
                        anchors.centerIn: parent
                        text: "复制诊断文本"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        color: "#EFF6FF"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            preflightFailureExportTextArea.selectAll()
                            preflightFailureExportTextArea.copy()
                            showToast("📋 诊断文本已复制")
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                radius: 8
                color: "#0B1220"
                border.width: 1
                border.color: "#1E293B"

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8

                    TextArea {
                        id: preflightFailureExportTextArea
                        readOnly: true
                        selectByMouse: true
                        text: root.buildPreflightFailureExportText(preflightFailureDialog.failures)
                        wrapMode: TextEdit.NoWrap
                        color: "#CBD5E1"
                        selectionColor: "#1D4ED8"
                        selectedTextColor: "#F8FAFC"
                        font.pixelSize: 11
                        background: null
                    }
                }
            }
        }
    }
    
    // ============ 组件定义 ============
    
    // 关键指标卡片组件
    component KeyMetricCard: Item {
        id: metricCardRoot
        property string title: ""
        property string value: ""
        property string description: ""
        property string color: "#F1F5F9"
        property string trend: "neutral"
        property string trendColor: root.returnTrendColor(trend)
        
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
                        color: metricCardRoot.color
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
        refreshFactorSupportMap()
        
        if (selectedFactorIds.length === 0) {
            console.log("请先选择要回测的因子")
            return
        }

        var supportMap = currentCacheFactorSupportMap()
        var unsupportedFactors = []
        for (var unsupportedIndex = 0; unsupportedIndex < selectedFactorIds.length; unsupportedIndex++) {
            var unsupportedId = selectedFactorIds[unsupportedIndex]
            var supportInfo = supportMap[String(unsupportedId)]
            if (supportInfo && supportInfo.supported === false) {
                unsupportedFactors.push(resolveFactorDisplayName(unsupportedId) + " (" + supportInfo.reason + ")")
            }
        }

        if (unsupportedFactors.length > 0) {
            console.log("以下因子当前不能参与回测，请重新选择:", unsupportedFactors.join("; "))
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
            factorIdList = normalizeSelectedFactorIds(factorIdList)
            root.activeRunFactorIds = factorIdList.slice()
            console.log("本次实际提交回测的因子:", JSON.stringify(factorIdList))
            
            // 直接设置控制器的selectedFactorIds属性（而不是调用方法）
            factorBacktestController.selectedFactorIds = factorIdList
            factorBacktestController.selectedDatasetId = selectedDatasetId
            factorBacktestController.dataSourceMode = selectedDataSourceMode
            root.syncBacktestRuntimeParamsToController()
            
            // 调用C++控制器开始回测，传递当前选中数据集对应的日期范围
            factorBacktestController.startBacktest(groupComboBox.currentText, selectedStartDate, selectedEndDate)
        }
    }
    
    // 打开因子选择对话框 - 简化版本
    function openFactorSelector() {
        console.log("打开因子选择对话框")
        refreshFactorSupportMap()
        
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
        setSelectedFactors(factorIds)
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
        var nextFactorIds = []
        for (var i = 0; i < selectedFactorIds.length; i++) {
            if (String(selectedFactorIds[i]) !== String(factorId)) {
                nextFactorIds.push(selectedFactorIds[i])
            }
        }
        setSelectedFactors(nextFactorIds)
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
        root.setDataSourceMode(selectedDataSourceMode)
        root.syncBacktestRuntimeParamsToController()

        if (cleanedDataController) {
            if (!cleanedDataController.isAvailable) {
                cleanedDataController.initialize()
            }
            refreshDatasetsThrottled(true)
        }

        cacheDatasetSyncTimer.restart()
        root.clearDisplayedBacktestState()
        root.activeRunFactorIds = []
        refreshFactorSupportMap()
        pendingFilterAfterSupportMap = true
        
        // 数据源和日期范围处理已移至C++控制器，QML只负责UI显示
        console.log("因子回测页面初始化完成，等待用户操作")
    }

    Connections {
        target: cleanedDataController

        function onDatasetListChanged() {
            cacheDatasetSyncTimer.restart()
        }

        function onSelectedDatasetChanged() {
            if (cleanedDataController && cleanedDataController.selectedDatasetInfo && cleanedDataController.selectedDatasetInfo.id !== undefined) {
                if (datasetSelectableForBacktest(cleanedDataController.selectedDatasetInfo)) {
                    selectedDatasetId = cleanedDataController.selectedDatasetInfo.id
                    factorBacktestController.selectedDatasetId = selectedDatasetId
                }
            }
            cacheDatasetSyncTimer.restart()
        }

        function onSelectedDatasetDiagnosticsChanged() {
            cacheDatasetSyncTimer.restart()
        }
    }
}

// FactorBacktestPage.qml
// 因子回测页面 - 重新设计版本
// 专注于回测进度监控和分组内容展示
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/MarketEnvironmentProfile.js" as MarketEnvironmentProfile

/**
 * 因子回测页面组件 - 重新设计版本
 * 专注于回测进度监控和分组内容展示
 */
Item {
    id: root

    signal analysisReportRequested(var result)
    property var previousBacktestReport: ({})
    property int factorDefinitionRevision: 0
    property var factorDisplayNameCache: ({})
    property var factorDefinitionCache: ({})
    readonly property int preAdjustPriceType: 0
    readonly property int postAdjustPriceType: 1
    readonly property var marketEnvironmentOptions: MarketEnvironmentProfile.options()

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

    function currentCacheSupportSnapshot() {
        var cacheDateRange = currentCacheDateRange()
        var cacheDatasetInfo = currentCacheDatasetInfo()
        return {
            startDate: cacheDateRange.startDate,
            endDate: cacheDateRange.endDate,
            availableFields: currentCacheAvailableFields(),
            fieldDiagnostics: currentCacheFieldDiagnostics(),
            tradeDateCount: cacheDatasetInfo && cacheDatasetInfo.tradeDateCount !== undefined
                ? cacheDatasetInfo.tradeDateCount
                : 0
        }
    }

    function runtimeParamsSnapshot() {
        if (!factorBacktestController || !factorBacktestController.backtestRuntimeParams) {
            return ({})
        }
        return factorBacktestController.backtestRuntimeParams
    }

    function runtimePercentToText(rate) {
        var numeric = Number(rate)
        if (!isFinite(numeric)) {
            numeric = 0
        }
        return (numeric * 100).toFixed(2)
    }

    function shallowCopyMap(source) {
        var target = {}
        if (!source) {
            return target
        }

        for (var key in source) {
            if (Object.prototype.hasOwnProperty.call(source, key)) {
                target[key] = source[key]
            }
        }

        return target
    }

    function normalizedBenchmarkText(value) {
        if (value === undefined || value === null) {
            return ""
        }
        return String(value).trim().toUpperCase()
    }

    function resolveBenchmarkSymbolFromValue(value, allowGenericKeys) {
        if (value === undefined || value === null) {
            return ""
        }

        if (Array.isArray(value)) {
            for (var arrayIndex = 0; arrayIndex < value.length; arrayIndex++) {
                var arraySymbol = resolveBenchmarkSymbolFromValue(value[arrayIndex], allowGenericKeys)
                if (arraySymbol) {
                    return arraySymbol
                }
            }
            return ""
        }

        if (typeof value === "object") {
            var directKeys = ["benchmarkSymbol", "benchmark_symbol", "benchmarkCode", "benchmark_code", "indexSymbol", "index_symbol", "indexCode", "index_code"]
            for (var directIndex = 0; directIndex < directKeys.length; directIndex++) {
                var directKey = directKeys[directIndex]
                if (Object.prototype.hasOwnProperty.call(value, directKey)) {
                    var directSymbol = normalizedBenchmarkText(value[directKey])
                    if (directSymbol) {
                        return directSymbol
                    }
                }
            }

            var nestedKeys = ["benchmark", "benchmarkInfo", "benchmarkMetadata", "index", "indexInfo", "indexMetadata"]
            for (var nestedIndex = 0; nestedIndex < nestedKeys.length; nestedIndex++) {
                var nestedKey = nestedKeys[nestedIndex]
                if (Object.prototype.hasOwnProperty.call(value, nestedKey)) {
                    var nestedSymbol = resolveBenchmarkSymbolFromValue(value[nestedKey], true)
                    if (nestedSymbol) {
                        return nestedSymbol
                    }
                }
            }

            if (allowGenericKeys === true) {
                var genericKeys = ["symbol", "code"]
                for (var genericIndex = 0; genericIndex < genericKeys.length; genericIndex++) {
                    var genericKey = genericKeys[genericIndex]
                    if (Object.prototype.hasOwnProperty.call(value, genericKey)) {
                        var genericSymbol = normalizedBenchmarkText(value[genericKey])
                        if (genericSymbol) {
                            return genericSymbol
                        }
                    }
                }
            }

            return ""
        }

        return normalizedBenchmarkText(value)
    }

    function resolvedSelectedDatasetBenchmarkMetadata() {
        var datasetInfo = currentCacheDatasetInfo()
        if (!datasetInfo || typeof datasetInfo !== "object") {
            return ({})
        }

        var metadata = {}
        var keys = ["benchmarkSymbol", "benchmark_symbol", "benchmarkCode", "benchmark_code", "indexSymbol", "index_symbol", "indexCode", "index_code", "benchmark", "benchmarkInfo", "benchmarkMetadata", "index", "indexInfo", "indexMetadata"]
        for (var index = 0; index < keys.length; index++) {
            var key = keys[index]
            if (Object.prototype.hasOwnProperty.call(datasetInfo, key)) {
                metadata[key] = datasetInfo[key]
            }
        }

        return metadata
    }

    function resolvedDatasetBenchmarkSymbol() {
        var metadata = factorBacktestController && factorBacktestController.selectedDatasetBenchmarkMetadata
            ? factorBacktestController.selectedDatasetBenchmarkMetadata
            : ({})
        var symbol = resolveBenchmarkSymbolFromValue(metadata, false)
        if (symbol) {
            return symbol
        }

        symbol = resolveBenchmarkSymbolFromValue(currentCacheDatasetInfo(), false)
        return symbol || "000300.SH"
    }

    function resolvedRuntimeBenchmarkSymbol(params) {
        var configured = normalizedBenchmarkText(params && params.benchmarkSymbol !== undefined ? params.benchmarkSymbol : "")
        if (configured) {
            return configured
        }
        return resolvedDatasetBenchmarkSymbol()
    }

    function syncSelectedDatasetBenchmarkMetadata() {
        if (!factorBacktestController) {
            return
        }

        var metadata = resolvedSelectedDatasetBenchmarkMetadata()
        factorBacktestController.selectedDatasetBenchmarkMetadata = metadata

        var datasetBenchmark = resolveBenchmarkSymbolFromValue(metadata, false)
        if (!datasetBenchmark) {
            return
        }

        var current = runtimeParamsSnapshot()
        var currentBenchmark = normalizedBenchmarkText(current.benchmarkSymbol)
        if (!currentBenchmark || currentBenchmark === lastAutoBenchmarkSymbol || currentBenchmark === "000300.SH") {
            var next = shallowCopyMap(current)
            next.benchmarkSymbol = datasetBenchmark
            factorBacktestController.backtestRuntimeParams = next
            runtimeBenchmarkSymbolField.text = datasetBenchmark
        }

        lastAutoBenchmarkSymbol = datasetBenchmark
    }

    function loadRuntimeParamsDialog() {
        var params = runtimeParamsSnapshot()
        runtimeMarketEnvironmentComboBox.currentIndex = MarketEnvironmentProfile.indexForValue(
                    params.marketEnvironmentProfile !== undefined && params.marketEnvironmentProfile !== null
                    ? params.marketEnvironmentProfile
                    : MarketEnvironmentProfile.GENERIC_EQUITY)
        runtimeInitialCapitalField.text = String(params.initialCapital !== undefined && params.initialCapital !== null ? params.initialCapital : 1000000)
        runtimeForwardDaysField.text = String(params.forwardDays !== undefined && params.forwardDays !== null ? params.forwardDays : 30)
        runtimeRebalanceDaysField.text = String(params.rebalanceDays !== undefined && params.rebalanceDays !== null ? params.rebalanceDays : 15)
        runtimeTransactionCostField.text = runtimePercentToText(params.commissionRate !== undefined && params.commissionRate !== null ? params.commissionRate : 0.001)
        runtimeSlippageRateField.text = runtimePercentToText(params.slippageRate !== undefined && params.slippageRate !== null ? params.slippageRate : 0.001)
        runtimeRiskFreeRateField.text = runtimePercentToText(params.riskFreeRate !== undefined && params.riskFreeRate !== null ? params.riskFreeRate : 0.02)
        runtimeBenchmarkSymbolField.text = resolvedRuntimeBenchmarkSymbol(params)

        var adjustPriceType = params.adjustPriceType !== undefined && params.adjustPriceType !== null
            ? params.adjustPriceType
            : postAdjustPriceType
        if (typeof adjustPriceType !== "number" || !isFinite(adjustPriceType)) {
            runtimeAdjustPriceTypePreButton.checked = false
            runtimeAdjustPriceTypePostButton.checked = false
            return
        }
        runtimeAdjustPriceTypePreButton.checked = adjustPriceType === preAdjustPriceType
        runtimeAdjustPriceTypePostButton.checked = adjustPriceType === postAdjustPriceType
    }

    function applyRuntimeParamsDialog() {
        if (!factorBacktestController) {
            return
        }

        var current = runtimeParamsSnapshot()
        if (!runtimeAdjustPriceTypePreButton.checked && !runtimeAdjustPriceTypePostButton.checked) {
            return
        }
        var runtimeParams = shallowCopyMap(current)
        runtimeParams.marketEnvironmentProfile = MarketEnvironmentProfile.valueForIndex(runtimeMarketEnvironmentComboBox.currentIndex)
        runtimeParams.initialCapital = parseFloat(runtimeInitialCapitalField.text) || current.initialCapital || 1000000
        runtimeParams.forwardDays = parseInt(runtimeForwardDaysField.text) || current.forwardDays || 30
        runtimeParams.rebalanceDays = parseInt(runtimeRebalanceDaysField.text) || current.rebalanceDays || 15
        runtimeParams.commissionRate = parseFloat(runtimeTransactionCostField.text) / 100 || current.commissionRate || 0.001
        runtimeParams.slippageRate = parseFloat(runtimeSlippageRateField.text) / 100 || current.slippageRate || 0.001
        runtimeParams.riskFreeRate = parseFloat(runtimeRiskFreeRateField.text) / 100 || current.riskFreeRate || 0.02
        runtimeParams.benchmarkSymbol = runtimeBenchmarkSymbolField.text ? String(runtimeBenchmarkSymbolField.text).trim().toUpperCase() : resolvedRuntimeBenchmarkSymbol(current)
        runtimeParams.adjustPriceType = runtimeAdjustPriceTypePreButton.checked ? preAdjustPriceType : postAdjustPriceType

        factorBacktestController.backtestRuntimeParams = runtimeParams
    }

    function openRuntimeParamsDialog() {
        loadRuntimeParamsDialog()
        runtimeParamsDialog.open()
    }

    function runtimeParamsSummaryText() {
        var params = runtimeParamsSnapshot()
        var marketEnvironmentLabel = MarketEnvironmentProfile.label(
                    params.marketEnvironmentProfile !== undefined && params.marketEnvironmentProfile !== null
                    ? params.marketEnvironmentProfile
                    : MarketEnvironmentProfile.GENERIC_EQUITY)
        var initialCapital = params.initialCapital !== undefined && params.initialCapital !== null ? params.initialCapital : 1000000
        var forwardDays = params.forwardDays !== undefined && params.forwardDays !== null ? params.forwardDays : 30
        var rebalanceDays = params.rebalanceDays !== undefined && params.rebalanceDays !== null ? params.rebalanceDays : 15
        var commissionRate = runtimePercentToText(params.commissionRate !== undefined && params.commissionRate !== null ? params.commissionRate : 0.001)
        var slippageRate = runtimePercentToText(params.slippageRate !== undefined && params.slippageRate !== null ? params.slippageRate : 0.001)
        return marketEnvironmentLabel + " · 初始资金 " + formatAssetMetric(initialCapital)
                + " · 持仓 " + forwardDays + " 天 · 调仓 " + rebalanceDays + " 天 · 手续费 " + commissionRate + "% · 滑点 " + slippageRate + "%"
    }

    function normalizePreflightFailures(value) {
        var normalized = []
        if (!value || !value.length) {
            return normalized
        }

        for (var i = 0; i < value.length; i++) {
            var item = value[i]
            if (!item) {
                continue
            }

            normalized.push({
                factorId: item.factorId !== undefined && item.factorId !== null ? String(item.factorId) : "",
                instanceId: item.instanceId !== undefined && item.instanceId !== null ? String(item.instanceId) : "",
                reason: item.reason !== undefined && item.reason !== null ? String(item.reason) : "",
                category: item.category !== undefined && item.category !== null ? String(item.category) : "",
                runFailureReason: item.runFailureReason !== undefined && item.runFailureReason !== null ? String(item.runFailureReason) : "",
                runErrorCode: item.runErrorCode !== undefined && item.runErrorCode !== null ? String(item.runErrorCode) : ""
            })
        }

        return normalized
    }

    function positiveDatasetId(value) {
        var numeric = Number(value)
        if (!isFinite(numeric) || numeric <= 0) {
            return 0
        }
        return Math.floor(numeric)
    }

    function resolvedSelectedDatasetId() {
        var selectedId = positiveDatasetId(selectedCacheDatasetId)
        if (selectedId > 0) {
            return selectedId
        }

        if (factorBacktestController) {
            selectedId = positiveDatasetId(factorBacktestController.selectedDatasetId)
            if (selectedId > 0) {
                return selectedId
            }
        }

        if (cleanedDataController && cleanedDataController.selectedDatasetInfo) {
            return positiveDatasetId(cleanedDataController.selectedDatasetInfo.id)
        }

        return 0
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
            values = value.split(/[,;\s，；]+/)
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

            var normalizedItem = String(item).trim()
            if (!normalizedItem || seen[normalizedItem]) {
                continue
            }

            seen[normalizedItem] = true
            normalized.push(normalizedItem)
        }

        return normalized
    }

    function currentCacheDatasetInfo() {
        var selectedId = resolvedSelectedDatasetId()
        if (selectedId > 0 && cacheDatasetOptions) {
            for (var index = 0; index < cacheDatasetOptions.length; index++) {
                var option = cacheDatasetOptions[index]
                if (option && positiveDatasetId(option.value) === selectedId && option.raw) {
                    return option.raw
                }
            }
        }

        if (cleanedDataController && cleanedDataController.selectedDatasetInfo) {
            return cleanedDataController.selectedDatasetInfo
        }

        return null
    }

    function currentCacheDateRange() {
        var datasetInfo = currentCacheDatasetInfo()
        var startDate = ""
        var endDate = ""

        if (datasetInfo) {
            startDate = datasetInfo.startDate || datasetInfo.beginDate || datasetInfo.firstTradeDate || ""
            endDate = datasetInfo.endDate || datasetInfo.lastTradeDate || ""
        }

        if (cleanedDataController) {
            if (!startDate && cleanedDataController.currentStartDate) {
                startDate = cleanedDataController.currentStartDate
            }
            if (!endDate && cleanedDataController.currentEndDate) {
                endDate = cleanedDataController.currentEndDate
            }
        }

        return {
            startDate: String(startDate || ""),
            endDate: String(endDate || "")
        }
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

    function currentCacheAvailableFields() {
        var datasetInfo = currentCacheDatasetInfo()
        var fields = []

        if (datasetInfo) {
            if (datasetInfo.availableFields) {
                fields = datasetInfo.availableFields
            } else if (datasetInfo.fields) {
                fields = datasetInfo.fields
            } else if (datasetInfo.fieldNames) {
                fields = datasetInfo.fieldNames
            }
        }

        if (!fields || fields.length === 0) {
            var diagnostics = currentCacheFieldDiagnostics()
            for (var key in diagnostics) {
                if (Object.prototype.hasOwnProperty.call(diagnostics, key)) {
                    fields.push(key)
                }
            }
        }

        return normalizeStringList(fields)
    }

    function currentCacheDatasetStockCodes() {
        var datasetInfo = currentCacheDatasetInfo()
        var symbols = []

        if (datasetInfo) {
            if (datasetInfo.stockCodes) {
                symbols = datasetInfo.stockCodes
            } else if (datasetInfo.symbols) {
                symbols = datasetInfo.symbols
            }
        }

        var normalized = []
        var seen = {}
        var values = normalizeStringList(symbols)
        for (var index = 0; index < values.length; index++) {
            var symbol = String(values[index] || "").trim().toUpperCase()
            if (!symbol || seen[symbol]) {
                continue
            }
            seen[symbol] = true
            normalized.push(symbol)
        }

        return normalized
    }

    function preflightCategoryMeta(category) {
        var normalizedCategory = category !== undefined && category !== null ? String(category).trim().toLowerCase() : ""
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

        if (normalizedCategory === "runtime-init-failed") {
            meta.statusText = "运行时初始化失败"
            meta.shortText = "运行时异常"
            meta.detail = "回测运行时没有初始化成功，本次无法判断因子支持性。"
            meta.accentColor = "#F87171"
            meta.chipBackground = "#3F1D24"
            meta.chipBorder = "#DC2626"
            meta.chipText = "#FECACA"
        } else if (normalizedCategory === "invalid-backtest-window") {
            meta.statusText = "回测窗口非法"
            meta.shortText = "窗口非法"
            meta.detail = "回测开始日期和结束日期必须同时提供，且不能使用隐式兜底窗口。"
            meta.accentColor = "#F87171"
            meta.chipBackground = "#3F1D24"
            meta.chipBorder = "#DC2626"
            meta.chipText = "#FECACA"
        } else if (normalizedCategory === "missing-resolved-symbols") {
            meta.statusText = "股票池为空"
            meta.shortText = "缺少股票池"
            meta.detail = "回测请求中缺少已解析股票池，无法进入因子运行阶段。"
            meta.accentColor = "#FB923C"
            meta.chipBackground = "#3F2A17"
            meta.chipBorder = "#EA580C"
            meta.chipText = "#FED7AA"
        } else if (normalizedCategory === "missing-selected-factors") {
            meta.statusText = "未选择因子"
            meta.shortText = "缺少因子"
            meta.detail = "回测请求中缺少选中因子列表，无法进入因子运行阶段。"
            meta.accentColor = "#FB923C"
            meta.chipBackground = "#3F2A17"
            meta.chipBorder = "#EA580C"
            meta.chipText = "#FED7AA"
        } else if (normalizedCategory === "instance-missing") {
            meta.statusText = "实例未解析"
            meta.shortText = "实例缺失"
            meta.detail = "没有找到可执行实例，请先检查因子实例同步状态。"
            meta.accentColor = "#F87171"
            meta.chipBackground = "#3F1D24"
            meta.chipBorder = "#DC2626"
            meta.chipText = "#FECACA"
        } else if (normalizedCategory === "unsupported-type") {
            meta.statusText = "因子类型未接入"
            meta.shortText = "类型未接入"
            meta.detail = "当前运行时还没有接入该因子类型的回测执行链路。"
            meta.accentColor = "#FB923C"
            meta.chipBackground = "#3F2A17"
            meta.chipBorder = "#EA580C"
            meta.chipText = "#FED7AA"
        } else if (normalizedCategory === "dataset-missing") {
            meta.statusText = "未选择缓存集"
            meta.shortText = "未选缓存集"
            meta.detail = "当前是缓存模式，但还没有选中可回测缓存集。"
            meta.accentColor = "#94A3B8"
            meta.chipBackground = "#1E293B"
            meta.chipBorder = "#475569"
            meta.chipText = "#CBD5E1"
        } else if (normalizedCategory === "dataset-invalid" || normalizedCategory === "dataset-empty") {
            meta.statusText = "缓存集异常"
            meta.shortText = "缓存异常"
            meta.detail = "当前缓存集缺少必要元数据，或者时间范围与内容不完整。"
            meta.accentColor = "#F59E0B"
            meta.chipBackground = "#3F2D16"
            meta.chipBorder = "#D97706"
            meta.chipText = "#FDE68A"
        } else if (normalizedCategory === "insufficient-history") {
            meta.statusText = "历史样本不足"
            meta.shortText = "样本不足"
            meta.detail = "结合预热窗口后，可用交易日仍不足以稳定计算该因子。"
            meta.accentColor = "#FACC15"
            meta.chipBackground = "#3F3518"
            meta.chipBorder = "#CA8A04"
            meta.chipText = "#FEF08A"
        } else if (normalizedCategory === "supported") {
            meta.statusText = "可执行"
            meta.shortText = "可执行"
            meta.detail = "当前已经通过统一支持校验，可以进入回测执行阶段。"
            meta.accentColor = "#22C55E"
            meta.chipBackground = "#133226"
            meta.chipBorder = "#16A34A"
            meta.chipText = "#BBF7D0"
        }

        return meta
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

    function preflightFailureDetailText(failure) {
        var meta = preflightCategoryMeta(failure && failure.category)
        var factorName = resolveFactorDisplayName(failure && failure.factorId ? failure.factorId : "")
        if (!factorName) {
            factorName = "该因子"
        }
        var typedTokens = []
        if (failure && failure.runFailureReason) {
            typedTokens.push("failureReason=" + failure.runFailureReason)
        }
        if (failure && failure.runErrorCode) {
            typedTokens.push("errorCode=" + failure.runErrorCode)
        }
        var tokenText = typedTokens.length > 0 ? "（" + typedTokens.join("，") + "）" : ""
        return factorName + "：" + meta.detail + tokenText
    }

    function buildPreflightFailureExportText(failures) {
        var lines = []
        var safeFailures = failures && failures.length ? failures : []
        var datasetInfo = currentCacheDatasetInfo()

        lines.push("AStockQuantEngine 因子组合回测预检失败诊断")
        lines.push("数据源模式: " + selectedDataSourceMode)
        if (datasetInfo) {
            lines.push("缓存集: #" + (positiveDatasetId(datasetInfo.id) || resolvedSelectedDatasetId()) + " " + (datasetInfo.displayName || datasetInfo.name || "未命名缓存集"))
        } else if (selectedDataSourceMode === "cache") {
            lines.push("缓存集: 未选择")
        }
        lines.push("选中因子数: " + selectedFactorIds.length)
        lines.push("失败因子数: " + safeFailures.length)
        lines.push("")

        for (var i = 0; i < safeFailures.length; i++) {
            var failure = safeFailures[i]
            if (!failure) {
                continue
            }

            var meta = preflightCategoryMeta(failure.category)
            lines.push("- factorId: " + (failure.factorId || ""))
            lines.push("  factorName: " + resolveFactorDisplayName(failure.factorId || ""))
            lines.push("  instanceId: " + (failure.instanceId || "未解析"))
            lines.push("  category: " + meta.statusText + " (" + (failure.category || "unknown") + ")")
            lines.push("  runFailureReason: " + (failure.runFailureReason || ""))
            lines.push("  runErrorCode: " + (failure.runErrorCode || ""))
            lines.push("  reason: " + (failure.reason || "未知预检失败"))
            lines.push("  detail: " + preflightFailureDetailText(failure))
            lines.push("")
        }

        return lines.join("\n").trim()
    }

    function buildFactorStockPoolComparisonText() {
        if (!selectedFactorIds || selectedFactorIds.length === 0) {
            return "先选择因子后再进入回测比较。"
        }

        if (!previousBacktestReport || Object.keys(previousBacktestReport).length === 0) {
            return "当前没有上一轮同因子回测基线，本次完成后会直接建立新的因子结果基线。"
        }

        return "当前比较仅关注因子指标、有效区间和预热裁剪信息，不再输出或比较股票池。"
    }

    function canStartSingleOrBatchBacktest() {
        if (isBacktesting || !factorBacktestController || !selectedFactorIds || selectedFactorIds.length === 0) {
            return false
        }

        if (selectedDataSourceMode !== "cache") {
            return false
        }

        if (!hasAvailableCacheDataset()) {
            return false
        }

        if (resolvedSelectedDatasetId() <= 0) {
            return false
        }

        var supportMap = factorBacktestController.factorSupportMapCache
        if (!supportMap) {
            return false
        }

        for (var i = 0; i < selectedFactorIds.length; i++) {
            var factorId = String(selectedFactorIds[i])
            var supportInfo = supportMap[factorId]
            if (!supportInfo) {
                return false
            }
            if (supportInfo.supported === false) {
                return false
            }
        }

        return true
    }

    function canStartCompositeBacktest() {
        if (isBacktesting || !factorBacktestController) {
            return false
        }

        if (selectedDataSourceMode !== "cache") {
            return false
        }

        if (!hasAvailableCacheDataset()) {
            return false
        }

        if (resolvedSelectedDatasetId() <= 0) {
            return false
        }

        if (!compositeChildAllocations || compositeChildAllocations.length < 2) {
            return false
        }

        var supportMap = factorBacktestController.factorSupportMapCache
        if (!supportMap) {
            return false
        }

        for (var i = 0; i < compositeChildAllocations.length; i++) {
            var child = compositeChildAllocations[i] || ({})
            var instanceId = String(child.instanceId || "").trim()
            var weight = Number(child.weight)
            if (!instanceId || !isFinite(weight) || weight <= 0) {
                return false
            }

            var supportInfo = supportMap[instanceId]
            if (!supportInfo || supportInfo.supported === false) {
                return false
            }
        }

        return isFinite(Number(compositeMinimumCoverageRatio))
                && Number(compositeMinimumCoverageRatio) > 0
                && Number(compositeMinimumCoverageRatio) <= 1
    }

    function canStartBacktest() {
        if (backtestEntryMode === 1) {
            return canStartCompositeBacktest()
        }
        return canStartSingleOrBatchBacktest()
    }

    function factorIdsForSupportCheck(includeAllFactors) {
        if (includeAllFactors === true) {
            return allFactorIdsForSupportCheck()
        }
        if (backtestEntryMode === 1) {
            return compositeChildIds()
        }
        return normalizeSelectedFactorIds(selectedFactorIds || [])
    }

    function normalizedSupportMapFactorIds(factorIds) {
        if (!factorIds || factorIds.length === 0) {
            return []
        }

        var seen = ({})
        var normalized = []
        for (var index = 0; index < factorIds.length; index++) {
            var factorId = String(factorIds[index] === undefined || factorIds[index] === null ? "" : factorIds[index]).trim()
            if (!factorId || seen[factorId] === true) {
                continue
            }
            seen[factorId] = true
            normalized.push(factorId)
        }

        normalized.sort()
        return normalized
    }

    function normalizedSupportMapScopeFingerprint(cacheSnapshot) {
        var snapshot = cacheSnapshot && typeof cacheSnapshot === "object" ? cacheSnapshot : ({})
        var payload = {
            dataSourceMode: String(selectedDataSourceMode || "cache"),
            selectedDatasetId: resolvedSelectedDatasetId(),
            startDate: String(snapshot.startDate || ""),
            endDate: String(snapshot.endDate || ""),
            availableFields: normalizedSupportMapFactorIds(snapshot.availableFields || []),
            tradeDateCount: Number(snapshot.tradeDateCount || 0),
            fieldDiagnostics: snapshot.fieldDiagnostics || ({})
        }
        return JSON.stringify(payload)
    }

    function factorIdListCoversFactorIds(coveredFactorIds, requestedFactorIds) {
        var covered = normalizedSupportMapFactorIds(coveredFactorIds)
        var requested = normalizedSupportMapFactorIds(requestedFactorIds)
        if (requested.length === 0) {
            return true
        }
        if (covered.length === 0) {
            return false
        }

        var coveredMap = ({})
        for (var index = 0; index < covered.length; index++) {
            coveredMap[covered[index]] = true
        }
        for (var requestIndex = 0; requestIndex < requested.length; requestIndex++) {
            if (coveredMap[requested[requestIndex]] !== true) {
                return false
            }
        }
        return true
    }

    function supportMapCoversFactorIds(supportMap, factorIds) {
        var requested = normalizedSupportMapFactorIds(factorIds)
        if (requested.length === 0) {
            return true
        }
        if (!supportMap || typeof supportMap !== "object") {
            return false
        }

        for (var index = 0; index < requested.length; index++) {
            if (supportMap[requested[index]] === undefined) {
                return false
            }
        }
        return true
    }

    function refreshFactorSupportMap(includeAllFactors) {
        supportMapRefreshAllFactorsRequested = includeAllFactors === true
        supportMapRefreshTimer.restart()
    }

    function rebuildFactorSupportMapNow() {
        if (!factorBacktestController) {
            root.factorSupportMapCache = ({});
            return
        }

        var resolvedDatasetId = resolvedSelectedDatasetId()
        if (resolvedDatasetId > 0 && factorBacktestController.selectedDatasetId !== resolvedDatasetId) {
            factorBacktestController.selectedDatasetId = resolvedDatasetId
        }
        factorBacktestController.dataSourceMode = selectedDataSourceMode

        var factorIds = allFactorIdsForSupportCheck()
        var cacheSnapshot = currentCacheSupportSnapshot()
        root.factorSupportMapCache = factorBacktestController.buildFactorSupportMap(
            factorIds,
            cacheSnapshot.startDate,
            cacheSnapshot.endDate,
            cacheSnapshot)
    }

    function runSupportMapRefresh() {
        console.log("因子支持校验开始")
        var resolvedDatasetId = resolvedSelectedDatasetId()
        if (resolvedDatasetId > 0 && factorBacktestController.selectedDatasetId !== resolvedDatasetId) {
            factorBacktestController.selectedDatasetId = resolvedDatasetId
        }
        factorBacktestController.dataSourceMode = selectedDataSourceMode

        var factorIds = factorIdsForSupportCheck(supportMapRefreshAllFactorsRequested)
        if (!factorIds || factorIds.length === 0) {
            root.factorSupportMapCache = ({})
            if (factorSelectorDialog) {
                factorSelectorDialog.supportMapLoading = false
                factorSelectorDialog.factorSupportMap = ({})
            }
            return
        }
        var cacheSnapshot = currentCacheSupportSnapshot()
        var scopeFingerprint = normalizedSupportMapScopeFingerprint(cacheSnapshot)

        if (scopeFingerprint === appliedSupportMapScopeFingerprint
                && supportMapCoversFactorIds(root.factorSupportMapCache, factorIds)) {
            console.log("因子支持校验命中当前缓存，跳过刷新")
            if (factorSelectorDialog) {
                factorSelectorDialog.supportMapLoading = false
                factorSelectorDialog.factorSupportMap = root.factorSupportMapCache
            }
            return
        }

        if (pendingSupportMapRequestId > 0
                && pendingSupportMapScopeFingerprint === scopeFingerprint
                && factorIdListCoversFactorIds(pendingSupportMapFactorIds, factorIds)) {
            console.log("因子支持校验请求进行中，跳过重复刷新")
            if (factorSelectorDialog) {
                factorSelectorDialog.supportMapLoading = true
            }
            return
        }

        root.factorSupportMapCache = ({})
        if (factorSelectorDialog) {
            factorSelectorDialog.supportMapLoading = true
            factorSelectorDialog.factorSupportMap = ({})
        }

        pendingSupportMapScopeFingerprint = scopeFingerprint
        pendingSupportMapFactorIds = normalizedSupportMapFactorIds(factorIds)
        pendingSupportMapRequestId = factorBacktestController.beginFactorSupportMapRefresh(
            factorIds,
            cacheSnapshot.startDate,
            cacheSnapshot.endDate,
            cacheSnapshot)
    }

    function currentCacheFactorSupportMap() {
        return factorBacktestController.factorSupportMapCache
    }

    function filterSelectedFactorsByCurrentCache() {
        if (selectedDataSourceMode !== "cache" || !selectedFactorIds || selectedFactorIds.length === 0) {
            return
        }

        var supportMap = currentCacheFactorSupportMap()
        var removedFactorNames = []

        for (var factorIndex = 0; factorIndex < selectedFactorIds.length; factorIndex++) {
            var factorId = String(selectedFactorIds[factorIndex])
            var supportInfo = supportMap ? supportMap[factorId] : null
            if (supportInfo && supportInfo.supported === false) {
                removedFactorNames.push(resolveFactorDisplayName(factorId))
            }
        }

        if (removedFactorNames.length > 0) {
            console.log("当前缓存不支持以下已选因子，但不会自动移除:", removedFactorNames.join(", "))
        }
    }

    function resolveFactorDisplayName(factorId) {
        if (!factorId) {
            return ""
        }

        var cacheKey = String(factorId)
        if (factorDisplayNameCache && Object.prototype.hasOwnProperty.call(factorDisplayNameCache, cacheKey)) {
            return factorDisplayNameCache[cacheKey]
        }

        if (factorService && factorService.getFactorById) {
            var factorInfo = factorService.getFactorById(cacheKey)
            if (factorInfo) {
                var resolvedName = factorInfo.displayName || factorInfo.factorName || factorInfo.name || cacheKey
                factorDisplayNameCache[cacheKey] = resolvedName
                factorDefinitionCache[cacheKey] = factorInfo
                return resolvedName
            }
        }

        factorDisplayNameCache[cacheKey] = cacheKey
        return cacheKey
    }

    function selectedFactorDisplayText() {
        if (backtestEntryMode === 1) {
            if (!compositeChildAllocations || compositeChildAllocations.length === 0) {
                return ""
            }

            var compositeNames = []
            for (var compositeIndex = 0; compositeIndex < compositeChildAllocations.length; compositeIndex++) {
                var compositeChild = compositeChildAllocations[compositeIndex] || ({})
                compositeNames.push(String(compositeChild.displayName || resolveFactorDisplayName(compositeChild.instanceId || "")))
            }

            if (compositeNames.length <= 3) {
                return compositeNames.join("、")
            }

            return compositeNames.slice(0, 3).join("、") + " 等 " + compositeNames.length + " 个子因子"
        }

        if (!selectedFactorIds || selectedFactorIds.length === 0) {
            return ""
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
        return factorBacktestController.normalizeFactorIds(factorIds || [])
    }

    function compositeChildIds() {
        var factorIds = []
        var seen = {}
        for (var i = 0; i < compositeChildAllocations.length; i++) {
            var instanceId = String((compositeChildAllocations[i] || {}).instanceId || "").trim()
            if (!instanceId || seen[instanceId]) {
                continue
            }
            seen[instanceId] = true
            factorIds.push(instanceId)
        }
        return factorIds
    }

    function normalizeCompositeChildAllocations(children) {
        var normalized = []
        var seen = {}
        var values = children || []

        for (var i = 0; i < values.length; i++) {
            var child = values[i] || ({})
            var instanceId = String(child.instanceId || child.factorId || "").trim()
            if (!instanceId || seen[instanceId]) {
                continue
            }

            seen[instanceId] = true
            var numericWeight = Number(child.weight)
            normalized.push({
                instanceId: instanceId,
                displayName: String(child.displayName || resolveFactorDisplayName(instanceId)),
                weight: isFinite(numericWeight) && numericWeight > 0 ? numericWeight : 0,
                ascending: child.ascending === undefined ? true : !!child.ascending,
                normalizeMode: child.normalizeMode === undefined ? 1 : Number(child.normalizeMode)
            })
        }

        return normalized
    }

    function setCompositeChildrenFromFactorIds(factorIds) {
        var existingById = ({})
        for (var existingIndex = 0; existingIndex < compositeChildAllocations.length; existingIndex++) {
            var existingChild = compositeChildAllocations[existingIndex] || ({})
            var existingId = String(existingChild.instanceId || "").trim()
            if (existingId) {
                existingById[existingId] = existingChild
            }
        }

        var nextChildren = []
        var normalizedIds = normalizeSelectedFactorIds(factorIds || [])
        for (var factorIndex = 0; factorIndex < normalizedIds.length; factorIndex++) {
            var instanceId = String(normalizedIds[factorIndex] || "").trim()
            if (!instanceId) {
                continue
            }
            var current = existingById[instanceId] || ({})
            nextChildren.push({
                instanceId: instanceId,
                displayName: String(current.displayName || resolveFactorDisplayName(instanceId)),
                weight: Number(current.weight || 0),
                ascending: current.ascending === undefined ? true : !!current.ascending,
                normalizeMode: current.normalizeMode === undefined ? 1 : Number(current.normalizeMode)
            })
        }

        compositeChildAllocations = normalizeCompositeChildAllocations(nextChildren)
        compositeDraftDirty = true
        if (compositeChildAllocations.some(function(item) { return !(Number(item.weight) > 0) })) {
            rebalanceCompositeChildWeights()
        }
    }

    function rebalanceCompositeChildWeights() {
        if (!compositeChildAllocations || compositeChildAllocations.length === 0) {
            return
        }

        var equalWeight = 1 / compositeChildAllocations.length
        var nextChildren = []
        for (var i = 0; i < compositeChildAllocations.length; i++) {
            var child = compositeChildAllocations[i] || ({})
            nextChildren.push({
                instanceId: String(child.instanceId || ""),
                displayName: String(child.displayName || resolveFactorDisplayName(child.instanceId || "")),
                weight: Number(equalWeight.toFixed(4)),
                ascending: child.ascending === undefined ? true : !!child.ascending,
                normalizeMode: child.normalizeMode === undefined ? 1 : Number(child.normalizeMode)
            })
        }
        compositeChildAllocations = normalizeCompositeChildAllocations(nextChildren)
        compositeDraftDirty = true
    }

    function removeCompositeChild(instanceId) {
        var nextChildren = []
        for (var i = 0; i < compositeChildAllocations.length; i++) {
            var child = compositeChildAllocations[i] || ({})
            if (String(child.instanceId || "") !== String(instanceId || "")) {
                nextChildren.push(child)
            }
        }
        compositeChildAllocations = normalizeCompositeChildAllocations(nextChildren)
        compositeDraftDirty = true
    }

    function updateCompositeChildWeight(instanceId, rawWeight) {
        var parsedWeight = Number(String(rawWeight || "").replace(/%/g, "").trim())
        var nextChildren = []
        for (var i = 0; i < compositeChildAllocations.length; i++) {
            var child = compositeChildAllocations[i] || ({})
            var nextChild = {
                instanceId: String(child.instanceId || ""),
                displayName: String(child.displayName || resolveFactorDisplayName(child.instanceId || "")),
                weight: Number(child.weight || 0),
                ascending: child.ascending === undefined ? true : !!child.ascending,
                normalizeMode: child.normalizeMode === undefined ? 1 : Number(child.normalizeMode)
            }
            if (String(nextChild.instanceId) === String(instanceId || "")) {
                nextChild.weight = isFinite(parsedWeight) ? parsedWeight : 0
            }
            nextChildren.push(nextChild)
        }
        compositeChildAllocations = normalizeCompositeChildAllocations(nextChildren)
        compositeDraftDirty = true
    }

    function updateCompositeChildAscending(instanceId, ascending) {
        var nextChildren = []
        for (var i = 0; i < compositeChildAllocations.length; i++) {
            var child = compositeChildAllocations[i] || ({})
            var nextChild = Object.assign({}, child)
            if (String(nextChild.instanceId || "") === String(instanceId || "")) {
                nextChild.ascending = !!ascending
            }
            nextChildren.push(nextChild)
        }
        compositeChildAllocations = normalizeCompositeChildAllocations(nextChildren)
        compositeDraftDirty = true
    }

    function updateCompositeChildNormalizeMode(instanceId, normalizeMode) {
        var nextChildren = []
        for (var i = 0; i < compositeChildAllocations.length; i++) {
            var child = compositeChildAllocations[i] || ({})
            var nextChild = Object.assign({}, child)
            if (String(nextChild.instanceId || "") === String(instanceId || "")) {
                nextChild.normalizeMode = Number(normalizeMode)
            }
            nextChildren.push(nextChild)
        }
        compositeChildAllocations = normalizeCompositeChildAllocations(nextChildren)
        compositeDraftDirty = true
    }

    function buildCompositeDraft() {
        return {
            name: compositeDraftName && String(compositeDraftName).trim().length > 0
                  ? String(compositeDraftName).trim()
                  : "composite_factor_draft",
            combineMode: Number(compositeCombineMode),
            missingPolicy: Number(compositeMissingPolicy),
            minimumCoverageRatio: Number(compositeMinimumCoverageRatio),
            children: compositeChildAllocations.map(function(child) {
                return {
                    instanceId: String(child.instanceId || ""),
                    displayName: String(child.displayName || ""),
                    weight: Number(child.weight),
                    ascending: !!child.ascending,
                    normalizeMode: Number(child.normalizeMode)
                }
            })
        }
    }

    function setSelectedFactors(factorIds) {
        selectedFactorIds = normalizeSelectedFactorIds(factorIds)
    }

    function hasMetricValue(value) {
        return value !== undefined && value !== null
    }

    function hasNumericMetricValue(value) {
        if (!hasMetricValue(value)) {
            return false
        }

        var numericValue = Number(value)
        return isFinite(numericValue)
    }

    function hasCompletedBacktestResult(result) {
        var target = result || displayedBacktestResult || backtestResult || ({})
        var status = String(target.status || "").trim().toUpperCase()
        return status === "SUCCESS" || status === "PARTIAL"
    }

    function shouldShowMetricPlaceholder() {
        return !selectedFactorIds || selectedFactorIds.length === 0
    }

    function formatMetric(value, digits, allowPlaceholder) {
        var placeholder = allowPlaceholder === undefined ? shouldShowMetricPlaceholder() : allowPlaceholder
        if (!hasNumericMetricValue(value)) {
            return placeholder ? "N/A" : Number(0).toFixed(digits)
        }
        return Number(value).toFixed(digits)
    }

    function formatPercentMetric(value, digits, allowPlaceholder) {
        var placeholder = allowPlaceholder === undefined ? shouldShowMetricPlaceholder() : allowPlaceholder
        if (!hasNumericMetricValue(value)) {
            return placeholder ? "N/A" : (Number(0) * 100).toFixed(digits) + "%"
        }
        return (Number(value) * 100).toFixed(digits) + "%"
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

    function currentTradingPreview() {
        var result = currentDisplayedBacktestResult() || ({})
        var diagnostics = result && result.diagnostics ? result.diagnostics : ({})
        var preview = diagnostics && diagnostics.tradingPreview ? diagnostics.tradingPreview : ({})
        return preview && typeof preview === "object" ? preview : ({})
    }

    function hasTradingPreview() {
        return Object.keys(currentTradingPreview()).length > 0
    }

    function tradingPreviewStatus() {
        return String(currentTradingPreview().status || "").trim().toLowerCase()
    }

    function tradingPreviewStatusLabel(status) {
        switch (String(status || "").trim().toLowerCase()) {
        case "pass":
            return "通过"
        case "warn":
            return "预警"
        case "blocked":
            return "阻断"
        case "force_reduce":
            return "强制减仓"
        case "trading_halt":
            return "停牌"
        case "no_order_plan":
            return "无委托计划"
        case "invalid_backtest_result":
            return "结果无效"
        case "missing_factor_snapshot":
            return "缺少因子截面"
        case "invalid_batch":
            return "交易批次无效"
        case "invalid_context":
            return "执行上下文无效"
        default:
            return "未生成"
        }
    }

    function tradingPreviewAccentColor(status) {
        switch (String(status || "").trim().toLowerCase()) {
        case "pass":
            return "#10B981"
        case "warn":
        case "no_order_plan":
            return "#F59E0B"
        case "blocked":
        case "force_reduce":
        case "trading_halt":
        case "invalid_backtest_result":
        case "missing_factor_snapshot":
        case "invalid_batch":
        case "invalid_context":
            return "#EF4444"
        default:
            return "#64748B"
        }
    }

    function tradingPreviewSecondaryMessage() {
        var preview = currentTradingPreview()
        var message = String(preview.message || "").trim()
        if (message.length > 0) {
            return message
        }

        switch (tradingPreviewStatus()) {
        case "pass":
            return "统一交易预执行已生成订单计划，可用来核对研究结果和执行落地之间的差异。"
        case "warn":
            return "统一交易预执行已运行，但存在需要人工关注的执行侧诊断。"
        case "blocked":
        case "force_reduce":
        case "trading_halt":
            return "统一交易预执行被风险链拦截，当前结果不应直接视为可执行订单。"
        case "no_order_plan":
            return "统一交易预执行没有形成委托计划，请检查权重、价格和资金约束。"
        case "missing_factor_snapshot":
            return "当前结果缺少最后一帧有效因子截面，无法生成统一交易预执行。"
        case "invalid_context":
        case "invalid_batch":
        case "invalid_backtest_result":
            return "当前结果的执行输入不完整，无法生成统一交易预执行。"
        default:
            return "当前结果未包含统一交易预执行诊断。"
        }
    }

    function tradingPreviewCountText(value) {
        if (!hasNumericMetricValue(value)) {
            return "0"
        }
        return String(Math.max(0, Math.round(Number(value))))
    }

    function currentFormalTradingExecution() {
        var result = currentDisplayedBacktestResult() || ({})
        var formal = result && result.formalTrading ? result.formalTrading : ({})
        return formal && typeof formal === "object" ? formal : ({})
    }

    function hasFormalTradingExecution() {
        return Object.keys(currentFormalTradingExecution()).length > 0
    }

    function formalTradingStatus() {
        return String(currentFormalTradingExecution().status || "").trim().toUpperCase()
    }

    function formalTradingStatusLabel(status) {
        switch (String(status || "").trim().toUpperCase()) {
        case "SUCCESS":
            return "已完成"
        case "PARTIAL":
            return "部分完成"
        case "FAILED":
            return "执行失败"
        case "NOT_RUN":
            return "未执行"
        default:
            return "未生成"
        }
    }

    function formalTradingAccentColor(status) {
        switch (String(status || "").trim().toUpperCase()) {
        case "SUCCESS":
            return "#10B981"
        case "PARTIAL":
            return "#F59E0B"
        case "FAILED":
            return "#EF4444"
        case "NOT_RUN":
            return "#64748B"
        default:
            return "#64748B"
        }
    }

    function formalTradingSecondaryMessage() {
        var formal = currentFormalTradingExecution()
        var message = String(formal.message || "").trim()
        if (message.length > 0) {
            return message
        }

        switch (formalTradingStatus()) {
        case "SUCCESS":
            return "正式统一交易已生成账户资金曲线与成交轨迹，可直接核对执行口径结果。"
        case "PARTIAL":
            return "正式统一交易已运行，但部分调仓被风险规则阻断，需要结合风险侧结果一起看。"
        case "FAILED":
            return "正式统一交易执行失败，请优先检查初始资金与交易执行上下文。"
        case "NOT_RUN":
            return "当前结果未执行正式统一交易回放。"
        default:
            return "当前结果未包含正式统一交易输出。"
        }
    }

    function formatAssetMetric(value) {
        if (!hasNumericMetricValue(value)) {
            return "0.00"
        }

        var numericValue = Number(value)
        var absoluteValue = Math.abs(numericValue)
        if (absoluteValue >= 100000000) {
            return (numericValue / 100000000).toFixed(2) + "亿"
        }
        if (absoluteValue >= 10000) {
            return (numericValue / 10000).toFixed(2) + "万"
        }
        return numericValue.toFixed(2)
    }

    function formatOptionalAssetMetric(value) {
        return hasNumericMetricValue(value) ? formatAssetMetric(value) : "N/A"
    }

    function formalTradingNumericSeries(values) {
        var numericSeries = []
        var normalizedValues = normalizedListValue(values)
        for (var index = 0; index < normalizedValues.length; index++) {
            if (!hasNumericMetricValue(normalizedValues[index])) {
                continue
            }
            numericSeries.push(Number(normalizedValues[index]))
        }
        return numericSeries
    }

    function formalTradingCurveSeries() {
        return formalTradingNumericSeries(currentFormalTradingExecution().totalAssetSeries)
    }

    function hasFormalTradingCurve() {
        return formalTradingCurveSeries().length > 1
    }

    function formalTradingCurveMinValue() {
        var series = formalTradingCurveSeries()
        if (series.length === 0) {
            return 0
        }

        var minValue = Math.min.apply(null, series)
        var maxValue = Math.max.apply(null, series)
        var padding = minValue === maxValue
            ? Math.max(1, Math.abs(maxValue) * 0.05)
            : Math.abs(maxValue - minValue) * 0.08
        return minValue - padding
    }

    function formalTradingCurveMaxValue() {
        var series = formalTradingCurveSeries()
        if (series.length === 0) {
            return 1
        }

        var minValue = Math.min.apply(null, series)
        var maxValue = Math.max.apply(null, series)
        var padding = minValue === maxValue
            ? Math.max(1, Math.abs(maxValue) * 0.05)
            : Math.abs(maxValue - minValue) * 0.08
        return maxValue + padding
    }

    function formalTradingCurvePointX(index, count, width, leftPadding, rightPadding) {
        var drawableWidth = Math.max(1, width - leftPadding - rightPadding)
        if (count <= 1) {
            return leftPadding + drawableWidth / 2
        }
        return leftPadding + drawableWidth * (index / (count - 1))
    }

    function formalTradingCurvePointY(value, minValue, maxValue, height, topPadding, bottomPadding) {
        var drawableHeight = Math.max(1, height - topPadding - bottomPadding)
        var range = Math.max(1e-9, maxValue - minValue)
        return topPadding + (maxValue - value) / range * drawableHeight
    }

    function formalTradingCurveNearestIndex(mouseX, canvasWidth) {
        var series = formalTradingCurveSeries()
        if (series.length === 0) {
            return -1
        }

        var leftPadding = 4
        var rightPadding = 4
        var drawableWidth = Math.max(1, canvasWidth - leftPadding - rightPadding)
        if (series.length === 1) {
            return 0
        }

        var clampedX = Math.max(leftPadding, Math.min(canvasWidth - rightPadding, mouseX))
        var ratio = (clampedX - leftPadding) / drawableWidth
        return Math.max(0, Math.min(series.length - 1, Math.round(ratio * (series.length - 1))))
    }

    function formalTradingCurveTooltipText(index) {
        var series = formalTradingCurveSeries()
        if (index < 0 || index >= series.length) {
            return ""
        }

        var formal = currentFormalTradingExecution()
        var executionDates = normalizedListValue(formal.executionDates)
        var cashSeries = normalizedListValue(formal.cashSeries)
        var marketValueSeries = normalizedListValue(formal.marketValueSeries)
        var dateLabel = executionDates.length > index ? String(executionDates[index]) : ("执行点 " + String(index + 1))
        var text = dateLabel + "\n总资产: " + formatAssetMetric(series[index])

        if (cashSeries.length > index && hasNumericMetricValue(cashSeries[index])) {
            text += "\n现金: " + formatAssetMetric(cashSeries[index])
        }
        if (marketValueSeries.length > index && hasNumericMetricValue(marketValueSeries[index])) {
            text += "\n持仓市值: " + formatAssetMetric(marketValueSeries[index])
        }

        return text
    }

    function formalTradingDetailRows() {
        var formal = currentFormalTradingExecution()
        var dates = normalizedListValue(formal.executionDates)
        var cashSeries = normalizedListValue(formal.cashSeries)
        var marketValueSeries = normalizedListValue(formal.marketValueSeries)
        var totalAssetSeries = normalizedListValue(formal.totalAssetSeries)
        var rowCount = Math.max(dates.length, cashSeries.length, marketValueSeries.length, totalAssetSeries.length)
        var rows = []

        for (var index = rowCount - 1; index >= 0; index--) {
            rows.push({
                date: index < dates.length ? String(dates[index]) : "--",
                cash: index < cashSeries.length && hasNumericMetricValue(cashSeries[index]) ? Number(cashSeries[index]) : undefined,
                marketValue: index < marketValueSeries.length && hasNumericMetricValue(marketValueSeries[index]) ? Number(marketValueSeries[index]) : undefined,
                totalAsset: index < totalAssetSeries.length && hasNumericMetricValue(totalAssetSeries[index]) ? Number(totalAssetSeries[index]) : undefined
            })
        }

        return rows
    }

    function formalTradingDetailPanelHeight() {
        if (!hasFormalTradingExecution() || !formalTradingDetailsExpanded) {
            return 0
        }

        var rowCount = formalTradingDetailRows().length
        if (rowCount <= 0) {
            return 0
        }

        return Math.min(240, 74 + rowCount * 34)
    }

    function executionDiagnosticsRowHeight() {
        if (!hasCompletedBacktestResult(currentDisplayedBacktestResult())) {
            return 0
        }

        var cardCount = 0
        if (hasTradingPreview()) {
            cardCount += 1
        }
        if (hasFormalTradingExecution()) {
            cardCount += 1
        }

        if (cardCount <= 0) {
            return 0
        }
        if (hasFormalTradingExecution()) {
            return 272
        }
        return 158
    }

    function coreRatingLabel(value, fallbackLabel) {
        var resolvedLabel = formatTextMetric(fallbackLabel, "")
        if (resolvedLabel.length > 0) {
            return resolvedLabel
        }

        var numeric = Number(value)
        if (!isFinite(numeric)) {
            numeric = 0
        }

        switch (numeric) {
        case 3:
            return "优秀"
        case 2:
            return "良好"
        case 1:
            return "合格"
        default:
            return "不合格"
        }
    }

    function coreRatingColor(value) {
        var numeric = Number(value)
        if (!isFinite(numeric)) {
            numeric = 0
        }

        switch (numeric) {
        case 3:
            return "#10B981"
        case 2:
            return "#38BDF8"
        case 1:
            return "#F59E0B"
        default:
            return "#EF4444"
        }
    }

    function returnMetricColor(value) {
        var numericValue = hasNumericMetricValue(value) ? Number(value) : 0
        if (numericValue > 0) {
            return "#EF4444"
        }
        if (numericValue < 0) {
            return "#10B981"
        }
        return "#94A3B8"
    }

    function returnMetricTrend(value) {
        var numericValue = hasNumericMetricValue(value) ? Number(value) : 0
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

        var cacheKey = String(factorId)
        if (factorDefinitionCache && Object.prototype.hasOwnProperty.call(factorDefinitionCache, cacheKey)) {
            return factorDefinitionCache[cacheKey]
        }

        var factorDefinition = factorService.getFactorById(cacheKey)
        if (factorDefinition) {
            factorDefinitionCache[cacheKey] = factorDefinition
        }
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

    function currentDisplayedBacktestResult() {
        return displayedBacktestResult && Object.keys(displayedBacktestResult).length > 0
            ? displayedBacktestResult
            : null
    }

    function resolvedRiskConfigurationState() {
        var appliedConfiguration = (riskConfigService && riskConfigService.appliedConfiguration
                                    && Object.keys(riskConfigService.appliedConfiguration).length > 0)
            ? riskConfigService.appliedConfiguration
            : ({})
        if ((!appliedConfiguration || Object.keys(appliedConfiguration).length === 0)
                && riskConfigService
                && typeof riskConfigService.loadAppliedConfiguration === "function") {
            appliedConfiguration = riskConfigService.loadAppliedConfiguration() || ({})
        }

        return factorBacktestController.resolveRiskConfigurationSnapshot(
            currentDisplayedBacktestResult() || ({}),
            appliedConfiguration,
            ({})
        )
    }

    function currentRiskConfigurationSnapshot() {
        var resolved = resolvedRiskConfigurationState()
        return resolved && resolved.snapshot ? resolved.snapshot : ({})
    }

    function currentRiskConfigurationSourceLabel() {
        var resolved = resolvedRiskConfigurationState()
        return resolved && resolved.sourceLabel ? resolved.sourceLabel : "未检测到已应用风控"
    }

    function riskConfigurationMetricCards() {
        return factorBacktestController.riskConfigMetricCards(currentRiskConfigurationSnapshot() || ({}))
    }

    function factorValidationState(factorId) {
        var factorName = resolveFactorDisplayName(factorId)
        var factorDefinition = factorDefinitionForValidation(factorId)
        var supportInfo = currentCacheFactorSupportMap()[String(factorId)] || ({})
        return factorBacktestController.factorValidationState(
            String(factorId || ""),
            factorName,
            !!factorDefinition,
            supportInfo,
            lastPreflightFailures,
            backtestResult || ({}),
            lastBacktestError || "",
            selectedFactorIds || [],
            selectedDataSourceMode,
            hasAvailableCacheDataset(),
            resolvedSelectedDatasetId()
        )
    }

    function clearDisplayedBacktestState() {
        root.applyDisplayedBacktestResult(null)
        root.currentGroup = 0
        root.totalGroups = 0
        root.selectedBacktestResultIndex = 0
    }

    function displayedBacktestResults() {
        return factorBacktestController.displayedBacktestResults(backtestResult || ({}))
    }

    function displayedBacktestResultName(entry) {
        return factorBacktestController.displayedBacktestResultName(entry || ({}))
    }

    function normalizedListValue(value) {
        if (!value) {
            return []
        }

        if (Array.isArray(value)) {
            return value
        }

        if (typeof value.length === "number") {
            var normalized = []
            for (var index = 0; index < value.length; index++) {
                normalized.push(value[index])
            }
            return normalized
        }

        return []
    }

    function stringifyLogValue(value) {
        try {
            return JSON.stringify(value)
        } catch (error) {
            return String(value)
        }
    }

    function applyDisplayedBacktestResult(result) {
        var rawBacktestResult = result && typeof result === "object" ? result : ({})
        var resultList = normalizedListValue(rawBacktestResult.results)

        var resolvedIndex = Number(root.selectedBacktestResultIndex)
        if (!isFinite(resolvedIndex) || resolvedIndex < 0) {
            resolvedIndex = 0
        }
        if (resultList.length > 0 && resolvedIndex >= resultList.length) {
            resolvedIndex = 0
        }

        var displayedResult = resultList.length > 0 ? resultList[resolvedIndex] : rawBacktestResult

        root.backtestResult = rawBacktestResult && typeof rawBacktestResult === "object" ? rawBacktestResult : ({})
        root.displayedBacktestResult = displayedResult && typeof displayedResult === "object" ? displayedResult : ({})
        root.resultMetrics = displayedResult && displayedResult.metrics ? displayedResult.metrics : ({})
        root.groupResults = normalizedListValue(root.resultMetrics && root.resultMetrics.groups ? root.resultMetrics.groups : [])
        root.icMetrics = root.resultMetrics && root.resultMetrics.ic ? root.resultMetrics.ic : ({})
        root.executionMetrics = root.resultMetrics && root.resultMetrics.execution ? root.resultMetrics.execution : ({})
        root.selectedBacktestResultIndex = resolvedIndex
        root.formalTradingDetailsExpanded = false
    }

    function buildSingleFactorRunEntry(result) {
        return factorBacktestController.buildSingleFactorRunEntry(
            result || ({}),
            selectedFactorDisplayText() || "单因子"
        )
    }

    function pushSingleFactorRunHistory(result) {
        singleFactorRunHistory = factorBacktestController.pushSingleFactorRunHistory(
            singleFactorRunHistory || [],
            result || ({}),
            singleFactorRunHistoryLimit,
            selectedFactorDisplayText() || "单因子"
        )
    }

    function formatRunTimestamp(value) {
        if (!value) {
            return "--"
        }
        return Qt.formatDateTime(new Date(value), "MM-dd hh:mm")
    }

    function backtestResultSwitchEntries() {
        var batchResults = displayedBacktestResults()
        if (batchResults && batchResults.length > 0) {
            return batchResults
        }

        if (singleFactorRunHistory && singleFactorRunHistory.length > 0) {
            return singleFactorRunHistory
        }

        var current = currentDisplayedBacktestResult()
        return current ? [current] : []
    }

    function usesBatchBacktestResultSwitching() {
        var batchResults = displayedBacktestResults()
        return batchResults && batchResults.length > 0
    }

    function backtestResultStatusLabel(status) {
        switch (String(status || "").trim().toUpperCase()) {
        case "SUCCESS":
            return "成功"
        case "PARTIAL":
            return "部分完成"
        case "FAILED":
            return "失败"
        case "RUNNING":
            return "运行中"
        default:
            return "待查看"
        }
    }

    function backtestResultStatusColor(status) {
        switch (String(status || "").trim().toUpperCase()) {
        case "SUCCESS":
            return "#10B981"
        case "PARTIAL":
            return "#F59E0B"
        case "FAILED":
            return "#EF4444"
        case "RUNNING":
            return "#3B82F6"
        default:
            return "#64748B"
        }
    }

    function backtestResultCardKey(entry) {
        var target = entry || ({})
        var taskId = String(target.taskId || "").trim()
        if (taskId.length > 0) {
            return taskId
        }

        var factorId = String(target.factorId || "").trim()
        var timestamp = String(target.timestamp || target.executionTime || "").trim()
        return factorId + "|" + timestamp
    }

    function backtestResultCardSelected(index, entry) {
        if (usesBatchBacktestResultSwitching()) {
            return Number(index) === Number(selectedBacktestResultIndex)
        }

        var current = currentDisplayedBacktestResult() || ({})
        return backtestResultCardKey(current) === backtestResultCardKey(entry)
    }

    function backtestResultCardSummary(entry) {
        var target = entry || ({})
        var metrics = target.metrics || ({})
        var execution = metrics.execution || ({})
        var factorQuality = metrics.factorQuality || ({})
        var parts = []

        if (hasNumericMetricValue(execution.annualReturn)) {
            parts.push("年化 " + formatPercentMetric(execution.annualReturn, 2, false))
        }
        if (hasNumericMetricValue(factorQuality.rankIcir)) {
            parts.push("ICIR " + formatMetric(factorQuality.rankIcir, 2, false))
        }
        if (hasNumericMetricValue(execution.maxDrawdown)) {
            parts.push("回撤 " + formatPercentMetric(execution.maxDrawdown, 2, false))
        }

        return parts.join(" · ")
    }

    function backtestResultCardMeta(entry) {
        var target = entry || ({})
        var config = target.config || ({})
        var metrics = target.metrics || ({})
        var parts = []

        if (target.horizonTag) {
            parts.push(String(target.horizonTag))
        } else if (hasNumericMetricValue(config.forwardDays)) {
            parts.push("持仓 " + String(Math.round(Number(config.forwardDays))) + " 天")
        }

        if (hasNumericMetricValue(config.rebalanceDays)) {
            parts.push("调仓 " + String(Math.round(Number(config.rebalanceDays))) + " 天")
        }

        var groups = normalizedListValue(metrics.groups)
        if (groups.length > 0) {
            parts.push(String(groups.length) + " 组")
        }

        return parts.join(" · ")
    }

    function backtestResultCardRatingText(entry) {
        var factorQuality = (entry && entry.metrics && entry.metrics.factorQuality) ? entry.metrics.factorQuality : ({})
        if (hasNumericMetricValue(factorQuality.coreRating) || String(factorQuality.coreRatingLabel || "").trim().length > 0) {
            return coreRatingLabel(factorQuality.coreRating, factorQuality.coreRatingLabel)
        }
        return "未评级"
    }

    function backtestResultCardRatingColor(entry) {
        var factorQuality = (entry && entry.metrics && entry.metrics.factorQuality) ? entry.metrics.factorQuality : ({})
        if (hasNumericMetricValue(factorQuality.coreRating) || String(factorQuality.coreRatingLabel || "").trim().length > 0) {
            return coreRatingColor(factorQuality.coreRating)
        }
        return "#94A3B8"
    }

    function selectBacktestResultCard(index, entry) {
        if (usesBatchBacktestResultSwitching()) {
            root.selectedBacktestResultIndex = index
            root.applyDisplayedBacktestResult(root.backtestResult)
            return
        }

        root.applyDisplayedBacktestResult(entry || null)
    }
    
    // ============ 属性 ============
    
    property Bridge.FactorService factorService: null
    property Bridge.CleanedDataController cleanedDataController: null
    
    // 因子选择相关属性 - 现在由C++控制器管理
    property int backtestEntryMode: 0 // 0=single_or_batch, 1=composite
    property var selectedFactorIds: []  // 支持多因子选择，与控制器同步
    property string selectedFactorId: ""
    property bool syncingSelectedFactorState: false
    property var compositeChildAllocations: []
    readonly property int compactCardSpacing: 10
    readonly property int selectedFactorCardMinWidth: 188
    readonly property int selectedFactorCardMaxWidth: 228
    readonly property int resultSwitchCardMinWidth: 188
    readonly property int resultSwitchCardMaxWidth: 228
    readonly property int compositeChildCardMinWidth: 248
    readonly property int compositeChildCardMaxWidth: 292
    readonly property var compositeCombineModeOptions: [
        { label: "加权平均" },
        { label: "加权求和" },
        { label: "排序平均" },
        { label: "投票合成" },
        { label: "取最大值" },
        { label: "取最小值" }
    ]
    readonly property var compositeMissingPolicyOptions: [
        { label: "丢弃标的" },
        { label: "权重重归一" },
        { label: "补中性值" },
        { label: "要求最小覆盖" }
    ]
    readonly property var compositeNormalizeModeOptions: [
        { label: "不标准化" },
        { label: "Z 分数" },
        { label: "排序值" },
        { label: "百分位" },
        { label: "缩尾 Z 分数" }
    ]
    property int compositeCombineMode: 0
    property int compositeMissingPolicy: 1
    property double compositeMinimumCoverageRatio: 0.5
    property string compositeDraftName: ""
    property bool compositeDraftDirty: false
    property var factorSupportMapCache: ({})
    property string appliedSupportMapScopeFingerprint: ""
    property string pendingSupportMapScopeFingerprint: ""
    property var pendingSupportMapFactorIds: []
    property int pendingSupportMapRequestId: 0
    property double lastDatasetRefreshAtMs: 0
    property bool supportMapRefreshAllFactorsRequested: false
    property string lastAutoBenchmarkSymbol: ""

    onSelectedFactorIdsChanged: {
        syncingSelectedFactorState = true
        selectedFactorId = selectedFactorIds && selectedFactorIds.length > 0 ? String(selectedFactorIds[0]) : ""
        syncingSelectedFactorState = false
        if (!root.isBacktesting) {
            root.clearDisplayedBacktestState()
            root.lastBacktestError = ""
            root.lastPreflightFailures = []
            root.activeRunFactorIds = []
            refreshFactorSupportMap(true)
        }
    }

    onSelectedCacheDatasetIdChanged: {
        if (!root.isBacktesting && factorIdsForSupportCheck(false).length > 0) {
            refreshFactorSupportMap(true)
        }
    }

    onFactorDefinitionRevisionChanged: {
        factorDefinitionCache = ({})
        factorDisplayNameCache = ({})
        if (!root.isBacktesting && factorIdsForSupportCheck(false).length > 0) {
            root.clearDisplayedBacktestState()
            root.lastBacktestError = ""
            root.lastPreflightFailures = []
            root.activeRunFactorIds = []
            refreshFactorSupportMap(true)
        }
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

    onBacktestEntryModeChanged: {
        if (!root.isBacktesting) {
            root.clearDisplayedBacktestState()
            root.lastBacktestError = ""
            root.lastPreflightFailures = []
            root.activeRunFactorIds = []
            refreshFactorSupportMap(true)
        }
    }

    function compactCardWidth(containerWidth, minWidth, maxWidth) {
        var availableWidth = Number(containerWidth)
        if (!isFinite(availableWidth) || availableWidth <= 0) {
            return minWidth
        }

        var twoColumnWidth = Math.floor((availableWidth - compactCardSpacing) / 2)
        var clampedWidth = Math.max(minWidth, Math.min(maxWidth, twoColumnWidth))
        return Math.min(availableWidth, clampedWidth)
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
        }
    }
    
    // 数据集模型 - 不再使用，由C++控制器自动处理缓存
    
    // 回测控制器 - 使用属性绑定
    Bridge.FactorBacktestController {
        id: factorBacktestController
        selectedStockPoolSymbols: currentCacheDatasetStockCodes()

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
        onResultMetricsChanged: {
            if (!controllerHasAggregatedResults()) {
                root.resultMetrics = factorBacktestController.resultMetrics || ({})
                root.groupResults = root.resultMetrics.groups || []
                root.icMetrics = root.resultMetrics.ic || ({})
                root.executionMetrics = root.resultMetrics.execution || ({})
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
            console.log("因子支持校验返回:", requestId)
            if (!factorBacktestController.handleFactorSupportMapReady(requestId, supportMap || ({}))) {
                return
            }

            if (requestId === root.pendingSupportMapRequestId) {
                root.appliedSupportMapScopeFingerprint = root.pendingSupportMapScopeFingerprint
            }
            root.pendingSupportMapRequestId = 0
            root.pendingSupportMapScopeFingerprint = ""
            root.pendingSupportMapFactorIds = []

            root.factorSupportMapCache = factorBacktestController.factorSupportMapCache
            if (factorSelectorDialog) {
                factorSelectorDialog.supportMapLoading = factorBacktestController.supportMapRequestInFlight
                factorSelectorDialog.factorSupportMap = root.factorSupportMapCache
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
            console.log("📊 最终 groupResults:", stringifyLogValue(root.groupResults))
            console.log("📊 最终 icMetrics:", stringifyLogValue(root.icMetrics))
            console.log("📊 最终 executionMetrics:", stringifyLogValue(root.executionMetrics))

            var analysisReport = result && typeof result === "object" ? Object.assign({}, result) : ({})
            var activeResult = currentDisplayedBacktestResult() || ({})
            var activeConfig = activeResult.config || ({})
            var activeFactorId = String(activeResult.factorId || activeConfig.factorId || "").trim()
            if (activeFactorId.length > 0) {
                analysisReport.activeAnalysisFactorId = activeFactorId
            }

            root.analysisReportRequested(analysisReport)
        }
        onBacktestFailed: function(error) {
            console.error("回测失败:", error)
            root.currentGroup = 0
            root.totalGroups = 0
            root.backtestResult = ({})
            root.resultMetrics = ({})
            root.groupResults = []
            root.icMetrics = ({})
            root.executionMetrics = ({})
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
    property var displayedBacktestResult: ({})
    property var resultMetrics: ({})
    property var groupResults: []
    property var icMetrics: ({})
    property var executionMetrics: ({})
    property string lastBacktestError: ""
    property var lastPreflightFailures: []
    property int selectedBacktestResultIndex: 0
    property bool formalTradingDetailsExpanded: false
    property var activeRunFactorIds: []
    property var singleFactorRunHistory: []
    property int singleFactorRunHistoryLimit: 3
    
    // 分组配置
    property var groupConfig: ({})
    
    // 数据源属性
    property string selectedDataSourceMode: "cache"
    property int selectedCacheDatasetId: -1
    property var cacheDatasetOptions: [{ text: "请选择缓存集", value: -1, raw: null }]
    property var riskConfigService: Bridge.RiskConfigService

    onSelectedDataSourceModeChanged: {
        if (selectedDataSourceMode !== "cache") {
            selectedDataSourceMode = "cache"
        }

        factorBacktestController.dataSourceMode = "cache"

        if (!root.isBacktesting && factorIdsForSupportCheck(false).length > 0) {
            refreshFactorSupportMap(true)
        }

        if (!hasAvailableCacheDataset()) {
            console.log("当前没有可用缓存集，回测功能暂不可用")
        }
    }

    function hasAvailableCacheDataset() {
        return !!(cacheDatasetOptions && cacheDatasetOptions.length > 1)
    }

    function setDataSourceMode(mode) {
        var normalizedMode = "cache"
        if (mode !== "cache") {
            console.log("因子回测仅支持缓存集模式，已忽略非缓存模式切换请求")
        }

        if (selectedDataSourceMode !== normalizedMode) {
            selectedDataSourceMode = normalizedMode
        }

        factorBacktestController.dataSourceMode = normalizedMode
    }

    function ensureUsableDataSourceMode() {
        if (selectedDataSourceMode !== "cache") {
            setDataSourceMode("cache")
        }
        if (!hasAvailableCacheDataset()) {
            console.log("当前没有可用缓存集，回测功能暂不可用")
        }
    }

    function cacheDatasetOptionText(index) {
        if (!cacheDatasetOptions || index < 0 || index >= cacheDatasetOptions.length) {
            return ""
        }

        var option = cacheDatasetOptions[index]
        return option && option.text ? option.text : ""
    }

    function selectedCacheDatasetText() {
        var selectedId = resolvedSelectedDatasetId()
        if (selectedId > 0 && cacheDatasetOptions) {
            for (var index = 0; index < cacheDatasetOptions.length; index++) {
                var option = cacheDatasetOptions[index]
                if (option && positiveDatasetId(option.value) === selectedId) {
                    return option.text ? option.text : ("#" + selectedId)
                }
            }
            return "#" + selectedId
        }

        return cacheDatasetOptions && cacheDatasetOptions.length > 1 ? "请选择缓存集" : "当前没有可用缓存集"
    }

    function datasetSelectableForBacktest(dataset) {
        if (!dataset) {
            return false
        }

        return factorBacktestController.datasetSelectableForBacktest(dataset)
    }

    function rebuildCacheDatasetOptions() {
        cacheDatasetOptions = factorBacktestController.buildBacktestDatasetOptions(
            cleanedDataController && cleanedDataController.datasetList ? cleanedDataController.datasetList : []
        )

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

        var targetId = resolvedSelectedDatasetId()

        for (var index = 0; index < options.length; index++) {
            if (options[index].value === targetId) {
                if (datasetComboBox.currentIndex !== index) {
                    datasetComboBox.currentIndex = index
                }
                return
            }
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

        var selectedId = positiveDatasetId(selected.value)
        if (selectedId <= 0) {
            selectedCacheDatasetId = -1
            factorBacktestController.selectedDatasetId = -1
            return
        }

        selectedCacheDatasetId = selectedId
        factorBacktestController.selectedDatasetId = selectedId
        syncSelectedDatasetBenchmarkMetadata()
        cleanedDataController.loadDatasetById(selectedId)
        console.log("回测页选择缓存集:", selectedId, selected.text)

        if (!root.isBacktesting && selectedFactorIds && selectedFactorIds.length > 0) {
            refreshFactorSupportMap(true)
        }
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: "因子回测"
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

                Rectangle {
                    id: backtestControlPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: controlPanelContent.implicitHeight + 32
                    radius: 12
                    color: "#1E293B"

                    ColumnLayout {
                        id: controlPanelContent
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 10
                            color: "#111827"
                            border.width: 1
                            border.color: "#243041"
                            implicitHeight: entryModeRow.implicitHeight + 20

                            RowLayout {
                                id: entryModeRow
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: 180
                                    Layout.preferredHeight: 34
                                    radius: 8
                                    color: backtestEntryMode === 0 ? "#2563EB" : "#172033"
                                    border.width: 1
                                    border.color: backtestEntryMode === 0 ? "#60A5FA" : "#334155"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "单因子 / 批量回测"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: backtestEntryMode === 0 ? "white" : "#CBD5E1"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backtestEntryMode = 0
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 180
                                    Layout.preferredHeight: 34
                                    radius: 8
                                    color: backtestEntryMode === 1 ? "#0F766E" : "#172033"
                                    border.width: 1
                                    border.color: backtestEntryMode === 1 ? "#2DD4BF" : "#334155"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "组合因子回测"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: backtestEntryMode === 1 ? "white" : "#CBD5E1"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backtestEntryMode = 1
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: backtestEntryMode === 1
                                        ? "模式: 单个组合因子实例回测"
                                        : "模式: 多个单因子独立批量回测"
                                    font.pixelSize: 11
                                    color: "#94A3B8"
                                }
                            }
                        }

                        // 因子选择区域
                        RowLayout {
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 160
                                Layout.preferredHeight: 40
                                radius: 8
                                color: backtestEntryMode === 1 ? "#0F766E" : "#3B82F6"

                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    Text {
                                        text: backtestEntryMode === 1 ? "🧩" : "📊"
                                        font.pixelSize: 14
                                        color: "white"
                                    }

                                    Text {
                                        text: backtestEntryMode === 1 ? "选择子因子" : "选择因子"
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

                            Rectangle {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 40
                                radius: 8
                                color: "#1F2937"
                                border.width: 1
                                border.color: "#334155"
                                visible: backtestEntryMode === 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "一键均权"
                                    font.pixelSize: 12
                                    color: (compositeChildAllocations && compositeChildAllocations.length > 0) ? "#E2E8F0" : "#64748B"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: compositeChildAllocations && compositeChildAllocations.length > 0
                                    onClicked: rebalanceCompositeChildWeights()
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: backtestEntryMode === 1
                                    ? (compositeChildAllocations.length > 0
                                       ? ("已选 " + compositeChildAllocations.length + " 个子因子")
                                       : "请选择要组合回测的子因子")
                                    : (selectedFactorIds.length > 0
                                       ? ("已选 " + selectedFactorIds.length + " 个因子")
                                       : "请选择要回测的因子")
                                font.pixelSize: 12
                                color: (backtestEntryMode === 1
                                        ? compositeChildAllocations.length > 0
                                        : selectedFactorIds.length > 0) ? "#38BDF8" : "#94A3B8"
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: selectedFactorsPanelContent.implicitHeight + 24
                            Layout.preferredHeight: implicitHeight
                            radius: 10
                            color: "#0F172A"
                            border.width: 1
                            border.color: "#1E293B"

                            ColumnLayout {
                                id: selectedFactorsPanelContent
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: backtestEntryMode === 1 ? "组合草稿与验证" : "验证状态"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "#F1F5F9"
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: backtestEntryMode === 1
                                            ? "流程: child 可执行性校验 -> 组合合同校验 -> 组合回测"
                                            : "流程: 可执行性校验 -> 回测效果校验"
                                        font.pixelSize: 10
                                        color: "#64748B"
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: backtestEntryMode === 1
                                        ? (compositeChildAllocations.length > 0
                                           ? ("当前组合草稿已选择 " + compositeChildAllocations.length + " 个子因子")
                                           : "当前组合草稿未选择子因子")
                                        : (selectedFactorIds.length > 0
                                           ? ("当前已选择 " + selectedFactorIds.length + " 个因子")
                                           : "当前未选择因子")
                                    font.pixelSize: 11
                                    color: ((backtestEntryMode === 1 ? compositeChildAllocations.length : selectedFactorIds.length) > 0) ? "#38BDF8" : "#64748B"
                                    wrapMode: Text.WordWrap
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    visible: backtestEntryMode === 0

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: selectedFactorsFlow.childrenRect.height
                                        visible: selectedFactorIds.length > 0

                                        Flow {
                                            id: selectedFactorsFlow
                                            width: parent.width
                                            spacing: root.compactCardSpacing

                                            Repeater {
                                                model: selectedFactorIds

                                                delegate: Rectangle {
                                                    width: root.compactCardWidth(
                                                               selectedFactorsFlow.width,
                                                               root.selectedFactorCardMinWidth,
                                                               root.selectedFactorCardMaxWidth)
                                                    radius: 8
                                                    color: "#111827"
                                                    border.width: 1
                                                    border.color: validationState.accentColor
                                                    implicitHeight: selectedFactorCardColumn.implicitHeight + 20

                                                    property var validationState: root.factorValidationState(modelData)

                                                    ColumnLayout {
                                                        id: selectedFactorCardColumn
                                                        anchors.fill: parent
                                                        anchors.margins: 10
                                                        anchors.rightMargin: 34
                                                        spacing: 5

                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            spacing: 6

                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: root.resolveFactorDisplayName(modelData)
                                                                font.pixelSize: 12
                                                                font.weight: Font.Medium
                                                                color: "#F1F5F9"
                                                                wrapMode: Text.WordWrap
                                                                maximumLineCount: 2
                                                            }

                                                            Text {
                                                                text: validationState.statusText
                                                                font.pixelSize: 10
                                                                color: validationState.accentColor
                                                                horizontalAlignment: Text.AlignRight
                                                            }
                                                        }

                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: "因子ID: " + String(modelData)
                                                            font.pixelSize: 10
                                                            color: "#94A3B8"
                                                            elide: Text.ElideRight
                                                        }

                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: validationState.reason
                                                            font.pixelSize: 10
                                                            color: "#94A3B8"
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 3
                                                        }
                                                    }

                                                    Rectangle {
                                                        anchors.top: parent.top
                                                        anchors.right: parent.right
                                                        anchors.topMargin: 8
                                                        anchors.rightMargin: 8
                                                        width: 20
                                                        height: 20
                                                        radius: 10
                                                        color: "#1F2937"
                                                        border.width: 1
                                                        border.color: "#334155"

                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: "×"
                                                            font.pixelSize: 13
                                                            font.weight: Font.DemiBold
                                                            color: "#94A3B8"
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: root.removeSelectedFactor(modelData)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Text {
                                        text: "选择后卡片会展示名称、状态和失败原因"
                                        font.pixelSize: 11
                                        color: "#64748B"
                                        visible: selectedFactorIds.length === 0
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    visible: backtestEntryMode === 1

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            Text {
                                                text: "组合名称"
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                            }

                                            TextField {
                                                Layout.fillWidth: true
                                                text: compositeDraftName
                                                placeholderText: "例如 quality_value_composite"
                                                color: "#F8FAFC"
                                                placeholderTextColor: "#64748B"
                                                selectByMouse: true
                                                background: Rectangle {
                                                    radius: 6
                                                    color: "#111827"
                                                    border.width: 1
                                                    border.color: "#334155"
                                                }
                                                onTextChanged: {
                                                    compositeDraftName = text
                                                    compositeDraftDirty = true
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.preferredWidth: 120
                                            spacing: 4

                                            Text {
                                                text: "组合模式"
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                            }

                                            ComboBox {
                                                Layout.fillWidth: true
                                                model: root.compositeCombineModeOptions
                                                textRole: "label"
                                                currentIndex: compositeCombineMode
                                                onActivated: function(index) {
                                                    compositeCombineMode = index
                                                    compositeDraftDirty = true
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.preferredWidth: 140
                                            spacing: 4

                                            Text {
                                                text: "缺失策略"
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                            }

                                            ComboBox {
                                                Layout.fillWidth: true
                                                model: root.compositeMissingPolicyOptions
                                                textRole: "label"
                                                currentIndex: compositeMissingPolicy
                                                onActivated: function(index) {
                                                    compositeMissingPolicy = index
                                                    compositeDraftDirty = true
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.preferredWidth: 120
                                            spacing: 4

                                            Text {
                                                text: "最小覆盖率"
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                            }

                                            TextField {
                                                Layout.fillWidth: true
                                                text: String(compositeMinimumCoverageRatio)
                                                color: "#F8FAFC"
                                                selectByMouse: true
                                                background: Rectangle {
                                                    radius: 6
                                                    color: "#111827"
                                                    border.width: 1
                                                    border.color: "#334155"
                                                }
                                                onEditingFinished: {
                                                    var parsedCoverage = Number(text)
                                                    compositeMinimumCoverageRatio = isFinite(parsedCoverage) ? parsedCoverage : 0.5
                                                    text = String(compositeMinimumCoverageRatio)
                                                    compositeDraftDirty = true
                                                }
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: compositeChildFlow.childrenRect.height
                                        visible: compositeChildAllocations.length > 0

                                        Flow {
                                            id: compositeChildFlow
                                            width: parent.width
                                            spacing: root.compactCardSpacing

                                            Repeater {
                                                model: compositeChildAllocations

                                                delegate: Rectangle {
                                                    width: root.compactCardWidth(
                                                               compositeChildFlow.width,
                                                               root.compositeChildCardMinWidth,
                                                               root.compositeChildCardMaxWidth)
                                                    radius: 8
                                                    color: "#111827"
                                                    border.width: 1
                                                    border.color: childSupport.supported === false ? "#DC2626" : "#0EA5E9"
                                                    implicitHeight: compositeChildColumn.implicitHeight + 18

                                                    property string childInstanceId: String((modelData || {}).instanceId || "")
                                                    property var childSupport: root.currentCacheFactorSupportMap()[childInstanceId] || ({})

                                                    ColumnLayout {
                                                        id: compositeChildColumn
                                                        anchors.fill: parent
                                                        anchors.margins: 10
                                                        spacing: 6

                                                        RowLayout {
                                                            Layout.fillWidth: true

                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: String((modelData || {}).displayName || root.resolveFactorDisplayName(childInstanceId))
                                                                font.pixelSize: 12
                                                                font.weight: Font.Medium
                                                                color: "#F8FAFC"
                                                                wrapMode: Text.WordWrap
                                                                maximumLineCount: 2
                                                            }

                                                            Text {
                                                                text: childSupport.supported === false ? "不可回测" : "已校验"
                                                                font.pixelSize: 10
                                                                color: childSupport.supported === false ? "#FCA5A5" : "#67E8F9"
                                                            }

                                                            Rectangle {
                                                                width: 20
                                                                height: 20
                                                                radius: 10
                                                                color: "#1F2937"
                                                                border.width: 1
                                                                border.color: "#334155"

                                                                Text {
                                                                    anchors.centerIn: parent
                                                                    text: "×"
                                                                    font.pixelSize: 13
                                                                    color: "#94A3B8"
                                                                }

                                                                MouseArea {
                                                                    anchors.fill: parent
                                                                    cursorShape: Qt.PointingHandCursor
                                                                    onClicked: root.removeCompositeChild(childInstanceId)
                                                                }
                                                            }
                                                        }

                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: "因子ID: " + childInstanceId
                                                            font.pixelSize: 10
                                                            color: "#94A3B8"
                                                            elide: Text.ElideRight
                                                        }

                                                        GridLayout {
                                                            Layout.fillWidth: true
                                                            columns: 2
                                                            columnSpacing: 8
                                                            rowSpacing: 6

                                                            ColumnLayout {
                                                                Layout.fillWidth: true
                                                                spacing: 4

                                                                Text {
                                                                    text: "权重"
                                                                    font.pixelSize: 10
                                                                    color: "#94A3B8"
                                                                }

                                                                TextField {
                                                                    Layout.fillWidth: true
                                                                    text: String(Number((modelData || {}).weight || 0))
                                                                    color: "#F8FAFC"
                                                                    selectByMouse: true
                                                                    background: Rectangle {
                                                                        radius: 6
                                                                        color: "#0F172A"
                                                                        border.width: 1
                                                                        border.color: "#334155"
                                                                    }
                                                                    onEditingFinished: root.updateCompositeChildWeight(childInstanceId, text)
                                                                }
                                                            }

                                                            ColumnLayout {
                                                                Layout.fillWidth: true
                                                                spacing: 4

                                                                Text {
                                                                    text: "方向"
                                                                    font.pixelSize: 10
                                                                    color: "#94A3B8"
                                                                }

                                                                ComboBox {
                                                                    Layout.fillWidth: true
                                                                    model: ["升序", "降序"]
                                                                    currentIndex: (modelData || {}).ascending === false ? 1 : 0
                                                                    onActivated: function(index) {
                                                                        root.updateCompositeChildAscending(childInstanceId, index === 0)
                                                                    }
                                                                }
                                                            }

                                                            ColumnLayout {
                                                                Layout.fillWidth: true
                                                                Layout.columnSpan: 2
                                                                spacing: 4

                                                                Text {
                                                                    text: "标准化"
                                                                    font.pixelSize: 10
                                                                    color: "#94A3B8"
                                                                }

                                                                ComboBox {
                                                                    Layout.fillWidth: true
                                                                    model: root.compositeNormalizeModeOptions
                                                                    textRole: "label"
                                                                    currentIndex: Number((modelData || {}).normalizeMode || 0)
                                                                    onActivated: function(index) {
                                                                        root.updateCompositeChildNormalizeMode(childInstanceId, index)
                                                                    }
                                                                }
                                                            }
                                                        }

                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: childSupport.reason ? String(childSupport.reason) : "子因子已加入组合草稿"
                                                            font.pixelSize: 10
                                                            color: childSupport.supported === false ? "#FCA5A5" : "#94A3B8"
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 3
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Text {
                                        text: "组合模式下，每个 child 必须显式设置 weight / ascending / normalizeMode。"
                                        font.pixelSize: 11
                                        color: "#64748B"
                                        visible: compositeChildAllocations.length === 0
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: backtestEntryMode === 1
                                        ? "组合模式要求所有 child 在同一缓存集和同一交易窗口上通过支持性检查。"
                                        : "目标阈值: 数据覆盖率 >= 90%, |IC| >= 0.02, IR >= 0.30, IC正率 >= 50%, 多空收益差 > 0"
                                    font.pixelSize: 10
                                    color: "#64748B"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        
                        // 回测配置
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Layout.alignment: Qt.AlignTop
                            
                            // 分组数量
                            ColumnLayout {
                                spacing: 4
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 96
                                Layout.minimumWidth: 96
                                
                                Text {
                                    text: "分组数量"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                ComboBox {
                                    id: groupComboBox
                                    Layout.preferredWidth: 96
                                    Layout.preferredHeight: 36
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

                                Text {
                                    text: "用于分层统计"
                                    font.pixelSize: 10
                                    color: "#64748B"
                                }
                            }

                            ColumnLayout {
                                spacing: 4
                                Layout.alignment: Qt.AlignTop
                                Layout.fillWidth: true

                                Text {
                                    text: "缓存集"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }

                                ComboBox {
                                    id: datasetComboBox
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 36
                                    model: cacheDatasetOptions
                                    textRole: "text"
                                    enabled: !isBacktesting
                                    opacity: enabled ? 1.0 : 0.45

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: enabled ? "#334155" : "#1E293B"
                                    }

                                    contentItem: Text {
                                        text: root.selectedCacheDatasetText()
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
                                            if (!isBacktesting) {
                                                root.selectCacheDatasetAt(index)
                                                datasetComboBox.popup.close()
                                            }
                                        }
                                    }

                                    onActivated: function(index) {
                                        if (!isBacktesting) {
                                            root.selectCacheDatasetAt(index)
                                        }
                                    }
                                }

                                Text {
                                    text: resolvedSelectedDatasetId() > 0 ? ("当前清洗缓存集: " + selectedCacheDatasetText()) : "请选择清洗缓存集"
                                    font.pixelSize: 10
                                    color: resolvedSelectedDatasetId() > 0 ? "#93C5FD" : "#64748B"
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    text: "因子回测仅支持清洗缓存集模式"
                                    font.pixelSize: 10
                                    color: "#64748B"
                                }
                            }

                            ColumnLayout {
                                spacing: 4
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 180
                                Layout.minimumWidth: 180

                                Text {
                                    text: "回测参数"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: "#E2E8F0"
                                }

                                Rectangle {
                                    Layout.preferredWidth: 112
                                    Layout.preferredHeight: 36
                                    radius: 6
                                    color: "#111827"
                                    border.width: 1
                                    border.color: "#334155"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "设置参数"
                                        font.pixelSize: 11
                                        color: "#E2E8F0"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: openRuntimeParamsDialog()
                                    }
                                }

                                Text {
                                    text: "持仓 / 调仓 / 费用 / 复权"
                                    font.pixelSize: 10
                                    color: "#94A3B8"
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Layout.topMargin: 2

                            Item { Layout.fillWidth: true }

                            // 回测按钮
                            Rectangle {
                                id: backtestButton
                                Layout.preferredWidth: 132
                                Layout.minimumWidth: 132
                                Layout.preferredHeight: 40
                                radius: 8
                                color: isBacktesting ? "#334155" : (canStartBacktest() ? "#3B82F6" : "#475569")

                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    Text {
                                        text: isBacktesting ? "⏸️" : "▶️"
                                        font.pixelSize: 14
                                        color: isBacktesting ? "#94A3B8" : (canStartBacktest() ? "white" : "#94A3B8")
                                    }

                                    Text {
                                        text: isBacktesting ? "回测中..." : (backtestEntryMode === 1 ? "开始组合回测" : "开始回测")
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: isBacktesting ? "#94A3B8" : (canStartBacktest() ? "white" : "#94A3B8")
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: canStartBacktest()
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
                                        text: "因子结果对比"
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
                                            { title: "上一轮基线", value: previousBacktestReport && Object.keys(previousBacktestReport).length > 0 ? "已存在" : "暂无", accent: "#38BDF8" },
                                            { title: "本轮结果", value: currentDisplayedBacktestResult() ? "已生成" : "待回测", accent: "#34D399" },
                                            { title: "比较维度", value: "指标/有效期", accent: "#F59E0B" }
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
                                                    text: modelData.value
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
                                    text: currentGroup > 0 ? "批次: " + currentGroup + "/" + totalGroups : ""
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
                                                text: (failure && failure.reason) || "Preflight failed"
                                                font.pixelSize: 11
                                                color: "#FECACA"
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }

                                Text {
                                    visible: lastPreflightFailures.length > 2
                                    text: "More failures: " + (lastPreflightFailures.length - 2)
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
                    Layout.preferredHeight: backtestResultSwitchPanelContent.implicitHeight + 24
                    visible: backtestResultSwitchEntries().length > 0
                    radius: 12
                    color: "#1E293B"

                    ColumnLayout {
                        id: backtestResultSwitchPanelContent
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "🧭 回测结果切换"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: "#F8FAFC"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "点击卡片刷新下方分组内容和交易回放"
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: resultSwitchFlow.childrenRect.height

                            Flow {
                                id: resultSwitchFlow
                                width: parent.width
                                spacing: root.compactCardSpacing

                                Repeater {
                                    model: backtestResultSwitchEntries()

                                    delegate: Rectangle {
                                        property var resultMetrics: modelData.metrics || ({})
                                        property var resultExecution: resultMetrics.execution || ({})
                                        property var resultFactorQuality: resultMetrics.factorQuality || ({})
                                        property bool hovered: resultCardMouse.containsMouse
                                        property bool selected: root.backtestResultCardSelected(index, modelData)
                                        property color accentColor: root.backtestResultStatusColor(modelData.status)

                                        width: root.compactCardWidth(
                                                   resultSwitchFlow.width,
                                                   root.resultSwitchCardMinWidth,
                                                   root.resultSwitchCardMaxWidth)
                                        radius: 8
                                        color: selected ? "#172554" : (hovered ? "#0F223F" : "#111827")
                                        border.width: 1
                                        border.color: selected ? "#60A5FA" : (hovered ? accentColor : "#334155")
                                        implicitHeight: resultSwitchCardColumn.implicitHeight + 20

                                        ColumnLayout {
                                            id: resultSwitchCardColumn
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 5

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: root.displayedBacktestResultName(modelData) || (modelData.factorName || "回测结果")
                                                    font.pixelSize: 12
                                                    font.weight: Font.Medium
                                                    color: "#F1F5F9"
                                                    wrapMode: Text.WordWrap
                                                    maximumLineCount: 2
                                                }

                                                Rectangle {
                                                    radius: 8
                                                    color: Qt.rgba(Qt.color(accentColor).r,
                                                                   Qt.color(accentColor).g,
                                                                   Qt.color(accentColor).b,
                                                                   0.18)
                                                    border.width: 1
                                                    border.color: accentColor
                                                    implicitWidth: resultStatusBadgeText.implicitWidth + 12
                                                    implicitHeight: 18

                                                    Text {
                                                        id: resultStatusBadgeText
                                                        anchors.centerIn: parent
                                                        text: root.backtestResultStatusLabel(modelData.status)
                                                        font.pixelSize: 10
                                                        font.weight: Font.DemiBold
                                                        color: accentColor
                                                    }
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.backtestResultCardMeta(modelData)
                                                font.pixelSize: 10
                                                color: "#94A3B8"
                                                elide: Text.ElideRight
                                                visible: text.length > 0
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.backtestResultCardSummary(modelData)
                                                font.pixelSize: 10
                                                color: "#CBD5E1"
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                visible: text.length > 0
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: "评级 " + root.backtestResultCardRatingText(modelData)
                                                    + " · 时间 " + root.formatRunTimestamp(modelData.timestamp)
                                                font.pixelSize: 10
                                                color: root.backtestResultCardRatingColor(modelData)
                                                elide: Text.ElideRight
                                            }
                                        }

                                        MouseArea {
                                            id: resultCardMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.selectBacktestResultCard(index, modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: executionDiagnosticsRowHeight()
                    visible: executionDiagnosticsRowHeight() > 0
                    spacing: 16

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: hasTradingPreview()
                        radius: 12
                        color: "#111827"
                        border.width: 1
                        border.color: tradingPreviewAccentColor(tradingPreviewStatus())

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: "统一交易预执行"
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                    }

                                    Text {
                                        text: "执行诊断，不参与研究指标评分"
                                        font.pixelSize: 11
                                        color: "#94A3B8"
                                    }
                                }

                                Rectangle {
                                    radius: 10
                                    color: Qt.rgba(Qt.color(tradingPreviewAccentColor(tradingPreviewStatus())).r,
                                                   Qt.color(tradingPreviewAccentColor(tradingPreviewStatus())).g,
                                                   Qt.color(tradingPreviewAccentColor(tradingPreviewStatus())).b,
                                                   0.16)
                                    border.width: 1
                                    border.color: tradingPreviewAccentColor(tradingPreviewStatus())
                                    implicitWidth: tradingPreviewStatusText.implicitWidth + 16
                                    implicitHeight: tradingPreviewStatusText.implicitHeight + 8

                                    Text {
                                        id: tradingPreviewStatusText
                                        anchors.centerIn: parent
                                        text: root.tradingPreviewStatusLabel(root.tradingPreviewStatus())
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        color: tradingPreviewAccentColor(tradingPreviewStatus())
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: tradingPreviewSecondaryMessage()
                                font.pixelSize: 12
                                color: "#CBD5E1"
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: hasTradingPreview()
                                spacing: 10

                                Repeater {
                                    model: [
                                        { label: "目标持仓", value: tradingPreviewCountText(currentTradingPreview().targetPositionCount) },
                                        { label: "委托计划", value: tradingPreviewCountText(currentTradingPreview().orderPlanCount) },
                                        { label: "已接受", value: tradingPreviewCountText(currentTradingPreview().acceptedOrderCount) },
                                        { label: "成交回报", value: tradingPreviewCountText(currentTradingPreview().fillCount) }
                                    ]

                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 48
                                        radius: 10
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#243041"

                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 2

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.value
                                                font.pixelSize: 15
                                                font.weight: Font.Bold
                                                color: "#F8FAFC"
                                            }

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.label
                                                font.pixelSize: 10
                                                color: "#94A3B8"
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: String(currentTradingPreview().riskReason || "").trim().length > 0
                                text: "风险原因: " + String(currentTradingPreview().riskReason || "").trim()
                                font.pixelSize: 11
                                color: tradingPreviewAccentColor(tradingPreviewStatus())
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: hasFormalTradingExecution()
                        radius: 12
                        color: "#111827"
                        border.width: 1
                        border.color: formalTradingAccentColor(formalTradingStatus())

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: "正式统一交易回放"
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                    }

                                    Text {
                                        text: "账户轨迹与成交结果，不参与研究指标评分"
                                        font.pixelSize: 11
                                        color: "#94A3B8"
                                    }
                                }

                                Rectangle {
                                    radius: 10
                                    color: Qt.rgba(Qt.color(formalTradingAccentColor(formalTradingStatus())).r,
                                                   Qt.color(formalTradingAccentColor(formalTradingStatus())).g,
                                                   Qt.color(formalTradingAccentColor(formalTradingStatus())).b,
                                                   0.16)
                                    border.width: 1
                                    border.color: formalTradingAccentColor(formalTradingStatus())
                                    implicitWidth: formalTradingStatusText.implicitWidth + 16
                                    implicitHeight: formalTradingStatusText.implicitHeight + 8

                                    Text {
                                        id: formalTradingStatusText
                                        anchors.centerIn: parent
                                        text: root.formalTradingStatusLabel(root.formalTradingStatus())
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        color: formalTradingAccentColor(formalTradingStatus())
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: formalTradingSecondaryMessage()
                                font.pixelSize: 12
                                color: "#CBD5E1"
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Repeater {
                                    model: [
                                        { label: "计划调仓", value: tradingPreviewCountText(currentFormalTradingExecution().scheduledRebalanceCount) },
                                        { label: "实际调仓", value: tradingPreviewCountText(currentFormalTradingExecution().executedRebalanceCount) },
                                        { label: "风险阻断", value: tradingPreviewCountText(currentFormalTradingExecution().blockedRebalanceCount) },
                                        { label: "成交回报", value: tradingPreviewCountText(currentFormalTradingExecution().fillCount) }
                                    ]

                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 48
                                        radius: 10
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#243041"

                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 2

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.value
                                                font.pixelSize: 15
                                                font.weight: Font.Bold
                                                color: "#F8FAFC"
                                            }

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.label
                                                font.pixelSize: 10
                                                color: "#94A3B8"
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                id: formalTradingCurvePanel
                                Layout.fillWidth: true
                                Layout.preferredHeight: 78
                                radius: 10
                                color: "#0F172A"
                                border.width: 1
                                border.color: "#243041"
                                property int hoveredCurveIndex: -1
                                property bool curveTooltipVisible: false
                                property string curveTooltipText: ""
                                property real curveTooltipX: 0
                                property real curveTooltipY: 0

                                Canvas {
                                    id: formalTradingCurveCanvas
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    antialiasing: true

                                    onPaint: {
                                        var context = getContext("2d")
                                        context.clearRect(0, 0, width, height)

                                        var series = root.formalTradingCurveSeries()
                                        if (series.length === 0) {
                                            context.fillStyle = "#94A3B8"
                                            context.font = "11px sans-serif"
                                            context.fillText("暂无总资产曲线", 8, 18)
                                            return
                                        }

                                        var minValue = root.formalTradingCurveMinValue()
                                        var maxValue = root.formalTradingCurveMaxValue()
                                        var leftPadding = 4
                                        var rightPadding = 4
                                        var topPadding = 16
                                        var bottomPadding = 6
                                        var startValue = series[0]
                                        var baselineY = root.formalTradingCurvePointY(startValue,
                                                                                       minValue,
                                                                                       maxValue,
                                                                                       height,
                                                                                       topPadding,
                                                                                       bottomPadding)

                                        context.strokeStyle = "#334155"
                                        context.lineWidth = 1
                                        context.beginPath()
                                        context.moveTo(leftPadding, baselineY)
                                        context.lineTo(width - rightPadding, baselineY)
                                        context.stroke()

                                        context.strokeStyle = root.formalTradingAccentColor(root.formalTradingStatus())
                                        context.lineWidth = 2
                                        context.beginPath()
                                        for (var pointIndex = 0; pointIndex < series.length; pointIndex++) {
                                            var x = root.formalTradingCurvePointX(pointIndex,
                                                                                  series.length,
                                                                                  width,
                                                                                  leftPadding,
                                                                                  rightPadding)
                                            var y = root.formalTradingCurvePointY(series[pointIndex],
                                                                                  minValue,
                                                                                  maxValue,
                                                                                  height,
                                                                                  topPadding,
                                                                                  bottomPadding)
                                            if (pointIndex === 0) {
                                                context.moveTo(x, y)
                                            } else {
                                                context.lineTo(x, y)
                                            }
                                        }
                                        context.stroke()

                                        if (formalTradingCurvePanel.hoveredCurveIndex >= 0
                                                && formalTradingCurvePanel.hoveredCurveIndex < series.length) {
                                            var highlightIndex = formalTradingCurvePanel.hoveredCurveIndex
                                            var highlightX = root.formalTradingCurvePointX(highlightIndex,
                                                                                           series.length,
                                                                                           width,
                                                                                           leftPadding,
                                                                                           rightPadding)
                                            var highlightY = root.formalTradingCurvePointY(series[highlightIndex],
                                                                                           minValue,
                                                                                           maxValue,
                                                                                           height,
                                                                                           topPadding,
                                                                                           bottomPadding)

                                            context.strokeStyle = Qt.rgba(Qt.color(root.formalTradingAccentColor(root.formalTradingStatus())).r,
                                                                          Qt.color(root.formalTradingAccentColor(root.formalTradingStatus())).g,
                                                                          Qt.color(root.formalTradingAccentColor(root.formalTradingStatus())).b,
                                                                          0.32)
                                            context.lineWidth = 1
                                            context.beginPath()
                                            context.moveTo(highlightX, topPadding)
                                            context.lineTo(highlightX, height - bottomPadding)
                                            context.stroke()

                                            context.fillStyle = "#F8FAFC"
                                            context.beginPath()
                                            context.arc(highlightX, highlightY, 3.5, 0, Math.PI * 2)
                                            context.fill()

                                            context.strokeStyle = root.formalTradingAccentColor(root.formalTradingStatus())
                                            context.lineWidth = 1.5
                                            context.beginPath()
                                            context.arc(highlightX, highlightY, 5.5, 0, Math.PI * 2)
                                            context.stroke()
                                        }
                                    }

                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                    Connections {
                                        target: root
                                        function onDisplayedBacktestResultChanged() { formalTradingCurveCanvas.requestPaint() }
                                    }
                                    Component.onCompleted: requestPaint()
                                }

                                MouseArea {
                                    anchors.fill: formalTradingCurveCanvas
                                    hoverEnabled: true
                                    onPositionChanged: function(mouse) {
                                        var nearestIndex = root.formalTradingCurveNearestIndex(mouse.x, formalTradingCurveCanvas.width)
                                        if (nearestIndex < 0) {
                                            formalTradingCurvePanel.hoveredCurveIndex = -1
                                            formalTradingCurvePanel.curveTooltipVisible = false
                                            formalTradingCurveCanvas.requestPaint()
                                            return
                                        }

                                        formalTradingCurvePanel.hoveredCurveIndex = nearestIndex
                                        formalTradingCurvePanel.curveTooltipText = root.formalTradingCurveTooltipText(nearestIndex)
                                        formalTradingCurvePanel.curveTooltipVisible = formalTradingCurvePanel.curveTooltipText.length > 0
                                        formalTradingCurvePanel.curveTooltipX = mouse.x + formalTradingCurveCanvas.anchors.leftMargin + 12
                                        formalTradingCurvePanel.curveTooltipY = mouse.y + formalTradingCurveCanvas.anchors.topMargin - 46
                                        formalTradingCurveCanvas.requestPaint()
                                    }
                                    onExited: {
                                        formalTradingCurvePanel.hoveredCurveIndex = -1
                                        formalTradingCurvePanel.curveTooltipVisible = false
                                        formalTradingCurveCanvas.requestPaint()
                                    }
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.top: parent.top
                                    anchors.topMargin: 8
                                    text: "总资产曲线"
                                    font.pixelSize: 10
                                    color: "#94A3B8"
                                }

                                Text {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.top: parent.top
                                    anchors.topMargin: 8
                                    text: formatAssetMetric(currentFormalTradingExecution().endingTotalAsset)
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    color: formalTradingAccentColor(formalTradingStatus())
                                }

                                Rectangle {
                                    visible: formalTradingCurvePanel.curveTooltipVisible
                                    radius: 8
                                    color: "#111827"
                                    border.width: 1
                                    border.color: formalTradingAccentColor(formalTradingStatus())
                                    z: 2
                                    x: Math.min(Math.max(8, formalTradingCurvePanel.curveTooltipX), formalTradingCurvePanel.width - width - 8)
                                    y: Math.min(Math.max(24, formalTradingCurvePanel.curveTooltipY), formalTradingCurvePanel.height - height - 8)
                                    width: formalTradingCurveTooltipText.implicitWidth + 16
                                    height: formalTradingCurveTooltipText.implicitHeight + 12

                                    Text {
                                        id: formalTradingCurveTooltipText
                                        anchors.centerIn: parent
                                        text: formalTradingCurvePanel.curveTooltipText
                                        font.pixelSize: 10
                                        color: "#E2E8F0"
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    Layout.fillWidth: true
                                    text: "执行日 "
                                          + tradingPreviewCountText(normalizedListValue(currentFormalTradingExecution().executionDates).length)
                                          + " 个 · 已接受 "
                                          + tradingPreviewCountText(currentFormalTradingExecution().acceptedOrderCount)
                                          + " 笔 · 期末总资产 "
                                          + formatAssetMetric(currentFormalTradingExecution().endingTotalAsset)
                                          + " · 现金 "
                                          + formatAssetMetric(currentFormalTradingExecution().endingCash)
                                          + " · 持仓市值 "
                                          + formatAssetMetric(currentFormalTradingExecution().endingMarketValue)
                                    font.pixelSize: 11
                                    color: "#94A3B8"
                                    wrapMode: Text.WordWrap
                                }

                                Rectangle {
                                    visible: formalTradingDetailRows().length > 0
                                    radius: 10
                                    color: formalTradingDetailsExpanded ? "#1D4ED8" : "#0F172A"
                                    border.width: 1
                                    border.color: formalTradingDetailsExpanded ? "#60A5FA" : "#334155"
                                    implicitWidth: formalTradingDetailsButtonText.implicitWidth + 18
                                    implicitHeight: formalTradingDetailsButtonText.implicitHeight + 10

                                    Text {
                                        id: formalTradingDetailsButtonText
                                        anchors.centerIn: parent
                                        text: formalTradingDetailsExpanded ? "收起明细" : "查看明细"
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        color: "#E2E8F0"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.formalTradingDetailsExpanded = !root.formalTradingDetailsExpanded
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: formalTradingDetailPanelHeight()
                    visible: formalTradingDetailPanelHeight() > 0
                    radius: 12
                    color: "#111827"
                    border.width: 1
                    border.color: "#243041"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "正式执行资产轨迹"
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: "#F8FAFC"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "最新在前"
                                font.pixelSize: 10
                                color: "#94A3B8"
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            radius: 8
                            color: "#0F172A"
                            border.width: 1
                            border.color: "#243041"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12

                                Text {
                                    Layout.preferredWidth: 104
                                    text: "执行日"
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    color: "#94A3B8"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "现金"
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    color: "#94A3B8"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "持仓市值"
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    color: "#94A3B8"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "总资产"
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    color: "#94A3B8"
                                }
                            }
                        }

                        Flickable {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: width
                            contentHeight: formalTradingDetailColumn.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds

                            Column {
                                id: formalTradingDetailColumn
                                width: parent.width
                                spacing: 8

                                Repeater {
                                    model: root.formalTradingDetailRows()

                                    delegate: Rectangle {
                                        width: formalTradingDetailColumn.width
                                        height: 32
                                        radius: 8
                                        color: index % 2 === 0 ? "#0F172A" : "#111827"
                                        border.width: 1
                                        border.color: "#243041"

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 12
                                            spacing: 12

                                            Text {
                                                Layout.preferredWidth: 104
                                                text: String(modelData.date || "--")
                                                font.pixelSize: 10
                                                color: "#CBD5E1"
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.formatOptionalAssetMetric(modelData.cash)
                                                font.pixelSize: 10
                                                color: "#CBD5E1"
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.formatOptionalAssetMetric(modelData.marketValue)
                                                font.pixelSize: 10
                                                color: "#CBD5E1"
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.formatOptionalAssetMetric(modelData.totalAsset)
                                                font.pixelSize: 10
                                                font.weight: Font.DemiBold
                                                color: root.formalTradingAccentColor(root.formalTradingStatus())
                                            }
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
                                    visible: false
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
                                                text: modelData.groupIndex || (index + 1)
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                                color: "#F1F5F9"
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2

                                            Text {
                                                text: "第 " + (modelData.groupIndex || (index + 1)) + " 组"
                                                font.pixelSize: 14
                                                font.weight: Font.Medium
                                                color: "#F1F5F9"
                                            }

                                            RowLayout {
                                                spacing: 16

                                                Text {
                                                    text: "股票: " + root.formatMetric(modelData.stockCount, 0, false)
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                }

                                                Text {
                                                    text: "因子值: " + root.formatMetric(modelData.minFactorValue, 2, false) + " - " + root.formatMetric(modelData.maxFactorValue, 2, false)
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.alignment: Qt.AlignRight
                                            spacing: 2

                                            Text {
                                                text: root.formatPercentMetric(modelData.returnRate, 2, false)
                                                font.pixelSize: 16
                                                font.weight: Font.Bold
                                                color: root.returnMetricColor(modelData.returnRate)
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
        property string exportText: ""

        onFailuresChanged: {
            exportText = root.buildPreflightFailureExportText(failures || [])
        }

        onOpened: {
            exportText = root.buildPreflightFailureExportText(failures || [])
        }

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
                        text: preflightFailureDialog.exportText
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
    
    // 开始回测 - 单因子/批量模式
    function startSingleOrBatchBacktest() {
        console.log("开始回测，因子数量:", selectedFactorIds.length)
        applyRuntimeParamsDialog()
        syncSelectedDatasetIndex()

        if (selectedDataSourceMode !== "cache") {
            setDataSourceMode("cache")
        }
        
        if (selectedFactorIds.length === 0) {
            console.log("请先选择要回测的因子")
            return
        }

        if (!hasAvailableCacheDataset()) {
            console.log("当前没有可用缓存集，无法开始因子回测")
            return
        }

        var resolvedDatasetId = resolvedSelectedDatasetId()
        if (resolvedDatasetId <= 0) {
            console.log("请先选择缓存集后再开始回测")
            return
        }

        if (factorBacktestController.selectedDatasetId !== resolvedDatasetId) {
            factorBacktestController.selectedDatasetId = resolvedDatasetId
        }
        
        // 首先将选择的因子ID传递给控制器
        var selectedStartDate = ""
        var selectedEndDate = ""

        if (cleanedDataController) {
            if (cleanedDataController.currentStartDate && cleanedDataController.currentEndDate) {
                selectedStartDate = cleanedDataController.currentStartDate
                selectedEndDate = cleanedDataController.currentEndDate
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

        factorBacktestController.selectedDatasetId = resolvedDatasetId
        factorBacktestController.dataSourceMode = selectedDataSourceMode

        // 调用C++控制器开始回测，传递当前选中数据集对应的日期范围
        factorBacktestController.startBacktestWithFactors(
            factorIdList,
            groupComboBox.currentText,
            selectedStartDate,
            selectedEndDate,
            currentCacheSupportSnapshot())
    }

    function startCompositeBacktest() {
        console.log("开始组合因子回测，子因子数量:", compositeChildAllocations.length)
        applyRuntimeParamsDialog()
        syncSelectedDatasetIndex()

        if (selectedDataSourceMode !== "cache") {
            setDataSourceMode("cache")
        }

        if (!hasAvailableCacheDataset()) {
            console.log("当前没有可用缓存集，无法开始组合因子回测")
            return
        }

        var resolvedDatasetId = resolvedSelectedDatasetId()
        if (resolvedDatasetId <= 0) {
            console.log("请先选择缓存集后再开始组合因子回测")
            return
        }

        if (factorBacktestController.selectedDatasetId !== resolvedDatasetId) {
            factorBacktestController.selectedDatasetId = resolvedDatasetId
        }

        var selectedStartDate = ""
        var selectedEndDate = ""
        if (cleanedDataController) {
            if (cleanedDataController.currentStartDate && cleanedDataController.currentEndDate) {
                selectedStartDate = cleanedDataController.currentStartDate
                selectedEndDate = cleanedDataController.currentEndDate
            }
        }

        root.activeRunFactorIds = compositeChildIds().slice()
        factorBacktestController.selectedDatasetId = resolvedDatasetId
        factorBacktestController.dataSourceMode = selectedDataSourceMode
        factorBacktestController.startCompositeBacktest(
            buildCompositeDraft(),
            groupComboBox.currentText,
            selectedStartDate,
            selectedEndDate,
            currentCacheSupportSnapshot())
    }

    // 开始回测 - 总入口
    function startBacktest() {
        if (backtestEntryMode === 1) {
            startCompositeBacktest()
            return
        }
        startSingleOrBatchBacktest()
    }

    // 打开因子选择对话框 - 简化版本
    function openFactorSelector() {
        console.log("打开因子选择对话框")

        var cachedSupportMap = currentCacheFactorSupportMap()
        var hasCachedSupportMap = cachedSupportMap && Object.keys(cachedSupportMap).length > 0
        var supportMapLoading = factorBacktestController
                ? factorBacktestController.supportMapRequestInFlight
                : false
        
        // 创建对话框组件
        var component = Qt.createComponent("FactorSelectorDialog.qml")
        if (component.status === Component.Ready) {
            var dialogParent = Qt.application.activeWindow ? Qt.application.activeWindow : root
            factorSelectorDialog = component.createObject(root, {
                factorService: factorService,
                factorViewModel: factorService ? factorService.getViewModel() : null,
                selectedFactorIds: backtestEntryMode === 1 ? compositeChildIds().slice() : selectedFactorIds.slice(),
                dataSourceMode: selectedDataSourceMode,
                supportMapRequested: hasCachedSupportMap || supportMapLoading,
                supportMapLoading: supportMapLoading,
                factorSupportMap: hasCachedSupportMap ? shallowCopyMap(cachedSupportMap) : ({}),
                supportMapRefreshCallback: function() { runSupportMapRefresh(true) }
            })
            if (factorSelectorDialog && dialogParent) {
                factorSelectorDialog.parent = dialogParent
            }
            
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
        if (backtestEntryMode === 1) {
            setCompositeChildrenFromFactorIds(factorIds)
            refreshFactorSupportMap(false)
            return
        }
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
        if (backtestEntryMode === 1) {
            removeCompositeChild(factorId)
            refreshFactorSupportMap(false)
            return
        }
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

    Popup {
        id: runtimeParamsDialog
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.max(24, (root.width - width) / 2)
        y: Math.max(24, (root.height - height) / 2)
        width: Math.min(780, root.width - 32)
        height: Math.min(660, root.height - 32)

        ButtonGroup {
            id: runtimeAdjustPriceTypeGroup
        }

        background: Rectangle {
            radius: 18
            border.width: 1
            border.color: "#273244"
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#0B1220" }
                GradientStop { position: 0.6; color: "#0F172A" }
                GradientStop { position: 1.0; color: "#111827" }
            }
        }

        contentItem: Item {
            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: "回测参数设置"
                            font.pixelSize: 18
                            font.weight: Font.Bold
                            color: "#F8FAFC"
                        }

                        Text {
                            text: "仅影响当前因子回测，不改风险页布局"
                            font.pixelSize: 11
                            color: "#94A3B8"
                        }
                    }

                    Text {
                        text: runtimeParamsSummaryText()
                        font.pixelSize: 11
                        color: "#38BDF8"
                        horizontalAlignment: Text.AlignRight
                        wrapMode: Text.WordWrap
                        Layout.preferredWidth: 240
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 14
                    border.width: 1
                    border.color: "#243244"
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#122033" }
                        GradientStop { position: 1.0; color: "#0F172A" }
                    }

                    implicitHeight: bannerColumn.implicitHeight + 26

                    ColumnLayout {
                        id: bannerColumn
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        Text {
                            text: "先设置复权，再调整参数"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: "#F8FAFC"
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "复权方式会直接影响价格序列、收益和回撤的展示口径，建议先确认这里再做其它参数微调。"
                            font.pixelSize: 11
                            color: "#94A3B8"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ColumnLayout {
                        id: runtimeParamsContent
                        width: Math.max(0, runtimeParamsDialog.width - 60)
                        spacing: 14

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 14
                            border.width: 1
                            border.color: "#243244"
                            color: "#0F172A"
                            implicitHeight: adjustCardColumn.implicitHeight + 28

                            ColumnLayout {
                                id: adjustCardColumn
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Text {
                                            text: "复权方式"
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            color: "#F8FAFC"
                                        }

                                        Text {
                                            text: "默认使用后复权；如果更关注历史原始价格走势，可切换为前复权。"
                                            font.pixelSize: 11
                                            color: "#94A3B8"
                                            wrapMode: Text.WordWrap
                                        }
                                    }

                                    Rectangle {
                                        radius: 999
                                        color: "#0B1220"
                                        border.width: 1
                                        border.color: "#334155"
                                        implicitWidth: defaultAdjustChip.implicitWidth + 18
                                        implicitHeight: defaultAdjustChip.implicitHeight + 10

                                        Text {
                                            id: defaultAdjustChip
                                            anchors.centerIn: parent
                                            text: "默认后复权"
                                            font.pixelSize: 10
                                            font.weight: Font.Medium
                                            color: "#93C5FD"
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    RadioButton {
                                        id: runtimeAdjustPriceTypePreButton
                                        ButtonGroup.group: runtimeAdjustPriceTypeGroup
                                        text: "前复权"
                                        checked: false
                                        Layout.fillWidth: true
                                    }

                                    RadioButton {
                                        id: runtimeAdjustPriceTypePostButton
                                        ButtonGroup.group: runtimeAdjustPriceTypeGroup
                                        text: "后复权"
                                        checked: true
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 14
                                border.width: 1
                                border.color: "#243244"
                                color: "#0F172A"
                                implicitHeight: coreCardColumn.implicitHeight + 28

                                ColumnLayout {
                                    id: coreCardColumn
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 10

                                    Text {
                                        text: "核心调仓参数"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 2
                                        columnSpacing: 12
                                        rowSpacing: 10

                                        Text { text: "初始资金"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeInitialCapitalField
                                            Layout.fillWidth: true
                                            text: "1000000"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            validator: DoubleValidator { bottom: 1; top: 1000000000000; decimals: 2 }
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeInitialCapitalField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }

                                        Text { text: "持仓天数"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeForwardDaysField
                                            Layout.fillWidth: true
                                            text: "30"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            validator: IntValidator { bottom: 1; top: 3650 }
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeForwardDaysField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }

                                        Text { text: "市场环境"; font.pixelSize: 11; color: "#94A3B8" }
                                        ComboBox {
                                            id: runtimeMarketEnvironmentComboBox
                                            Layout.fillWidth: true
                                            model: root.marketEnvironmentOptions
                                            textRole: "label"
                                            currentIndex: 0
                                            font.pixelSize: 12
                                            onActivated: applyRuntimeParamsDialog()
                                        }

                                        Text { text: "调仓天数"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeRebalanceDaysField
                                            Layout.fillWidth: true
                                            text: "15"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            validator: IntValidator { bottom: 1; top: 3650 }
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeRebalanceDaysField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }

                                        Text { text: "信号阈值(σ)"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeSignalThresholdField
                                            Layout.fillWidth: true
                                            text: "0.30"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            validator: DoubleValidator { bottom: 0; top: 100 }
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeSignalThresholdField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }

                                        Text { text: "换手上限"; font.pixelSize: 11; color: "#94A3B8" }
                                        RadioButton {
                                            id: runtimeEnableTurnoverLimitBox
                                            Layout.fillWidth: true
                                            text: "启用换手上限"
                                            checked: false
                                            font.pixelSize: 12
                                        }

                                        Text { text: "最大换手"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeMaxRebalanceTurnoverField
                                            Layout.fillWidth: true
                                            text: "0.50"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            validator: DoubleValidator { bottom: 0; top: 100 }
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeMaxRebalanceTurnoverField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 14
                                border.width: 1
                                border.color: "#243244"
                                color: "#0F172A"
                                implicitHeight: costCardColumn.implicitHeight + 28

                                ColumnLayout {
                                    id: costCardColumn
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 10

                                    Text {
                                        text: "交易成本与基准"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 2
                                        columnSpacing: 12
                                        rowSpacing: 10

                                        Text { text: "手续费(%)"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeTransactionCostField
                                            Layout.fillWidth: true
                                            text: "0.10"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            validator: DoubleValidator { bottom: 0; top: 100 }
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeTransactionCostField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }

                                        Text { text: "滑点(%)"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeSlippageRateField
                                            Layout.fillWidth: true
                                            text: "0.10"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            validator: DoubleValidator { bottom: 0; top: 100 }
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeSlippageRateField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }

                                        Text { text: "无风险利率(%)"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeRiskFreeRateField
                                            Layout.fillWidth: true
                                            text: "2.00"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            validator: DoubleValidator { bottom: 0; top: 100 }
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeRiskFreeRateField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }

                                        Text { text: "基准代码"; font.pixelSize: 11; color: "#94A3B8" }
                                        TextField {
                                            id: runtimeBenchmarkSymbolField
                                            Layout.fillWidth: true
                                            text: "000300.SH"
                                            color: "#F1F5F9"
                                            font.pixelSize: 12
                                            placeholderText: "000300.SH"
                                            background: Rectangle {
                                                radius: 10
                                                color: "#0B1220"
                                                border.width: 1
                                                border.color: runtimeBenchmarkSymbolField.activeFocus ? "#3B82F6" : "#334155"
                                            }
                                            onEditingFinished: applyRuntimeParamsDialog()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 2

                    Text {
                        text: "修改后会直接写入当前回测参数，开始回测时自动生效"
                        font.pixelSize: 10
                        color: "#64748B"
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.preferredWidth: 92
                        Layout.preferredHeight: 34
                        radius: 8
                        color: "#334155"

                        Text {
                            anchors.centerIn: parent
                            text: "重载"
                            font.pixelSize: 12
                            color: "#E2E8F0"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: loadRuntimeParamsDialog()
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 92
                        Layout.preferredHeight: 34
                        radius: 8
                        color: "#2563EB"

                        Text {
                            anchors.centerIn: parent
                            text: "应用"
                            font.pixelSize: 12
                            color: "white"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                applyRuntimeParamsDialog()
                                runtimeParamsDialog.close()
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 92
                        Layout.preferredHeight: 34
                        radius: 8
                        color: "#334155"

                        Text {
                            anchors.centerIn: parent
                            text: "关闭"
                            font.pixelSize: 12
                            color: "#E2E8F0"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: runtimeParamsDialog.close()
                        }
                    }
                }
            }
        }
    }
    
    Component.onCompleted: {
        console.log("因子回测页面初始化完成")
        console.log("因子服务:", factorService)
        console.log("当前选择因子:", selectedFactorId)
        console.log("当前选择因子列表:", selectedFactorIds)
        if (riskConfigService && typeof riskConfigService.initialize === "function") {
            riskConfigService.initialize()
        }
        loadRuntimeParamsDialog()
        applyRuntimeParamsDialog()
        root.setDataSourceMode(selectedDataSourceMode)
        syncSelectedDatasetBenchmarkMetadata()

        if (cleanedDataController) {
            if (!cleanedDataController.isAvailable) {
                cleanedDataController.initialize()
            }
            refreshDatasetsThrottled(true)
        }

        cacheDatasetSyncTimer.restart()
        root.clearDisplayedBacktestState()
        root.activeRunFactorIds = []
        
        // 数据源和日期范围处理已移至C++控制器，QML只负责UI显示
        console.log("因子回测页面初始化完成，等待用户操作")
    }

    Connections {
        target: cleanedDataController

        function onDatasetListChanged() {
            cacheDatasetSyncTimer.restart()
        }

        function onSelectedDatasetChanged() {
            var datasetId = cleanedDataController && cleanedDataController.selectedDatasetInfo
                ? positiveDatasetId(cleanedDataController.selectedDatasetInfo.id)
                : -1
            if (datasetId > 0) {
                selectedCacheDatasetId = datasetId
                if (factorBacktestController && factorBacktestController.selectedDatasetId !== datasetId) {
                    factorBacktestController.selectedDatasetId = datasetId
                }
                syncSelectedDatasetBenchmarkMetadata()
            } else if (factorBacktestController && factorBacktestController.selectedDatasetId > 0) {
                selectedCacheDatasetId = -1
                factorBacktestController.selectedDatasetId = -1
                factorBacktestController.selectedDatasetBenchmarkMetadata = ({})
            }
        }

        function onSelectedDatasetDiagnosticsChanged() {
            cacheDatasetSyncTimer.restart()
        }
    }
}


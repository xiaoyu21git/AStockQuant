import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15
import AStock.Bridge 1.0
import "../../components/Backtest" as BacktestComponents
import "../../components/FactorWorkbench/Creation/components" as PluginComponents
import "../../components/FactorWorkbench/Navigation" as NavigationComponents
import "../../utils/StrategyCreationUtils.js" as CreationUtils
import "../../utils/StrategyStructureAdapter.js" as StructureAdapter

Rectangle {
    id: strategyBacktestPage
    color: "#0F172A"
    property bool embeddedMode: false
    property string preferredStrategyId: ""

    property var strategyService: null
    readonly property var cleanedDataController: CleanedDataController
    property var strategyViewModel: null
    property bool serviceSignalsBound: false
    property bool pageServicesReady: false

    property int selectedStrategyIndex: -1
    property string selectedStrategyId: ""

    property string actionFeedbackMessage: ""
    property bool actionFeedbackError: false

    property var strategyBacktestRuntimeBaseParameters: ({})
    property var strategyBacktestParameterValues: ({})
    property bool strategyBacktestParametersLoaded: false
    property string strategyBacktestSelectedUniverseType: "market"
    property string strategyBacktestSelectedIndexSymbol: "000300.SH"
    property string strategyBacktestSelectedStartDate: ""
    property string strategyBacktestSelectedEndDate: ""
    property int strategyBacktestSelectedDataSourceMode: 0
    property bool strategyBacktestRunning: false
    property real strategyBacktestCompletionRatio: 0
    property string strategyBacktestStatusText: ""
    property var strategyBacktestPendingContext: ({})
    property var strategyBacktestPendingStrategyData: ({})
    property var strategyBacktestLastResult: ({})
    property var strategyBacktestActiveHandle: ({})
    property var strategyBacktestDynamicParamConfigs: []
    property var strategyBacktestDynamicParamGroups: []
    property int strategyBacktestSelectedCacheDatasetId: -1
    property int strategyBacktestSelectedCacheDatasetIndex: 0
    property var strategyBacktestCacheDatasetOptions: []
    property bool historyChartsExpanded: false
    property bool historyListExpanded: false
    property bool latestBacktestExpanded: true
    property bool historySectionExpanded: false

    readonly property bool hasSelectedStrategy: selectedStrategyIndex >= 0
        && strategyViewModel
        && strategyViewModel.count > selectedStrategyIndex

    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color secondaryBg: "#1E293B"
    readonly property color tertiaryBg: "#334155"
    readonly property color accentBlue: "#3B82F6"
    readonly property color borderColor: "#475569"
    readonly property color successGreen: "#10B981"
    readonly property color riseRed: "#EF4444"
    readonly property color warningAmber: "#F59E0B"
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    readonly property real spacingXLarge: 24
    readonly property real borderRadiusMedium: 8
    readonly property real borderRadiusXLarge: 16
    readonly property int historyPreviewCount: 3
    readonly property real contentMaxWidth: 1480
    readonly property real pageSidePadding: 18

    readonly property var strategyBacktestUniverseOptions: [
        { label: "全市场", value: "market" },
        { label: "指数成分", value: "index" }
    ]
    readonly property var strategyBacktestIndexOptions: [
        { label: "沪深300", value: "000300.SH" },
        { label: "中证500", value: "000905.SH" },
        { label: "创业板指", value: "399006.SZ" }
    ]
    readonly property var strategyBacktestDataSourceOptions: [
        { label: "原始K线", value: 0 },
        { label: "缓存K线", value: 1 }
    ]

    PluginComponents.ParamComponents {
        id: paramComponents

        Component.onCompleted: {
            if (typeof paramComponents.registerAllComponents === "function") {
                paramComponents.registerAllComponents()
            }
        }
    }

    function initializeStrategyViewModel() {
        strategyService = null
        if (!strategyService) {
            console.error("无法获取 StrategyService 实例")
            return
        }

        var strategyServiceReady = false
        if (typeof strategyService.isInitialized === "function") {
            strategyServiceReady = !!strategyService.isInitialized()
        } else if (strategyService.isInitialized !== undefined) {
            strategyServiceReady = !!strategyService.isInitialized
        }

        if (!strategyServiceReady) {
            if (typeof strategyService.initializeAsync === "function") {
                strategyService.initializeAsync()
            } else if (typeof strategyService.initialize === "function") {
                strategyService.initialize()
            }
        }

        strategyViewModel = null

        if (!serviceSignalsBound) {
            serviceSignalsBound = true

            strategyService.initializedChanged.connect(function() {
                rebuildStrategyVisibleModel()
                syncSelectedStrategy()
                syncPreferredStrategySelection()
            })

            strategyService.strategiesLoaded.connect(function() {
                rebuildStrategyVisibleModel()
                syncSelectedStrategy()
                syncPreferredStrategySelection()
            })

            strategyService.dataChanged.connect(function() {
                rebuildStrategyVisibleModel()
                syncSelectedStrategy()
                syncPreferredStrategySelection()
                syncStrategyBacktestEditor()
            })

            strategyService.strategyCreated.connect(function() {
                rebuildStrategyVisibleModel()
                syncSelectedStrategy()
                syncPreferredStrategySelection()
            })
        }

        rebuildStrategyVisibleModel()
        syncSelectedStrategy()
        syncPreferredStrategySelection()
    }

    function syncPreferredStrategySelection() {
        var normalizedId = String(preferredStrategyId || "").trim()
        if (normalizedId.length > 0) {
            selectStrategyById(normalizedId)
        }
    }

    function toPlainJsValue(rawValue) {
        if (rawValue === undefined || rawValue === null) {
            return rawValue
        }

        if (typeof rawValue === "object") {
            try {
                return JSON.parse(JSON.stringify(rawValue))
            } catch (error) {
            }
        }

        return rawValue
    }

    function getStrategyData(index) {
        if (!strategyViewModel || index < 0 || index >= strategyViewModel.count) {
            return null
        }
        return strategyViewModel.getRow(index)
    }

    function rebuildStrategyVisibleModel() {
        strategyVisibleModel.clear()

        if (!strategyViewModel) {
            return
        }

        for (var index = 0; index < strategyViewModel.count; ++index) {
            var row = strategyViewModel.getRow(index)
            strategyVisibleModel.append({
                sourceIndex: index,
                strategyId: row ? (row.strategyId || "") : ""
            })
        }
    }

    function selectStrategyAt(index) {
        if (!strategyViewModel || index < 0 || index >= strategyViewModel.count) {
            selectedStrategyIndex = -1
            selectedStrategyId = ""
            return
        }

        var selectedRow = strategyViewModel.getRow(index)
        selectedStrategyIndex = index
        selectedStrategyId = selectedRow ? String(selectedRow.strategyId || "") : ""
    }

    function selectStrategyById(strategyId) {
        var normalizedId = String(strategyId || "").trim()
        if (!normalizedId || !strategyViewModel) {
            return
        }

        for (var index = 0; index < strategyViewModel.count; ++index) {
            var row = strategyViewModel.getRow(index)
            var rowId = row ? String(row.strategyId || "") : ""
            if (rowId === normalizedId) {
                selectStrategyAt(index)
                return
            }
        }
    }

    function syncSelectedStrategy() {
        if (!strategyViewModel || strategyViewModel.count === 0) {
            selectedStrategyIndex = -1
            selectedStrategyId = ""
            return
        }

        if (selectedStrategyId) {
            for (var index = 0; index < strategyViewModel.count; ++index) {
                var row = strategyViewModel.getRow(index)
                var rowId = row ? String(row.strategyId || "") : ""
                if (rowId === selectedStrategyId) {
                    selectedStrategyIndex = index
                    return
                }
            }
        }

        if (selectedStrategyIndex >= 0 && selectedStrategyIndex < strategyViewModel.count) {
            var currentRow = strategyViewModel.getRow(selectedStrategyIndex)
            selectedStrategyId = currentRow ? String(currentRow.strategyId || "") : ""
            return
        }

        if (strategyVisibleModel.count > 0) {
            selectStrategyAt(strategyVisibleModel.get(0).sourceIndex)
            return
        }

        selectStrategyAt(0)
    }

    function getSelectedStrategySummary() {
        if (strategyViewModel && selectedStrategyId) {
            for (var index = 0; index < strategyViewModel.count; ++index) {
                var row = strategyViewModel.getRow(index)
                var rowId = row ? String(row.strategyId || "") : ""
                if (rowId === selectedStrategyId) {
                    return row
                }
            }
        }

        if (selectedStrategyIndex >= 0) {
            return getStrategyData(selectedStrategyIndex)
        }

        return null
    }

    function getSelectedStrategyDetail(strategyId) {
        if (!strategyService || !strategyId || !strategyService.getStrategyById) {
            return ({})
        }

        return toPlainJsValue(strategyService.getStrategyById(strategyId)) || ({})
    }

    function currentStrategyDescription(strategy) {
        if (!strategy) {
            return "暂无描述"
        }

        var description = String(strategy.description || "").trim()
        if (description) {
            return description
        }

        var typeLabel = String(strategy.strategyType || "策略").trim()
        return typeLabel ? (typeLabel + "，尚未填写详细说明") : "暂无描述"
    }

    function isoDateDaysAgo(days) {
        var date = new Date()
        date.setDate(date.getDate() - days)
        return date.toISOString().slice(0, 10)
    }

    function positiveDatasetId(value) {
        var number = Number(value)
        return isNaN(number) || number <= 0 ? -1 : Math.round(number)
    }

    function normalizeBacktestDateText(value, fallbackText) {
        var text = String(value || "").trim()
        return text ? text : fallbackText
    }

    function normalizeBacktestPercentageToRatio(value) {
        var numeric = Number(value)
        if (!isFinite(numeric)) {
            return value
        }
        return numeric > 1 ? numeric / 100 : numeric
    }

    function resolveStrategyBacktestTypeIndex(strategyDetail) {
        var strategy = toPlainJsValue(strategyDetail) || ({})
        var explicitTypeIndex = Number(strategy.strategyTypeIndex)
        if (isFinite(explicitTypeIndex) && explicitTypeIndex >= 0) {
            var normalizedTypeIndex = CreationUtils.normalizeStrategyTypeIndex(explicitTypeIndex)
            if (normalizedTypeIndex !== CreationUtils.StrategyTypeIndex.Invalid) {
                return normalizedTypeIndex
            }
        }

        return CreationUtils.StrategyTypeIndex.Invalid
    }

    function mergeStrategyBacktestParameterMaps(baseValue, overrideValue) {
        var baseMap = toPlainJsValue(baseValue)
        var overrideMap = toPlainJsValue(overrideValue)
        var baseIsObject = !!(baseMap && typeof baseMap === "object" && !Array.isArray(baseMap))
        var overrideIsObject = !!(overrideMap && typeof overrideMap === "object" && !Array.isArray(overrideMap))

        if (!baseIsObject) {
            return overrideIsObject ? overrideMap : (overrideMap !== undefined ? overrideMap : baseMap)
        }
        if (!overrideIsObject) {
            return baseMap
        }

        var merged = ({})
        for (var baseKey in baseMap) {
            merged[baseKey] = baseMap[baseKey]
        }
        for (var overrideKey in overrideMap) {
            var overrideItem = overrideMap[overrideKey]
            if (overrideItem && typeof overrideItem === "object" && !Array.isArray(overrideItem)
                    && merged[overrideKey] && typeof merged[overrideKey] === "object" && !Array.isArray(merged[overrideKey])) {
                merged[overrideKey] = mergeStrategyBacktestParameterMaps(merged[overrideKey], overrideItem)
            } else {
                merged[overrideKey] = overrideItem
            }
        }
        return merged
    }

    function buildStrategyBacktestParameterSource(strategyDetail, latestBacktest) {
        var detailParameters = toPlainJsValue((strategyDetail && strategyDetail.parameters) || ({})) || ({})
        var latestRuntimeParameters = toPlainJsValue((latestBacktest && latestBacktest.runtimeParameters) || ({})) || ({})
        return mergeStrategyBacktestParameterMaps(detailParameters, latestRuntimeParameters)
    }

    function buildStrategyBacktestDynamicParamConfigs(strategyTypeIndex) {
        var configs = CreationUtils.buildParamConfigs(strategyTypeIndex)
        return Array.isArray(configs) ? configs : []
    }

    function strategyBacktestDatasetSelectable(dataset) {
        if (!dataset) {
            return false
        }

        var datasetId = positiveDatasetId(dataset.id !== undefined ? dataset.id : dataset.value)
        if (datasetId <= 0) {
            return false
        }

        var isBacktestReady = dataset.isBacktestReady === true
        var tags = Array.isArray(dataset.tags) ? dataset.tags : []
        if (!isBacktestReady && tags.indexOf("factor_backtest_ready") < 0) {
            return false
        }

        var availableFields = Array.isArray(dataset.availableFields) ? dataset.availableFields : []
        return availableFields.length > 0
    }

    function buildStrategyBacktestCacheDatasetOptions() {
        var options = [{ label: "请选择清洗数据", value: -1, raw: null }]
        var datasetList = cleanedDataController && cleanedDataController.datasetList ? cleanedDataController.datasetList : []
        var selectable = []

        for (var index = 0; index < datasetList.length; ++index) {
            var dataset = toPlainJsValue(datasetList[index]) || null
            if (strategyBacktestDatasetSelectable(dataset)) {
                selectable.push(dataset)
            }
        }

        selectable.sort(function(left, right) {
            return positiveDatasetId(right && right.id) - positiveDatasetId(left && left.id)
        })

        for (var selectableIndex = 0; selectableIndex < selectable.length; ++selectableIndex) {
            var current = selectable[selectableIndex]
            var datasetId = positiveDatasetId(current.id)
            var displayName = String(current.displayName || current.name || "").trim()
            var startDate = String(current.startDate || "").trim()
            var endDate = String(current.endDate || "").trim()
            var label = displayName ? displayName : ("清洗数据 #" + datasetId)
            if (startDate && endDate) {
                label += " (" + startDate + " ~ " + endDate + ")"
            }

            options.push({
                label: label,
                value: datasetId,
                raw: current
            })
        }

        return options
    }

    function syncStrategyBacktestSelectedDatasetIndex() {
        var targetId = strategyBacktestSelectedCacheDatasetId
        if (targetId <= 0 && cleanedDataController && cleanedDataController.selectedDatasetInfo) {
            targetId = positiveDatasetId(cleanedDataController.selectedDatasetInfo.id)
        }

        var resolvedIndex = 0
        for (var index = 0; index < strategyBacktestCacheDatasetOptions.length; ++index) {
            var option = strategyBacktestCacheDatasetOptions[index]
            if (positiveDatasetId(option && option.value) === targetId) {
                resolvedIndex = index
                break
            }
        }

        strategyBacktestSelectedCacheDatasetIndex = resolvedIndex
    }

    function rebuildStrategyBacktestCacheDatasetOptions() {
        strategyBacktestCacheDatasetOptions = buildStrategyBacktestCacheDatasetOptions()
        syncStrategyBacktestSelectedDatasetIndex()
    }

    function applySelectedCleanedDataset(datasetInfo) {
        var dataset = toPlainJsValue(datasetInfo) || ({})
        var datasetId = positiveDatasetId(dataset.id)
        if (datasetId <= 0) {
            strategyBacktestSelectedCacheDatasetId = -1
            syncStrategyBacktestSelectedDatasetIndex()
            return
        }

        strategyBacktestSelectedCacheDatasetId = datasetId
        syncStrategyBacktestSelectedDatasetIndex()

        var datasetStartDate = String(dataset.startDate || "").trim()
        var datasetEndDate = String(dataset.endDate || "").trim()
        if (strategyBacktestSelectedDataSourceMode === 1) {
            if (datasetStartDate) {
                strategyBacktestSelectedStartDate = datasetStartDate
            }
            if (datasetEndDate) {
                strategyBacktestSelectedEndDate = datasetEndDate
            }
        }
    }

    function selectStrategyBacktestCacheDatasetAt(index) {
        if (strategyBacktestSelectedDataSourceMode !== 1) {
            return
        }

        if (!strategyBacktestCacheDatasetOptions || index < 0 || index >= strategyBacktestCacheDatasetOptions.length) {
            return
        }

        var selectedOption = strategyBacktestCacheDatasetOptions[index]
        var selectedId = positiveDatasetId(selectedOption && selectedOption.value)
        strategyBacktestSelectedCacheDatasetIndex = index

        if (selectedId <= 0) {
            strategyBacktestSelectedCacheDatasetId = -1
            return
        }

        strategyBacktestSelectedCacheDatasetId = selectedId
        if (cleanedDataController && typeof cleanedDataController.loadDatasetById === "function") {
            cleanedDataController.loadDatasetById(selectedId)
        }
        applySelectedCleanedDataset(selectedOption.raw)
    }

    function initializeStrategyBacktestCleanedData() {
        if (!cleanedDataController) {
            return
        }

        if (!cleanedDataController.isAvailable && typeof cleanedDataController.initialize === "function") {
            cleanedDataController.initialize()
        }
        if (typeof cleanedDataController.refreshDatasets === "function") {
            cleanedDataController.refreshDatasets()
        }
        rebuildStrategyBacktestCacheDatasetOptions()
    }

    function buildStrategyBacktestDynamicParamGroups(configs) {
        var resolvedConfigs = Array.isArray(configs) ? configs : []
        var commonParamIds = {
            positionSize: true,
            stopLoss: true,
            takeProfit: true,
            maxDrawdownLimit: true,
            rebalanceDays: true
        }
        var commonParams = []
        var strategySpecificParams = []

        for (var index = 0; index < resolvedConfigs.length; ++index) {
            var config = resolvedConfigs[index]
            if (!config || !config.id) {
                continue
            }

            if (commonParamIds[config.id]) {
                commonParams.push(config.id)
            } else {
                strategySpecificParams.push(config.id)
            }
        }

        var groups = []
        if (commonParams.length > 0) {
            groups.push({
                id: "commonBacktestParams",
                name: "通用回测参数",
                description: "只保留仓位、风控和调仓频率等核心回测参数。",
                minColumnWidth: 320,
                maxColumns: 2,
                params: commonParams
            })
        }
        if (strategySpecificParams.length > 0) {
            groups.push({
                id: "strategySpecificParams",
                name: "当前策略参数",
                description: "仅展示当前策略类型真正参与运行的参数。",
                minColumnWidth: 320,
                maxColumns: 2,
                params: strategySpecificParams
            })
        }
        return groups
    }

    function resolveVisibleBacktestHistory(historyItems) {
        var resolvedHistory = toPlainJsValue(historyItems)
        if (!Array.isArray(resolvedHistory)) {
            return []
        }
        if (historyListExpanded) {
            return resolvedHistory
        }
        return resolvedHistory.slice(0, historyPreviewCount)
    }

    function resolveStrategyBacktestRunContextDataSourceMode() {
        if (strategyBacktestSelectedDataSourceMode !== 1) {
            return 0
        }
        return 2
    }

    function selectedStrategyBacktestCacheDatasetInfo() {
        if (strategyBacktestSelectedCacheDatasetId > 0
                && cleanedDataController
                && cleanedDataController.selectedDatasetInfo) {
            var selectedDatasetInfo = toPlainJsValue(cleanedDataController.selectedDatasetInfo) || ({})
            if (positiveDatasetId(selectedDatasetInfo.id) === strategyBacktestSelectedCacheDatasetId) {
                return selectedDatasetInfo
            }
        }

        for (var index = 0; index < strategyBacktestCacheDatasetOptions.length; ++index) {
            var option = strategyBacktestCacheDatasetOptions[index]
            if (positiveDatasetId(option && option.value) === strategyBacktestSelectedCacheDatasetId) {
                return toPlainJsValue(option.raw) || ({})
            }
        }

        return ({})
    }

    function resolveStrategyBacktestWindowSelection() {
        var startDate = normalizeBacktestDateText(strategyBacktestSelectedStartDate, "")
        var endDate = normalizeBacktestDateText(strategyBacktestSelectedEndDate, "")

        if (strategyBacktestSelectedDataSourceMode === 1) {
            var datasetInfo = selectedStrategyBacktestCacheDatasetInfo()
            var datasetStartDate = normalizeBacktestDateText(datasetInfo.startDate, "")
            var datasetEndDate = normalizeBacktestDateText(datasetInfo.endDate, "")
            if (datasetStartDate) {
                startDate = datasetStartDate
            }
            if (datasetEndDate) {
                endDate = datasetEndDate
            }
        }

        return {
            startDate: startDate,
            endDate: endDate
        }
    }

    function strategyBacktestCandidateFieldMessage(fieldName) {
        switch (String(fieldName || "")) {
        case "strategyId":
            return "当前策略缺少可执行的 strategyId。"
        case "universeId":
            return "当前策略缺少可解析的回测范围。"
        case "targetPositionCount":
            return "当前策略缺少目标持仓数。"
        case "startDate":
            return strategyBacktestSelectedDataSourceMode === 1
                ? "缓存K线回测无法解析所选清洗数据的开始日期。"
                : "请先选择开始日期。"
        case "endDate":
            return strategyBacktestSelectedDataSourceMode === 1
                ? "缓存K线回测无法解析所选清洗数据的结束日期。"
                : "请先选择结束日期。"
        case "windowStartDay":
            return "开始日期无法映射到交易日历。"
        case "windowEndDay":
            return "结束日期无法映射到交易日历。"
        case "dataSourceDatasetId":
            return "缓存K线回测必须先选择清洗数据。"
        case "benchmarkSymbolId":
            return "当前策略配置了基准，但基准代码无法解析。"
        default:
            return "缺少字段: " + String(fieldName || "--")
        }
    }

    function formatStrategyBacktestCandidateError(candidate) {
        if (!candidate) {
            return "策略回测上下文解析失败。"
        }

        var parts = []
        var missingFields = Array.isArray(candidate.missingFields) ? candidate.missingFields : []
        var seenFields = ({})

        for (var fieldIndex = 0; fieldIndex < missingFields.length; ++fieldIndex) {
            var fieldName = String(missingFields[fieldIndex] || "").trim()
            if (!fieldName || seenFields[fieldName]) {
                continue
            }
            seenFields[fieldName] = true
            parts.push(strategyBacktestCandidateFieldMessage(fieldName))
        }

        var unresolvedSymbols = Array.isArray(candidate.unresolvedSymbols) ? candidate.unresolvedSymbols : []
        if (unresolvedSymbols.length > 0) {
            parts.push("以下证券代码无法解析 symbolId: " + unresolvedSymbols.join(", "))
        }

        if (parts.length === 0) {
            return String(candidate.errorText || "策略回测上下文解析失败。")
        }

        return parts.join("；")
    }

    function buildStrategyBacktestRuntimeParameters(strategyTypeIndex, sourceParameters) {
        var sourceParams = toPlainJsValue(sourceParameters) || ({})
        var normalizedStrategyTypeIndex = CreationUtils.normalizeStrategyTypeIndex(strategyTypeIndex)
        var mappedValues = ({})
        var persistedRuleProfile = StructureAdapter.resolveRuleProfile(sourceParams)
        var persistedExecutionPolicy = StructureAdapter.resolveExecutionPolicy(sourceParams)
        var persistedBacktestAssumptions = StructureAdapter.resolveBacktestAssumptions(sourceParams)

        function assignIfPresent(targetKey, sourceKeys, transform) {
            for (var index = 0; index < sourceKeys.length; ++index) {
                var key = sourceKeys[index]
                var resolvedValue = sourceParams[key]
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    resolvedValue = persistedRuleProfile[key]
                }
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    resolvedValue = persistedExecutionPolicy[key]
                }
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    resolvedValue = persistedBacktestAssumptions[key]
                }
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    if (key === "stopLoss") {
                        resolvedValue = persistedRuleProfile.stopLossPercent
                    } else if (key === "takeProfit") {
                        resolvedValue = persistedRuleProfile.takeProfitPercent
                    } else if (key === "rebalanceDays") {
                        resolvedValue = persistedRuleProfile.rebalanceDays
                    }
                }
                if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                    continue
                }
                mappedValues[targetKey] = transform ? transform(resolvedValue) : resolvedValue
                return
            }
        }

        function ratioToPercent(value) {
            var numeric = Number(value)
            if (!isFinite(numeric)) {
                return value
            }
            return numeric <= 1 ? numeric * 100 : numeric
        }

        assignIfPresent("positionSize", ["positionSize"], ratioToPercent)
        assignIfPresent("stopLoss", ["stopLoss"], ratioToPercent)
        assignIfPresent("takeProfit", ["takeProfit"], ratioToPercent)
        assignIfPresent("maxDrawdownLimit", ["maxDrawdownLimit"], Number)
        assignIfPresent("rebalanceDays", ["rebalanceDays"], Number)

        if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.TrendFollowing) {
            assignIfPresent("fastPeriod", ["fastPeriod"], Number)
            assignIfPresent("slowPeriod", ["slowPeriod"], Number)
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.TrendBreakout) {
            assignIfPresent("longTrendPeriod", ["longTrendPeriod"], Number)
            assignIfPresent("breakoutLookbackPeriod", ["breakoutLookbackPeriod"], Number)
            assignIfPresent("breakoutThreshold", ["breakoutThreshold"], ratioToPercent)
            assignIfPresent("adxPeriod", ["adxPeriod"], Number)
            assignIfPresent("adxThreshold", ["adxThreshold"], Number)
            assignIfPresent("exitMaPeriod", ["exitMaPeriod"], Number)
            assignIfPresent("atrPeriod", ["atrPeriod"], Number)
            assignIfPresent("atrMultiplier", ["atrMultiplier"], Number)
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.MeanReversion) {
            assignIfPresent("bollPeriod", ["bollPeriod"], Number)
            assignIfPresent("bollStd", ["bollStd"], Number)
            assignIfPresent("reversionThreshold", ["reversionThreshold"], Number)
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.Momentum) {
            assignIfPresent("momentumPeriod", ["momentumPeriod"], Number)
            assignIfPresent("topN", ["topN"], Number)
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.Arbitrage) {
            assignIfPresent("spreadThreshold", ["spreadThreshold"], Number)
            assignIfPresent("entryZScore", ["entryZScore"], Number)
            assignIfPresent("exitZScore", ["exitZScore"], Number)
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.MachineLearning) {
            assignIfPresent("featureWindow", ["featureWindow"], Number)
            assignIfPresent("predictionDays", ["predictionDays"], Number)
            assignIfPresent("trainingDays", ["trainingDays"], Number)
            assignIfPresent("confidenceThreshold", ["confidenceThreshold"], ratioToPercent)
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.MultiFactor) {
            assignIfPresent("factorTypes", ["factorTypes"])
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.HighFrequency) {
            assignIfPresent("timeframe", ["timeframe"])
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.EventDriven) {
            assignIfPresent("eventTypes", ["eventTypes"])
        } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.Custom) {
            assignIfPresent("customCode", ["customCode"])
        }

        return mappedValues
    }

    function buildStrategyBacktestRuntimePayload(editableValues) {
        var source = toPlainJsValue(editableValues) || ({})
        var runtimeParameters = toPlainJsValue(strategyBacktestRuntimeBaseParameters) || ({})
        runtimeParameters.rule_profile = toPlainJsValue(runtimeParameters.rule_profile) || ({})

        function assignIfDefined(targetMap, key, value, transform) {
            if (value === undefined || value === null || value === "") {
                return
            }
            targetMap[key] = transform ? transform(value) : value
        }

        assignIfDefined(runtimeParameters, "positionSize", source.positionSize, normalizeBacktestPercentageToRatio)
        assignIfDefined(runtimeParameters, "initialCapital", source.initialCapital, Number)
        assignIfDefined(runtimeParameters, "commissionRate", source.commissionRate, Number)
        assignIfDefined(runtimeParameters, "slippageRate", source.slippageRate, Number)
        assignIfDefined(runtimeParameters.rule_profile, "stopLossPercent", source.stopLoss, normalizeBacktestPercentageToRatio)
        assignIfDefined(runtimeParameters.rule_profile, "takeProfitPercent", source.takeProfit, normalizeBacktestPercentageToRatio)
        assignIfDefined(runtimeParameters.rule_profile, "maxDrawdownLimit", source.maxDrawdownLimit, Number)
        assignIfDefined(runtimeParameters.rule_profile, "rebalanceDays", source.rebalanceDays, Number)
        assignIfDefined(runtimeParameters, "fastPeriod", source.fastPeriod, Number)
        assignIfDefined(runtimeParameters, "slowPeriod", source.slowPeriod, Number)
        assignIfDefined(runtimeParameters, "longTrendPeriod", source.longTrendPeriod, Number)
        assignIfDefined(runtimeParameters, "breakoutLookbackPeriod", source.breakoutLookbackPeriod, Number)
        assignIfDefined(runtimeParameters, "breakoutThreshold", source.breakoutThreshold, normalizeBacktestPercentageToRatio)
        assignIfDefined(runtimeParameters, "adxPeriod", source.adxPeriod, Number)
        assignIfDefined(runtimeParameters, "adxThreshold", source.adxThreshold, Number)
        assignIfDefined(runtimeParameters, "exitMaPeriod", source.exitMaPeriod, Number)
        assignIfDefined(runtimeParameters, "atrPeriod", source.atrPeriod, Number)
        assignIfDefined(runtimeParameters, "atrMultiplier", source.atrMultiplier, Number)
        assignIfDefined(runtimeParameters, "bollPeriod", source.bollPeriod, Number)
        assignIfDefined(runtimeParameters, "bollStd", source.bollStd, Number)
        assignIfDefined(runtimeParameters, "reversionThreshold", source.reversionThreshold, Number)
        assignIfDefined(runtimeParameters, "momentumPeriod", source.momentumPeriod, Number)
        assignIfDefined(runtimeParameters, "topN", source.topN, Number)
        assignIfDefined(runtimeParameters, "spreadThreshold", source.spreadThreshold, Number)
        assignIfDefined(runtimeParameters, "entryZScore", source.entryZScore, Number)
        assignIfDefined(runtimeParameters, "exitZScore", source.exitZScore, Number)
        assignIfDefined(runtimeParameters, "featureWindow", source.featureWindow, Number)
        assignIfDefined(runtimeParameters, "predictionDays", source.predictionDays, Number)
        assignIfDefined(runtimeParameters, "trainingDays", source.trainingDays, Number)
        assignIfDefined(runtimeParameters, "confidenceThreshold", source.confidenceThreshold, normalizeBacktestPercentageToRatio)
        assignIfDefined(runtimeParameters, "factorTypes", source.factorTypes)
        assignIfDefined(runtimeParameters, "timeframe", source.timeframe)
        assignIfDefined(runtimeParameters, "eventTypes", source.eventTypes)
        assignIfDefined(runtimeParameters, "customCode", source.customCode)

        return runtimeParameters
    }

    function buildStrategyBacktestExecutionStrategyData(editableValues) {
        var strategyDetail = toPlainJsValue(strategyBacktestSelectedDetail()) || ({})
        var strategyCopy = toPlainJsValue(strategyDetail) || ({})
        var parameters = toPlainJsValue(strategyCopy.parameters) || ({})
        var runtimeValues = toPlainJsValue(editableValues) || ({})
        var ruleProfile = toPlainJsValue(parameters.rule_profile) || ({})
        var executionPolicy = toPlainJsValue(parameters.execution_policy) || ({})
        var backtestAssumptions = toPlainJsValue(parameters.backtest_assumptions) || ({})
        var strategyScopeContext = toPlainJsValue(parameters.strategy_scope_context) || ({})
        function assignIfConfigured(target, key, value) {
            if (value === undefined || value === null || value === "") {
                return
            }
            target[key] = value
        }

        assignIfConfigured(backtestAssumptions, "initialCapital", Number(runtimeValues.initialCapital))
        assignIfConfigured(backtestAssumptions, "commissionRate", Number(runtimeValues.commissionRate))
        assignIfConfigured(backtestAssumptions, "slippageRate", Number(runtimeValues.slippageRate))
        assignIfConfigured(backtestAssumptions, "dataSourceMode", resolveStrategyBacktestRunContextDataSourceMode())
        assignIfConfigured(backtestAssumptions, "dataSourceDatasetId", strategyBacktestSelectedCacheDatasetId > 0 ? strategyBacktestSelectedCacheDatasetId : undefined)

        assignIfConfigured(strategyScopeContext, "universeType", strategyBacktestSelectedUniverseType === "index" ? 1 : 0)
        assignIfConfigured(strategyScopeContext, "selectedStrategyName", strategyBacktestSelectedName())
        if (strategyBacktestSelectedUniverseType === "index") {
            assignIfConfigured(strategyScopeContext, "universeId", strategyBacktestSelectedIndexSymbol)
            assignIfConfigured(strategyScopeContext, "indexSymbol", strategyBacktestSelectedIndexSymbol)
        }

        assignIfConfigured(ruleProfile, "stopLossPercent", normalizeBacktestPercentageToRatio(runtimeValues.stopLoss))
        assignIfConfigured(ruleProfile, "takeProfitPercent", normalizeBacktestPercentageToRatio(runtimeValues.takeProfit))
        assignIfConfigured(ruleProfile, "maxDrawdownLimit", Number(runtimeValues.maxDrawdownLimit))
        assignIfConfigured(ruleProfile, "maxPositionPercent", normalizeBacktestPercentageToRatio(runtimeValues.positionSize))

        assignIfConfigured(executionPolicy, "rebalanceDays", Number(runtimeValues.rebalanceDays))

        var strategyParamKeys = [
            "fastPeriod", "slowPeriod", "longTrendPeriod", "breakoutLookbackPeriod", "breakoutThreshold",
            "adxPeriod", "adxThreshold", "exitMaPeriod", "atrPeriod", "atrMultiplier", "bollPeriod",
            "bollStd", "reversionThreshold", "momentumPeriod", "topN", "spreadThreshold", "entryZScore",
            "exitZScore", "featureWindow", "predictionDays", "trainingDays", "confidenceThreshold",
            "factorTypes", "timeframe", "eventTypes", "customCode"
        ]
        for (var index = 0; index < strategyParamKeys.length; ++index) {
            var strategyParamKey = strategyParamKeys[index]
            assignIfConfigured(parameters, strategyParamKey, runtimeValues[strategyParamKey])
        }

        parameters.rule_profile = ruleProfile
        parameters.execution_policy = executionPolicy
        parameters.backtest_assumptions = backtestAssumptions
        parameters.strategy_scope_context = strategyScopeContext
        strategyCopy.parameters = parameters
        return strategyCopy
    }

    function buildStrategyBacktestUiContext() {
        var windowSelection = resolveStrategyBacktestWindowSelection()
        var context = {
            startDate: windowSelection.startDate,
            endDate: windowSelection.endDate,
            dataSourceMode: resolveStrategyBacktestRunContextDataSourceMode()
        }

        if (strategyBacktestSelectedDataSourceMode === 1 && strategyBacktestSelectedCacheDatasetId > 0) {
            context.dataSourceDatasetId = strategyBacktestSelectedCacheDatasetId
        }

        return context
    }

    function buildStrategyBacktestRecordContext(runtimeParameters) {
        var windowSelection = resolveStrategyBacktestWindowSelection()
        return {
            selectedStrategyId: selectedStrategyId,
            selectedStrategyName: strategyBacktestSelectedName(),
            selectedUniverseType: strategyBacktestSelectedUniverseType,
            selectedIndexSymbol: strategyBacktestSelectedIndexSymbol,
            universeLabel: strategyBacktestSelectedUniverseType === "index" ? "指数成分" : "全市场",
            indexLabel: strategyBacktestSelectedUniverseType === "index" ? strategyBacktestSelectedIndexSymbol : "",
            dataSourceMode: strategyBacktestSelectedDataSourceMode === 1 ? "cache" : "raw",
            dataSourceDatasetId: strategyBacktestSelectedCacheDatasetId > 0 ? strategyBacktestSelectedCacheDatasetId : undefined,
            startDate: windowSelection.startDate,
            endDate: windowSelection.endDate,
            runtimeParameters: runtimeParameters
        }
    }

    function buildStrategyBacktestStrategyOptions() {
        var options = []
        if (!strategyViewModel) {
            return options
        }

        for (var index = 0; index < strategyViewModel.count; ++index) {
            var row = strategyViewModel.getRow(index)
            if (!row) {
                continue
            }

            var strategyId = row.strategyId || ""
            var strategyName = row.strategyName || row.name || "未命名策略"
            options.push({
                strategyId: strategyId,
                id: strategyId,
                value: strategyId,
                displayText: strategyName,
                label: strategyName
            })
        }

        return options
    }

    function findStrategyOptionIndex(options, strategyId) {
        var normalizedId = String(strategyId || "").trim()
        if (!normalizedId || !Array.isArray(options)) {
            return -1
        }

        for (var index = 0; index < options.length; ++index) {
            var option = options[index]
            var optionId = option ? String(option.strategyId || option.value || "") : ""
            if (optionId === normalizedId) {
                return index
            }
        }

        return -1
    }

    function resolveStrategyBacktestDataSourceModeValue(rawValue) {
        var numeric = Number(rawValue)
        if (isFinite(numeric)) {
            var rounded = Math.round(numeric)
            if (rounded === 0) {
                return 0
            }
            if (rounded === 1 || rounded === 2) {
                return 1
            }
        }

        var normalized = String(rawValue || "").trim().toLowerCase()
        if (normalized === "raw") {
            return 0
        }
        if (normalized === "cleaned" || normalized === "cachedataset" || normalized === "cache_dataset" || normalized === "cache") {
            return 1
        }
        return 0
    }

    function strategyBacktestSelectedSummary() {
        return getSelectedStrategySummary()
    }

    function strategyBacktestSelectedDetail() {
        var summary = strategyBacktestSelectedSummary()
        var strategyId = summary ? String(summary.strategyId || "") : ""
        return strategyId ? getSelectedStrategyDetail(strategyId) : ({})
    }

    function strategyBacktestSelectedName() {
        var summary = strategyBacktestSelectedSummary()
        return summary ? String(summary.strategyName || summary.name || "") : ""
    }

    function strategyBacktestContextMessage() {
        if (!hasSelectedStrategy) {
            return "请先选择一个策略。"
        }
        if (strategyBacktestRunning) {
            return strategyBacktestStatusText || "策略回测运行中。"
        }
        if (strategyBacktestSelectedDataSourceMode === 1) {
            if (strategyBacktestSelectedCacheDatasetId > 0) {
                var windowSelection = resolveStrategyBacktestWindowSelection()
                return "缓存K线回测将直接使用清洗数据窗口 "
                    + String(windowSelection.startDate || "--") + " ~ " + String(windowSelection.endDate || "--")
            }
            return "缓存K线回测必须先选择清洗数据，日期将自动沿用数据集窗口。"
        }
        return "原始K线回测使用你选择的日期区间，并由 bridge 统一解析交易日窗口、symbolId 和 typed runContext。"
    }

    function syncStrategyBacktestEditor() {
        if (!hasSelectedStrategy) {
            strategyBacktestRuntimeBaseParameters = ({})
            strategyBacktestParameterValues = ({ dataSourceMode: strategyBacktestSelectedDataSourceMode })
            strategyBacktestDynamicParamConfigs = []
            strategyBacktestDynamicParamGroups = []
            strategyBacktestSelectedCacheDatasetId = -1
            strategyBacktestSelectedCacheDatasetIndex = 0
            strategyBacktestParametersLoaded = false
            return
        }

        var strategyDetail = strategyBacktestSelectedDetail()
        var latestBacktest = getLatestBacktestRecord(strategyDetail)
        var sourceParameters = buildStrategyBacktestParameterSource(strategyDetail, latestBacktest)
        var strategyTypeIndex = resolveStrategyBacktestTypeIndex(strategyDetail)
        var dynamicConfigs = buildStrategyBacktestDynamicParamConfigs(strategyTypeIndex)
        var universeContext = StructureAdapter.resolveUniverseContext(Object.keys(latestBacktest || ({})).length > 0 ? latestBacktest : strategyDetail)
        var assumptions = StructureAdapter.resolveBacktestAssumptions(sourceParameters)
        var dataSourceMode = resolveStrategyBacktestDataSourceModeValue(latestBacktest.dataSourceMode || assumptions.dataSourceMode)
        var dataSourceDatasetId = positiveDatasetId(latestBacktest.dataSourceDatasetId || assumptions.dataSourceDatasetId || sourceParameters.dataSourceDatasetId)

        strategyBacktestRuntimeBaseParameters = toPlainJsValue(sourceParameters) || ({})
        strategyBacktestDynamicParamConfigs = dynamicConfigs
        strategyBacktestDynamicParamGroups = buildStrategyBacktestDynamicParamGroups(dynamicConfigs)
        strategyBacktestSelectedUniverseType = universeContext.universeType === "index" ? "index" : "market"
        strategyBacktestSelectedIndexSymbol = String(universeContext.indexSymbol || "000300.SH").trim() || "000300.SH"
        strategyBacktestSelectedStartDate = normalizeBacktestDateText(
            latestBacktest.startDate || assumptions.startDate,
            isoDateDaysAgo(365))
        strategyBacktestSelectedEndDate = normalizeBacktestDateText(
            latestBacktest.endDate || assumptions.endDate,
            isoDateDaysAgo(0))
        strategyBacktestSelectedDataSourceMode = dataSourceMode
        strategyBacktestSelectedCacheDatasetId = dataSourceDatasetId
        syncStrategyBacktestSelectedDatasetIndex()
        strategyBacktestParameterValues = buildStrategyBacktestRuntimeParameters(strategyTypeIndex, sourceParameters)
        strategyBacktestParameterValues.dataSourceMode = dataSourceMode
        strategyBacktestParametersLoaded = true
    }

    function showActionFeedback(message, isError) {
        actionFeedbackMessage = String(message || "")
        actionFeedbackError = isError === true
        actionFeedbackDialog.open()
    }

    function requestStrategyBacktestPreview() {
        if (!strategyService || !strategyService.buildStrategyBacktestRunContextCandidate) {
            showActionFeedback("StrategyService 未提供策略回测上下文解析入口。", true)
            return
        }

        var strategyData = buildStrategyBacktestExecutionStrategyData(strategyBacktestParameterValues)
        var candidate = strategyService.buildStrategyBacktestRunContextCandidate(strategyData, buildStrategyBacktestUiContext())
        if (!candidate || !candidate.ok) {
            showActionFeedback(formatStrategyBacktestCandidateError(candidate), true)
            return
        }

        var previewResult = strategyService.buildStrategyBacktestRequestPreview(strategyData, candidate.runContext)
        if (!previewResult || !previewResult.ok) {
            showActionFeedback("策略回测请求预览失败，错误码: " + String((previewResult || ({})).errorCode), true)
            return
        }

        var request = previewResult.request || ({})
        showActionFeedback(
            "请求预览成功。universeId=" + String(request.universeId || "--")
            + "，layerId=" + String((request.layers && request.layers.length > 0 && request.layers[0].layerId) ? request.layers[0].layerId : "--")
            + "，window=" + String(request.windowStartDay || "--") + " ~ " + String(request.windowEndDay || "--"),
            false)
    }

    function requestStrategyBacktestStart() {
        if (!strategyService || !strategyService.buildStrategyBacktestRunContextCandidate || !strategyService.launchStrategyBacktest) {
            showActionFeedback("StrategyService 未提供策略回测运行入口。", true)
            return
        }

        var runtimePayload = buildStrategyBacktestRuntimePayload(strategyBacktestParameterValues)
        var strategyData = buildStrategyBacktestExecutionStrategyData(strategyBacktestParameterValues)
        var backtestContext = buildStrategyBacktestRecordContext(runtimePayload)
        var candidate = strategyService.buildStrategyBacktestRunContextCandidate(strategyData, buildStrategyBacktestUiContext())
        if (!candidate || !candidate.ok) {
            showActionFeedback(formatStrategyBacktestCandidateError(candidate), true)
            return
        }

        var launchResult = strategyService.launchStrategyBacktest(strategyData, candidate.runContext)
        if (!launchResult || !launchResult.ok) {
            showActionFeedback("启动策略回测失败，错误码: " + String((launchResult || ({})).errorCode), true)
            return
        }

        strategyBacktestPendingContext = backtestContext
        strategyBacktestPendingStrategyData = strategyData
        strategyBacktestActiveHandle = launchResult.handle || ({})
        strategyBacktestRunning = true
        strategyBacktestCompletionRatio = 0
        strategyBacktestStatusText = "策略回测任务已提交，等待首个进度快照。"
        strategyBacktestProgressTimer.start()
    }

    function getStrategyPerformanceMetrics(strategyDetail) {
        if (!strategyDetail) {
            return ({})
        }

        return strategyDetail.performanceMetrics || ({})
    }

    function getLatestBacktestRecord(strategyDetail) {
        var performance = getStrategyPerformanceMetrics(strategyDetail)
        return performance.latestBacktest || ({})
    }

    function getBacktestHistory(strategyDetail) {
        var performance = getStrategyPerformanceMetrics(strategyDetail)
        return performance.backtestHistory || []
    }

    function formatBacktestPercentValue(value, decimals) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return number.toFixed(decimals === undefined ? 2 : decimals) + "%"
    }

    function formatBacktestNumberValue(value, decimals) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return number.toFixed(decimals === undefined ? 2 : decimals)
    }

    function formatBacktestIntegerValue(value) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return Math.round(number).toString()
    }

    function buildLatestBacktestItems(strategyDetail) {
        var latest = getLatestBacktestRecord(strategyDetail)
        var summary = latest.summary || ({})
        var universeContext = StructureAdapter.resolveUniverseContext(latest)
        var assumptions = StructureAdapter.resolveBacktestAssumptions(latest)
        if (!latest || Object.keys(latest).length === 0) {
            return []
        }

        return [
            { label: "回测时间", value: latest.recordedAt || "--" },
            { label: "回测范围", value: latest.universeLabel || universeContext.universeType || "--" },
            { label: "指数", value: latest.indexLabel || universeContext.indexSymbol || "--" },
            { label: "数据源", value: latest.dataSourceMode || assumptions.dataSourceMode || "--" },
            { label: "区间", value: (latest.startDate || assumptions.startDate || "--") + " ~ " + (latest.endDate || assumptions.endDate || "--") },
            { label: "总收益", value: formatBacktestPercentValue(summary.returns, 2) },
            { label: "最大回撤", value: formatBacktestPercentValue(summary.maxDrawdown, 2) },
            { label: "夏普比率", value: formatBacktestNumberValue(summary.sharpeRatio, 2) },
            { label: "胜率", value: formatBacktestPercentValue(summary.winRate, 2) },
            { label: "交易次数", value: formatBacktestIntegerValue(summary.tradesCount) },
            { label: "运行天数", value: formatBacktestIntegerValue(summary.runningDays) },
            { label: "净值点数", value: formatBacktestIntegerValue(latest.equityPointCount) }
        ]
    }

    function calculateMetricAxisBounds(history, metricKey, fallbackMin, fallbackMax) {
        if (!history || history.length === 0) {
            return { min: fallbackMin, max: fallbackMax }
        }

        var minValue = 0
        var maxValue = 0
        var initialized = false
        for (var index = 0; index < history.length; ++index) {
            var summary = history[index] && history[index].summary ? history[index].summary : ({})
            var currentValue = Number(summary[metricKey])
            if (isNaN(currentValue)) {
                continue
            }

            if (!initialized) {
                minValue = currentValue
                maxValue = currentValue
                initialized = true
            } else {
                minValue = Math.min(minValue, currentValue)
                maxValue = Math.max(maxValue, currentValue)
            }
        }

        if (!initialized) {
            return { min: fallbackMin, max: fallbackMax }
        }

        if (minValue === maxValue) {
            var singlePadding = Math.max(Math.abs(minValue) * 0.08, metricKey === "sharpeRatio" ? 0.2 : 1)
            return { min: minValue - singlePadding, max: maxValue + singlePadding }
        }

        var padding = (maxValue - minValue) * 0.1
        return { min: minValue - padding, max: maxValue + padding }
    }

    function updateHistoryMetricSeries(series, history, metricKey) {
        if (!series) {
            return
        }

        series.clear()
        if (!history) {
            return
        }

        for (var index = 0; index < history.length; ++index) {
            var summary = history[index] && history[index].summary ? history[index].summary : ({})
            var currentValue = Number(summary[metricKey])
            if (!isNaN(currentValue)) {
                series.append(index, currentValue)
            }
        }
    }

    function warmupPage() {
        initializeStrategyViewModel()
        initializeStrategyBacktestCleanedData()
        syncSelectedStrategy()
        syncStrategyBacktestEditor()
    }

    Connections {
        target: cleanedDataController
        ignoreUnknownSignals: true

        function onDatasetListChanged() {
            strategyBacktestPage.rebuildStrategyBacktestCacheDatasetOptions()
        }

        function onSelectedDatasetChanged() {
            if (!cleanedDataController || !cleanedDataController.selectedDatasetInfo) {
                return
            }
            strategyBacktestPage.applySelectedCleanedDataset(cleanedDataController.selectedDatasetInfo)
        }
    }

    function ensurePageServicesReady() {
        if (pageServicesReady || !visible) {
            return
        }

        pageServicesReady = true
        warmupPage()
    }

    ListModel {
        id: strategyVisibleModel
    }

    Timer {
        id: strategyBacktestActivationTimer
        interval: 0
        repeat: false
        onTriggered: strategyBacktestPage.ensurePageServicesReady()
    }

    Timer {
        id: strategyBacktestProgressTimer
        interval: 800
        repeat: true
        running: false
        onTriggered: {
            if (!strategyBacktestRunning || !strategyService || !strategyService.pollStrategyBacktestProgress) {
                strategyBacktestProgressTimer.stop()
                return
            }

            var handleRunId = Number((strategyBacktestActiveHandle || ({})).handleRunId || 0)
            if (!(handleRunId > 0)) {
                strategyBacktestRunning = false
                strategyBacktestProgressTimer.stop()
                showActionFeedback("策略回测句柄无效。", true)
                return
            }

            var progressResult = strategyService.pollStrategyBacktestProgress(handleRunId)
            if (!progressResult || !progressResult.ok) {
                strategyBacktestRunning = false
                strategyBacktestProgressTimer.stop()
                showActionFeedback("策略回测进度轮询失败，错误码: " + String((progressResult || ({})).errorCode), true)
                return
            }

            var progress = progressResult.progress || ({})
            var completionRatio = Number(progress.completionRatio || 0)
            strategyBacktestCompletionRatio = isFinite(completionRatio)
                ? Math.max(0, Math.min(1, completionRatio))
                : 0

            var currentTradingDay = String(progress.currentTradingDay || "--")
            var completedTradingDays = Number(progress.completedTradingDays || 0)
            var totalTradingDays = Number(progress.totalTradingDays || 0)
            if (strategyBacktestCompletionRatio > 0) {
                strategyBacktestStatusText = "策略回测运行中，已完成 "
                    + Math.round(strategyBacktestCompletionRatio * 100) + "%"
                    + (totalTradingDays > 0 ? ("（" + completedTradingDays + "/" + totalTradingDays + "）") : "")
                    + "，当前交易日索引: " + currentTradingDay
            } else if (totalTradingDays > 0) {
                strategyBacktestStatusText = "策略回测运行中，已处理 " + completedTradingDays + "/" + totalTradingDays
                    + " 个交易日，当前交易日索引: " + currentTradingDay
            } else {
                strategyBacktestStatusText = "策略回测任务已提交，等待首个进度快照。"
            }

            if (strategyBacktestCompletionRatio < 1) {
                return
            }

            var collectionResult = strategyService.collectStrategyBacktestResult(handleRunId)
            if (!collectionResult || !collectionResult.ok) {
                return
            }

            var collection = collectionResult.collection || ({})
            if (!collection.hasResult) {
                return
            }

            strategyBacktestProgressTimer.stop()
            strategyBacktestRunning = false
            strategyBacktestLastResult = collection.result || ({})
            strategyService.recordStrategyBacktestResult(selectedStrategyId, strategyBacktestLastResult, strategyBacktestPendingContext)
            syncSelectedStrategy()
            syncStrategyBacktestEditor()
            showActionFeedback("策略回测完成，结果已写入当前策略的最近一次回测。", false)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        NavigationComponents.ModeTitleBar {
            Layout.fillWidth: true
            visible: !embeddedMode
            currentMode: "backtest"
            showBackButton: false
            modeOptions: []
            modeTitleMap: {
                "backtest": "策略回测"
            }
            modeSubtitleMap: {
                "backtest": "选择策略、配置回测参数，并查看最近结果与历史对比。"
            }
        }

        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: Math.max(0, Math.min(contentMaxWidth, scrollView.width - pageSidePadding * 2))
                x: Math.max(0, (scrollView.width - width) / 2)
                spacing: spacingLarge

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    implicitHeight: workspaceColumn.implicitHeight + 40
                    Layout.preferredHeight: implicitHeight
                    radius: borderRadiusXLarge
                    color: secondaryBg
                    border.color: Qt.rgba(71 / 255, 85 / 255, 105 / 255, 0.22)

                    ColumnLayout {
                        id: workspaceColumn
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: spacingMedium

                        Text {
                            text: "回测工作台"
                            font.pixelSize: fontSizeLarge
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: !hasSelectedStrategy
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1
                            implicitHeight: emptyStrategyContent.implicitHeight + 32

                            Text {
                                id: emptyStrategyContent
                                anchors.fill: parent
                                anchors.margins: 16
                                text: "请先从上方策略列表中选择一个策略，再配置回测参数。"
                                wrapMode: Text.WordWrap
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: fontSizeNormal
                                color: textTertiary
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: hasSelectedStrategy
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1
                            implicitHeight: summaryColumn.implicitHeight + 24

                            ColumnLayout {
                                id: summaryColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                property var selectedStrategySummary: strategyBacktestPage.getSelectedStrategySummary()
                                property var selectedStrategyDetail: strategyBacktestPage.strategyBacktestSelectedDetail()
                                property var latestBacktest: strategyBacktestPage.getLatestBacktestRecord(selectedStrategyDetail)

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: selectedStrategySummary
                                            ? (selectedStrategySummary.strategyName || selectedStrategySummary.name || "未命名策略")
                                            : "未选择策略"
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                        color: textPrimary
                                    }

                                    Rectangle {
                                        radius: 999
                                        color: "#1E40AF"
                                        implicitWidth: strategyIdText.implicitWidth + 16
                                        implicitHeight: strategyIdText.implicitHeight + 8

                                        Text {
                                            id: strategyIdText
                                            anchors.centerIn: parent
                                            text: "ID " + String(strategyBacktestPage.selectedStrategyId || "--")
                                            font.pixelSize: 11
                                            color: "white"
                                        }
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: latestBacktest && Object.keys(latestBacktest).length > 0
                                            ? ("最近回测: " + String(latestBacktest.recordedAt || "--"))
                                            : "当前还没有回测记录"
                                        font.pixelSize: 12
                                        color: textSecondary
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: summaryColumn.selectedStrategyDetail && Object.keys(summaryColumn.selectedStrategyDetail).length > 0
                                        ? strategyBacktestPage.currentStrategyDescription(summaryColumn.selectedStrategyDetail)
                                        : strategyBacktestPage.currentStrategyDescription(summaryColumn.selectedStrategySummary)
                                    font.pixelSize: 12
                                    color: textSecondary
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        BacktestComponents.BacktestParameterPanel {
                            Layout.fillWidth: true
                            strategyOptions: strategyBacktestPage.buildStrategyBacktestStrategyOptions()
                            universeOptions: strategyBacktestPage.strategyBacktestUniverseOptions
                            indexPoolOptions: strategyBacktestPage.strategyBacktestIndexOptions
                            dataSourceOptions: strategyBacktestPage.strategyBacktestDataSourceOptions
                            cacheDatasetOptions: strategyBacktestPage.strategyBacktestCacheDatasetOptions
                            selectedStrategyName: strategyBacktestPage.strategyBacktestSelectedName()
                            selectedStrategyId: strategyBacktestPage.selectedStrategyId
                            selectedUniverseType: strategyBacktestPage.strategyBacktestSelectedUniverseType
                            selectedIndexSymbol: strategyBacktestPage.strategyBacktestSelectedIndexSymbol
                            selectedStartDate: strategyBacktestPage.strategyBacktestSelectedStartDate
                            selectedEndDate: strategyBacktestPage.strategyBacktestSelectedEndDate
                            dynamicParamConfigs: strategyBacktestPage.strategyBacktestDynamicParamConfigs
                            dynamicParamGroups: strategyBacktestPage.strategyBacktestDynamicParamGroups
                            dynamicParamValues: strategyBacktestPage.strategyBacktestParameterValues
                            parametersLoaded: strategyBacktestPage.strategyBacktestParametersLoaded
                            dateSelectionEnabled: strategyBacktestPage.strategyBacktestSelectedDataSourceMode !== 1
                            paramRegistry: paramComponents
                            selectedCacheDatasetIndex: strategyBacktestPage.strategyBacktestSelectedCacheDatasetIndex
                            showStrategySelector: true
                            showDataSourceSelector: true
                            showCacheDatasetSelector: strategyBacktestPage.strategyBacktestSelectedDataSourceMode === 1

                            onStrategyOptionSelected: function(index, option) {
                                if (option && option.strategyId) {
                                    strategyBacktestPage.selectStrategyById(option.strategyId)
                                }
                            }

                            onUniverseOptionSelected: function(index, option) {
                                strategyBacktestPage.strategyBacktestSelectedUniverseType = String((option || ({})).value || "market")
                            }

                            onIndexOptionSelected: function(index, option) {
                                strategyBacktestPage.strategyBacktestSelectedIndexSymbol = String((option || ({})).value || "000300.SH")
                            }

                            onDataSourceOptionSelected: function(index, option) {
                                var modeValue = Number((option || ({})).value)
                                strategyBacktestPage.strategyBacktestSelectedDataSourceMode = isFinite(modeValue) ? modeValue : 0
                            }

                            onCacheDatasetOptionSelected: function(index, option) {
                                strategyBacktestPage.selectStrategyBacktestCacheDatasetAt(index)
                            }

                            onStartDateSelected: function(dateText) {
                                strategyBacktestPage.strategyBacktestSelectedStartDate = dateText
                            }

                            onEndDateSelected: function(dateText) {
                                strategyBacktestPage.strategyBacktestSelectedEndDate = dateText
                            }

                            onDynamicParamsChanged: function(newValues) {
                                var nextValues = strategyBacktestPage.toPlainJsValue(newValues) || ({})
                                nextValues.dataSourceMode = strategyBacktestPage.strategyBacktestSelectedDataSourceMode
                                strategyBacktestPage.strategyBacktestParameterValues = nextValues
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 8
                            color: "#0F172A"
                            border.width: 1
                            border.color: "#334155"
                            implicitHeight: actionColumn.implicitHeight + 24

                            ColumnLayout {
                                id: actionColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Button {
                                        text: "预览回测请求"
                                        enabled: hasSelectedStrategy && !strategyBacktestRunning
                                        onClicked: strategyBacktestPage.requestStrategyBacktestPreview()
                                    }

                                    Button {
                                        text: strategyBacktestRunning ? "回测运行中" : "启动策略回测"
                                        enabled: hasSelectedStrategy && !strategyBacktestRunning
                                        onClicked: strategyBacktestPage.requestStrategyBacktestStart()
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: strategyBacktestPage.strategyBacktestContextMessage()
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 12
                                        color: "#94A3B8"
                                    }
                                }

                                ProgressBar {
                                    Layout.fillWidth: true
                                    visible: strategyBacktestRunning || strategyBacktestCompletionRatio > 0
                                    from: 0
                                    to: 1
                                    value: Math.max(0, Math.min(1, strategyBacktestCompletionRatio))
                                    indeterminate: strategyBacktestRunning && strategyBacktestCompletionRatio <= 0
                                }

                                Text {
                                    visible: strategyBacktestRunning || strategyBacktestStatusText.length > 0
                                    text: strategyBacktestRunning
                                        ? (strategyBacktestStatusText + (strategyBacktestCompletionRatio > 0
                                            ? (" · " + Math.round(strategyBacktestCompletionRatio * 100) + "%")
                                            : ""))
                                        : strategyBacktestStatusText
                                    font.pixelSize: 12
                                    color: strategyBacktestRunning ? accentBlue : textSecondary
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: hasSelectedStrategy
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1
                            implicitHeight: latestBacktestColumn.implicitHeight + 24

                            ColumnLayout {
                                id: latestBacktestColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                property var selectedStrategyDetail: strategyBacktestPage.strategyBacktestSelectedDetail()
                                property var latestBacktestItems: strategyBacktestPage.buildLatestBacktestItems(selectedStrategyDetail)

                                Text {
                                    text: "最近一次回测"
                                    font.pixelSize: fontSizeNormal + 1
                                    font.weight: Font.DemiBold
                                    color: textPrimary
                                }

                                Button {
                                    text: strategyBacktestPage.latestBacktestExpanded ? "收起" : "展开"
                                    visible: latestBacktestColumn.latestBacktestItems.length > 0
                                    onClicked: strategyBacktestPage.latestBacktestExpanded = !strategyBacktestPage.latestBacktestExpanded
                                }

                                Text {
                                    visible: latestBacktestColumn.latestBacktestItems.length === 0
                                    text: "当前策略还没有可展示的回测记录。"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                GridLayout {
                                    visible: strategyBacktestPage.latestBacktestExpanded && latestBacktestColumn.latestBacktestItems.length > 0
                                    Layout.fillWidth: true
                                    columns: 4
                                    columnSpacing: 10
                                    rowSpacing: 8

                                    Repeater {
                                        model: latestBacktestColumn.latestBacktestItems

                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 44
                                            radius: 8
                                            color: "#0B1220"

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 2

                                                Text {
                                                    text: modelData.label
                                                    font.pixelSize: 11
                                                    color: textTertiary
                                                }

                                                Text {
                                                    text: modelData.value
                                                    font.pixelSize: 13
                                                    font.weight: Font.Medium
                                                    color: textPrimary
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    visible: hasSelectedStrategy
                    radius: borderRadiusXLarge
                    color: secondaryBg
                    border.color: Qt.rgba(71 / 255, 85 / 255, 105 / 255, 0.22)
                    implicitHeight: historySection.implicitHeight + 40
                    Layout.preferredHeight: implicitHeight

                    ColumnLayout {
                        id: historySection
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: spacingMedium

                        property var selectedStrategyDetail: strategyBacktestPage.strategyBacktestSelectedDetail()
                        property var backtestHistory: strategyBacktestPage.getBacktestHistory(selectedStrategyDetail)
                        property var visibleBacktestHistory: strategyBacktestPage.resolveVisibleBacktestHistory(backtestHistory)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "回测历史"
                                font.pixelSize: fontSizeLarge
                                font.weight: Font.DemiBold
                                color: textPrimary
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: historySection.backtestHistory.length > 0
                                    ? ("最近 " + historySection.backtestHistory.length + " 条")
                                    : "暂无历史"
                                font.pixelSize: 12
                                color: textSecondary
                            }

                            Button {
                                visible: historySection.backtestHistory.length > 0 && strategyBacktestPage.historySectionExpanded
                                text: strategyBacktestPage.historyChartsExpanded ? "收起图表" : "展开图表"
                                onClicked: strategyBacktestPage.historyChartsExpanded = !strategyBacktestPage.historyChartsExpanded
                            }

                            Button {
                                visible: historySection.backtestHistory.length > 0
                                text: strategyBacktestPage.historySectionExpanded ? "收起历史" : "展开历史"
                                onClicked: strategyBacktestPage.historySectionExpanded = !strategyBacktestPage.historySectionExpanded
                            }

                            Button {
                                visible: historySection.backtestHistory.length > strategyBacktestPage.historyPreviewCount && strategyBacktestPage.historySectionExpanded
                                text: strategyBacktestPage.historyListExpanded ? "收起历史" : "展开全部历史"
                                onClicked: strategyBacktestPage.historyListExpanded = !strategyBacktestPage.historyListExpanded
                            }
                        }

                        Text {
                            visible: historySection.backtestHistory.length === 0
                            text: "这里会保留不同回测范围、不同日期区间的回测摘要，便于横向比较。"
                            font.pixelSize: fontSizeNormal
                            color: textTertiary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Loader {
                            active: strategyBacktestPage.historySectionExpanded && historySection.backtestHistory.length > 0 && strategyBacktestPage.historyChartsExpanded
                            visible: active
                            Layout.fillWidth: true
                            sourceComponent: Rectangle {
                                radius: 10
                                color: "#0B1220"
                                border.color: "#1E293B"
                                border.width: 1
                                implicitHeight: Math.max(240, Math.min(420, width * 0.32))

                                ColumnLayout {
                                    id: chartsColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 12

                                    function refreshCharts() {
                                        strategyBacktestPage.updateHistoryMetricSeries(historyReturnsSeries, historySection.backtestHistory, "returns")
                                        strategyBacktestPage.updateHistoryMetricSeries(historyDrawdownSeries, historySection.backtestHistory, "maxDrawdown")
                                        strategyBacktestPage.updateHistoryMetricSeries(historySharpeSeries, historySection.backtestHistory, "sharpeRatio")
                                    }

                                    Component.onCompleted: refreshCharts()

                                    GridLayout {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        columns: 3
                                        columnSpacing: 12
                                        rowSpacing: 12

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            radius: 8
                                            color: "#111827"

                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                spacing: 8

                                                Text {
                                                    text: "收益对比(%)"
                                                    font.pixelSize: 12
                                                    font.weight: Font.Medium
                                                    color: textPrimary
                                                }

                                                ChartView {
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                                    antialiasing: true
                                                    legend.visible: false
                                                    backgroundColor: "transparent"
                                                    plotAreaColor: "transparent"

                                                    ValueAxis {
                                                        id: historyReturnsAxisX
                                                        min: 0
                                                        max: Math.max(1, historyReturnsSeries.count > 0 ? historyReturnsSeries.count - 1 : 1)
                                                        tickCount: Math.min(6, Math.max(2, historyReturnsSeries.count > 1 ? 6 : 2))
                                                        labelsColor: textTertiary
                                                        gridLineColor: "#1E293B"
                                                        lineVisible: false
                                                    }

                                                    ValueAxis {
                                                        id: historyReturnsAxisY
                                                        min: strategyBacktestPage.calculateMetricAxisBounds(historySection.backtestHistory, "returns", -5, 5).min
                                                        max: strategyBacktestPage.calculateMetricAxisBounds(historySection.backtestHistory, "returns", -5, 5).max
                                                        tickCount: 5
                                                        labelsColor: textSecondary
                                                        gridLineColor: "#1E293B"
                                                        labelFormat: "%.1f"
                                                    }

                                                    LineSeries {
                                                        id: historyReturnsSeries
                                                        axisX: historyReturnsAxisX
                                                        axisY: historyReturnsAxisY
                                                        color: riseRed
                                                        width: 2
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            radius: 8
                                            color: "#111827"

                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                spacing: 8

                                                Text {
                                                    text: "回撤对比(%)"
                                                    font.pixelSize: 12
                                                    font.weight: Font.Medium
                                                    color: textPrimary
                                                }

                                                ChartView {
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                                    antialiasing: true
                                                    legend.visible: false
                                                    backgroundColor: "transparent"
                                                    plotAreaColor: "transparent"

                                                    ValueAxis {
                                                        id: historyDrawdownAxisX
                                                        min: 0
                                                        max: Math.max(1, historyDrawdownSeries.count > 0 ? historyDrawdownSeries.count - 1 : 1)
                                                        tickCount: Math.min(6, Math.max(2, historyDrawdownSeries.count > 1 ? 6 : 2))
                                                        labelsColor: textTertiary
                                                        gridLineColor: "#1E293B"
                                                        lineVisible: false
                                                    }

                                                    ValueAxis {
                                                        id: historyDrawdownAxisY
                                                        min: strategyBacktestPage.calculateMetricAxisBounds(historySection.backtestHistory, "maxDrawdown", 0, 10).min
                                                        max: strategyBacktestPage.calculateMetricAxisBounds(historySection.backtestHistory, "maxDrawdown", 0, 10).max
                                                        tickCount: 5
                                                        labelsColor: textSecondary
                                                        gridLineColor: "#1E293B"
                                                        labelFormat: "%.1f"
                                                    }

                                                    LineSeries {
                                                        id: historyDrawdownSeries
                                                        axisX: historyDrawdownAxisX
                                                        axisY: historyDrawdownAxisY
                                                        color: warningAmber
                                                        width: 2
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            radius: 8
                                            color: "#111827"

                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                spacing: 8

                                                Text {
                                                    text: "夏普对比"
                                                    font.pixelSize: 12
                                                    font.weight: Font.Medium
                                                    color: textPrimary
                                                }

                                                ChartView {
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                                    antialiasing: true
                                                    legend.visible: false
                                                    backgroundColor: "transparent"
                                                    plotAreaColor: "transparent"

                                                    ValueAxis {
                                                        id: historySharpeAxisX
                                                        min: 0
                                                        max: Math.max(1, historySharpeSeries.count > 0 ? historySharpeSeries.count - 1 : 1)
                                                        tickCount: Math.min(6, Math.max(2, historySharpeSeries.count > 1 ? 6 : 2))
                                                        labelsColor: textTertiary
                                                        gridLineColor: "#1E293B"
                                                        lineVisible: false
                                                    }

                                                    ValueAxis {
                                                        id: historySharpeAxisY
                                                        min: strategyBacktestPage.calculateMetricAxisBounds(historySection.backtestHistory, "sharpeRatio", -1, 1).min
                                                        max: strategyBacktestPage.calculateMetricAxisBounds(historySection.backtestHistory, "sharpeRatio", -1, 1).max
                                                        tickCount: 5
                                                        labelsColor: textSecondary
                                                        gridLineColor: "#1E293B"
                                                        labelFormat: "%.2f"
                                                    }

                                                    LineSeries {
                                                        id: historySharpeSeries
                                                        axisX: historySharpeAxisX
                                                        axisY: historySharpeAxisY
                                                        color: "#38BDF8"
                                                        width: 2
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Column {
                            visible: strategyBacktestPage.historySectionExpanded && historySection.visibleBacktestHistory.length > 0
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: historySection.visibleBacktestHistory

                                delegate: Rectangle {
                                    width: parent.width
                                    radius: 10
                                    color: "#0B1220"
                                    border.color: "#1E293B"
                                    border.width: 1
                                    implicitHeight: historyContent.implicitHeight + 24

                                    Column {
                                        id: historyContent
                                        x: 12
                                        y: 12
                                        width: parent.width - 24
                                        spacing: 8

                                        property var summary: modelData.summary || ({})

                                        RowLayout {
                                            width: parent.width

                                            Text {
                                                text: modelData.recordedAt || ("历史记录 #" + String(index + 1))
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: textPrimary
                                            }

                                            Item { Layout.fillWidth: true }

                                            Text {
                                                text: (modelData.startDate || "--") + " ~ " + (modelData.endDate || "--")
                                                font.pixelSize: 12
                                                color: textSecondary
                                            }
                                        }

                                        Text {
                                            width: parent.width
                                            text: "回测范围: " + String(modelData.universeLabel || "--")
                                                + "，指数: " + String(modelData.indexLabel || "--")
                                                + "，数据源: " + String(modelData.dataSourceMode || "--")
                                            font.pixelSize: 12
                                            color: textSecondary
                                            wrapMode: Text.WordWrap
                                        }

                                        GridLayout {
                                            width: parent.width
                                            columns: 4
                                            columnSpacing: 8
                                            rowSpacing: 8

                                            Repeater {
                                                model: [
                                                    { label: "收益", value: strategyBacktestPage.formatBacktestPercentValue(historyContent.summary.returns, 2) },
                                                    { label: "回撤", value: strategyBacktestPage.formatBacktestPercentValue(historyContent.summary.maxDrawdown, 2) },
                                                    { label: "夏普", value: strategyBacktestPage.formatBacktestNumberValue(historyContent.summary.sharpeRatio, 2) },
                                                    { label: "胜率", value: strategyBacktestPage.formatBacktestPercentValue(historyContent.summary.winRate, 2) },
                                                    { label: "交易次数", value: strategyBacktestPage.formatBacktestIntegerValue(historyContent.summary.tradesCount) },
                                                    { label: "运行天数", value: strategyBacktestPage.formatBacktestIntegerValue(historyContent.summary.runningDays) },
                                                    { label: "净值点数", value: strategyBacktestPage.formatBacktestIntegerValue(modelData.equityPointCount) },
                                                    { label: "范围类型", value: String(modelData.selectedUniverseType || "--") }
                                                ]

                                                delegate: Rectangle {
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 48
                                                    radius: 8
                                                    color: "#111827"

                                                    Column {
                                                        anchors.fill: parent
                                                        anchors.margins: 8
                                                        spacing: 2

                                                        Text {
                                                            text: modelData.label
                                                            font.pixelSize: 11
                                                            color: textTertiary
                                                        }

                                                        Text {
                                                            text: modelData.value
                                                            font.pixelSize: 13
                                                            color: textPrimary
                                                            elide: Text.ElideRight
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: spacingXLarge
                }
            }
        }
    }

    Dialog {
        id: actionFeedbackDialog
        anchors.centerIn: parent
        modal: true
        width: 440

        background: Rectangle {
            radius: borderRadiusMedium
            color: secondaryBg
            border.color: actionFeedbackError ? riseRed : accentBlue
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: spacingLarge

            Text {
                text: actionFeedbackError ? "策略回测失败" : "策略回测结果"
                font.pixelSize: fontSizeLarge
                font.weight: Font.DemiBold
                color: textPrimary
            }

            Text {
                text: actionFeedbackMessage
                color: actionFeedbackError ? "#FCA5A5" : textSecondary
                font.pixelSize: fontSizeNormal
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "知道了"
                    onClicked: actionFeedbackDialog.close()
                }
            }
        }
    }

    Component.onCompleted: {
        if (visible) {
            strategyBacktestActivationTimer.start()
        }
    }

    onVisibleChanged: {
        if (visible && !pageServicesReady) {
            strategyBacktestActivationTimer.start()
        }
    }

    onSelectedStrategyIndexChanged: {
        if (selectedStrategyIndex >= 0) {
            syncStrategyBacktestEditor()
        }
    }

    onPreferredStrategyIdChanged: {
        syncPreferredStrategySelection()
    }

    onStrategyBacktestSelectedDataSourceModeChanged: {
        var nextValues = toPlainJsValue(strategyBacktestParameterValues) || ({})
        nextValues.dataSourceMode = strategyBacktestSelectedDataSourceMode
        strategyBacktestParameterValues = nextValues

        if (strategyBacktestSelectedDataSourceMode === 1 && strategyBacktestSelectedCacheDatasetId > 0) {
            applySelectedCleanedDataset(selectedStrategyBacktestCacheDatasetInfo())
        }
    }
}
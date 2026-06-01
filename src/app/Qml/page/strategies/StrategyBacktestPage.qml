import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0
import "../../components/Backtest" as BacktestComponents
import "../../components/FactorWorkbench/Creation/components" as PluginComponents
import "../../components/FactorWorkbench/Navigation" as NavigationComponents
import "../../components/Strategy" as StrategyComponents
import "../../utils/StrategyStructureAdapter.js" as StructureAdapter

Rectangle {
    id: strategyBacktestPage
    color: "#0F172A"
    property bool embeddedMode: false
    property string preferredStrategyId: ""
    property string pageMode: "workbench"

    readonly property var strategyService: StrategyBridge
    readonly property var cleanedDataController: CleanedDataController
    property var strategyViewModel: null
    property bool serviceSignalsBound: false
    property bool pageServicesReady: false

    property int selectedStrategyIndex: -1
    property string selectedStrategyId: ""

    property var strategyBacktestRuntimeBaseParameters: ({})
    property var strategyBacktestParameterValues: ({})
    property bool strategyBacktestParametersLoaded: false
    property string strategyBacktestSelectedUniverseType: "market"
    property string strategyBacktestSelectedIndexSymbol: "000300.SH"
    property string strategyBacktestSelectedStartDate: ""
    property string strategyBacktestSelectedEndDate: ""
    property int strategyBacktestSelectedDataSourceMode: 0
    property var strategyBacktestDynamicParamConfigs: []
    property var strategyBacktestDynamicParamGroups: []
    property int strategyBacktestSelectedCacheDatasetId: -1
    property int strategyBacktestSelectedCacheDatasetIndex: 0
    property var strategyBacktestCacheDatasetOptions: []

    readonly property bool hasSelectedStrategy: selectedStrategyIndex >= 0
        && strategyViewModel
        && strategyViewModel.count > selectedStrategyIndex
    readonly property var selectedStrategySummaryData: {
        if (strategyViewModel && selectedStrategyId) {
            for (var index = 0; index < strategyViewModel.count; ++index) {
                var row = strategyViewModel.getRow(index)
                var rowId = row ? String(row.strategyId || "") : ""
                if (rowId === selectedStrategyId) {
                    return row
                }
            }
        }

        if (selectedStrategyIndex >= 0 && strategyViewModel && selectedStrategyIndex < strategyViewModel.count) {
            return strategyViewModel.getRow(selectedStrategyIndex)
        }

        return null
    }
    readonly property var selectedStrategyDetailData: {
        var strategyId = selectedStrategySummaryData
            ? String(selectedStrategySummaryData.strategyId || "") : ""
        if (!strategyService || !strategyId || !strategyService.get) {
            return ({})
        }

        return toPlainJsValue(strategyService.get(strategyId)) || ({})
    }
    readonly property var selectedLatestBacktestRecordData: {
        var performance = selectedStrategyDetailData ? (selectedStrategyDetailData.performanceMetrics || ({})) : ({})
        return performance.latestBacktest || ({})
    }
    readonly property string selectedStrategyNameText: selectedStrategySummaryData
        ? String(selectedStrategySummaryData.strategyName || selectedStrategySummaryData.name || "") : ""
    readonly property var strategyOptionList: {
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

    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color secondaryBg: "#1E293B"
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    readonly property real spacingXLarge: 24
    readonly property real borderRadiusMedium: 8
    readonly property real borderRadiusXLarge: 16
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
        if (!strategyService) {
            console.error("无法获取 StrategyBridge 实例")
            return
        }

        strategyService.initAsync()
        strategyViewModel = strategyService.listModel

        if (!serviceSignalsBound) {
            serviceSignalsBound = true

            strategyService.initedChanged.connect(function() {
                rebuildStrategyVisibleModel()
                syncSelectedStrategy()
                syncPreferredStrategySelection()
            })

            strategyService.strategiesChanged.connect(function() {
                rebuildStrategyVisibleModel()
                syncSelectedStrategy()
                syncPreferredStrategySelection()
            })

            strategyService.updated.connect(function() {
                rebuildStrategyVisibleModel()
                syncSelectedStrategy()
                syncPreferredStrategySelection()
                syncStrategyBacktestEditor()
            })

            strategyService.created.connect(function() {
                rebuildStrategyVisibleModel()
                syncSelectedStrategy()
                syncPreferredStrategySelection()
            })

            strategyService.deleted.connect(function() {
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
        strategyBacktestCacheDatasetOptions = StructureAdapter.buildCacheDatasetOptions(
            cleanedDataController && cleanedDataController.datasetList ? cleanedDataController.datasetList : [])
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

        var strategyDetail = selectedStrategyDetailData
        var latestBacktest = selectedLatestBacktestRecordData
        var sourceParameters = StructureAdapter.buildParameterSource(strategyDetail, latestBacktest)
        var strategyTypeIndex = StructureAdapter.resolveStrategyTypeIndex(strategyDetail)
        var dynamicConfigs = StructureAdapter.buildDynamicParamConfigs(strategyTypeIndex)
        var universeContext = StructureAdapter.resolveUniverseContext(Object.keys(latestBacktest || ({})).length > 0 ? latestBacktest : strategyDetail)
        var assumptions = StructureAdapter.resolveBacktestAssumptions(sourceParameters)
        var dataSourceMode = StructureAdapter.resolveDataSourceModeValue(latestBacktest.dataSourceMode || assumptions.dataSourceMode)
        var dataSourceDatasetId = positiveDatasetId(latestBacktest.dataSourceDatasetId || assumptions.dataSourceDatasetId || sourceParameters.dataSourceDatasetId)

        strategyBacktestRuntimeBaseParameters = toPlainJsValue(sourceParameters) || ({})
        strategyBacktestDynamicParamConfigs = dynamicConfigs
        strategyBacktestDynamicParamGroups = StructureAdapter.buildDynamicParamGroups(dynamicConfigs)
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
        strategyBacktestParameterValues = StructureAdapter.buildRuntimeParameters(strategyTypeIndex, sourceParameters)
        strategyBacktestParameterValues.dataSourceMode = dataSourceMode
        strategyBacktestParametersLoaded = true
    }

    function warmupPage() {
        initializeStrategyViewModel()
        initializeStrategyBacktestCleanedData()
        syncSelectedStrategy()
        syncStrategyBacktestEditor()
    }

    function setPageMode(modeValue) {
        var normalizedMode = String(modeValue || "").trim().toLowerCase()
        if (normalizedMode !== "analysis") {
            normalizedMode = "workbench"
        }
        pageMode = normalizedMode
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
                    visible: pageMode !== "analysis"
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

                        StrategyComponents.StrategySummaryHeaderCard {
                            Layout.fillWidth: true
                            hasSelectedStrategy: strategyBacktestPage.hasSelectedStrategy
                            strategySummary: strategyBacktestPage.selectedStrategySummaryData
                            latestBacktestRecord: strategyBacktestPage.selectedLatestBacktestRecordData
                            selectedStrategyId: strategyBacktestPage.selectedStrategyId
                            descriptionText: strategyBacktestPage.selectedStrategyDetailData && Object.keys(strategyBacktestPage.selectedStrategyDetailData).length > 0
                                ? strategyBacktestPage.currentStrategyDescription(strategyBacktestPage.selectedStrategyDetailData)
                                : strategyBacktestPage.currentStrategyDescription(strategyBacktestPage.selectedStrategySummaryData)
                        }

                        BacktestComponents.BacktestParameterPanel {
                            Layout.fillWidth: true
                            strategyOptions: strategyBacktestPage.strategyOptionList
                            universeOptions: strategyBacktestPage.strategyBacktestUniverseOptions
                            indexPoolOptions: strategyBacktestPage.strategyBacktestIndexOptions
                            dataSourceOptions: strategyBacktestPage.strategyBacktestDataSourceOptions
                            cacheDatasetOptions: strategyBacktestPage.strategyBacktestCacheDatasetOptions
                            selectedStrategyName: strategyBacktestPage.selectedStrategyNameText
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

                    }
                }

                StrategyBacktestAnalysisPage {
                    visible: pageMode !== "workbench"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    hasSelectedStrategy: strategyBacktestPage.hasSelectedStrategy
                    selectedStrategyDetail: strategyBacktestPage.selectedStrategyDetailData
                    latestBacktestRecord: strategyBacktestPage.selectedLatestBacktestRecordData
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: spacingXLarge
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
            applySelectedCleanedDataset(StructureAdapter.selectedCacheDatasetInfo(
                strategyBacktestSelectedCacheDatasetId,
                cleanedDataController ? cleanedDataController.selectedDatasetInfo : null,
                strategyBacktestCacheDatasetOptions))
        }
    }
}
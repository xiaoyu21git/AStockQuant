// FactorWorkbench.qml
// 统一因子工作台 - 五模式量化因子工作台设计
// 多页面可见性切换方案，避免组件重新加载

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import ConsoleUi 1.0
import "../components/FactorWorkbench/Creation" as CreationComponents
import "../components/FactorWorkbench/Backtest" as BacktestComponents
import "../components/FactorWorkbench/Debug" as DebugComponents
import "../components/FactorWorkbench/Library" as LibraryComponents

/**
 * 统一因子工作台 - 五模式量化工作台设计
 * 多页面可见性切换方案，避免组件重新加载
 * 包含：因子库、创建、调试、分析、回测五大模式
 */
Item {
    id: root

    signal requestOpenStrategyCreation(var importPayload)

    // ============ 页面属性 ============
    
    property string currentMode: "library"  // library, create, debug, analyze, backtest
    property string requestedRouteMode: "library"
    property string selectedFactorId: ""
    property var editingFactorData: ({})
    property string statusMessage: "系统已就绪"
    property var latestBacktestReport: ({})
    property var factorBacktestBaselineReports: ({})
    property int factorDefinitionRevision: 0
    property var pendingFactorCoverageReport: ({})
    property var pendingFactorCoveragePreviousReport: ({})
    property string pendingFactorCoverageSummary: ""
    property string pendingFactorCoverageAction: ""
    property bool suppressAnalyzeAutoRun: false
    property bool factorMutationInProgress: false
    property var factorOperationReport: ({})
    property bool factorOperationReportVisible: false
    property bool creationFormResetPending: false
    property string transientStatusMessage: ""
    readonly property int transientStatusDurationMs: 10000

    property int selectedType: -1  // 当前选择的因子类型
    property var factorMetaMap: null   // 全部metadata
    property var mergedMeta: null      // 合并后的meta
    property bool createPageLoaded: false
    property bool debugPageLoaded: false
    property bool analyzePageLoaded: false
    property bool backtestPageLoaded: false

    function ensureFactorServiceReady() {
        if (factorService && typeof factorService.initialize === "function") {
            factorService.initialize()
        }
        // 仅在 ViewModel 为空时填充，避免重复 DB 查询阻塞 UI
        if (!factorViewModel || (typeof factorViewModel.rowCount === "function" && factorViewModel.rowCount() === 0)) {
            factorViewModel = factorService ? factorService.getViewModel() : null
        }
    }

    function warmupPage() {
        ensureFactorServiceReady()
        // getAllFactors() 已由 getViewModel() → ensureViewModelPopulated() 内部调用，此处不再重复
    }

    function resolveBaseStatusMessage() {
        if (factorMutationInProgress) {
            return "因子服务正在处理写操作"
        }
        if (hasFactorOperationReport()) {
            return formatFactorOperationStatus(factorOperationReport)
        }
        return "系统已就绪"
    }
    
    // ============ C++ 数据绑定 ============
    
    // 因子服务（单例模式）- 直接使用，不需要实例化
    readonly property var factorService: Bridge.FactorService
    
    // 因子视图模型 - 由 ensureFactorServiceReady() 显式初始化，避免绑定求值时提前触发 DB 查询
    property var factorViewModel: null

    // ============ 因子参数配置加载 ============
    Component.onCompleted: {
        console.log("FactorWorkbench 初始化完成")
        if (visible) {
            warmupPage()
        }
        if (factorService) {
            factorMutationInProgress = factorService.mutationInProgress
            factorOperationReport = factorService.lastOperationReport || ({})
            factorOperationReportVisible = factorMutationInProgress
                || (factorOperationReport && factorOperationReport.success === false)
        }
        if (requestedRouteMode === "analyze" && currentMode !== requestedRouteMode) {
            switchMode(requestedRouteMode)
        }
        // 因子参数配置现在由 CreationPageDynamic 组件动态加载
    }

    onVisibleChanged: {
        if (visible) {
            warmupPage()
        }
    }

    onRequestedRouteModeChanged: {
        if ((requestedRouteMode === "library" || requestedRouteMode === "analyze")
                && currentMode !== requestedRouteMode) {
            switchMode(requestedRouteMode)
        }
    }

    Connections {
        target: factorService

        function onMutationInProgressChanged() {
            if (!factorService) {
                return
            }

            root.factorMutationInProgress = factorService.mutationInProgress
            if (root.factorMutationInProgress) {
                statusMessageTimer.stop()
                root.factorOperationReportVisible = true
                root.statusMessage = "因子服务正在处理写操作"
            } else {
                root.statusMessage = root.resolveBaseStatusMessage()
            }
        }

        function onLastOperationReportChanged() {
            if (!factorService) {
                return
            }

            root.factorOperationReport = factorService.lastOperationReport || ({})
            root.factorOperationReportVisible = root.factorMutationInProgress
                || (root.factorOperationReport && Object.keys(root.factorOperationReport).length > 0)
            root.statusMessage = root.formatFactorOperationStatus(root.factorOperationReport)
            if (!root.factorMutationInProgress && root.factorOperationReport && root.factorOperationReport.success !== false) {
                statusMessageTimer.restart()
            } else if (root.factorOperationReport && root.factorOperationReport.success === false) {
                statusMessageTimer.stop()
            }
        }

        function onFactorAdded(factorId, factorData) {
            root.noteFactorDefinitionChanged(factorId, "add")
        }

        function onFactorUpdated(factorId, factorData) {
            root.noteFactorDefinitionChanged(factorId, "update")
        }

        function onFactorDeleted(factorId) {
            root.noteFactorDefinitionChanged(factorId, "delete")
        }
    }

    function ensureModeLoaded(mode) {
        switch (mode) {
            case "create":
                createPageLoaded = true
                break
            case "debug":
                debugPageLoaded = true
                break
            case "analyze":
                analyzePageLoaded = true
                break
            case "backtest":
                backtestPageLoaded = true
                break
        }
    }

    function resetCreationPageForm() {
        if (!creationPageLoader || !creationPageLoader.item || typeof creationPageLoader.item.resetForm !== "function") {
            return false
        }

        creationPageLoader.item.resetForm()
        creationFormResetPending = false
        return true
    }

    function leaveCreateMode() {
        selectedFactorId = ""
        editingFactorData = ({})
        selectedType = -1
        latestBacktestReport = ({})
        creationFormResetPending = false
        createPageLoaded = false
        switchMode("library")
    }

    Component {
        id: creationPageComponent

        CreationComponents.CreationPagePluginIntegrated {
            anchors.fill: parent
            anchors.topMargin: 10
            visible: root.currentMode === "create"
            selectedType: root.selectedType
            factorService: root.factorService
            editingFactorData: root.editingFactorData

            onToastRequested: function(message) {
                root.showToast(message)
            }

            onFactorCreated: function(factorData) {
                handleFactorCreated(factorData)
            }

            onTypeChanged: function(type) {
                root.selectedType = type
            }

            onBackClicked: {
                root.leaveCreateMode()
            }
        }
    }

    Component {
        id: debugPageComponent

        DebugComponents.DebugPage {
            anchors.fill: parent
            anchors.topMargin: 10
            visible: root.currentMode === "debug"
            factorService: root.factorService
            selectedFactorId: root.selectedFactorId

            Component.onCompleted: {
                console.log("DebugPage 初始化完成")
            }

            onVisibleChanged: {
                if (visible && selectedFactorId && typeof autoValidateCurrentSelection === "function") {
                    console.log("DebugPage 变为可见，加载因子:", selectedFactorId)
                    autoValidateCurrentSelection()
                }
            }
        }
    }

    Component {
        id: analysisPageComponent

        AnalysisPage {
            anchors.fill: parent
            anchors.topMargin: 10
            visible: root.currentMode === "analyze"
            factorService: root.factorService
            selectedFactorId: root.selectedFactorId
            backtestReport: root.latestBacktestReport
            factorDefinitionRevision: root.factorDefinitionRevision
            suppressAutoAnalyze: root.suppressAnalyzeAutoRun

            onRequestWriteBacktestMetrics: function(report) {
                root.handleWriteBacktestMetrics(report || ({}))
            }

            onRequestImportToStrategy: function(report) {
                root.handleImportBacktestToStrategy(report || ({}))
            }

            Component.onCompleted: {
                console.log("AnalysisPage 初始化完成")
            }

            onVisibleChanged: {
                if (visible && selectedFactorId && !suppressAutoAnalyze && !(backtestReport && Object.keys(backtestReport).length > 0)) {
                    console.log("AnalysisPage 变为可见，分析因子:", selectedFactorId)
                    if (factorService) {
                        factorService.analyzeFactor(selectedFactorId)
                    }
                }
            }
        }
    }

    Component {
        id: backtestPageComponent

        FactorBacktestPage {
            anchors.fill: parent
            anchors.topMargin: 10
            visible: root.currentMode === "backtest"
            factorService: root.factorService
            cleanedDataController: Bridge.CleanedDataController
            selectedFactorId: root.selectedFactorId
            previousBacktestReport: root.factorBacktestBaselineFor(root.selectedFactorId)
            factorDefinitionRevision: root.factorDefinitionRevision

            onAnalysisReportRequested: function(result) {
                console.log("回测完成，切换到分析报告页面")
                root.suppressAnalyzeAutoRun = true
                var report = result || ({})
                root.latestBacktestReport = report
                var reportConfig = report.config || ({})
                var activeFactorId = String(report.activeAnalysisFactorId || reportConfig.factorId || "").trim()
                if (activeFactorId.length > 0) {
                    root.selectedFactorId = activeFactorId
                }
                root.showToast("📈 回测完成，已切换到分析报告")
                Qt.callLater(function() {
                    switchMode("analyze")
                    Qt.callLater(function() {
                        root.handleFactorBacktestCoverage(report)
                    })
                })
            }

            Component.onCompleted: {
                console.log("BacktestPage 初始化完成")
                console.log("CleanedDataController:", cleanedDataController ? "有效" : "无效")
            }
        }
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 顶部导航栏 - 使用外部组件
        ModeTitleBar {
            currentMode: root.currentMode
            showBackButton: currentMode !== "library"
            onModeSelected: function(mode) { switchMode(mode) }
            onBackClicked: {
                if (currentMode === "create") {
                    root.leaveCreateMode()
                } else {
                    switchMode("library")
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: root.factorMutationInProgress ? "#FFF7ED" : root.factorOperationTone().background
            border.color: root.factorOperationTone().border
            border.width: 1
            visible: root.factorMutationInProgress || root.hasFactorOperationReport()

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: root.factorMutationInProgress ? "#F97316" : root.factorOperationTone().accent
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: root.factorMutationInProgress
                            ? "因子服务写操作进行中"
                            : root.formatFactorOperationHeadline(root.factorOperationReport)
                        font.pixelSize: 14
                        font.bold: true
                        color: "#0F172A"
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.factorMutationInProgress
                            ? "当前会串行处理 add/update/delete，避免并发写入交叉。"
                            : root.formatFactorOperationStatus(root.factorOperationReport)
                        font.pixelSize: 12
                        color: "#475569"
                        elide: Text.ElideRight
                    }
                }

                ActionChip {
                    label: root.factorMutationInProgress
                        ? "处理中"
                        : root.formatFactorOperationChip(root.factorOperationReport)
                    useCustomColors: true
                    customBackgroundColor: root.factorMutationInProgress ? "#FFEDD5" : root.factorOperationTone().chip
                    customBorderColor: "transparent"
                    customTextColor: root.factorMutationInProgress ? "#C2410C" : root.factorOperationTone().accent
                    chipEnabled: false
                }
            }
        }
        
        // 主内容区 - 多页面并行加载，通过可见性控制显示
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        
            // 1. 因子库页面
            LibraryComponents.FactorLibraryPage {
                id: libraryPage
                anchors.fill: parent
                visible: root.currentMode === "library"
                factorService: root.factorService
                factorModel: root.factorViewModel
                selectedFactorId: root.selectedFactorId
                onFactorSelected: function(factorId) { handleFactorSelected(factorId) }
                onFactorDoubleClicked: function(factorId) { handleFactorDoubleClicked(factorId) }
                onFavoriteToggled: function(factorId, favorite) { handleFavoriteToggled(factorId, favorite) }
                onPreviewRequested: function(factorId) { handlePreviewRequested(factorId) }
                onAnalyzeRequested: function(factorId) { handleAnalyzeRequested(factorId) }
                onEditRequested: function(factorId) { handleEditRequested(factorId) }
                onDeleteRequested: function(factorId) {
                    console.log("FactorLibraryPage 请求删除因子:", factorId)
                    factorService.deleteFactor(factorId)
                }
                onCreateRequested: openCreateMode()
                
                Component.onCompleted: {
                    console.log("FactorLibraryPage 初始化完成，factorModel:", factorModel ? "有效" : "无效")
                }
                
                // 页面激活时不需要刷新数据，因为deleteFactor会自动更新
                onVisibleChanged: {
                    if (visible) {
                        console.log("FactorLibraryPage 变为可见")
                        // 不需要调用refreshFactorLibrary，因为deleteFactor会自动更新视图模型
                    }
                }
            }
        
            Loader {
                id: creationPageLoader
                anchors.fill: parent
                active: root.createPageLoaded
                asynchronous: true
                visible: root.currentMode === "create"
                sourceComponent: creationPageComponent

                onStatusChanged: {
                    if (status === Loader.Ready && root.creationFormResetPending) {
                        root.resetCreationPageForm()
                    }
                }
            }

            Loader {
                id: debugPageLoader
                anchors.fill: parent
                active: root.debugPageLoaded
                asynchronous: true
                visible: root.currentMode === "debug"
                sourceComponent: debugPageComponent
            }

            Loader {
                id: analysisPageLoader
                anchors.fill: parent
                active: root.analyzePageLoaded
                asynchronous: true
                visible: root.currentMode === "analyze"
                sourceComponent: analysisPageComponent
            }

            Loader {
                id: backtestPageLoader
                anchors.fill: parent
                active: root.backtestPageLoaded
                asynchronous: true
                visible: root.currentMode === "backtest"
                sourceComponent: backtestPageComponent
            }
        }
        
        // 底部通知栏
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#1E293B"
            
            Text {
                anchors.centerIn: parent
                text: root.statusMessage
                font.pixelSize: 14
                color: "#94A3B8"
            }

            Timer {
                id: statusMessageTimer
                interval: root.transientStatusDurationMs
                repeat: false
                onTriggered: {
                    root.transientStatusMessage = ""
                    if (!root.factorMutationInProgress && root.factorOperationReport && root.factorOperationReport.success !== false) {
                        root.factorOperationReportVisible = false
                    }
                    root.statusMessage = root.resolveBaseStatusMessage()
                }
            }
        }
    }

    Dialog {
        id: factorCoverageDecisionDialog
        modal: true
        width: 520
        title: "因子股票池覆盖确认"
        standardButtons: Dialog.Yes | Dialog.No

        onAccepted: {
            storeFactorBacktestBaseline(pendingFactorCoverageReport)
            showToast(pendingFactorCoverageSummary.length > 0
                ? pendingFactorCoverageSummary
                : "已使用本次因子回测股票池覆盖上一轮基线")
        }

        onRejected: {
            showToast(pendingFactorCoverageSummary.length > 0
                ? pendingFactorCoverageSummary
                : "已保留上一轮因子回测股票池基线")
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: pendingFactorCoverageSummary
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: "#E2E8F0"
            }

            Text {
                Layout.fillWidth: true
                text: "选择“是”将把本次因子回测结果设置为新的股票池基线；选择“否”则仅保留本次分析结果，不覆盖上一轮基线。"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#94A3B8"
            }
        }

        background: Rectangle {
            radius: 14
            color: "#0F172A"
            border.width: 1
            border.color: "#334155"
        }
    }
    
    // ============ 核心函数 ============
    
    // 切换模式
    function switchMode(mode) {
        console.log("切换到模式:", mode, "当前模式:", currentMode)
        ensureModeLoaded(mode)
        currentMode = mode
        
        // 记录切换时的性能信息
        console.time("模式切换耗时")
        
        // 触发页面激活逻辑
        switch(mode) {
            case "library":
                break
            case "create":
                break
            case "debug":
                break
            case "analyze":
                break
            case "backtest":
                if (backtestPageLoader.item && typeof backtestPageLoader.item.rebuildCacheDatasetOptions === "function") {
                    backtestPageLoader.item.rebuildCacheDatasetOptions()
                }
                if (Bridge.CleanedDataController && typeof Bridge.CleanedDataController.refreshDatasets === "function") {
                    Bridge.CleanedDataController.refreshDatasets()
                }
                break
        }
        
        console.timeEnd("模式切换耗时")
    }

    function openCreateMode() {
        editingFactorData = ({})
        latestBacktestReport = ({})
        selectedType = -1
        creationFormResetPending = true
        switchMode("create")

        if (!resetCreationPageForm()) {
            creationFormResetPending = true
        }
    }
    
    // 获取模式标题
    function getModeTitle(mode) {
        switch(mode) {
            case "library": return "因子库浏览"
            case "create": return root.editingFactorData && root.editingFactorData.factorId ? "因子编辑" : "因子创建"
            case "debug": return "因子调试"
            case "analyze": return "因子分析"
            case "backtest": return "因子回测"
            default: return "因子分析"
        }
    }
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
        transientStatusMessage = message
        statusMessage = message
        statusMessageTimer.restart()
    }

    function handleWriteBacktestMetrics(report) {
        var activeReport = report || ({})
        var factorId = ""
        if (activeReport.config && activeReport.config.factorId !== undefined) {
            factorId = String(activeReport.config.factorId || "")
        } else if (activeReport.factorId !== undefined) {
            factorId = String(activeReport.factorId || "")
        } else {
            factorId = String(selectedFactorId || "")
        }

        if (!factorId) {
            showToast("未找到可写入的因子")
            return false
        }

        if (!factorService || typeof factorService.updateFactor !== "function") {
            showToast("因子服务不可用，无法写入指标")
            return false
        }

        var updateSuccess = factorService.writeBacktestMetrics(factorId, activeReport)
        if (updateSuccess) {
            showToast("已写入回测指标: " + factorId)
        } else {
            showToast("写入回测指标失败: " + factorId)
        }
        return updateSuccess
    }

    function buildStrategyImportPayload(report) {
        var activeReport = report || ({})
        var config = activeReport.config || ({})
        var factorId = String(activeReport.factorId || config.factorId || selectedFactorId || "").trim()
        var factorDetail = factorId && factorService && typeof factorService.getFactorById === "function"
            ? (factorService.getFactorById(factorId) || ({}))
            : ({})
        var factorName = String(config.factorName || activeReport.factorName || factorDetail.factorName || factorDetail.displayName || factorId).trim()
        var actualStartDate = String(config.actualStartDate || "").trim()
        var effectiveStartDate = actualStartDate || String(config.startDate || "").trim()
        var effectiveEndDate = String(config.endDate || "").trim()
        var warmupTrimmedTradingDays = Number(config.warmupTrimmedTradingDays || 0)

        if (!isFinite(warmupTrimmedTradingDays) || warmupTrimmedTradingDays < 0) {
            warmupTrimmedTradingDays = 0
        }

        return {
            parameters: {
                factorImportContext: {
                    importSource: "factorBacktest",
                    factorId: factorId,
                    factorName: factorName,
                    actualStartDate: actualStartDate,
                    effectiveStartDate: effectiveStartDate,
                    effectiveEndDate: effectiveEndDate,
                    warmupTrimmedTradingDays: Math.floor(warmupTrimmedTradingDays)
                },
                factor_overlay: {
                    enabled: factorId.length > 0,
                    targetPositionCount: 10,
                    minimumCompositeScore: 0,
                    combineMode: "rank_only",
                    selectionScope: "rule_eligible",
                    allocations: factorId.length > 0 ? [{
                        factor_id: factorId,
                        display_name: factorName || factorId,
                        weight_percent: 100
                    }] : []
                }
            }
        }
    }

    function handleImportBacktestToStrategy(report) {
        var importPayload = buildStrategyImportPayload(report)
        if (!importPayload || Object.keys(importPayload).length === 0) {
            showToast("当前因子回测结果缺少可导入的因子信息")
            return false
        }

        root.requestOpenStrategyCreation(importPayload)
        showToast("已携带因子回测上下文跳转到策略创建")
        return true
    }

    function factorBacktestBaselineFor(factorId) {
        var normalizedFactorId = String(factorId || "")
        if (!normalizedFactorId || !factorBacktestBaselineReports[normalizedFactorId]) {
            return ({})
        }
        return factorBacktestBaselineReports[normalizedFactorId]
    }

    function clearFactorBacktestBaseline(factorId) {
        var normalizedFactorId = String(factorId || "")
        if (!normalizedFactorId || !factorBacktestBaselineReports[normalizedFactorId]) {
            return
        }

        var nextStore = Object.assign({}, factorBacktestBaselineReports)
        delete nextStore[normalizedFactorId]
        factorBacktestBaselineReports = nextStore
    }

    function clearLatestBacktestReportForFactor(factorId) {
        var normalizedFactorId = String(factorId || "")
        if (!normalizedFactorId || !latestBacktestReport || Object.keys(latestBacktestReport).length === 0) {
            return
        }

        if (latestBacktestReport.results && Array.isArray(latestBacktestReport.results)) {
            var filteredResults = []
            var removedCount = 0
            var currentActiveFactorId = String(latestBacktestReport.activeAnalysisFactorId || "")
            var activeFactorStillExists = false
            for (var resultIndex = 0; resultIndex < latestBacktestReport.results.length; resultIndex++) {
                var resultItem = latestBacktestReport.results[resultIndex] || ({})
                var resultConfig = resultItem.config || ({})
                var resultFactorId = String(resultItem.factorId || resultConfig.factorId || "")
                if (resultFactorId === normalizedFactorId) {
                    removedCount++
                    continue
                }
                if (resultFactorId === currentActiveFactorId) {
                    activeFactorStillExists = true
                }
                filteredResults.push(resultItem)
            }

            if (removedCount > 0) {
                if (filteredResults.length > 0) {
                    var nextActiveFactorId = currentActiveFactorId
                    if (!nextActiveFactorId || !activeFactorStillExists) {
                        var firstRemainingResult = filteredResults[0] || ({})
                        var firstRemainingConfig = firstRemainingResult.config || ({})
                        nextActiveFactorId = String(firstRemainingResult.factorId || firstRemainingConfig.factorId || "")
                    }

                    var nextFactorIds = []
                    for (var factorIndex = 0; factorIndex < filteredResults.length; factorIndex++) {
                        var factorResult = filteredResults[factorIndex] || ({})
                        var factorConfig = factorResult.config || ({})
                        var factorResultId = String(factorResult.factorId || factorConfig.factorId || "")
                        if (factorResultId.length > 0) {
                            nextFactorIds.push(factorResultId)
                        }
                    }

                    latestBacktestReport = Object.assign({}, latestBacktestReport, {
                        results: filteredResults,
                        factorIds: nextFactorIds,
                        factorCount: filteredResults.length,
                        activeAnalysisFactorId: nextActiveFactorId
                    })
                } else {
                    latestBacktestReport = ({})
                }
                return
            }
        }

        var reportFactorId = ""
        if (latestBacktestReport.config && latestBacktestReport.config.factorId !== undefined) {
            reportFactorId = String(latestBacktestReport.config.factorId || "")
        } else if (latestBacktestReport.factorId !== undefined) {
            reportFactorId = String(latestBacktestReport.factorId || "")
        }

        if (reportFactorId === normalizedFactorId) {
            latestBacktestReport = ({})
        }
    }

    function noteFactorDefinitionChanged(factorId, operation) {
        var normalizedFactorId = String(factorId || "")
        factorDefinitionRevision += 1

        if (!normalizedFactorId) {
            return
        }

        if (operation === "update") {
            clearLatestBacktestReportForFactor(normalizedFactorId)
            clearFactorBacktestBaseline(normalizedFactorId)
        }

        if (operation === "delete") {
            clearLatestBacktestReportForFactor(normalizedFactorId)
            clearFactorBacktestBaseline(normalizedFactorId)
            if (String(selectedFactorId || "") === normalizedFactorId) {
                selectedFactorId = ""
                editingFactorData = ({})
            }
        }
    }

    function compareFactorBacktestCoverage(previousReport, currentReport) {
        if (!previousReport || Object.keys(previousReport).length === 0) {
            return {
                action: "replace",
                summary: "当前没有上一轮同因子回测基线，本次结果将直接作为新的股票池基线。"
            }
        }

        var previousMetrics = previousReport.metrics || ({})
        var currentMetrics = currentReport.metrics || ({})
        var previousResearch = previousMetrics.research || ({})
        var currentResearch = currentMetrics.research || ({})
        var previousIcir = previousMetrics.ic || ({})
        var currentIcir = currentMetrics.ic || ({})
        var betterSignals = 0
        var worseSignals = 0
        var detailParts = []

        var coverageDiff = Number(currentResearch.dataCoverage || 0) - Number(previousResearch.dataCoverage || 0)
        var irDiff = Number(currentIcir.ir || 0) - Number(previousIcir.ir || 0)
        var icAbsDiff = Math.abs(Number(currentIcir.value || 0)) - Math.abs(Number(previousIcir.value || 0))
        var spreadDiff = Number(currentResearch.spreadReturn || 0) - Number(previousResearch.spreadReturn || 0)

        if (coverageDiff >= 0.05) {
            betterSignals++
            detailParts.push("覆盖率 +" + (coverageDiff * 100).toFixed(1) + "%")
        } else if (coverageDiff <= -0.05) {
            worseSignals++
            detailParts.push("覆盖率 " + (coverageDiff * 100).toFixed(1) + "%")
        }

        if (irDiff >= 0.15) {
            betterSignals++
            detailParts.push("IR +" + irDiff.toFixed(2))
        } else if (irDiff <= -0.15) {
            worseSignals++
            detailParts.push("IR " + irDiff.toFixed(2))
        }

        if (icAbsDiff >= 0.01) {
            betterSignals++
            detailParts.push("|IC| +" + icAbsDiff.toFixed(3))
        } else if (icAbsDiff <= -0.01) {
            worseSignals++
            detailParts.push("|IC| " + icAbsDiff.toFixed(3))
        }

        if (spreadDiff >= 0.02) {
            betterSignals++
            detailParts.push("多空收益差 +" + (spreadDiff * 100).toFixed(2) + "%")
        } else if (spreadDiff <= -0.02) {
            worseSignals++
            detailParts.push("多空收益差 " + (spreadDiff * 100).toFixed(2) + "%")
        }

        var summaryPrefix = "本次比较仅基于因子指标与有效期，不再携带或比较股票池信息。"
        var detailSummary = detailParts.length > 0 ? ("关键差异: " + detailParts.join("，") + "。") : "两次关键指标接近。"

        if (betterSignals >= 2 && worseSignals === 0) {
            return {
                action: "replace",
                summary: summaryPrefix + detailSummary + " 本次因子结果明显更优，已自动覆盖上一轮基线。"
            }
        }

        if (worseSignals >= 2 && betterSignals === 0) {
            return {
                action: "keep",
                summary: summaryPrefix + detailSummary + " 上一轮结果更稳健，已保留上一轮基线。"
            }
        }

        return {
            action: "ask",
            summary: summaryPrefix + detailSummary + " 两次结果接近，请决定是否用本次股票池覆盖上一轮基线。"
        }
    }

    function storeFactorBacktestBaseline(report) {
        if (!report || !report.config || !report.config.factorId) {
            return
        }

        var nextStore = Object.assign({}, factorBacktestBaselineReports)
        nextStore[String(report.config.factorId)] = report
        factorBacktestBaselineReports = nextStore
    }

    function handleFactorBacktestCoverage(report) {
        if (!report || Object.keys(report).length === 0) {
            return
        }

        if (report.results && Array.isArray(report.results)) {
            for (var resultIndex = 0; resultIndex < report.results.length; resultIndex++) {
                handleFactorBacktestCoverage(report.results[resultIndex] || ({}))
            }
            return
        }

        if (!report.config || !report.config.factorId) {
            return
        }

        var factorId = String(report.config.factorId)
        var previousReport = factorBacktestBaselineFor(factorId)
        var decision = compareFactorBacktestCoverage(previousReport, report)

        pendingFactorCoverageReport = report
        pendingFactorCoveragePreviousReport = previousReport
        pendingFactorCoverageSummary = decision.summary || ""
        pendingFactorCoverageAction = decision.action || "replace"

        if (decision.action === "replace") {
            storeFactorBacktestBaseline(report)
            showToast(decision.summary)
            return
        }

        if (decision.action === "keep") {
            showToast(decision.summary)
            return
        }

        factorCoverageDecisionDialog.open()
    }

    function hasFactorOperationReport() {
        return factorOperationReportVisible && factorOperationReport && Object.keys(factorOperationReport).length > 0
    }

    function formatFactorOperationHeadline(report) {
        if (!report || Object.keys(report).length === 0) {
            return "因子服务状态"
        }

        var operation = report.operation || "factorOperation"
        var factorId = report.factorId || "未指定因子"
        var action = operation === "addFactor"
            ? "新增因子"
            : operation === "updateFactor"
                ? "更新因子"
                : operation === "deleteFactor"
                    ? "删除因子"
                    : operation
        return action + " · " + factorId
    }

    function formatFactorOperationStatus(report) {
        if (!report || Object.keys(report).length === 0) {
            return "系统已就绪"
        }

        var message = report.message || "因子服务已更新"
        var stage = report.stage || "unknown"
        var prefix = report.success ? "已完成" : "需关注"
        return prefix + " · " + message + " · 阶段: " + stage
    }

    function formatFactorOperationChip(report) {
        if (!report || Object.keys(report).length === 0) {
            return "空闲"
        }

        return report.success ? "成功" : "失败"
    }

    function factorOperationTone() {
        if (factorMutationInProgress) {
            return {
                background: "#FFF7ED",
                border: "#FDBA74",
                accent: "#C2410C",
                chip: "#FFEDD5"
            }
        }

        if (factorOperationReport && factorOperationReport.success === false) {
            return {
                background: "#FEF2F2",
                border: "#FCA5A5",
                accent: "#B91C1C",
                chip: "#FEE2E2"
            }
        }

        return {
            background: "#ECFDF5",
            border: "#86EFAC",
            accent: "#047857",
            chip: "#D1FAE5"
        }
    }
    
    // 处理因子选择
    function handleFactorSelected(factorId) {
        console.log("因子选择:", factorId)
        selectedFactorId = factorId
        latestBacktestReport = ({})
    }

    function handleFactorDoubleClicked(factorId) {
        console.log("因子双击:", factorId)
        selectedFactorId = factorId
        latestBacktestReport = ({})
        if (factorId && currentMode !== "debug") {
            switchMode("debug")
        }
    }

    function handleFavoriteToggled(factorId, favorite) {
        selectedFactorId = factorId
        latestBacktestReport = ({})
        showToast((favorite ? "已收藏因子: " : "已取消收藏: ") + factorId)
    }

    function handlePreviewRequested(factorId) {
        selectedFactorId = factorId
        latestBacktestReport = ({})
        showToast("预览因子: " + factorId)
        switchMode("analyze")
    }

    function handleAnalyzeRequested(factorId) {
        selectedFactorId = factorId
        latestBacktestReport = ({})
        showToast("分析因子: " + factorId)
        switchMode("analyze")
    }

    function handleEditRequested(factorId) {
        var normalizedFactorId = String(factorId || "")
        if (!normalizedFactorId) {
            showToast("未识别到有效因子，无法进入编辑")
            return
        }

        ensureFactorServiceReady()
        //resetCreationPageForm()
        var factorDetail = ({})
        if (factorService && typeof factorService.getFactorByIdFromRepository === "function") {
            factorDetail = factorService.getFactorByIdFromRepository(normalizedFactorId) || ({})
        }

        if ((!factorDetail || Object.keys(factorDetail).length === 0)
                && factorService
                && typeof factorService.getFactorById === "function") {
            factorDetail = factorService.getFactorById(normalizedFactorId) || ({})
        }

        if (!factorDetail || Object.keys(factorDetail).length === 0) {
            showToast("未找到因子详情，无法进入编辑: " + normalizedFactorId)
            return
        }

        editingFactorData = factorDetail
        selectedFactorId = String(factorDetail.factorId || normalizedFactorId)
        latestBacktestReport = ({})
        showToast("进入因子编辑: " + (factorDetail.displayName || selectedFactorId))
        switchMode("create")
    }
    
    // 处理因子创建
    function handleFactorCreated(factorData) {
        console.log("因子创建完成:", factorData)

        var isEditOperation = factorData && factorData.operation === "update"
        var displayName = factorData && factorData.displayName ? factorData.displayName : "未命名因子"
        if (factorData && factorData.factorId) {
            selectedFactorId = factorData.factorId
        }
        latestBacktestReport = ({})
        editingFactorData = ({})
        if (isEditOperation && factorData && factorData.factorId) {
            clearFactorBacktestBaseline(factorData.factorId)
        }
        if (factorData && factorData.factorType !== undefined && factorData.factorType !== null) {
            selectedType = factorData.factorType
        }
        root.showToast("因子 '" + displayName + "' " + (isEditOperation ? "更新成功" : "创建成功"))
        
        // 切换到因子库页面
        switchMode("library")
    }
}

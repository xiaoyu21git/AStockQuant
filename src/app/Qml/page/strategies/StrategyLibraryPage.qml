// pages/StrategyLibraryPage.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0
import "../../components/FactorWorkbench/Navigation" as NavigationComponents
import "../../components/Strategy" as StrategyComponents
import "../../components/Strategy/Creation" as StrategyCreationComponents
import "../../components/Base" as BaseComponents
import "../../components" as Components

import "../../utils/StartupGateFormatter.js" as StartupGateFormatter
import "../../utils/FormatUtils.js" as FormatUtils
import "../../utils/NormalizeUtils.js" as NormalizeUtils
import "../../utils/DataAccessPatterns.js" as DataAccess
import "../../utils/PureUtils.js" as PureUtils

Rectangle {
    id: strategyLibraryPage
    color: "#0F172A"  // primaryBg
    
    // 属性
    property int selectedStrategyIndex: 0
    property string selectedStrategyId: ""
    property string strategyLibrarySearchText: ""
    property bool showFilter: false
    property bool showSorter: false
    property int runningStrategyIndex: 0
    property bool serviceSignalsBound: false
    property bool pageServicesReady: false
    property bool deleteInProgress: false
    property string actionFeedbackMessage: ""
    property bool actionFeedbackError: false
    property var recentStartRequests: ({})
    property var localStatusOverrides: ({})     // {strategyId: "启动中"|"停止中"} QML 本地即时状态
    property bool showBacktestWorkbench: false
    property bool showPerformance: false
    property bool showTuning: false
    property string backtestWorkbenchStatusText: ""
    property bool backtestWorkbenchLoadedOnce: false
    property var backtestResult: ({})
    property string backtestWorkbenchMode: "workbench"
    property alias strategyVisibleModel: strategyVisibleModel
    readonly property bool hasSelectedStrategy: selectedStrategyIndex >= 0
        && strategyViewModel
        && strategyViewModel.count > selectedStrategyIndex
    readonly property real contentMaxWidth: 1480
    readonly property real pageSidePadding: 18
    
    // 信号
    signal createNewStrategy()
    signal strategySelected(string strategyName)
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color primaryBg: "#0F172A"
    readonly property color secondaryBg: "#1E293B"
    readonly property color tertiaryBg: "#334155"
    readonly property color accentBlue: "#3B82F6"
    readonly property color borderColor: "#475569"
    readonly property color sectionCardBorderColor: Qt.rgba(71 / 255, 85 / 255, 105 / 255, 0.22)
    readonly property color warningAmber: "#F59E0B"
    readonly property color successGreen: "#10B981"
    readonly property color riseRed: "#EF4444"
    readonly property color fallGreen: "#10B981"
    
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeXLarge: 24
    
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    readonly property real spacingXLarge: 24
    readonly property real sectionCardPadding: 20
    readonly property real sectionCardSpacing: 12
    
    readonly property real borderRadiusMedium: 8
    readonly property real borderRadiusXLarge: 16
    
    // C++服务引用
    readonly property var strategyViewModel: strategyService.listModel
    readonly property var tradingConnectionConfigService: TradingConnectionConfigService
    readonly property var tradingMarketCalendarService: TradingMarketCalendarService
    readonly property var tradingRuntimeStatusService: TradingRuntimeStatusService
    readonly property var tradeExecutionBridge: TradeExecutionBridge
    readonly property var uiLifecycleCoordinator: UiLifecycleCoordinator
    property int marketSessionRevision: 0
    property int runtimeSnapshotRevision: 0
    property int statusRefreshCounter: 0
    
    // 初始化策略服务 - 确保数据自动加载
    function initializeStrategyViewModel() {
        if (strategyService) {
            strategyService.initAsync()

            if (!serviceSignalsBound) {
                serviceSignalsBound = true

                strategyService.strategiesChanged.connect(function() {
                    console.log("策略数据已更新，刷新列表")
                    // 清除 QML 本地即时状态，让 C++ 真实状态透出
                    var overrides = localStatusOverrides
                    var changed = false
                    for (var i = 0; i < strategyViewModel.count; ++i) {
                        var row = strategyViewModel.getRow(i)
                        var sid = row ? (row.strategyId || "") : ""
                        if (sid && overrides.hasOwnProperty(sid) && row.displayStatus) {
                            delete overrides[sid]
                            changed = true
                        }
                    }
                    if (changed) localStatusOverrides = overrides
                    statusRefreshCounter++
                    rebuildStrategyVisibleModel()
                    syncSelectedStrategy()
                })

                strategyService.created.connect(function(strategyId, strategyData) {
                    console.log("新策略创建成功，ID:", strategyId, "名称:", strategyData.strategyName || strategyData.name || "")
                    rebuildStrategyVisibleModel()
                })
            }

            rebuildStrategyVisibleModel()
            
            console.log("策略服务初始化完成，视图模型已绑定")
        } else {
            console.error("无法获取StrategyService实例")
        }
    }
    
    // 包装器函数：获取策略数量
    function getStrategyCount() {
        if (strategyViewModel) {
            return strategyViewModel.count
        }
        return 0
    }
    
    // 包装器函数：获取策略数据
    function getStrategyData(index) {
        if (strategyViewModel && index >= 0 && index < strategyViewModel.count) {
            return strategyViewModel.getRow(index)
        }
        return null
    }

    function buildStrategyLibrarySearchBlob(strategy) {
        if (!strategy) {
            return ""
        }

        var tags = buildStrategyRuntimeTags(strategy)
        return [
            strategy.strategyName || strategy.name || "",
            strategy.description || "",
            Array.isArray(tags) ? tags.join(" ") : String(tags || "")
        ].join(" ").toLowerCase()
    }

    function strategyMatchesLibrarySearch(strategy) {
        var keyword = String(strategyLibrarySearchText || "").trim().toLowerCase()
        if (!keyword) {
            return true
        }

        return buildStrategyLibrarySearchBlob(strategy).indexOf(keyword) >= 0
    }

    function rebuildStrategyVisibleModel() {
        strategyVisibleModel.clear()

        if (!strategyViewModel) {
            return
        }

        for (var index = 0; index < strategyViewModel.count; ++index) {
            var row = strategyViewModel.getRow(index)
            if (!strategyMatchesLibrarySearch(row)) {
                continue
            }

            strategyVisibleModel.append({
                sourceIndex: index,
                strategyId: row ? (row.strategyId || "") : ""
            })
        }
    }

    function currentTradingConfiguration() {
        marketSessionRevision
        if (tradingConnectionConfigService && tradingConnectionConfigService.currentConfiguration) {
            return tradingConnectionConfigService.currentConfiguration
        }
        return ({})
    }

    function currentMarketCalendarSnapshot() {
        marketSessionRevision
        if (tradingMarketCalendarService && tradingMarketCalendarService.currentSessionSnapshot) {
            return tradingMarketCalendarService.currentSessionSnapshot
        }
        return ({})
    }

    function normalizeSymbolValue(symbol) {
        return NormalizeUtils.normalizeSymbolValue(symbol);
    }

    function appendSymbolCollection(targetSymbols, seenSymbols, rawCollection) {
        var addSymbol = function(symbol) {
            var normalized = normalizeSymbolValue(symbol)
            if (!normalized || seenSymbols[normalized]) {
                return
            }

            seenSymbols[normalized] = true
            targetSymbols.push(normalized)
        }

        if (Array.isArray(rawCollection)) {
            for (var index = 0; index < rawCollection.length; ++index) {
                addSymbol(rawCollection[index])
            }
            return
        }

        if (rawCollection !== undefined && rawCollection !== null) {
            var rawText = String(rawCollection).trim()
            if (!rawText) {
                return
            }

            if (rawText.charAt(0) === "[") {
                try {
                    var parsed = JSON.parse(rawText)
                    if (Array.isArray(parsed)) {
                        for (var parsedIndex = 0; parsedIndex < parsed.length; ++parsedIndex) {
                            addSymbol(parsed[parsedIndex])
                        }
                        return
                    }
                } catch (error) {
                }
            }

            rawText.split(/[,;\s，；]+/).forEach(addSymbol)
        }
    }

    function getStrategyDisplayStatus(strategy) {
        return DataAccess.getStrategyDisplayStatus(strategy);
    }

    function currentRuntimeSnapshot(strategy) {
        runtimeSnapshotRevision
        if (!strategy || !tradingRuntimeStatusService || !tradingRuntimeStatusService.sessionSnapshotForStrategy) {
            return ({})
        }

        var strategyId = strategy.strategyId || ""
        if (!strategyId) {
            return ({})
        }

        var snapshot = tradingRuntimeStatusService.sessionSnapshotForStrategy(strategyId) || ({})
        if (snapshot && Object.keys(snapshot).length > 0) {
            return snapshot
        }

        var configuration = currentTradingConfiguration()
        if (isStrategyBoundToTradingConfiguration(strategy, configuration)
                && tradingRuntimeStatusService.sessionSnapshotForAccount
                && configuration.accountId) {
            return tradingRuntimeStatusService.sessionSnapshotForAccount(configuration.accountId) || ({})
        }

        return ({})
    }

    function isRunningStrategy(strategy) {
        return DataAccess.isRunningStrategy(strategy);
    }

    function hasRuntimeSnapshotData(snapshot) {
        return DataAccess.hasRuntimeSnapshotData(snapshot);
    }

    function normalizeRuntimeDisplayValue(value, fallbackValue) {
        return FormatUtils.normalizeRuntimeDisplayValue(value, fallbackValue);
    }

    function formatRuntimeBooleanValue(value, trueText, falseText, fallbackText) {
        return FormatUtils.formatRuntimeBooleanValue(value, trueText, falseText, fallbackText);
    }

    function getStrategyDisplayStatusLabel(status) {
        return DataAccess.getStrategyDisplayStatusLabel(status);
    }

    function getRuntimeDiagnosticColor(status) {
        switch (status) {
        case "RUNNING":
            return successGreen
        case "WAIT_OPEN":
        case "STARTING":
            return accentBlue
        case "STOPPING":
            return warningAmber
        case "ERROR":
            return riseRed
        default:
            return textPrimary
        }
    }

    function describeStrategyBinding(strategy, configuration) {
        var strategyId = strategy ? (strategy.strategyId || "") : ""
        if (!strategyId) {
            return "未选择策略"
        }

        var config = configuration || ({})
        if (!isStrategyBoundToTradingConfiguration(strategy, config)) {
            return "未绑定"
        }

        if (!config.enabled) {
            return "已绑定未启用"
        }

        if (config.readOnly) {
            return "只读绑定"
        }

        return "可交易绑定"
    }

    function isStrategyBoundToTradingConfiguration(strategy, configuration) {
        return DataAccess.isStrategyBoundToTradingConfiguration(strategy, configuration);
    }

    function showActionFeedback(message, isError) {
        var normalizedMessage = String(message || "").trim()
        if (!normalizedMessage) {
            return
        }

        actionFeedbackMessage = normalizedMessage
        actionFeedbackError = !!isError
        strategyDialogs.actionFeedbackDialog.open()
    }

    function resolveStrategyIdentifier(strategyCandidate) {
        return DataAccess.resolveStrategyIdentifier(strategyCandidate);
    }

    function resolveStrategyName(strategyCandidate, strategyId) {
        return DataAccess.resolveStrategyName(strategyCandidate, strategyId);
    }

    function resolveStrategyDetail(strategyCandidate) {
        var strategyId = resolveStrategyIdentifier(strategyCandidate)
        if (strategyId && strategyService && strategyService.get) {
            var detail = strategyService.get(strategyId) || ({})
            if (detail && Object.keys(detail).length > 0) {
                return detail
            }
        }

        return strategyCandidate || ({})
    }

    function cloneStartRequestMap() {
        var snapshot = ({})
        for (var key in recentStartRequests) {
            if (recentStartRequests.hasOwnProperty(key)) {
                snapshot[key] = recentStartRequests[key]
            }
        }
        return snapshot
    }

    function isStartRequestInFlight(strategyId) {
        var normalizedStrategyId = String(strategyId || "").trim()
        if (!normalizedStrategyId) {
            return false
        }

        var lastRequestAt = recentStartRequests[normalizedStrategyId]
        if (!lastRequestAt) {
            return false
        }

        return (Date.now() - lastRequestAt) < 1500
    }

    function markStartRequest(strategyId) {
        var normalizedStrategyId = String(strategyId || "").trim()
        if (!normalizedStrategyId) {
            return
        }

        var nextRequests = cloneStartRequestMap()
        nextRequests[normalizedStrategyId] = Date.now()
        recentStartRequests = nextRequests
    }

    function clearStartRequest(strategyId) {
        var normalizedStrategyId = String(strategyId || "").trim()
        if (!normalizedStrategyId || !recentStartRequests[normalizedStrategyId]) {
            return
        }

        var nextRequests = cloneStartRequestMap()
        delete nextRequests[normalizedStrategyId]
        recentStartRequests = nextRequests
    }

    function startStrategyFromCard(strategyCandidate) {
        var strategyId = resolveStrategyIdentifier(strategyCandidate)
        var strategyName = resolveStrategyName(strategyCandidate, strategyId)
        if (!strategyId) {
            showActionFeedback("当前策略缺少 ID，无法启动", true)
            return
        }

        if (isStartRequestInFlight(strategyId)) {
            return
        }

        markStartRequest(strategyId)

        var startGate = getStrategyStartGateState(strategyCandidate)
        if (!startGate.canStart) {
            clearStartRequest(strategyId)
            showActionFeedback("策略“" + strategyName + "”当前不满足启动条件，请先修复后再启动", true)
            return
        }

        if (!tradingConnectionConfigService || !tradingConnectionConfigService.bindStrategyConfiguration) {
            clearStartRequest(strategyId)
            showActionFeedback("交易绑定服务不可用，无法启动策略", true)
            return
        }

        var bindingResult = tradingConnectionConfigService.addBoundStrategyConfiguration
            ? (tradingConnectionConfigService.addBoundStrategyConfiguration(strategyId, strategyName, true, false) || ({}))
            : (tradingConnectionConfigService.bindStrategyConfiguration(strategyId, strategyName, true, false) || ({}))
        if (!bindingResult.success) {
            clearStartRequest(strategyId)
            showActionFeedback(bindingResult.message || ("策略“" + strategyName + "”绑定失败"), true)
            return
        }

        var wantsLiveTrading = !!bindingResult.enabled && !bindingResult.readOnly
        if (wantsLiveTrading && !bindingResult.readyForTrading) {
            clearStartRequest(strategyId)
            var startupGateMessage = StartupGateFormatter.blockedActionMessage(
                bindingResult.startupGate || ({}),
                bindingResult.message || ("策略“" + strategyName + "”当前不满足实盘启动条件"))
            showActionFeedback(startupGateMessage, true)
            return
        }

        if (wantsLiveTrading && (!tradeExecutionBridge || !tradeExecutionBridge.isLiveBridgeReady
                || !tradeExecutionBridge.isLiveBridgeReady())) {
            clearStartRequest(strategyId)
            var liveBridgeError = (tradeExecutionBridge && tradeExecutionBridge.liveBridgeStatusMessage)
                ? String(tradeExecutionBridge.liveBridgeStatusMessage() || "").trim()
                : ""
            showActionFeedback(liveBridgeError || ("策略“" + strategyName + "”绑定成功，但共享交易会话未就绪"), true)
            return
        }

        // 先切 QML 按钮状态，不等待 C++
        var overrides = localStatusOverrides
        overrides[strategyId] = "启动中"
        localStatusOverrides = overrides
        statusRefreshCounter = statusRefreshCounter + 1

        if (strategyService && strategyService.start && !strategyService.start(strategyId)) {
            clearStartRequest(strategyId)
            var failOverrides = localStatusOverrides
            delete failOverrides[strategyId]
            localStatusOverrides = failOverrides
            statusRefreshCounter = statusRefreshCounter + 1
            showActionFeedback("策略“" + strategyName + "”已绑定，但激活失败", true)
            return
        }

        syncSelectedStrategy()
        console.log("StrategyLibraryPage: 启动策略", strategyName)
    }

    function stopStrategyFromCard(strategyCandidate) {
        var strategyId = resolveStrategyIdentifier(strategyCandidate)
        var strategyName = resolveStrategyName(strategyCandidate, strategyId)
        if (!strategyId) {
            console.log("StrategyLibraryPage: 停止失败，缺少策略ID")
            return
        }

        var overrides = localStatusOverrides
        overrides[strategyId] = "停止中"
        localStatusOverrides = overrides
        statusRefreshCounter = statusRefreshCounter + 1

        if (strategyService && strategyService.stop) {
            strategyService.stop(strategyId)
        }
        syncSelectedStrategy()
        console.log("StrategyLibraryPage: 停止策略", strategyName)
    }

    function truncateDisplayText(value, maxLength) {
        return FormatUtils.truncateDisplayText(value, maxLength);
    }

    function getMarketCalendarPhaseLabel(snapshot) {
        return DataAccess.getMarketCalendarPhaseLabel(snapshot);
    }

    function getMarketCalendarSourceTag(snapshot) {
        return DataAccess.getMarketCalendarSourceTag(snapshot);
    }

    function getMarketCalendarStatusAccent(snapshot) {
        if (!snapshot || Object.keys(snapshot).length === 0) {
            return textSecondary
        }

        if (snapshot.sessionOpen) {
            return successGreen
        }

        if (!snapshot.holidayAware) {
            return warningAmber
        }

        return accentBlue
    }

    function getStrategyStartGateState(strategyCandidate) {
        return DataAccess.getStrategyStartGateState(strategyCandidate);
    }

    function getStrategyStartActionLabel(strategyCandidate) {
        return DataAccess.getStrategyStartActionLabel(strategyCandidate);
    }

    function getStrategyStaticStartupGatePreview(strategyCandidate) {
        var startGate = getStrategyStartGateState(strategyCandidate)
        if (!startGate.canStart || !tradingConnectionConfigService || !tradingConnectionConfigService.evaluateStartupGate) {
            return ({})
        }

        var startupGate = tradingConnectionConfigService.evaluateStartupGate(false) || ({})
        if (!startupGate || Object.keys(startupGate).length === 0 || startupGate.ready) {
            return ({})
        }

        var ignoredReasonCodes = {
            trading_connection_disabled: true,
            read_only_mode: true,
            bound_strategy_missing: true,
            runtime_strategy_missing: true
        }
        if (ignoredReasonCodes[String(startupGate.reasonCode || "")]) {
            return ({})
        }

        return startupGate
    }

    function getStrategyStartActionHint(strategyCandidate) {
        return StartupGateFormatter.compactHintText(getStrategyStaticStartupGatePreview(strategyCandidate))
    }

    function handleStrategyStartActionHint(strategyCandidate) {
        var startupGate = getStrategyStaticStartupGatePreview(strategyCandidate)
        if (startupGate && Object.keys(startupGate).length > 0) {
            showActionFeedback(
                StartupGateFormatter.blockedActionMessage(startupGate, "当前实盘启动仍受 StartupGate 限制"),
                true)
        }
    }

    function getConfigurationSymbols(configuration) {
        return DataAccess.getConfigurationSymbols(configuration);
    }

    function getStrategySubscriptionSyncLabel(strategy, configuration) {
        return DataAccess.getStrategySubscriptionSyncLabel(strategy, configuration);
    }

    function getStrategySubscriptionSyncAccent(strategy, configuration) {
        var label = getStrategySubscriptionSyncLabel(strategy, configuration)
        if (label === "已配置") {
            return successGreen
        }
        if (label === "未配置") {
            return warningAmber
        }
        return textSecondary
    }

    function buildStrategyRuntimeTags(strategy) {
        var tags = []
        if (!strategy) {
            return tags
        }

        var configuration = currentTradingConfiguration()
        var snapshot = currentRuntimeSnapshot(strategy)
        var marketCalendarSnapshot = currentMarketCalendarSnapshot()
        var strategyId = strategy.strategyId || ""
        var isBound = strategyId !== "" && isStrategyBoundToTradingConfiguration(strategy, configuration)
        var displayStatus = getStrategyDisplayStatus(strategy)
        tags.push(getStrategyDisplayStatusLabel(displayStatus))

        if (hasRuntimeSnapshotData(snapshot)) {
            if (snapshot.hasError || normalizeRuntimeDisplayValue(snapshot.state, "") === "ERROR") {
                tags.push("运行异常")
            } else if (snapshot.connected === false) {
                tags.push("会话未连接")
            } else if (snapshot.initialized === false) {
                tags.push("未初始化")
            } else {
                tags.push("真实会话")
            }

            if (snapshot.accountId) {
                tags.push("账户 " + snapshot.accountId)
            }
        } else if (isBound) {
            tags.push(getMarketCalendarPhaseLabel(marketCalendarSnapshot))
            tags.push(getMarketCalendarSourceTag(marketCalendarSnapshot))
            if (marketCalendarSnapshot.error) {
                tags[2] = "日历回退"
            } else if (configuration.accountId) {
                tags[2] = "账户 " + configuration.accountId
            }
        } else {
            tags.push("未绑定")
        }

        return tags.slice(0, 3)
    }

    function buildStrategyCardDescription(strategy) {
        if (!strategy) {
            return "暂无描述"
        }

        var baseDescription = normalizeRuntimeDisplayValue(strategy.description, "暂无描述")
        var configuration = currentTradingConfiguration()
        var snapshot = currentRuntimeSnapshot(strategy)
        var marketCalendarSnapshot = currentMarketCalendarSnapshot()
        var strategyId = strategy.strategyId || ""
        var isBound = strategyId !== "" && isStrategyBoundToTradingConfiguration(strategy, configuration)

        if (hasRuntimeSnapshotData(snapshot)) {
            if (snapshot.lastError) {
                return "运行错误: " + truncateDisplayText(snapshot.lastError, 42)
            }

            var runtimeSummary = []
            if (snapshot.accountId) {
                runtimeSummary.push("账户 " + snapshot.accountId)
            }
            runtimeSummary.push("会话 " + normalizeRuntimeDisplayValue(snapshot.stateLabel, getStrategyDisplayStatusLabel(getStrategyDisplayStatus(strategy))))
            return runtimeSummary.join(" · ")
        }

        if (isBound) {
            if (marketCalendarSnapshot.error) {
                return "日历回退: " + truncateDisplayText(marketCalendarSnapshot.error, 42)
            }

            var boundSummary = describeStrategyBinding(strategy, configuration)
            var calendarSummary = getMarketCalendarPhaseLabel(marketCalendarSnapshot)
            if (calendarSummary !== "--") {
                boundSummary += " · " + calendarSummary
            }
            var calendarSource = getMarketCalendarSourceTag(marketCalendarSnapshot)
            if (calendarSource) {
                boundSummary += " · " + calendarSource
            }
            return boundSummary
        }

        return baseDescription
    }
    
    // 包装器函数：获取运行策略数量
    function getRunningStrategyCount() {
        var count = 0
        if (strategyViewModel) {
            for (var i = 0; i < strategyViewModel.count; i++) {
                var strategy = strategyViewModel.getRow(i)
                if (strategy && isRunningStrategy(strategy)) {
                    count++
                }
            }
        }
        return count
    }
    
    // 包装器函数：获取指定索引的运行策略
    function getRunningStrategy(runningIndex) {
        var runningCount = 0
        if (strategyViewModel) {
            for (var i = 0; i < strategyViewModel.count; i++) {
                var strategy = strategyViewModel.getRow(i)
                if (strategy && isRunningStrategy(strategy)) {
                    if (runningCount === runningIndex) {
                        return strategy
                    }
                    runningCount++
                }
            }
        }
        return null
    }

    function requestDeleteStrategy(strategyId, strategyName) {
        if (!strategyId || deleteInProgress) {
            console.warn("删除策略失败：缺少策略ID")
            return
        }

        strategyDialogs.deleteConfirmDialog.strategyId = strategyId
        strategyDialogs.deleteConfirmDialog.strategyName = strategyName || "未命名策略"
        strategyDialogs.deleteConfirmDialog.open()
    }

    function getSelectedStrategySummary() {
        if (strategyViewModel && selectedStrategyId) {
            for (var index = 0; index < strategyViewModel.count; ++index) {
                var row = strategyViewModel.getRow(index)
                var rowId = row ? (row.strategyId || "") : ""
                if (rowId === selectedStrategyId) {
                    return row
                }
            }
        }

        if (selectedStrategyIndex >= 0 && strategyViewModel && strategyViewModel.count > selectedStrategyIndex) {
            return strategyViewModel.getRow(selectedStrategyIndex)
        }
        return null
    }

    function selectStrategyAt(index) {
        if (!strategyViewModel || index < 0 || index >= strategyViewModel.count) {
            selectedStrategyIndex = -1
            selectedStrategyId = ""
            return
        }

        var selectedRow = strategyViewModel.getRow(index)
        selectedStrategyIndex = index
        selectedStrategyId = selectedRow ? (selectedRow.strategyId || "") : ""
        strategySelected(selectedRow ? (selectedRow.strategyName || selectedRow.name || "") : "")
    }

    function selectStrategyById(strategyId) {
        var normalizedId = String(strategyId || "").trim()
        if (!normalizedId || !strategyViewModel) {
            return
        }

        for (var index = 0; index < strategyViewModel.count; ++index) {
            var row = strategyViewModel.getRow(index)
            var rowId = row ? (row.strategyId || "") : ""
            if (String(rowId) === normalizedId) {
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
                var rowId = row ? (row.strategyId || "") : ""
                if (rowId === selectedStrategyId) {
                    selectedStrategyIndex = index
                    return
                }
            }
        }

        if (selectedStrategyIndex >= 0 && selectedStrategyIndex < strategyViewModel.count) {
            var currentRow = strategyViewModel.getRow(selectedStrategyIndex)
            selectedStrategyId = currentRow ? (currentRow.strategyId || "") : ""
            return
        }

        selectStrategyAt(0)
    }

    function getSelectedStrategyDetail(strategyId) {
        if (!strategyService || !strategyId || !strategyService.get) {
            return ({})
        }

        return strategyService.get(strategyId) || ({})
    }

    function toPlainJsValue(rawValue) {
        return PureUtils.toPlainJsValue(rawValue);
    }

    function strategyHasEditableRulePayload(strategyObject) {
        return DataAccess.strategyHasEditableRulePayload(strategyObject);
    }

    function enrichStrategyForEdit(strategyObject, strategyId) {
        var source = toPlainJsValue(strategyObject) || ({})
        var enriched = ({})
        for (var key in source) {
            enriched[key] = source[key]
        }

        if (!strategyService || !strategyId || !strategyService.get) {
            return enriched
        }

        var detail = toPlainJsValue(strategyService.get(strategyId)) || ({})
        var detailParameters = toPlainJsValue(detail.parameters) || ({})
        if (Object.keys(detailParameters).length === 0) {
            return enriched
        }

        var mergedParameters = ({})
        var baseParameters = toPlainJsValue(enriched.parameters) || ({})
        for (var baseKey in baseParameters) {
            mergedParameters[baseKey] = baseParameters[baseKey]
        }
        for (var detailKey in detailParameters) {
            mergedParameters[detailKey] = detailParameters[detailKey]
        }

        enriched.parameters = mergedParameters

        return enriched
    }

    function resolveStrategyForEdit(strategyCandidate) {
        var candidate = toPlainJsValue(strategyCandidate) || ({})
        var strategyId = ""

        if (typeof candidate === "string") {
            strategyId = candidate
        } else {
            strategyId = candidate.strategyId || ""
        }

        if (strategyId && strategyService && strategyService.get) {
            var detail = toPlainJsValue(strategyService.get(strategyId)) || ({})
            var enrichedDetail = enrichStrategyForEdit(detail, strategyId)
            if (enrichedDetail && Object.keys(enrichedDetail).length > 0) {
                return enrichedDetail
            }
        }

        if (typeof candidate === "object") {
            return enrichStrategyForEdit(candidate, strategyId)
        }

        return ({})
    }

    function openStrategyCreation(strategyDetail) {
        var resolvedStrategy = resolveStrategyForEdit(strategyDetail)
        strategyCreationLoader.pendingStrategyData = resolvedStrategy || ({})
        strategyCreationLoader._strategyDataConsumed = false
        strategyCreationLoader.active = true

        if (strategyCreationLoader.item) {
            if (!strategyCreationLoader._strategyDataConsumed && resolvedStrategy && Object.keys(resolvedStrategy).length > 0 && strategyCreationLoader.item.loadStrategyForEdit) {
                strategyCreationLoader._strategyDataConsumed = true
                strategyCreationLoader.item.loadStrategyForEdit(resolvedStrategy)
            } else if (!strategyCreationLoader._strategyDataConsumed && strategyCreationLoader.item.resetForm) {
                strategyCreationLoader._strategyDataConsumed = true
                strategyCreationLoader.item.resetForm()
            }

        }
    }

    function showBacktestResult() {
        var r = backtestResult || ({})
        var m = r.performance || r.metrics || ({})
        var ann = (Number(m.annualizedReturn || 0) * 100).toFixed(2)
        var sharpe = Number(m.sharpeRatio || 0).toFixed(2)
        var dd = (Number(m.maxDrawdown || 0) * 100).toFixed(1)
        var win = (Number(m.winRate || 0) * 100).toFixed(1)
        var fullK = Number(m.fullKelly || 0)
        var halfK = Number(m.halfKelly || 0)
        var msg = "年化 " + ann + "% | 夏普 " + sharpe + " | 回撤 " + dd + "% | 胜率 " + win + "% | 半凯 " + (halfK * 100).toFixed(1) + "%"
        console.log("策略回测完成: " + msg)
        showActionFeedback(msg, false)
        showPerformance = false
        showBacktestWorkbench = true
        // 策略卡片数据由 C++ StrategyBacktestBridge 直接更新 StrategyListModel 单行，QML 不再调 init()
        backtestWorkbenchMode = "analysis"
        backtestWorkbenchLoadedOnce = true
        // 绩效页数据由 StrategyBridge.strategiesChanged 信号驱动刷新，无需主动调用
    }


    function openBacktestWorkbench(strategyId, modeValue) {
        if (strategyId) {
            selectStrategyById(strategyId)
        }

        var normalizedMode = String(modeValue || "").trim().toLowerCase()
        backtestWorkbenchMode = normalizedMode === "analysis" ? "analysis" : "workbench"
        backtestWorkbenchLoadedOnce = true
        showBacktestWorkbench = true
        backtestWorkbenchStatusText = ""
        if (backtestWorkbenchLoader.item) {
            backtestWorkbenchLoader.item.selectedStrategyId = selectedStrategyId
            backtestWorkbenchLoader.item.selectedStrategyName = getSelectedStrategySummary() ? (getSelectedStrategySummary().strategyName || getSelectedStrategySummary().name || "") : ""
        }
    }

    function closeBacktestWorkbench() {
        showBacktestWorkbench = false
    }

    function openTuning() {
        if (!selectedStrategyId) return
        var summary = getSelectedStrategySummary() || ({})
        var strategyName = summary.strategyName || summary.name || ""
        var typeIndex = summary.strategyTypeIndex || summary.typeIndex || 0
        tuningConfigPanel.strategyId = selectedStrategyId
        tuningConfigPanel.strategyTypeIndex = typeIndex
        tuningConfigPanel.strategyName = strategyName
        tuningConfigPanel.open()
    }

    function closeTuning() {
        showTuning = false
    }

    // ── 参数调优配置弹窗 ──
    StrategyCreationComponents.ParameterTuningConfigPanel {
        id: tuningConfigPanel
        strategyId: strategyLibraryPage.selectedStrategyId

        onTuningStarted: function(config) {
            // 启动 C++ 桥接层调优
            Bridge.ParameterTuningBridge.startTuning(
                config.strategyId, config.strategyTypeIndex, config)
            // 切换到结果面板
            strategyLibraryPage.showTuning = true
        }
    }

    // 数据模型（完全使用数据库数据，移除模拟数据）
    ListModel {
        id: strategyModel
        // 不再使用硬编码数据，完全依赖数据库
    }

    ListModel {
        id: strategyVisibleModel
    }
    
    // 定时器 - 用于自动滚动
    Timer {
        id: autoScrollTimer
        interval: 3000  // 3秒切换一次
        running: true
        repeat: true
        onTriggered: {
            var runningCount = 0;
            
            // 只使用数据库数据
            if (strategyViewModel && strategyViewModel.count > 0) {
                for (var i = 0; i < strategyViewModel.count; i++) {
                    var strategy = strategyViewModel.getRow(i);
                    if (strategy && isRunningStrategy(strategy)) {
                        runningCount++;
                    }
                }
            }
            
            if (runningCount > 1) {
                runningStrategyIndex++;
                if (runningStrategyIndex >= runningCount) {
                    runningStrategyIndex = 0;
                }
            }
        }
    }

    // 主布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        NavigationComponents.ModeTitleBar {
            Layout.fillWidth: true
            currentMode: !strategyLibraryPage.showBacktestWorkbench && !strategyLibraryPage.showPerformance && !strategyLibraryPage.showTuning
                ? "library"
                : (strategyLibraryPage.backtestWorkbenchMode === "analysis" ? "backtest_analysis"
                    : strategyLibraryPage.showPerformance ? "performance"
                    : strategyLibraryPage.showTuning ? "tuning" : "backtest")
            showBackButton: false
            modeOptions: [
                { value: "library", label: "策略库" },
                { value: "backtest", label: "策略回测" },
                { value: "backtest_analysis", label: "回测分析" },
                { value: "tuning", label: "参数调优" },
                { value: "performance", label: "策略绩效" }
            ]
            modeTitleMap: {
                "library": "策略库",
                "backtest": "策略回测",
                "backtest_analysis": "回测分析",
                "tuning": "参数调优",
                "performance": "策略绩效"
            }
            modeSubtitleMap: {
                "library": "浏览并管理策略，新建入口保留在策略库页。",
                "backtest": "在策略库内直接配置并运行当前策略回测。",
                "backtest_analysis": "独立展示最近回测与历史对比结果，不承载回测配置。",
                "tuning": "自动搜索最优参数组合，提升策略绩效。",
                "performance": "查看策略历史回测记录与绩效对比。"
            }
            onModeSelected: function(mode) {
                if (mode === "library") {
                    strategyLibraryPage.showPerformance = false
                    strategyLibraryPage.showBacktestWorkbench = false
                    strategyLibraryPage.showTuning = false
                    return
                }
                if (mode === "performance") {
                    strategyLibraryPage.showPerformance = true
                    strategyLibraryPage.showBacktestWorkbench = false
                    strategyLibraryPage.showTuning = false
                    return
                }
                if (mode === "tuning") {
                    strategyLibraryPage.showPerformance = false
                    strategyLibraryPage.showBacktestWorkbench = false
                    strategyLibraryPage.openTuning()
                    return
                }
                if (mode === "backtest") {
                    strategyLibraryPage.showPerformance = false
                    strategyLibraryPage.showBacktestWorkbench = false
                    strategyLibraryPage.showTuning = false
                    strategyLibraryPage.openBacktestWorkbench(strategyLibraryPage.selectedStrategyId, "workbench")
                    return
                }
                if (mode === "backtest_analysis") {
                    strategyLibraryPage.showPerformance = false
                    strategyLibraryPage.showBacktestWorkbench = false
                    strategyLibraryPage.showTuning = false
                    strategyLibraryPage.openBacktestWorkbench(strategyLibraryPage.selectedStrategyId, "analysis")
                    return
                }
            }
        }

        ScrollView {
            id: scrollView
            visible: !strategyLibraryPage.showBacktestWorkbench && !strategyLibraryPage.showPerformance && !strategyLibraryPage.showTuning
            Layout.fillWidth: true
            Layout.fillHeight: !strategyLibraryPage.showBacktestWorkbench && !strategyLibraryPage.showPerformance && !strategyLibraryPage.showTuning
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: Math.max(0, Math.min(contentMaxWidth, scrollView.width - pageSidePadding * 2))
                x: Math.max(0, (scrollView.width - width) / 2)
                spacing: spacingLarge

                StrategyLibrarySearchBar {
                    id: strategyLibrarySearchBar
                    page: strategyLibraryPage
                }

                StrategyGridPanel {
                    id: strategyGridPanel
                    page: strategyLibraryPage
                }

                StrategyDetailPanel {
                    id: strategyDetailPanel
                    page: strategyLibraryPage
                }

                StrategyRuntimeDiagnostics {
                    id: strategyRuntimeDiagnostics
                    page: strategyLibraryPage
                }

                Item {
                    visible: !strategyLibraryPage.showBacktestWorkbench && !strategyLibraryPage.showPerformance && !strategyLibraryPage.showTuning
                    Layout.fillWidth: true
                    Layout.preferredHeight: spacingXLarge
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: strategyLibraryPage.showPerformance
            visible: strategyLibraryPage.showPerformance

            Loader {
                id: performanceLoader
                anchors.fill: parent
                asynchronous: true
                active: strategyLibraryPage.showPerformance
                visible: status === Loader.Ready && strategyLibraryPage.showPerformance
                source: "../../components/Strategy/PerformanceHistoryList.qml"
                onLoaded: {
                    if (!item) return
                    item.selectedStrategyId = strategyLibraryPage.selectedStrategyId
                    item.selectedStrategyName = strategyLibraryPage.getSelectedStrategySummary()
                        ? (strategyLibraryPage.getSelectedStrategySummary().strategyName || strategyLibraryPage.getSelectedStrategySummary().name || "") : ""
                }
            }
        }

        // ── 参数调优结果面板 ──
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: strategyLibraryPage.showTuning
            visible: strategyLibraryPage.showTuning

            Loader {
                id: tuningResultLoader
                anchors.fill: parent
                asynchronous: true
                active: strategyLibraryPage.showTuning
                visible: status === Loader.Ready && strategyLibraryPage.showTuning
                source: "qrc:/page/strategies/ParameterTuningResultPanel.qml"
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: strategyLibraryPage.showBacktestWorkbench
            visible: strategyLibraryPage.showBacktestWorkbench

            Loader {
                id: backtestWorkbenchLoader
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(0, Math.min(contentMaxWidth, parent.width - pageSidePadding * 2))
                asynchronous: true
                active: backtestWorkbenchLoadedOnce
                visible: status === Loader.Ready && strategyLibraryPage.showBacktestWorkbench
                source: strategyLibraryPage.backtestWorkbenchMode === "analysis"
                    ? "qrc:/page/strategies/BacktestResultAnalysisView.qml"
                    : "qrc:/components/Strategy/StrategyBacktestParams.qml"

                onLoaded: {
                    strategyLibraryPage.backtestWorkbenchStatusText = ""
                    if (!item) {
                        return
                    }
                    if (typeof item.selectedStrategyId !== "undefined") {
                        item.selectedStrategyId = strategyLibraryPage.selectedStrategyId
                        item.selectedStrategyName = strategyLibraryPage.getSelectedStrategySummary() ? (strategyLibraryPage.getSelectedStrategySummary().strategyName || strategyLibraryPage.getSelectedStrategySummary().name || "") : ""
                    }
                    if (typeof item.strategyId !== "undefined") {
                        item.strategyId = strategyLibraryPage.selectedStrategyId
                        item.strategyName = strategyLibraryPage.backtestResult.strategyName || strategyLibraryPage.selectedStrategyName || ""
                    }
                    if (typeof item.backtestResult !== "undefined") {
                        item.backtestResult = strategyLibraryPage.backtestResult
                    }
                    if (typeof item.backToWorkbench !== "undefined") {
                        item.backToWorkbench.connect(function() {
                            strategyLibraryPage.openBacktestWorkbench(strategyLibraryPage.selectedStrategyId, "workbench")
                        })
                    }
                    var cdc = CleanedDataController
                    if (cdc && typeof cdc.initialize === "function") { cdc.initialize() }
                    if (typeof item.cleanedDataController !== "undefined") {
                        item.cleanedDataController = cdc
                    }
                    if (typeof item.startBacktestRequested !== "undefined") {
                        item.startBacktestRequested.connect(function() {
                            console.log("StrategyBacktestParams: 用户点击开始回测")
                        })
                    }
                    if (typeof item.backtestFinished !== "undefined") {
                        item.backtestFinished.connect(function(result) {
                            strategyLibraryPage.backtestResult = result || ({})
                            strategyLibraryPage.showBacktestResult()
                        })
                    }
                }

                onStatusChanged: {
                    if (status === Loader.Loading) {
                        strategyLibraryPage.backtestWorkbenchStatusText = "策略回测页加载中..."
                        return
                    }
                    if (status === Loader.Ready) {
                        strategyLibraryPage.backtestWorkbenchStatusText = ""
                        return
                    }
                    if (status === Loader.Error) {
                        strategyLibraryPage.backtestWorkbenchStatusText = "策略回测页加载失败，请检查运行日志。"
                    }
                }
            }

            Rectangle {
                visible: backtestWorkbenchStatusText.length > 0
                    && backtestWorkbenchLoader.status !== Loader.Ready
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(0, Math.min(contentMaxWidth, parent.width - pageSidePadding * 2))
                anchors.topMargin: 12
                radius: borderRadiusMedium
                color: Qt.rgba(245 / 255, 158 / 255, 11 / 255, 0.12)
                border.width: 1
                border.color: Qt.rgba(245 / 255, 158 / 255, 11 / 255, 0.35)
                implicitHeight: loadStatusText.implicitHeight + 24
                z: 1

                Text {
                    id: loadStatusText
                    anchors.fill: parent
                    anchors.margins: 12
                    text: strategyLibraryPage.backtestWorkbenchStatusText
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: warningAmber
                }
            }
        }

    }
    
    // 对话框集合
    StrategyLibraryDialogs {
        id: strategyDialogs
        page: strategyLibraryPage
    }
    
    // 新建策略页面加载器 - 使用专业版
    Loader {
        id: strategyCreationLoader
        anchors.fill: parent
        active: false
        source: "StrategyCreationPagePro.qml"
        property var pendingStrategyData: ({})
        property bool _strategyDataConsumed: false

        onLoaded: {
            if (item) {
                item.strategyService = strategyLibraryPage.strategyService

                if (!_strategyDataConsumed && pendingStrategyData && Object.keys(pendingStrategyData).length > 0 && typeof item.loadStrategyForEdit !== "undefined") {
                    _strategyDataConsumed = true
                    item.loadStrategyForEdit(pendingStrategyData)
                } else if (!_strategyDataConsumed && typeof item.resetForm !== "undefined") {
                    _strategyDataConsumed = true
                    item.resetForm()
                }

                // 连接返回信号
                if (typeof item.backClicked !== "undefined") {
                    item.backClicked.connect(function() {
                        console.log("收到创建页面返回信号，关闭创建页面")
                        strategyCreationLoader.pendingStrategyData = ({})
                        strategyCreationLoader._strategyDataConsumed = false
                        strategyCreationLoader.active = false;
                        // 确保返回到策略库页面
                        strategyLibraryPage.forceActiveFocus();
                        // 注意：不需要手动调用syncWithDatabase，因为StrategyService.add()
                        // 列表由策略模型自动刷新
                        console.log("创建页面已关闭，列表将由策略模型刷新")
                    });
                }

            }
        }
    }
    
    // 工具函数
    function updateStrategyParameter(index, value) {
        console.log("更新参数:", index, value);
    }
    
    function resetStrategyParameters() {
        console.log("重置参数");
    }

    function warmupPage() {
        initializeStrategyViewModel()
        syncSelectedStrategy()
    }
    
    function optimizeStrategy() {
        console.log("优化策略");
    }

    function ensurePageServicesReady() {
        if (pageServicesReady || !visible) {
            return
        }

        pageServicesReady = true
        if (uiLifecycleCoordinator && typeof uiLifecycleCoordinator.activateStrategyLibraryPage === "function") {
            uiLifecycleCoordinator.activateStrategyLibraryPage()
        }
        warmupPage()
    }
    
    // 初始化
    Component.onCompleted: {
        console.log("策略库页面初始化完成")
        if (tradingMarketCalendarService && tradingMarketCalendarService.currentSessionSnapshotChanged) {
            tradingMarketCalendarService.currentSessionSnapshotChanged.connect(function() {
                marketSessionRevision++
            })
        }
        if (tradingRuntimeStatusService && tradingRuntimeStatusService.sessionSnapshotsChanged) {
            tradingRuntimeStatusService.sessionSnapshotsChanged.connect(function() {
                runtimeSnapshotRevision++
            })
        }

        // 注意：不再使用硬编码数据作为后备，完全依赖数据库数据
        // 策略数据将通过strategyListModel自动更新
        if (visible) {
            ensurePageServicesReady()
        }
    }

    onVisibleChanged: {
        if (visible) {
            ensurePageServicesReady()
            rebuildStrategyVisibleModel()
        }
    }

    onStrategyLibrarySearchTextChanged: {
        rebuildStrategyVisibleModel()
    }

    onSelectedStrategyIdChanged: {
        if (backtestWorkbenchLoader.item) {
            backtestWorkbenchLoader.item.selectedStrategyId = selectedStrategyId
            backtestWorkbenchLoader.item.selectedStrategyName = getSelectedStrategySummary() ? (getSelectedStrategySummary().strategyName || getSelectedStrategySummary().name || "") : ""
        }
    }

    property var strategyService: StrategyBridge
}

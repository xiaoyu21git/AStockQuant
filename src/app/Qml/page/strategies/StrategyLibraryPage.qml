// pages/StrategyLibraryPage.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtCharts 2.15
import AStock.Bridge 1.0  // 导入C++桥接模块
import "../../components/Strategy" as StrategyComponents
import "../../components/Base" as BaseComponents
import "../../components" as Components
import "../../utils/StrategyDataAdapter.js" as StrategyAdapter
import "../../utils/StrategyStructureAdapter.js" as StructureAdapter
import "../../utils/StartupGateFormatter.js" as StartupGateFormatter

Rectangle {
    id: strategyLibraryPage
    color: "#0F172A"  // primaryBg
    
    // 属性
    property int selectedStrategyIndex: 0
    property string selectedStrategyId: ""
    property bool showFilter: false
    property bool showSorter: false
    property int runningStrategyIndex: 0
    property bool serviceSignalsBound: false
    property bool deleteInProgress: false
    property string actionFeedbackMessage: ""
    property bool actionFeedbackError: false
    property var recentStartRequests: ({})
    property bool focusSymbolPoolAfterOpen: false
    
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
    
    readonly property real borderRadiusMedium: 8
    readonly property real borderRadiusXLarge: 16
    
    // C++服务引用
    property var strategyService: StrategyService
    property var strategyViewModel: null
    readonly property var tradingConnectionConfigService: TradingConnectionConfigService
    readonly property var tradingMarketCalendarService: TradingMarketCalendarService
    readonly property var tradingRuntimeStatusService: TradingRuntimeStatusService
    readonly property var tradeExecutionService: TradeExecutionService
    property int marketSessionRevision: 0
    property int runtimeSnapshotRevision: 0
    
    // 初始化策略服务 - 确保数据自动加载
    function initializeStrategyViewModel() {
        // 获取StrategyService单例
        strategyService = StrategyService
        if (strategyService) {
            // 获取视图模型 - 先获取，以便绑定到UI
            strategyViewModel = strategyService.getViewModel()

            if (!serviceSignalsBound) {
                serviceSignalsBound = true

                strategyService.initializedChanged.connect(function() {
                    if (strategyViewModel && strategyViewModel.count === 0) {
                        selectedStrategyIndex = -1
                    }
                })

                strategyService.cacheLoadedChanged.connect(function() {
                })

                strategyService.strategiesLoaded.connect(function(strategies) {
                    console.log("策略加载完成信号，数量:", strategies.length)
                })

                strategyService.dataChanged.connect(function() {
                    console.log("策略数据已变更，ViewModel会自动更新")
                    syncSelectedStrategy()
                })

                strategyService.strategyCreated.connect(function(strategyId, strategyData) {
                    console.log("新策略创建成功，ID:", strategyId, "名称:", strategyData.strategy_name)
                })
            }
            
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
        return String(symbol || "").trim().toUpperCase()
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
        runtimeSnapshotRevision
        marketSessionRevision
        if (typeof StrategyAdapter !== "undefined" && StrategyAdapter.resolveStrategyRuntimeStatus) {
            return StrategyAdapter.resolveStrategyRuntimeStatus(
                strategy,
                currentTradingConfiguration(),
                currentRuntimeSnapshot(strategy),
                currentMarketCalendarSnapshot(),
                new Date())
        }
        return strategy && strategy.status ? strategy.status : "STOPPED"
    }

    function currentRuntimeSnapshot(strategy) {
        runtimeSnapshotRevision
        if (!strategy || !tradingRuntimeStatusService || !tradingRuntimeStatusService.sessionSnapshotForStrategy) {
            return ({})
        }

        var strategyId = strategy.strategyId || strategy.strategy_id || strategy.id || ""
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
        var displayStatus = getStrategyDisplayStatus(strategy)
        if (typeof StrategyAdapter !== "undefined" && StrategyAdapter.isRunningDisplayStatus) {
            return StrategyAdapter.isRunningDisplayStatus(displayStatus)
        }
        return displayStatus === "RUNNING"
    }

    function hasRuntimeSnapshotData(snapshot) {
        return snapshot && Object.keys(snapshot).length > 0
    }

    function normalizeRuntimeDisplayValue(value, fallbackValue) {
        var fallback = fallbackValue === undefined ? "--" : fallbackValue
        if (value === undefined || value === null) {
            return fallback
        }

        var text = String(value).trim()
        return text.length > 0 ? text : fallback
    }

    function formatRuntimeBooleanValue(value, trueText, falseText, fallbackText) {
        if (value === true) {
            return trueText
        }
        if (value === false) {
            return falseText
        }
        return fallbackText === undefined ? "--" : fallbackText
    }

    function getStrategyDisplayStatusLabel(status) {
        switch (status) {
        case "RUNNING":
            return "运行中"
        case "WAIT_OPEN":
            return "待开盘"
        case "STARTING":
            return "启动中"
        case "STOPPING":
            return "停止中"
        case "ERROR":
            return "异常"
        case "STOPPED":
            return "已停止"
        case "ACTIVE":
            return "已启用"
        case "TESTING":
            return "测试中"
        case "PAUSED":
            return "已暂停"
        case "INACTIVE":
            return "未启用"
        case "ARCHIVED":
            return "已归档"
        default:
            return normalizeRuntimeDisplayValue(status)
        }
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
        var strategyId = strategy ? (strategy.strategyId || strategy.strategy_id || strategy.id || "") : ""
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
        if (typeof StrategyAdapter !== "undefined" && StrategyAdapter.isStrategyBoundToTradingConfiguration) {
            return StrategyAdapter.isStrategyBoundToTradingConfiguration(strategy, configuration)
        }

        var strategyId = strategy ? (strategy.strategyId || strategy.strategy_id || strategy.id || "") : ""
        if (!strategyId) {
            return false
        }

        var config = configuration || ({})
        var boundStrategies = config.boundStrategies || []
        for (var index = 0; index < boundStrategies.length; ++index) {
            var entry = boundStrategies[index] || ({})
            var boundStrategyId = typeof entry === "string"
                ? String(entry || "").trim()
                : String(entry.strategyId || entry.strategy_id || entry.id || "").trim()
            if (boundStrategyId === strategyId) {
                return true
            }
        }

        return String(config.boundStrategyId || "").trim() === strategyId
    }

    function showActionFeedback(message, isError) {
        var normalizedMessage = String(message || "").trim()
        if (!normalizedMessage) {
            return
        }

        actionFeedbackMessage = normalizedMessage
        actionFeedbackError = !!isError
        actionFeedbackDialog.open()
    }

    function resolveStrategyIdentifier(strategyCandidate) {
        if (!strategyCandidate) {
            return ""
        }

        return strategyCandidate.strategyId || strategyCandidate.strategy_id || strategyCandidate.id || ""
    }

    function resolveStrategyName(strategyCandidate, strategyId) {
        if (!strategyCandidate) {
            return strategyId || ""
        }

        return strategyCandidate.strategyName || strategyCandidate.strategy_name || strategyCandidate.name || strategyId || ""
    }

    function resolveStrategyDetail(strategyCandidate) {
        var strategyId = resolveStrategyIdentifier(strategyCandidate)
        if (strategyId && strategyService && strategyService.getStrategyById) {
            var detail = strategyService.getStrategyById(strategyId) || ({})
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
            showActionFeedback("策略“" + strategyName + "”缺少手动股票池、自选股票池，且最近回测没有产出股票池，请先准备股票池后再启动", true)
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

        if (wantsLiveTrading && (!tradeExecutionService || !tradeExecutionService.isLiveBridgeReady
                || !tradeExecutionService.isLiveBridgeReady())) {
            clearStartRequest(strategyId)
            var liveBridgeError = (tradeExecutionService && tradeExecutionService.liveBridgeStatusMessage)
                ? String(tradeExecutionService.liveBridgeStatusMessage() || "").trim()
                : ""
            showActionFeedback(liveBridgeError || ("策略“" + strategyName + "”绑定成功，但共享交易会话未就绪"), true)
            return
        }

        if (strategyService && strategyService.activateStrategy && !strategyService.activateStrategy(strategyId)) {
            clearStartRequest(strategyId)
            showActionFeedback("策略“" + strategyName + "”已绑定，但激活失败", true)
            return
        }

        syncSelectedStrategy()
        showActionFeedback(bindingResult.message || ("已从策略卡片启动“" + strategyName + "”"), false)
    }

    function stopStrategyFromCard(strategyCandidate) {
        var strategyId = resolveStrategyIdentifier(strategyCandidate)
        var strategyName = resolveStrategyName(strategyCandidate, strategyId)
        if (!strategyId) {
            showActionFeedback("当前策略缺少 ID，无法停止", true)
            return
        }

        if (strategyService && strategyService.deactivateStrategy && !strategyService.deactivateStrategy(strategyId)) {
            showActionFeedback("策略“" + strategyName + "”停止失败", true)
            return
        }

        if (tradingConnectionConfigService && tradingConnectionConfigService.removeBoundStrategyConfiguration) {
            var removalResult = tradingConnectionConfigService.removeBoundStrategyConfiguration(strategyId) || ({})
            if (!removalResult.success) {
                showActionFeedback(removalResult.message || ("策略“" + strategyName + "”已停用，但交易绑定移除失败"), true)
                return
            }
        }

        syncSelectedStrategy()
        showActionFeedback("已停止策略“" + strategyName + "”", false)
    }

    function truncateDisplayText(value, maxLength) {
        var text = normalizeRuntimeDisplayValue(value, "")
        if (!text) {
            return ""
        }

        var limit = maxLength === undefined ? 32 : maxLength
        if (text.length <= limit) {
            return text
        }
        return text.substring(0, Math.max(0, limit - 1)) + "..."
    }

    function getMarketCalendarPhaseLabel(snapshot) {
        return normalizeRuntimeDisplayValue(snapshot && snapshot.sessionPhaseLabel, "--")
    }

    function getMarketCalendarSourceTag(snapshot) {
        if (!snapshot || Object.keys(snapshot).length === 0) {
            return "本地时间窗"
        }

        if (snapshot.holidayAware) {
            return "真实日历"
        }

        return snapshot.error ? "日历降级" : "本地回退"
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

    function getStrategySymbolPool(strategy) {
        return StructureAdapter.resolvePersistedStrategySymbolPool(strategy)
    }

    function getStrategySymbolPoolSummary(strategy, maxCount) {
        var symbolPool = getStrategySymbolPool(strategy)
        if (symbolPool.length === 0) {
            return "未绑定标的池"
        }

        var limit = maxCount === undefined ? 2 : maxCount
        var preview = symbolPool.slice(0, limit).join("、")
        if (symbolPool.length > limit) {
            return preview + " 等" + symbolPool.length + "只"
        }
        return preview
    }

    function getLatestBacktestSymbolPool(strategyDetail) {
        var latestBacktest = getLatestBacktestRecord(strategyDetail)
        return StructureAdapter.resolveBacktestRecordSymbolPool(latestBacktest)
    }

    function getLinkedStockPoolState(strategyDetail) {
        if (!strategyDetail) {
            return { poolId: "", poolName: "", symbols: [] }
        }

        var parameters = strategyDetail.parameters || ({})
        var poolId = String(parameters.linked_stock_pool_id || parameters.linkedStockPoolId || "").trim()
        var poolName = String(parameters.linked_stock_pool_name || parameters.linkedStockPoolName || "").trim()
        var linkedSymbols = StructureAdapter.resolveLinkedStockPoolSymbols(strategyDetail)
        return {
            poolId: poolId,
            poolName: poolName,
            symbols: linkedSymbols
        }
    }

    function getStrategyStartGateState(strategyCandidate) {
        var strategyDetail = resolveStrategyDetail(strategyCandidate)
        var manualSymbolPool = getStrategySymbolPool(strategyDetail)
        var linkedStockPool = getLinkedStockPoolState(strategyDetail)
        var linkedStockPoolSymbols = []
        for (var linkedIndex = 0; linkedIndex < linkedStockPool.symbols.length; ++linkedIndex) {
            var normalizedLinkedSymbol = normalizeSymbolValue(linkedStockPool.symbols[linkedIndex])
            if (normalizedLinkedSymbol && linkedStockPoolSymbols.indexOf(normalizedLinkedSymbol) === -1) {
                linkedStockPoolSymbols.push(normalizedLinkedSymbol)
            }
        }
        var latestBacktestSymbolPool = getLatestBacktestSymbolPool(strategyDetail)

        return {
            canStart: manualSymbolPool.length > 0 || linkedStockPoolSymbols.length > 0 || latestBacktestSymbolPool.length > 0,
            manualSymbolPool: manualSymbolPool,
            linkedStockPool: linkedStockPool,
            linkedStockPoolSymbols: linkedStockPoolSymbols,
            latestBacktestSymbolPool: latestBacktestSymbolPool
        }
    }

    function getStrategyStartActionLabel(strategyCandidate) {
        return getStrategyStartGateState(strategyCandidate).canStart ? "启动实盘" : "先生成股票池"
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
        if (!getStrategyStartGateState(strategyCandidate).canStart) {
            return "缺少手动池/自选池/最近回测池"
        }

        return StartupGateFormatter.compactHintText(getStrategyStaticStartupGatePreview(strategyCandidate))
    }

    function handleStrategyStartActionHint(strategyCandidate) {
        if (!getStrategyStartGateState(strategyCandidate).canStart) {
            openStrategyCreationForSymbolPool(strategyCandidate)
            return
        }

        var startupGate = getStrategyStaticStartupGatePreview(strategyCandidate)
        if (startupGate && Object.keys(startupGate).length > 0) {
            showActionFeedback(
                StartupGateFormatter.blockedActionMessage(startupGate, "当前实盘启动仍受 StartupGate 限制"),
                true)
        }
    }

    function getConfigurationSymbols(configuration) {
        var config = configuration || ({})
        var source = config.symbols || []
        var values = Array.isArray(source) ? source : String(source || "").split(/[,;\s，；]+/)
        var normalized = []
        for (var index = 0; index < values.length; ++index) {
            var token = String(values[index] || "").trim().toUpperCase()
            if (!token || normalized.indexOf(token) !== -1) {
                continue
            }
            normalized.push(token)
        }
        return normalized
    }

    function getStrategySubscriptionSyncLabel(strategy, configuration) {
        var config = configuration || ({})
        var strategyId = strategy ? (strategy.strategyId || strategy.strategy_id || strategy.id || "") : ""
        if (!strategyId || !isStrategyBoundToTradingConfiguration(strategy, config)) {
            return "--"
        }

        var pool = getStrategySymbolPool(strategy)
        if (pool.length === 0) {
            return "策略池为空"
        }

        var configSymbols = getConfigurationSymbols(config)
        for (var index = 0; index < pool.length; ++index) {
            if (configSymbols.indexOf(pool[index]) === -1) {
                return "待同步"
            }
        }

        return "已同步"
    }

    function getStrategySubscriptionSyncAccent(strategy, configuration) {
        var label = getStrategySubscriptionSyncLabel(strategy, configuration)
        if (label === "已同步") {
            return successGreen
        }
        if (label === "待同步") {
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
        var strategyId = strategy.strategyId || strategy.strategy_id || strategy.id || ""
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
        var strategyId = strategy.strategyId || strategy.strategy_id || strategy.id || ""
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

        var symbolPoolSummary = getStrategySymbolPoolSummary(strategy, 2)
        var linkedStockPool = getLinkedStockPoolState(strategy)
        var linkedStockPoolLabel = linkedStockPool.poolName ? (" · 自选池: " + linkedStockPool.poolName) : ""
        if (baseDescription === "暂无描述") {
            return "标的池: " + symbolPoolSummary + linkedStockPoolLabel
        }
        return baseDescription + " · 标的池: " + symbolPoolSummary + linkedStockPoolLabel
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

        deleteConfirmDialog.strategyId = strategyId
        deleteConfirmDialog.strategyName = strategyName || "未命名策略"
        deleteConfirmDialog.open()
    }

    function getSelectedStrategySummary() {
        if (strategyViewModel && selectedStrategyId) {
            for (var index = 0; index < strategyViewModel.count; ++index) {
                var row = strategyViewModel.getRow(index)
                var rowId = row ? (row.strategyId || row.id || "") : ""
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
        selectedStrategyId = selectedRow ? (selectedRow.strategyId || selectedRow.id || "") : ""
        strategySelected(selectedRow ? (selectedRow.strategyName || selectedRow.name || "") : "")
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
                var rowId = row ? (row.strategyId || row.id || "") : ""
                if (rowId === selectedStrategyId) {
                    selectedStrategyIndex = index
                    return
                }
            }
        }

        if (selectedStrategyIndex >= 0 && selectedStrategyIndex < strategyViewModel.count) {
            var currentRow = strategyViewModel.getRow(selectedStrategyIndex)
            selectedStrategyId = currentRow ? (currentRow.strategyId || currentRow.id || "") : ""
            return
        }

        selectStrategyAt(0)
    }

    function getSelectedStrategyDetail(strategyId) {
        if (!strategyService || !strategyId || !strategyService.getStrategyById) {
            return ({})
        }

        return strategyService.getStrategyById(strategyId) || ({})
    }

    function resolveStrategyForEdit(strategyCandidate) {
        var candidate = strategyCandidate || ({})
        var strategyId = ""

        if (typeof candidate === "string") {
            strategyId = candidate
        } else {
            strategyId = candidate.strategy_id || candidate.strategyId || candidate.id || ""
        }

        if (strategyId && strategyService && strategyService.getStrategyById) {
            var detail = strategyService.getStrategyById(strategyId) || ({})
            if (detail && Object.keys(detail).length > 0) {
                return detail
            }
        }

        return typeof candidate === "object" ? candidate : ({})
    }

    function openStrategyCreation(strategyDetail) {
        var resolvedStrategy = resolveStrategyForEdit(strategyDetail)
        strategyCreationLoader.pendingStrategyData = resolvedStrategy || ({})
        strategyCreationLoader.active = true

        if (strategyCreationLoader.item) {
            if (resolvedStrategy && Object.keys(resolvedStrategy).length > 0 && strategyCreationLoader.item.loadStrategyForEdit) {
                strategyCreationLoader.item.loadStrategyForEdit(resolvedStrategy)
            } else if (strategyCreationLoader.item.resetForm) {
                strategyCreationLoader.item.resetForm()
            }

            if (focusSymbolPoolAfterOpen && strategyCreationLoader.item.focusSymbolPoolEditor) {
                strategyCreationLoader.item.focusSymbolPoolEditor()
                focusSymbolPoolAfterOpen = false
            }
        }
    }

    function openStrategyCreationForSymbolPool(strategyDetail) {
        focusSymbolPoolAfterOpen = true
        openStrategyCreation(strategyDetail)
    }

    function getStrategyPerformanceMetrics(strategyDetail) {
        if (!strategyDetail) {
            return ({})
        }

        return strategyDetail.performance_metrics || strategyDetail.performanceMetrics || ({})
    }

    function getLatestBacktestRecord(strategyDetail) {
        var performance = getStrategyPerformanceMetrics(strategyDetail)
        return performance.latestBacktest || performance.latest_backtest || ({})
    }

    function getBacktestHistory(strategyDetail) {
        var performance = getStrategyPerformanceMetrics(strategyDetail)
        return performance.backtestHistory || performance.backtest_history || []
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
            { label: "股票池", value: latest.universeLabel || universeContext.universeType || "--" },
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
    
    // 数据模型（完全使用数据库数据，移除模拟数据）
    ListModel {
        id: strategyModel
        // 不再使用硬编码数据，完全依赖数据库
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
        
        // 头部区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            color: secondaryBg
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: spacingXLarge
                
                // 标题
                ColumnLayout {
                    spacing: spacingMedium
                    
                    Text {
                        text: "量化策略库"
                        font.pixelSize: fontSizeXLarge
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }
                    
                    Text {
                        text: "管理您的量化交易策略，监控实时运行状态"
                        font.pixelSize: fontSizeNormal
                        color: textTertiary
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // 操作按钮组
                Row {
                    spacing: spacingLarge
                    
                    // 筛选按钮
                    StrategyComponents.StrategyFilterButton {
                        onClicked: showFilter = !showFilter
                    }
                    
                    // 排序按钮
                    StrategyComponents.StrategySortButton {
                        onClicked: showSorter = !showSorter
                    }
                    
                    // 新建策略按钮
                    StrategyComponents.CreateStrategyButton {
                        onClicked: {
                            strategyLibraryPage.openStrategyCreation({});
                        }
                    }
                }
            }
        }
        
        // 主要内容区域
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff  // 隐藏垂直滚动条
            
            ColumnLayout {
                width: scrollView.width - 10
                spacing: spacingXLarge
                
                // 列表头部信息
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    
                    Text {
                        text: "显示 " + (strategyViewModel ? strategyViewModel.count : strategyModel.count) + " 个策略 (数据库: " + (strategyViewModel ? strategyViewModel.count : 0) + ")"
                        font.pixelSize: fontSizeNormal
                        color: textSecondary
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 视图切换按钮
                    StrategyComponents.ViewModeToggle {
                        currentMode: "grid"
                        onModeChanged: {
                            // 视图模式切换逻辑
                        }
                    }
                }
                
                // 策略列表 - 卡片形式
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 420
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    radius: borderRadiusXLarge
                    color: secondaryBg
                    border.color: borderColor
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: spacingMedium
                        
                        Text {
                            text: "策略列表"
                            font.pixelSize: fontSizeLarge
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }
                        
                            // 策略卡片网格 - 使用统一的StrategyCard组件
                            GridView {
                                id: strategyGridView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                
                                // 模型绑定到StrategyViewModel
                                model: strategyViewModel
                                
                                // 2列布局，使用更大的卡片高度
                                cellWidth: (width - 30) / 2
                                cellHeight: 280  // 统一卡片高度+间距
                                
                                // 隐藏滚动条
                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AlwaysOff
                                }
                                
                                delegate: Components.StrategyCard {
                                    width: strategyGridView.cellWidth - 12
                                    height: strategyGridView.cellHeight - 20
                                    
                                    // 策略基本属性
                                    strategyId: model.strategyId || model.id || ""
                                    strategyName: model.strategyName || model.name || "未命名策略"
                                    displayName: model.strategyName || model.name || "未命名策略"
                                    strategyType: model.strategyType || "趋势策略"
                                    description: strategyLibraryPage.buildStrategyCardDescription(model)
                                    status: strategyLibraryPage.getStrategyDisplayStatus(model)
                                    tags: strategyLibraryPage.buildStrategyRuntimeTags(model)
                                    startActionAvailable: strategyLibraryPage.getStrategyStartGateState(model).canStart
                                    startActionLabel: strategyLibraryPage.getStrategyStartActionLabel(model)
                                    startActionHint: strategyLibraryPage.getStrategyStartActionHint(model)
                                    
                                    // 性能指标
                                    returns: parseFloat(model.returns) || 0.0
                                    sharpeRatio: parseFloat(model.sharpeRatio) || 0.0
                                    maxDrawdown: parseFloat(model.maxDrawdown) || 0.0
                                    winRate: parseFloat(model.winRate) || 0.0
                                    
                                    // 实时状态
                                    runningDays: model.runningDays || 0
                                    tradesCount: model.tradesCount || 0
                                    dailyPnL: parseFloat(model.dailyPnL) || 0
                                    position: parseFloat(model.position) || 0
                                    
                                    // 布局设置
                                    selected: strategyLibraryPage.selectedStrategyId !== ""
                                        ? strategyLibraryPage.selectedStrategyId === (model.strategyId || model.id || "")
                                        : strategyLibraryPage.selectedStrategyIndex === index
                                    showMiniChart: true
                                    showParameterPanel: false  // 列表视图不显示参数面板
                                    cardWidth: strategyGridView.cellWidth - 12
                                    cardHeight: 260
                                    
                                    // 确保颜色正确
                                    Component.onCompleted: {
                                        // 如果数据适配器可用，使用统一的颜色映射
                                        if (typeof StrategyAdapter !== 'undefined') {
                                            categoryColor = StrategyAdapter.getStrategyTypeColor(strategyType)
                                        }
                                    }
                                    
                                    enableCardClick: true

                                    onClicked: {
                                        strategyLibraryPage.selectStrategyAt(index)
                                    }

                                    onEntitySelected: function(entityId) {
                                        strategyLibraryPage.selectStrategyAt(index)
                                    }
                                    
                                    onStartClicked: {
                                        strategyLibraryPage.startStrategyFromCard(model)
                                    }

                                    onStartActionHintClicked: {
                                        strategyLibraryPage.handleStrategyStartActionHint(model)
                                    }

                                    onPauseClicked: {
                                        strategyLibraryPage.stopStrategyFromCard(model)
                                    }
                                    
                                    onStopClicked: {
                                        strategyLibraryPage.stopStrategyFromCard(model)
                                    }
                                    
                                    onOptimizeClicked: {
                                        console.log("优化策略:", model.strategyId || model.id)
                                        // TODO: 实现策略优化功能
                                    }

                                    onDeleteClicked: {
                                        strategyLibraryPage.requestDeleteStrategy(
                                            model.strategyId || model.id || "",
                                            model.strategyName || model.name || "未命名策略"
                                        )
                                    }
                                }
                            }
                    }
                }
                
                // 策略详细区域 - 使用统一的StrategyCard（集成策略控制功能）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1120
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    radius: borderRadiusXLarge
                    color: secondaryBg
                    border.color: borderColor
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: spacingMedium
                        
                        Text {
                            text: "策略详情与控制"
                            font.pixelSize: fontSizeLarge
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }
                        
                        // 当前选择的策略卡片
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 320
                            radius: borderRadiusMedium
                            color: Qt.rgba(59/255, 130/255, 246/255, 0.05)
                            border.color: Qt.rgba(59/255, 130/255, 246/255, 0.3)
                            border.width: 1
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8
                                
                                // 空状态
                                Text {
                                    text: "请从上方策略列表中选择一个策略"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                                    visible: selectedStrategyIndex < 0
                                }
                                
                        // 已选择策略的详细信息 - 使用统一的StrategyCard
                        Components.StrategyCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: selectedStrategyIndex >= 0 && strategyViewModel && strategyViewModel.count > selectedStrategyIndex
                            
                            property var selectedStrategy: {
                                if (selectedStrategyIndex >= 0 && strategyViewModel && strategyViewModel.count > selectedStrategyIndex) {
                                    return strategyViewModel.getRow(selectedStrategyIndex);
                                }
                                return null;
                            }
                            
                            // 策略基本属性
                            strategyId: selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id || "") : ""
                            strategyName: selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : ""
                            displayName: selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : ""
                            strategyType: selectedStrategy ? (selectedStrategy.strategyType || "趋势策略") : "趋势策略"
                            description: selectedStrategy ? strategyLibraryPage.buildStrategyCardDescription(selectedStrategy) : "暂无描述"
                            status: selectedStrategy ? strategyLibraryPage.getStrategyDisplayStatus(selectedStrategy) : "STOPPED"
                            tags: selectedStrategy ? strategyLibraryPage.buildStrategyRuntimeTags(selectedStrategy) : []
                            startActionAvailable: selectedStrategy ? strategyLibraryPage.getStrategyStartGateState(selectedStrategy).canStart : false
                            startActionLabel: selectedStrategy ? strategyLibraryPage.getStrategyStartActionLabel(selectedStrategy) : "先生成股票池"
                            startActionHint: selectedStrategy ? strategyLibraryPage.getStrategyStartActionHint(selectedStrategy) : "缺少手动池/自选池/最近回测池"
                            
                            // 性能指标
                            returns: selectedStrategy ? parseFloat(selectedStrategy.returns) || 0.0 : 0.0
                            sharpeRatio: selectedStrategy ? parseFloat(selectedStrategy.sharpeRatio) || 0.0 : 0.0
                            maxDrawdown: selectedStrategy ? parseFloat(selectedStrategy.maxDrawdown) || 0.0 : 0.0
                            winRate: selectedStrategy ? parseFloat(selectedStrategy.winRate) || 0.0 : 0.0
                            
                            // 实时状态
                            runningDays: selectedStrategy ? (selectedStrategy.runningDays || 0) : 0
                            tradesCount: selectedStrategy ? (selectedStrategy.tradesCount || 0) : 0
                            dailyPnL: selectedStrategy ? parseFloat(selectedStrategy.dailyPnL) || 0 : 0
                            position: selectedStrategy ? parseFloat(selectedStrategy.position) || 0 : 0
                            
                            // 布局设置 - 详细视图显示更多信息
                            selected: true
                            showMiniChart: true
                            showParameterPanel: true  // 详细视图显示参数面板
                            cardWidth: parent.width - 32  // 减去边距
                            cardHeight: parent.height - 32
                            
                            // 确保颜色正确
                            Component.onCompleted: {
                                // 如果数据适配器可用，使用统一的颜色映射
                                if (typeof StrategyAdapter !== 'undefined' && selectedStrategy) {
                                    var strategyType = selectedStrategy.strategyType || "趋势策略"
                                    categoryColor = StrategyAdapter.getStrategyTypeColor(strategyType)
                                }
                            }
                            
                            // 信号连接
                            onStartClicked: {
                                strategyLibraryPage.startStrategyFromCard(selectedStrategy)
                            }

                            onStartActionHintClicked: {
                                strategyLibraryPage.handleStrategyStartActionHint(selectedStrategy)
                            }

                            onPauseClicked: {
                                strategyLibraryPage.stopStrategyFromCard(selectedStrategy)
                            }
                            
                            onStopClicked: {
                                strategyLibraryPage.stopStrategyFromCard(selectedStrategy)
                            }
                            
                            onOptimizeClicked: {
                                console.log("优化策略:", selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id) : "")
                                // TODO: 实现策略优化功能
                                optimizeStrategy()
                            }

                            onDeleteClicked: {
                                strategyLibraryPage.requestDeleteStrategy(
                                    selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id || "") : "",
                                    selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : "未命名策略"
                                )
                            }
                        }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 150
                            visible: selectedStrategyIndex >= 0
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1

                            ColumnLayout {
                                id: latestBacktestSection
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10

                                property var selectedStrategySummary: strategyLibraryPage.getSelectedStrategySummary()
                                property string selectedStrategyId: selectedStrategySummary ? (selectedStrategySummary.strategyId || selectedStrategySummary.id || "") : ""
                                property var selectedStrategyDetail: strategyLibraryPage.getSelectedStrategyDetail(selectedStrategyId)
                                property var latestBacktestItems: strategyLibraryPage.buildLatestBacktestItems(selectedStrategyDetail)

                                Text {
                                    text: "最近一次回测"
                                    font.pixelSize: fontSizeNormal + 1
                                    font.weight: Font.DemiBold
                                    color: textPrimary
                                }

                                Text {
                                    visible: latestBacktestSection.latestBacktestItems.length === 0
                                    text: "当前策略还没有可展示的回测记录。"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                GridLayout {
                                    visible: latestBacktestSection.latestBacktestItems.length > 0
                                    Layout.fillWidth: true
                                    columns: 4
                                    columnSpacing: 10
                                    rowSpacing: 8

                                    Repeater {
                                        model: latestBacktestSection.latestBacktestItems

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

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 64
                            visible: selectedStrategyIndex >= 0
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1

                            RowLayout {
                                id: currentStrategyRow
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 12

                                property var selectedStrategySummary: strategyLibraryPage.getSelectedStrategySummary()
                                property string selectedStrategyId: selectedStrategySummary ? (selectedStrategySummary.strategyId || selectedStrategySummary.id || "") : ""
                                property var selectedStrategyDetail: strategyLibraryPage.getSelectedStrategyDetail(selectedStrategyId)

                                Text {
                                    text: "当前策略"
                                    font.pixelSize: fontSizeNormal
                                    font.weight: Font.DemiBold
                                    color: textPrimary
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: currentStrategyRow.selectedStrategyDetail && Object.keys(currentStrategyRow.selectedStrategyDetail).length > 0
                                        ? (currentStrategyRow.selectedStrategyDetail.strategy_name || currentStrategyRow.selectedStrategyDetail.strategyName || "未命名策略")
                                        : "未选择策略"
                                    font.pixelSize: fontSizeNormal
                                    color: textSecondary
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    Layout.preferredWidth: 96
                                    Layout.preferredHeight: 34
                                    radius: 6
                                    color: "#2563eb"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "编辑策略"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "white"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            var editStrategyId = currentStrategyRow.selectedStrategyDetail
                                                ? (currentStrategyRow.selectedStrategyDetail.strategy_id || currentStrategyRow.selectedStrategyDetail.strategyId || currentStrategyRow.selectedStrategyDetail.id || "")
                                                : ""
                                            if (editStrategyId) {
                                                strategyLibraryPage.openStrategyCreation(currentStrategyRow.selectedStrategySummary)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: runtimeDiagnosticSection.issueText.length > 0 ? 312 : 252
                            visible: selectedStrategyIndex >= 0
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1

                            ColumnLayout {
                                id: runtimeDiagnosticSection
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10

                                property var selectedStrategySummary: strategyLibraryPage.getSelectedStrategySummary()
                                property var tradingConfiguration: strategyLibraryPage.currentTradingConfiguration()
                                property var marketCalendarSnapshot: strategyLibraryPage.currentMarketCalendarSnapshot()
                                property string selectedStrategyId: selectedStrategySummary
                                    ? (selectedStrategySummary.strategyId || selectedStrategySummary.strategy_id || selectedStrategySummary.id || "")
                                    : ""
                                property var runtimeSnapshot: strategyLibraryPage.currentRuntimeSnapshot(selectedStrategySummary)
                                property bool hasRuntimeSnapshot: strategyLibraryPage.hasRuntimeSnapshotData(runtimeSnapshot)
                                property bool isBoundStrategy: selectedStrategyId !== ""
                                    && strategyLibraryPage.isStrategyBoundToTradingConfiguration(selectedStrategySummary, tradingConfiguration)
                                property string displayStatus: selectedStrategySummary
                                    ? strategyLibraryPage.getStrategyDisplayStatus(selectedStrategySummary)
                                    : "STOPPED"
                                property string issueTitle: hasRuntimeSnapshot ? "最近错误" : "日历回退原因"
                                property string issueText: hasRuntimeSnapshot
                                    ? strategyLibraryPage.normalizeRuntimeDisplayValue(runtimeSnapshot.lastError, "")
                                    : (isBoundStrategy
                                        ? strategyLibraryPage.normalizeRuntimeDisplayValue(marketCalendarSnapshot.error, "")
                                        : "")
                                property var diagnosticItems: [
                                    {
                                        label: "显示状态",
                                        value: hasRuntimeSnapshot
                                            ? strategyLibraryPage.normalizeRuntimeDisplayValue(runtimeSnapshot.stateLabel, strategyLibraryPage.getStrategyDisplayStatusLabel(displayStatus))
                                            : strategyLibraryPage.getStrategyDisplayStatusLabel(displayStatus),
                                        accent: strategyLibraryPage.getRuntimeDiagnosticColor(displayStatus)
                                    },
                                    {
                                        label: "交易绑定",
                                        value: strategyLibraryPage.describeStrategyBinding(selectedStrategySummary, tradingConfiguration),
                                        accent: isBoundStrategy ? accentBlue : textSecondary
                                    },
                                    {
                                        label: "订阅同步",
                                        value: strategyLibraryPage.getStrategySubscriptionSyncLabel(selectedStrategySummary, tradingConfiguration),
                                        accent: strategyLibraryPage.getStrategySubscriptionSyncAccent(selectedStrategySummary, tradingConfiguration)
                                    },
                                    {
                                        label: "日历来源",
                                        value: strategyLibraryPage.normalizeRuntimeDisplayValue(marketCalendarSnapshot.sourceLabel, "本地时间窗"),
                                        accent: marketCalendarSnapshot.holidayAware ? successGreen : warningAmber
                                    },
                                    {
                                        label: "日历阶段",
                                        value: strategyLibraryPage.getMarketCalendarPhaseLabel(marketCalendarSnapshot),
                                        accent: strategyLibraryPage.getMarketCalendarStatusAccent(marketCalendarSnapshot)
                                    },
                                    {
                                        label: "账户 ID",
                                        value: strategyLibraryPage.normalizeRuntimeDisplayValue(
                                            hasRuntimeSnapshot ? runtimeSnapshot.accountId : (isBoundStrategy ? tradingConfiguration.accountId : "")),
                                        accent: textPrimary
                                    },
                                    {
                                        label: "业务策略 ID",
                                        value: strategyLibraryPage.normalizeRuntimeDisplayValue(selectedStrategyId),
                                        accent: textPrimary
                                    },
                                    {
                                        label: "运行时策略 ID",
                                        value: strategyLibraryPage.normalizeRuntimeDisplayValue(
                                            hasRuntimeSnapshot ? runtimeSnapshot.runtimeStrategyId : (isBoundStrategy ? tradingConfiguration.runtimeStrategyId : "")),
                                        accent: textPrimary
                                    },
                                    {
                                        label: "会话 ID",
                                        value: strategyLibraryPage.normalizeRuntimeDisplayValue(hasRuntimeSnapshot ? runtimeSnapshot.sessionId : ""),
                                        accent: textPrimary
                                    },
                                    {
                                        label: "最近收盘交易日",
                                        value: strategyLibraryPage.normalizeRuntimeDisplayValue(marketCalendarSnapshot.latestClosedTradeDate, "--"),
                                        accent: textPrimary
                                    },
                                    {
                                        label: "连接状态",
                                        value: strategyLibraryPage.formatRuntimeBooleanValue(
                                            hasRuntimeSnapshot ? runtimeSnapshot.connected : undefined,
                                            "已连接",
                                            "未连接",
                                            "--"),
                                        accent: hasRuntimeSnapshot && runtimeSnapshot.connected ? successGreen : textSecondary
                                    },
                                    {
                                        label: "初始化",
                                        value: strategyLibraryPage.formatRuntimeBooleanValue(
                                            hasRuntimeSnapshot ? runtimeSnapshot.initialized : undefined,
                                            "已初始化",
                                            "未初始化",
                                            "--"),
                                        accent: hasRuntimeSnapshot && runtimeSnapshot.initialized ? successGreen : textSecondary
                                    }
                                ]

                                Text {
                                    text: "运行时诊断"
                                    font.pixelSize: fontSizeNormal + 1
                                    font.weight: Font.DemiBold
                                    color: textPrimary
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    color: textTertiary
                                    text: runtimeDiagnosticSection.hasRuntimeSnapshot
                                        ? "当前状态来自真实运行时会话快照，可直接用于判断策略是否已经进入交易运行态。"
                                        : (runtimeDiagnosticSection.isBoundStrategy
                                            ? "当前策略已绑定到活动交易配置，但暂未发现运行时会话，页面会优先参考交易日历，再回退到本地时间窗。"
                                            : "当前策略尚未绑定到活动交易配置，因此不会出现对应的运行时会话。")
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 4
                                    columnSpacing: 10
                                    rowSpacing: 8

                                    Repeater {
                                        model: runtimeDiagnosticSection.diagnosticItems

                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 52
                                            radius: 8
                                            color: "#0B1220"
                                            border.width: 1
                                            border.color: Qt.rgba(71/255, 85/255, 105/255, 0.35)

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 3

                                                Text {
                                                    text: modelData.label
                                                    font.pixelSize: 11
                                                    color: textTertiary
                                                }

                                                Text {
                                                    text: modelData.value
                                                    font.pixelSize: 13
                                                    font.weight: Font.Medium
                                                    color: modelData.accent || textPrimary
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: runtimeDiagnosticSection.issueText.length > 0 ? errorText.implicitHeight + 24 : 0
                                    visible: runtimeDiagnosticSection.issueText.length > 0
                                    radius: 8
                                    color: Qt.rgba(239/255, 68/255, 68/255, 0.10)
                                    border.width: 1
                                    border.color: Qt.rgba(239/255, 68/255, 68/255, 0.35)

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 4

                                        Text {
                                            text: runtimeDiagnosticSection.issueTitle
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            color: riseRed
                                        }

                                        Text {
                                            id: errorText
                                            width: parent.width
                                            text: runtimeDiagnosticSection.issueText
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 12
                                            color: textPrimary
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: selectedStrategyIndex >= 0
                            radius: borderRadiusMedium
                            color: "#111827"
                            border.color: "#1F2937"
                            border.width: 1

                            ColumnLayout {
                                id: backtestHistorySection
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10

                                property var selectedStrategySummary: strategyLibraryPage.getSelectedStrategySummary()
                                property string selectedStrategyId: selectedStrategySummary ? (selectedStrategySummary.strategyId || selectedStrategySummary.id || "") : ""
                                property var selectedStrategyDetail: strategyLibraryPage.getSelectedStrategyDetail(selectedStrategyId)
                                property var backtestHistory: strategyLibraryPage.getBacktestHistory(selectedStrategyDetail)

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: "回测历史"
                                        font.pixelSize: fontSizeNormal + 1
                                        font.weight: Font.DemiBold
                                        color: textPrimary
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: backtestHistorySection.backtestHistory.length > 0 ? ("最近 " + backtestHistorySection.backtestHistory.length + " 条") : "暂无历史"
                                        font.pixelSize: fontSizeNormal - 1
                                        color: textSecondary
                                    }
                                }

                                Text {
                                    visible: backtestHistorySection.backtestHistory.length === 0
                                    text: "这里会保留不同股票池、不同日期区间的回测摘要，便于横向比较。"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    visible: backtestHistorySection.backtestHistory.length > 0
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 360
                                    radius: 10
                                    color: "#0B1220"
                                    border.color: "#1E293B"
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 10

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Text {
                                                text: "历史回测对比图"
                                                font.pixelSize: fontSizeNormal
                                                font.weight: Font.DemiBold
                                                color: textPrimary
                                            }

                                            Item { Layout.fillWidth: true }

                                            Text {
                                                text: "横轴按回测记录时间顺序排列"
                                                font.pixelSize: 11
                                                color: textTertiary
                                            }
                                        }

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
                                                        id: historyReturnsChart
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
                                                            min: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "returns", -5, 5).min
                                                            max: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "returns", -5, 5).max
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

                                                            Component.onCompleted: strategyLibraryPage.updateHistoryMetricSeries(historyReturnsSeries, backtestHistorySection.backtestHistory, "returns")
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
                                                        id: historyDrawdownChart
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
                                                            min: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "maxDrawdown", 0, 10).min
                                                            max: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "maxDrawdown", 0, 10).max
                                                            tickCount: 5
                                                            labelsColor: textSecondary
                                                            gridLineColor: "#1E293B"
                                                            labelFormat: "%.1f"
                                                        }

                                                        LineSeries {
                                                            id: historyDrawdownSeries
                                                            axisX: historyDrawdownAxisX
                                                            axisY: historyDrawdownAxisY
                                                            color: "#F59E0B"
                                                            width: 2

                                                            Component.onCompleted: strategyLibraryPage.updateHistoryMetricSeries(historyDrawdownSeries, backtestHistorySection.backtestHistory, "maxDrawdown")
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
                                                        id: historySharpeChart
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
                                                            min: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "sharpeRatio", -1, 1).min
                                                            max: strategyLibraryPage.calculateMetricAxisBounds(backtestHistorySection.backtestHistory, "sharpeRatio", -1, 1).max
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

                                                            Component.onCompleted: strategyLibraryPage.updateHistoryMetricSeries(historySharpeSeries, backtestHistorySection.backtestHistory, "sharpeRatio")
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                ScrollView {
                                    visible: backtestHistorySection.backtestHistory.length > 0
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                                    Column {
                                        width: parent.width
                                        spacing: 10

                                        Repeater {
                                            model: backtestHistorySection.backtestHistory

                                            delegate: Rectangle {
                                                width: parent.width
                                                radius: 10
                                                color: "#0B1220"
                                                border.color: "#1E293B"
                                                border.width: 1
                                                implicitHeight: historyContent.implicitHeight + 24

                                                ColumnLayout {
                                                    id: historyContent
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.margins: 12
                                                    spacing: 8

                                                    property var summary: modelData.summary || ({})

                                                    RowLayout {
                                                        Layout.fillWidth: true

                                                        Text {
                                                            text: (modelData.recordedAt || "--") + "  ·  " + (modelData.universeLabel || modelData.universeType || "未知股票池")
                                                            font.pixelSize: 13
                                                            font.weight: Font.Medium
                                                            color: textPrimary
                                                        }

                                                        Item { Layout.fillWidth: true }

                                                        Text {
                                                            text: strategyLibraryPage.formatBacktestPercentValue(historyContent.summary.returns, 2)
                                                            font.pixelSize: 13
                                                            font.weight: Font.DemiBold
                                                            color: Number(historyContent.summary.returns) >= 0 ? riseRed : fallGreen
                                                        }
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: (modelData.startDate || "--") + " ~ " + (modelData.endDate || "--")
                                                            + "    数据源: " + (modelData.dataSourceMode || "--")
                                                            + (modelData.indexLabel || modelData.indexSymbol ? ("    指数: " + (modelData.indexLabel || modelData.indexSymbol)) : "")
                                                        font.pixelSize: 12
                                                        color: textSecondary
                                                        wrapMode: Text.WordWrap
                                                    }

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 14

                                                        Text {
                                                            text: "最大回撤: " + strategyLibraryPage.formatBacktestPercentValue(historyContent.summary.maxDrawdown, 2)
                                                            font.pixelSize: 12
                                                            color: textSecondary
                                                        }

                                                        Text {
                                                            text: "夏普: " + strategyLibraryPage.formatBacktestNumberValue(historyContent.summary.sharpeRatio, 2)
                                                            font.pixelSize: 12
                                                            color: textSecondary
                                                        }

                                                        Text {
                                                            text: "胜率: " + strategyLibraryPage.formatBacktestPercentValue(historyContent.summary.winRate, 2)
                                                            font.pixelSize: 12
                                                            color: textSecondary
                                                        }

                                                        Text {
                                                            text: "交易: " + strategyLibraryPage.formatBacktestIntegerValue(historyContent.summary.tradesCount)
                                                            font.pixelSize: 12
                                                            color: textSecondary
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                onBacktestHistoryChanged: {
                                    strategyLibraryPage.updateHistoryMetricSeries(historyReturnsSeries, backtestHistorySection.backtestHistory, "returns")
                                    strategyLibraryPage.updateHistoryMetricSeries(historyDrawdownSeries, backtestHistorySection.backtestHistory, "maxDrawdown")
                                    strategyLibraryPage.updateHistoryMetricSeries(historySharpeSeries, backtestHistorySection.backtestHistory, "sharpeRatio")
                                }
                            }
                        }
                        
                        // 提示信息
                        Text {
                            text: "提示：统一量化卡片组件已集成到策略库页面"
                            font.pixelSize: fontSizeNormal - 1
                            color: textTertiary
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                
                // 策略图表
                StrategyComponents.StrategyChart {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    Layout.bottomMargin: spacingXLarge
                }
            }
        }
    }
    
    // 新建策略对话框
    StrategyComponents.CreateStrategyDialog {
        id: createDialog
        anchors.centerIn: parent
        visible: isOpen
        
        onStrategyCreated: function(strategyData) {
            console.log("创建策略:", strategyData);
            // 添加到策略模型
            strategyModel.append({
                name: strategyData.name,
                description: strategyData.description,
                status: strategyData.status,
                returns: strategyData.returns,
                maxDrawdown: strategyData.maxDrawdown,
                sharpeRatio: strategyData.sharpeRatio,
                winRate: strategyData.winRate,
                tags: strategyData.tags,
                runningDays: 0,
                tradesCount: 0,
                position: 0,
                dailyPnL: 0
            });
        }
        
        onClosed: {
            // 关闭对话框
        }
    }
    
    // 筛选弹窗
    StrategyComponents.StrategyFilter {
        id: filterComponent
        anchors.centerIn: parent
        visible: showFilter
        
        onFilterApplied: function(filterData) {
            console.log("应用筛选:", filterData);
            showFilter = false;
        }
        
        onFilterReset: function() {
            console.log("重置筛选");
        }
        
        onFilterClosed: function() {
            showFilter = false;
        }
    }
    
    // 排序弹窗
    StrategyComponents.StrategySorter {
        id: sorterComponent
        anchors.centerIn: parent
        visible: showSorter
        
        onSortApplied: function(sortType) {
            console.log("应用排序:", sortType);
            showSorter = false;
        }
        
        onSortClosed: function() {
            showSorter = false;
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
                text: actionFeedbackError ? "策略操作失败" : "策略操作结果"
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

    Dialog {
        id: deleteConfirmDialog
        anchors.centerIn: parent
        modal: true
        width: 420
        property string strategyId: ""
        property string strategyName: ""

        background: Rectangle {
            radius: borderRadiusMedium
            color: secondaryBg
            border.color: borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: spacingLarge

            Text {
                text: "删除策略"
                font.pixelSize: fontSizeLarge
                font.weight: Font.DemiBold
                color: textPrimary
            }

            Text {
                text: "确认删除策略“" + deleteConfirmDialog.strategyName + "”？此操作不可撤销。"
                color: textSecondary
                font.pixelSize: fontSizeNormal
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    onClicked: deleteConfirmDialog.close()
                }

                Button {
                    text: "确认删除"
                    enabled: !deleteInProgress
                    onClicked: {
                        if (deleteInProgress) {
                            return
                        }

                        deleteInProgress = true
                        if (strategyService && deleteConfirmDialog.strategyId) {
                            var deletedStrategyId = deleteConfirmDialog.strategyId
                            var ok = strategyService.deleteStrategy(deletedStrategyId)
                            if (ok) {
                                if (selectedStrategyIndex >= 0 && strategyViewModel && selectedStrategyIndex >= strategyViewModel.count - 1) {
                                    selectedStrategyIndex = Math.max(0, strategyViewModel.count - 2)
                                }
                                console.log("策略删除成功:", deletedStrategyId)
                            } else {
                                console.error("策略删除失败:", deletedStrategyId)
                            }
                        }
                        deleteInProgress = false
                        deleteConfirmDialog.close()
                    }
                }
            }
        }
    }
    
    // 遮罩层
    Rectangle {
        anchors.fill: parent
        color: "#00000060"
        visible: showFilter || showSorter || createDialog.isOpen || deleteConfirmDialog.visible || actionFeedbackDialog.visible
        
        MouseArea {
            anchors.fill: parent
            onClicked: {
                showFilter = false;
                showSorter = false;
                createDialog.closeDialog();
                if (actionFeedbackDialog.visible) {
                    actionFeedbackDialog.close()
                }
                if (deleteConfirmDialog.visible) {
                    deleteConfirmDialog.close()
                }
            }
        }
    }
    
    // 新建策略页面加载器 - 使用专业版
    Loader {
        id: strategyCreationLoader
        anchors.fill: parent
        active: false
        source: "StrategyCreationPagePro.qml"
        property var pendingStrategyData: ({})
        
        onLoaded: {
            if (item) {
                if (pendingStrategyData && Object.keys(pendingStrategyData).length > 0 && typeof item.loadStrategyForEdit !== "undefined") {
                    item.loadStrategyForEdit(pendingStrategyData)
                } else if (typeof item.resetForm !== "undefined") {
                    item.resetForm()
                }

                if (focusSymbolPoolAfterOpen && typeof item.focusSymbolPoolEditor !== "undefined") {
                    item.focusSymbolPoolEditor()
                    focusSymbolPoolAfterOpen = false
                }

                // 连接返回信号
                if (typeof item.backClicked !== "undefined") {
                    item.backClicked.connect(function() {
                        console.log("收到创建页面返回信号，关闭创建页面")
                        strategyCreationLoader.pendingStrategyData = ({})
                        focusSymbolPoolAfterOpen = false
                        strategyCreationLoader.active = false;
                        // 确保返回到策略库页面
                        strategyLibraryPage.forceActiveFocus();
                        // 注意：不需要手动调用syncWithDatabase，因为StrategyService.createStrategy()
                        // 已经发送了dataChanged信号，这个信号会被我们的监听器处理
                        console.log("创建页面已关闭，数据更新将由dataChanged信号处理")
                    });
                }
                
                // 连接策略创建信号（兼容旧版本）
                if (typeof item.strategyCreated !== "undefined") {
                    item.strategyCreated.connect(function(strategyData) {
                        console.log("创建策略:", strategyData);
                        // 添加到策略模型
                        strategyModel.append({
                            name: strategyData.name,
                            description: strategyData.description,
                            status: strategyData.status,
                            returns: strategyData.returns,
                            maxDrawdown: strategyData.maxDrawdown,
                            sharpeRatio: strategyData.sharpeRatio,
                            winRate: strategyData.winRate,
                            tags: strategyData.tags,
                            runningDays: 0,
                            tradesCount: 0,
                            position: 0,
                            dailyPnL: 0
                        });
                        
                        // 关闭创建页面
                        strategyCreationLoader.pendingStrategyData = ({})
                        strategyCreationLoader.active = false;
                    });
                }
                
                // 连接回测请求信号
                if (typeof item.requestBacktest !== "undefined") {
                    item.requestBacktest.connect(function(strategyId, strategyName, backtestConfig) {
                        console.log("接收到回测请求，策略ID:", strategyId, "策略名称:", strategyName);
                        // 关闭创建页面
                        strategyCreationLoader.pendingStrategyData = ({})
                        strategyCreationLoader.active = false;
                        // 通知主窗口切换到回测页面
                        if (typeof window !== "undefined" && window.handleStrategyBacktestRequest) {
                            window.handleStrategyBacktestRequest(strategyId, strategyName, backtestConfig);
                        }
                    });
                }
                
                // 专业版使用resetForm完成后的返回
                if (typeof item.resetForm !== "undefined") {
                    // 监听resetForm完成事件（如果有）
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
    
    function optimizeStrategy() {
        console.log("优化策略");
    }
    
    // 初始化
    Component.onCompleted: {
        console.log("策略库页面初始化完成")
        // 初始化策略视图模型，连接到数据库
        initializeStrategyViewModel()
        if (tradingConnectionConfigService && tradingConnectionConfigService.initialize) {
            tradingConnectionConfigService.initialize()
        }
        if (tradingMarketCalendarService && tradingMarketCalendarService.initialize) {
            tradingMarketCalendarService.initialize()
        }
        if (tradingRuntimeStatusService && tradingRuntimeStatusService.initialize) {
            tradingRuntimeStatusService.initialize()
        }
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
        // 策略数据将通过dataChanged信号自动更新
    }
}
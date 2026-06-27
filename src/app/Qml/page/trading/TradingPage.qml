import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0
import AStock.Bridge 1.0 as Bridge
import "../../utils/OrderUtils.js" as OrderUtils
import "../../components/Trading" as TradingComponents
import "../../utils/TradingPageAdapter.js" as TradeJs

Item {
    id: root

    property var marketData: []
    property var marketSnapshot: ({
        price: 0,
        priceStr: "--",
        changePercent: "--",
        isUp: true,
        preClose: 0,
        upperLimit: 0,
        lowerLimit: 0,
        live: false,
        snapshotOnly: false,
        source: "",
        futuresPrice: 0,
        futuresPriceStr: "--",
        symbol: "",
        name: "",
        updatedAt: ""
    })
    property var stockDepthSnapshot: ({ bids: [], asks: [], totalBid: 0, totalAsk: 0 })
    property var depthSnapshot: ({ bids: [], asks: [], totalBid: 0, totalAsk: 0 })
    property var tickRows: []
    property var pendingOrders: []
    property string toastMessage: ""
    property bool toastError: false
    property var orderStatusDigestById: ({})
    property bool orderStatusDigestReady: false
    property string activeMode: "stock"
    property string activeSymbol: ""
    property int requestedDepthLevels: 5
    property bool formPanelRequested: false
    property bool depthPanelRequested: false
    property bool deferredPageReady: false
    property bool serviceBindingsActive: false
    property bool holdingsSectionRequested: false
    property bool strategyStatusSectionRequested: false
    property bool marketBootstrapCompleted: false
    property bool runtimeBootstrapCompleted: false
    property bool holdingsBootstrapCompleted: false
    readonly property real pageContentMaxWidth: 1360
    readonly property real tradingSectionMaxWidth: 1180
    readonly property var marketDataService: Bridge.MarketDataBridge
    readonly property var positionAccountService: Bridge.PositionAccountBridge
    readonly property var tradeExecutionService: Bridge.TradeExecutionBridge
    readonly property var tradingConnectionConfigService: Bridge.TradingConnectionConfigService
    readonly property var tradingRuntimeStatusService: Bridge.TradingRuntimeStatusService
    readonly property var strategyService: null
    readonly property var uiLifecycleCoordinator: Bridge.UiLifecycleCoordinator
    readonly property var tradingConfiguration: tradingConnectionConfigService ? (tradingConnectionConfigService.currentConfiguration || ({})) : ({})
    readonly property string boundStrategyId: String(tradingConfiguration.boundStrategyId || "").trim()
    readonly property string boundStrategyName: String(tradingConfiguration.boundStrategyName || boundStrategyId || "").trim()
    readonly property string boundRuntimeStrategyId: String(tradingConfiguration.runtimeStrategyId || "").trim()

    readonly property bool marketBridgeReady: marketDataService && marketDataService.initialized
    readonly property bool liveServicesReady: marketBridgeReady && positionAccountService && tradeExecutionService
        && positionAccountService.initialized && tradeExecutionService.initialized
    readonly property bool stockModeUsesLiveBridge: activeMode === "stock" || activeMode === "margin_buy" || activeMode === "margin_sell"
    readonly property bool quoteBridgeMode: activeMode === "stock" || activeMode === "margin_buy" || activeMode === "margin_sell"
        || activeMode === "futures" || activeMode === "options"
    readonly property bool usingLiveMarketData: quoteBridgeMode && !!(marketSnapshot && marketSnapshot.live)
    readonly property bool usingCachedSnapshot: quoteBridgeMode && !usingLiveMarketData && !!(marketSnapshot && marketSnapshot.snapshotOnly)
    readonly property string marketDisplayState: usingLiveMarketData ? "live" : (usingCachedSnapshot ? "cached" : "empty")
    readonly property var accountSnapshot: positionAccountService ? (positionAccountService.accountSnapshot || ({})) : ({})
    readonly property var rawPositions: positionAccountService ? (positionAccountService.positions || []) : []
    readonly property var recentRuleHits: tradeExecutionService ? (tradeExecutionService.recentRuleHits || []) : []
    readonly property real resolvedTotalAsset: accountSnapshot && accountSnapshot.totalAsset !== undefined
        ? Number(accountSnapshot.totalAsset)
        : (resolvedAvailableCapital + resolvedPositionMarketValue)
    readonly property real resolvedPositionMarketValue: holdingsSectionRequested
        ? calculatePositionMarketValue(rawPositions)
        : 0
    readonly property var displayPositions: holdingsSectionRequested
        ? mapDisplayPositions(rawPositions, resolvedPositionMarketValue)
        : []
    readonly property var groupedDisplayPositions: holdingsSectionRequested
        ? buildGroupedDisplayPositions(displayPositions)
        : []
    readonly property var currentCloseablePositionInfo: describeCurrentCloseablePosition(activeMode, activeSymbol)
    readonly property real holdingsPanelContentHeight: holdingsSectionRequested
        ? calculateHoldingsPanelHeight(groupedDisplayPositions)
        : 244
    readonly property real holdingsPanelMaxHeight: Math.max(244, Math.min(420, root.height * 0.42))
    readonly property real holdingsPanelPreferredHeight: holdingsSectionRequested
        ? Math.min(holdingsPanelContentHeight, holdingsPanelMaxHeight)
        : 244
    readonly property bool holdingsPanelScrollable: holdingsSectionRequested
        && holdingsPanelContentHeight > holdingsPanelPreferredHeight + 1
    readonly property real resolvedAvailableCapital: positionAccountService && positionAccountService.accountSnapshot && positionAccountService.accountSnapshot.availableCash !== undefined
        ? Number(positionAccountService.accountSnapshot.availableCash)
        : 800000
    property var executionLogs: []
    property var executionLogKeyByBrokerId: ({})
    property string lastSubmittedRequestId: ""
    property var strategyRuntimeSnapshot: ({})
    property string runtimeSnapshotDigest: ""
    property string lastWatchTraceKey: ""
    property double lastWatchTraceAt: 0
    property int suppressedWatchTraceCount: 0
    property bool marketStateSyncQueued: false
    property bool snapshotRefreshQueued: false
    property string lastSnapshotRefreshReason: ""
    property double lastSnapshotRefreshAt: 0
    property int bridgeStatusRevision: 0
    property var latestRuntimeRuleEvaluation: ({})

    function cloneList(list) {
        return list ? list.slice(0) : []
    }

    function executionLogTimeText() {
        return Qt.formatDateTime(new Date(), "hh:mm:ss")
    }

    function pushExecutionLog(kind, title, detail, severity, orderKey) {
        // ── 若传入 orderKey → 在同一行上更新字段, 不新增条目 ──
        var effectiveKey = String(orderKey || "").trim()
        if (effectiveKey.length > 0) {
            var existingLogs = root.executionLogs ? root.executionLogs.slice(0) : []
            var i
            for (i = 0; i < existingLogs.length; ++i) {
                if (String(existingLogs[i].orderKey || "") === effectiveKey) {
                    existingLogs[i].kind     = String(kind || existingLogs[i].kind)
                    existingLogs[i].title    = String(title || existingLogs[i].title)
                    existingLogs[i].detail   = String(detail || "").trim()
                    existingLogs[i].severity = String(severity || existingLogs[i].severity)
                    existingLogs[i].time     = root.executionLogTimeText()
                    root.executionLogs       = existingLogs
                    return
                }
            }
        }
        // ── 无匹配 key → 新增条目 ──
        var nextLogs = root.executionLogs ? root.executionLogs.slice(0) : []
        nextLogs.unshift({
            id: String(Date.now()) + "|" + String(Math.random()),
            orderKey: effectiveKey,
            time: root.executionLogTimeText(),
            kind: String(kind || "info"),
            title: String(title || "状态更新"),
            detail: String(detail || "").trim(),
            severity: String(severity || "info")
        })
        while (nextLogs.length > 40) {
            nextLogs.pop()
        }
        root.executionLogs = nextLogs
    }

    function clearExecutionLogs() {
        root.executionLogs = []
    }

    function activateInteractivePanels() {
        if (root.formPanelRequested && root.depthPanelRequested) {
            return
        }
        root.formPanelRequested = true
        root.depthPanelRequested = true
    }

    function activateServiceBindings() {
        if (root.serviceBindingsActive) {
            return
        }
        root.serviceBindingsActive = true
    }

    function deactivateServiceBindings() {
        root.marketStateSyncQueued = false
        root.snapshotRefreshQueued = false
        root.serviceBindingsActive = false
        TradeJs.clearCallbacks()
    }

    function reactivateVisiblePage() {
        if (!root.visible) {
            return
        }

        root.activateServiceBindings()
        bindCallbacks()

        if (!root.deferredPageReady) {
            Qt.callLater(root.performDeferredPageInitialization)
            return
        }

        ensureLiveWatch("page_reactivated")
        root.syncLiveState()
        root.refreshRuntimeSnapshot(false)
        if (root.holdingsSectionRequested) {
            root.scheduleInitialSnapshotRefresh("page_reactivated", false)
        }
    }

    function snapshotLoaded() {
        if (!positionAccountService || typeof positionAccountService.initialSnapshotLoaded !== "function") {
            return false
        }
        return !!positionAccountService.initialSnapshotLoaded()
    }

    function scheduleInitialSnapshotRefresh(reason, force) {
        if (!positionAccountService || typeof positionAccountService.requestInitialSnapshot !== "function") {
            return false
        }
        if (!force && (!root.visible || !root.serviceBindingsActive)) {
            return false
        }
        if (!force && root.snapshotLoaded()) {
            return false
        }
        if (root.snapshotRefreshQueued) {
            return false
        }

        var resolvedReason = String(reason || "snapshot_refresh")
        var now = Date.now()
        if (!force
                && root.lastSnapshotRefreshReason === resolvedReason
                && now - root.lastSnapshotRefreshAt < 1000) {
            return false
        }

        root.snapshotRefreshQueued = true
        Qt.callLater(function() {
            root.snapshotRefreshQueued = false
            if (!positionAccountService || typeof positionAccountService.requestInitialSnapshot !== "function") {
                return
            }
            if (!force && (!root.visible || !root.serviceBindingsActive || root.snapshotLoaded())) {
                return
            }

            if (positionAccountService && !positionAccountService.initialized
                    && typeof positionAccountService.initialize === "function") {
                positionAccountService.initialize()
            }

            root.lastSnapshotRefreshReason = resolvedReason
            root.lastSnapshotRefreshAt = Date.now()
            positionAccountService.requestInitialSnapshot()
        })
        return true
    }

    function traceActiveSelection(reason) {
        var resolvedReason = String(reason || "selection_changed")
        var watchSymbol = serviceSymbolForMode(root.activeMode, root.activeSymbol)
        console.log("TradingPage active selection",
                    "reason=" + resolvedReason,
                    "mode=" + String(root.activeMode || ""),
                    "activeSymbol=" + String(root.activeSymbol || ""),
                    "watchSymbol=" + String(watchSymbol || ""),
                    "visible=" + String(root.visible))
    }

    function traceWatchRequest(reason, watchSymbol) {
        var resolvedReason = String(reason || "watch")
        var normalizedWatchSymbol = String(watchSymbol || "")
        var now = Date.now()
        var traceKey = [resolvedReason, root.activeMode, root.activeSymbol, normalizedWatchSymbol].join("|")

        if (traceKey === root.lastWatchTraceKey && now - root.lastWatchTraceAt < 1200) {
            root.suppressedWatchTraceCount += 1
            return
        }

        if (root.suppressedWatchTraceCount > 0 && root.lastWatchTraceKey.length > 0) {
            console.log("TradingPage watch trace suppressed",
                        "count=" + root.suppressedWatchTraceCount,
                        "lastKey=" + root.lastWatchTraceKey)
            root.suppressedWatchTraceCount = 0
        }

        root.lastWatchTraceKey = traceKey
        root.lastWatchTraceAt = now

        console.log("TradingPage ensureLiveWatch",
                    "reason=" + resolvedReason,
                    "mode=" + String(root.activeMode || ""),
                    "activeSymbol=" + String(root.activeSymbol || ""),
                    "watchSymbol=" + normalizedWatchSymbol,
                    "marketBridgeReady=" + String(root.marketBridgeReady),
                    "visible=" + String(root.visible))
    }

    function hasRuntimeSnapshot(snapshot) {
        var data = snapshot || ({})
        return String(data.sessionId || "").trim().length > 0
            || String(data.state || "").trim().length > 0
            || String(data.strategyId || "").trim().length > 0
    }

    function runtimeSnapshotKey(snapshot) {
        var data = snapshot || ({})
        var subscriptions = data.subscriptions || []
        return [
            String(data.sessionId || ""),
            String(data.state || ""),
            String(data.connected || false),
            String(data.initialized || false),
            String(data.lastError || ""),
            String(subscriptions.length || 0)
        ].join("|")
    }

    function resolveInstrumentLabel(symbol) {
        var normalizedSymbol = String(symbol || "").trim().toUpperCase()
        if (normalizedSymbol.length === 0) {
            return "--"
        }
        if (marketDataService && marketDataService.resolveInstrument) {
            var instrument = marketDataService.resolveInstrument(normalizedSymbol) || ({})
            var instrumentName = String(instrument.name || "").trim()
            if (instrumentName.length > 0 && instrumentName !== normalizedSymbol) {
                return instrumentName + " · " + normalizedSymbol
            }
        }
        return normalizedSymbol
    }

    function matchesBoundStrategyPayload(payload) {
        var data = payload || ({})
        var payloadStrategyId = String(data.strategyId || "").trim()
        var payloadRuntimeStrategyId = String(data.runtimeStrategyId || "").trim()
        if (root.boundStrategyId.length === 0 && root.boundRuntimeStrategyId.length === 0) {
            return true
        }
        if (root.boundStrategyId.length > 0 && payloadStrategyId === root.boundStrategyId) {
            return true
        }
        if (root.boundRuntimeStrategyId.length > 0 && payloadRuntimeStrategyId === root.boundRuntimeStrategyId) {
            return true
        }
        return false
    }

    function logQuantityText(payload) {
        var data = payload || ({})
        var rawQuantity = data.fillQuantity !== undefined ? data.fillQuantity : data.quantity
        var quantity = Number(rawQuantity)
        if (isNaN(quantity) || quantity <= 0) {
            return ""
        }
        return quantity + root.orderUnit({ type: resolveLiveOrderType(data), action: data.action })
    }

    function formatDisplayPrice(value, digits) {
        var numericValue = Number(value)
        var resolvedDigits = digits === undefined ? 2 : Math.max(0, Number(digits))
        if (isNaN(numericValue) || numericValue <= 0) {
            return "--"
        }
        return numericValue.toFixed(resolvedDigits)
    }

    function logPriceText(payload) {
        var data = payload || ({})
        var priceValue = Number(data.fillPrice !== undefined ? data.fillPrice : data.price)
        if (isNaN(priceValue) || priceValue <= 0) {
            return ""
        }
        var digits = resolveLiveOrderType(data) === "options" ? 4 : (resolveLiveOrderType(data) === "futures" ? 0 : 2)
        return "@ " + formatDisplayPrice(priceValue, digits)
    }

    function logRequestDetails(payload) {
        var data = payload || ({})
        var parts = [resolveInstrumentLabel(data.symbol)]
        var quantityText = logQuantityText(data)
        var priceText = logPriceText(data)
        if (quantityText.length > 0) {
            parts.push(quantityText)
        }
        if (priceText.length > 0) {
            parts.push(priceText)
        }
        var strategyText = String(data.strategyName || root.boundStrategyName || "").trim()
        if (strategyText.length > 0) {
            parts.push("策略 " + strategyText)
        }
        return parts.join(" · ")
    }

    function appendOrderRequestLog(orderRequest) {
        if (!matchesBoundStrategyPayload(orderRequest)) {
            return
        }
        var key = String(orderRequest.clientOrderId || root.lastSubmittedRequestId || "")
        pushExecutionLog(
            "request",
            resolveLiveOrderAction(orderRequest) + " 委托已提交",
            logRequestDetails(orderRequest),
            "info",
            key)
    }

    function appendOrderStatusLog(orderStatus) {
        if (!matchesBoundStrategyPayload(orderStatus)) {
            return
        }
        var statusText = translateOrderStatus(orderStatus.status || orderStatus.rawStatus)
        var detailParts = [logRequestDetails(orderStatus), statusText]
        var ruleId = String(orderStatus.ruleId || "").trim()
        var reasonCode = String(orderStatus.reasonCode || "").trim()
        var requiredBatchId = String(orderStatus.requiredBatchId || orderStatus.batchId || "").trim()
        var blockingBatchId = String(orderStatus.blockingBatchId || "").trim()
        var messageText = String(orderStatus.message || "").trim()
        if (ruleId.length > 0) {
            detailParts.push("规则 " + ruleId)
        }
        if (reasonCode.length > 0) {
            detailParts.push("原因码 " + reasonCode)
        }
        if (requiredBatchId.length > 0) {
            detailParts.push("目标批次 " + requiredBatchId)
        }
        if (blockingBatchId.length > 0) {
            detailParts.push("阻断批次 " + blockingBatchId)
        }
        if (messageText.length > 0) {
            detailParts.push(messageText)
        }
        var rawStatus = String(orderStatus.status || orderStatus.rawStatus || "").toUpperCase()
        var isExecutionRuleReject = String(orderStatus.statusOrigin || "").trim().toLowerCase() === "execution_rule_reject"
        var severity = rawStatus === "REJECTED"
            ? (isExecutionRuleReject ? "warning" : "error")
            : "info"
        // ── 同一条订单的状态变更在原记录上更新 ──
        var orderKey = String(orderStatus.brokerOrderId || orderStatus.clientOrderId || root.lastSubmittedRequestId || "").trim()
        pushExecutionLog(
            isExecutionRuleReject ? "rule" : "status",
            isExecutionRuleReject ? "执行规则阻断" : "委托状态更新",
            detailParts.join(" · "),
            severity,
            orderKey)
    }

    function appendTradeFillLog(tradeFill) {
        if (!matchesBoundStrategyPayload(tradeFill)) {
            return
        }
        pushExecutionLog(
            "fill",
            "成交回报",
            logRequestDetails(tradeFill),
            "success")
    }

    function currentRuntimeSnapshotForBinding() {
        if (!tradingRuntimeStatusService) {
            return ({})
        }
        var snapshot = ({})
        if (root.boundRuntimeStrategyId.length > 0) {
            snapshot = tradingRuntimeStatusService.sessionSnapshotForStrategy(root.boundRuntimeStrategyId) || ({})
            if (hasRuntimeSnapshot(snapshot)) {
                return snapshot
            }
        }
        if (root.boundStrategyId.length > 0) {
            snapshot = tradingRuntimeStatusService.sessionSnapshotForStrategy(root.boundStrategyId) || ({})
            if (hasRuntimeSnapshot(snapshot)) {
                return snapshot
            }
        }
        var accountId = String(tradingConfiguration.accountId || "").trim()
        if (accountId.length > 0) {
            snapshot = tradingRuntimeStatusService.sessionSnapshotForAccount(accountId) || ({})
            if (hasRuntimeSnapshot(snapshot)) {
                return snapshot
            }
        }
        return ({})
    }

    function refreshRuntimeSnapshot(logChange) {
        var snapshot = currentRuntimeSnapshotForBinding()
        var nextDigest = runtimeSnapshotKey(snapshot)
        if (logChange && root.runtimeSnapshotDigest.length > 0 && root.runtimeSnapshotDigest !== nextDigest) {
            if (hasRuntimeSnapshot(snapshot)) {
                var detailParts = [String(snapshot.stateLabel || snapshot.state || "未知")]
                detailParts.push(snapshot.connected ? "已连接" : "未连接")
                detailParts.push("订阅 " + String((snapshot.subscriptions || []).length) + " 个")
                var lastError = String(snapshot.lastError || "").trim()
                if (lastError.length > 0) {
                    detailParts.push(lastError)
                }
                pushExecutionLog("runtime", "策略运行状态更新", detailParts.join(" · "), snapshot.hasError ? "error" : "info")
            } else if (root.boundStrategyId.length > 0) {
                pushExecutionLog("runtime", "策略运行状态更新", "当前未检测到活动会话", "warning")
            }
        }
        root.strategyRuntimeSnapshot = snapshot
        root.runtimeSnapshotDigest = nextDigest
    }

    function marketStateLabel() {
        if (usingLiveMarketData) {
            return "实时行情"
        }
        if (usingCachedSnapshot) {
            return "缓存快照"
        }
        return "等待行情"
    }

    function bridgeStateLabel() {
        var revision = root.bridgeStatusRevision
        if (!tradeExecutionService) {
            return "未初始化"
        }
        return tradeExecutionService.isLiveBridgeReady() ? "可执行" : "待连接"
    }

    function ruleHitToneColor(ruleHit) {
        var stageCode = String(ruleHit && ruleHit.stageCode ? ruleHit.stageCode : "").trim()
        if (stageCode === "ExecutionScheduling") {
            return "#f59e0b"
        }
        if (stageCode === "PreTradeRisk") {
            return "#fb7185"
        }
        if (stageCode === "BrokerSubmission") {
            return "#38bdf8"
        }
        return "#94a3b8"
    }

    function ruleHitBadgeText(ruleHit) {
        var stageLabel = String(ruleHit && ruleHit.stageLabel ? ruleHit.stageLabel : "规则命中").trim()
        if (stageLabel.indexOf("执行") === 0) {
            return "执行"
        }
        if (stageLabel.indexOf("预交易") === 0) {
            return "风控"
        }
        if (stageLabel.indexOf("券商") === 0) {
            return "券商"
        }
        return "规则"
    }

    function ruleHitGroupText(ruleHit) {
        var payload = ruleHit || ({})
        var title = String(payload.templateRuleGroupTitle || payload.groupTitle || payload.group_title || payload.templateRuleGroupId || payload.groupId || payload.group_id || "").trim()
        var role = String(payload.templateRuleGroupRole || payload.groupRole || payload.group_role || "").trim()
        if (title.length > 0 && role.length > 0) {
            return title + " / " + role
        }
        if (title.length > 0) {
            return title
        }
        return role.length > 0 ? role : ""
    }

    function ruleHitGroupLogicText(ruleHit) {
        var payload = ruleHit || ({})
        var operator = String(payload.templateRuleGroupOperator || payload.groupOperator || payload.group_operator || "").trim().toLowerCase()
        if (operator === "all") {
            return "组内全部满足"
        }
        if (operator === "any") {
            return "组内任一满足"
        }
        return operator
    }

    function ruleHitTitle(ruleHit) {
        var ruleId = String(ruleHit && ruleHit.ruleId ? ruleHit.ruleId : "").trim()
        var reasonCode = String(ruleHit && ruleHit.reasonCode ? ruleHit.reasonCode : "").trim()
        if (ruleId.length > 0 && reasonCode.length > 0) {
            return ruleId + " · " + reasonCode
        }
        return ruleId.length > 0 ? ruleId : (reasonCode.length > 0 ? reasonCode : "规则命中")
    }

    function ruleHitHeadline(ruleHit) {
        var groupText = root.ruleHitGroupText(ruleHit)
        var title = root.ruleHitTitle(ruleHit)
        if (groupText.length > 0 && title !== "规则命中") {
            return groupText + " · " + title
        }
        return groupText.length > 0 ? groupText : title
    }

    function ruleHitDetail(ruleHit) {
        var parts = []
        var symbol = String(ruleHit && ruleHit.symbol ? ruleHit.symbol : "").trim()
        var action = String(ruleHit && ruleHit.action ? ruleHit.action : "").trim()
        var requiredBatchId = String(ruleHit && (ruleHit.requiredBatchId || ruleHit.batchId) ? (ruleHit.requiredBatchId || ruleHit.batchId) : "").trim()
        var blockingBatchId = String(ruleHit && ruleHit.blockingBatchId ? ruleHit.blockingBatchId : "").trim()
        var observedAt = String(ruleHit && ruleHit.observedAt ? ruleHit.observedAt : "").trim()
        var groupLogic = root.ruleHitGroupLogicText(ruleHit)

        if (symbol.length > 0) {
            parts.push(symbol)
        }
        if (action.length > 0) {
            parts.push(action)
        }
        if (groupLogic.length > 0) {
            parts.push(groupLogic)
        }
        if (requiredBatchId.length > 0) {
            parts.push("目标批次 " + requiredBatchId)
        }
        if (blockingBatchId.length > 0) {
            parts.push("阻断批次 " + blockingBatchId)
        }
        if (observedAt.length > 0) {
            parts.push(observedAt)
        }
        return parts.join(" · ")
    }

    function tradingStatusCards() {
        var revision = root.bridgeStatusRevision
        var runtimeSnapshot = root.strategyRuntimeSnapshot || ({})
        var runtimeValue = root.boundStrategyId.length === 0
            ? "未绑定"
            : (hasRuntimeSnapshot(runtimeSnapshot) ? String(runtimeSnapshot.stateLabel || runtimeSnapshot.state || "未知") : "未启动")
        var runtimeDetail = hasRuntimeSnapshot(runtimeSnapshot)
            ? ((runtimeSnapshot.connected ? "已连接" : "未连接") + " · 订阅 " + String((runtimeSnapshot.subscriptions || []).length) + " 个")
            : (root.boundRuntimeStrategyId.length > 0 ? root.boundRuntimeStrategyId : "等待运行时会话")
        var strategyLabel = root.boundStrategyName.length > 0 ? root.boundStrategyName : "当前未绑定策略"
        var strategyDetail = root.boundStrategyId.length > 0
            ? (root.boundStrategyId + (root.boundRuntimeStrategyId.length > 0 ? " · runtime " + root.boundRuntimeStrategyId : ""))
            : "绑定策略后才会执行真实交易"
        var quoteDetail = String(root.marketSnapshot.updatedAt || root.currentMarketDisplaySymbol() || "").trim()
        var bridgeDetail = tradeExecutionService
            ? String(tradeExecutionService.liveBridgeStatusMessage() || "").trim()
            : "交易服务未初始化"
        var latestRuleHit = root.recentRuleHits.length > 0 ? (root.recentRuleHits[0] || ({})) : ({})
        var latestRuleHitTitle = root.ruleHitTitle(latestRuleHit)
        var runtimeRuleSummary = root.runtimeRuleDecisionHeadline(root.latestRuntimeRuleEvaluation)

        return [
            { title: "绑定策略", value: strategyLabel, detail: strategyDetail },
            { title: "策略状态", value: runtimeValue, detail: runtimeDetail },
            { title: "行情状态", value: marketStateLabel(), detail: quoteDetail.length > 0 ? quoteDetail : "等待目标标的行情" },
            { title: "执行桥接", value: bridgeStateLabel(), detail: bridgeDetail.length > 0 ? bridgeDetail : "等待桥接状态" },
            { title: "规则命中", value: String(root.recentRuleHits.length) + " 条", detail: latestRuleHitTitle !== "规则命中" ? root.ruleHitHeadline(latestRuleHit) : (runtimeRuleSummary.length > 0 ? runtimeRuleSummary : "最近暂无规则阻断") }
        ]
    }

    function runtimeRuleGroupDecisions(evaluation) {
        var payload = evaluation || ({})
        return payload.templateRuleGroupDecisions instanceof Array ? payload.templateRuleGroupDecisions : []
    }

    function runtimeRuleDecisionHeadline(evaluation) {
        var payload = evaluation || ({})
        var decisions = runtimeRuleGroupDecisions(payload)
        if (decisions.length === 0) {
            return ""
        }

        var consideredCount = 0
        var skippedCount = 0
        for (var index = 0; index < decisions.length; ++index) {
            var decision = decisions[index] || {}
            if (String(decision.disposition || "").toLowerCase() === "skipped") {
                skippedCount += 1
            } else {
                consideredCount += 1
            }
        }

        var reasonCode = String(payload.templateRuleDecisionReasonCode || payload.templateRuleReasonCode || "").trim()
        var fragments = ["裁决纳入 " + consideredCount + " 组", "跳过 " + skippedCount + " 组"]
        if (reasonCode.length > 0) {
            fragments.push(reasonCode)
        }
        return fragments.join(" · ")
    }

    function executionLogBadgeText(kind) {
        if (kind === "request") {
            return "提交"
        }
        if (kind === "rule") {
            return "规则"
        }
        if (kind === "status") {
            return "状态"
        }
        if (kind === "fill") {
            return "成交"
        }
        if (kind === "runtime") {
            return "策略"
        }
        if (kind === "position") {
            return "仓位"
        }
        return "系统"
    }

    function executionLogSeverityColor(level) {
        if (level === "error") {
            return "#ef4444"
        }
        if (level === "success") {
            return "#10b981"
        }
        if (level === "warning") {
            return "#f59e0b"
        }
        return "#3b82f6"
    }

    function safeNumber(value, fallback) {
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return fallback === undefined ? 0 : fallback
        }
        return numericValue
    }

    function normalizePositionTypeText(value) {
        var text = String(value || "").trim().toLowerCase()
        if (text.length === 0) {
            return ""
        }
        if (text === "margin_buy" || text === "marginbuy" || text.indexOf("融资") >= 0) {
            return "margin_buy"
        }
        if (text === "margin_sell" || text === "marginsell" || text.indexOf("融券") >= 0) {
            return "margin_sell"
        }
        if (text === "futures" || text === "future" || text.indexOf("期货") >= 0) {
            return "futures"
        }
        if (text === "options" || text === "option" || text.indexOf("期权") >= 0) {
            return "options"
        }
        if (text === "stock" || text === "equity" || text.indexOf("股票") >= 0) {
            return "stock"
        }
        return ""
    }

    function normalizePositionSideText(value) {
        var text = String(value || "").trim().toUpperCase()
        if (text === "BUY" || text === "LONG" || text === "多") {
            return "LONG"
        }
        if (text === "SELL" || text === "SHORT" || text === "空") {
            return "SHORT"
        }
        return ""
    }

    function resolvePositionType(raw) {
        var explicitType = normalizePositionTypeText(
            raw && (raw.type || raw.assetType || raw.asset_type || raw.accountType || raw.account_type
                || raw.positionType || raw.position_type || raw.instrumentType || raw.instrument_type)
        )
        if (explicitType.length > 0) {
            return explicitType
        }

        var optionType = String(raw && raw.optionType ? raw.optionType : "").trim().toLowerCase()
        var underlying = String(raw && raw.underlying ? raw.underlying : "").trim().toUpperCase()
        var expiry = String(raw && raw.expiry ? raw.expiry : "").trim()
        if (optionType.length > 0 || underlying.length > 0 || expiry.length > 0) {
            return "options"
        }

        if (isFuturesExchange(raw && raw.exchange ? raw.exchange : "")) {
            return "futures"
        }

        var side = normalizePositionSideText(raw && (raw.positionSide || raw.position_side || raw.side))
        return side === "SHORT" ? "margin_sell" : "stock"
    }

    function resolvePositionSide(raw, positionType) {
        var side = normalizePositionSideText(raw && (raw.positionSide || raw.position_side || raw.side))
        if (side.length > 0) {
            return side
        }
        return positionType === "margin_sell" ? "SHORT" : "LONG"
    }

    function positionTypeTitle(type) {
        if (type === "margin_buy") {
            return "融资"
        }
        if (type === "margin_sell") {
            return "融券"
        }
        if (type === "futures") {
            return "期货"
        }
        if (type === "options") {
            return "期权"
        }
        return "股票"
    }

    function positionUnit(type) {
        return type === "futures" || type === "options" ? "手" : "股"
    }

    function positionSideLabel(side) {
        return side === "SHORT" ? "空头" : "多头"
    }

    function closeableLabel(type, side) {
        if (type === "futures" || type === "options" || side === "SHORT") {
            return "可平"
        }
        return "可卖"
    }

    function normalizePositionQuantity(value, type) {
        var quantity = safeNumber(value, 0)
        if (type === "futures" || type === "options") {
            return Math.abs(quantity - Math.round(quantity)) < 0.000001 ? Math.round(quantity) : Number(quantity.toFixed(2))
        }
        return Math.round(quantity)
    }

    function inferCloseableQuantity(raw, type, quantity, availableQuantity) {
        var closeableQuantity = safeNumber(
            raw && (raw.closeableQuantity !== undefined ? raw.closeableQuantity
                : (raw.closeable_quantity !== undefined ? raw.closeable_quantity
                    : (raw.closableQuantity !== undefined ? raw.closableQuantity : availableQuantity))),
            availableQuantity)
        if (closeableQuantity <= 0) {
            closeableQuantity = availableQuantity > 0 ? availableQuantity : quantity
        }
        return normalizePositionQuantity(closeableQuantity, type)
    }

    function formatCurrencyText(value) {
        return "¥" + safeNumber(value, 0).toLocaleString(Qt.locale(), 'f', 2)
    }

    function buildPositionDetailText(type, raw, exchange) {
        var details = []
        if (type === "futures" && String(exchange || "").trim().length > 0) {
            details.push(String(exchange || "").trim().toUpperCase())
        }
        if (type === "options") {
            var underlying = String(raw && raw.underlying ? raw.underlying : "").trim().toUpperCase()
            var optionType = String(raw && raw.optionType ? raw.optionType : "").trim().toLowerCase()
            var expiry = String(raw && raw.expiry ? raw.expiry : "").trim()
            if (underlying.length > 0) {
                details.push("标的 " + underlying)
            }
            if (optionType.length > 0) {
                details.push(optionType === "put" ? "认沽" : "认购")
            }
            if (expiry.length > 0) {
                details.push(expiry)
            }
        }
        return details.join(" · ")
    }

    function canQuickClosePosition(positionData) {
        var data = positionData || ({})
        // closeableQuantity=0 时不能短路, 走 || 链
        var rawQty = safeNumber(data.closeableQuantity || data.availableQuantity || data.quantity, 0)
        var quantity = normalizePositionQuantity(rawQty, data.type || "stock")
        if (quantity <= 0) return false
        if (data.type === "futures" || data.type === "options") return quantity >= 1
        return Math.floor(quantity / 100) >= 1
    }

    function buildGroupedDisplayPositions(rows) {
        var definitions = [
            { key: "stock", title: "股票持仓" },
            { key: "margin_buy", title: "融资持仓" },
            { key: "margin_sell", title: "融券持仓" },
            { key: "futures", title: "期货持仓" },
            { key: "options", title: "期权持仓" }
        ]
        var groups = []
        var defIndex
        for (defIndex = 0; defIndex < definitions.length; ++defIndex) {
            var positions = []
            var rowIndex
            for (rowIndex = 0; rowIndex < (rows ? rows.length : 0); ++rowIndex) {
                if (rows[rowIndex].type === definitions[defIndex].key) {
                    positions.push(rows[rowIndex])
                }
            }
            if (positions.length > 0) {
                groups.push({
                    key: definitions[defIndex].key,
                    title: definitions[defIndex].title,
                    positions: positions
                })
            }
        }
        return groups
    }

    function calculateHoldingsPanelHeight(groups) {
        var groupCount = groups ? groups.length : 0
        var rowCount = 0
        var index
        for (index = 0; index < groupCount; ++index) {
            rowCount += groups[index].positions ? groups[index].positions.length : 0
        }
        return Math.max(208, 144 + groupCount * 28 + rowCount * 54)
    }

    function calculatePositionMarketValue(rawPositions) {
        var snapshotValue = accountSnapshot && accountSnapshot.marketValue !== undefined
            ? Number(accountSnapshot.marketValue)
            : 0
        if (!isNaN(snapshotValue) && snapshotValue > 0) {
            return snapshotValue
        }

        var total = 0
        for (var index = 0; index < (rawPositions ? rawPositions.length : 0); ++index) {
            var item = rawPositions[index] || ({})
            var itemMarketValue = Number(item.marketValue !== undefined ? item.marketValue : item.currentValue)
            if (!isNaN(itemMarketValue) && itemMarketValue > 0) {
                total += itemMarketValue
            }
        }
        return total
    }

    function mapDisplayPositions(rawPositions, totalMarketValue) {
        var rows = []
        var effectiveTotalMarketValue = Number(totalMarketValue || 0)
        var index

        for (index = 0; index < (rawPositions ? rawPositions.length : 0); ++index) {
            var item = rawPositions[index] || ({})
            var symbol = String(item.symbol || "").trim()
            var type = resolvePositionType(item)
            var side = resolvePositionSide(item, type)
            var quote = resolveDisplayQuote(symbol)
            var quantity = normalizePositionQuantity(item.shares !== undefined ? item.shares : item.quantity, type)
            var availableQuantity = normalizePositionQuantity(item.availableQuantity !== undefined ? item.availableQuantity : item.available, type)
            var closeableQuantity = inferCloseableQuantity(item, type, quantity, availableQuantity)
            var avgPrice = safeNumber(item.avgPrice !== undefined ? item.avgPrice : item.costBasis, 0)
            var lastPrice = safeNumber(item.lastPrice !== undefined ? item.lastPrice : (quote && quote.price !== undefined ? quote.price : 0), 0)
            var currentValue = safeNumber(item.currentValue !== undefined ? item.currentValue : item.marketValue, 0)
            var pnlValue = safeNumber(item.pnl !== undefined ? item.pnl : item.unrealizedPnl, 0)
            var costValue = avgPrice * quantity

            if (quantity <= 0 && currentValue <= 0 && closeableQuantity <= 0) {
                continue
            }

            if ((isNaN(currentValue) || currentValue <= 0) && !isNaN(lastPrice) && lastPrice > 0 && !isNaN(quantity) && quantity > 0) {
                currentValue = lastPrice * quantity
            }

            if (isNaN(pnlValue)) {
                pnlValue = 0
            }

            rows.push({
                id: symbol + "|" + type + "|" + side,
                symbol: symbol,
                name: String(item.name || (quote && quote.name ? quote.name : "")),
                type: type,
                typeLabel: positionTypeTitle(type),
                positionSide: side,
                positionSideLabel: positionSideLabel(side),
                detailText: buildPositionDetailText(type, item, item.exchange || (quote && quote.exchange ? quote.exchange : "")),
                exchange: String(item.exchange || (quote && quote.exchange ? quote.exchange : "")),
                quantity: isNaN(quantity) ? 0 : quantity,
                shares: isNaN(quantity) ? 0 : quantity,
                availableQuantity: isNaN(availableQuantity) ? 0 : availableQuantity,
                closeableQuantity: isNaN(closeableQuantity) ? 0 : closeableQuantity,
                closeableLabel: closeableLabel(type, side),
                unit: positionUnit(type),
                lastPrice: isNaN(lastPrice) ? 0 : lastPrice,
                avgPrice: isNaN(avgPrice) ? 0 : avgPrice,
                currentValue: isNaN(currentValue) ? 0 : currentValue,
                pnl: pnlValue,
                pnlRate: costValue > 0 ? (pnlValue / costValue) * 100 : 0,
                weight: effectiveTotalMarketValue > 0 && !isNaN(currentValue) ? (currentValue / effectiveTotalMarketValue) * 100 : 0,
                underlying: String(item.underlying || ""),
                optionType: String(item.optionType || ""),
                expiry: String(item.expiry || ""),
                canQuickClose: canQuickClosePosition({ type: type, closeableQuantity: closeableQuantity })
            })
        }

        rows.sort(function(lhs, rhs) {
            return Number(rhs.currentValue || 0) - Number(lhs.currentValue || 0)
        })

        return rows
    }

    function resolvePositionDisplayName(positionData) {
        var position = positionData || ({})
        var explicitName = String(position.name || "").trim()
        if (explicitName.length > 0) {
            return explicitName
        }

        var symbol = String(position.symbol || "").trim()
        if (symbol.length > 0 && marketBridgeReady && marketDataService) {
            var normalizedSymbol = serviceSymbolForMode(String(position.type || "stock"), symbol)
            var instrument = marketDataService.resolveInstrument(normalizedSymbol || symbol)
            var instrumentName = instrument && instrument.name ? String(instrument.name).trim() : ""
            if (instrumentName.length > 0) {
                return instrumentName
            }
        }

        return symbol.length > 0 ? symbol : "--"
    }

    function requiresCloseableLongPosition(mode, action, requestSide, requestPositionEffect) {
        if (mode === "stock") {
            return requestSide === "SELL"
        }
        if (mode === "margin_buy") {
            return requestPositionEffect === "CLOSE" && action !== "repay"
        }
        return false
    }

    function closeablePositionLabelForMode(mode) {
        return mode === "margin_buy" ? "可平" : "可卖"
    }

    function findDisplayPositionForMode(mode, symbol) {
        var normalizedSymbol = serviceSymbolForMode(mode, symbol)
        if (!normalizedSymbol) {
            return null
        }
        for (var index = 0; index < root.displayPositions.length; ++index) {
            var row = root.displayPositions[index] || ({})
            if (String(row.type || "").trim().toLowerCase() !== String(mode || "").trim().toLowerCase()) {
                continue
            }
            if (String(row.positionSide || "LONG").trim().toUpperCase() !== "LONG") {
                continue
            }
            if (serviceSymbolForMode(mode, row.symbol) === normalizedSymbol) {
                return row
            }
        }
        return null
    }

    function describeCurrentCloseablePosition(mode, symbol) {
        if (mode !== "stock" && mode !== "margin_buy") {
            return { summary: "", error: false, closeableQuantity: 0, unit: "股" }
        }

        var normalizedSymbol = serviceSymbolForMode(mode, symbol)
        if (!normalizedSymbol) {
            return { summary: "输入代码后显示当前" + closeablePositionLabelForMode(mode) + "数量", error: false, closeableQuantity: 0, unit: "股" }
        }

        var positionData = findDisplayPositionForMode(mode, normalizedSymbol)
        var closeableQuantity = normalizePositionQuantity(positionData ? positionData.closeableQuantity : 0, mode)
        var unit = positionData && positionData.unit ? String(positionData.unit) : "股"
        if (closeableQuantity <= 0) {
            return {
                summary: "当前无" + closeablePositionLabelForMode(mode) + "持仓",
                error: true,
                closeableQuantity: 0,
                unit: unit
            }
        }

        return {
            summary: "当前" + closeablePositionLabelForMode(mode) + " " + closeableQuantity + unit,
            error: false,
            closeableQuantity: closeableQuantity,
            unit: unit
        }
    }

    function validateCloseablePositionForTrade(mode, action, symbol, quantity, requestSide, requestPositionEffect) {
        if (!requiresCloseableLongPosition(mode, action, requestSide, requestPositionEffect)) {
            return { ok: true }
        }

        var positionInfo = describeCurrentCloseablePosition(mode, symbol)
        if (positionInfo.closeableQuantity <= 0) {
            return {
                ok: false,
                message: positionInfo.summary.length > 0 ? positionInfo.summary : ("当前无" + closeablePositionLabelForMode(mode) + "持仓")
            }
        }
        if (quantity > positionInfo.closeableQuantity) {
            return {
                ok: false,
                message: "委托数量 " + quantity + positionInfo.unit + " 超过当前" + closeablePositionLabelForMode(mode) + " " + positionInfo.closeableQuantity + positionInfo.unit
            }
        }
        return { ok: true }
    }

    function emptyMarketSnapshot(symbol) {
        return {
            price: 0,
            priceStr: "--",
            changePercent: "--",
            isUp: true,
            preClose: 0,
            upperLimit: 0,
            lowerLimit: 0,
            live: false,
            snapshotOnly: false,
            source: "",
            futuresPrice: 0,
            futuresPriceStr: "--",
            symbol: symbol || "",
            name: "",
            updatedAt: ""
        }
    }

    function cloneDepth(depth) {
        return {
            bids: depth && depth.bids ? depth.bids.slice(0) : [],
            asks: depth && depth.asks ? depth.asks.slice(0) : [],
            totalBid: depth && depth.totalBid ? depth.totalBid : 0,
            totalAsk: depth && depth.totalAsk ? depth.totalAsk : 0,
            levelCount: depth && depth.levelCount ? depth.levelCount : 0,
            live: !!(depth && depth.live),
            source: depth && depth.source ? depth.source : ""
        }
    }

    function emptyDepthSnapshot() {
        return {
            bids: [],
            asks: [],
            totalBid: 0,
            totalAsk: 0,
            levelCount: 0,
            live: false,
            source: ""
        }
    }

    function hasDepthRows(depth) {
        return !!(depth && ((depth.bids && depth.bids.length > 0) || (depth.asks && depth.asks.length > 0)))
    }

    function sumVolumes(rows) {
        var total = 0
        var index
        for (index = 0; index < rows.length; ++index) {
            total += Number(rows[index].volume || 0)
        }
        return total
    }

    function normalizeEquitySymbolInput(symbol) {
        var text = String(symbol || "").trim().toUpperCase()
        var match
        if (text.length === 0) {
            return ""
        }
        if (/^(SHSE|SZSE|BSE)\.\d{6}$/.test(text)) {
            if (text.indexOf("SHSE.") === 0) {
                return text.slice(5) + ".SH"
            }
            if (text.indexOf("SZSE.") === 0) {
                return text.slice(5) + ".SZ"
            }
            return text.slice(4) + ".BJ"
        }
        match = text.match(/^(\d{6})\.(SH|SZ|BJ)$/)
        if (match) {
            return match[1] + "." + match[2]
        }
        if (/^\d{6}$/.test(text)) {
            if (text.indexOf("8") === 0 || text.indexOf("4") === 0) {
                return text + ".BJ"
            }
            if (text.indexOf("6") === 0 || text.indexOf("5") === 0 || text.indexOf("9") === 0) {
                return text + ".SH"
            }
            return text + ".SZ"
        }
        return ""
    }

    function serviceSymbolForMode(mode, symbol) {
        var text = String(symbol || "").trim().toUpperCase()
        if (text.length === 0) {
            return ""
        }
        if (mode === "futures" || mode === "options") {
            return text
        }
        return normalizeEquitySymbolInput(text)
    }

    function currentMarketDisplaySymbol() {
        var serviceSymbol = serviceSymbolForMode(root.activeMode, root.activeSymbol)
        if (serviceSymbol.length > 0) {
            return serviceSymbol
        }
        return String(root.activeSymbol || "").trim().toUpperCase()
    }

    function priceDigitsForMode(mode) {
        if (mode === "futures") {
            return 0
        }
        if (mode === "options") {
            return 4
        }
        return 2
    }

    function boardLimitRatio(symbol) {
        var normalized = String(symbol || "").trim().toUpperCase()
        var code = normalized.indexOf(".") > 0 ? normalized.split(".")[0] : normalized
        if (normalized.indexOf(".BJ") > 0 || code.indexOf("8") === 0 || code.indexOf("4") === 0) {
            return 0.30
        }
        if (code.indexOf("300") === 0 || code.indexOf("301") === 0 || code.indexOf("688") === 0) {
            return 0.20
        }
        return 0.10
    }

    function roundPriceByMode(value, mode) {
        var digits = priceDigitsForMode(mode)
        return Number(Number(value || 0).toFixed(digits))
    }

    function signedPercentText(value) {
        var numericValue = Number(value || 0)
        return (numericValue >= 0 ? "+" : "") + numericValue.toFixed(2) + "%"
    }

    function translateOrderSide(side) {
        var text = String(side || "").trim().toUpperCase()
        if (text === "BUY") {
            return "买入"
        }
        if (text === "SELL") {
            return "卖出"
        }
        return text || "待处理"
    }

    function boolishOrderValue(value) {
        if (typeof value === "boolean") {
            return value
        }
        if (typeof value === "number") {
            return value !== 0
        }
        var text = String(value === undefined || value === null ? "" : value).trim().toLowerCase()
        return text === "1" || text === "true" || text === "yes"
    }

    function isFuturesExchange(exchange) {
        var text = String(exchange || "").trim().toUpperCase()
        return text === "CFFEX" || text === "SHFE" || text === "DCE"
            || text === "CZCE" || text === "INE" || text === "GFEX"
    }

    function resolveLiveOrderType(raw) {
        var explicitType = String(raw && raw.type ? raw.type : "").trim().toLowerCase()
        if (explicitType.length > 0) {
            return explicitType
        }

        var optionType = String(raw && raw.optionType ? raw.optionType : "").trim().toLowerCase()
        var underlying = String(raw && raw.underlying ? raw.underlying : "").trim()
        if (optionType.length > 0 || underlying.length > 0) {
            return "options"
        }

        if (isFuturesExchange(raw && raw.exchange ? raw.exchange : "")) {
            return "futures"
        }

        return "stock"
    }

    function resolveLiveOrderAction(raw) {
        var type = resolveLiveOrderType(raw)
        var rawAction = String(raw && raw.action ? raw.action : "").trim()
        if (rawAction.length > 0) {
            var actionLabel = tradeActionLabel(type, rawAction)
            if (actionLabel !== rawAction) {
                return actionLabel
            }
        }

        var side = String(raw && raw.side ? raw.side : "").trim().toUpperCase()
        var positionEffect = String(
            raw && raw.positionEffect ? raw.positionEffect
                : (raw && raw.position_effect_text ? raw.position_effect_text : "")
        ).trim().toUpperCase()

        if (type === "futures") {
            if (side === "BUY" && positionEffect === "OPEN") {
                return "开多"
            }
            if (side === "SELL" && positionEffect === "OPEN") {
                return "开空"
            }
            if (side === "SELL" && positionEffect === "CLOSE") {
                return "平多"
            }
            if (side === "BUY" && positionEffect === "CLOSE") {
                return "平空"
            }
        }

        if (type === "options") {
            if (side === "BUY" && positionEffect === "OPEN") {
                return "买入开仓"
            }
            if (side === "SELL" && positionEffect === "CLOSE") {
                return "卖出平仓"
            }
            if (side === "SELL" && positionEffect === "OPEN") {
                return "备兑开仓"
            }
            if (side === "BUY" && positionEffect === "CLOSE") {
                return "买入平仓"
            }
        }

        return translateOrderSide(side || rawAction)
    }

    function translateOrderStatus(status) { return OrderUtils.translateOrderStatus(status) }
    function orderUnit(order)               { return OrderUtils.orderUnit(order) }
    function normalizedOrderStatusValue(s)  { return OrderUtils.normalizedOrderStatus(s) }

    function orderStatusPhaseValue(orderItem) {
        var status = normalizedOrderStatusValue(orderItem && orderItem.rawStatus ? orderItem.rawStatus : (orderItem ? orderItem.status : ""))
        if (status === "REQUESTED") {
            return 0
        }
        if (status === "PENDING_RISK") {
            return 1
        }
        if (status === "PENDING" || status === "SUBMITTED") {
            return 2
        }
        if (status === "PARTIAL_FILLED" || status === "PENDING_CANCEL") {
            return 3
        }
        if (status === "CANCELLED" || status === "REJECTED") {
            return 4
        }
        if (status === "FILLED") {
            return 5
        }
        return 0
    }

    function orderFilledQuantityValue(orderItem) {
        var filledQuantity = Number(orderItem && orderItem.filledQty !== undefined ? orderItem.filledQty : 0)
        return isNaN(filledQuantity) ? 0 : filledQuantity
    }

    function orderUpdatedTimeValue(orderItem) {
        return String(orderItem && orderItem.time ? orderItem.time : "")
    }

    function incomingOrderPreferred(existingOrder, incomingOrder) {
        var existingPhase = orderStatusPhaseValue(existingOrder)
        var incomingPhase = orderStatusPhaseValue(incomingOrder)
        if (incomingPhase !== existingPhase) {
            return incomingPhase > existingPhase
        }

        var existingFilled = orderFilledQuantityValue(existingOrder)
        var incomingFilled = orderFilledQuantityValue(incomingOrder)
        if (incomingFilled !== existingFilled) {
            return incomingFilled > existingFilled
        }

        var existingTime = orderUpdatedTimeValue(existingOrder)
        var incomingTime = orderUpdatedTimeValue(incomingOrder)
        if (incomingTime !== existingTime) {
            return incomingTime > existingTime
        }

        return false
    }

    function mergeOrderItems(existingOrder, incomingOrder) {
        var preferIncoming = incomingOrderPreferred(existingOrder, incomingOrder)
        var preferred = preferIncoming ? incomingOrder : existingOrder
        var fallback = preferIncoming ? existingOrder : incomingOrder
        var merged = {}
        var key

        for (key in fallback) {
            merged[key] = fallback[key]
        }
        for (key in preferred) {
            merged[key] = preferred[key]
        }

        return merged
    }

    function orderStatusDigest(orderItem) {
        var quantity = Number(orderItem && orderItem.qty !== undefined ? orderItem.qty : 0)
        var filledQuantity = Number(orderItem && orderItem.filledQty !== undefined ? orderItem.filledQty : 0)
        var status = normalizedOrderStatusValue(orderItem && orderItem.rawStatus ? orderItem.rawStatus : (orderItem ? orderItem.status : ""))
        var message = String(orderItem && orderItem.message ? orderItem.message : "").trim()
        var ruleId = String(orderItem && orderItem.ruleId ? orderItem.ruleId : "").trim()
        var reasonCode = String(orderItem && orderItem.reasonCode ? orderItem.reasonCode : "").trim()
        var requiredBatchId = String(orderItem && (orderItem.requiredBatchId || orderItem.batchId) ? (orderItem.requiredBatchId || orderItem.batchId) : "").trim()
        if (isNaN(quantity)) {
            quantity = 0
        }
        if (isNaN(filledQuantity)) {
            filledQuantity = 0
        }
        var digest = status + "|" + quantity + "|" + filledQuantity
        if (status === "REJECTED" && message.length > 0) {
            digest += "|" + message
        }
        if (ruleId.length > 0) {
            digest += "|" + ruleId
        }
        if (reasonCode.length > 0) {
            digest += "|" + reasonCode
        }
        if (requiredBatchId.length > 0) {
            digest += "|" + requiredBatchId
        }
        return digest
    }

    function resolveOrderLabel(orderItem) {
        var symbol = String(orderItem && orderItem.symbol ? orderItem.symbol : "").trim()
        if (!symbol) {
            return "当前委托"
        }
        if (marketBridgeReady && marketDataService) {
            var instrument = marketDataService.resolveInstrument(symbol)
            var name = instrument && instrument.name ? String(instrument.name).trim() : ""
            if (name.length > 0) {
                return name + " " + symbol
            }
        }
        return symbol
    }

    function buildOrderStatusToast(orderItem) {
        var status = normalizedOrderStatusValue(orderItem && orderItem.rawStatus ? orderItem.rawStatus : (orderItem ? orderItem.status : ""))
        var statusOrigin = String(orderItem && orderItem.statusOrigin ? orderItem.statusOrigin : "").trim().toLowerCase()
        var action = String(orderItem && orderItem.action ? orderItem.action : "委托")
        var quantity = Number(orderItem && orderItem.qty !== undefined ? orderItem.qty : 0)
        var filledQuantity = Number(orderItem && orderItem.filledQty !== undefined ? orderItem.filledQty : 0)
        var message = String(orderItem && orderItem.message ? orderItem.message : "").trim()
        var ruleId = String(orderItem && orderItem.ruleId ? orderItem.ruleId : "").trim()
        var reasonCode = String(orderItem && orderItem.reasonCode ? orderItem.reasonCode : "").trim()
        var requiredBatchId = String(orderItem && (orderItem.requiredBatchId || orderItem.batchId) ? (orderItem.requiredBatchId || orderItem.batchId) : "").trim()
        var unit = root.orderUnit(orderItem || {})
        var label = resolveOrderLabel(orderItem)

        if (isNaN(quantity) || quantity < 0) {
            quantity = 0
        }
        if (isNaN(filledQuantity) || filledQuantity < 0) {
            filledQuantity = 0
        }

        if (status === "PENDING_RISK") {
            return {
                message: label + " " + action + "已进入风控审批",
                isError: false
            }
        }
        if (status === "SUBMITTED") {
            if (message.indexOf("本地待处理") !== -1) {
                return {
                    message: label + " " + action + "已通过风控，当前为本地待处理",
                    isError: false
                }
            }
            return {
                message: label + " " + action + (statusOrigin === "local_request" ? "已发往交易通道" : "委托已提交"),
                isError: false
            }
        }
        if (status === "PENDING") {
            return {
                message: label + " " + action + "已通过风控，正在等待交易通道确认",
                isError: false
            }
        }
        if (status === "PARTIAL_FILLED") {
            var partialSuffix = filledQuantity > 0 && quantity > 0
                ? " " + filledQuantity + "/" + quantity + unit
                : ""
            return {
                message: label + " " + action + "部分成交" + partialSuffix,
                isError: false
            }
        }
        if (status === "FILLED") {
            var filledSuffix = quantity > 0 ? " " + quantity + unit : ""
            return {
                message: label + " " + action + "已全部成交" + filledSuffix,
                isError: false
            }
        }
        if (status === "CANCELLED") {
            return {
                message: label + " " + action + "委托已撤单",
                isError: false
            }
        }
        if (status === "PENDING_CANCEL") {
            return {
                message: label + " " + action + "撤单请求已提交",
                isError: false
            }
        }
        if (status === "REJECTED") {
            if (statusOrigin === "execution_rule_reject" || ruleId.length > 0) {
                var ruleParts = []
                if (ruleId.length > 0) {
                    ruleParts.push("规则 " + ruleId)
                }
                if (reasonCode.length > 0) {
                    ruleParts.push("原因码 " + reasonCode)
                }
                if (requiredBatchId.length > 0) {
                    ruleParts.push("批次 " + requiredBatchId)
                }
                return {
                    message: label + " " + action + "被执行规则阻断"
                        + (ruleParts.length > 0 ? "：" + ruleParts.join(" / ") : "")
                        + (message.length > 0 ? " · " + message : ""),
                    isError: true
                }
            }
            return {
                message: message.length > 0
                    ? (label + " " + action + "委托被拒绝：" + message)
                    : (label + " " + action + "委托被拒绝"),
                isError: true
            }
        }
        return null
    }

    function buildMarketSnapshotFromQuote(quote) {
        var priceValue = Number(quote && quote.price !== undefined ? quote.price : 0)
        var isRealtime = hasRealtimeQuote(quote)
        var isSnapshotQuote = hasSnapshotQuote(quote)
        var digits = priceDigitsForMode(root.activeMode)
        var usesStockLimits = root.activeMode === "stock" || root.activeMode === "margin_buy" || root.activeMode === "margin_sell"
        var symbolValue = quote && quote.symbol ? String(quote.symbol) : ""
        var sourceValue = String(quote && quote.source ? quote.source : "").trim().toLowerCase()
        var updatedAtValue = String(quote && quote.updatedAt ? quote.updatedAt : "").trim()
        var supportsPlaceholder = !isRealtime
            && (isSnapshotQuote || sourceValue === "seed" || sourceValue === "watchlist" || sourceValue === "database_name")
        if (!isRealtime && !supportsPlaceholder) {
            return null
        }
        if (!priceValue || isNaN(priceValue) || priceValue <= 0) {
            if (!supportsPlaceholder) {
                return null
            }
            return {
                price: 0,
                priceStr: "--",
                changePercent: "--",
                isUp: true,
                preClose: 0,
                upperLimit: 0,
                lowerLimit: 0,
                live: false,
                snapshotOnly: true,
                source: sourceValue,
                futuresPrice: 0,
                futuresPriceStr: "--",
                symbol: symbolValue,
                name: quote.name || "",
                updatedAt: updatedAtValue
            }
        }

        var changeValue = Number(quote && quote.changePct !== undefined ? quote.changePct
            : (quote && quote.changePercent !== undefined ? quote.changePercent : 0))
        var preCloseValue = Number(quote && quote.preClose !== undefined ? quote.preClose : (quote && quote.pre_close !== undefined ? quote.pre_close : 0))
        if ((!preCloseValue || isNaN(preCloseValue) || preCloseValue <= 0) && priceValue > 0) {
            preCloseValue = priceValue / (1 + changeValue / 100.0)
        }
        if (!preCloseValue || isNaN(preCloseValue) || preCloseValue <= 0) {
            preCloseValue = priceValue
        }
        var upperLimitPrice = 0
        var lowerLimitPrice = 0
        if (usesStockLimits) {
            var limitRatio = boardLimitRatio(symbolValue)
            upperLimitPrice = roundPriceByMode(preCloseValue * (1 + limitRatio), "stock")
            lowerLimitPrice = roundPriceByMode(preCloseValue * (1 - limitRatio), "stock")
        }
        return {
            price: priceValue,
            priceStr: priceValue.toFixed(digits),
            changePercent: signedPercentText(changeValue),
            isUp: changeValue >= 0,
            preClose: preCloseValue,
            upperLimit: upperLimitPrice,
            lowerLimit: lowerLimitPrice,
            live: isRealtime,
            snapshotOnly: !isRealtime,
            source: sourceValue,
            futuresPrice: root.activeMode === "futures" ? priceValue : 0,
            futuresPriceStr: root.activeMode === "futures" ? priceValue.toFixed(digits) : "--",
            symbol: symbolValue,
            name: quote.name || "",
            updatedAt: updatedAtValue
        }
    }

    function tradeActionLabel(mode, action) {
        if (mode === "stock") {
            return action === "buy" ? "买入" : "卖出"
        }
        if (mode === "futures") {
            if (action === "long") {
                return "开多"
            }
            if (action === "short") {
                return "开空"
            }
            if (action === "closeLong") {
                return "平多"
            }
            if (action === "closeShort") {
                return "平空"
            }
        }
        if (mode === "margin_buy") {
            if (action === "repay") {
                return "现金还款"
            }
            if (action === "closeLong") {
                return "卖券还款"
            }
            return "融资买入"
        }
        if (mode === "margin_sell") {
            if (action === "returnStock") {
                return "现券还券"
            }
            if (action === "closeShort") {
                return "买券还券"
            }
            return "融券卖出"
        }
        if (mode === "options") {
            if (action === "optionBuy") {
                return "买入开仓"
            }
            if (action === "optionSell") {
                return "卖出平仓"
            }
            if (action === "optionClose") {
                return "备兑开仓"
            }
            if (action === "optionCoveredClose") {
                return "备兑平仓"
            }
            if (action === "optionExercise") {
                return "行权"
            }
        }
        return translateOrderSide(action)
    }

    function isBridgeManagedTrade(mode, action) {
        return mode === "stock" || mode === "margin_buy" || mode === "margin_sell"
            || mode === "futures" || mode === "options"
    }

    function invalidSymbolMessageForMode(mode) {
        if (mode === "futures") {
            return "请输入有效期货合约代码"
        }
        if (mode === "options") {
            return "请输入有效期权合约代码"
        }
        return "请输入有效6位股票代码"
    }

    function hasRealtimeQuote(quote) {
        var source = String(quote && quote.source ? quote.source : "").trim().toLowerCase()
        var updatedAt = String(quote && quote.updatedAt ? quote.updatedAt : "").trim()
        if (!quote || !quote.symbol) {
            return false
        }
        return source !== "seed" && source !== "watchlist" && source !== "daily_snapshot" && updatedAt.length > 0 && updatedAt !== "--"
    }

    function hasSnapshotQuote(quote) {
        var source = String(quote && quote.source ? quote.source : "").trim().toLowerCase()
        var updatedAt = String(quote && quote.updatedAt ? quote.updatedAt : "").trim()
        if (!quote || !quote.symbol) {
            return false
        }
        if (source === "seed" || source === "watchlist") {
            return false
        }
        return updatedAt.length > 0 && updatedAt !== "--"
    }

    function hasDisplayQuote(quote) {
        var source = String(quote && quote.source ? quote.source : "").trim().toLowerCase()
        if (!(quote && quote.symbol)) {
            return false
        }
        if (hasRealtimeQuote(quote) || hasSnapshotQuote(quote)) {
            return true
        }
        return source === "seed" || source === "watchlist" || source === "database_name"
    }

    function resolveLiveQuote(symbol) {
        var normalizedSymbol = serviceSymbolForMode(root.activeMode, symbol)
        if (!normalizedSymbol || !marketBridgeReady) {
            return null
        }

        var quote = marketDataService.resolveInstrument(normalizedSymbol)
        if (!hasRealtimeQuote(quote)) {
            return null
        }
        return quote
    }

    function resolveDisplayQuote(symbol) {
        var normalizedSymbol = serviceSymbolForMode(root.activeMode, symbol)
        if (!normalizedSymbol || !marketBridgeReady) {
            return null
        }

        var quote = marketDataService.resolveInstrument(normalizedSymbol)
        if (!hasDisplayQuote(quote)) {
            return null
        }
        return quote
    }

    function mapServiceOrders(sourceList, options) {
        var result = []
        var seenIds = {}
        var settings = options || ({})
        var skipLocalRequest = !!settings.skipLocalRequest
        var index

        for (index = 0; index < (sourceList ? sourceList.length : 0); ++index) {
            var raw = sourceList[index] || ({})
            var statusOrigin = String(raw.statusOrigin || raw.status_origin || "").trim().toLowerCase()
            if (skipLocalRequest && statusOrigin === "local_request") {
                continue
            }
            var rawId = String(raw.orderId || raw.id || "").trim()
            var clientOrderId = String(raw.clientOrderId || raw.client_order_id || rawId).trim()
            var brokerOrderId = String(raw.brokerOrderId || raw.broker_order_id || "").trim()
            var canonicalId = clientOrderId || rawId || brokerOrderId
            if (!canonicalId || seenIds[canonicalId]) {
                continue
            }
            seenIds[canonicalId] = true
            result.push({
                id: canonicalId,
                rawOrderId: rawId,
                clientOrderId: clientOrderId,
                brokerOrderId: brokerOrderId,
                cancelOrderId: clientOrderId || rawId || brokerOrderId,
                source: "live",
                symbol: String(raw.symbol || "--"),
                type: resolveLiveOrderType(raw),
                action: resolveLiveOrderAction(raw),
                requestAction: String(raw.action || "").trim(),
                mode: String(raw.mode || raw.type || resolveLiveOrderType(raw)).trim().toLowerCase(),
                side: String(raw.side || "").trim().toUpperCase(),
                orderType: String(raw.orderType || raw.order_type || "").trim().toUpperCase(),
                positionEffect: String(raw.positionEffect || raw.position_effect || "").trim().toUpperCase(),
                underlying: String(raw.underlying || "").trim(),
                optionType: String(raw.optionType || raw.option_type || "").trim(),
                expiry: String(raw.expiry || "").trim(),
                qty: Number(raw.quantity !== undefined ? raw.quantity : (raw.qty !== undefined ? raw.qty : (raw.totalQuantity !== undefined ? raw.totalQuantity : 0))),
                price: Number(raw.price || 0),
                cashAmount: Number(raw.cashAmount !== undefined ? raw.cashAmount : (raw.cash_amount !== undefined ? raw.cash_amount : (raw.requestedNotional !== undefined ? raw.requestedNotional : 0))),
                message: String(raw.message || "").trim(),
                time: String(raw.updatedAt || raw.createdAt || raw.time || "--"),
                statusOrigin: statusOrigin,
                status: translateOrderStatus(raw.status || raw.rawStatus),
                rawStatus: String(raw.status || raw.rawStatus || ""),
                filledQty: Number(raw.filledQuantity !== undefined ? raw.filledQuantity : (raw.filledQty !== undefined ? raw.filledQty : (raw.filled !== undefined ? raw.filled : 0))),
                strategyId: String(raw.strategyId || "").trim(),
                strategyName: String(raw.strategyName || "").trim(),
                executionScopeId: String(raw.executionScopeId || raw.execution_scope_id || "").trim(),
                batchId: String(raw.batchId || raw.batch_id || "").trim(),
                batchIndex: Number(raw.batchIndex !== undefined ? raw.batchIndex : raw.batch_index),
                executionSequence: Number(raw.executionSequence !== undefined ? raw.executionSequence : raw.execution_sequence),
                batchRole: String(raw.batchRole || raw.batch_role || "").trim(),
                batchPhase: String(raw.batchPhase || raw.batch_phase || "").trim(),
                batchOrderCount: Number(raw.batchOrderCount !== undefined ? raw.batchOrderCount : raw.batch_order_count),
                previousBatchId: String(raw.previousBatchId || raw.previous_batch_id || "").trim(),
                previousBatchOrderCount: Number(raw.previousBatchOrderCount !== undefined ? raw.previousBatchOrderCount : raw.previous_batch_order_count),
                nextBatchId: String(raw.nextBatchId || raw.next_batch_id || "").trim(),
                requiresPreviousBatchFilled: boolishOrderValue(raw.requiresPreviousBatchFilled !== undefined ? raw.requiresPreviousBatchFilled : raw.requires_previous_batch_filled),
                pauseOnConflict: boolishOrderValue(raw.pauseOnConflict !== undefined ? raw.pauseOnConflict : raw.pause_on_conflict),
                pauseOnAbnormalReject: boolishOrderValue(raw.pauseOnAbnormalReject !== undefined ? raw.pauseOnAbnormalReject : raw.pause_on_abnormal_reject),
                requiresManualCheckpoint: boolishOrderValue(raw.requiresManualCheckpoint !== undefined ? raw.requiresManualCheckpoint : raw.requires_manual_checkpoint),
                manualCheckpointBatchIndex: Number(raw.manualCheckpointBatchIndex !== undefined ? raw.manualCheckpointBatchIndex : raw.manual_checkpoint_batch_index),
                blocksFollowingBatches: boolishOrderValue(raw.blocksFollowingBatches !== undefined ? raw.blocksFollowingBatches : raw.blocks_following_batches),
                ruleId: String(raw.ruleId || raw.rule_id || "").trim(),
                reasonCode: String(raw.reasonCode || raw.reason_code || "").trim(),
                requiredBatchId: String(raw.requiredBatchId || raw.required_batch_id || "").trim(),
                blockingBatchId: String(raw.blockingBatchId || raw.blocking_batch_id || "").trim(),
                blockingOrderId: String(raw.blockingOrderId || raw.blocking_order_id || "").trim(),
                blockingStatus: String(raw.blockingStatus || raw.blocking_status || "").trim()
            })
        }

        return result
    }

    function mapSimulatedOrders(sourceList) {
        var result = []
        var index

        for (index = 0; index < (sourceList ? sourceList.length : 0); ++index) {
            var raw = sourceList[index] || ({})
            result.push({
                id: raw.id,
                rawOrderId: raw.id,
                clientOrderId: raw.id,
                brokerOrderId: "",
                cancelOrderId: raw.id,
                source: "simulation",
                symbol: raw.symbol || "--",
                type: raw.type || "stock",
                action: raw.action || "待处理",
                qty: Number(raw.qty || 0),
                price: Number(raw.price || 0),
                cashAmount: Number(raw.cashAmount || 0),
                message: String(raw.message || "").trim(),
                time: raw.time || "--",
                status: raw.status || "待处理",
                rawStatus: String(raw.rawStatus || raw.status || ""),
                filledQty: Number(raw.filledQty !== undefined ? raw.filledQty : (raw.filledQuantity !== undefined ? raw.filledQuantity : 0))
            })
        }

        return result
    }

    function showPageToast(message, isError) {
        root.toastMessage = message
        root.toastError = !!isError
    }

    function requestHoldingsRefresh() {
        if (!positionAccountService || typeof positionAccountService.requestInitialSnapshot !== "function") {
            showPageToast("持仓服务未就绪", true)
            return
        }
        scheduleInitialSnapshotRefresh("manual_refresh", true)
        root.syncMarketState()
        root.pushExecutionLog("position", "仓位刷新", "已请求最新持仓与账户快照", "info")
        showPageToast("已请求刷新持仓快照", false)
    }

    function refreshStrategyRuntimeStatus(showToast) {
        if (tradingConnectionConfigService) {
            if (typeof tradingConnectionConfigService.refreshClientProcessStatusAsync === "function") {
                tradingConnectionConfigService.refreshClientProcessStatusAsync()
            } else if (typeof tradingConnectionConfigService.refreshClientProcessStatus === "function") {
                tradingConnectionConfigService.refreshClientProcessStatus()
            }
        }
        if (tradingRuntimeStatusService) {
            if (typeof tradingRuntimeStatusService.refreshAsync === "function") {
                tradingRuntimeStatusService.refreshAsync()
            } else if (typeof tradingRuntimeStatusService.refresh === "function") {
                tradingRuntimeStatusService.refresh()
            }
        }
        root.refreshRuntimeSnapshot(false)
        if (showToast) {
            showPageToast("已刷新策略状态", false)
        }
    }

    function syncPendingOrders() {
        if (!root.serviceBindingsActive) {
            return
        }
        var mergedOrders = []
        var mergedOrderById = {}
        var orderKeys = []
        var nextOrderStatusDigestById = {}
        var toastPayloads = []
        var lists = [
            mapServiceOrders(positionAccountService ? (positionAccountService.recentOrderStatuses || []) : [], { skipLocalRequest: true }),
            mapServiceOrders(tradeExecutionService ? (tradeExecutionService.recentOrders || []) : [], { skipLocalRequest: true }),
            mapSimulatedOrders(TradeJs.getOrders())
        ]
        var listIndex
        var itemIndex

        for (listIndex = 0; listIndex < lists.length; ++listIndex) {
            for (itemIndex = 0; itemIndex < lists[listIndex].length; ++itemIndex) {
                var orderItem = lists[listIndex][itemIndex]
                var orderKey = String(orderItem.id)
                if (!mergedOrderById[orderKey]) {
                    mergedOrderById[orderKey] = orderItem
                    orderKeys.push(orderKey)
                } else {
                    mergedOrderById[orderKey] = mergeOrderItems(mergedOrderById[orderKey], orderItem)
                }
            }
        }

        for (itemIndex = 0; itemIndex < orderKeys.length; ++itemIndex) {
            var resolvedKey = orderKeys[itemIndex]
            var resolvedOrder = mergedOrderById[resolvedKey]
            if (!resolvedOrder) {
                continue
            }
            nextOrderStatusDigestById[resolvedKey] = orderStatusDigest(resolvedOrder)
            if (root.orderStatusDigestReady && root.orderStatusDigestById[resolvedKey] !== nextOrderStatusDigestById[resolvedKey]) {
                var toastPayload = root.buildOrderStatusToast(resolvedOrder)
                if (toastPayload) {
                    toastPayloads.push(toastPayload)
                }
            }
            mergedOrders.push(resolvedOrder)
        }

        root.pendingOrders = mergedOrders
        root.orderStatusDigestById = nextOrderStatusDigestById
        root.orderStatusDigestReady = true

        if (toastPayloads.length > 0) {
            var latestToast = toastPayloads[toastPayloads.length - 1]
            root.showPageToast(latestToast.message, latestToast.isError)
        }
    }

    function ensureLiveWatch(reason) {
        if (!root.serviceBindingsActive) {
            return
        }
        var watchSymbol = serviceSymbolForMode(root.activeMode, root.activeSymbol)
        if (!watchSymbol || !marketBridgeReady) {
            return
        }
        traceWatchRequest(reason || "watch", watchSymbol)
        marketDataService.ensureWatchSymbol(watchSymbol)
    }

    function updateDepthForMode(liveQuote) {
        if (root.quoteBridgeMode && root.marketBridgeReady) {
            var resolvedQuote = liveQuote || resolveLiveQuote(root.activeSymbol)
            var resolvedDepth = resolvedQuote && resolvedQuote.depthSnapshot ? root.cloneDepth(resolvedQuote.depthSnapshot) : root.emptyDepthSnapshot()
            root.stockDepthSnapshot = resolvedDepth
            root.depthSnapshot = root.cloneDepth(resolvedDepth)
            root.tickRows = resolvedQuote && resolvedQuote.recentTicks ? root.cloneList(resolvedQuote.recentTicks) : []
            return
        }

        root.stockDepthSnapshot = root.emptyDepthSnapshot()
        root.depthSnapshot = root.emptyDepthSnapshot()
        root.tickRows = []
    }

    function buildQuickCloseRequest(positionData) {
        var data = positionData || ({})
        var mode = String(data.type || "stock").trim().toLowerCase()
        // closeableQuantity=0 时不能短路, 需要走 || 链到 quantity
        var rawCloseQty = safeNumber(data.closeableQuantity || data.availableQuantity || data.quantity, 0)
        var closeQuantity = normalizePositionQuantity(rawCloseQty, mode)

        if (closeQuantity <= 0) {
            return { error: "当前仓位没有可平数量" }
        }

        // 股票/融资融券向下取整到整手(100股), A股买卖均为100的整数倍
        if (mode === "stock" || mode === "margin_buy" || mode === "margin_sell") {
            closeQuantity = Math.floor(closeQuantity / 100) * 100
            if (closeQuantity < 100) {
                return { error: "当前可平数量不足1手(100股)" }
            }
        }

        var requestSide = "SELL"
        var requestPositionEffect = ""
        var requestAction = "sell"
        var positionSide = String(data.positionSide || "LONG").trim().toUpperCase()

        if (mode === "margin_buy") {
            requestSide = "SELL"
            requestPositionEffect = "CLOSE"
            requestAction = "closeLong"
        } else if (mode === "margin_sell") {
            requestSide = "BUY"
            requestPositionEffect = "CLOSE"
            requestAction = "closeShort"
        } else if (mode === "futures") {
            requestSide = positionSide === "SHORT" ? "BUY" : "SELL"
            requestPositionEffect = "CLOSE"
            requestAction = positionSide === "SHORT" ? "closeShort" : "closeLong"
        } else if (mode === "options") {
            requestSide = positionSide === "SHORT" ? "BUY" : "SELL"
            requestPositionEffect = "CLOSE"
            requestAction = positionSide === "SHORT" ? "optionCoveredClose" : "optionSell"
        }

        var requestSymbol = serviceSymbolForMode(mode, data.symbol)
        if (!requestSymbol) {
            return { error: invalidSymbolMessageForMode(mode) }
        }

        var liveQuote = resolveLiveQuote(requestSymbol)
        var livePrice = safeNumber(liveQuote && liveQuote.price !== undefined ? liveQuote.price : 0, 0)
        var fallbackPrice = safeNumber(data.lastPrice || data.avgPrice, 0)
        var useMarket = hasRealtimeQuote(liveQuote) && livePrice > 0
        var requestPrice = useMarket ? livePrice : (fallbackPrice > 0 ? fallbackPrice : livePrice)
        if (requestPrice <= 0) {
            return { error: "当前仓位缺少可用价格，暂时无法一键平仓" }
        }

        var request = {
            symbol: requestSymbol,
            side: requestSide,
            price: requestPrice,
            quantity: closeQuantity,
            orderType: useMarket ? "MARKET" : "LIMIT",
            mode: mode,
            action: requestAction
        }

        if (requestPositionEffect.length > 0) {
            request.positionEffect = requestPositionEffect
        }
        if (mode === "options") {
            request.underlying = data.underlying
            request.optionType = data.optionType
            request.expiry = data.expiry
        }

        return {
            request: request,
            actionLabel: tradeActionLabel(mode, requestAction)
        }
    }

    function quickClosePosition(symbol, type) {
        if (!tradeExecutionService) {
            showPageToast("交易服务未就绪", true)
            return
        }
        var mode = String(type || "stock").trim().toLowerCase()
        var result = tradeExecutionService.quickClosePosition(String(symbol || ""), mode)
        if (result && result.accepted) {
            syncPendingOrders()
            showPageToast(String(result.message || "平仓委托已提交"), false)
        } else {
            showPageToast(String(result ? (result.message || "平仓失败") : "平仓失败"), true)
        }
    }

    function buildRuleRetryRequest(orderData) {
        var source = orderData || ({})
        var symbol = String(source.symbol || "").trim().toUpperCase()
        var side = String(source.side || "").trim().toUpperCase()
        var quantity = Number(source.qty !== undefined ? source.qty : 0)
        var cashAmount = Number(source.cashAmount !== undefined ? source.cashAmount : 0)
        var orderType = String(source.orderType || "LIMIT").trim().toUpperCase()
        var price = Number(source.price !== undefined ? source.price : 0)

        if (symbol.length === 0 || side.length === 0) {
            return null
        }
        if ((isNaN(quantity) || quantity <= 0) && (isNaN(cashAmount) || cashAmount <= 0)) {
            return null
        }
        if (orderType !== "MARKET" && (isNaN(price) || price <= 0)) {
            return null
        }

        var request = {
            symbol: symbol,
            side: side,
            price: isNaN(price) ? 0 : price,
            quantity: isNaN(quantity) ? 0 : quantity,
            orderType: orderType,
            mode: String(source.mode || source.type || "stock").trim().toLowerCase(),
            action: String(source.requestAction || "").trim()
        }

        if (!isNaN(cashAmount) && cashAmount > 0) {
            request.cashAmount = cashAmount
        }

        var positionEffect = String(source.positionEffect || "").trim().toUpperCase()
        if (positionEffect.length > 0) {
            request.positionEffect = positionEffect
        }

        var underlying = String(source.underlying || "").trim()
        if (underlying.length > 0) {
            request.underlying = underlying
        }

        var optionType = String(source.optionType || "").trim()
        if (optionType.length > 0) {
            request.optionType = optionType
        }

        var expiry = String(source.expiry || "").trim()
        if (expiry.length > 0) {
            request.expiry = expiry
        }

        var propagationKeys = [
            "batchId",
            "batchIndex",
            "executionSequence",
            "batchRole",
            "batchPhase",
            "batchOrderCount",
            "previousBatchId",
            "previousBatchOrderCount",
            "nextBatchId",
            "executionScopeId",
            "requiresPreviousBatchFilled",
            "pauseOnConflict",
            "pauseOnAbnormalReject",
            "requiresManualCheckpoint",
            "manualCheckpointBatchIndex",
            "blocksFollowingBatches",
            "strategyId",
            "strategyName"
        ]
        var index
        for (index = 0; index < propagationKeys.length; ++index) {
            var key = propagationKeys[index]
            if (source[key] !== undefined && source[key] !== null && String(source[key]).length > 0) {
                request[key] = source[key]
            }
        }

        if ((!request.batchId || String(request.batchId).trim().length === 0)
                && String(source.requiredBatchId || "").trim().length > 0) {
            request.batchId = String(source.requiredBatchId).trim()
        }

        return request
    }

    function resumeExecutionPauseForOrder(orderData, retryAfterResume) {
        var source = orderData || ({})
        var executionScopeId = String(source.executionScopeId || "").trim()
        var pausedBatchId = String(source.blockingBatchId || source.requiredBatchId || "").trim()
        var currentBatchId = String(source.batchId || "").trim()

        if (executionScopeId.length === 0) {
            showPageToast("当前委托缺少执行域信息，无法恢复执行暂停", true)
            return
        }
        if (!tradeExecutionService || typeof tradeExecutionService.resumeExecutionPause !== "function") {
            showPageToast("交易服务未就绪，暂时无法恢复执行暂停", true)
            return
        }

        if (!tradeExecutionService.resumeExecutionPause(executionScopeId, pausedBatchId)) {
            var resumeError = tradeExecutionService.lastErrorMessage
                ? String(tradeExecutionService.lastErrorMessage).trim()
                : ""
            showPageToast(resumeError.length > 0 ? resumeError : "执行暂停恢复失败", true)
            return
        }

        root.pushExecutionLog(
            "rule",
            "执行暂停已恢复",
            resolveInstrumentLabel(source.symbol) + " · 执行域 " + executionScopeId
                + (pausedBatchId.length > 0 ? " · 阻断批次 " + pausedBatchId : ""),
            "warning")

        if (!retryAfterResume) {
            showPageToast(currentBatchId.length > 0
                              ? "已恢复执行暂停，请重新提交批次 " + currentBatchId
                              : "已恢复执行暂停，请重新提交当前批次",
                          false)
            return
        }

        var retryRequest = buildRuleRetryRequest(source)
        if (!retryRequest) {
            showPageToast("已恢复执行暂停，但当前记录不足以自动重试，请手动重新提交", false)
            return
        }

        if (tradeExecutionService.submitBridgeOrder(retryRequest)) {
            root.pushExecutionLog(
                "rule",
                "执行暂停已恢复并重试",
                logRequestDetails(retryRequest),
                "info")
            syncPendingOrders()
            showPageToast(currentBatchId.length > 0
                              ? "已恢复暂停并重新提交批次 " + currentBatchId
                              : "已恢复暂停并重新提交当前批次",
                          false)
            return
        }

        var retryError = tradeExecutionService.lastErrorMessage
            ? String(tradeExecutionService.lastErrorMessage).trim()
            : ""
        showPageToast(
            retryError.length > 0
                ? ("暂停已恢复，但自动重试失败: " + retryError)
                : "暂停已恢复，但自动重试失败，请手动重新提交",
            true)
    }

    function approveExecutionCheckpointForOrder(orderData, retryAfterApproval) {
        var source = orderData || ({})
        var executionScopeId = String(source.executionScopeId || "").trim()
        var batchId = String(source.batchId || source.requiredBatchId || "").trim()

        if (executionScopeId.length === 0 || batchId.length === 0) {
            showPageToast("当前委托缺少执行域或批次信息，无法执行人工确认", true)
            return
        }
        if (!tradeExecutionService || typeof tradeExecutionService.approveExecutionCheckpoint !== "function") {
            showPageToast("交易服务未就绪，暂时无法确认执行检查点", true)
            return
        }

        if (!tradeExecutionService.approveExecutionCheckpoint(executionScopeId, batchId)) {
            var approveError = tradeExecutionService.lastErrorMessage
                ? String(tradeExecutionService.lastErrorMessage).trim()
                : ""
            showPageToast(approveError.length > 0 ? approveError : "人工检查点确认失败", true)
            return
        }

        root.pushExecutionLog(
            "rule",
            "人工检查点已确认",
            resolveInstrumentLabel(source.symbol) + " · 执行域 " + executionScopeId + " · 批次 " + batchId,
            "warning")

        if (!retryAfterApproval) {
            showPageToast("已确认批次 " + batchId + "，请重新提交该批次委托", false)
            return
        }

        var retryRequest = buildRuleRetryRequest(source)
        if (!retryRequest) {
            showPageToast("已确认批次 " + batchId + "，但当前记录不足以自动重试，请手动重新提交", false)
            return
        }

        if (tradeExecutionService.submitBridgeOrder(retryRequest)) {
            root.pushExecutionLog(
                "rule",
                "人工检查点已确认并重试",
                logRequestDetails(retryRequest),
                "info")
            syncPendingOrders()
            showPageToast("已确认批次 " + batchId + " 并重新提交委托", false)
            return
        }

        var retryError = tradeExecutionService.lastErrorMessage
            ? String(tradeExecutionService.lastErrorMessage).trim()
            : ""
        showPageToast(
            retryError.length > 0
                ? ("检查点已确认，但重试失败: " + retryError)
                : "检查点已确认，但自动重试失败，请手动重新提交",
            true)
    }

    function submitFallbackTrade(mode, action, payload) {
        if (mode === "stock") {
            return TradeJs.stockTrade(action, payload.code, payload.shares, payload.priceType, payload.priceInput)
        }
        if (mode === "futures") {
            return TradeJs.futuresTrade(action, payload.code, payload.lots, payload.priceType, payload.priceInput)
        }
        if (mode === "margin_buy") {
            if (action === "repay") {
                return TradeJs.repayTrade(payload.code)
            }
            return TradeJs.marginBuyTrade(payload.code, payload.shares, payload.priceType, payload.priceInput)
        }
        if (mode === "margin_sell") {
            if (action === "returnStock") {
                return TradeJs.returnStockTrade(payload.code)
            }
            return TradeJs.marginSellTrade(payload.code, payload.shares, payload.priceType, payload.priceInput)
        }
        if (mode === "options") {
            var optionAction = action === "optionBuy" ? "buy"
                : action === "optionSell" ? "sell"
                : action === "optionClose" ? "close"
                : action === "optionCoveredClose" ? "coveredClose"
                : "exercise"
            return TradeJs.optionTrade(
                optionAction,
                payload.code,
                payload.underlying,
                payload.lots,
                payload.priceType,
                payload.priceInput,
                payload.optionType,
                payload.expiry)
        }
        return false
    }

    function submitTrade(mode, action, payload) {
        var realBridgeAction = isBridgeManagedTrade(mode, action)
        var quote
        var requestSymbol
        var requestSide
        var requestPositionEffect = ""
        var requestPrice
        var requestQuantity
        var requestOrderType

        if (realBridgeAction && tradeExecutionService) {
            if (mode === "margin_buy" && action === "repay") {
                requestSymbol = serviceSymbolForMode("stock", payload.code)
                if (!requestSymbol) {
                    requestSymbol = "CASH_REPAY"
                }
            } else {
                requestSymbol = serviceSymbolForMode(mode, payload.code)
            }
            if (mode === "stock") {
                requestSide = action === "sell" ? "SELL" : "BUY"
            } else if (mode === "margin_buy") {
                requestSide = action === "repay" || action === "closeLong" ? "SELL" : "BUY"
                requestPositionEffect = action === "repay" || action === "closeLong" ? "CLOSE" : "OPEN"
            } else if (mode === "margin_sell") {
                requestSide = action === "returnStock" || action === "closeShort" ? "BUY" : "SELL"
                requestPositionEffect = action === "returnStock" || action === "closeShort" ? "CLOSE" : "OPEN"
            } else if (mode === "futures") {
                requestSide = action === "long" || action === "closeShort" ? "BUY" : "SELL"
                requestPositionEffect = action === "long" || action === "short" ? "OPEN" : "CLOSE"
            } else if (mode === "options") {
                if (action === "optionExercise") {
                    requestSide = "BUY"
                } else if (action === "optionCoveredClose") {
                    requestSide = "BUY"
                    requestPositionEffect = "CLOSE"
                } else {
                    requestSide = action === "optionBuy" ? "BUY" : "SELL"
                    requestPositionEffect = action === "optionBuy" || action === "optionClose" ? "OPEN" : "CLOSE"
                }
            }
            requestOrderType = payload.priceType === "market" ? "MARKET" : "LIMIT"
            requestPrice = Number(payload.priceInput)
            if (!requestSymbol) {
                showPageToast(invalidSymbolMessageForMode(mode), true)
                return
            }
            if (action === "optionExercise") {
                quote = resolveDisplayQuote(requestSymbol)
                requestPrice = Number(quote && quote.price !== undefined ? quote.price : 0)
            } else {
                if (payload.priceType === "market") {
                    quote = resolveLiveQuote(requestSymbol)
                    requestPrice = Number(quote && quote.price !== undefined ? quote.price : 0)
                } else if (isNaN(requestPrice) || requestPrice <= 0) {
                    quote = resolveDisplayQuote(requestSymbol)
                    requestPrice = Number(quote && quote.price !== undefined ? quote.price : 0)
                }
            }
            requestQuantity = Number((mode === "futures" || mode === "options") ? (payload.lots || 0) : (payload.shares || 0))
            if ((action !== "repay" && action !== "returnStock") && (!requestQuantity || requestQuantity <= 0)) {
                requestQuantity = mode === "futures" || mode === "options" ? 1 : 100
            }
            requestQuantity = Math.floor(requestQuantity)

            if (action !== "repay"
                    && (mode === "stock" || mode === "margin_buy" || mode === "margin_sell")
                    && (requestQuantity < 100 || requestQuantity % 100 !== 0)) {
                showPageToast("股票股数必须是100的整数倍", true)
                return
            }
            if ((mode === "futures" || mode === "options") && requestQuantity < 1) {
                showPageToast("委托手数必须大于0", true)
                return
            }

            if (action === "repay" && (!requestQuantity || requestQuantity <= 0)) {
                showPageToast("请输入有效还款金额基数", true)
                return
            }

            if (action !== "optionExercise" && action !== "returnStock" && requestPrice <= 0) {
                showPageToast(requestOrderType === "MARKET"
                    ? "当前未收到实时行情，市价单不可用，请切换限价后输入价格"
                    : "请输入有效委托价格", true)
                return
            }

            var closeableValidation = validateCloseablePositionForTrade(
                mode,
                action,
                requestSymbol,
                requestQuantity,
                requestSide,
                requestPositionEffect)
            if (!closeableValidation.ok) {
                showPageToast(closeableValidation.message || "当前持仓不足，无法提交卖出委托", true)
                return
            }

            var clientOrderId = "co_" + String(Date.now()) + "_" + String(Math.random()).slice(2, 8)
            root.lastSubmittedRequestId = clientOrderId

            var bridgeRequest = {
                strategyId: root.boundStrategyId,
                symbol: requestSymbol,
                side: requestSide,
                price: requestPrice,
                quantity: requestQuantity,
                orderType: requestOrderType,
                mode: mode,
                action: action,
                clientOrderId: clientOrderId
            }
            if (action === "repay") {
                bridgeRequest.cashAmount = requestPrice * requestQuantity
                bridgeRequest.quantity = 0
            }
            if (requestPositionEffect.length > 0) {
                bridgeRequest.positionEffect = requestPositionEffect
            }
            if (mode === "options") {
                bridgeRequest.underlying = payload.underlying
                bridgeRequest.optionType = payload.optionType
                bridgeRequest.expiry = payload.expiry
            }

            if (tradeExecutionService.submitBridgeOrder(bridgeRequest)) {
                syncPendingOrders()
                showPageToast("已提交" + tradeActionLabel(mode, action) + "委托，等待风控审批", false)
            } else {
                var submitError = tradeExecutionService && tradeExecutionService.lastErrorMessage
                    ? String(tradeExecutionService.lastErrorMessage).trim()
                    : ""
                showPageToast(submitError.length > 0 ? submitError : "委托提交失败", true)
            }
            return
        }

        if (realBridgeAction) {
            if (submitFallbackTrade(mode, action, payload)) {
                syncPendingOrders()
                showPageToast("交易服务未就绪，已回退为本地模拟委托", false)
            } else {
                showPageToast("交易服务未就绪", true)
            }
            return
        }

        if (submitFallbackTrade(mode, action, payload)) {
            syncPendingOrders()
        }
    }

    function cancelPendingOrder(orderId) {
        var normalizedOrderId = String(orderId || "").trim()
        var matchedOrder = null
        var matchedStatus = ""
        var matchedMessage = ""
        var index
        for (index = 0; index < (root.pendingOrders ? root.pendingOrders.length : 0); ++index) {
            var candidate = root.pendingOrders[index] || ({})
            if (String(candidate.cancelOrderId || candidate.id || "").trim() === normalizedOrderId
                    || String(candidate.id || "").trim() === normalizedOrderId
                    || String(candidate.clientOrderId || "").trim() === normalizedOrderId
                    || String(candidate.brokerOrderId || "").trim() === normalizedOrderId) {
                matchedOrder = candidate
                break
            }
        }

        if (matchedOrder && matchedOrder.source !== "simulation") {
            matchedStatus = normalizedOrderStatusValue(matchedOrder.rawStatus ? matchedOrder.rawStatus : matchedOrder.status)
            matchedMessage = String(matchedOrder.message || "").trim()

            if (matchedStatus === "REJECTED") {
                showPageToast(matchedMessage.length > 0
                    ? ("当前委托已拒绝：" + matchedMessage)
                    : "当前委托已拒绝，无需撤单", true)
                return
            }
            if (matchedStatus === "CANCELLED") {
                showPageToast("当前委托已撤单", false)
                return
            }
            if (matchedStatus === "FILLED") {
                showPageToast("当前委托已成交，不能撤单", true)
                return
            }
            if (matchedStatus === "PENDING_CANCEL") {
                showPageToast("当前委托已在撤单中", false)
                return
            }
        }

        if (typeof orderId === "string" && orderId.length > 0) {
            if (tradeExecutionService && tradeExecutionService.cancelManualTestOrder(orderId)) {
                syncPendingOrders()
                showPageToast("撤单请求已提交", false)
                return
            }

            if (matchedOrder && matchedOrder.source !== "simulation") {
                var cancelError = tradeExecutionService && tradeExecutionService.lastErrorMessage
                    ? String(tradeExecutionService.lastErrorMessage).trim()
                    : ""
                showPageToast(cancelError.length > 0 ? cancelError : "撤单失败", true)
                return
            }
        }

        TradeJs.cancelOrder(orderId)
        syncPendingOrders()
    }

    function bindCallbacks() {
        TradeJs.setCallbacks({
            onOrderListChanged: function() {
                root.syncPendingOrders()
            },
            onToast: function(message, isError) {
                root.showPageToast(message, isError)
            }
        })
    }

    function syncMarketState() {
        if (!root.serviceBindingsActive) {
            return
        }
        var liveQuote = resolveLiveQuote(root.activeSymbol)
        var displayQuote = liveQuote || resolveDisplayQuote(root.activeSymbol)
        var displaySnapshot = buildMarketSnapshotFromQuote(displayQuote)
        if (displaySnapshot) {
            root.marketSnapshot = displaySnapshot
        } else {
            root.marketSnapshot = root.emptyMarketSnapshot(root.currentMarketDisplaySymbol())
        }
        root.updateDepthForMode(liveQuote)
    }

    function scheduleMarketStateSync() {
        if (!root.visible || !root.serviceBindingsActive) {
            return
        }
        if (root.marketStateSyncQueued) {
            return
        }
        root.marketStateSyncQueued = true
        Qt.callLater(function() {
            root.marketStateSyncQueued = false
            root.syncMarketState()
        })
    }

    function syncLiveState() {
        if (!root.serviceBindingsActive) {
            return
        }
        root.syncMarketState()
        root.syncPendingOrders()
    }

    function performDeferredPageInitialization() {
        if (root.deferredPageReady || !root.visible) {
            return
        }

        root.deferredPageReady = true

        bindCallbacks()
        root.activateInteractivePanels()
        root.performMarketBootstrap()
    }

    function performMarketBootstrap() {
        if (!root.visible || root.marketBootstrapCompleted) {
            return
        }

        root.marketBootstrapCompleted = true
        root.activateServiceBindings()

        if (marketDataService && !marketDataService.initialized) {
            if (marketDataService.initializeAsync) {
                marketDataService.initializeAsync()
            } else if (marketDataService.initialize) {
                marketDataService.initialize()
            }
        }

        if (marketBridgeReady && marketDataService && marketDataService.activateDefaultWatchlist) {
            marketDataService.activateDefaultWatchlist()
        }

        if (uiLifecycleCoordinator && typeof uiLifecycleCoordinator.activateTradingPage === "function") {
            uiLifecycleCoordinator.activateTradingPage()
        }

        if ((!root.activeSymbol || String(root.activeSymbol).trim().length === 0)
                && marketDataService && marketDataService.primarySymbol) {
            root.activeSymbol = String(marketDataService.primarySymbol || "").trim()
        }

        ensureLiveWatch("market_bootstrap")
        syncMarketState()
        root.performRuntimeBootstrap()
    }

    function performRuntimeBootstrap() {
        if (!root.visible || root.runtimeBootstrapCompleted) {
            return
        }

        root.runtimeBootstrapCompleted = true
        root.strategyStatusSectionRequested = true

        root.refreshRuntimeSnapshot(false)
        syncPendingOrders()
        root.performHoldingsBootstrap()
    }

    function performHoldingsBootstrap() {
        if (!root.visible || root.holdingsBootstrapCompleted) {
            return
        }

        root.holdingsBootstrapCompleted = true
        root.holdingsSectionRequested = true

        root.scheduleInitialSnapshotRefresh("holdings_bootstrap", false)
        syncMarketState()
    }

    onActiveModeChanged: {
        traceActiveSelection("active_mode_changed")
        ensureLiveWatch("active_mode_changed")
        syncMarketState()
    }

    onActiveSymbolChanged: {
        traceActiveSelection("active_symbol_changed")
        ensureLiveWatch("active_symbol_changed")
        syncMarketState()
    }

    Component {
        id: holdingsPanelComponent

        Rectangle {
            width: holdingsPanelLoader.width
            implicitHeight: holdingsPanelPreferredHeight
            radius: 24
            color: "#091321"
            border.color: "#1c314b"
            border.width: 1
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        spacing: 0

                        Text {
                            text: "持仓管理"
                            color: "#f8fafc"
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        radius: 14
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1
                        implicitWidth: 82
                        implicitHeight: 34

                        Text {
                            anchors.centerIn: parent
                            text: "刷新仓位"
                            color: "#dbeafe"
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.requestHoldingsRefresh()
                        }
                    }

                    Rectangle {
                        radius: 14
                        color: "#0d2236"
                        border.color: "#274765"
                        border.width: 1
                        implicitWidth: holdingsCountText.implicitWidth + 20
                        implicitHeight: 34

                        Text {
                            id: holdingsCountText
                            anchors.centerIn: parent
                            text: String(root.displayPositions.length) + " 条仓位"
                            color: "#dbeafe"
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: [
                            {
                                title: "账户总资产",
                                value: formatCurrencyText(root.resolvedTotalAsset),
                                detail: "账户快照 totalAsset"
                            },
                            {
                                title: "持仓市值",
                                value: formatCurrencyText(root.resolvedPositionMarketValue),
                                detail: "股票 / 两融 / 期货 / 期权仓位合计"
                            },
                            {
                                title: "可用资金",
                                value: formatCurrencyText(root.resolvedAvailableCapital),
                                detail: "accountSnapshot.availableCash"
                            },
                            {
                                title: "未实现盈亏",
                                value: formatCurrencyText(accountSnapshot && accountSnapshot.unrealizedPnl !== undefined ? accountSnapshot.unrealizedPnl : 0),
                                detail: "持仓浮动收益"
                            }
                        ]

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 68
                            radius: 16
                            color: "#0d1728"
                            border.color: "#21354c"
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 2

                                Text {
                                    text: modelData.title
                                    color: "#8ba4c7"
                                    font.pixelSize: 11
                                }

                                Text {
                                    text: modelData.value
                                    color: "#f8fafc"
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: modelData.detail
                                    color: "#64748b"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Text {
                        anchors.centerIn: parent
                        visible: groupedDisplayPositions.length === 0
                        text: "收到持仓、账户或成交回流后，这里会显示股票、融资融券、期货、期权仓位列表"
                        color: "#64748b"
                        font.pixelSize: 12
                    }

                    Flickable {
                        id: holdingsViewport
                        anchors.fill: parent
                        visible: groupedDisplayPositions.length > 0
                        clip: true
                        contentWidth: width
                        contentHeight: holdingsGroupsColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: root.holdingsPanelScrollable

                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

                        Column {
                            id: holdingsGroupsColumn
                            width: holdingsViewport.width
                            spacing: 8

                            Repeater {
                                model: root.groupedDisplayPositions

                                ColumnLayout {
                                    width: holdingsGroupsColumn.width
                                    spacing: 6
                                    readonly property var groupData: modelData || ({})

                                    Text {
                                        text: groupData.title || "当前持仓"
                                        color: "#dbeafe"
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                    }

                                    Repeater {
                                        model: groupData.positions || []

                                        Rectangle {
                                            id: positionRow
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 46
                                            radius: 14
                                            color: "#0d1728"
                                            border.color: "#21354c"
                                            border.width: 1

                                            readonly property var positionData: modelData || ({})

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 7
                                                spacing: 5

                                                ColumnLayout {
                                                    Layout.preferredWidth: Math.max(160, root.width * 0.165)
                                                    Layout.alignment: Qt.AlignVCenter
                                                    spacing: 1

                                                    Text {
                                                        text: resolvePositionDisplayName(positionData)
                                                        color: "#f8fafc"
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        elide: Text.ElideRight
                                                    }

                                                    Text {
                                                        text: positionData.typeLabel + " · " + positionData.positionSideLabel
                                                            + " · 数量 " + Number(positionData.quantity || 0) + positionData.unit
                                                            + " · " + positionData.closeableLabel + " " + Number(positionData.closeableQuantity || 0) + positionData.unit
                                                        color: "#8ba4c7"
                                                        font.pixelSize: 9
                                                        elide: Text.ElideRight
                                                    }
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    Layout.alignment: Qt.AlignVCenter
                                                    spacing: 1

                                                    Text {
                                                        text: "市值 " + formatCurrencyText(positionData.currentValue || 0)
                                                        color: "#f8fafc"
                                                        font.pixelSize: 9
                                                        elide: Text.ElideRight
                                                    }

                                                    Text {
                                                        text: String(positionData.detailText || "").length > 0
                                                            ? String(positionData.detailText || "")
                                                            : ((Number(positionData.pnl || 0) >= 0 ? "+" : "-")
                                                                + formatCurrencyText(Math.abs(Number(positionData.pnl || 0)))
                                                                + " · 成本 " + formatCurrencyText(positionData.avgPrice || 0))
                                                        color: Number(positionData.pnl || 0) >= 0 ? "#fb7185" : "#34d399"
                                                        font.pixelSize: 8
                                                        elide: Text.ElideRight
                                                    }
                                                }

                                                Rectangle {
                                                    Layout.preferredWidth: 62
                                                    Layout.preferredHeight: 24
                                                    radius: 10
                                                    color: positionData.canQuickClose ? "#3f1d24" : "#1f2937"
                                                    border.color: positionData.canQuickClose ? "#fda4af" : "#334155"
                                                    border.width: 1
                                                    opacity: positionData.canQuickClose ? 1 : 0.55

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: "一键平仓"
                                                        color: positionData.canQuickClose ? "#ffe4e6" : "#94a3b8"
                                                        font.pixelSize: 9
                                                        font.weight: Font.Medium
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            var sym = String(positionRow.positionData.symbol || "")
                                                            var typ = String(positionRow.positionData.type || "stock")
                                                            var result = tradeExecutionService.quickClosePosition(sym, typ)
                                                            var msg = String(result ? (result.gwError || result.message || "") : "")
                                                            root.showPageToast(msg, !(result && result.accepted))
                                                            if (result && result.accepted) root.syncPendingOrders()
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

                    Text {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 4
                        anchors.bottomMargin: 0
                        visible: groupedDisplayPositions.length > 0 && root.holdingsPanelScrollable
                        text: "向下滚动查看更多持仓"
                        color: "#64748b"
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    Component {
        id: strategyStatusPanelComponent

        Rectangle {
            width: strategyStatusPanelLoader.width
            implicitHeight: 392
            radius: 24
            color: "#091321"
            border.color: "#1c314b"
            border.width: 1
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        spacing: 0

                        Text {
                            text: "策略状态与执行日志"
                            color: "#f8fafc"
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: root.boundStrategyId.length > 0
                                ? (root.boundStrategyName.length > 0 ? root.boundStrategyName : root.boundStrategyId)
                                : "当前未绑定真实交易策略"
                            color: "#8ba4c7"
                            font.pixelSize: 11
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        radius: 12
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1
                        implicitWidth: 88
                        implicitHeight: 32

                        Text {
                            anchors.centerIn: parent
                            text: "刷新状态"
                            color: "#dbeafe"
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.refreshStrategyRuntimeStatus(true)
                        }
                    }

                    Rectangle {
                        radius: 12
                        color: "#0d2236"
                        border.color: "#274765"
                        border.width: 1
                        implicitWidth: 78
                        implicitHeight: 32

                        Text {
                            anchors.centerIn: parent
                            text: "清空日志"
                            color: "#dbeafe"
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.clearExecutionLogs()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: root.tradingStatusCards()

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 72
                            radius: 16
                            color: "#0d1728"
                            border.color: "#21354c"
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 2

                                Text {
                                    text: modelData.title
                                    color: "#8ba4c7"
                                    font.pixelSize: 11
                                }

                                Text {
                                    text: modelData.value
                                    color: "#f8fafc"
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: modelData.detail
                                    color: "#64748b"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 92
                    radius: 18
                    color: "#08111e"
                    border.color: "#182a40"
                    border.width: 1

                    Item {
                        anchors.fill: parent
                        anchors.margins: 12

                        Text {
                            anchors.centerIn: parent
                            visible: root.recentRuleHits.length === 0
                            text: "最近规则命中会汇总在这里，独立于执行日志保留"
                            color: "#64748b"
                            font.pixelSize: 11
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6
                            visible: root.recentRuleHits.length > 0

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: "规则命中历史"
                                    color: "#f8fafc"
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "最近 " + String(Math.min(root.recentRuleHits.length, 3)) + " 条"
                                    color: "#64748b"
                                    font.pixelSize: 10
                                }
                            }

                            Repeater {
                                model: root.recentRuleHits.slice(0, 3)

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 22
                                    radius: 9
                                    color: "#0b1625"
                                    border.color: Qt.rgba(0, 0, 0, 0)

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 8

                                        Rectangle {
                                            Layout.preferredWidth: 36
                                            Layout.preferredHeight: 16
                                            radius: 8
                                            color: Qt.rgba(0, 0, 0, 0)
                                            border.color: root.ruleHitToneColor(modelData)
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: root.ruleHitBadgeText(modelData)
                                                color: root.ruleHitToneColor(modelData)
                                                font.pixelSize: 9
                                                font.weight: Font.Medium
                                            }
                                        }

                                        Rectangle {
                                            visible: root.ruleHitGroupText(modelData).length > 0
                                            radius: 8
                                            color: "#1e293b"
                                            border.color: "#475569"
                                            border.width: 1
                                            Layout.preferredHeight: 16
                                            Layout.preferredWidth: Math.min(groupBadgeLabel.implicitWidth + 14, 128)

                                            Text {
                                                id: groupBadgeLabel
                                                anchors.centerIn: parent
                                                text: root.ruleHitGroupText(modelData)
                                                color: "#cbd5e1"
                                                font.pixelSize: 9
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                                width: Math.max(parent.width - 10, 0)
                                                horizontalAlignment: Text.AlignHCenter
                                            }
                                        }

                                        Text {
                                            text: root.ruleHitTitle(modelData)
                                            color: "#e2e8f0"
                                            font.pixelSize: 10
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            Layout.preferredWidth: 220
                                        }

                                        Text {
                                            text: root.ruleHitDetail(modelData)
                                            color: "#8ba4c7"
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 18
                    color: "#08111e"
                    border.color: "#182a40"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        visible: root.executionLogs.length === 0
                        text: "策略一旦发起委托、收到回报或状态变化，这里会追加真实执行日志"
                        color: "#64748b"
                        font.pixelSize: 12
                    }

                    ListView {
                        anchors.fill: parent
                        anchors.margins: 10
                        visible: root.executionLogs.length > 0
                        clip: true
                        spacing: 6
                        model: root.executionLogs

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 48
                            radius: 14
                            color: "#0b1625"
                            border.color: "#163047"
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                Rectangle {
                                    Layout.preferredWidth: 44
                                    Layout.preferredHeight: 24
                                    radius: 10
                                    color: Qt.rgba(0, 0, 0, 0)
                                    border.color: root.executionLogSeverityColor(modelData.severity)
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: root.executionLogBadgeText(modelData.kind)
                                        color: root.executionLogSeverityColor(modelData.severity)
                                        font.pixelSize: 10
                                        font.weight: Font.Medium
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    Text {
                                        text: modelData.title
                                        color: "#f8fafc"
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: modelData.detail
                                        color: "#8ba4c7"
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                Text {
                                    text: modelData.time
                                    color: "#64748b"
                                    font.pixelSize: 10
                                    Layout.alignment: Qt.AlignTop
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        if (typeof TradeJs.setDepthLevelCount === "function") {
            TradeJs.setDepthLevelCount(root.requestedDepthLevels)
        }
        if (visible) {
            Qt.callLater(root.performDeferredPageInitialization)
        }
    }

    onVisibleChanged: {
        if (!visible) {
            root.deactivateServiceBindings()
            return
        }

        if (!deferredPageReady) {
            Qt.callLater(root.performDeferredPageInitialization)
            return
        }

        Qt.callLater(root.reactivateVisiblePage)
    }

    Component.onDestruction: TradeJs.clearCallbacks()

    Connections {
        target: strategyService
        enabled: root.serviceBindingsActive && !!strategyService

        function onStrategyRuntimeRuleEvaluated(evaluationData) {
            if (!evaluationData) {
                return
            }
            if (root.boundStrategyId.length > 0
                    && String(evaluationData.strategyId || "").trim() !== root.boundStrategyId) {
                return
            }
            root.latestRuntimeRuleEvaluation = evaluationData
        }
    }

    Connections {
        target: marketDataService
        enabled: root.serviceBindingsActive && !!marketDataService

        function onInitializedChanged() {
            if (!root.visible || !root.marketBridgeReady) {
                return
            }
            if (marketDataService.activateDefaultWatchlist) {
                marketDataService.activateDefaultWatchlist()
            }
            if ((!root.activeSymbol || String(root.activeSymbol).trim().length === 0)
                    && marketDataService.primarySymbol) {
                root.activeSymbol = String(marketDataService.primarySymbol || "").trim()
            }
            root.scheduleMarketStateSync()
        }

        function onMarketSnapshotsChanged() {
            root.scheduleMarketStateSync()
        }
    }

    Connections {
        target: positionAccountService
        enabled: root.serviceBindingsActive && !!positionAccountService

        function onRecentOrderStatusesChanged() {
            root.syncPendingOrders()
        }

        function onAccountSnapshotChanged() {
            root.syncPendingOrders()
        }

        function onErrorOccurred(message) {
            root.showPageToast(String(message || "持仓快照刷新失败"), true)
        }
    }

    Connections {
        target: tradeExecutionService
        enabled: root.serviceBindingsActive && !!tradeExecutionService

        function onOrderRequestPublished(orderRequest) {
            root.appendOrderRequestLog(orderRequest)
        }

        function onOrderGenerated(orderInfo) {
            if (!root.matchesBoundStrategyPayload(orderInfo)) {
                return
            }
            root.pushExecutionLog(
                "request",
                "策略产生委托",
                root.logRequestDetails(orderInfo),
                "info")
        }

        function onOrderSubmitResult(result) {
            if (!result) {
                return
            }
            if (result.accepted) {
                return
            }
            root.pushExecutionLog(
                "status",
                "委托提交失败",
                String(result.symbol || "") + " · " + String(result.reason || result.message || "未知原因"),
                "error")
            root.syncPendingOrders()
        }

        function onOrderStatusPublished(orderStatus) {
            root.appendOrderStatusLog(orderStatus)
            root.syncPendingOrders()
        }

        function onOrderStatusChanged(statusEntry) {
            // orderStatusChanged/orderStatusPublished 是成对发射的别名信号
            // Published 负责写执行日志, Changed 只刷新委托列表, 避免日志重复
            root.syncPendingOrders()
        }

        function onTradeFillPublished(tradeFill) {
            root.appendTradeFillLog(tradeFill)
            root.syncPendingOrders()
        }

        function onRecentOrdersChanged() {
            root.syncPendingOrders()
        }
    }

    Connections {
        target: tradingRuntimeStatusService
        enabled: root.serviceBindingsActive && !!tradingRuntimeStatusService

        function onSessionSnapshotsChanged() {
            root.refreshRuntimeSnapshot(true)
        }
    }

    Connections {
        target: tradingConnectionConfigService
        enabled: root.serviceBindingsActive && !!tradingConnectionConfigService

        function onCurrentConfigurationChanged() {
            root.bridgeStatusRevision += 1
            root.refreshRuntimeSnapshot(false)
            root.scheduleInitialSnapshotRefresh("configuration_changed", false)
        }

        function onClientProcessStatusChanged() {
            root.bridgeStatusRevision += 1
            root.refreshRuntimeSnapshot(false)
            root.scheduleInitialSnapshotRefresh("client_status_changed", false)
        }
    }

    Connections {
        target: tradeExecutionService
        enabled: root.serviceBindingsActive && !!tradeExecutionService

        function onInitializedChanged() {
            root.bridgeStatusRevision += 1
        }

        function onLastErrorMessageChanged() {
            root.bridgeStatusRevision += 1
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
    }

    Flickable {
        id: pageViewport
        anchors.fill: parent
        anchors.margins: 28
        clip: true
        contentWidth: width
        contentHeight: pageContent.height
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }

        Item {
            id: pageContent
            width: Math.min(pageViewport.width, root.pageContentMaxWidth)
            x: Math.max(0, (pageViewport.width - width) / 2)
            height: pageColumn.implicitHeight

            ColumnLayout {
                id: pageColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 18

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: holdingsPanelPreferredHeight

                    Loader {
                        id: holdingsPanelLoader
                        width: parent.width
                        height: parent.height
                        asynchronous: true
                        active: root.holdingsSectionRequested
                        sourceComponent: holdingsPanelComponent
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 24
                        color: "#091321"
                        border.color: "#1c314b"
                        border.width: 1
                        visible: !root.holdingsSectionRequested || holdingsPanelLoader.status !== Loader.Ready

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12

                            Text {
                                text: "持仓管理"
                                color: "#f8fafc"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }

                            Repeater {
                                model: 3

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: index === 0 ? 72 : 44
                                    radius: 16
                                    color: index === 0 ? "#0d2236" : "#0d1728"
                                    border.color: "#21354c"
                                    border.width: 1
                                    opacity: 0.78 - index * 0.12
                                }
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 392

                    Loader {
                        id: strategyStatusPanelLoader
                        width: parent.width
                        height: parent.height
                        asynchronous: true
                        active: root.strategyStatusSectionRequested
                        sourceComponent: strategyStatusPanelComponent
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 24
                        color: "#091321"
                        border.color: "#1c314b"
                        border.width: 1
                        visible: !root.strategyStatusSectionRequested || strategyStatusPanelLoader.status !== Loader.Ready

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12

                            Text {
                                text: "策略状态与执行日志"
                                color: "#f8fafc"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }

                            Repeater {
                                model: 4

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: index === 0 ? 72 : 40
                                    radius: 16
                                    color: index === 0 ? "#0d2236" : "#0d1728"
                                    border.color: "#21354c"
                                    border.width: 1
                                    opacity: 0.8 - index * 0.1
                                }
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 0

                Text {
                    text: "交易执行"
                    color: "#f8fafc"
                    font.pixelSize: 30
                    font.weight: Font.Bold
                }
            }

            Item { Layout.fillWidth: true }
        }

                Item {
            id: tradingViewport
            Layout.fillWidth: false
            Layout.preferredWidth: Math.min(pageContent.width, root.tradingSectionMaxWidth)
            Layout.alignment: Qt.AlignHCenter
            implicitHeight: Math.max(formPanelHeight, depthPanelHeight)
            readonly property real formPanelHeight: formPanelLoader.item
                ? formPanelLoader.item.implicitHeight
                : 800
            readonly property real depthPanelHeight: depthPanelLoader.item
                ? depthPanelLoader.item.implicitHeight
                : 600

            Item {
                id: tradingContent
                anchors.fill: parent
                readonly property real formPanelPreferredWidth: Math.min(430, Math.max(360, width * 0.39))
                readonly property real depthPanelPreferredWidth: Math.min(530, Math.max(450, width * 0.45))

                Component {
                    id: formPanelComponent

                    TradingComponents.TradingFormPanel {
                        width: formPanelLoader.width
                        marketSnapshot: root.marketSnapshot
                        depthSnapshot: root.depthSnapshot
                        pendingOrders: root.pendingOrders
                        toastMessage: root.toastMessage
                        toastError: root.toastError
                        availableCapital: root.resolvedAvailableCapital
                        positionAvailabilitySummary: root.currentCloseablePositionInfo.summary
                        positionAvailabilityError: root.currentCloseablePositionInfo.error
                        compactMode: true

                        onModeContextChanged: function(mode, symbol) {
                            if (root.activeMode !== mode) {
                                root.activeMode = mode
                            }

                            var incomingSymbol = String(symbol || "").trim().toUpperCase()
                            if (incomingSymbol.length === 0) {
                                return
                            }

                            var normalizedIncomingSymbol = serviceSymbolForMode(mode, incomingSymbol)
                            var currentSymbol = String(root.activeSymbol || "").trim().toUpperCase()
                            var currentPlainCode = currentSymbol.indexOf(".") >= 0 ? currentSymbol.split(".")[0] : currentSymbol
                            if (/^\d{6}$/.test(incomingSymbol)
                                    && currentSymbol.indexOf(".") >= 0
                                    && currentPlainCode === incomingSymbol) {
                                normalizedIncomingSymbol = currentSymbol
                            }
                            if (normalizedIncomingSymbol.length === 0) {
                                normalizedIncomingSymbol = incomingSymbol
                            }

                            if (root.activeSymbol !== normalizedIncomingSymbol) {
                                root.activeSymbol = normalizedIncomingSymbol
                            }
                        }

                        onExecuteTrade: function(mode, action, payload) {
                            root.submitTrade(mode, action, payload)
                        }

                        onCancelOrderRequested: function(orderId) {
                            root.cancelPendingOrder(orderId)
                        }

                        onApproveCheckpointRequested: function(orderData, retryAfterApproval) {
                            root.approveExecutionCheckpointForOrder(orderData, retryAfterApproval)
                        }

                        onResumeExecutionPauseRequested: function(orderData, retryAfterResume) {
                            root.resumeExecutionPauseForOrder(orderData, retryAfterResume)
                        }
                    }
                }

                Component {
                    id: depthPanelComponent

                    TradingComponents.DepthMarketPanel {
                        width: depthPanelLoader.width
                        marketSnapshot: root.marketSnapshot
                        depthSnapshot: root.depthSnapshot
                        tickRows: root.tickRows
                        activeMode: root.activeMode
                        activeSymbol: root.activeSymbol
                        selectedDepthLevels: root.requestedDepthLevels
                        compactMode: true

                        onDepthLevelsChanged: function(levels) {
                            root.requestedDepthLevels = Math.min(10, Math.max(5, Number(levels || 5)))
                            if (typeof TradeJs.setDepthLevelCount === "function") {
                                TradeJs.setDepthLevelCount(root.requestedDepthLevels)
                            }
                            root.syncMarketState()
                        }
                    }
                }

                RowLayout {
                    id: tradingPanels
                    width: Math.min(parent.width, tradingContent.formPanelPreferredWidth + tradingContent.depthPanelPreferredWidth + spacing)
                    height: parent.height
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    Loader {
                        id: formPanelLoader
                        Layout.preferredWidth: tradingContent.formPanelPreferredWidth
                        Layout.alignment: Qt.AlignTop
                        Layout.fillHeight: true
                        asynchronous: true
                        active: root.formPanelRequested
                        sourceComponent: formPanelComponent
                    }

                    Rectangle {
                        Layout.preferredWidth: tradingContent.formPanelPreferredWidth
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignTop
                        radius: 24
                        color: "#091321"
                        border.color: "#1c314b"
                        border.width: 1
                        visible: formPanelLoader.status !== Loader.Ready

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12

                            Text {
                                text: "交易表单"
                                color: "#f8fafc"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }

                            Repeater {
                                model: 6

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: index === 0 ? 54 : 42
                                    radius: 14
                                    color: index === 0 ? "#0d2236" : "#0d1728"
                                    border.color: "#21354c"
                                    border.width: 1
                                    opacity: 0.82 - index * 0.08
                                }
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }

                    Loader {
                        id: depthPanelLoader
                        Layout.preferredWidth: tradingContent.depthPanelPreferredWidth
                        Layout.minimumWidth: 0
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignTop
                        asynchronous: true
                        active: root.depthPanelRequested
                        sourceComponent: depthPanelComponent
                    }

                    Rectangle {
                        Layout.preferredWidth: tradingContent.depthPanelPreferredWidth
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignTop
                        radius: 24
                        color: "#091321"
                        border.color: "#1c314b"
                        border.width: 1
                        visible: depthPanelLoader.status !== Loader.Ready

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12

                            Text {
                                text: "行情与盘口"
                                color: "#f8fafc"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }

                            Repeater {
                                model: 7

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: index === 0 ? 66 : 38
                                    radius: 14
                                    color: index === 0 ? "#0d2236" : "#0d1728"
                                    border.color: "#21354c"
                                    border.width: 1
                                    opacity: 0.82 - index * 0.07
                                }
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }
        }
            }
        }
    }
}
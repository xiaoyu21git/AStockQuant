import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../utils/StrategyStructureAdapter.js" as StructureAdapter
import "../../utils/StartupGateFormatter.js" as StartupGateFormatter

Item {
    id: root

    property var configService
    property var strategyService: Bridge.StrategyService
    property var marketDataService: Bridge.MarketDataService
    property var marketCalendarService: Bridge.TradingMarketCalendarService
    property var runtimeStatusService: Bridge.TradingRuntimeStatusService
    property var draftConfiguration: ({})
    property var strategyOptions: []
    property string feedbackMessage: ""
    property bool feedbackError: false
    property string selectedBoundStrategyId: ""
    property string selectedBoundStrategyName: ""
    property string boundStrategySymbolsPreview: ""
    property var latestLiveValidationReport: ({ errors: [], warnings: [], checkedAt: "" })
    property var latestStartupGate: ({})

    function defaultRuntimeRuleDefaults() {
        return {
            ruleProfile: {
                maxPositionPercent: 0.2,
                stopLossPercent: 0.05,
                takeProfitPercent: 0.12,
                maxDrawdownLimit: 0.15
            },
            executionPolicy: {
                rebalanceDays: 5
            },
            backtestAssumptions: {
                initialCapital: 1000000,
                commissionRate: 0.0015,
                slippageRate: 0.001
            },
            strategyScopeContext: {
                executionTimeframe: "5min",
                symbol_pool: []
            },
            validation: {
                maxSlippagePercent: 0.005,
                requireBoundStrategy: true,
                requireSymbolPool: true,
                requireClientProcess: true,
                requireTradingSessionOpen: true,
                requireRuntimeReady: true
            }
        }
    }

    function mergedRuntimeRuleDefaults(source) {
        var defaults = defaultRuntimeRuleDefaults()
        var raw = source || ({})
        var ruleProfile = raw.ruleProfile || ({})
        var executionPolicy = raw.executionPolicy || ({})
        var backtestAssumptions = raw.backtestAssumptions || ({})
        var strategyScopeContext = raw.strategyScopeContext || ({})
        var validation = raw.validation || ({})
        return {
            ruleProfile: {
                maxPositionPercent: ruleProfile.maxPositionPercent !== undefined ? Number(ruleProfile.maxPositionPercent) : defaults.ruleProfile.maxPositionPercent,
                stopLossPercent: ruleProfile.stopLossPercent !== undefined ? Number(ruleProfile.stopLossPercent) : defaults.ruleProfile.stopLossPercent,
                takeProfitPercent: ruleProfile.takeProfitPercent !== undefined ? Number(ruleProfile.takeProfitPercent) : defaults.ruleProfile.takeProfitPercent,
                maxDrawdownLimit: ruleProfile.maxDrawdownLimit !== undefined ? Number(ruleProfile.maxDrawdownLimit) : defaults.ruleProfile.maxDrawdownLimit
            },
            executionPolicy: {
                rebalanceDays: executionPolicy.rebalanceDays !== undefined ? Number(executionPolicy.rebalanceDays) : defaults.executionPolicy.rebalanceDays
            },
            backtestAssumptions: {
                initialCapital: backtestAssumptions.initialCapital !== undefined ? Number(backtestAssumptions.initialCapital) : defaults.backtestAssumptions.initialCapital,
                commissionRate: backtestAssumptions.commissionRate !== undefined ? Number(backtestAssumptions.commissionRate) : defaults.backtestAssumptions.commissionRate,
                slippageRate: backtestAssumptions.slippageRate !== undefined ? Number(backtestAssumptions.slippageRate) : defaults.backtestAssumptions.slippageRate
            },
            strategyScopeContext: {
                executionTimeframe: strategyScopeContext.executionTimeframe !== undefined
                    ? String(strategyScopeContext.executionTimeframe).trim()
                    : defaults.strategyScopeContext.executionTimeframe,
                symbol_pool: normalizeSymbolPool(strategyScopeContext.symbol_pool !== undefined
                    ? strategyScopeContext.symbol_pool
                    : (strategyScopeContext.symbolPool !== undefined ? strategyScopeContext.symbolPool : defaults.strategyScopeContext.symbol_pool))
            },
            validation: {
                maxSlippagePercent: validation.maxSlippagePercent !== undefined ? Number(validation.maxSlippagePercent) : defaults.validation.maxSlippagePercent,
                requireBoundStrategy: validation.requireBoundStrategy !== undefined ? !!validation.requireBoundStrategy : defaults.validation.requireBoundStrategy,
                requireSymbolPool: validation.requireSymbolPool !== undefined ? !!validation.requireSymbolPool : defaults.validation.requireSymbolPool,
                requireClientProcess: validation.requireClientProcess !== undefined ? !!validation.requireClientProcess : defaults.validation.requireClientProcess,
                requireTradingSessionOpen: validation.requireTradingSessionOpen !== undefined ? !!validation.requireTradingSessionOpen : defaults.validation.requireTradingSessionOpen,
                requireRuntimeReady: validation.requireRuntimeReady !== undefined ? !!validation.requireRuntimeReady : defaults.validation.requireRuntimeReady
            }
        }
    }

    function percentTextFromRatio(value, decimals) {
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            numericValue = 0
        }
        return (numericValue * 100).toFixed(decimals === undefined ? 2 : decimals)
    }

    function ratioFromPercentText(text) {
        var numericValue = Number(String(text || "").trim())
        if (isNaN(numericValue)) {
            return NaN
        }
        return numericValue / 100.0
    }

    function currentRuntimeSymbols() {
        var fallbackScopeSymbols = normalizeSymbolPool(scopeSymbolPoolField ? scopeSymbolPoolField.text : "")
        if (selectedBoundStrategyId) {
            var boundSymbols = normalizeSymbolPool(boundStrategySymbolsPreview)
            return boundSymbols.length > 0 ? boundSymbols : fallbackScopeSymbols
        }
        var runtimeSymbols = normalizeSymbolPool(symbolsField ? symbolsField.text : "")
        return runtimeSymbols.length > 0 ? runtimeSymbols : fallbackScopeSymbols
    }

    function resolveCurrentRuntimeSessionSnapshot() {
        if (!runtimeStatusService || !runtimeStatusService.sessionSnapshotForStrategy || !configService || !configService.currentConfiguration) {
            return ({})
        }

        var configuration = configService.currentConfiguration || ({})
        var runtimeStrategyId = String(configuration.runtimeStrategyId || configuration.gmStrategyId || "").trim()
        var strategyId = String(selectedBoundStrategyId || configuration.boundStrategyId || "").trim()
        var snapshot = ({})
        if (runtimeStrategyId) {
            snapshot = runtimeStatusService.sessionSnapshotForStrategy(runtimeStrategyId) || ({})
        }
        if ((!snapshot || Object.keys(snapshot).length === 0) && strategyId) {
            snapshot = runtimeStatusService.sessionSnapshotForStrategy(strategyId) || ({})
        }
        return snapshot || ({})
    }

    function syncRuntimeRuleFieldsFromDraft() {
        if (!maxPositionField || !stopLossField || !takeProfitField || !maxDrawdownField
                || !rebalanceDaysField || !initialCapitalField || !commissionRateField
                || !backtestSlippageField || !executionTimeframeField || !scopeSymbolPoolField
                || !slippageLimitField || !requireBoundStrategySwitch
                || !requireSymbolPoolSwitch || !requireClientProcessSwitch
                || !requireTradingSessionSwitch || !requireRuntimeReadySwitch) {
            Qt.callLater(syncRuntimeRuleFieldsFromDraft)
            return
        }

        var runtimeRuleDefaults = mergedRuntimeRuleDefaults(draftConfiguration.runtimeRuleDefaults)
        maxPositionField.text = percentTextFromRatio(runtimeRuleDefaults.ruleProfile.maxPositionPercent, 2)
        stopLossField.text = percentTextFromRatio(runtimeRuleDefaults.ruleProfile.stopLossPercent, 2)
        takeProfitField.text = percentTextFromRatio(runtimeRuleDefaults.ruleProfile.takeProfitPercent, 2)
        maxDrawdownField.text = percentTextFromRatio(runtimeRuleDefaults.ruleProfile.maxDrawdownLimit, 2)
        rebalanceDaysField.text = String(runtimeRuleDefaults.executionPolicy.rebalanceDays)
        initialCapitalField.text = String(runtimeRuleDefaults.backtestAssumptions.initialCapital)
        commissionRateField.text = percentTextFromRatio(runtimeRuleDefaults.backtestAssumptions.commissionRate, 3)
        backtestSlippageField.text = percentTextFromRatio(runtimeRuleDefaults.backtestAssumptions.slippageRate, 3)
        executionTimeframeField.text = runtimeRuleDefaults.strategyScopeContext.executionTimeframe
        scopeSymbolPoolField.text = runtimeRuleDefaults.strategyScopeContext.symbol_pool.join(",")
        slippageLimitField.text = percentTextFromRatio(runtimeRuleDefaults.validation.maxSlippagePercent, 2)
        requireBoundStrategySwitch.checked = runtimeRuleDefaults.validation.requireBoundStrategy
        requireSymbolPoolSwitch.checked = runtimeRuleDefaults.validation.requireSymbolPool
        requireClientProcessSwitch.checked = runtimeRuleDefaults.validation.requireClientProcess
        requireTradingSessionSwitch.checked = runtimeRuleDefaults.validation.requireTradingSessionOpen
        requireRuntimeReadySwitch.checked = runtimeRuleDefaults.validation.requireRuntimeReady
    }

    function buildRuntimeRuleDefaultsPayload() {
        return {
            ruleProfile: {
                maxPositionPercent: ratioFromPercentText(maxPositionField.text),
                stopLossPercent: ratioFromPercentText(stopLossField.text),
                takeProfitPercent: ratioFromPercentText(takeProfitField.text),
                maxDrawdownLimit: ratioFromPercentText(maxDrawdownField.text)
            },
            executionPolicy: {
                rebalanceDays: Number(String(rebalanceDaysField.text || "").trim())
            },
            backtestAssumptions: {
                initialCapital: Number(String(initialCapitalField.text || "").trim()),
                commissionRate: ratioFromPercentText(commissionRateField.text),
                slippageRate: ratioFromPercentText(backtestSlippageField.text)
            },
            strategyScopeContext: {
                executionTimeframe: String(executionTimeframeField.text || "").trim(),
                symbol_pool: normalizeSymbolPool(scopeSymbolPoolField.text)
            },
            validation: {
                maxSlippagePercent: ratioFromPercentText(slippageLimitField.text),
                requireBoundStrategy: requireBoundStrategySwitch.checked,
                requireSymbolPool: requireSymbolPoolSwitch.checked,
                requireClientProcess: requireClientProcessSwitch.checked,
                requireTradingSessionOpen: requireTradingSessionSwitch.checked,
                requireRuntimeReady: requireRuntimeReadySwitch.checked
            }
        }
    }

    function buildLiveValidationReport() {
        var runtimeRuleDefaults = buildRuntimeRuleDefaultsPayload()
        var errors = []
        var warnings = []
        var runtimeSymbols = currentRuntimeSymbols()
        var marketSession = marketCalendarService && marketCalendarService.currentSessionSnapshot
            ? (marketCalendarService.currentSessionSnapshot || ({}))
            : ({})
        var runtimeSession = resolveCurrentRuntimeSessionSnapshot()
        var enabled = !!enabledSwitch.checked
        var autoExecuteRuntimeCandidates = runtimeAutoExecutionSwitch ? !!runtimeAutoExecutionSwitch.checked : false
        var liveUnlockConfirmed = liveUnlockSwitch ? !!liveUnlockSwitch.checked : false

        function validateRatio(name, value, allowZero) {
            if (isNaN(value)) {
                errors.push(name + " 不是有效数字")
                return
            }
            if ((!allowZero && value <= 0) || value < 0 || value >= 1) {
                errors.push(name + " 必须在 0% 到 100% 之间")
            }
        }

        if (isNaN(runtimeRuleDefaults.backtestAssumptions.initialCapital)
                || runtimeRuleDefaults.backtestAssumptions.initialCapital <= 0) {
            errors.push("默认初始资金必须大于 0")
        }
        validateRatio("默认佣金率", runtimeRuleDefaults.backtestAssumptions.commissionRate, true)
        validateRatio("默认回测滑点", runtimeRuleDefaults.backtestAssumptions.slippageRate, true)
        validateRatio("单策略最大仓位", runtimeRuleDefaults.ruleProfile.maxPositionPercent, false)
        validateRatio("止损比例", runtimeRuleDefaults.ruleProfile.stopLossPercent, false)
        validateRatio("止盈比例", runtimeRuleDefaults.ruleProfile.takeProfitPercent, false)
        validateRatio("最大回撤限制", runtimeRuleDefaults.ruleProfile.maxDrawdownLimit, false)
        validateRatio("最大允许滑点", runtimeRuleDefaults.validation.maxSlippagePercent, false)

        if (!isNaN(runtimeRuleDefaults.ruleProfile.takeProfitPercent)
                && !isNaN(runtimeRuleDefaults.ruleProfile.stopLossPercent)
                && runtimeRuleDefaults.ruleProfile.takeProfitPercent <= runtimeRuleDefaults.ruleProfile.stopLossPercent) {
            warnings.push("止盈比例小于等于止损比例，收益风险比偏弱")
        }
        if (!isNaN(runtimeRuleDefaults.ruleProfile.maxDrawdownLimit)
                && !isNaN(runtimeRuleDefaults.ruleProfile.stopLossPercent)
                && runtimeRuleDefaults.ruleProfile.maxDrawdownLimit < runtimeRuleDefaults.ruleProfile.stopLossPercent) {
            warnings.push("最大回撤限制低于单笔止损比例，可能导致刚开仓就触发整体保护")
        }
        if (isNaN(runtimeRuleDefaults.executionPolicy.rebalanceDays)
                || runtimeRuleDefaults.executionPolicy.rebalanceDays <= 0
                || Math.floor(runtimeRuleDefaults.executionPolicy.rebalanceDays) !== runtimeRuleDefaults.executionPolicy.rebalanceDays) {
            errors.push("调仓周期必须是正整数")
        } else if (runtimeRuleDefaults.executionPolicy.rebalanceDays > 60) {
            warnings.push("调仓周期大于 60 天，实盘信号刷新可能过慢")
        }

        if (!String(runtimeRuleDefaults.strategyScopeContext.executionTimeframe || "").trim()) {
            errors.push("默认执行周期不能为空")
        }

        if (enabled) {
            if (!String(tokenField.text || "").trim()) {
                errors.push("已启用连接，但 Token 为空")
            }
            if (!String(activeAccountIdValue() || "").trim()) {
                errors.push("已启用连接，但当前账户 ID 为空")
            }
            if (draftConfiguration && draftConfiguration.readOnly === false && !liveUnlockConfirmed) {
                warnings.push("当前配置仍未显式解锁实盘提交，保存后 broker 提交仍会被门禁阻断")
            }
        }

        if (runtimeRuleDefaults.validation.requireBoundStrategy && !String(selectedBoundStrategyId || "").trim()) {
            errors.push("规则校验要求必须绑定业务策略")
        }
        if (runtimeRuleDefaults.validation.requireSymbolPool && runtimeSymbols.length === 0) {
            errors.push("规则校验要求必须存在运行时标的池")
        }
        if (runtimeRuleDefaults.validation.requireClientProcess && configService && !configService.clientProcessRunning) {
            errors.push("规则校验要求检测到掘金客户端进程")
        }
        if (runtimeRuleDefaults.validation.requireTradingSessionOpen && marketSession.sessionOpen === false) {
            warnings.push("当前不在交易时段，市场环境门禁会阻断候选信号")
        }
        if (runtimeRuleDefaults.validation.requireRuntimeReady) {
            if (!runtimeSession || Object.keys(runtimeSession).length === 0) {
                warnings.push("当前还没有 runtime session 快照，实盘运行态尚不可确认")
            } else if (!runtimeSession.initialized || !runtimeSession.connected || !runtimeSession.isRunning) {
                warnings.push("runtime session 尚未 ready，当前状态为 " + String(runtimeSession.stateLabel || runtimeSession.state || "UNKNOWN"))
            }
        }
        if (autoExecuteRuntimeCandidates && (!draftConfiguration || draftConfiguration.readOnly !== false)) {
            warnings.push("当前交易连接仍处于只读模式，已保存自动执行开关但运行时不会直接下单")
        }
        if (autoExecuteRuntimeCandidates && !String(selectedBoundStrategyId || "").trim()) {
            warnings.push("自动执行依赖绑定业务策略，当前仅保存开关配置")
        }

        var report = {
            checkedAt: Qt.formatDateTime(new Date(), "hh:mm:ss"),
            errors: errors,
            warnings: warnings,
            runtimeRuleDefaults: runtimeRuleDefaults,
            passed: errors.length === 0
        }
        latestLiveValidationReport = report
        return report
    }

    function currentStartupGateRequireClientProcess() {
        if (requireClientProcessSwitch) {
            return !!requireClientProcessSwitch.checked
        }

        return mergedRuntimeRuleDefaults(draftConfiguration.runtimeRuleDefaults).validation.requireClientProcess
    }

    function refreshStartupGateSnapshot() {
        if (!configService || !configService.evaluateStartupGate) {
            latestStartupGate = ({})
            return
        }

        var requireClientProcess = currentStartupGateRequireClientProcess()
        var startupGate = configService.evaluateStartupGate(requireClientProcess) || ({})
        startupGate.requireClientProcess = requireClientProcess
        latestStartupGate = startupGate
    }

    function startupGateBorderColor() {
        if (!latestStartupGate || Object.keys(latestStartupGate).length === 0) {
            return "#334155"
        }

        return latestStartupGate.ready ? "#22C55E" : "#F97316"
    }

    function startupGateBackgroundColor() {
        if (!latestStartupGate || Object.keys(latestStartupGate).length === 0) {
            return "#111827"
        }

        return latestStartupGate.ready ? "#10261D" : "#2A1B14"
    }

    function startupGateSummaryText() {
        return StartupGateFormatter.summaryText(latestStartupGate, "尚未获取已保存配置的 StartupGate 状态")
    }

    function startupGateMetaText() {
        return StartupGateFormatter.metaText(latestStartupGate)
    }

    function startupGateCheckSummaryText() {
        return StartupGateFormatter.checkSummaryText(latestStartupGate)
    }

    function normalizeSymbolPool(source) {
        return StructureAdapter.normalizeSymbolPool(source)
    }

    function loadBoundStrategySymbols(strategyId) {
        var resolvedStrategyId = String(strategyId || "").trim()
        if (!resolvedStrategyId || !strategyService || !strategyService.getStrategyById) {
            return []
        }

        try {
            if (strategyService.initialize) {
                strategyService.initialize()
            }
            var strategy = strategyService.getStrategyById(resolvedStrategyId) || ({})
            var linkedSymbols = StructureAdapter.resolveLinkedStockPoolSymbols(strategy)
            if (linkedSymbols.length > 0) {
                return linkedSymbols
            }

            var persistedSymbols = StructureAdapter.resolvePersistedStrategySymbolPool(strategy)
            if (persistedSymbols.length > 0) {
                return persistedSymbols
            }

            return StructureAdapter.resolveSymbolPool(strategy)
        } catch (error) {
            return []
        }
    }

    function resolveRuntimeSymbolsFallback() {
        var source = ""
        if (symbolsField && String(symbolsField.text || "").trim()) {
            source = symbolsField.text
        } else if (draftConfiguration && String(draftConfiguration.symbols || "").trim()) {
            source = draftConfiguration.symbols
        } else if (configService && configService.currentConfiguration) {
            source = configService.currentConfiguration.symbols || ""
        }

        return normalizeSymbolPool(source)
    }

    function ensureBoundStrategySymbolPoolFromRuntime() {
        if (!selectedBoundStrategyId) {
            return ({ synced: false, usedFallback: false, symbolCount: 0 })
        }

        var existingSymbols = loadBoundStrategySymbols(selectedBoundStrategyId)
        if (existingSymbols.length > 0) {
            boundStrategySymbolsPreview = existingSymbols.join(",")
            if (symbolsField) {
                symbolsField.text = boundStrategySymbolsPreview
            }
            return ({ synced: true, usedFallback: false, symbolCount: existingSymbols.length })
        }

        var fallbackSymbols = resolveRuntimeSymbolsFallback()
        if (fallbackSymbols.length === 0) {
            return ({ synced: false, usedFallback: false, symbolCount: 0 })
        }

        boundStrategySymbolsPreview = fallbackSymbols.join(",")
        if (symbolsField) {
            symbolsField.text = boundStrategySymbolsPreview
        }

        return ({ synced: false, usedFallback: true, previewOnly: true, symbolCount: fallbackSymbols.length })
    }

    function syncSymbolsPreviewFromBoundStrategy() {
        var symbols = loadBoundStrategySymbols(selectedBoundStrategyId)
        boundStrategySymbolsPreview = symbols.join(",")
        if (selectedBoundStrategyId && symbolsField && symbols.length > 0) {
            symbolsField.text = boundStrategySymbolsPreview
        }
    }

    function accountProfileValue() {
        return accountProfileBox && accountProfileBox.selectedProfileValue
            ? accountProfileBox.selectedProfileValue
            : "live"
    }

    function activeAccountIdValue() {
        var liveAccountId = liveAccountIdField ? liveAccountIdField.text.trim() : (draftConfiguration.liveAccountId || draftConfiguration.accountId || "")
        var simAccountId = simAccountIdField ? simAccountIdField.text.trim() : (draftConfiguration.simAccountId || "")
        return accountProfileValue() === "simulation"
            ? simAccountId
            : liveAccountId
    }

    signal showMessageRequested(string message)

    function syncBoundStrategySelection() {
        var targetId = draftConfiguration.boundStrategyId || ""
        selectedBoundStrategyId = targetId
        selectedBoundStrategyName = draftConfiguration.boundStrategyName || ""

        if (!strategyOptions || strategyOptions.length === 0 || !boundStrategyBox) {
            return
        }

        var matchedIndex = 0
        for (var index = 0; index < strategyOptions.length; ++index) {
            if ((strategyOptions[index].value || "") === targetId) {
                matchedIndex = index
                break
            }
        }

        boundStrategyBox.currentIndex = matchedIndex
        selectedBoundStrategyId = strategyOptions[matchedIndex] ? (strategyOptions[matchedIndex].value || "") : ""
        selectedBoundStrategyName = strategyOptions[matchedIndex] ? (strategyOptions[matchedIndex].name || "") : ""
        syncSymbolsPreviewFromBoundStrategy()
    }

    function reloadStrategyOptions() {
        var options = [
            {
                label: "未绑定业务策略",
                value: "",
                name: ""
            }
        ]

        try {
            var strategyServiceReady = false
            if (strategyService) {
                if (typeof strategyService.isInitialized === "function") {
                    strategyServiceReady = !!strategyService.isInitialized()
                } else if (strategyService.initialized !== undefined) {
                    strategyServiceReady = !!strategyService.initialized
                }
            }

            if (!strategyServiceReady) {
                if (strategyService && typeof strategyService.initializeAsync === "function") {
                    strategyService.initializeAsync()
                } else if (strategyService && typeof strategyService.initialize === "function") {
                    Qt.callLater(function() {
                        if (strategyService && typeof strategyService.initialize === "function") {
                            strategyService.initialize()
                        }
                    })
                }

                strategyOptions = options
                syncBoundStrategySelection()
                return
            }

            if (strategyService && strategyService.getAllStrategies) {
                var strategies = strategyService.getAllStrategies() || []
                for (var index = 0; index < strategies.length; ++index) {
                    var rawStrategy = strategies[index] || ({})
                    var strategyId = rawStrategy.strategy_id || rawStrategy.strategyId || rawStrategy.id || ""
                    if (!strategyId) {
                        continue
                    }

                    var strategyName = rawStrategy.strategy_name || rawStrategy.strategyName || rawStrategy.name || strategyId
                    options.push({
                        label: strategyName + " (" + strategyId + ")",
                        value: strategyId,
                        name: strategyName
                    })
                }
            }
        } catch (error) {
            feedbackError = true
            feedbackMessage = "加载业务策略列表失败: " + error
        }

        strategyOptions = options
        syncBoundStrategySelection()
    }

    function initializeBridgeService(service) {
        if (!service) {
            return
        }

        if (typeof service.initializeAsync === "function") {
            service.initializeAsync()
            return
        }

        if (typeof service.initialize === "function") {
            Qt.callLater(function() {
                if (service && typeof service.initialize === "function") {
                    service.initialize()
                }
            })
        }
    }

    function syncFieldsFromDraft() {
        if (!enabledSwitch || !tokenField || !liveAccountIdField || !simAccountIdField
                || !gmStrategyIdField || !symbolsField || !serverUrlField || !accountProfileBox
                || !runtimeAutoExecutionSwitch || !liveUnlockSwitch) {
            Qt.callLater(syncFieldsFromDraft)
            return
        }

        enabledSwitch.checked = !!draftConfiguration.enabled
        runtimeAutoExecutionSwitch.checked = !!draftConfiguration.autoExecuteRuntimeCandidates
        liveUnlockSwitch.checked = !!draftConfiguration.liveUnlockConfirmed
        tokenField.text = draftConfiguration.token || ""
        liveAccountIdField.text = draftConfiguration.liveAccountId || (!draftConfiguration.simtradeOnly ? (draftConfiguration.accountId || "") : "")
        simAccountIdField.text = draftConfiguration.simAccountId || (draftConfiguration.simtradeOnly ? (draftConfiguration.accountId || "") : "")
        gmStrategyIdField.text = draftConfiguration.gmStrategyId || draftConfiguration.runtimeStrategyId || draftConfiguration.strategyId || ""
        symbolsField.text = draftConfiguration.symbols || ""
        serverUrlField.text = draftConfiguration.serverUrl || ""
        syncBoundStrategySelection()

        var savedAccountProfile = draftConfiguration.accountProfile || (draftConfiguration.simtradeOnly ? "simulation" : "live")
        accountProfileBox.currentIndex = savedAccountProfile === "simulation" ? 1 : 0
        accountProfileBox.selectedProfileValue = accountProfileBox.model[accountProfileBox.currentIndex].value
        syncRuntimeRuleFieldsFromDraft()

    }

    function reloadConfiguration() {
        if (!configService || !configService.loadConfiguration) {
            return
        }

        draftConfiguration = configService.loadConfiguration()
        syncFieldsFromDraft()
        if (configService.refreshClientProcessStatus) {
            configService.refreshClientProcessStatus()
        }
        refreshStartupGateSnapshot()
        feedbackError = false
        feedbackMessage = "已从配置文件读取交易连接参数"
    }

    function buildConfigurationPayload() {
        return {
            enabled: enabledSwitch.checked,
            autoExecuteRuntimeCandidates: runtimeAutoExecutionSwitch.checked,
            liveUnlockConfirmed: liveUnlockSwitch.checked,
            liveUnlockAcknowledgedAt: liveUnlockSwitch.checked
                                     ? (draftConfiguration.liveUnlockAcknowledgedAt || "")
                                     : "",
            token: tokenField.text,
            accountProfile: accountProfileValue(),
            liveAccountId: liveAccountIdField.text.trim(),
            simAccountId: simAccountIdField.text.trim(),
            accountId: activeAccountIdValue(),
            simtradeOnly: false,
            readOnly: draftConfiguration.readOnly !== undefined ? !!draftConfiguration.readOnly : true,
            boundStrategyId: selectedBoundStrategyId,
            boundStrategyName: selectedBoundStrategyName,
            gmStrategyId: gmStrategyIdField.text.trim(),
            runtimeStrategyId: gmStrategyIdField.text.trim(),
            strategyId: gmStrategyIdField.text.trim(),
            mode: "1",
            serverUrl: serverUrlField.text,
            symbols: symbolsField.text,
            runtimeRuleDefaults: buildRuntimeRuleDefaultsPayload(),
            updatedAt: draftConfiguration.updatedAt || "",
            provider: "jujin"
        }

    }

    function saveConfiguration(options) {
        if (!configService || !configService.saveConfiguration) {
            return false
        }

        var saveOptions = options || ({})
        if (selectedBoundStrategyId && !saveOptions.skipStrategySymbolPoolSync) {
            var syncResult = ensureBoundStrategySymbolPoolFromRuntime()
            if (syncResult.failed) {
                feedbackError = true
                feedbackMessage = syncResult.message || "策略标的池回写失败"
                if (!saveOptions.suppressToast) {
                    showMessageRequested(feedbackMessage)
                }
                return false
            }
        }

        var validationReport = buildLiveValidationReport()
        if (!validationReport.passed) {
            feedbackError = true
            feedbackMessage = "实盘规则校验未通过（" + validationReport.errors.length + " 项错误）：\n- " + validationReport.errors.join("\n- ")
            if (!saveOptions.suppressToast) {
                showMessageRequested("实盘规则校验未通过，请先修正参数")
            }
            return false
        }

        var payload = buildConfigurationPayload()

        if (configService.saveConfiguration(payload)) {
            draftConfiguration = configService.currentConfiguration
            syncFieldsFromDraft()
            refreshStartupGateSnapshot()
            feedbackError = false
            var baseMessage = saveOptions.successMessage || "掘金连接配置已保存，下一次连接将读取新 token"
            feedbackMessage = validationReport.warnings.length > 0
                ? (baseMessage + "\n实盘提示：\n- " + validationReport.warnings.join("\n- "))
                : baseMessage
            if (!saveOptions.suppressToast) {
                showMessageRequested(feedbackMessage)
            }
            return true
        }

        return false
    }

    function persistBoundStrategySelection() {
        var syncResult = ensureBoundStrategySymbolPoolFromRuntime()
        if (syncResult.failed) {
            feedbackError = true
            feedbackMessage = syncResult.message || "策略标的池回写失败"
            showMessageRequested(feedbackMessage)
            return
        }

        saveConfiguration({
            successMessage: selectedBoundStrategyId
                ? (syncResult.usedFallback && syncResult.synced
                    ? ("已同步交易绑定到策略“" + (selectedBoundStrategyName || selectedBoundStrategyId) + "”，并立即刷新运行时订阅标的")
                    : (syncResult.previewOnly
                    ? ("已同步交易绑定到策略“" + (selectedBoundStrategyName || selectedBoundStrategyId) + "”，当前仅预览运行时订阅，不再回写回测股票池")
                    : (boundStrategySymbolsPreview
                    ? ("已同步交易绑定到策略“" + (selectedBoundStrategyName || selectedBoundStrategyId) + "”，并立即刷新运行时订阅标的")
                    : ("已同步交易绑定到策略“" + (selectedBoundStrategyName || selectedBoundStrategyId) + "”，当前沿用既有订阅标的"))))
                : "已取消业务策略绑定，当前可手动维护运行时行情订阅列表"
        })
    }

    Component.onCompleted: {
        initializeBridgeService(configService)
        initializeBridgeService(marketDataService)
        initializeBridgeService(marketCalendarService)
        initializeBridgeService(runtimeStatusService)
        Qt.callLater(reloadConfiguration)
        Qt.callLater(reloadStrategyOptions)
    }

    Connections {
        target: strategyService
        ignoreUnknownSignals: true

        function onStrategiesLoaded() {
            reloadStrategyOptions()
        }

        function onInitializedChanged() {
            reloadStrategyOptions()
        }
    }

    Connections {
        target: configService
        function onConfigurationSaved(configuration) {
            draftConfiguration = configuration
            syncFieldsFromDraft()
            refreshStartupGateSnapshot()
        }
        function onClientProcessStatusChanged() {
            refreshStartupGateSnapshot()
        }
        function onErrorOccurred(message) {
            feedbackError = true
            feedbackMessage = message
            showMessageRequested(message)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
    }

    ScrollView {
        anchors.fill: parent
        clip: true

        ColumnLayout {
            width: Math.max(root.width - 48, 920)
            spacing: 20
            anchors.margins: 24

            Rectangle {
                Layout.fillWidth: true
                radius: 24
                color: "#111C34"
                border.color: "#22314F"
                border.width: 1
                implicitHeight: headerColumn.implicitHeight + 32

                ColumnLayout {
                    id: headerColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    Text {
                        text: "系统设置 / 掘金连接"
                        font.pixelSize: 28
                        font.bold: true
                        color: "#F8FAFC"
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "这里主要维护掘金 token、实盘/仿真账户、固定掘金策略 ID 和订阅兜底。现在可直接在策略卡片点击启动实盘，系统会自动完成交易绑定；设置页只保留为辅助配置入口。当前交易连接固定使用实时交易会话，实盘/仿真只通过账户 ID 切换。"
                        font.pixelSize: 14
                        color: "#94A3B8"
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: feedbackError ? "#3F1D24" : "#132338"
                        border.color: feedbackError ? "#F87171" : "#38BDF8"
                        border.width: 1
                        implicitHeight: feedbackText.implicitHeight + 20

                        Text {
                            id: feedbackText
                            anchors.fill: parent
                            anchors.margins: 10
                            text: feedbackMessage.length > 0
                                  ? feedbackMessage
                                  : "配置文件位置: " + (configService && configService.configFilePath ? configService.configFilePath : "")
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: feedbackError ? "#FECACA" : "#BAE6FD"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: configService && configService.marketConnectorCompiled ? "#10261D" : "#2A1B14"
                        border.color: configService && configService.marketConnectorCompiled ? "#22C55E" : "#FB923C"
                        border.width: 1
                        implicitHeight: buildStatusText.implicitHeight + 20

                        Text {
                            id: buildStatusText
                            anchors.fill: parent
                            anchors.margins: 10
                            text: configService && configService.marketConnectorBuildStatus
                                  ? configService.marketConnectorBuildStatus
                                  : "尚未获取连接器构建状态"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: configService && configService.marketConnectorCompiled ? "#DCFCE7" : "#FFEDD5"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: configService && configService.clientProcessRunning ? "#10261D" : "#2A1B14"
                        border.color: configService && configService.clientProcessRunning ? "#22C55E" : "#FB923C"
                        border.width: 1
                        implicitHeight: processStatusRow.implicitHeight + 20

                        RowLayout {
                            id: processStatusRow
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 12

                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: configService && configService.clientProcessRunning ? "#22C55E" : "#F97316"
                            }

                            Text {
                                Layout.fillWidth: true
                                text: configService && configService.clientProcessStatus
                                      ? configService.clientProcessStatus
                                      : "尚未检查客户端进程"
                                wrapMode: Text.WordWrap
                                font.pixelSize: 13
                                color: configService && configService.clientProcessRunning ? "#DCFCE7" : "#FFEDD5"
                            }

                            Button {
                                text: "重新检测"
                                onClicked: {
                                    if (configService && configService.refreshClientProcessStatus) {
                                        configService.refreshClientProcessStatus()
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "当前监控进程名: " + (configService && configService.clientProcessNames
                              ? configService.clientProcessNames.join(", ")
                              : "")
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 24
                color: "#111827"
                border.color: "#1F2A44"
                border.width: 1
                implicitHeight: formColumn.implicitHeight + 32

                ColumnLayout {
                    id: formColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 18

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text {
                            text: "连接控制"
                            font.pixelSize: 20
                            font.bold: true
                            color: "#F8FAFC"
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: !(configService && configService.marketConnectorCompiled)
                                  ? "当前构建不可启用"
                                  : (enabledSwitch.checked ? "已启用连接" : "未启用连接")
                            font.pixelSize: 13
                            color: !(configService && configService.marketConnectorCompiled)
                                   ? "#FB923C"
                                   : (enabledSwitch.checked ? "#34D399" : "#94A3B8")
                        }

                        Switch {
                            id: enabledSwitch
                            checked: !!draftConfiguration.enabled
                            enabled: !!(configService && configService.marketConnectorCompiled)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 16
                        color: runtimeAutoExecutionSwitch.checked ? "#10261D" : "#0F172A"
                        border.color: runtimeAutoExecutionSwitch.checked ? "#22C55E" : "#334155"
                        border.width: 1
                        implicitHeight: runtimeAutoExecutionColumn.implicitHeight + 20

                        ColumnLayout {
                            id: runtimeAutoExecutionColumn
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    Layout.fillWidth: true
                                    text: "运行时候选信号自动执行"
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: "#F8FAFC"
                                }

                                Switch {
                                    id: runtimeAutoExecutionSwitch
                                    checked: !!draftConfiguration.autoExecuteRuntimeCandidates
                                    enabled: !!(configService && configService.marketConnectorCompiled)
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: runtimeAutoExecutionSwitch.checked
                                      ? "候选信号会直接走正式委托链路：生成委托请求、进入风控审批、再由执行服务提交。建议仅在已完成账户、策略和运行时校验后开启。"
                                      : "关闭时，运行时规则仍会实时评估并展示 candidate_ready，但不会自动提交委托。"
                                font.pixelSize: 12
                                color: runtimeAutoExecutionSwitch.checked ? "#DCFCE7" : "#94A3B8"
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 16
                        color: liveUnlockSwitch.checked ? "#2A130F" : "#0F172A"
                        border.color: liveUnlockSwitch.checked ? "#F97316" : "#334155"
                        border.width: 1
                        implicitHeight: liveUnlockColumn.implicitHeight + 20

                        ColumnLayout {
                            id: liveUnlockColumn
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    Layout.fillWidth: true
                                    text: "显式解锁实盘提交"
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: "#F8FAFC"
                                }

                                Switch {
                                    id: liveUnlockSwitch
                                    checked: !!draftConfiguration.liveUnlockConfirmed
                                    enabled: !!(configService && configService.marketConnectorCompiled)
                                             && enabledSwitch.checked
                                             && draftConfiguration.readOnly === false
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: draftConfiguration.readOnly === false
                                      ? (liveUnlockSwitch.checked
                                            ? "当前允许进入 broker 提交门禁。若切回只读、禁用连接或更换账户/绑定策略，解锁状态会自动清空。"
                                            : "未开启时，即使连接已启用且 readOnly=false，执行服务也会阻断真实 broker 提交。")
                                      : "当前仍处于只读链路，显式解锁开关仅在 readOnly=false 时生效。"
                                font.pixelSize: 12
                                color: liveUnlockSwitch.checked ? "#FED7AA" : "#94A3B8"
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width > 1200 ? 2 : 1
                        columnSpacing: 16
                        rowSpacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "掘金 Token"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: tokenField
                                Layout.fillWidth: true
                                text: draftConfiguration.token || ""
                                echoMode: TextInput.Password
                                placeholderText: "请输入最新的掘金 token"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: tokenField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "账户环境"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            ComboBox {
                                id: accountProfileBox
                                Layout.fillWidth: true
                                model: [
                                    { label: "实盘账户", value: "live" },
                                    { label: "仿真账户", value: "simulation" }
                                ]
                                textRole: "label"
                                property string selectedProfileValue: model[currentIndex] ? model[currentIndex].value : "live"
                                onActivated: selectedProfileValue = model[currentIndex].value
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "实盘账户 ID"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: liveAccountIdField
                                Layout.fillWidth: true
                                text: draftConfiguration.liveAccountId || ""
                                placeholderText: "填写掘金实盘账户 ID"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: liveAccountIdField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "仿真账户 ID"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: simAccountIdField
                                Layout.fillWidth: true
                                text: draftConfiguration.simAccountId || ""
                                placeholderText: "填写掘金仿真账户 ID"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: simAccountIdField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "绑定业务策略"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            ComboBox {
                                id: boundStrategyBox
                                Layout.fillWidth: true
                                model: strategyOptions
                                textRole: "label"
                                onActivated: {
                                    var selected = strategyOptions[currentIndex] || ({})
                                    selectedBoundStrategyId = selected.value || ""
                                    selectedBoundStrategyName = selected.name || ""
                                    syncSymbolsPreviewFromBoundStrategy()
                                    persistBoundStrategySelection()
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "掘金策略 ID"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: gmStrategyIdField
                                Layout.fillWidth: true
                                text: draftConfiguration.gmStrategyId || draftConfiguration.runtimeStrategyId || draftConfiguration.strategyId || ""
                                placeholderText: "填写掘金客户端里那条固定策略的真实 ID"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: gmStrategyIdField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "这是一条固定的掘金外部策略 ID。系统里的业务策略仍通过“绑定业务策略”单独区分。"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "当前生效账户 ID: " + activeAccountIdValue()
                                font.pixelSize: 12
                                color: "#BAE6FD"
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "交易会话模式"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                Layout.fillWidth: true
                                text: "固定为 1 - 实时交易会话"
                                readOnly: true
                                color: "#CBD5E1"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: "#334155"
                                    border.width: 1
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "仿真账户不等于 SDK 回测模式。当前链路固定使用 mode=1，避免因 mode=2 或 simtrade_only 导致运行时无法进入可交易状态。"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "行情订阅标的"
                            font.pixelSize: 14
                            color: "#E2E8F0"
                        }

                        TextField {
                            id: symbolsField
                            Layout.fillWidth: true
                            visible: !selectedBoundStrategyId
                            text: draftConfiguration.symbols || ""
                            placeholderText: "用逗号分隔，例如 600000.SH,000001.SZ"
                            color: "#F8FAFC"
                            placeholderTextColor: "#64748B"
                            selectByMouse: true
                            background: Rectangle {
                                radius: 12
                                color: "#0F172A"
                                border.color: symbolsField.activeFocus ? "#38BDF8" : "#334155"
                                border.width: 1
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: !!selectedBoundStrategyId
                            radius: 12
                            color: "#0F172A"
                            border.color: "#475569"
                            border.width: 1
                            implicitHeight: syncPreviewColumn.implicitHeight + 24

                            ColumnLayout {
                                id: syncPreviewColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    text: boundStrategySymbolsPreview
                                        ? ("策略股票池: " + normalizeSymbolPool(boundStrategySymbolsPreview).length + " 个标的")
                                        : "当前策略暂无股票池"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: "#E2E8F0"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: selectedBoundStrategyId
                                                     ? ("实时订阅: "
                                                         + (marketDataService ? marketDataService.runtimeSubscriptionCount : 0)
                                                         + "，上限: "
                                                         + ((marketDataService && marketDataService.runtimeSubscriptionLimit > 0)
                                                             ? marketDataService.runtimeSubscriptionLimit
                                                             : 0)
                                                         + " 个")
                                        : "未绑定策略时不显示策略股票池同步状态"
                                    font.pixelSize: 13
                                    color: "#FCD34D"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: boundStrategySymbolsPreview
                                        ? ("上面第一行是绑定策略自带的股票池数量；第二行分别显示当前已订阅数量和订阅上限。两者不是同一个概念。")
                                        : "切换绑定后会立即保存；若当前策略没有标的池，系统将保留现有运行时订阅列表。"
                                    font.pixelSize: 13
                                    color: "#BAE6FD"
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: selectedBoundStrategyId
                                ? (boundStrategySymbolsPreview
                                    ? ("已绑定策略“" + (selectedBoundStrategyName || selectedBoundStrategyId) + "”。页面显示的策略股票池来自策略配置；真实订阅数以上面的实时订阅统计为准。")
                                    : ("已绑定策略“" + (selectedBoundStrategyName || selectedBoundStrategyId) + "”，该策略当前没有标的池，系统将继续沿用现有订阅列表"))
                                : "未绑定业务策略时，可在这里手动维护运行时行情订阅列表；也可以直接回到策略卡片点击启动实盘，系统会自动绑定当前策略。"
                            font.pixelSize: 12
                            color: selectedBoundStrategyId ? "#BAE6FD" : "#94A3B8"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "服务器地址"
                            font.pixelSize: 14
                            color: "#E2E8F0"
                        }

                        TextField {
                            id: serverUrlField
                            Layout.fillWidth: true
                            text: draftConfiguration.serverUrl || ""
                            placeholderText: "可选，自定义服务地址时填写"
                            color: "#F8FAFC"
                            placeholderTextColor: "#64748B"
                            selectByMouse: true
                            background: Rectangle {
                                radius: 12
                                color: "#0F172A"
                                border.color: serverUrlField.activeFocus ? "#38BDF8" : "#334155"
                                border.width: 1
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 16
                        color: "#0F172A"
                        border.color: "#334155"
                        border.width: 1
                        implicitHeight: ruleDefaultsColumn.implicitHeight + 24

                        ColumnLayout {
                            id: ruleDefaultsColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            Text {
                                text: "默认运行规则参数"
                                font.pixelSize: 18
                                font.bold: true
                                color: "#F8FAFC"
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "这里配置未显式写入策略时的运行规则默认值，并在保存前执行一次实盘前置校验。百分比字段按 % 输入。"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: root.width > 1200 ? 3 : 2
                                columnSpacing: 16
                                rowSpacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "最大仓位 (%)"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: maxPositionField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "20"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: maxPositionField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "止损比例 (%)"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: stopLossField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "5"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: stopLossField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "止盈比例 (%)"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: takeProfitField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "12"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: takeProfitField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "最大回撤限制 (%)"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: maxDrawdownField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "15"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: maxDrawdownField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "调仓周期 (天)"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: rebalanceDaysField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "5"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: rebalanceDaysField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "最大允许滑点 (%)"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: slippageLimitField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "0.50"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: slippageLimitField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }
                            }

                            Text {
                                text: "默认回测假设快照"
                                font.pixelSize: 15
                                font.bold: true
                                color: "#E2E8F0"
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: root.width > 1200 ? 3 : 2
                                columnSpacing: 16
                                rowSpacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "默认初始资金"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: initialCapitalField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "1000000"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: initialCapitalField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "默认佣金率 (%)"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: commissionRateField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "0.15"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: commissionRateField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "默认回测滑点 (%)"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: backtestSlippageField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "0.10"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: backtestSlippageField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }
                            }

                            Text {
                                text: "默认作用域上下文"
                                font.pixelSize: 15
                                font.bold: true
                                color: "#E2E8F0"
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: root.width > 1200 ? 3 : 2
                                columnSpacing: 16
                                rowSpacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "默认执行周期"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: executionTimeframeField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "5min"
                                        placeholderTextColor: "#64748B"
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: executionTimeframeField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.columnSpan: root.width > 1200 ? 2 : 1
                                    spacing: 6
                                    Text { text: "默认兜底标的池"; font.pixelSize: 13; color: "#E2E8F0" }
                                    TextField {
                                        id: scopeSymbolPoolField
                                        Layout.fillWidth: true
                                        color: "#F8FAFC"
                                        placeholderText: "当绑定策略自身没有标的池时，用这个兜底，例如 600000.SH,000001.SZ"
                                        placeholderTextColor: "#64748B"
                                        selectByMouse: true
                                        background: Rectangle { radius: 12; color: "#111827"; border.color: scopeSymbolPoolField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                                    }
                                }
                            }

                            Text {
                                text: "实盘校验开关"
                                font.pixelSize: 15
                                font.bold: true
                                color: "#E2E8F0"
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: root.width > 1200 ? 3 : 2
                                columnSpacing: 16
                                rowSpacing: 8

                                Switch { id: requireBoundStrategySwitch; text: "必须绑定业务策略" }
                                Switch { id: requireSymbolPoolSwitch; text: "必须存在标的池" }
                                Switch {
                                    id: requireClientProcessSwitch
                                    text: "必须检测到客户端"
                                    onCheckedChanged: refreshStartupGateSnapshot()
                                }
                                Switch { id: requireTradingSessionSwitch; text: "检查当前交易时段" }
                                Switch { id: requireRuntimeReadySwitch; text: "检查 runtime ready" }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 12
                                color: startupGateBackgroundColor()
                                border.color: startupGateBorderColor()
                                border.width: 1
                                implicitHeight: startupGateColumn.implicitHeight + 20

                                ColumnLayout {
                                    id: startupGateColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 6

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Text {
                                            text: latestStartupGate && Object.keys(latestStartupGate).length > 0
                                                ? (latestStartupGate.ready ? "已保存 StartupGate: Pass" : "已保存 StartupGate: Block")
                                                : "已保存 StartupGate"
                                            font.pixelSize: 13
                                            font.bold: true
                                            color: "#F8FAFC"
                                        }

                                        Item { Layout.fillWidth: true }

                                        Text {
                                            text: latestStartupGate && latestStartupGate.requireClientProcess
                                                ? "包含客户端进程门禁"
                                                : "不含客户端进程门禁"
                                            font.pixelSize: 12
                                            color: "#CBD5E1"
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: startupGateSummaryText()
                                        font.pixelSize: 12
                                        color: latestStartupGate && latestStartupGate.ready ? "#DCFCE7" : "#FFEDD5"
                                    }

                                    Text {
                                        visible: latestStartupGate && Object.keys(latestStartupGate).length > 0
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: startupGateMetaText()
                                        font.pixelSize: 12
                                        color: "#94A3B8"
                                    }

                                    Text {
                                        visible: !!(latestStartupGate
                                            && latestStartupGate.checks
                                            && latestStartupGate.checks.length > 0)
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: startupGateCheckSummaryText()
                                        font.pixelSize: 12
                                        color: "#CBD5E1"
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 12
                                color: "#111827"
                                border.color: latestLiveValidationReport.errors && latestLiveValidationReport.errors.length > 0 ? "#F87171" : "#334155"
                                border.width: 1
                                implicitHeight: validationColumn.implicitHeight + 20

                                ColumnLayout {
                                    id: validationColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 6

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: latestLiveValidationReport.checkedAt
                                                ? ("最近一次实盘校验: " + latestLiveValidationReport.checkedAt)
                                                : "尚未执行实盘校验"
                                            font.pixelSize: 13
                                            font.bold: true
                                            color: "#F8FAFC"
                                        }
                                        Item { Layout.fillWidth: true }
                                        Button {
                                            text: "执行实盘校验"
                                            onClicked: {
                                                var report = buildLiveValidationReport()
                                                feedbackError = report.errors.length > 0
                                                feedbackMessage = report.errors.length > 0
                                                    ? ("实盘规则校验未通过：\n- " + report.errors.join("\n- "))
                                                    : (report.warnings.length > 0
                                                        ? ("实盘规则校验通过，但存在提示：\n- " + report.warnings.join("\n- "))
                                                        : "实盘规则校验通过，当前参数可保存")
                                            }
                                        }
                                    }

                                    Text {
                                        visible: latestLiveValidationReport.errors && latestLiveValidationReport.errors.length > 0
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: "错误：\n- " + latestLiveValidationReport.errors.join("\n- ")
                                        font.pixelSize: 12
                                        color: "#FCA5A5"
                                    }

                                    Text {
                                        visible: latestLiveValidationReport.warnings && latestLiveValidationReport.warnings.length > 0
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: "提示：\n- " + latestLiveValidationReport.warnings.join("\n- ")
                                        font.pixelSize: 12
                                        color: "#FCD34D"
                                    }

                                    Text {
                                        visible: latestLiveValidationReport.checkedAt && (!latestLiveValidationReport.errors || latestLiveValidationReport.errors.length === 0) && (!latestLiveValidationReport.warnings || latestLiveValidationReport.warnings.length === 0)
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: "未发现参数错误或运行环境提示。"
                                        font.pixelSize: 12
                                        color: "#86EFAC"
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Button {
                            text: "重新读取"
                            onClicked: reloadConfiguration()
                        }

                        Button {
                            text: "恢复默认"
                            onClicked: {
                                draftConfiguration = configService && configService.defaultConfiguration
                                                     ? configService.defaultConfiguration()
                                                     : ({})
                                syncFieldsFromDraft()
                                feedbackError = false
                                feedbackMessage = "已恢复为默认连接配置，保存后生效"
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "保存配置"
                            highlighted: true
                            enabled: tokenField.text.trim().length > 0
                            onClicked: saveConfiguration()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "说明：实盘绑定现在区分业务策略 ID 与掘金固定策略 ID。界面里选择的是系统内业务策略，掘金策略 ID 需要手工填写为客户端那条固定策略；未检测到掘金客户端时，即使 token 正确也无法使用对应接口。"
                        font.pixelSize: 13
                        color: "#94A3B8"
                    }
                }
            }

        }
    }
}
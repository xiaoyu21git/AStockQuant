import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0
import AStock.Bridge 1.0 as Bridge
import "../../components/Trading" as TradingComponents
import "../../utils/TradingPageAdapter.js" as TradeJs

Item {
    id: root

    property var marketData: []
    property var marketSnapshot: ({
        price: 12.58,
        priceStr: "12.58",
        changePercent: "+0.00%",
        isUp: true,
        futuresPrice: 3650,
        futuresPriceStr: "3650"
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
    property string activeSymbol: "000001"
    property int requestedDepthLevels: 5
    readonly property int tradingPanelMinWidth: 980
    readonly property var marketDataService: Bridge.MarketDataService
    readonly property var positionAccountService: Bridge.PositionAccountService
    readonly property var tradeExecutionService: Bridge.TradeExecutionService

    readonly property bool marketBridgeReady: marketDataService && marketDataService.initialized
    readonly property bool liveServicesReady: marketBridgeReady && positionAccountService && tradeExecutionService
        && positionAccountService.initialized && tradeExecutionService.initialized
    readonly property bool stockModeUsesLiveBridge: activeMode === "stock" || activeMode === "margin_buy" || activeMode === "margin_sell"
    readonly property bool fallbackModeAllowed: activeMode === "futures" || activeMode === "options"
    readonly property bool usingLiveMarketData: stockModeUsesLiveBridge && !!(marketSnapshot && marketSnapshot.live)
    readonly property bool usingCachedSnapshot: stockModeUsesLiveBridge && !usingLiveMarketData && !!(marketSnapshot && marketSnapshot.snapshotOnly)
    readonly property real resolvedAvailableCapital: positionAccountService && positionAccountService.accountSnapshot && positionAccountService.accountSnapshot.availableCash !== undefined
        ? Number(positionAccountService.accountSnapshot.availableCash)
        : 800000

    function cloneList(list) {
        return list ? list.slice(0) : []
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
            futuresPrice: root.marketSnapshot && root.marketSnapshot.futuresPrice !== undefined ? root.marketSnapshot.futuresPrice : 3650,
            futuresPriceStr: root.marketSnapshot && root.marketSnapshot.futuresPriceStr ? root.marketSnapshot.futuresPriceStr : "3650",
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
        if (text.length === 0 || mode === "futures" || mode === "options") {
            return ""
        }
        return normalizeEquitySymbolInput(text)
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

    function translateOrderStatus(status) {
        var text = String(status || "").trim().toUpperCase()
        if (text === "REQUESTED") {
            return "已请求"
        }
        if (text === "SUBMITTED") {
            return "已报"
        }
        if (text === "PENDING") {
            return "待处理"
        }
        if (text === "PARTIALLY_FILLED") {
            return "部分成交"
        }
        if (text === "PARTIAL_FILLED") {
            return "部分成交"
        }
        if (text === "FILLED") {
            return "已成"
        }
        if (text === "CANCELLED") {
            return "已撤"
        }
        if (text === "REJECTED") {
            return "已拒"
        }
        return text || "待处理"
    }

    function orderUnit(order) {
        return order && (order.type === "futures" || order.type === "options") ? "手" : "股"
    }

    function normalizedOrderStatusValue(status) {
        var text = String(status || "").trim().toUpperCase()
        if (text === "PARTIALLY_FILLED") {
            return "PARTIAL_FILLED"
        }
        return text
    }

    function orderStatusDigest(orderItem) {
        var quantity = Number(orderItem && orderItem.qty !== undefined ? orderItem.qty : 0)
        var filledQuantity = Number(orderItem && orderItem.filledQty !== undefined ? orderItem.filledQty : 0)
        if (isNaN(quantity)) {
            quantity = 0
        }
        if (isNaN(filledQuantity)) {
            filledQuantity = 0
        }
        return normalizedOrderStatusValue(orderItem && orderItem.rawStatus ? orderItem.rawStatus : (orderItem ? orderItem.status : ""))
            + "|" + quantity + "|" + filledQuantity
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
        var action = String(orderItem && orderItem.action ? orderItem.action : "委托")
        var quantity = Number(orderItem && orderItem.qty !== undefined ? orderItem.qty : 0)
        var filledQuantity = Number(orderItem && orderItem.filledQty !== undefined ? orderItem.filledQty : 0)
        var unit = root.orderUnit(orderItem || {})
        var label = resolveOrderLabel(orderItem)

        if (isNaN(quantity) || quantity < 0) {
            quantity = 0
        }
        if (isNaN(filledQuantity) || filledQuantity < 0) {
            filledQuantity = 0
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
        if (status === "REJECTED") {
            return {
                message: label + " " + action + "委托被拒绝",
                isError: true
            }
        }
        return null
    }

    function buildMarketSnapshotFromQuote(quote) {
        var priceValue = Number(quote && quote.price !== undefined ? quote.price : 0)
        if (!priceValue || isNaN(priceValue) || priceValue <= 0) {
            return null
        }

        var changeValue = Number(quote && quote.change !== undefined ? quote.change : 0)
        var preCloseValue = Number(quote && quote.preClose !== undefined ? quote.preClose : (quote && quote.pre_close !== undefined ? quote.pre_close : 0))
        var symbolValue = quote && quote.symbol ? String(quote.symbol) : ""
        var sourceValue = String(quote && quote.source ? quote.source : "").trim().toLowerCase()
        var updatedAtValue = String(quote && quote.updatedAt ? quote.updatedAt : "").trim()
        var isRealtime = sourceValue !== "seed" && sourceValue !== "watchlist" && sourceValue !== "daily_snapshot"
            && updatedAtValue.length > 0 && updatedAtValue !== "--"
        if ((!preCloseValue || isNaN(preCloseValue) || preCloseValue <= 0) && priceValue > 0) {
            preCloseValue = priceValue / (1 + changeValue / 100.0)
        }
        if (!preCloseValue || isNaN(preCloseValue) || preCloseValue <= 0) {
            preCloseValue = priceValue
        }
        var limitRatio = boardLimitRatio(symbolValue)
        var upperLimitPrice = roundPriceByMode(preCloseValue * (1 + limitRatio), "stock")
        var lowerLimitPrice = roundPriceByMode(preCloseValue * (1 - limitRatio), "stock")
        return {
            price: priceValue,
            priceStr: priceValue.toFixed(2),
            changePercent: signedPercentText(changeValue),
            isUp: changeValue >= 0,
            preClose: preCloseValue,
            upperLimit: upperLimitPrice,
            lowerLimit: lowerLimitPrice,
            live: isRealtime,
            snapshotOnly: !isRealtime,
            source: sourceValue,
            futuresPrice: root.marketSnapshot && root.marketSnapshot.futuresPrice !== undefined ? root.marketSnapshot.futuresPrice : 3650,
            futuresPriceStr: root.marketSnapshot && root.marketSnapshot.futuresPriceStr ? root.marketSnapshot.futuresPriceStr : "3650",
            symbol: symbolValue,
            name: quote.name || "",
            updatedAt: updatedAtValue
        }
    }

    function tradeActionLabel(mode, action) {
        if (mode === "stock") {
            return action === "buy" ? "买入" : "卖出"
        }
        if (mode === "margin_buy") {
            return action === "repay" ? "现金还款" : "融资买入"
        }
        if (mode === "margin_sell") {
            return action === "returnStock" ? "现券还券" : "融券卖出"
        }
        return action
    }

    function hasRealtimeQuote(quote) {
        var source = String(quote && quote.source ? quote.source : "").trim().toLowerCase()
        var updatedAt = String(quote && quote.updatedAt ? quote.updatedAt : "").trim()
        if (!quote || !quote.symbol) {
            return false
        }
        return source !== "seed" && source !== "watchlist" && source !== "daily_snapshot" && updatedAt.length > 0 && updatedAt !== "--"
    }

    function hasDisplayQuote(quote) {
        var priceValue = Number(quote && quote.price !== undefined ? quote.price : 0)
        return !!(quote && quote.symbol && !isNaN(priceValue) && priceValue > 0)
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

    function mapServiceOrders(sourceList) {
        var result = []
        var seenIds = {}
        var index

        for (index = 0; index < (sourceList ? sourceList.length : 0); ++index) {
            var raw = sourceList[index] || ({})
            var rawId = String(raw.orderId || raw.id || "").trim()
            if (!rawId || seenIds[rawId]) {
                continue
            }
            seenIds[rawId] = true
            result.push({
                id: rawId,
                rawOrderId: rawId,
                source: "live",
                symbol: String(raw.symbol || "--"),
                type: "stock",
                action: translateOrderSide(raw.side || raw.action),
                qty: Number(raw.quantity !== undefined ? raw.quantity : (raw.qty !== undefined ? raw.qty : (raw.totalQuantity !== undefined ? raw.totalQuantity : 0))),
                price: Number(raw.price || 0),
                time: String(raw.updatedAt || raw.createdAt || raw.time || "--"),
                status: translateOrderStatus(raw.status || raw.rawStatus),
                rawStatus: String(raw.status || raw.rawStatus || ""),
                filledQty: Number(raw.filledQuantity !== undefined ? raw.filledQuantity : (raw.filledQty !== undefined ? raw.filledQty : (raw.filled !== undefined ? raw.filled : 0)))
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
                source: "simulation",
                symbol: raw.symbol || "--",
                type: raw.type || "stock",
                action: raw.action || "待处理",
                qty: Number(raw.qty || 0),
                price: Number(raw.price || 0),
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
        toastTimer.restart()
    }

    function syncPendingOrders() {
        var mergedOrders = []
        var seenIds = {}
        var nextOrderStatusDigestById = {}
        var toastPayloads = []
        var lists = [
            mapServiceOrders(positionAccountService ? (positionAccountService.recentOrderStatuses || []) : []),
            mapServiceOrders(tradeExecutionService ? (tradeExecutionService.recentOrders || []) : []),
            mapSimulatedOrders(TradeJs.getOrders())
        ]
        var listIndex
        var itemIndex

        for (listIndex = 0; listIndex < lists.length; ++listIndex) {
            for (itemIndex = 0; itemIndex < lists[listIndex].length; ++itemIndex) {
                var orderItem = lists[listIndex][itemIndex]
                var orderKey = String(orderItem.id)
                if (seenIds[orderKey]) {
                    continue
                }
                seenIds[orderKey] = true
                nextOrderStatusDigestById[orderKey] = orderStatusDigest(orderItem)
                if (root.orderStatusDigestReady && root.orderStatusDigestById[orderKey] !== nextOrderStatusDigestById[orderKey]) {
                    var toastPayload = root.buildOrderStatusToast(orderItem)
                    if (toastPayload) {
                        toastPayloads.push(toastPayload)
                    }
                }
                mergedOrders.push(orderItem)
            }
        }

        root.pendingOrders = mergedOrders
        root.orderStatusDigestById = nextOrderStatusDigestById
        root.orderStatusDigestReady = true

        if (toastPayloads.length > 0) {
            var latestToast = toastPayloads[toastPayloads.length - 1]
            root.showPageToast(latestToast.message, latestToast.isError)
        }
    }

    function ensureLiveWatch() {
        var watchSymbol = serviceSymbolForMode(root.activeMode, root.activeSymbol)
        if (!watchSymbol || !marketBridgeReady) {
            return
        }
        marketDataService.ensureWatchSymbol(watchSymbol)
    }

    function buildOptionDepth(priceValue, levelCount) {
        var bids = []
        var asks = []
        var basePrice = Number(priceValue || 0.0850)
        var step = 0.0005
        var resolvedLevels = Math.min(10, Math.max(5, Math.floor(Number(levelCount || root.requestedDepthLevels || 5))))
        var index

        for (index = 1; index <= resolvedLevels; ++index) {
            bids.push({
                price: Number((basePrice - index * step).toFixed(4)),
                volume: Math.floor(Math.random() * 300 + 50)
            })
            asks.push({
                price: Number((basePrice + index * step).toFixed(4)),
                volume: Math.floor(Math.random() * 300 + 50)
            })
        }

        return {
            bids: bids,
            asks: asks,
            totalBid: sumVolumes(bids),
            totalAsk: sumVolumes(asks),
            levelCount: resolvedLevels,
            live: false,
            source: "options.mock"
        }
    }

    function updateDepthForMode(liveQuote) {
        if (root.activeMode === "futures") {
            root.depthSnapshot = cloneDepth(TradeJs.generateDepth(Number(root.marketSnapshot.futuresPrice || 3650), false, root.requestedDepthLevels))
            root.tickRows = cloneList(TradeJs.getTickHistory())
            return
        }

        if (root.activeMode === "options") {
            root.depthSnapshot = buildOptionDepth(0.0850, root.requestedDepthLevels)
            root.tickRows = cloneList(TradeJs.getTickHistory())
            return
        }

        if (root.stockModeUsesLiveBridge && root.marketBridgeReady) {
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
        var realBridgeAction = mode === "stock" || mode === "margin_buy" || mode === "margin_sell"
        var quote
        var requestSymbol
        var requestSide
        var requestPrice
        var requestQuantity
        var requestOrderType

        if (realBridgeAction && tradeExecutionService && action !== "repay" && action !== "returnStock") {
            requestSymbol = serviceSymbolForMode(mode, payload.code)
            requestSide = (action === "buy" || action === "marginBuy") ? "BUY" : "SELL"
            requestOrderType = payload.priceType === "market" ? "MARKET" : "LIMIT"
            requestPrice = Number(payload.priceInput)
            if (!requestSymbol) {
                showPageToast("请输入有效6位股票代码", true)
                return
            }
            if (payload.priceType === "market") {
                quote = resolveLiveQuote(requestSymbol)
                requestPrice = Number(quote && quote.price !== undefined ? quote.price : 0)
            } else if (isNaN(requestPrice) || requestPrice <= 0) {
                quote = resolveDisplayQuote(requestSymbol)
                requestPrice = Number(quote && quote.price !== undefined ? quote.price : 0)
            }
            requestQuantity = Number(payload.shares || 0)
            if (!requestQuantity || requestQuantity <= 0) {
                requestQuantity = 100
            }
            requestQuantity = Math.floor(requestQuantity)

            if (requestQuantity < 100 || requestQuantity % 100 !== 0) {
                showPageToast("股票股数必须是100的整数倍", true)
                return
            }

            if (requestPrice <= 0) {
                showPageToast(requestOrderType === "MARKET"
                    ? "当前未收到实时行情，市价单不可用，请切换限价后输入价格"
                    : "请输入有效委托价格", true)
                return
            }

            if (tradeExecutionService.submitManualTestOrder(requestSymbol, requestSide, requestPrice, requestQuantity, requestOrderType, "manual_test", "Manual Test")) {
                syncLiveState()
                showPageToast("已提交" + tradeActionLabel(mode, action) + "委托", false)
            } else {
                showPageToast("委托提交失败", true)
            }
            return
        }

        if (realBridgeAction) {
            showPageToast("交易服务未就绪", true)
            return
        }

        if (submitFallbackTrade(mode, action, payload)) {
            syncLiveState()
        }
    }

    function cancelPendingOrder(orderId) {
        if (typeof orderId === "string" && orderId.length > 0) {
            if (tradeExecutionService && tradeExecutionService.cancelManualTestOrder(orderId)) {
                syncPendingOrders()
                showPageToast("撤单请求已提交", false)
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
            onMarketDataChanged: function(data) {
                if (root.fallbackModeAllowed) {
                    root.marketSnapshot = data
                }
                root.updateDepthForMode()
            },
            onDepthChanged: function(data) {
                if (!root.fallbackModeAllowed) {
                    return
                }
                root.stockDepthSnapshot = root.cloneDepth(data)
                root.updateDepthForMode()
            },
            onTickChanged: function(list) {
                if (!root.fallbackModeAllowed) {
                    return
                }
                root.tickRows = root.cloneList(list)
            },
            onToast: function(message, isError) {
                root.showPageToast(message, isError)
            }
        })
    }

    function syncLiveState() {
        var liveQuote = resolveLiveQuote(root.activeSymbol)
        var displayQuote = liveQuote || resolveDisplayQuote(root.activeSymbol)
        var displaySnapshot = buildMarketSnapshotFromQuote(displayQuote)
        if (displaySnapshot) {
            root.marketSnapshot = displaySnapshot
        } else if (root.stockModeUsesLiveBridge) {
            root.marketSnapshot = root.emptyMarketSnapshot(serviceSymbolForMode(root.activeMode, root.activeSymbol))
        }
        root.updateDepthForMode(liveQuote)
        root.syncPendingOrders()
    }

    onActiveModeChanged: {
        ensureLiveWatch()
        syncLiveState()
    }

    onActiveSymbolChanged: {
        ensureLiveWatch()
        syncLiveState()
    }

    Component.onCompleted: {
        if (typeof TradeJs.setDepthLevelCount === "function") {
            TradeJs.setDepthLevelCount(root.requestedDepthLevels)
        }
        if (marketDataService && typeof marketDataService.initialize === "function") {
            marketDataService.initialize()
        }
        if (positionAccountService && typeof positionAccountService.initialize === "function") {
            positionAccountService.initialize()
        }
        if (tradeExecutionService && typeof tradeExecutionService.initialize === "function") {
            tradeExecutionService.initialize()
        }
        bindCallbacks()
        if (root.fallbackModeAllowed) {
            TradeJs.initTicks(8)
            root.tickRows = cloneList(TradeJs.getTickHistory())
            TradeJs.updateMarketPrice()
        }
        ensureLiveWatch()
        syncLiveState()
    }

    Component.onDestruction: TradeJs.clearCallbacks()

    Connections {
        target: marketDataService
        enabled: !!marketDataService

        function onMarketSnapshotsChanged() {
            root.syncLiveState()
        }

        function onMarketEventReceived() {
            root.syncLiveState()
        }
    }

    Connections {
        target: positionAccountService
        enabled: !!positionAccountService

        function onRecentOrderStatusesChanged() {
            root.syncPendingOrders()
        }

        function onAccountSnapshotChanged() {
            root.syncPendingOrders()
        }
    }

    Connections {
        target: tradeExecutionService
        enabled: !!tradeExecutionService

        function onRecentOrdersChanged() {
            root.syncPendingOrders()
        }
    }

    Timer {
        id: marketTimer
        interval: 1800
        running: root.visible && root.fallbackModeAllowed
        repeat: true
        onTriggered: TradeJs.updateMarketPrice()
    }

    Timer {
        id: toastTimer
        interval: 2200
        repeat: false
        onTriggered: {
            root.toastMessage = ""
            root.toastError = false
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#040913"
    }

    Rectangle {
        width: 360
        height: 360
        radius: 180
        x: -90
        y: -120
        color: "#1d4e8930"
    }

    Rectangle {
        width: 420
        height: 420
        radius: 210
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -120
        anchors.topMargin: -140
        color: "#0f766e26"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 22

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 4

                Text {
                    text: "交易执行"
                    color: "#f8fafc"
                    font.pixelSize: 30
                    font.weight: Font.Bold
                }

                Text {
                    text: root.usingLiveMarketData
                        ? "股票与融资方向使用真实掘金桥接，盘口与L2仅在真实行情可用时显示"
                        : (root.usingCachedSnapshot
                            ? "当前显示最近缓存快照，盘口与L2仅在真实行情可用时显示"
                            : "当前未收到股票实时行情，盘口与L2不会使用模拟数据")
                    color: "#8ba4c7"
                    font.pixelSize: 14
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                radius: 18
                color: "#0d2236"
                border.color: "#274765"
                border.width: 1
                implicitWidth: 148
                implicitHeight: 46

                Row {
                    anchors.centerIn: parent
                    spacing: 8

                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.usingLiveMarketData ? "#14b8a6" : (root.usingCachedSnapshot ? "#38bdf8" : (marketTimer.running ? "#f59e0b" : "#64748b"))
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.usingLiveMarketData ? "Trading / Live" : (root.usingCachedSnapshot ? "Trading / Snapshot" : "Trading / No-L2")
                        color: "#dbeafe"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        ScrollView {
            id: tradingViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            implicitWidth: tradingPanelMinWidth
            readonly property real viewportAvailableWidth: Math.max(0, root.width - 56)
            readonly property real targetContentWidth: Math.max(viewportAvailableWidth, tradingPanelMinWidth)
            readonly property real targetContentHeight: Math.max(formPanel.implicitHeight, depthPanel.implicitHeight)
            contentWidth: targetContentWidth
            contentHeight: targetContentHeight
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn
            ScrollBar.vertical.interactive: true
            ScrollBar.horizontal.policy: ScrollBar.AsNeeded

            Item {
                id: tradingContent
                width: tradingViewport.targetContentWidth
                height: tradingViewport.targetContentHeight

                RowLayout {
                    id: tradingPanels
                    x: 0
                    y: 0
                    width: parent.width
                    height: parent.height
                    spacing: 14

                    TradingComponents.TradingFormPanel {
                        id: formPanel
                        Layout.preferredWidth: Math.max(380, Math.min(462, tradingContent.width * 0.38))
                        Layout.alignment: Qt.AlignTop
                        marketSnapshot: root.marketSnapshot
                        depthSnapshot: root.depthSnapshot
                        pendingOrders: root.pendingOrders
                        toastMessage: root.toastMessage
                        toastError: root.toastError
                        availableCapital: root.resolvedAvailableCapital
                        compactMode: true

                        onModeContextChanged: function(mode, symbol) {
                            root.activeMode = mode
                            root.activeSymbol = symbol
                        }

                        onExecuteTrade: function(mode, action, payload) {
                            root.submitTrade(mode, action, payload)
                        }

                        onCancelOrderRequested: function(orderId) {
                            root.cancelPendingOrder(orderId)
                        }
                    }

                    TradingComponents.DepthMarketPanel {
                        id: depthPanel
                        Layout.fillWidth: true
                        Layout.minimumWidth: 400
                        Layout.alignment: Qt.AlignTop
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
                            root.syncLiveState()
                        }
                    }
                }
            }
        }
    }
}
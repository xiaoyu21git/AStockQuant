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
    readonly property bool quoteBridgeMode: activeMode === "stock" || activeMode === "margin_buy" || activeMode === "margin_sell"
        || activeMode === "futures" || activeMode === "options"
    readonly property bool usingLiveMarketData: quoteBridgeMode && !!(marketSnapshot && marketSnapshot.live)
    readonly property bool usingCachedSnapshot: quoteBridgeMode && !usingLiveMarketData && !!(marketSnapshot && marketSnapshot.snapshotOnly)
    readonly property string marketDisplayState: usingLiveMarketData ? "live" : (usingCachedSnapshot ? "cached" : "empty")
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
        if (text === "PENDING_CANCEL") {
            return "撤单中"
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
        var rawText = String(status || "").trim()
        if (rawText === "已请求") {
            return "REQUESTED"
        }
        if (rawText === "已报") {
            return "SUBMITTED"
        }
        if (rawText === "待处理") {
            return "PENDING"
        }
        if (rawText === "部分成交") {
            return "PARTIAL_FILLED"
        }
        if (rawText === "已成") {
            return "FILLED"
        }
        if (rawText === "撤单中") {
            return "PENDING_CANCEL"
        }
        if (rawText === "已撤") {
            return "CANCELLED"
        }
        if (rawText === "已拒") {
            return "REJECTED"
        }

        var text = rawText.toUpperCase()
        if (text === "PARTIALLY_FILLED") {
            return "PARTIAL_FILLED"
        }
        return text
    }

    function orderStatusPhaseValue(orderItem) {
        var status = normalizedOrderStatusValue(orderItem && orderItem.rawStatus ? orderItem.rawStatus : (orderItem ? orderItem.status : ""))
        if (status === "REQUESTED") {
            return 0
        }
        if (status === "PENDING" || status === "SUBMITTED") {
            return 1
        }
        if (status === "PARTIAL_FILLED" || status === "PENDING_CANCEL") {
            return 2
        }
        if (status === "CANCELLED" || status === "REJECTED") {
            return 3
        }
        if (status === "FILLED") {
            return 4
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
        var action = String(orderItem && orderItem.action ? orderItem.action : "委托")
        var quantity = Number(orderItem && orderItem.qty !== undefined ? orderItem.qty : 0)
        var filledQuantity = Number(orderItem && orderItem.filledQty !== undefined ? orderItem.filledQty : 0)
        var message = String(orderItem && orderItem.message ? orderItem.message : "").trim()
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
        if (status === "PENDING_CANCEL") {
            return {
                message: label + " " + action + "撤单请求已提交",
                isError: false
            }
        }
        if (status === "REJECTED") {
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
        if (!priceValue || isNaN(priceValue) || priceValue <= 0) {
            return null
        }
        if (!isRealtime && !isSnapshotQuote) {
            return null
        }

        var changeValue = Number(quote && quote.change !== undefined ? quote.change : 0)
        var preCloseValue = Number(quote && quote.preClose !== undefined ? quote.preClose : (quote && quote.pre_close !== undefined ? quote.pre_close : 0))
        var symbolValue = quote && quote.symbol ? String(quote.symbol) : ""
        var sourceValue = String(quote && quote.source ? quote.source : "").trim().toLowerCase()
        var updatedAtValue = String(quote && quote.updatedAt ? quote.updatedAt : "").trim()
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
            return action === "repay" ? "现金还款" : "融资买入"
        }
        if (mode === "margin_sell") {
            return action === "returnStock" ? "现券还券" : "融券卖出"
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
        if (action === "repay" || action === "returnStock") {
            return false
        }
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
        var priceValue = Number(quote && quote.price !== undefined ? quote.price : 0)
        if (!(quote && quote.symbol && !isNaN(priceValue) && priceValue > 0)) {
            return false
        }
        return hasRealtimeQuote(quote) || hasSnapshotQuote(quote)
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
                qty: Number(raw.quantity !== undefined ? raw.quantity : (raw.qty !== undefined ? raw.qty : (raw.totalQuantity !== undefined ? raw.totalQuantity : 0))),
                price: Number(raw.price || 0),
                message: String(raw.message || "").trim(),
                time: String(raw.updatedAt || raw.createdAt || raw.time || "--"),
                statusOrigin: statusOrigin,
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
                clientOrderId: raw.id,
                brokerOrderId: "",
                cancelOrderId: raw.id,
                source: "simulation",
                symbol: raw.symbol || "--",
                type: raw.type || "stock",
                action: raw.action || "待处理",
                qty: Number(raw.qty || 0),
                price: Number(raw.price || 0),
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
        toastTimer.interval = root.toastError ? 4200 : 2200
        toastTimer.restart()
    }

    function syncPendingOrders() {
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

    function ensureLiveWatch() {
        var watchSymbol = serviceSymbolForMode(root.activeMode, root.activeSymbol)
        if (!watchSymbol || !marketBridgeReady) {
            return
        }
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
            requestSymbol = serviceSymbolForMode(mode, payload.code)
            if (mode === "stock") {
                requestSide = action === "sell" ? "SELL" : "BUY"
            } else if (mode === "margin_buy") {
                requestSide = "BUY"
            } else if (mode === "margin_sell") {
                requestSide = "SELL"
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
            if (!requestQuantity || requestQuantity <= 0) {
                requestQuantity = mode === "futures" || mode === "options" ? 1 : 100
            }
            requestQuantity = Math.floor(requestQuantity)

            if ((mode === "stock" || mode === "margin_buy" || mode === "margin_sell")
                    && (requestQuantity < 100 || requestQuantity % 100 !== 0)) {
                showPageToast("股票股数必须是100的整数倍", true)
                return
            }
            if ((mode === "futures" || mode === "options") && requestQuantity < 1) {
                showPageToast("委托手数必须大于0", true)
                return
            }

            if (action !== "optionExercise" && requestPrice <= 0) {
                showPageToast(requestOrderType === "MARKET"
                    ? "当前未收到实时行情，市价单不可用，请切换限价后输入价格"
                    : "请输入有效委托价格", true)
                return
            }

            var bridgeRequest = {
                symbol: requestSymbol,
                side: requestSide,
                price: requestPrice,
                quantity: requestQuantity,
                orderType: requestOrderType,
                mode: mode,
                action: action,
                strategyId: "manual_test",
                strategyName: "Manual Test"
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
                syncLiveState()
                showPageToast("已提交" + tradeActionLabel(mode, action) + "委托", false)
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
                syncLiveState()
                showPageToast("交易服务未就绪，已回退为本地模拟委托", false)
            } else {
                showPageToast("交易服务未就绪", true)
            }
            return
        }

        if (submitFallbackTrade(mode, action, payload)) {
            syncLiveState()
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

    function syncLiveState() {
        var liveQuote = resolveLiveQuote(root.activeSymbol)
        var displayQuote = liveQuote || resolveDisplayQuote(root.activeSymbol)
        var displaySnapshot = buildMarketSnapshotFromQuote(displayQuote)
        if (displaySnapshot) {
            root.marketSnapshot = displaySnapshot
        } else {
            root.marketSnapshot = root.emptyMarketSnapshot(root.currentMarketDisplaySymbol())
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
                    text: root.marketDisplayState === "live"
                        ? "当前模式已接入真实桥接行情，盘口与L2按实时 tick/bar 更新"
                        : (root.marketDisplayState === "cached"
                            ? "当前显示最近缓存快照，盘口与L2仍只在实时行情可用时显示"
                            : "当前未收到桥接行情，页面保持空态且不会注入模拟盘口")
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
                        color: root.marketDisplayState === "live" ? "#14b8a6" : (root.marketDisplayState === "cached" ? "#38bdf8" : "#64748b")
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                            text: root.marketDisplayState === "live" ? "Trading / Live" : (root.marketDisplayState === "cached" ? "Trading / Snapshot" : "Trading / Empty")
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
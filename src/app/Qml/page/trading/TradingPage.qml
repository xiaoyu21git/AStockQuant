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
    property bool depthPanelRequested: false
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
    readonly property var accountSnapshot: positionAccountService ? (positionAccountService.accountSnapshot || ({})) : ({})
    readonly property var rawPositions: positionAccountService ? (positionAccountService.positions || []) : []
    readonly property real resolvedTotalAsset: accountSnapshot && accountSnapshot.totalAsset !== undefined
        ? Number(accountSnapshot.totalAsset)
        : (resolvedAvailableCapital + resolvedPositionMarketValue)
    readonly property real resolvedPositionMarketValue: calculatePositionMarketValue(rawPositions)
    readonly property var displayPositions: mapDisplayPositions(rawPositions, resolvedPositionMarketValue)
    readonly property var groupedDisplayPositions: buildGroupedDisplayPositions(displayPositions)
    readonly property real holdingsPanelContentHeight: calculateHoldingsPanelHeight(groupedDisplayPositions)
    readonly property real holdingsPanelMaxHeight: Math.max(244, Math.min(420, root.height * 0.42))
    readonly property real holdingsPanelPreferredHeight: Math.min(holdingsPanelContentHeight, holdingsPanelMaxHeight)
    readonly property bool holdingsPanelScrollable: holdingsPanelContentHeight > holdingsPanelPreferredHeight + 1
    readonly property real resolvedAvailableCapital: positionAccountService && positionAccountService.accountSnapshot && positionAccountService.accountSnapshot.availableCash !== undefined
        ? Number(positionAccountService.accountSnapshot.availableCash)
        : 800000

    function cloneList(list) {
        return list ? list.slice(0) : []
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
        var quantity = normalizePositionQuantity(positionData ? positionData.closeableQuantity : 0, positionData ? positionData.type : "stock")
        if (quantity <= 0) {
            return false
        }
        if (positionData.type === "futures" || positionData.type === "options") {
            return quantity >= 1
        }
        return quantity >= 100 && quantity % 100 === 0
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
        if (text === "PENDING_RISK") {
            return "风控审批中"
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
        if (rawText === "风控审批中") {
            return "PENDING_RISK"
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
        var statusOrigin = String(orderItem && orderItem.statusOrigin ? orderItem.statusOrigin : "").trim().toLowerCase()
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
                cashAmount: Number(raw.cashAmount !== undefined ? raw.cashAmount : (raw.cash_amount !== undefined ? raw.cash_amount : (raw.requestedNotional !== undefined ? raw.requestedNotional : 0))),
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

    function buildQuickCloseRequest(positionData) {
        var mode = String(positionData && positionData.type ? positionData.type : "stock").trim().toLowerCase()
        var closeQuantity = normalizePositionQuantity(positionData && positionData.closeableQuantity !== undefined
            ? positionData.closeableQuantity
            : 0, mode)

        if (closeQuantity <= 0) {
            return { error: "当前仓位没有可平数量" }
        }

        if ((mode === "stock" || mode === "margin_buy" || mode === "margin_sell")
                && (closeQuantity < 100 || closeQuantity % 100 !== 0)) {
            return { error: "当前可平数量不足100股或不是100股整数倍" }
        }

        var requestSide = "SELL"
        var requestPositionEffect = ""
        var requestAction = "sell"
        var positionSide = String(positionData && positionData.positionSide ? positionData.positionSide : "LONG").trim().toUpperCase()

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

        var requestSymbol = serviceSymbolForMode(mode, positionData.symbol)
        if (!requestSymbol) {
            return { error: invalidSymbolMessageForMode(mode) }
        }

        var liveQuote = resolveLiveQuote(requestSymbol)
        var livePrice = safeNumber(liveQuote && liveQuote.price !== undefined ? liveQuote.price : 0, 0)
        var fallbackPrice = safeNumber(positionData && (positionData.lastPrice || positionData.avgPrice), 0)
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
            request.underlying = positionData.underlying
            request.optionType = positionData.optionType
            request.expiry = positionData.expiry
        }

        return {
            request: request,
            actionLabel: tradeActionLabel(mode, requestAction)
        }
    }

    function quickClosePosition(positionData) {
        var resolved = buildQuickCloseRequest(positionData)
        if (resolved.error) {
            showPageToast(resolved.error, true)
            return
        }
        if (!tradeExecutionService) {
            showPageToast("交易服务未就绪，暂时无法一键平仓", true)
            return
        }

        if (tradeExecutionService.submitBridgeOrder(resolved.request)) {
            syncLiveState()
            showPageToast("已提交" + resolved.actionLabel + "委托，等待风控审批", false)
            return
        }

        var submitError = tradeExecutionService.lastErrorMessage
            ? String(tradeExecutionService.lastErrorMessage).trim()
            : ""
        showPageToast(submitError.length > 0 ? submitError : "一键平仓委托提交失败", true)
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

            var bridgeRequest = {
                symbol: requestSymbol,
                side: requestSide,
                price: requestPrice,
                quantity: requestQuantity,
                orderType: requestOrderType,
                mode: mode,
                action: action
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
                syncLiveState()
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
        if (positionAccountService && typeof positionAccountService.requestInitialSnapshot === "function") {
            positionAccountService.requestInitialSnapshot()
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

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        Item {
            id: pageContent
            width: pageViewport.width
            height: pageColumn.implicitHeight

            ColumnLayout {
                id: pageColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 18

                Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: holdingsPanelPreferredHeight
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

                        ScrollBar.horizontal: ScrollBar {
                            policy: ScrollBar.AlwaysOff
                        }

                        ScrollBar.vertical: ScrollBar {
                            policy: root.holdingsPanelScrollable ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                        }

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
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 46
                                            radius: 14
                                            color: "#0d1728"
                                            border.color: "#21354c"
                                            border.width: 1

                                            readonly property var positionData: modelData || ({})

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 6

                                                ColumnLayout {
                                                    Layout.preferredWidth: Math.max(172, root.width * 0.18)
                                                    Layout.alignment: Qt.AlignVCenter
                                                    spacing: 1

                                                    Text {
                                                        text: String(positionData.symbol || "--")
                                                            + (String(positionData.name || "").length > 0 ? "  " + String(positionData.name || "") : "")
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
                                                    Layout.preferredWidth: 72
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
                                                        enabled: positionData.canQuickClose
                                                        cursorShape: positionData.canQuickClose ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                                                        onClicked: root.quickClosePosition(positionData)
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
            Layout.fillWidth: true
            implicitHeight: Math.max(formPanelHeight, depthPanelHeight)
            readonly property real formPanelHeight: formPanelLoader.item
                ? Math.max(formPanelLoader.item.implicitHeight, formPanelLoader.item.height)
                : 800
            readonly property real depthPanelHeight: depthPanelLoader.item
                ? Math.max(depthPanelLoader.item.implicitHeight, depthPanelLoader.item.height)
                : 600

            Item {
                id: tradingContent
                anchors.fill: parent

                RowLayout {
                    id: tradingPanels
                    anchors.fill: parent
                    spacing: 14

                    Loader {
                        id: formPanelLoader
                        Layout.preferredWidth: Math.min(468, Math.max(336, tradingContent.width * 0.37))
                        Layout.alignment: Qt.AlignTop
                        Layout.fillHeight: true
                        asynchronous: true
                        active: true
                        sourceComponent: TradingComponents.TradingFormPanel {
                            width: formPanelLoader.width
                            height: tradingPanels.height
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

                        onStatusChanged: {
                            if (status === Loader.Ready) {
                                root.depthPanelRequested = true
                            }
                        }
                    }

                    Loader {
                        id: depthPanelLoader
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignTop
                        asynchronous: true
                        active: root.depthPanelRequested
                        sourceComponent: TradingComponents.DepthMarketPanel {
                            width: depthPanelLoader.width
                            height: tradingPanels.height
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
        }
    }
}
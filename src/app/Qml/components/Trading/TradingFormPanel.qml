import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    radius: 28
    color: "#091120"
    border.color: "#1f3148"
    border.width: 1
    implicitHeight: compactMode ? 800 : 980

    property var marketSnapshot: ({})
    property var depthSnapshot: ({})
    property real availableCapital: 500000
    property var pendingOrders: []
    property string toastMessage: ""
    property bool toastError: false
    property string positionAvailabilitySummary: ""
    property bool positionAvailabilityError: false
    property bool compactMode: false
    readonly property int compactTitleFont: compactMode ? 18 : 24
    readonly property int compactBodyFont: compactMode ? 11 : 13
    readonly property int compactMetaFont: compactMode ? 10 : 12
    readonly property int compactButtonFont: compactMode ? 10 : 13
    readonly property int compactButtonHeight: compactMode ? 28 : 38
    readonly property int compactChipHeight: compactMode ? 32 : 46
    readonly property int compactOrderRowHeight: compactMode ? 58 : 72
    readonly property int compactSectionLabelFont: compactMode ? 10 : 12
    readonly property int compactInputFont: compactMode ? 10 : 12
    readonly property int compactInputHeight: compactMode ? 30 : 38
    readonly property int compactInputRadius: compactMode ? 10 : 12
    readonly property int compactInputHorizontalPadding: compactMode ? 10 : 12
    readonly property int compactInputVerticalPadding: compactMode ? 0 : 1
    readonly property int compactQuickButtonHeight: compactMode ? 24 : 30
    readonly property int compactQuickButtonFont: compactMode ? 9 : 10
    readonly property int compactActionHeight: compactMode ? 28 : 38
    readonly property int compactActionRadius: compactMode ? 12 : 16

    property int currentTabIndex: 0
    property bool deferredOrderListReady: false

    property string stockCode: "000001"
    property string stockShares: "100"
    property string stockPriceType: "limit"
    property string stockPrice: ""

    property string futuresCode: "RB2410"
    property string futuresLots: "1"
    property string futuresPriceType: "market"
    property string futuresPrice: ""

    property string marginBuyCode: "000001"
    property string marginBuyShares: "100"
    property string marginBuyPriceType: "limit"
    property string marginBuyPrice: ""

    property string marginSellCode: "000001"
    property string marginSellShares: "100"
    property string marginSellPriceType: "limit"
    property string marginSellPrice: ""
    property string lastAutoStockPrice: ""
    property string lastAutoMarginBuyPrice: ""
    property string lastAutoMarginSellPrice: ""
    property string lastAutoStockPriceType: ""
    property string lastAutoMarginBuyPriceType: ""
    property string lastAutoMarginSellPriceType: ""

    property string optionCode: "10004411"
    property string optionUnderlying: "510050"
    property string optionLots: "1/1"
    property string optionPriceType: "market"
    property string optionPrice: ""
    property string optionType: "call"
    property string optionExpiry: "当月"

    readonly property var tabs: [
        { code: "stock", label: "普通股票", icon: "📊" },
        { code: "futures", label: "期货", icon: "📈" },
        { code: "margin_buy", label: "融资买入", icon: "💳" },
        { code: "margin_sell", label: "融券卖出", icon: "📉" },
        { code: "options", label: "期权", icon: "🎯" }
    ]

    readonly property string currentMode: tabs[currentTabIndex].code
    readonly property string currentSymbol: currentMode === "stock" ? stockCode
        : currentMode === "futures" ? futuresCode
        : currentMode === "margin_buy" ? marginBuyCode
        : currentMode === "margin_sell" ? marginSellCode
        : (optionCode.length > 0 ? optionCode : optionUnderlying)

    signal modeContextChanged(string mode, string symbol)
    signal executeTrade(string mode, string action, var payload)
    signal cancelOrderRequested(var orderId)
    signal approveCheckpointRequested(var orderData, bool retryAfterApproval)
    signal resumeExecutionPauseRequested(var orderData, bool retryAfterResume)

    function isEquityMode(mode) {
        return mode === "stock" || mode === "margin_buy" || mode === "margin_sell"
    }

    function currentCodeForMode(mode) {
        if (mode === "stock") {
            return stockCode
        }
        if (mode === "margin_buy") {
            return marginBuyCode
        }
        if (mode === "margin_sell") {
            return marginSellCode
        }
        return ""
    }

    function isValidEquityCodeInput(mode) {
        var text = String(currentCodeForMode(mode || currentMode) || "").trim().toUpperCase()
        return /^\d{6}$/.test(text) || /^(\d{6})\.(SH|SZ|BJ)$/.test(text) || /^(SHSE|SZSE|BSE)\.\d{6}$/.test(text)
    }

    function currentSharesForMode(mode) {
        if (mode === "stock") {
            return stockShares
        }
        if (mode === "margin_buy") {
            return marginBuyShares
        }
        if (mode === "margin_sell") {
            return marginSellShares
        }
        return ""
    }

    function currentPriceTypeForMode(mode) {
        if (mode === "stock") {
            return stockPriceType
        }
        if (mode === "margin_buy") {
            return marginBuyPriceType
        }
        if (mode === "margin_sell") {
            return marginSellPriceType
        }
        return "market"
    }

    function currentPriceInputForMode(mode) {
        if (mode === "stock") {
            return stockPrice
        }
        if (mode === "margin_buy") {
            return marginBuyPrice
        }
        if (mode === "margin_sell") {
            return marginSellPrice
        }
        return ""
    }

    function formatDisplayPrice(value, digits) {
        var numericValue = Number(value)
        if (isNaN(numericValue) || numericValue <= 0) {
            return "--"
        }
        return numericValue.toFixed(digits === undefined ? 2 : digits)
    }

    function formatAmountShort(value) {
        var numericValue = Number(value)
        if (isNaN(numericValue) || numericValue <= 0) {
            return "--"
        }
        if (numericValue >= 100000000) {
            return (numericValue / 100000000).toFixed(2) + "亿"
        }
        if (numericValue >= 10000) {
            return (numericValue / 10000).toFixed(2) + "万"
        }
        return numericValue.toFixed(2)
    }

    function modePrice() {
        if (currentMode === "futures") {
            return Number(marketSnapshot && marketSnapshot.futuresPrice !== undefined ? marketSnapshot.futuresPrice : 0)
        }
        if (currentMode === "options") {
            return Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
        }
        return Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
    }

    function modeDigits() {
        if (currentMode === "futures") {
            return 0
        }
        if (currentMode === "options") {
            return 4
        }
        return 2
    }

    function formatPrice(value) {
        return formatDisplayPrice(value, modeDigits())
    }

    function modePriceText() {
        var priceText = formatPrice(modePrice())
        if (priceText === "--") {
            return priceText
        }
        return isEquityMode(currentMode) ? "¥" + priceText : priceText
    }

    function currentModeDisplayTitle() {
        var baseLabel = root.tabs[root.currentTabIndex].label
        if (!isEquityMode(currentMode)) {
            return baseLabel + "  ·  " + root.currentSymbol
        }

        var nameText = marketSnapshot && marketSnapshot.name ? String(marketSnapshot.name).trim() : ""
        return baseLabel + "  ·  " + (nameText.length > 0 ? nameText : "待识别标的")
    }

    function quoteTimeText() {
        var updatedText = marketSnapshot && marketSnapshot.updatedAt ? String(marketSnapshot.updatedAt).trim() : ""
        if (updatedText.length === 0) {
            return ""
        }
        if (updatedText.indexOf(" ") >= 0) {
            return updatedText.slice(updatedText.length - 8)
        }
        if (updatedText.length >= 8 && updatedText.indexOf(":") >= 0) {
            return updatedText.slice(updatedText.length - 8)
        }
        return ""
    }

    function isOpeningMarketWindow() {
        var timeText = root.quoteTimeText()
        if (timeText.length !== 8) {
            return false
        }
        return (timeText >= "09:30:00" && timeText <= "09:35:00")
            || (timeText >= "13:00:00" && timeText <= "13:05:00")
    }

    function preferredEquityPriceType(mode) {
        var targetMode = mode || currentMode
        if (!isEquityMode(targetMode)) {
            return "market"
        }
        return hasRealtimeEquityQuote(targetMode) && root.isOpeningMarketWindow() ? "market" : "limit"
    }

    function currentAutoPriceTypeForMode(mode) {
        if (mode === "stock") {
            return root.lastAutoStockPriceType
        }
        if (mode === "margin_buy") {
            return root.lastAutoMarginBuyPriceType
        }
        if (mode === "margin_sell") {
            return root.lastAutoMarginSellPriceType
        }
        return ""
    }

    function setAutoPriceTypeForMode(mode, value) {
        if (mode === "stock") {
            root.lastAutoStockPriceType = value
            return
        }
        if (mode === "margin_buy") {
            root.lastAutoMarginBuyPriceType = value
            return
        }
        if (mode === "margin_sell") {
            root.lastAutoMarginSellPriceType = value
        }
    }

    function syncModeReferencePriceType(mode) {
        var targetMode = mode || currentMode
        var preferredType = root.preferredEquityPriceType(targetMode)
        var currentType
        var autoType = root.currentAutoPriceTypeForMode(targetMode)

        if (!isEquityMode(targetMode)) {
            return
        }

        currentType = root.currentPriceTypeForMode(targetMode)
        if (currentType.length === 0 || currentType === autoType) {
            if (targetMode === "stock") {
                root.stockPriceType = preferredType
            } else if (targetMode === "margin_buy") {
                root.marginBuyPriceType = preferredType
            } else if (targetMode === "margin_sell") {
                root.marginSellPriceType = preferredType
            }
        }

        root.setAutoPriceTypeForMode(targetMode, preferredType)
    }

    function syncAllReferencePriceTypes() {
        root.syncModeReferencePriceType("stock")
        root.syncModeReferencePriceType("margin_buy")
        root.syncModeReferencePriceType("margin_sell")
    }

    function syncModeReferencePrice(mode) {
        var targetMode = mode || currentMode
        var latestPrice = Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
        var formattedPrice
        var currentText

        if (!isEquityMode(targetMode) || isNaN(latestPrice) || latestPrice <= 0) {
            return
        }

        formattedPrice = latestPrice.toFixed(2)

        if (targetMode === "stock") {
            currentText = String(root.stockPrice || "").trim()
            if (root.stockPriceType === "market" || currentText.length === 0 || currentText === root.lastAutoStockPrice) {
                root.stockPrice = formattedPrice
            }
            root.lastAutoStockPrice = formattedPrice
            return
        }

        if (targetMode === "margin_buy") {
            currentText = String(root.marginBuyPrice || "").trim()
            if (root.marginBuyPriceType === "market" || currentText.length === 0 || currentText === root.lastAutoMarginBuyPrice) {
                root.marginBuyPrice = formattedPrice
            }
            root.lastAutoMarginBuyPrice = formattedPrice
            return
        }

        if (targetMode === "margin_sell") {
            currentText = String(root.marginSellPrice || "").trim()
            if (root.marginSellPriceType === "market" || currentText.length === 0 || currentText === root.lastAutoMarginSellPrice) {
                root.marginSellPrice = formattedPrice
            }
            root.lastAutoMarginSellPrice = formattedPrice
        }
    }

    function syncAllReferencePrices() {
        root.syncModeReferencePrice("stock")
        root.syncModeReferencePrice("margin_buy")
        root.syncModeReferencePrice("margin_sell")
    }

    function priceStepForMode(mode) {
        var targetMode = mode || currentMode
        if (targetMode === "futures") {
            return 1
        }
        if (targetMode === "options") {
            return 0.0001
        }
        return 0.01
    }

    function modeReferencePrice(mode) {
        var targetMode = mode || currentMode
        if (targetMode === "futures") {
            return Number(marketSnapshot && marketSnapshot.futuresPrice !== undefined ? marketSnapshot.futuresPrice : 0)
        }
        return Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
    }

    function setModePriceInput(mode, priceValue) {
        var targetMode = mode || currentMode
        var numericValue = Number(priceValue)
        if (isNaN(numericValue) || numericValue <= 0) {
            return
        }

        var formattedValue = Number(root.roundPriceByMode(numericValue, targetMode)).toFixed(root.priceDigitsForMode(targetMode))
        if (targetMode === "stock") {
            root.stockPriceType = "limit"
            root.stockPrice = formattedValue
            return
        }
        if (targetMode === "futures") {
            root.futuresPriceType = "limit"
            root.futuresPrice = formattedValue
            return
        }
        if (targetMode === "margin_buy") {
            root.marginBuyPriceType = "limit"
            root.marginBuyPrice = formattedValue
            return
        }
        if (targetMode === "margin_sell") {
            root.marginSellPriceType = "limit"
            root.marginSellPrice = formattedValue
            return
        }
        root.optionPriceType = "limit"
        root.optionPrice = formattedValue
    }

    function adjustModePrice(mode, stepDelta) {
        var targetMode = mode || currentMode
        var currentValue = Number(root.currentPriceInputForMode(targetMode))
        var basePrice = currentValue
        var step = root.priceStepForMode(targetMode)

        if (isNaN(basePrice) || basePrice <= 0) {
            basePrice = root.modeReferencePrice(targetMode)
        }
        if (isNaN(basePrice) || basePrice <= 0 || step <= 0) {
            return
        }

        root.setModePriceInput(targetMode, Math.max(step, basePrice + step * Number(stepDelta || 0)))
    }

    function quickButtons() {
        if (currentMode === "stock" || currentMode === "margin_buy" || currentMode === "margin_sell") {
            return ["100", "500", "1000", "2000", "5000"]
        }
        if (currentMode === "futures") {
            return ["1", "5", "10", "20"]
        }
        return ["1/1", "1/2", "1/3", "1/4"]
    }

    function applyQuickValue(value) {
        if (currentMode === "stock") {
            stockShares = value
        } else if (currentMode === "futures") {
            futuresLots = value
        } else if (currentMode === "margin_buy") {
            marginBuyShares = value
        } else if (currentMode === "margin_sell") {
            marginSellShares = value
        } else {
            optionLots = value
        }
    }

    function equityQuickPriceButtons() {
        return [
            { code: "opponent", label: "对手" },
            { code: "bid1", label: "买一" },
            { code: "ask1", label: "卖一" },
            { code: "latest", label: "最新" },
            { code: "upper", label: "涨停" },
            { code: "lower", label: "跌停" }
        ]
    }

    function hasEquityDisplayQuote(mode) {
        var targetMode = mode || currentMode
        if (!isEquityMode(targetMode)) {
            return false
        }
        var numericPrice = Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
        return !isNaN(numericPrice) && numericPrice > 0
    }

    function hasRealtimeEquityQuote(mode) {
        return hasEquityDisplayQuote(mode || currentMode) && !!(marketSnapshot && marketSnapshot.live)
    }

    function equitySymbolText(mode) {
        var liveSymbol = marketSnapshot && marketSnapshot.symbol ? String(marketSnapshot.symbol) : ""
        if (liveSymbol.length > 0) {
            return liveSymbol
        }
        return String(currentCodeForMode(mode || currentMode) || "").trim().toUpperCase()
    }

    function equityIdentitySummary(mode) {
        var targetMode = mode || currentMode
        var symbolText = equitySymbolText(targetMode)
        if (!isValidEquityCodeInput(targetMode)) {
            return "请输入有效6位股票代码"
        }
        if (!hasEquityDisplayQuote(targetMode)) {
            return symbolText.length > 0 ? symbolText + "  未收到实时行情" : "输入股票代码后等待实时行情"
        }

        var nameText = marketSnapshot && marketSnapshot.name ? String(marketSnapshot.name) : ""
        var updatedText = marketSnapshot && marketSnapshot.updatedAt ? String(marketSnapshot.updatedAt) : ""
        if (updatedText.indexOf(" ") >= 0 && updatedText.length > 8) {
            updatedText = updatedText.slice(updatedText.length - 8)
        }

        var parts = []
        if (nameText.length > 0) {
            parts.push(nameText)
        }
        if (symbolText.length > 0) {
            parts.push(symbolText)
        }
        if (updatedText.length > 0) {
            parts.push(updatedText)
        }
        if (!hasRealtimeEquityQuote(targetMode)) {
            parts.push("缓存快照")
        }
        if (targetMode === currentMode && String(positionAvailabilitySummary || "").length > 0) {
            parts.push(String(positionAvailabilitySummary || ""))
        }
        return parts.length > 0 ? parts.join("  ") : "实时行情"
    }

    function equityIdentitySummaryColor(mode) {
        if ((mode || currentMode) === currentMode && positionAvailabilityError) {
            return "#fbbf24"
        }
        return "#7ea1c5"
    }

    function depthTopPrice(side) {
        var rows = depthSnapshot && depthSnapshot[side] ? depthSnapshot[side] : []
        if (!rows || rows.length === 0) {
            return 0
        }
        var numericPrice = Number(rows[0].price || 0)
        return isNaN(numericPrice) ? 0 : numericPrice
    }

    function resolveEquityShortcutPrice(shortcut, mode) {
        var targetMode = mode || currentMode
        if (!hasEquityDisplayQuote(targetMode)) {
            return 0
        }

        var latestPrice = Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
        var bid1Price = depthTopPrice("bids")
        var ask1Price = depthTopPrice("asks")

        if (shortcut === "opponent") {
            if (!hasRealtimeEquityQuote(targetMode)) {
                return 0
            }
            if (targetMode === "margin_sell") {
                return bid1Price > 0 ? bid1Price : latestPrice
            }
            return ask1Price > 0 ? ask1Price : latestPrice
        }
        if (shortcut === "bid1") {
            if (!hasRealtimeEquityQuote(targetMode)) {
                return 0
            }
            return bid1Price > 0 ? bid1Price : latestPrice
        }
        if (shortcut === "ask1") {
            if (!hasRealtimeEquityQuote(targetMode)) {
                return 0
            }
            return ask1Price > 0 ? ask1Price : latestPrice
        }
        if (shortcut === "upper") {
            var upperValue = Number(marketSnapshot && marketSnapshot.upperLimit !== undefined ? marketSnapshot.upperLimit : 0)
            return upperValue > 0 ? upperValue : latestPrice
        }
        if (shortcut === "lower") {
            var lowerValue = Number(marketSnapshot && marketSnapshot.lowerLimit !== undefined ? marketSnapshot.lowerLimit : 0)
            return lowerValue > 0 ? lowerValue : latestPrice
        }
        return latestPrice
    }

    function equityShortcutButtonText(buttonModel, mode) {
        return buttonModel.label + " " + formatDisplayPrice(resolveEquityShortcutPrice(buttonModel.code, mode), 2)
    }

    function applyEquityPriceShortcut(targetMode, shortcut) {
        var priceValue = resolveEquityShortcutPrice(shortcut, targetMode)
        if (priceValue <= 0) {
            return
        }
        var formattedPrice = Number(priceValue).toFixed(2)
        if (targetMode === "stock") {
            stockPriceType = "limit"
            stockPrice = formattedPrice
            return
        }
        if (targetMode === "margin_buy") {
            marginBuyPriceType = "limit"
            marginBuyPrice = formattedPrice
            return
        }
        if (targetMode === "margin_sell") {
            marginSellPriceType = "limit"
            marginSellPrice = formattedPrice
        }
    }

    function currentEquityResolvedPrice(mode) {
        var targetMode = mode || currentMode
        var manualPrice = Number(currentPriceInputForMode(targetMode))
        if (currentPriceTypeForMode(targetMode) === "limit" && !isNaN(manualPrice) && manualPrice > 0) {
            return manualPrice
        }
        return resolveEquityShortcutPrice("latest", targetMode)
    }

    function currentEquityOrderAmount(mode) {
        var targetMode = mode || currentMode
        var sharesValue = Number(currentSharesForMode(targetMode))
        var resolvedPrice = currentEquityResolvedPrice(targetMode)
        if (isNaN(sharesValue) || sharesValue <= 0 || resolvedPrice <= 0) {
            return 0
        }
        return sharesValue * resolvedPrice
    }

    function currentEquityPositionRatio(mode) {
        var amountValue = currentEquityOrderAmount(mode || currentMode)
        var capitalValue = Number(availableCapital)
        if (amountValue <= 0 || isNaN(capitalValue) || capitalValue <= 0) {
            return 0
        }
        return amountValue / capitalValue * 100
    }

    function equityPriceSummary(mode) {
        var targetMode = mode || currentMode
        return "最新 " + formatDisplayPrice(resolveEquityShortcutPrice("latest", targetMode), 2)
            + " / 买一 " + formatDisplayPrice(resolveEquityShortcutPrice("bid1", targetMode), 2)
            + " / 卖一 " + formatDisplayPrice(resolveEquityShortcutPrice("ask1", targetMode), 2)
    }

    function equityAmountSummary(mode) {
        var targetMode = mode || currentMode
        var amountValue = currentEquityOrderAmount(targetMode)
        var ratioValue = currentEquityPositionRatio(targetMode)
        var capitalValue = Number(availableCapital)
        return "金额 " + formatAmountShort(amountValue)
            + " / 仓位 " + (ratioValue > 0 ? ratioValue.toFixed(2) + "%" : "--")
            + " / 可用 " + formatAmountShort(capitalValue)
    }

    function referenceText() {
        return "市价参考 " + formatPrice(modePrice())
    }

    function submit(action) {
        if (currentMode === "stock") {
            executeTrade("stock", action, {
                code: stockCode,
                shares: stockShares,
                priceType: stockPriceType,
                priceInput: stockPrice
            })
        } else if (currentMode === "futures") {
            executeTrade("futures", action, {
                code: futuresCode,
                lots: futuresLots,
                priceType: futuresPriceType,
                priceInput: futuresPrice
            })
        } else if (currentMode === "margin_buy") {
            executeTrade("margin_buy", action, {
                code: marginBuyCode,
                shares: marginBuyShares,
                priceType: marginBuyPriceType,
                priceInput: marginBuyPrice
            })
        } else if (currentMode === "margin_sell") {
            executeTrade("margin_sell", action, {
                code: marginSellCode,
                shares: marginSellShares,
                priceType: marginSellPriceType,
                priceInput: marginSellPrice
            })
        } else {
            executeTrade("options", action, {
                code: optionCode,
                underlying: optionUnderlying,
                lots: optionLots,
                priceType: optionPriceType,
                priceInput: optionPrice,
                optionType: optionType,
                expiry: optionExpiry
            })
        }
    }

    function orderUnit(order) {
        var action = String(order && order.action ? order.action : "").trim()
        if (action === "现金还款") {
            return "元"
        }
        return order.type === "futures" || order.type === "options" ? "手" : "股"
    }

    function orderHeadlineAmount(order) {
        var cashAmount = Number(order && order.cashAmount !== undefined ? order.cashAmount : 0)
        if (!isNaN(cashAmount) && cashAmount > 0 && String(order && order.action ? order.action : "").trim() === "现金还款") {
            return formatAmountShort(cashAmount)
        }

        var quantity = Number(order && order.qty !== undefined ? order.qty : 0)
        if (isNaN(quantity)) {
            quantity = 0
        }
        return quantity + root.orderUnit(order || {})
    }

    function orderFilledSummary(order) {
        var cashAmount = Number(order && order.cashAmount !== undefined ? order.cashAmount : 0)
        if (!isNaN(cashAmount) && cashAmount > 0 && String(order && order.action ? order.action : "").trim() === "现金还款") {
            var repayStatus = String(order && order.status ? order.status : "").trim()
            if (repayStatus === "已成") {
                return "已还款 " + formatAmountShort(cashAmount)
            }
            return ""
        }

        var totalQuantity = Number(order && order.qty !== undefined ? order.qty : 0)
        var filledQuantity = Number(order && order.filledQty !== undefined ? order.filledQty : 0)
        var statusText = String(order && order.status ? order.status : "").trim()
        var progressPrefix = statusText === "已成" ? "全部成交 " : "成交 "

        if (isNaN(totalQuantity) || totalQuantity < 0) {
            totalQuantity = 0
        }
        if (isNaN(filledQuantity) || filledQuantity < 0) {
            filledQuantity = 0
        }
        if (totalQuantity > 0 && filledQuantity > totalQuantity) {
            filledQuantity = totalQuantity
        }

        if (statusText === "已成" && filledQuantity <= 0 && totalQuantity > 0) {
            filledQuantity = totalQuantity
        }

        if (statusText !== "部分成交" && statusText !== "已成" && statusText !== "已撤" && statusText !== "已拒") {
            return ""
        }

        if (filledQuantity <= 0 && statusText !== "已成") {
            return ""
        }

        if (filledQuantity <= 0 && totalQuantity <= 0) {
            return ""
        }

        if (totalQuantity > 0) {
            return progressPrefix + filledQuantity + "/" + totalQuantity + root.orderUnit(order || {})
        }

        return progressPrefix + filledQuantity + root.orderUnit(order || {})
    }

    function orderPriceSummary(order) {
        var cashAmount = Number(order && order.cashAmount !== undefined ? order.cashAmount : 0)
        if (!isNaN(cashAmount) && cashAmount > 0 && String(order && order.action ? order.action : "").trim() === "现金还款") {
            return "还款额 " + formatAmountShort(cashAmount)
        }

        var digits = 2
        if (order && order.type === "options") {
            digits = 4
        } else if (order && order.type === "futures") {
            digits = 0
        }

        var priceText = formatDisplayPrice(order && order.price !== undefined ? order.price : 0, digits)
        if (priceText === "--") {
            return "委托价 --"
        }

        return "委托价 " + priceText
    }

    function orderIdentifierSummary(order) {
        var clientOrderId = String(order && order.clientOrderId ? order.clientOrderId : "").trim()
        var brokerOrderId = String(order && order.brokerOrderId ? order.brokerOrderId : "").trim()
        if (!clientOrderId && !brokerOrderId) {
            return ""
        }

        if (brokerOrderId && brokerOrderId !== clientOrderId) {
            return "委托 " + clientOrderId + "  ·  柜台 " + brokerOrderId
        }

        return "委托 " + (clientOrderId || brokerOrderId)
    }

    function orderAuxiliarySummary(order) {
        var message = String(order && order.message ? order.message : "").trim()
        if (canonicalOrderStatus(order) === "REJECTED") {
            var parts = []
            var ruleId = String(order && order.ruleId ? order.ruleId : "").trim()
            var reasonCode = String(order && order.reasonCode ? order.reasonCode : "").trim()
            var batchId = String(order && (order.requiredBatchId || order.batchId) ? (order.requiredBatchId || order.batchId) : "").trim()
            var blockingBatchId = String(order && order.blockingBatchId ? order.blockingBatchId : "").trim()
            if (ruleId.length > 0) {
                parts.push("规则 " + ruleId)
            }
            if (reasonCode.length > 0) {
                parts.push("原因码 " + reasonCode)
            }
            if (batchId.length > 0) {
                parts.push("批次 " + batchId)
            }
            if (blockingBatchId.length > 0) {
                parts.push("阻断批次 " + blockingBatchId)
            }
            if (message.length > 0) {
                parts.push(message)
            }
            if (parts.length > 0) {
                return parts.join(" · ")
            }
        }
        return orderIdentifierSummary(order)
    }

    function canonicalOrderStatus(order) {
        var rawStatus = String(order && order.rawStatus ? order.rawStatus : (order && order.status ? order.status : "")).trim()
        if (rawStatus === "已请求") {
            return "REQUESTED"
        }
        if (rawStatus === "已报") {
            return "SUBMITTED"
        }
        if (rawStatus === "待处理") {
            return "PENDING"
        }
        if (rawStatus === "部分成交") {
            return "PARTIAL_FILLED"
        }
        if (rawStatus === "已成") {
            return "FILLED"
        }
        if (rawStatus === "撤单中") {
            return "PENDING_CANCEL"
        }
        if (rawStatus === "已撤") {
            return "CANCELLED"
        }
        if (rawStatus === "已拒") {
            return "REJECTED"
        }

        rawStatus = rawStatus.toUpperCase()
        if (rawStatus === "PARTIALLY_FILLED") {
            return "PARTIAL_FILLED"
        }
        if (rawStatus === "NEW" || rawStatus === "PENDINGNEW") {
            return "SUBMITTED"
        }
        return rawStatus
    }

    function canCancelOrder(order) {
        if (!order) {
            return false
        }

        var totalQuantity = Number(order && order.qty !== undefined ? order.qty : 0)
        var filledQuantity = Number(order && order.filledQty !== undefined ? order.filledQty : 0)
        if (!isNaN(totalQuantity) && totalQuantity > 0 && !isNaN(filledQuantity) && filledQuantity >= totalQuantity) {
            return false
        }

        if (order.source === "simulation") {
            var simulationStatus = canonicalOrderStatus(order)
            return simulationStatus !== "CANCELLED"
                && simulationStatus !== "REJECTED"
                && simulationStatus !== "FILLED"
                && simulationStatus !== "PENDING_CANCEL"
        }

        var status = canonicalOrderStatus(order)
        return status === "SUBMITTED"
            || status === "PENDING"
            || status === "PARTIAL_FILLED"
    }

    function canApproveManualCheckpoint(order) {
        if (!order) {
            return false
        }

        if (canonicalOrderStatus(order) !== "REJECTED") {
            return false
        }

        if (String(order.ruleId || "").trim() !== "ManualCheckpointRule") {
            return false
        }

        var executionScopeId = String(order.executionScopeId || "").trim()
        var batchId = String(order.batchId || order.requiredBatchId || "").trim()
        return executionScopeId.length > 0 && batchId.length > 0
    }

    function canRetryManualCheckpoint(order) {
        if (!canApproveManualCheckpoint(order)) {
            return false
        }

        var symbol = String(order.symbol || "").trim()
        var side = String(order.side || "").trim().toUpperCase()
        var quantity = Number(order && order.qty !== undefined ? order.qty : 0)
        var cashAmount = Number(order && order.cashAmount !== undefined ? order.cashAmount : 0)
        return symbol.length > 0
            && side.length > 0
            && ((!isNaN(quantity) && quantity > 0) || (!isNaN(cashAmount) && cashAmount > 0))
    }

    function checkpointActionLabel(order) {
        return canRetryManualCheckpoint(order) ? "确认并重试" : "人工确认"
    }

    function canResumeExecutionPause(order) {
        if (!order) {
            return false
        }

        if (canonicalOrderStatus(order) !== "REJECTED") {
            return false
        }

        if (String(order.ruleId || "").trim() !== "RetryOrPauseRule") {
            return false
        }

        return String(order.executionScopeId || "").trim().length > 0
    }

    function canRetryExecutionPause(order) {
        if (!canResumeExecutionPause(order)) {
            return false
        }

        var symbol = String(order.symbol || "").trim()
        var side = String(order.side || "").trim().toUpperCase()
        var quantity = Number(order && order.qty !== undefined ? order.qty : 0)
        var cashAmount = Number(order && order.cashAmount !== undefined ? order.cashAmount : 0)
        return symbol.length > 0
            && side.length > 0
            && ((!isNaN(quantity) && quantity > 0) || (!isNaN(cashAmount) && cashAmount > 0))
    }

    function executionPauseActionLabel(order) {
        return canRetryExecutionPause(order) ? "恢复并重试" : "恢复执行"
    }

    onCurrentModeChanged: {
        modeContextChanged(currentMode, currentSymbol)
        root.syncModeReferencePriceType(currentMode)
        root.syncModeReferencePrice(currentMode)
    }
    onCurrentSymbolChanged: {
        modeContextChanged(currentMode, currentSymbol)
        root.syncModeReferencePriceType(currentMode)
        root.syncModeReferencePrice(currentMode)
    }
    onMarketSnapshotChanged: {
        root.syncAllReferencePriceTypes()
        root.syncAllReferencePrices()
    }
    onStockPriceTypeChanged: {
        root.syncModeReferencePriceType("stock")
        root.syncModeReferencePrice("stock")
    }
    onMarginBuyPriceTypeChanged: {
        root.syncModeReferencePriceType("margin_buy")
        root.syncModeReferencePrice("margin_buy")
    }
    onMarginSellPriceTypeChanged: {
        root.syncModeReferencePriceType("margin_sell")
        root.syncModeReferencePrice("margin_sell")
    }

    Component.onCompleted: {
        modeContextChanged(currentMode, currentSymbol)
        root.syncAllReferencePriceTypes()
        root.syncAllReferencePrices()
    }

    Timer {
        id: deferredOrderListTimer
        interval: 0
        running: true
        repeat: false
        onTriggered: root.deferredOrderListReady = true
    }

    Component {
        id: futuresTradeFormComponent

        Item {
            ColumnLayout {
                anchors.fill: parent
                spacing: compactMode ? 8 : 12

                Text {
                    text: "📌 期货合约"
                    color: "#8ba4c7"
                    font.pixelSize: compactSectionLabelFont
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: compactInputHeight
                    text: root.futuresCode
                    placeholderText: "如 RB2410"
                    color: "#f8fafc"
                    font.pixelSize: compactInputFont
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    topPadding: compactInputVerticalPadding
                    bottomPadding: compactInputVerticalPadding
                    leftPadding: compactInputHorizontalPadding
                    rightPadding: compactInputHorizontalPadding
                    onTextChanged: root.futuresCode = text
                    background: Rectangle {
                        radius: compactInputRadius
                        color: "#0f2238"
                        border.color: "#20364f"
                        border.width: 1
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Repeater {
                        model: root.quickButtons()

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactQuickButtonHeight
                            radius: compactInputRadius
                            color: "#10243a"
                            border.color: "#214362"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: "#dbeafe"
                                font.pixelSize: compactQuickButtonFont
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.applyQuickValue(modelData)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 8 : 12

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        text: root.futuresLots
                        placeholderText: "手数"
                        color: "#f8fafc"
                        font.pixelSize: compactInputFont
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: compactInputVerticalPadding
                        bottomPadding: compactInputVerticalPadding
                        leftPadding: compactInputHorizontalPadding
                        rightPadding: compactInputHorizontalPadding
                        onTextChanged: root.futuresLots = text
                        background: Rectangle {
                            radius: compactInputRadius
                            color: "#0f2238"
                            border.color: "#20364f"
                            border.width: 1
                        }
                    }

                    ComboBox {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: compactInputHeight
                        font.pixelSize: compactInputFont
                        model: ["市价", "限价"]
                        currentIndex: root.futuresPriceType === "market" ? 0 : 1
                        onActivated: root.futuresPriceType = currentIndex === 0 ? "market" : "limit"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Rectangle {
                        Layout.preferredWidth: compactInputHeight
                        Layout.preferredHeight: compactInputHeight
                        radius: compactInputRadius
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "-"
                            color: "#dbeafe"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.adjustModePrice("futures", -1)
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        text: root.futuresPrice
                        placeholderText: root.referenceText()
                        color: "#f8fafc"
                        font.pixelSize: compactInputFont
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: compactInputVerticalPadding
                        bottomPadding: compactInputVerticalPadding
                        leftPadding: compactInputHorizontalPadding
                        rightPadding: compactInputHorizontalPadding
                        onTextChanged: root.futuresPrice = text
                        background: Rectangle {
                            radius: compactInputRadius
                            color: "#0f2238"
                            border.color: "#20364f"
                            border.width: 1
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: compactInputHeight
                        Layout.preferredHeight: compactInputHeight
                        radius: compactInputRadius
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: "#dbeafe"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.adjustModePrice("futures", 1)
                        }
                    }
                }
            }
        }
    }

    Component {
        id: marginBuyTradeFormComponent

        Item {
            ColumnLayout {
                anchors.fill: parent
                spacing: compactMode ? 8 : 12

                Text {
                    text: "💳 融资买入"
                    color: "#8ba4c7"
                    font.pixelSize: compactSectionLabelFont
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: compactInputHeight
                    text: root.marginBuyCode
                    placeholderText: "股票代码"
                    color: "#f8fafc"
                    font.pixelSize: compactInputFont
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    topPadding: compactInputVerticalPadding
                    bottomPadding: compactInputVerticalPadding
                    leftPadding: compactInputHorizontalPadding
                    rightPadding: compactInputHorizontalPadding
                    onTextChanged: root.marginBuyCode = text
                    background: Rectangle {
                        radius: compactInputRadius
                        color: "#0f2238"
                        border.color: "#20364f"
                        border.width: 1
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.equityIdentitySummary("margin_buy")
                    color: root.equityIdentitySummaryColor("margin_buy")
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Repeater {
                        model: root.quickButtons()

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactQuickButtonHeight
                            radius: compactInputRadius
                            color: "#10243a"
                            border.color: "#214362"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: "#dbeafe"
                                font.pixelSize: compactQuickButtonFont
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.applyQuickValue(modelData)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 8 : 12

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        text: root.marginBuyShares
                        placeholderText: "股数"
                        color: "#f8fafc"
                        font.pixelSize: compactInputFont
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: compactInputVerticalPadding
                        bottomPadding: compactInputVerticalPadding
                        leftPadding: compactInputHorizontalPadding
                        rightPadding: compactInputHorizontalPadding
                        onTextChanged: root.marginBuyShares = text
                        background: Rectangle {
                            radius: compactInputRadius
                            color: "#0f2238"
                            border.color: "#20364f"
                            border.width: 1
                        }
                    }

                    ComboBox {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: compactInputHeight
                        font.pixelSize: compactInputFont
                        model: ["市价", "限价"]
                        currentIndex: root.marginBuyPriceType === "market" ? 0 : 1
                        onActivated: root.marginBuyPriceType = currentIndex === 0 ? "market" : "limit"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Rectangle {
                        Layout.preferredWidth: compactInputHeight
                        Layout.preferredHeight: compactInputHeight
                        radius: compactInputRadius
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "-"
                            color: "#dbeafe"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.adjustModePrice("margin_buy", -1)
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        text: root.marginBuyPrice
                        placeholderText: root.referenceText()
                        color: "#f8fafc"
                        font.pixelSize: compactInputFont
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: compactInputVerticalPadding
                        bottomPadding: compactInputVerticalPadding
                        leftPadding: compactInputHorizontalPadding
                        rightPadding: compactInputHorizontalPadding
                        onTextChanged: root.marginBuyPrice = text
                        background: Rectangle {
                            radius: compactInputRadius
                            color: "#0f2238"
                            border.color: "#20364f"
                            border.width: 1
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: compactInputHeight
                        Layout.preferredHeight: compactInputHeight
                        radius: compactInputRadius
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: "#dbeafe"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.adjustModePrice("margin_buy", 1)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Repeater {
                        model: root.equityQuickPriceButtons()

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactQuickButtonHeight
                            radius: compactInputRadius
                            color: "#10243a"
                            border.color: "#214362"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: root.equityShortcutButtonText(modelData, "margin_buy")
                                color: "#dbeafe"
                                font.pixelSize: compactQuickButtonFont
                                horizontalAlignment: Text.AlignHCenter
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.applyEquityPriceShortcut("margin_buy", modelData.code)
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.equityPriceSummary("margin_buy")
                    color: "#7ea1c5"
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: root.equityAmountSummary("margin_buy")
                    color: "#7ea1c5"
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    Component {
        id: marginSellTradeFormComponent

        Item {
            ColumnLayout {
                anchors.fill: parent
                spacing: compactMode ? 8 : 12

                Text {
                    text: "📉 融券卖出"
                    color: "#8ba4c7"
                    font.pixelSize: compactSectionLabelFont
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: compactInputHeight
                    text: root.marginSellCode
                    placeholderText: "股票代码"
                    color: "#f8fafc"
                    font.pixelSize: compactInputFont
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    topPadding: compactInputVerticalPadding
                    bottomPadding: compactInputVerticalPadding
                    leftPadding: compactInputHorizontalPadding
                    rightPadding: compactInputHorizontalPadding
                    onTextChanged: root.marginSellCode = text
                    background: Rectangle {
                        radius: compactInputRadius
                        color: "#0f2238"
                        border.color: "#20364f"
                        border.width: 1
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.equityIdentitySummary("margin_sell")
                    color: root.equityIdentitySummaryColor("margin_sell")
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Repeater {
                        model: root.quickButtons()

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactQuickButtonHeight
                            radius: compactInputRadius
                            color: "#10243a"
                            border.color: "#214362"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: "#dbeafe"
                                font.pixelSize: compactQuickButtonFont
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.applyQuickValue(modelData)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 8 : 12

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        text: root.marginSellShares
                        placeholderText: "股数"
                        color: "#f8fafc"
                        font.pixelSize: compactInputFont
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: compactInputVerticalPadding
                        bottomPadding: compactInputVerticalPadding
                        leftPadding: compactInputHorizontalPadding
                        rightPadding: compactInputHorizontalPadding
                        onTextChanged: root.marginSellShares = text
                        background: Rectangle {
                            radius: compactInputRadius
                            color: "#0f2238"
                            border.color: "#20364f"
                            border.width: 1
                        }
                    }

                    ComboBox {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: compactInputHeight
                        font.pixelSize: compactInputFont
                        model: ["市价", "限价"]
                        currentIndex: root.marginSellPriceType === "market" ? 0 : 1
                        onActivated: root.marginSellPriceType = currentIndex === 0 ? "market" : "limit"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Rectangle {
                        Layout.preferredWidth: compactInputHeight
                        Layout.preferredHeight: compactInputHeight
                        radius: compactInputRadius
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "-"
                            color: "#dbeafe"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.adjustModePrice("margin_sell", -1)
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        text: root.marginSellPrice
                        placeholderText: root.referenceText()
                        color: "#f8fafc"
                        font.pixelSize: compactInputFont
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: compactInputVerticalPadding
                        bottomPadding: compactInputVerticalPadding
                        leftPadding: compactInputHorizontalPadding
                        rightPadding: compactInputHorizontalPadding
                        onTextChanged: root.marginSellPrice = text
                        background: Rectangle {
                            radius: compactInputRadius
                            color: "#0f2238"
                            border.color: "#20364f"
                            border.width: 1
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: compactInputHeight
                        Layout.preferredHeight: compactInputHeight
                        radius: compactInputRadius
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: "#dbeafe"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.adjustModePrice("margin_sell", 1)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Repeater {
                        model: root.equityQuickPriceButtons()

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactQuickButtonHeight
                            radius: compactInputRadius
                            color: "#10243a"
                            border.color: "#214362"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: root.equityShortcutButtonText(modelData, "margin_sell")
                                color: "#dbeafe"
                                font.pixelSize: compactQuickButtonFont
                                horizontalAlignment: Text.AlignHCenter
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.applyEquityPriceShortcut("margin_sell", modelData.code)
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.equityPriceSummary("margin_sell")
                    color: "#7ea1c5"
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: root.equityAmountSummary("margin_sell")
                    color: "#7ea1c5"
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    Component {
        id: optionsTradeFormComponent

        Item {
            ColumnLayout {
                anchors.fill: parent
                spacing: compactMode ? 8 : 12

                Text {
                    text: "🎯 期权合约"
                    color: "#8ba4c7"
                    font.pixelSize: compactSectionLabelFont
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: compactInputHeight
                    text: root.optionCode
                    placeholderText: "如 10004411"
                    color: "#f8fafc"
                    font.pixelSize: compactInputFont
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    topPadding: compactInputVerticalPadding
                    bottomPadding: compactInputVerticalPadding
                    leftPadding: compactInputHorizontalPadding
                    rightPadding: compactInputHorizontalPadding
                    onTextChanged: root.optionCode = text
                    background: Rectangle {
                        radius: compactInputRadius
                        color: "#0f2238"
                        border.color: "#20364f"
                        border.width: 1
                    }
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: compactInputHeight
                    text: root.optionUnderlying
                    placeholderText: "标的代码"
                    color: "#f8fafc"
                    font.pixelSize: compactInputFont
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    topPadding: compactInputVerticalPadding
                    bottomPadding: compactInputVerticalPadding
                    leftPadding: compactInputHorizontalPadding
                    rightPadding: compactInputHorizontalPadding
                    onTextChanged: root.optionUnderlying = text
                    background: Rectangle {
                        radius: compactInputRadius
                        color: "#0f2238"
                        border.color: "#20364f"
                        border.width: 1
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Repeater {
                        model: root.quickButtons()

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactQuickButtonHeight
                            radius: compactInputRadius
                            color: "#10243a"
                            border.color: "#214362"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: "#dbeafe"
                                font.pixelSize: compactQuickButtonFont
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.applyQuickValue(modelData)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 8 : 12

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        text: root.optionLots
                        placeholderText: "手数(1/1, 1/2...)"
                        color: "#f8fafc"
                        font.pixelSize: compactInputFont
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: compactInputVerticalPadding
                        bottomPadding: compactInputVerticalPadding
                        leftPadding: compactInputHorizontalPadding
                        rightPadding: compactInputHorizontalPadding
                        onTextChanged: root.optionLots = text
                        background: Rectangle {
                            radius: compactInputRadius
                            color: "#0f2238"
                            border.color: "#20364f"
                            border.width: 1
                        }
                    }

                    ComboBox {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: compactInputHeight
                        font.pixelSize: compactInputFont
                        model: ["市价", "限价"]
                        currentIndex: root.optionPriceType === "market" ? 0 : 1
                        onActivated: root.optionPriceType = currentIndex === 0 ? "market" : "limit"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Rectangle {
                        Layout.preferredWidth: compactInputHeight
                        Layout.preferredHeight: compactInputHeight
                        radius: compactInputRadius
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "-"
                            color: "#dbeafe"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.adjustModePrice("options", -1)
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        text: root.optionPrice
                        placeholderText: root.referenceText()
                        color: "#f8fafc"
                        font.pixelSize: compactInputFont
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        topPadding: compactInputVerticalPadding
                        bottomPadding: compactInputVerticalPadding
                        leftPadding: compactInputHorizontalPadding
                        rightPadding: compactInputHorizontalPadding
                        onTextChanged: root.optionPrice = text
                        background: Rectangle {
                            radius: compactInputRadius
                            color: "#0f2238"
                            border.color: "#20364f"
                            border.width: 1
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: compactInputHeight
                        Layout.preferredHeight: compactInputHeight
                        radius: compactInputRadius
                        color: "#10243a"
                        border.color: "#214362"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: "#dbeafe"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.adjustModePrice("options", 1)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 8 : 12

                    ComboBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        font.pixelSize: compactInputFont
                        model: ["认购期权", "认沽期权"]
                        currentIndex: root.optionType === "call" ? 0 : 1
                        onActivated: root.optionType = currentIndex === 0 ? "call" : "put"
                    }

                    ComboBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: compactInputHeight
                        font.pixelSize: compactInputFont
                        model: ["当月", "下月", "季月"]
                        currentIndex: root.optionExpiry === "当月" ? 0 : root.optionExpiry === "下月" ? 1 : 2
                        onActivated: root.optionExpiry = currentIndex === 0 ? "当月" : currentIndex === 1 ? "下月" : "季月"
                    }
                }
            }
        }
    }

    Component {
        id: pendingOrdersContentComponent

        Item {
            ListView {
                anchors.fill: parent
                anchors.margins: compactMode ? 6 : 8
                clip: true
                spacing: compactMode ? 4 : 6
                model: root.pendingOrders

                delegate: Rectangle {
                    property var orderData: modelData
                    width: ListView.view.width
                    height: compactOrderRowHeight
                    radius: compactMode ? 12 : 14
                    color: "#0b1625"
                    border.color: orderData.status === "已撤" ? "#5a2a2a" : "#164b5c"
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: compactMode ? 8 : 12
                        spacing: compactMode ? 6 : 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: orderData.symbol + "  " + orderData.action + "  " + root.orderHeadlineAmount(orderData)
                                color: "#0ff"
                                font.pixelSize: compactMetaFont
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Text {
                                text: root.orderPriceSummary(orderData)
                                    + "  ·  " + orderData.time
                                    + "  ·  " + orderData.status
                                    + (root.orderFilledSummary(orderData).length > 0 ? "  ·  " + root.orderFilledSummary(orderData) : "")
                                color: "#8aaeff"
                                font.pixelSize: compactMode ? 10 : 11
                                elide: Text.ElideRight
                            }

                            Text {
                                visible: root.orderAuxiliarySummary(orderData).length > 0
                                text: root.orderAuxiliarySummary(orderData)
                                color: "#5f85a8"
                                font.pixelSize: compactMode ? 9 : 10
                                elide: Text.ElideMiddle
                            }
                        }

                        Rectangle {
                            visible: root.canCancelOrder(orderData)
                            radius: compactMode ? 12 : 14
                            color: "#3f1d24"
                            border.color: "#ff8888"
                            border.width: 1
                            implicitWidth: compactMode ? 56 : 72
                            implicitHeight: compactMode ? 24 : 30

                            Text {
                                anchors.centerIn: parent
                                text: "撤单"
                                color: "#ff8888"
                                font.pixelSize: compactMode ? 10 : 11
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.cancelOrderRequested(orderData.cancelOrderId || orderData.id)
                            }
                        }

                        Rectangle {
                            visible: root.canApproveManualCheckpoint(orderData)
                            radius: compactMode ? 12 : 14
                            color: "#15334a"
                            border.color: "#67E8F9"
                            border.width: 1
                            implicitWidth: compactMode ? 88 : 110
                            implicitHeight: compactMode ? 24 : 30

                            Text {
                                anchors.centerIn: parent
                                text: root.checkpointActionLabel(orderData)
                                color: "#67E8F9"
                                font.pixelSize: compactMode ? 10 : 11
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.approveCheckpointRequested(orderData, root.canRetryManualCheckpoint(orderData))
                            }
                        }

                        Rectangle {
                            visible: root.canResumeExecutionPause(orderData)
                            radius: compactMode ? 12 : 14
                            color: "#3a2a14"
                            border.color: "#FBBF24"
                            border.width: 1
                            implicitWidth: compactMode ? 88 : 110
                            implicitHeight: compactMode ? 24 : 30

                            Text {
                                anchors.centerIn: parent
                                text: root.executionPauseActionLabel(orderData)
                                color: "#FBBF24"
                                font.pixelSize: compactMode ? 10 : 11
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.resumeExecutionPauseRequested(orderData, root.canRetryExecutionPause(orderData))
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: root.pendingOrders.length === 0
                text: "暂无委托订单"
                color: "#4a6a8a"
                font.pixelSize: compactMetaFont
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 27
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0d1728" }
            GradientStop { position: 1.0; color: "#08101d" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: compactMode ? 16 : 24
        spacing: compactMode ? 10 : 16

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 4

                Text {
                    text: "交易执行"
                    color: "#f8fafc"
                    font.pixelSize: compactTitleFont
                    font.weight: Font.DemiBold
                }

                Text {
                    text: "下单 / 撤单 / 当前委托"
                    color: "#8ba4c7"
                    font.pixelSize: compactBodyFont
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                radius: compactMode ? 12 : 14
                color: "#10243a"
                border.color: "#1d446d"
                border.width: 1
                implicitWidth: compactMode ? 108 : 126
                implicitHeight: compactMode ? 40 : 52

                Column {
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        text: "可用资金"
                        color: "#89a2c8"
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: "¥" + Number(root.availableCapital).toLocaleString(Qt.locale(), "f", 0)
                        color: "#e2e8f0"
                        font.pixelSize: compactMode ? 12 : 14
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: compactMode ? 6 : 8

            Repeater {
                model: root.tabs

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: compactChipHeight
                    radius: compactMode ? 12 : 18
                    color: index === root.currentTabIndex ? "#14f1ff" : "#0f1b2d"
                    border.color: index === root.currentTabIndex ? "#14f1ff" : "#1d3147"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: modelData.icon + " " + modelData.label
                        color: index === root.currentTabIndex ? "#03111a" : "#b2c5de"
                        font.pixelSize: compactMetaFont
                        font.weight: Font.Bold
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentTabIndex = index
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: compactMode ? 16 : 20
            color: "#0c1828"
            border.color: "#1e3147"
            border.width: 1
            implicitHeight: compactMode ? 70 : 94

            RowLayout {
                anchors.fill: parent
                anchors.margins: compactMode ? 12 : 16
                spacing: compactMode ? 10 : 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "当前模式"
                        color: "#8ba4c7"
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: root.currentModeDisplayTitle()
                        color: "#eff6ff"
                        font.pixelSize: compactMode ? 13 : 16
                        font.weight: Font.DemiBold
                    }
                }

                ColumnLayout {
                    spacing: 4

                    Text {
                        text: "参考价格"
                        color: "#8ba4c7"
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: root.modePriceText()
                        color: "#0ff"
                        font.pixelSize: compactMode ? 17 : 22
                        font.weight: Font.Bold
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: compactMode ? 286 : 360
            radius: compactMode ? 16 : 20
            color: "#091321"
            border.color: "#1b3047"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: compactMode ? 12 : 18
                spacing: compactMode ? 8 : 14

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.currentTabIndex

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: compactMode ? 8 : 12

                            Text {
                                text: "📌 股票代码"
                                color: "#8ba4c7"
                                font.pixelSize: compactSectionLabelFont
                            }

                            TextField {
                                Layout.fillWidth: true
                                Layout.preferredHeight: compactInputHeight
                                text: root.stockCode
                                placeholderText: "如 000001"
                                color: "#f8fafc"
                                font.pixelSize: compactInputFont
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                topPadding: compactInputVerticalPadding
                                bottomPadding: compactInputVerticalPadding
                                leftPadding: compactInputHorizontalPadding
                                rightPadding: compactInputHorizontalPadding
                                onTextChanged: root.stockCode = text
                                background: Rectangle {
                                    radius: compactInputRadius
                                    color: "#0f2238"
                                    border.color: "#20364f"
                                    border.width: 1
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.equityIdentitySummary("stock")
                                color: root.equityIdentitySummaryColor("stock")
                                font.pixelSize: compactMetaFont
                                horizontalAlignment: Text.AlignHCenter
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: compactMode ? 6 : 8

                                Repeater {
                                    model: root.quickButtons()

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: compactQuickButtonHeight
                                        radius: compactInputRadius
                                        color: "#10243a"
                                        border.color: "#214362"
                                        border.width: 1

                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData
                                            color: "#dbeafe"
                                            font.pixelSize: compactQuickButtonFont
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.applyQuickValue(modelData)
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: compactMode ? 8 : 12

                                TextField {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: compactInputHeight
                                    text: root.stockShares
                                    placeholderText: "股数(100倍数)"
                                    color: "#f8fafc"
                                    font.pixelSize: compactInputFont
                                    horizontalAlignment: TextInput.AlignHCenter
                                    verticalAlignment: TextInput.AlignVCenter
                                    topPadding: compactInputVerticalPadding
                                    bottomPadding: compactInputVerticalPadding
                                    leftPadding: compactInputHorizontalPadding
                                    rightPadding: compactInputHorizontalPadding
                                    onTextChanged: root.stockShares = text
                                    background: Rectangle {
                                        radius: compactInputRadius
                                        color: "#0f2238"
                                        border.color: "#20364f"
                                        border.width: 1
                                    }
                                }

                                ComboBox {
                                    Layout.preferredWidth: 120
                                    Layout.preferredHeight: compactInputHeight
                                    font.pixelSize: compactInputFont
                                    model: ["市价", "限价"]
                                    currentIndex: root.stockPriceType === "market" ? 0 : 1
                                    onActivated: root.stockPriceType = currentIndex === 0 ? "market" : "limit"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: compactMode ? 6 : 8

                                Rectangle {
                                    Layout.preferredWidth: compactInputHeight
                                    Layout.preferredHeight: compactInputHeight
                                    radius: compactInputRadius
                                    color: "#10243a"
                                    border.color: "#214362"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "-"
                                        color: "#dbeafe"
                                        font.pixelSize: compactButtonFont
                                        font.weight: Font.DemiBold
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.adjustModePrice("stock", -1)
                                    }
                                }

                                TextField {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: compactInputHeight
                                    text: root.stockPrice
                                    placeholderText: root.referenceText()
                                    color: "#f8fafc"
                                    font.pixelSize: compactInputFont
                                    horizontalAlignment: TextInput.AlignHCenter
                                    verticalAlignment: TextInput.AlignVCenter
                                    topPadding: compactInputVerticalPadding
                                    bottomPadding: compactInputVerticalPadding
                                    leftPadding: compactInputHorizontalPadding
                                    rightPadding: compactInputHorizontalPadding
                                    onTextChanged: root.stockPrice = text
                                    background: Rectangle {
                                        radius: compactInputRadius
                                        color: "#0f2238"
                                        border.color: "#20364f"
                                        border.width: 1
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: compactInputHeight
                                    Layout.preferredHeight: compactInputHeight
                                    radius: compactInputRadius
                                    color: "#10243a"
                                    border.color: "#214362"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "+"
                                        color: "#dbeafe"
                                        font.pixelSize: compactButtonFont
                                        font.weight: Font.DemiBold
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.adjustModePrice("stock", 1)
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: compactMode ? 6 : 8

                                Repeater {
                                    model: root.equityQuickPriceButtons()

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: compactQuickButtonHeight
                                        radius: compactInputRadius
                                        color: "#10243a"
                                        border.color: "#214362"
                                        border.width: 1

                                        Text {
                                            anchors.centerIn: parent
                                            text: root.equityShortcutButtonText(modelData, "stock")
                                            color: "#dbeafe"
                                            font.pixelSize: compactQuickButtonFont
                                            horizontalAlignment: Text.AlignHCenter
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.applyEquityPriceShortcut("stock", modelData.code)
                                        }
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.equityPriceSummary("stock")
                                color: "#7ea1c5"
                                font.pixelSize: compactMetaFont
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.equityAmountSummary("stock")
                                color: "#7ea1c5"
                                font.pixelSize: compactMetaFont
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: root.currentTabIndex === 1
                        asynchronous: true
                        sourceComponent: futuresTradeFormComponent
                    }

                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: root.currentTabIndex === 2
                        asynchronous: true
                        sourceComponent: marginBuyTradeFormComponent
                    }

                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: root.currentTabIndex === 3
                        asynchronous: true
                        sourceComponent: marginSellTradeFormComponent
                    }

                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: root.currentTabIndex === 4
                        asynchronous: true
                        sourceComponent: optionsTradeFormComponent
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: compactMode ? 14 : 18
            color: "#0b1625"
            border.color: "#1d3147"
            border.width: 1
            implicitHeight: root.currentMode === "futures" || root.currentMode === "options" || root.currentMode === "margin_buy"
                ? (compactMode ? 74 : 112)
                : (compactMode ? 42 : 64)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: compactMode ? 10 : 14
                spacing: compactMode ? 6 : 10

                RowLayout {
                    visible: root.currentMode === "stock"
                    spacing: compactMode ? 8 : 12

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactActionHeight
                        radius: compactActionRadius
                        color: "#ff6a00"

                        Text {
                            anchors.centerIn: parent
                            text: "买入"
                            color: "white"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.Bold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.submit("buy")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactActionHeight
                        radius: compactActionRadius
                        readonly property bool sellEnabled: !root.positionAvailabilityError
                        color: sellEnabled ? "#00cc88" : "#334155"

                        Text {
                            anchors.centerIn: parent
                            text: "卖出"
                            color: parent.sellEnabled ? "white" : "#94a3b8"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.Bold
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: parent.sellEnabled
                            cursorShape: parent.sellEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: root.submit("sell")
                        }
                    }
                }

                ColumnLayout {
                    visible: root.currentMode === "futures"
                    spacing: compactMode ? 6 : 10

                    RowLayout {
                        spacing: compactMode ? 8 : 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#ff6a00"

                            Text {
                                anchors.centerIn: parent
                                text: "开多"
                                color: "white"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("long")
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#00cc88"

                            Text {
                                anchors.centerIn: parent
                                text: "开空"
                                color: "white"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("short")
                            }
                        }
                    }

                    RowLayout {
                        spacing: compactMode ? 8 : 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#334155"

                            Text {
                                anchors.centerIn: parent
                                text: "平多"
                                color: "#ffccd5"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("closeLong")
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#334155"

                            Text {
                                anchors.centerIn: parent
                                text: "平空"
                                color: "#ffccd5"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("closeShort")
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: root.currentMode === "margin_buy"
                    spacing: compactMode ? 8 : 12

                    RowLayout {
                        spacing: compactMode ? 8 : 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#8b5cf6"

                            Text {
                                anchors.centerIn: parent
                                text: "融资买入"
                                color: "white"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("marginBuy")
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            readonly property bool closeLongEnabled: !root.positionAvailabilityError
                            color: closeLongEnabled ? "#00cc88" : "#334155"

                            Text {
                                anchors.centerIn: parent
                                text: "卖出平仓"
                                color: parent.closeLongEnabled ? "white" : "#94a3b8"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: parent.closeLongEnabled
                                cursorShape: parent.closeLongEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: root.submit("closeLong")
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactActionHeight
                        radius: compactActionRadius
                        color: "#3b82f6"

                        Text {
                            anchors.centerIn: parent
                            text: "现金还款"
                            color: "white"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.Bold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.submit("repay")
                        }
                    }
                }

                RowLayout {
                    visible: root.currentMode === "margin_sell"
                    spacing: compactMode ? 8 : 12

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactActionHeight
                        radius: compactActionRadius
                        color: "#8b5cf6"

                        Text {
                            anchors.centerIn: parent
                            text: "融券卖出"
                            color: "white"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.Bold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.submit("marginSell")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactActionHeight
                        radius: compactActionRadius
                        color: "#3b82f6"

                        Text {
                            anchors.centerIn: parent
                            text: "现券还券"
                            color: "white"
                            font.pixelSize: compactButtonFont
                            font.weight: Font.Bold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.submit("returnStock")
                        }
                    }
                }

                ColumnLayout {
                    visible: root.currentMode === "options"
                    spacing: compactMode ? 6 : 10

                    RowLayout {
                        spacing: compactMode ? 8 : 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#f59e0b"

                            Text {
                                anchors.centerIn: parent
                                text: "买入开仓"
                                color: "white"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("optionBuy")
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#7c3aed"

                            Text {
                                anchors.centerIn: parent
                                text: "卖出平仓"
                                color: "white"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("optionSell")
                            }
                        }
                    }

                    RowLayout {
                        spacing: compactMode ? 8 : 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#334155"

                            Text {
                                anchors.centerIn: parent
                                text: "备兑开仓"
                                color: "#ffccd5"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("optionClose")
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#475569"

                            Text {
                                anchors.centerIn: parent
                                text: "备兑平仓"
                                color: "#dbeafe"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("optionCoveredClose")
                            }
                        }
                    }

                    RowLayout {
                        spacing: compactMode ? 8 : 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactActionHeight
                            radius: compactActionRadius
                            color: "#334155"

                            Text {
                                anchors.centerIn: parent
                                text: "行权"
                                color: "#ffccd5"
                                font.pixelSize: compactButtonFont
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submit("optionExercise")
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        Text {
            text: "执行回报"
            color: "#ff8888"
            font.pixelSize: compactMetaFont
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 18
            color: "#08111e"
            border.color: "#182a40"
            border.width: 1

            Loader {
                anchors.fill: parent
                active: root.deferredOrderListReady
                asynchronous: true
                sourceComponent: pendingOrdersContentComponent
            }

            Column {
                anchors.centerIn: parent
                spacing: compactMode ? 10 : 12
                visible: !root.deferredOrderListReady

                Repeater {
                    model: 3

                    Rectangle {
                        width: compactMode ? 260 : 320
                        height: compactMode ? 18 : 22
                        radius: 9
                        color: "#10243a"
                        opacity: index === 0 ? 0.9 : index === 1 ? 0.65 : 0.45
                    }
                }
            }
        }
    }

    Rectangle {
        visible: root.toastMessage.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        radius: compactMode ? 14 : 18
        color: root.toastError ? "#3b0d0d" : "#041f24"
        border.color: root.toastError ? "#ff6b6b" : "#0ff"
        border.width: 1
        implicitHeight: compactMode ? 34 : 38
        implicitWidth: toastLabel.implicitWidth + 28

        Text {
            id: toastLabel
            anchors.centerIn: parent
            text: root.toastMessage
            color: root.toastError ? "#ffd5d5" : "#b6feff"
            font.pixelSize: compactMetaFont
        }
    }
}
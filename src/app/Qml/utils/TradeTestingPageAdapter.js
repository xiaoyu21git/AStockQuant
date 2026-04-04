function safeNumber(value, fallback) {
    var numeric = Number(value)
    if (isNaN(numeric)) {
        return fallback === undefined ? 0 : fallback
    }
    return numeric
}

function normalizeSymbol(symbol) {
    var text = String(symbol || "").trim().toUpperCase()
    if (text.indexOf("SHSE.") === 0) {
        return text.substring(5) + ".SH"
    }
    if (text.indexOf("SZSE.") === 0) {
        return text.substring(5) + ".SZ"
    }
    if (text.indexOf("BSE.") === 0) {
        return text.substring(4) + ".BJ"
    }
    return text
}

function resolveQuote(symbol, marketData, marketDataService) {
    var query = normalizeSymbol(symbol)
    if (marketDataService && typeof marketDataService.resolveInstrument === "function" && query.length > 0) {
        var resolved = marketDataService.resolveInstrument(query)
        if (resolved && resolved.symbol) {
            return resolved
        }
    }
    for (var index = 0; index < (marketData ? marketData.length : 0); ++index) {
        var quote = marketData[index] || ({})
        if (normalizeSymbol(quote.symbol) === query) {
            return quote
        }
    }
    return ({ symbol: query })
}

function toneForChange(change) {
    var numeric = safeNumber(change, 0)
    if (numeric > 0) {
        return "#ff6b6b"
    }
    if (numeric < 0) {
        return "#2ec27e"
    }
    return "#d7dde8"
}

function formatCurrency(currencySymbol, value) {
    return String(currencySymbol || "") + Number(safeNumber(value, 0)).toLocaleString(Qt.locale(), "f", 2)
}

function instrumentLabel(symbol, name) {
    var code = String(symbol || "--")
    var title = String(name || "")
    return title.length > 0 ? (code + " " + title) : code
}

function signedPercent(value) {
    var numeric = safeNumber(value, 0)
    return (numeric >= 0 ? "+" : "") + numeric.toFixed(2) + "%"
}

function displayOrderStatus(status) {
    var text = String(status || "").toUpperCase()
    if (text === "REQUESTED") { return "已请求" }
    if (text === "SUBMITTED") { return "已报" }
    if (text === "PENDING") { return "待处理" }
    if (text === "PARTIAL_FILLED") { return "部分成交" }
    if (text === "FILLED") { return "已成交" }
    if (text === "CANCELLED") { return "已撤单" }
    if (text === "REJECTED") { return "已拒绝" }
    return text || "--"
}

function displayOrderSide(side) {
    var text = String(side || "").toUpperCase()
    if (text === "BUY") { return "买入" }
    if (text === "SELL") { return "卖出" }
    return text || "--"
}

function detectBoardName(symbol) {
    var normalized = normalizeSymbol(symbol)
    var code = normalized.split(".")[0]
    if (normalized.indexOf(".BJ") > 0) { return "北交所" }
    if (code.indexOf("688") === 0) { return "科创板" }
    if (code.indexOf("300") === 0) { return "创业板" }
    if (normalized.indexOf(".SH") > 0) { return "沪市主板" }
    if (normalized.indexOf(".SZ") > 0) { return "深市主板" }
    return "A股"
}

function detectIndustryName(quote) {
    var nameText = String((quote || {}).name || "")
    if (nameText.indexOf("银行") !== -1) { return "银行" }
    if (nameText.indexOf("酒") !== -1) { return "白酒" }
    if (nameText.indexOf("电子") !== -1 || nameText.indexOf("芯") !== -1) { return "半导体" }
    if (nameText.indexOf("电") !== -1 || nameText.indexOf("新能源") !== -1) { return "新能源" }
    return "宽基跟踪"
}

function buildAccountCards(accountSnapshot, positions, currencySymbol) {
    var totalAsset = safeNumber(accountSnapshot.totalAsset, 0)
    var availableCash = safeNumber(accountSnapshot.availableCash, 0)
    var marketValue = safeNumber(accountSnapshot.marketValue, 0)
    var realizedPnl = safeNumber(accountSnapshot.realizedPnl, 0)
    return [
        { label: "账户资产", value: formatCurrency(currencySymbol, totalAsset), detail: "可用 " + formatCurrency(currencySymbol, availableCash), tone: "#f3f4f6" },
        { label: "股票市值", value: formatCurrency(currencySymbol, marketValue), detail: "持仓 " + String(positions ? positions.length : 0) + " 只", tone: "#f3f4f6" },
        { label: "当日盈亏", value: (realizedPnl >= 0 ? "+" : "-") + formatCurrency(currencySymbol, Math.abs(realizedPnl)), detail: "未实现 " + formatCurrency(currencySymbol, safeNumber(accountSnapshot.unrealizedPnl, 0)), tone: toneForChange(realizedPnl) }
    ]
}

function buildInstrumentFacts(quote, currencySymbol) {
    var currentPrice = safeNumber(quote.price || quote.close, 0)
    return [
        { label: "证券名称", value: String(quote.name || "待锁定") },
        { label: "交易所", value: detectBoardName(quote.symbol) },
        { label: "昨收", value: currentPrice > 0 ? formatCurrency(currencySymbol, quote.preClose || quote.pre_close || quote.close || 0) : "--" },
        { label: "开盘", value: currentPrice > 0 ? formatCurrency(currencySymbol, quote.open || currentPrice) : "--" },
        { label: "振幅", value: signedPercent(quote.change || 0) },
        { label: "成交额", value: formatCurrency(currencySymbol, quote.amount || 0) }
    ]
}

function buildIndustryFacts(quote) {
    var boardName = detectBoardName(quote.symbol)
    var industryName = detectIndustryName(quote)
    return [
        { label: "行业赛道", value: industryName },
        { label: "交易风格", value: boardName.indexOf("科创") !== -1 || boardName.indexOf("创业") !== -1 ? "弹性成长" : "核心权重" },
        { label: "盘口特征", value: safeNumber(quote.volume || 0) > 0 ? "实时撮合活跃" : "等待实时推送" },
        { label: "执行建议", value: safeNumber(quote.change || 0) >= 0 ? "优先盯盘口承接" : "关注回落吸纳" }
    ]
}

function buildLevel5Book(quote) {
    var currentPrice = safeNumber(quote.price || quote.close, 0)
    if (currentPrice <= 0) { currentPrice = safeNumber(quote.preClose || quote.pre_close, 0) }
    var rows = []
    var askDefaults = [5, 4, 3, 2, 1]
    var bidDefaults = [1, 2, 3, 4, 5]
    var step = currentPrice > 20 ? 0.05 : 0.01
    for (var level = 0; level < 5; ++level) {
        var askIndex = askDefaults[level]
        var bidIndex = bidDefaults[level]
        rows.push({
            askLabel: "卖" + String(askIndex),
            askPrice: safeNumber(quote["askPrice" + String(askIndex)], currentPrice + step * askIndex).toFixed(2),
            askVolume: Math.round(safeNumber(quote["askVolume" + String(askIndex)], 8 + level * 2)) + "手",
            bidLabel: "买" + String(bidIndex),
            bidPrice: safeNumber(quote["bidPrice" + String(bidIndex)], currentPrice - step * bidIndex).toFixed(2),
            bidVolume: Math.round(safeNumber(quote["bidVolume" + String(bidIndex)], 9 + level * 2)) + "手"
        })
    }
    return rows
}

function buildPositionRows(positions, currencySymbol, maxCount) {
    var rows = []
    var limit = Math.min(positions ? positions.length : 0, maxCount || 5)
    for (var index = 0; index < limit; ++index) {
        var item = positions[index] || ({})
        rows.push({
            symbol: String(item.symbol || "--"),
            name: String(item.name || ""),
            shares: String(safeNumber(item.shares, 0)),
            available: String(safeNumber(item.availableQuantity, 0)),
            price: formatCurrency(currencySymbol, item.lastPrice || 0),
            cost: formatCurrency(currencySymbol, item.avgPrice || 0),
            marketValue: formatCurrency(currencySymbol, item.currentValue || 0),
            pnlText: (safeNumber(item.pnl, 0) >= 0 ? "+" : "-") + formatCurrency(currencySymbol, Math.abs(safeNumber(item.pnl, 0))),
            pnlRate: signedPercent(item.pnlRate || 0),
            tone: toneForChange(item.pnl || 0)
        })
    }
    return rows
}

function buildOrderRows(recentOrders, recentStatuses, currencySymbol, maxCount) {
    var rows = []
    var merged = []
    for (var i = 0; i < (recentStatuses ? recentStatuses.length : 0); ++i) { merged.push(recentStatuses[i]) }
    for (var j = 0; j < (recentOrders ? recentOrders.length : 0); ++j) { merged.push(recentOrders[j]) }
    var limit = Math.min(merged.length, maxCount || 6)
    for (var index = 0; index < limit; ++index) {
        var item = merged[index] || ({})
        rows.push({
            symbol: String(item.symbol || "--"),
            side: displayOrderSide(item.side),
            price: formatCurrency(currencySymbol, item.price || 0),
            quantity: String(safeNumber(item.quantity, 0)) + "股",
            status: displayOrderStatus(item.status),
            tone: String(item.side || "").toUpperCase() === "BUY" ? "#ff6b6b" : "#2ec27e"
        })
    }
    return rows
}

function buildStrategyCards(tradeExecutionService, positionAccountService, latestOrder) {
    return [
        { title: "执行引擎", value: tradeExecutionService && tradeExecutionService.initialized ? "已联通" : "待初始化", detail: tradeExecutionService && tradeExecutionService.initialized ? "支持真实委托链路" : "当前仅待命", tone: tradeExecutionService && tradeExecutionService.initialized ? "#f6c85f" : "#94a3b8" },
        { title: "订单回流", value: positionAccountService && positionAccountService.recentOrderStatuses ? String(positionAccountService.recentOrderStatuses.length) + " 条" : "0 条", detail: latestOrder && latestOrder.status ? displayOrderStatus(latestOrder.status) : "等待回报", tone: "#8ad6cc" },
        { title: "策略席位", value: latestOrder && latestOrder.strategyName ? String(latestOrder.strategyName) : "manual_test", detail: latestOrder && latestOrder.symbol ? instrumentLabel(latestOrder.symbol, latestOrder.name) : "尚无最近委托", tone: "#f0a35e" }
    ]
}

function buildLogEntries(quote, latestOrder, marketDataService, tradeExecutionService, positionAccountService, actionMessage) {
    var entries = []
    entries.push({ level: "行情", text: quote && quote.symbol ? (instrumentLabel(quote.symbol, quote.name) + " 已进入监控") : "等待锁定标的", tone: "#8ad6cc" })
    entries.push({ level: "链路", text: marketDataService && marketDataService.hasLiveData ? "实时快照已接入" : "行情链路等待推送", tone: "#d7dde8" })
    entries.push({ level: "执行", text: tradeExecutionService && tradeExecutionService.initialized ? "交易执行服务已就绪" : "执行服务未初始化", tone: "#f6c85f" })
    entries.push({ level: "回报", text: positionAccountService && positionAccountService.recentOrderStatuses && positionAccountService.recentOrderStatuses.length > 0 ? "已收到最新状态回报" : "尚未收到订单状态回报", tone: "#f0a35e" })
    if (latestOrder && latestOrder.orderId) {
        entries.push({ level: "委托", text: instrumentLabel(latestOrder.symbol, latestOrder.name) + " / " + displayOrderSide(latestOrder.side) + " / " + displayOrderStatus(latestOrder.status), tone: String(latestOrder.side || "").toUpperCase() === "BUY" ? "#ff6b6b" : "#2ec27e" })
    }
    if (String(actionMessage || "").length > 0) {
        entries.unshift({ level: "操作", text: String(actionMessage), tone: "#f3f4f6" })
    }
    return entries.slice(0, 6)
}

function normalizeQuantity(value) {
    var quantity = parseInt(String(value || "0"), 10)
    if (isNaN(quantity) || quantity <= 0) { return 0 }
    return quantity
}

function normalizeLots(value) {
    var lots = parseInt(String(value || "0"), 10)
    if (isNaN(lots) || lots <= 0) { return 0 }
    return lots
}

function quantityFromLots(value) {
    return normalizeLots(value) * 100
}

function lotsFromQuantity(value) {
    var quantity = normalizeQuantity(value)
    return quantity > 0 ? Math.max(1, Math.round(quantity / 100)) : 0
}

function isBoardLotValid(value) {
    var quantity = normalizeQuantity(value)
    return quantity >= 100 && quantity % 100 === 0
}

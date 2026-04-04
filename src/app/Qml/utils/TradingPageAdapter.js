// ============================================================
// 闪电交易组件 - QML 可用 JavaScript 模块
// 包含: 股票/期货/融资/融券/期权交易 + 五档行情 + L2逐笔
// 使用方式: 在 QML 中 import 此文件，或直接作为 JS 资源加载
// ============================================================

//.pragma library

// 订单存储
var orders = [];
var nextOrderId = 1;

// 行情数据
var currentPrice = 12.58;
var currentFuturesPrice = 3650;
var lastPrice = 12.58;
var tickHistory = [];
var currentDepthLevels = 5;

// 回调函数 (由 QML 界面设置)
var uiCallbacks = {
    onOrderListChanged: null,   // 订单列表变化时调用
    onMarketDataChanged: null,  // 行情数据变化时调用
    onDepthChanged: null,       // 五档盘口变化时调用
    onTickChanged: null,        // 逐笔成交变化时调用
    onToast: null               // 提示消息
};

// ========== 初始化设置 ==========
function setCallbacks(callbacks) {
    if (callbacks.onOrderListChanged) uiCallbacks.onOrderListChanged = callbacks.onOrderListChanged;
    if (callbacks.onMarketDataChanged) uiCallbacks.onMarketDataChanged = callbacks.onMarketDataChanged;
    if (callbacks.onDepthChanged) uiCallbacks.onDepthChanged = callbacks.onDepthChanged;
    if (callbacks.onTickChanged) uiCallbacks.onTickChanged = callbacks.onTickChanged;
    if (callbacks.onToast) uiCallbacks.onToast = callbacks.onToast;
}

function clearCallbacks() {
    uiCallbacks.onOrderListChanged = null;
    uiCallbacks.onMarketDataChanged = null;
    uiCallbacks.onDepthChanged = null;
    uiCallbacks.onTickChanged = null;
    uiCallbacks.onToast = null;
}

function setDepthLevelCount(levelCount) {
    var resolvedLevels = Math.floor(Number(levelCount || 5));
    currentDepthLevels = Math.max(5, resolvedLevels);
}

function showToast(msg, isError) {
    if (uiCallbacks.onToast) {
        uiCallbacks.onToast(msg, isError);
    } else {
        console.log((isError ? "[ERROR] " : "[INFO] ") + msg);
    }
}

// ========== 订单管理 ==========
function addOrder(symbol, type, action, qty, price, tradeType, extra) {
    var id = nextOrderId++;
    var now = new Date();
    var timeStr = now.getHours().toString().padStart(2,'0') + ":" + 
                  now.getMinutes().toString().padStart(2,'0') + ":" + 
                  now.getSeconds().toString().padStart(2,'0');
    var order = {
        id: id,
        symbol: symbol,
        type: type,
        action: action,
        qty: qty,
        price: typeof price === 'number' ? price : parseFloat(price),
        time: timeStr,
        status: "已报"
    };
    if (extra) {
        for (var key in extra) order[key] = extra[key];
    }
    orders.unshift(order);
    if (orders.length > 30) orders.pop();
    if (uiCallbacks.onOrderListChanged) uiCallbacks.onOrderListChanged(orders);
    return order;
}

function cancelOrder(orderId) {
    for (var i = 0; i < orders.length; i++) {
        if (orders[i].id === orderId) {
            orders[i].status = "已撤";
            showToast("✅ 已撤单: " + orders[i].symbol + " " + orders[i].action, false);
            if (uiCallbacks.onOrderListChanged) uiCallbacks.onOrderListChanged(orders);
            break;
        }
    }
}

function getOrders() {
    return orders;
}

// ========== 辅助函数 ==========
function getExecPrice(priceType, priceInput, defaultMarketPrice) {
    if (priceType === "market") return defaultMarketPrice;
    var p = parseFloat(priceInput);
    if (isNaN(p) || p <= 0) {
        showToast("请输入有效限价", true);
        return null;
    }
    return p;
}

function parseOptionLots(lotsStr) {
    if (!lotsStr) return 1;
    var parts = lotsStr.split('/');
    if (parts.length === 2) {
        var n = parseFloat(parts[0]);
        var d = parseFloat(parts[1]);
        if (!isNaN(n) && !isNaN(d) && d !== 0) return n / d;
    }
    var direct = parseFloat(lotsStr);
    return !isNaN(direct) ? direct : 1;
}

// ========== 交易函数 (供 QML 调用) ==========
function stockTrade(action, code, shares, priceType, priceInput) {
    if (!code || code === "") {
        showToast("请输入股票代码", true);
        return false;
    }
    var sharesNum = parseInt(shares);
    if (isNaN(sharesNum) || sharesNum <= 0) sharesNum = 100;
    if (sharesNum % 100 !== 0) {
        showToast("股数必须是100的整数倍", true);
        return false;
    }
    var execPrice = getExecPrice(priceType, priceInput, currentPrice);
    if (execPrice === null) return false;
    var actionName = action === "buy" ? "买入" : "卖出";
    addOrder(code, "stock", actionName, sharesNum, execPrice, "stock");
    showToast("📊 委托 " + actionName + " " + code + " " + sharesNum + "股 @" + execPrice.toFixed(2), false);
    return true;
}

function futuresTrade(action, code, lots, priceType, priceInput) {
    if (!code || code === "") {
        showToast("请输入合约代码", true);
        return false;
    }
    var lotsNum = parseInt(lots);
    if (isNaN(lotsNum) || lotsNum <= 0) lotsNum = 1;
    var execPrice = getExecPrice(priceType, priceInput, currentFuturesPrice);
    if (execPrice === null) return false;
    var actionMap = { "long": "开多", "short": "开空", "closeLong": "平多", "closeShort": "平空" };
    var actionName = actionMap[action] || action;
    addOrder(code, "futures", actionName, lotsNum, execPrice, "futures");
    showToast("📈 期货委托 " + actionName + " " + code + " " + lotsNum + "手 @" + execPrice.toFixed(0), false);
    return true;
}

function marginBuyTrade(code, shares, priceType, priceInput) {
    if (!code || code === "") {
        showToast("请输入股票代码", true);
        return false;
    }
    var sharesNum = parseInt(shares);
    if (isNaN(sharesNum) || sharesNum <= 0) sharesNum = 100;
    if (sharesNum % 100 !== 0) {
        showToast("股数必须是100的整数倍", true);
        return false;
    }
    var execPrice = getExecPrice(priceType, priceInput, currentPrice);
    if (execPrice === null) return false;
    addOrder(code, "marginBuy", "融资买入", sharesNum, execPrice, "margin");
    showToast("💳 融资买入委托 " + code + " " + sharesNum + "股 @" + execPrice.toFixed(2), false);
    return true;
}

function repayTrade(code) {
    var symbol = code || "000001";
    addOrder(symbol, "marginBuy", "现金还款", 0, 0, "margin");
    showToast("💰 现金还款委托 " + symbol, false);
    return true;
}

function marginSellTrade(code, shares, priceType, priceInput) {
    if (!code || code === "") {
        showToast("请输入股票代码", true);
        return false;
    }
    var sharesNum = parseInt(shares);
    if (isNaN(sharesNum) || sharesNum <= 0) sharesNum = 100;
    if (sharesNum % 100 !== 0) {
        showToast("股数必须是100的整数倍", true);
        return false;
    }
    var execPrice = getExecPrice(priceType, priceInput, currentPrice);
    if (execPrice === null) return false;
    addOrder(code, "marginSell", "融券卖出", sharesNum, execPrice, "margin");
    showToast("📉 融券卖出委托 " + code + " " + sharesNum + "股 @" + execPrice.toFixed(2), false);
    return true;
}

function returnStockTrade(code) {
    var symbol = code || "000001";
    addOrder(symbol, "marginSell", "现券还券", 0, 0, "margin");
    showToast("📦 现券还券委托 " + symbol, false);
    return true;
}

function optionTrade(action, code, underlying, lotsStr, priceType, priceInput, optionType, expiry) {
    if (!code || code === "") {
        showToast("请输入期权合约代码", true);
        return false;
    }
    var lots = parseOptionLots(lotsStr);
    if (lots <= 0) {
        showToast("请输入有效手数", true);
        return false;
    }
    var execPrice = getExecPrice(priceType, priceInput, 0.0850);
    if (execPrice === null) return false;
    var optionTypeName = optionType === "call" ? "认购" : "认沽";
    var actionMap = { "buy": "买入开仓", "sell": "卖出平仓", "close": "备兑开仓", "exercise": "行权" };
    var actionName = actionMap[action] + " " + optionTypeName;
    addOrder(code, "options", actionName, lotsStr, execPrice, "options", { underlying: underlying, expiry: expiry, optionType: optionType });
    showToast("🎯 期权委托 " + actionName + " " + code + " " + lotsStr + "手 @" + execPrice.toFixed(4), false);
    return true;
}

// ========== 行情数据模拟 (五档+L2逐笔) ==========
function generateDepth(price, isStock, levelCount) {
    var step = isStock !== false ? 0.02 : 5;
    var bids = [], asks = [];
    var totalBid = 0;
    var totalAsk = 0;
    var resolvedLevels = Math.max(5, Math.floor(Number(levelCount || currentDepthLevels || 5)));
    for (var i = 1; i <= resolvedLevels; i++) {
        var bidVolume = Math.floor(Math.random() * 500 + 100);
        var askVolume = Math.floor(Math.random() * 500 + 100);
        bids.push({ price: price - i * step, volume: bidVolume });
        asks.push({ price: price + i * step, volume: askVolume });
        totalBid += bidVolume;
        totalAsk += askVolume;
    }
    return {
        bids: bids,
        asks: asks,
        totalBid: totalBid,
        totalAsk: totalAsk,
        levelCount: resolvedLevels,
        live: false
    };
}

function getDepth() {
    return generateDepth(currentPrice, true, currentDepthLevels);
}

function addTick() {
    var change = (Math.random() - 0.5) * 0.04;
    var tickPrice = currentPrice + change;
    var direction = tickPrice > currentPrice ? "buy" : (tickPrice < currentPrice ? "sell" : "buy");
    var now = new Date();
    var timeStr = now.getHours().toString().padStart(2,'0') + ":" + 
                  now.getMinutes().toString().padStart(2,'0') + ":" + 
                  now.getSeconds().toString().padStart(2,'0');
    var tick = {
        time: timeStr,
        price: tickPrice.toFixed(2),
        volume: Math.floor(Math.random() * 50 + 10),
        direction: direction
    };
    tickHistory.unshift(tick);
    if (tickHistory.length > 20) tickHistory.pop();
    if (uiCallbacks.onTickChanged) uiCallbacks.onTickChanged(tickHistory);
    return tick;
}

function getTickHistory() {
    return tickHistory;
}

function updateMarketPrice() {
    var change = (Math.random() - 0.48) * 0.012;
    var newPrice = currentPrice * (1 + change);
    newPrice = Math.max(3, Math.min(999, newPrice));
    lastPrice = currentPrice;
    currentPrice = newPrice;
    var percent = ((currentPrice - lastPrice) / lastPrice * 100);
    var sign = percent >= 0 ? "+" : "";
    var isUp = percent >= 0;
    
    // 更新期货价格
    var futPrice = Math.floor(3650 + (Math.random() - 0.5) * 30);
    currentFuturesPrice = futPrice;
    
    var depth = generateDepth(currentPrice, true, currentDepthLevels);
    
    var marketData = {
        price: currentPrice,
        priceStr: currentPrice.toFixed(2),
        changePercent: sign + percent.toFixed(2) + "%",
        isUp: isUp,
        futuresPrice: currentFuturesPrice,
        futuresPriceStr: currentFuturesPrice.toString()
    };
    
    var depthData = {
        bids: depth.bids,
        asks: depth.asks,
        totalBid: depth.totalBid,
        totalAsk: depth.totalAsk,
        levelCount: depth.levelCount,
        live: false
    };
    
    if (uiCallbacks.onMarketDataChanged) uiCallbacks.onMarketDataChanged(marketData);
    if (uiCallbacks.onDepthChanged) uiCallbacks.onDepthChanged(depthData);
    
    // 随机产生逐笔成交
    if (Math.random() > 0.6) addTick();
    
    return { marketData: marketData, depthData: depthData };
}

// 启动行情模拟定时器 (需在 QML 中调用 setInterval)
var marketTimer = null;
function startMarketTimer(intervalMs) {
    if (marketTimer) clearInterval(marketTimer);
    marketTimer = setInterval(function() {
        updateMarketPrice();
    }, intervalMs || 2000);
}

function stopMarketTimer() {
    if (marketTimer) {
        clearInterval(marketTimer);
        marketTimer = null;
    }
}

// 初始化一些模拟逐笔数据
function initTicks(count) {
    tickHistory = [];
    for (var i = 0; i < (count || 5); i++) {
        addTick();
    }
}

// ========== 获取当前行情价格 ==========
function getCurrentPrice() {
    return currentPrice;
}

function getCurrentFuturesPrice() {
    return currentFuturesPrice;
}

// ========== 导出接口供 QML 使用 ==========
// 在 QML 中:
// var tradeModule = require("TradeModule.js")
// tradeModule.setCallbacks({ onToast: function(msg,isError){...}, ... })
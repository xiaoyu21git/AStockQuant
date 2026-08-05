import QtQuick 2.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading" as Trading

Trading.TradingFormPanel {
    id: form
    property var widgetConfig: ({})
    scaleFactor: Math.min(1.0, Math.max(0.4, height / 620))
    compactMode: true

    readonly property var marketService: Bridge.MarketDataBridge
    readonly property var positionService: Bridge.PositionAccountBridge
    readonly property var tradeService: Bridge.TradeExecutionBridge
    readonly property var configService: Bridge.TradingConnectionConfigService

    readonly property var accountSnapshot: positionService && positionService.accountSnapshot ? positionService.accountSnapshot : ({})
    readonly property real availableCash: accountSnapshot && accountSnapshot.availableCash !== undefined ? Number(accountSnapshot.availableCash) : 0
    readonly property var snap: marketService && marketService.latestSnapshot ? marketService.latestSnapshot : ({})
    readonly property string boundStrategyId: {
        if (!configService || !configService.currentConfiguration) return ""
        return String(configService.currentConfiguration.boundStrategyId || "").trim()
    }
    property string toastMsg: ""
    property bool toastErr: false

    function showToast(msg, isErr) { toastMsg = msg; toastErr = isErr; toastTimer.restart() }
    Timer { id: toastTimer; interval: 3500; onTriggered: { toastMsg = ""; toastErr = false } }

    marketSnapshot: snap
    pendingOrders: []
    toastMessage: toastMsg
    toastError: toastErr
    availableCapital: availableCash

    // 标的变更 → C++ → C++推送数据到所有控件
    onModeContextChanged: function(mode, symbol) {
        if (symbol && marketService && marketService.ensureWatchSymbol) {
            marketService.ensureWatchSymbol(symbol.toUpperCase().trim())
        }
    }

    onExecuteTrade: function(mode, action, payload) {
        var sym = String(payload && payload.code || "").trim().toUpperCase()
        if (!sym) { showToast("请输入有效代码", true); return }
        var side = action === "sell" ? "SELL" : "BUY"
        var price = Number(payload.priceInput || 0)
        var qty = Number(payload.shares || payload.lots || 0)
        var req = {
            strategyId: boundStrategyId, symbol: sym, side: side,
            price: price, quantity: qty,
            orderType: payload.priceType === "market" ? "MARKET" : "LIMIT",
            mode: mode, action: action,
            clientOrderId: "dw_" + String(Date.now()) + "_" + String(Math.random()).slice(2,8)
        }
        if (tradeService && tradeService.submitBridgeOrder(req)) {
            showToast("已提交委托", false)
        } else {
            showToast(tradeService ? (tradeService.lastErrorMessage || "提交失败") : "交易服务未连接", true)
        }
    }
    onCancelOrderRequested: function(orderId) {
        if (tradeService) tradeService.cancelOrder(orderId)
    }
}

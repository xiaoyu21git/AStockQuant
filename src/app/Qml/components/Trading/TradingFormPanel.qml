import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../utils/TradingConstants.js" as Const

Rectangle {
    id: root
    radius: 28
    color: Const.tradingPanelBg
    border.color: Const.tradingPanelBorder
    border.width: 1
    implicitHeight: compactMode ? 620 : 980

    // 解析完整symbol (代码→代码.后缀)
    function resolveFullSymbol(code) {
        if (!code) return ""
        if (code.indexOf('.') >= 0) return code
        var snaps = Bridge.MarketDataBridge.marketSnapshots
        return (snaps[code + ".SZ"] && snaps[code + ".SZ"].price > 0) ? code + ".SZ"
             : (snaps[code + ".SH"] && snaps[code + ".SH"].price > 0) ? code + ".SH"
             : (snaps[code + ".BJ"] && snaps[code + ".BJ"].price > 0) ? code + ".BJ"
             : code + ".SZ"
    }
    property var marketSnapshot: {
        var sym = resolveFullSymbol(currentSymbol)
        return sym ? (Bridge.MarketDataBridge.marketSnapshots[sym] || {}) : {}
    }

    // 持仓列表点击 → primarySymbol 变更 → 自动填入下单控件+价格
    Connections {
        target: Bridge.MarketDataBridge
        function onPrimarySymbolChanged() {
            var sym = Bridge.MarketDataBridge.primarySymbol || ""
            if (sym) {
                var code = String(sym).replace(".SZ","").replace(".SH","").replace(".BJ","")
                stockCode = code
                Bridge.MarketDataBridge.resolveInstrument(sym)
                Qt.callLater(function() {
                    var snap = Bridge.MarketDataBridge.marketSnapshots[sym] || {}
                    if (snap.price > 0) {
                        stockPrice = snap.price.toFixed(2)
                        lastAutoStockPrice = snap.price.toFixed(2)
                    }
                })
            }
        }
        function onMarketSnapshotsChanged() {
            var sym = Bridge.MarketDataBridge.primarySymbol || ""
            if (!sym) return
            var snap = Bridge.MarketDataBridge.marketSnapshots[sym] || {}
            if (snap.price > 0 && stockPrice === "") {
                stockPrice = snap.price.toFixed(2)
                lastAutoStockPrice = snap.price.toFixed(2)
            }
        }
    }
    property var depthSnapshot: (marketSnapshot && marketSnapshot.depthSnapshot) ? marketSnapshot.depthSnapshot : ({})
    property real availableCapital: 500000
    property var pendingOrders: []
    property string toastMessage: ""
    property bool toastError: false
    property var positionAvailabilitySummary: ({})
    property bool positionAvailabilityError: false
    property bool compactMode: false
    property real scaleFactor: 1.0
    readonly property var tradingFormHelper: Bridge.TradingFormPanelHelper

    function _s(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    readonly property int compactTitleFont: _s(compactMode ? 15 : 24)
    readonly property int compactBodyFont: _s(compactMode ? 10 : 13)
    readonly property int compactMetaFont: _s(compactMode ? 9 : 12)
    readonly property int compactButtonFont: _s(compactMode ? 9 : 13)
    readonly property int compactButtonHeight: _s(compactMode ? 26 : 38)
    readonly property int compactChipHeight: _s(compactMode ? 28 : 46)
    readonly property int compactOrderRowHeight: _s(compactMode ? 48 : 72)
    readonly property int compactSectionLabelFont: _s(compactMode ? 9 : 12)
    readonly property int compactInputFont: _s(compactMode ? 9 : 12)
    readonly property int compactInputHeight: _s(compactMode ? 26 : 38)
    readonly property int compactInputRadius: _s(compactMode ? 8 : 12)
    readonly property int compactInputHorizontalPadding: _s(compactMode ? 8 : 12)
    readonly property int compactInputVerticalPadding: _s(compactMode ? 0 : 1)
    readonly property int compactQuickButtonHeight: _s(compactMode ? 20 : 30)
    readonly property int compactQuickButtonFont: _s(compactMode ? 8 : 10)
    readonly property int compactActionHeight: _s(compactMode ? 26 : 38)
    readonly property int compactActionRadius: _s(compactMode ? 10 : 16)

    property int currentTabIndex: 0
    property bool deferredOrderListReady: false
    property string lastPublishedModeContext: ""
    property string lastPublishedSymbolContext: ""

    property string stockCode: "000001"
    property string stockDisplayName: ""
    property string stockShares: "100"
    property string stockPriceType: "limit"
    property string stockPrice: ""

    // 当前参考价文本（最新价/涨停/跌停/昨收）
    property var currentReferenceText: {
        var ms = marketSnapshot
        var px = ms.price || ms.lastPrice || 0
        return px > 0 ? ("最新 " + px.toFixed(2)) : "暂无行情"
    }
    property var preClose:  (marketSnapshot && marketSnapshot.preClose  > 0) ? marketSnapshot.preClose  : 0
    property var limitUp:   (marketSnapshot && marketSnapshot.limitUp   > 0) ? marketSnapshot.limitUp   : 0
    property var limitDown: (marketSnapshot && marketSnapshot.limitDown > 0) ? marketSnapshot.limitDown : 0
    property string lastAutoStockPrice: ""

    // 选股/切股时自动拉取最新市价
    onStockCodeChanged: {
        if (!stockCode) return
        var fullSym = resolveFullSymbol(stockCode)
        if (fullSym) Bridge.MarketDataBridge.resolveInstrument(fullSym)
        Qt.callLater(function() {
            var ms = marketSnapshot
            if (ms && ms.price > 0) {
                stockPrice = ms.price.toFixed(2)
                lastAutoStockPrice = ms.price.toFixed(2)
            }
        })
    }

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
    readonly property var headerDisplay: tradingFormHelper.buildHeaderState(
        currentMode,
        currentSymbol,
        tabs[currentTabIndex].label,
        marketSnapshot || ({})
    )
    readonly property bool openingMarketWindow: headerDisplay.openingMarketWindow === true
    readonly property var quickButtonModel: tradingFormHelper.quickButtonsForMode(currentMode)
    readonly property var equityQuickPriceButtonModel: tradingFormHelper.equityQuickPriceButtons()
    function equityDisplay(eqMode, code, shares, priceType, price) {
        if (!code) return ({})
        return tradingFormHelper.buildEquityDisplay(
            eqMode, currentMode, code, shares, priceType, price,
            marketSnapshot, ({}), availableCapital, ({}), false)
    }
    readonly property var stockEquityDisplay: equityDisplay("stock", stockCode, stockShares, stockPriceType, stockPrice)
    readonly property var marginBuyEquityDisplay: equityDisplay("margin_buy", marginBuyCode, marginBuyShares, marginBuyPriceType, marginBuyPrice)
    readonly property var marginSellEquityDisplay: equityDisplay("margin_sell", marginSellCode, marginSellShares, marginSellPriceType, marginSellPrice)

    signal modeContextChanged(string mode, string symbol)
    signal executeTrade(string mode, string action, var payload)
    signal cancelOrderRequested(var orderId)
    signal approveCheckpointRequested(var orderData, bool retryAfterApproval)
    signal resumeExecutionPauseRequested(var orderData, bool retryAfterResume)

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

    function currentAutoPriceForMode(mode) {
        if (mode === "stock") {
            return root.lastAutoStockPrice
        }
        if (mode === "margin_buy") {
            return root.lastAutoMarginBuyPrice
        }
        if (mode === "margin_sell") {
            return root.lastAutoMarginSellPrice
        }
        return ""
    }

    function setAutoReferenceStateForMode(mode, autoPrice, autoPriceType) {
        if (mode === "stock") {
            root.lastAutoStockPrice = autoPrice
            root.lastAutoStockPriceType = autoPriceType
            return
        }
        if (mode === "margin_buy") {
            root.lastAutoMarginBuyPrice = autoPrice
            root.lastAutoMarginBuyPriceType = autoPriceType
            return
        }
        if (mode === "margin_sell") {
            root.lastAutoMarginSellPrice = autoPrice
            root.lastAutoMarginSellPriceType = autoPriceType
        }
    }

    function applyModePriceState(mode, priceType, priceInput) {
        var targetMode = mode || currentMode
        if (targetMode === "stock") {
            root.stockPriceType = priceType
            root.stockPrice = priceInput
            return
        }
        if (targetMode === "futures") {
            root.futuresPriceType = priceType
            root.futuresPrice = priceInput
            return
        }
        if (targetMode === "margin_buy") {
            root.marginBuyPriceType = priceType
            root.marginBuyPrice = priceInput
            return
        }
        if (targetMode === "margin_sell") {
            root.marginSellPriceType = priceType
            root.marginSellPrice = priceInput
            return
        }
        root.optionPriceType = priceType
        root.optionPrice = priceInput
    }

    function syncEquityReferenceState(mode) {
        var targetMode = mode || currentMode
        var state = tradingFormHelper.syncEquityReferenceState(
            targetMode,
            root.currentPriceTypeForMode(targetMode),
            root.currentPriceInputForMode(targetMode),
            root.currentAutoPriceForMode(targetMode),
            root.currentAutoPriceTypeForMode(targetMode),
            marketSnapshot, ({}),
            root.openingMarketWindow
        )
        if (!state || !state.priceType) {
            return
        }
        root.applyModePriceState(targetMode, String(state.priceType || "limit"), String(state.priceInput || ""))
        root.setAutoReferenceStateForMode(targetMode, String(state.autoPrice || ""), String(state.autoPriceType || ""))
    }

    function setModePriceInput(mode, priceValue) {
        var targetMode = mode || currentMode
        var formattedValue = tradingFormHelper.formattedModePriceInput(targetMode, Number(priceValue))
        if (String(formattedValue || "").length === 0) {
            return
        }
        root.applyModePriceState(targetMode, "limit", String(formattedValue))
    }

    function adjustModePrice(mode, stepDelta) {
        var targetMode = mode || currentMode
        var adjustedValue = tradingFormHelper.adjustedModePriceInput(
            targetMode,
            root.currentPriceInputForMode(targetMode),
            marketSnapshot || ({}),
            Number(stepDelta || 0)
        )
        if (String(adjustedValue || "").length === 0) {
            return
        }

        root.applyModePriceState(targetMode, "limit", String(adjustedValue))
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

    function applyEquityPriceShortcut(targetMode, shortcut) {
        var formattedValue = tradingFormHelper.formattedModePriceInput(targetMode, tradingFormHelper.resolveEquityShortcutPrice(
            shortcut,
            targetMode,
            marketSnapshot || ({}),
            depthSnapshot || ({})
        ))
        if (String(formattedValue || "").length === 0) {
            return
        }
        root.applyModePriceState(targetMode, "limit", String(formattedValue))
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

    function publishModeContextAsync() {
        var mode = String(currentMode || "")
        var symbol = String(currentSymbol || "").trim().toUpperCase()
        var snapshotSymbol = String(marketSnapshot && marketSnapshot.symbol ? marketSnapshot.symbol : "").trim().toUpperCase()
        var symbolCode = symbol.indexOf(".") >= 0 ? symbol.split(".")[0] : symbol
        var snapshotCode = snapshotSymbol.indexOf(".") >= 0 ? snapshotSymbol.split(".")[0] : snapshotSymbol

        if (snapshotSymbol.length > 0
                && (symbol.length === 0 || symbol === snapshotSymbol || symbolCode === snapshotCode)) {
            symbol = snapshotSymbol
        }

        if (mode === root.lastPublishedModeContext && symbol === root.lastPublishedSymbolContext) {
            return
        }

        root.lastPublishedModeContext = mode
        root.lastPublishedSymbolContext = symbol
        Qt.callLater(function() {
            modeContextChanged(mode, symbol)
        })
    }

    onCurrentModeChanged: {
        publishModeContextAsync()
        root.syncEquityReferenceState(currentMode)
    }
    onCurrentSymbolChanged: {
        Bridge.MarketDataBridge.resolveInstrument(currentSymbol)
        publishModeContextAsync()
        root.syncEquityReferenceState(currentMode)
    }
    onMarketSnapshotChanged: {
        publishModeContextAsync()
        root.syncEquityReferenceState("stock")
        root.syncEquityReferenceState("margin_buy")
        root.syncEquityReferenceState("margin_sell")
    }
    onStockPriceTypeChanged: {
        root.syncEquityReferenceState("stock")
    }
    onMarginBuyPriceTypeChanged: {
        root.syncEquityReferenceState("margin_buy")
    }
    onMarginSellPriceTypeChanged: {
        root.syncEquityReferenceState("margin_sell")
    }

    Component.onCompleted: {
        publishModeContextAsync()
        root.syncEquityReferenceState("stock")
        root.syncEquityReferenceState("margin_buy")
        root.syncEquityReferenceState("margin_sell")
    }

    Timer {
        id: deferredOrderListTimer
        interval: 0
        running: true
        repeat: false
        onTriggered: root.deferredOrderListReady = true
    }


    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 27
        gradient: Gradient {
            GradientStop { position: 0.0; color: Const.tradingPanelGradientStart }
            GradientStop { position: 1.0; color: Const.tradingPanelGradientEnd }
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
                    color: Const.tradingTitleText
                    font.pixelSize: compactTitleFont
                    font.weight: Font.DemiBold
                }

                Text {
                    text: "下单 / 撤单 / 当前委托"
                    color: Const.tradingLabelSecondary
                    font.pixelSize: compactBodyFont
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                radius: compactMode ? 12 : 14
                color: Const.tradingButtonBg
                border.color: Const.tradingChipBorder
                border.width: 1
                implicitWidth: compactMode ? 108 : 126
                implicitHeight: compactMode ? 40 : 52

                Column {
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        text: "可用资金"
                        color: Const.tradingLabelLight
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: "¥" + Number(root.availableCapital).toLocaleString(Qt.locale(), "f", 0)
                        color: Const.tradingValueText
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
                    color: index === root.currentTabIndex ? Const.tradingTabActiveBorder : Const.tradingTabInactiveBg
                    border.color: index === root.currentTabIndex ? Const.tradingTabActiveBorder : Const.tradingTabInactiveBorder
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: modelData.icon + " " + modelData.label
                        color: index === root.currentTabIndex ? Const.tradingTabActiveText : Const.tradingTabInactiveText
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
            color: Const.tradingHeaderBg
            border.color: Const.tradingHeaderBorder
            border.width: 1
            implicitHeight: compactMode ? 52 : 94

            RowLayout {
                anchors.fill: parent
                anchors.margins: compactMode ? 12 : 16
                spacing: compactMode ? 10 : 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "当前模式"
                        color: Const.tradingLabelSecondary
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: String(root.headerDisplay.currentModeDisplayTitle || "")
                        color: Const.tradingBrightText
                        font.pixelSize: compactMode ? 13 : 16
                        font.weight: Font.DemiBold
                    }
                }

                ColumnLayout {
                    spacing: 4

                    Text {
                        text: "参考价格"
                        color: Const.tradingLabelSecondary
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: String(root.headerDisplay.modePriceText || "--")
                        color: Const.tradingAccentCyan
                        font.pixelSize: compactMode ? 17 : 22
                        font.weight: Font.Bold
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: compactMode ? 220 : 360
            radius: compactMode ? 16 : 20
            color: Const.tradingPanelBgAlt
            border.color: Const.tradingFormAreaBorder
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: compactMode ? 12 : 18
                spacing: compactMode ? 8 : 14

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.currentTabIndex

                    // Tab 0: 普通股票
                    StockTradeForm {
                        id: stockForm
                        compactMode: root.compactMode
                        scaleFactor: root.scaleFactor
                        stockCode: root.stockCode
                        stockDisplayName: root.stockDisplayName
                        stockShares: root.stockShares
                        stockPriceType: root.stockPriceType
                        stockPrice: root.stockPrice
                        quickButtonModel: root.quickButtonModel
                        equityQuickPriceButtonModel: root.equityQuickPriceButtonModel
                        equityDisplay: root.stockEquityDisplay
                        marketSnapshot: root.marketSnapshot
                        depthSnapshot: root.depthSnapshot
                        tradingFormHelper: root.tradingFormHelper
                        currentReferenceText: root.currentReferenceText
                        symbolSearchModel: symbolSearch
                        onCodeEdited: function(c) { root.stockCode = c }
                        onSearchRequested: function(query) {
                            symbolSearch.search(query)
                            Qt.callLater(function() {
                                searchPopup.visible = symbolSearch.count > 0
                            })
                        }
                        onSharesEdited: function(s) { root.stockShares = s }
                        onPriceTypeEdited: function(t) { root.stockPriceType = t }
                        onPriceEdited: function(p) { root.stockPrice = p }
                        onQuickValueSelected: function(v) { root.applyQuickValue(v) }
                        onPriceAdjustRequested: function(d) { root.adjustModePrice("stock", d) }
                        onEquityPriceShortcutRequested: function(sc) { root.applyEquityPriceShortcut("stock", sc) }
                    }

                    // Tab 1: 期货
                    FuturesTradeForm {
                        id: futuresForm
                        compactMode: root.compactMode
                        scaleFactor: root.scaleFactor
                        code: root.futuresCode
                        lots: root.futuresLots
                        priceType: root.futuresPriceType
                        price: root.futuresPrice
                        quickButtonModel: root.quickButtonModel
                        currentReferenceText: root.currentReferenceText
                        onCodeEdited: function(c) { root.futuresCode = c }
                        onLotsEdited: function(l) { root.futuresLots = l }
                        onPriceTypeEdited: function(t) { root.futuresPriceType = t }
                        onPriceEdited: function(p) { root.futuresPrice = p }
                        onQuickValueSelected: function(v) { root.applyQuickValue(v) }
                        onPriceAdjustRequested: function(d) { root.adjustModePrice("futures", d) }
                    }

                    // Tab 2: 融资买入
                    MarginBuyTradeForm {
                        id: marginBuyForm
                        compactMode: root.compactMode
                        scaleFactor: root.scaleFactor
                        code: root.marginBuyCode
                        shares: root.marginBuyShares
                        priceType: root.marginBuyPriceType
                        price: root.marginBuyPrice
                        quickButtonModel: root.quickButtonModel
                        equityQuickPriceButtonModel: root.equityQuickPriceButtonModel
                        equityDisplay: root.marginBuyEquityDisplay
                        marketSnapshot: root.marketSnapshot
                        depthSnapshot: root.depthSnapshot
                        tradingFormHelper: root.tradingFormHelper
                        currentReferenceText: root.currentReferenceText
                        onCodeEdited: function(c) { root.marginBuyCode = c }
                        onSharesEdited: function(s) { root.marginBuyShares = s }
                        onPriceTypeEdited: function(t) { root.marginBuyPriceType = t }
                        onPriceEdited: function(p) { root.marginBuyPrice = p }
                        onQuickValueSelected: function(v) { root.applyQuickValue(v) }
                        onPriceAdjustRequested: function(d) { root.adjustModePrice("margin_buy", d) }
                        onEquityPriceShortcutRequested: function(sc) { root.applyEquityPriceShortcut("margin_buy", sc) }
                    }

                    // Tab 3: 融券卖出
                    MarginSellTradeForm {
                        id: marginSellForm
                        compactMode: root.compactMode
                        scaleFactor: root.scaleFactor
                        code: root.marginSellCode
                        shares: root.marginSellShares
                        priceType: root.marginSellPriceType
                        price: root.marginSellPrice
                        quickButtonModel: root.quickButtonModel
                        equityQuickPriceButtonModel: root.equityQuickPriceButtonModel
                        equityDisplay: root.marginSellEquityDisplay
                        marketSnapshot: root.marketSnapshot
                        depthSnapshot: root.depthSnapshot
                        tradingFormHelper: root.tradingFormHelper
                        currentReferenceText: root.currentReferenceText
                        onCodeEdited: function(c) { root.marginSellCode = c }
                        onSharesEdited: function(s) { root.marginSellShares = s }
                        onPriceTypeEdited: function(t) { root.marginSellPriceType = t }
                        onPriceEdited: function(p) { root.marginSellPrice = p }
                        onQuickValueSelected: function(v) { root.applyQuickValue(v) }
                        onPriceAdjustRequested: function(d) { root.adjustModePrice("margin_sell", d) }
                        onEquityPriceShortcutRequested: function(sc) { root.applyEquityPriceShortcut("margin_sell", sc) }
                    }

                    // Tab 4: 期权
                    OptionsTradeForm {
                        id: optionsForm
                        compactMode: root.compactMode
                        scaleFactor: root.scaleFactor
                        code: root.optionCode
                        underlying: root.optionUnderlying
                        lots: root.optionLots
                        priceType: root.optionPriceType
                        price: root.optionPrice
                        optionType: root.optionType
                        optionExpiry: root.optionExpiry
                        quickButtonModel: root.quickButtonModel
                        currentReferenceText: root.currentReferenceText
                        onCodeEdited: function(c) { root.optionCode = c }
                        onUnderlyingEdited: function(u) { root.optionUnderlying = u }
                        onLotsEdited: function(l) { root.optionLots = l }
                        onPriceTypeEdited: function(t) { root.optionPriceType = t }
                        onPriceEdited: function(p) { root.optionPrice = p }
                        onOptionTypeEdited: function(t) { root.optionType = t }
                        onOptionExpiryEdited: function(e) { root.optionExpiry = e }
                        onQuickValueSelected: function(v) { root.applyQuickValue(v) }
                        onPriceAdjustRequested: function(d) { root.adjustModePrice("options", d) }
                    }
                }
            }
        }

        OrderActionBar {
            compactMode: root.compactMode
            Layout.fillWidth: true

                RowLayout {
                    visible: root.currentMode === "stock"
                    spacing: compactMode ? 8 : 12

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactActionHeight
                        radius: compactActionRadius
                        color: Const.tradingBuyRed

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
                        color: sellEnabled ? Const.tradingSellGreen : Const.tradingDisabledBtnBg

                        Text {
                            anchors.centerIn: parent
                            text: "卖出"
                            color: parent.sellEnabled ? "white" : Const.tradingDisabledBtnText
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
                            color: Const.tradingBuyRed

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
                            color: Const.tradingSellGreen

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
                            color: Const.tradingDisabledBtnBg

                            Text {
                                anchors.centerIn: parent
                                text: "平多"
                                color: Const.tradingCloseBtnText
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
                            color: Const.tradingDisabledBtnBg

                            Text {
                                anchors.centerIn: parent
                                text: "平空"
                                color: Const.tradingCloseBtnText
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
                            color: Const.tradingPurpleBtn

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
                            color: closeLongEnabled ? Const.tradingSellGreen : Const.tradingDisabledBtnBg

                            Text {
                                anchors.centerIn: parent
                                text: "卖出平仓"
                                color: parent.closeLongEnabled ? "white" : Const.tradingDisabledBtnText
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
                        color: Const.tradingBlueBtn

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
                        color: Const.tradingPurpleBtn

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
                        color: Const.tradingBlueBtn

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
                            color: Const.tradingAmberBtn

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
                            color: Const.tradingPurpleBtnDark

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
                            color: Const.tradingDisabledBtnBg

                            Text {
                                anchors.centerIn: parent
                                text: "备兑开仓"
                                color: Const.tradingCloseBtnText
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
                            color: Const.tradingGrayBtn

                            Text {
                                anchors.centerIn: parent
                                text: "备兑平仓"
                                color: Const.tradingLightBlue
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
                            color: Const.tradingDisabledBtnBg

                            Text {
                                anchors.centerIn: parent
                                text: "行权"
                                color: Const.tradingCloseBtnText
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

        Text {
            text: "执行回报"
            color: Const.tradingCancelText
            font.pixelSize: compactMetaFont
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 18
            color: Const.tradingOrderListBg
            border.color: Const.tradingOrderListBorder
            border.width: 1

            PendingOrderList {
                id: pendingOrderListView
                compactMode: root.compactMode
                scaleFactor: root.scaleFactor
                orders: root.pendingOrders
                tradingFormHelper: root.tradingFormHelper
                onCancelRequested: function(oid) { root.cancelOrderRequested(oid) }
                onApproveCheckpointRequested: function(data, retry) { root.approveCheckpointRequested(data, retry) }
                onResumeExecutionRequested: function(data, retry) { root.resumeExecutionPauseRequested(data, retry) }
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
                        color: Const.tradingButtonBg
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
        color: root.toastError ? Const.tradingToastErrorBg : Const.tradingToastSuccessBg
        border.color: root.toastError ? Const.tradingToastErrorBorder : "#0ff"
        border.width: 1
        implicitHeight: compactMode ? 34 : 38
        implicitWidth: toastLabel.implicitWidth + 28

        Text {
            id: toastLabel
            anchors.centerIn: parent
            text: root.toastMessage
            color: root.toastError ? Const.tradingToastErrorText : Const.tradingToastSuccessText
            font.pixelSize: compactMetaFont
        }
    }

    Bridge.SymbolSearchModel { id: symbolSearch; Component.onCompleted: init() }

    Popup {
        id: searchPopup
        parent: Overlay.overlay
        visible: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 2
        opacity: 0.92
        property var _formRef: root
        property var _fieldRef: stockForm.codeField
        background: Rectangle { radius: 6; color: Const.tradingSearchPopupBg; border.color: Const.tradingSearchPopupBorder; border.width: 1.5 }

        onVisibleChanged: {
            if (visible) {
                var fld = _fieldRef
                var pt = fld ? fld.mapToItem(null, 0, fld.height) : ({x:0, y:0})
                x = pt.x
                y = pt.y
                width = fld ? Math.max(220, fld.width) : 220
                var rows = Math.min(symbolSearch.count, 3)
                height = rows > 0 ? rows * 34 + 8 : 40
            }
        }

        ListView {
            id: searchList
            anchors.fill: parent; anchors.margins: 2
            model: symbolSearch
            clip: true; spacing: 1
            property var _formRef: root
            property var _fieldRef: stockForm.codeField
            property var _popupRef: searchPopup
            delegate: Rectangle {
                id: row
                width: searchList.width; height: 34
                color: rowMa.containsMouse ? Const.tradingSearchPopupHover : "transparent"; radius: 4
                property var item: symbolSearch.getRow(index)
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 8
                    text: {
                        var it = row.item || {}
                        var sym = it.symbol || ""; var nm = it.secName || ""
                        var dot = sym.indexOf('.'); var code = dot > 0 ? sym.substring(0, dot) : sym
                        return code + "  " + nm
                    }
                    color: Const.tradingSearchPopupText; font.pixelSize: 12
                }
                MouseArea {
                    id: rowMa
                    anchors.fill: parent; hoverEnabled: true
                    onClicked: {
                        var it = row.item || {}
                        var sym = it.symbol || ""
                        var nm = it.secName || ""
                        var lv = row.ListView.view
                        var frm = lv._formRef
                        var fld = lv._fieldRef
                        var pop = lv._popupRef
                        if (fld) {
                            fld.suppressTextChange = true
                        }
                        if (frm) {
                            frm.stockCode = sym
                            frm.stockDisplayName = nm
                        }
                        if (fld) {
                            fld.text = nm + " " + sym
                            fld.suppressTextChange = false
                        }
                        if (pop) pop.visible = false
                    }
                }
            }
        }
    }
}
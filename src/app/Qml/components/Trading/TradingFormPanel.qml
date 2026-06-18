import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

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
    property var positionAvailabilitySummary: ({})
    property bool positionAvailabilityError: false
    property bool compactMode: false
    readonly property var tradingFormHelper: Bridge.TradingFormPanelHelper
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
    property string lastPublishedModeContext: ""
    property string lastPublishedSymbolContext: ""

    property string stockCode: "000001"
    property string stockDisplayName: ""
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
    readonly property var headerDisplay: tradingFormHelper.buildHeaderState(
        currentMode,
        currentSymbol,
        tabs[currentTabIndex].label,
        marketSnapshot || ({})
    )
    readonly property bool openingMarketWindow: headerDisplay.openingMarketWindow === true
    readonly property string currentReferenceText: String(headerDisplay.referenceText || "")
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
                        model: root.quickButtonModel

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
                        placeholderText: root.currentReferenceText
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
                    text: String(root.marginBuyEquityDisplay.identitySummary || "")
                    color: String(root.marginBuyEquityDisplay.identityColor || "#7ea1c5")
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Repeater {
                        model: root.quickButtonModel

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
                        placeholderText: root.currentReferenceText
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
                        model: root.equityQuickPriceButtonModel

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactQuickButtonHeight
                            radius: compactInputRadius
                            color: "#10243a"
                            border.color: "#214362"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: tradingFormHelper.equityShortcutButtonText(modelData.code, modelData.label, "margin_buy", marketSnapshot || ({}), depthSnapshot || ({}))
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
                    text: String(root.marginBuyEquityDisplay.priceSummary || "")
                    color: "#7ea1c5"
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: String(root.marginBuyEquityDisplay.amountSummary || "")
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
                    text: String(root.marginSellEquityDisplay.identitySummary || "")
                    color: String(root.marginSellEquityDisplay.identityColor || "#7ea1c5")
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: compactMode ? 6 : 8

                    Repeater {
                        model: root.quickButtonModel

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
                        placeholderText: root.currentReferenceText
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
                        model: root.equityQuickPriceButtonModel

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: compactQuickButtonHeight
                            radius: compactInputRadius
                            color: "#10243a"
                            border.color: "#214362"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: tradingFormHelper.equityShortcutButtonText(modelData.code, modelData.label, "margin_sell", marketSnapshot || ({}), depthSnapshot || ({}))
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
                    text: String(root.marginSellEquityDisplay.priceSummary || "")
                    color: "#7ea1c5"
                    font.pixelSize: compactMetaFont
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: String(root.marginSellEquityDisplay.amountSummary || "")
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
                        model: root.quickButtonModel

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
                        placeholderText: root.currentReferenceText
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
                    readonly property var orderUi: tradingFormHelper.buildOrderPresentation(orderData || ({}))
                    width: ListView.view.width
                    height: compactOrderRowHeight
                    radius: compactMode ? 12 : 14
                    color: "#0b1625"
                    border.color: orderUi.normalizedStatus === "CANCELLED" ? "#5a2a2a" : "#164b5c"
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: compactMode ? 8 : 12
                        spacing: compactMode ? 6 : 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: orderData.symbol + "  " + orderData.action + "  " + String(orderUi.headlineAmount || "")
                                color: "#0ff"
                                font.pixelSize: compactMetaFont
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Text {
                                text: String(orderUi.priceSummary || "")
                                    + "  ·  " + orderData.time
                                    + "  ·  " + orderData.status
                                    + (String(orderUi.filledSummary || "").length > 0 ? "  ·  " + String(orderUi.filledSummary || "") : "")
                                color: "#8aaeff"
                                font.pixelSize: compactMode ? 10 : 11
                                elide: Text.ElideRight
                            }

                            Text {
                                visible: String(orderUi.auxiliarySummary || "").length > 0
                                text: String(orderUi.auxiliarySummary || "")
                                color: "#5f85a8"
                                font.pixelSize: compactMode ? 9 : 10
                                elide: Text.ElideMiddle
                            }
                        }

                        Rectangle {
                            visible: orderUi.canCancel === true
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
                            visible: orderUi.canApproveManualCheckpoint === true
                            radius: compactMode ? 12 : 14
                            color: "#15334a"
                            border.color: "#67E8F9"
                            border.width: 1
                            implicitWidth: compactMode ? 88 : 110
                            implicitHeight: compactMode ? 24 : 30

                            Text {
                                anchors.centerIn: parent
                                text: String(orderUi.checkpointActionLabel || "人工确认")
                                color: "#67E8F9"
                                font.pixelSize: compactMode ? 10 : 11
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.approveCheckpointRequested(orderData, orderUi.canRetryManualCheckpoint === true)
                            }
                        }

                        Rectangle {
                            visible: orderUi.canResumeExecutionPause === true
                            radius: compactMode ? 12 : 14
                            color: "#3a2a14"
                            border.color: "#FBBF24"
                            border.width: 1
                            implicitWidth: compactMode ? 88 : 110
                            implicitHeight: compactMode ? 24 : 30

                            Text {
                                anchors.centerIn: parent
                                text: String(orderUi.executionPauseActionLabel || "恢复执行")
                                color: "#FBBF24"
                                font.pixelSize: compactMode ? 10 : 11
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.resumeExecutionPauseRequested(orderData, orderUi.canRetryExecutionPause === true)
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
                        text: String(root.headerDisplay.currentModeDisplayTitle || "")
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
                        text: String(root.headerDisplay.modePriceText || "--")
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
                                id: stockCodeField
                                Layout.fillWidth: true
                                Layout.preferredHeight: compactInputHeight
                                placeholderText: "输代码/名称搜索"
                                color: "#f8fafc"
                                font.pixelSize: compactInputFont
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                topPadding: compactInputVerticalPadding
                                bottomPadding: compactInputVerticalPadding
                                leftPadding: compactInputHorizontalPadding
                                rightPadding: compactInputHorizontalPadding
                                property bool suppressTextChange: false
                                text: root.stockDisplayName || root.stockCode
                                onTextChanged: {
                                    if (suppressTextChange) return
                                    var t = text.trim()
                                    root.stockDisplayName = ""
                                    root.stockCode = t
                                    symbolSearch.search(t)
                                    Qt.callLater(function() {
                                        searchPopup.visible = symbolSearch.count > 0
                                    })
                                }
                                background: Rectangle {
                                    radius: compactInputRadius
                                    color: "#0f2238"
                                    border.color: "#20364f"
                                    border.width: 1
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: String(root.stockEquityDisplay.identitySummary || "")
                                color: String(root.stockEquityDisplay.identityColor || "#7ea1c5")
                                font.pixelSize: compactMetaFont
                                horizontalAlignment: Text.AlignHCenter
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: compactMode ? 6 : 8

                                Repeater {
                                    model: root.quickButtonModel

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
                                    placeholderText: root.currentReferenceText
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
                                    model: root.equityQuickPriceButtonModel

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: compactQuickButtonHeight
                                        radius: compactInputRadius
                                        color: "#10243a"
                                        border.color: "#214362"
                                        border.width: 1

                                        Text {
                                            anchors.centerIn: parent
                                            text: tradingFormHelper.equityShortcutButtonText(modelData.code, modelData.label, "stock", marketSnapshot || ({}), depthSnapshot || ({}))
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
                                text: String(root.stockEquityDisplay.priceSummary || "")
                                color: "#7ea1c5"
                                font.pixelSize: compactMetaFont
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                Layout.fillWidth: true
                                text: String(root.stockEquityDisplay.amountSummary || "")
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

    Bridge.SymbolSearchModel { id: symbolSearch; Component.onCompleted: init() }

    Popup {
        id: searchPopup
        parent: Overlay.overlay
        visible: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 2
        opacity: 0.92
        background: Rectangle { radius: 6; color: "#080E1A"; border.color: "#38BDF8"; border.width: 1.5 }

        onVisibleChanged: {
            if (visible) {
                var pt = stockCodeField.mapToItem(null, 0, stockCodeField.height)
                x = pt.x
                y = pt.y
                width = Math.max(220, stockCodeField.width)
                var rows = Math.min(symbolSearch.count, 3)
                height = rows > 0 ? rows * 34 + 8 : 40
            }
        }

        ListView {
            id: searchList
            anchors.fill: parent; anchors.margins: 2
            model: symbolSearch
            clip: true; spacing: 1
            delegate: Rectangle {
                id: row
                width: searchList.width; height: 34
                color: rowMa.containsMouse ? "#1E3A5F" : "transparent"; radius: 4
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
                    color: "#E2E8F0"; font.pixelSize: 12
                }
                MouseArea {
                    id: rowMa
                    anchors.fill: parent; hoverEnabled: true
                    onClicked: {
                        var it = row.item || {}
                        root.stockCode = it.symbol || ""
                        root.stockDisplayName = it.secName || ""
                        stockCodeField.suppressTextChange = true
                        stockCodeField.text = root.stockDisplayName
                        stockCodeField.suppressTextChange = false
                        searchPopup.visible = false
                    }
                }
            }
        }
    }
}
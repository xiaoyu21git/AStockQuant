import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/TradingConstants.js" as Const

Rectangle {
    id: root
    signal depthLevelsChanged(int levels)

    onDepthLevelsChanged: function(levels) { selectedDepthLevels = levels }

    radius: 28
    color: Const.depthPanelBg
    border.color: Const.depthPanelBorder
    border.width: 1
    implicitHeight: compactMode ? 640 : 680

    property var marketSnapshot: ({})
    property var depthSnapshot: ({ bids: [], asks: [], totalBid: 0, totalAsk: 0 })
    property var tickRows: []
    property string activeMode: "stock"
    property string activeSymbol: "000001"
    property bool compactMode: false
    property real scaleFactor: 1.0
    property int selectedDepthLevels: 5
    readonly property int requestedDepthLevels: selectedDepthLevels > 10 ? 10 : (selectedDepthLevels < 5 ? 5 : selectedDepthLevels)
    function _s(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    readonly property int compactHeaderFont: _s(compactMode ? 15 : 20)
    readonly property int compactSectionTitleFont: _s(compactMode ? 12 : 15)
    readonly property int compactStatCardHeight: _s(compactMode ? 48 : 64)
    readonly property int compactBookRowHeight: _s(compactMode ? 24 : 30)
    readonly property int compactTradeRowHeight: _s(compactMode ? 26 : 34)
    readonly property int compactMidPriceHeight: _s(compactMode ? 26 : 34)
    readonly property int compactOrderBookHeight: _s(compactMode ? 310 : 420)
    readonly property int compactPanelGap: _s(compactMode ? 6 : 12)
    readonly property int compactBookTagFont: _s(compactMode ? 8 : 11)
    readonly property int compactBookPriceFont: _s(compactMode ? 10 : 12)
    readonly property int compactBookVolumeFont: _s(compactMode ? 8 : 11)
    readonly property int compactMetaFont: _s(compactMode ? 9 : 11)
    readonly property int compactTradePanelWidth: _s(compactMode ? 150 : 256)
    readonly property int compactDepthChipHeight: _s(compactMode ? 20 : 26)
    readonly property int compactDepthHeaderHeight: _s(compactMode ? 24 : 30)
    readonly property int compactDepthSideWidth: _s(compactMode ? 24 : 34)

    readonly property bool isLimitBoard: !!(marketSnapshot && (marketSnapshot.limitUp || marketSnapshot.limitDown))
    function statusBadgeText() {
        if (marketSnapshot && marketSnapshot.limitUp) return "涨停"
        if (marketSnapshot && marketSnapshot.limitDown) return "跌停"
        return ""
    }
    function statusBadgeColor() {
        if (marketSnapshot && marketSnapshot.limitUp) return Const.tradingBuyRed
        if (marketSnapshot && marketSnapshot.limitDown) return Const.depthLimitDownGreen
        return "transparent"
    }
    function sealedInfo() {
        if (!isLimitBoard) return ""
        var vol = Number(marketSnapshot.sealedVolume || 0)
        var amt = Number(marketSnapshot.sealedAmount || 0)
        if (vol <= 0) return "封单: --"
        var volStr = vol >= 1e8 ? (vol/1e8).toFixed(2)+"亿" : (vol/1e4).toFixed(0)+"万"
        var amtStr = amt >= 1e8 ? (amt/1e8).toFixed(2)+"亿" : (amt/1e4).toFixed(0)+"万"
        var side = marketSnapshot.limitUp ? "买一封单" : "卖一封单"
        return side + ": " + volStr + " 约" + amtStr
    }
    readonly property int compactDepthPriceWidth: _s(compactMode ? 40 : 60)
    readonly property int compactDepthLotWidth: _s(compactMode ? 28 : 44)
    readonly property int compactDepthShareWidth: _s(compactMode ? 38 : 56)
    readonly property int compactDepthAmountWidth: _s(compactMode ? 48 : 68)
    readonly property bool l2PanelVisible: !!(tickRows && tickRows.length > 0)
    readonly property string shareCountLabel: activeMode === "stock" || activeMode === "margin_buy" || activeMode === "margin_sell"
        ? "股数"
        : (activeMode === "options" ? "张数" : "数量")
    readonly property int availableDepthLevels: {
        var bidLevels = depthSnapshot && depthSnapshot.bids ? depthSnapshot.bids.length : 0
        var askLevels = depthSnapshot && depthSnapshot.asks ? depthSnapshot.asks.length : 0
        return bidLevels > askLevels ? bidLevels : askLevels
    }
    readonly property int visibleDepthLevels: {
        if (availableDepthLevels === 0) {
            return 0
        }
        if (availableDepthLevels > 0 && availableDepthLevels < selectedDepthLevels) {
            return availableDepthLevels
        }
        return requestedDepthLevels
    }
    readonly property var depthTableRows: buildDepthTableRows()
    readonly property bool hasDisplayPrice: currentPrice() > 0

    function isShareBasedMode() {
        return activeMode === "stock" || activeMode === "margin_buy" || activeMode === "margin_sell"
    }

    function formatLotCount(value) {
        var numericValue = Number(value || 0)
        if (isShareBasedMode()) {
            numericValue = numericValue / 100.0
        }
        if (numericValue >= 10000) {
            return (numericValue / 10000).toFixed(2) + "万"
        }
        if (Math.abs(numericValue - Math.round(numericValue)) < 0.001) {
            return String(Math.round(numericValue))
        }
        return numericValue.toFixed(numericValue >= 100 ? 0 : 2)
    }

    function formatShareCount(value) {
        return formatVolume(value)
    }

    function formatAmount(priceValue, volumeValue) {
        var amount = Number(priceValue || 0) * Number(volumeValue || 0)
        if (amount >= 100000000) {
            return "¥" + (amount / 100000000).toFixed(2) + "亿"
        }
        if (amount >= 10000) {
            return "¥" + (amount / 10000).toFixed(2) + "万"
        }
        return "¥" + Math.round(amount)
    }

    function buildDepthRowsForSide(rows, isBid) {
        var sourceRows = rows || []
        var count = visibleDepthLevels
        var limit = sourceRows.length < count ? sourceRows.length : count
        var result = []
        var index

        if (isBid) {
            for (index = 0; index < limit; ++index) {
                result.push({
                    kind: "depth",
                    side: "买",
                    level: index + 1,
                    price: Number(sourceRows[index].price || 0),
                    volume: Number(sourceRows[index].volume || 0),
                    isBid: true
                })
            }
            return result
        }

        for (index = limit - 1; index >= 0; --index) {
            result.push({
                kind: "depth",
                side: "卖",
                level: index + 1,
                price: Number(sourceRows[index].price || 0),
                volume: Number(sourceRows[index].volume || 0),
                isBid: false
            })
        }
        return result
    }

    function buildDepthTableRows() {
        var result = []
        var askRows = buildDepthRowsForSide(depthSnapshot && depthSnapshot.asks ? depthSnapshot.asks : [], false)
        var bidRows = buildDepthRowsForSide(depthSnapshot && depthSnapshot.bids ? depthSnapshot.bids : [], true)
        var index

        for (index = 0; index < askRows.length; ++index) {
            result.push(askRows[index])
        }
        if (askRows.length > 0 || bidRows.length > 0) {
            result.push({ kind: "divider" })
        }
        for (index = 0; index < bidRows.length; ++index) {
            result.push(bidRows[index])
        }

        return result
    }

    function modeName() {
        if (activeMode === "futures") {
            return "期货"
        }
        if (activeMode === "margin_buy") {
            return "融资买入"
        }
        if (activeMode === "margin_sell") {
            return "融券卖出"
        }
        if (activeMode === "options") {
            return "期权"
        }
        return "普通股票"
    }

    function priceDigits() {
        if (activeMode === "futures") {
            return 0
        }
        if (activeMode === "options") {
            return 4
        }
        return 2
    }

    function currentPrice() {
        var numericValue = 0
        if (activeMode === "futures") {
            numericValue = Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
            return isNaN(numericValue) ? 0 : numericValue
        }
        if (activeMode === "options") {
            numericValue = Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
            return isNaN(numericValue) ? 0 : numericValue
        }
        numericValue = Number(marketSnapshot && marketSnapshot.price !== undefined ? marketSnapshot.price : 0)
        return isNaN(numericValue) ? 0 : numericValue
    }

    function formatPrice(value) {
        var numericValue = Number(value)
        if (isNaN(numericValue) || numericValue <= 0) {
            return "--"
        }
        return numericValue.toFixed(priceDigits())
    }

    function formatVolume(value) {
        var numericValue = Number(value || 0)
        if (numericValue >= 100000000) {
            return (numericValue / 100000000).toFixed(2) + "亿"
        }
        if (numericValue >= 10000) {
            return (numericValue / 10000).toFixed(2) + "万"
        }
        return String(Math.round(numericValue))
    }

    function trendText() {
        if (!hasDisplayPrice) {
            return "--"
        }
        return marketSnapshot && marketSnapshot.changePercent ? marketSnapshot.changePercent : "--"
    }

    function trendUp() {
        return !!(marketSnapshot && marketSnapshot.isUp)
    }

    function quoteStateText() {
        if (depthSnapshot && depthSnapshot.live) {
            return "实时盘口"
        }
        if (marketSnapshot && marketSnapshot.snapshotOnly) {
            return "缓存快照，等待实时盘口"
        }
        if (hasDisplayPrice) {
            return "已收到行情，等待实时盘口"
        }
        return "等待桥接行情"
    }

    readonly property color trendColor: !hasDisplayPrice ? Const.tradingStatusDefault : (trendUp() ? Const.tradingBuyRed : Const.depthLimitDownGreen)

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 27
        gradient: Gradient {
            GradientStop { position: 0.0; color: Const.depthGradientStart }
            GradientStop { position: 1.0; color: Const.depthGradientEnd }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: compactMode ? 16 : 24
        spacing: compactMode ? 10 : 18

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: compactOrderBookHeight
            Layout.alignment: Qt.AlignTop
            spacing: compactPanelGap

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignTop
                radius: 22
                color: Const.tradingPanelBgAlt
                border.color: Const.tradingFormAreaBorder
                border.width: 1
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: compactMode ? 8 : 14
                    spacing: compactMode ? 5 : 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: compactMode ? 4 : 8

                        Text {
                            text: "盘口列表"
                            color: Const.tradingBrightText
                            font.pixelSize: compactSectionTitleFont
                            font.weight: Font.DemiBold
                        }

                        Item { Layout.fillWidth: true }

                        Repeater {
                            model: [5, 10]

                            Rectangle {
                                radius: compactMode ? 8 : 10
                                implicitWidth: compactMode ? 28 : 36
                                implicitHeight: compactDepthChipHeight
                                color: root.requestedDepthLevels === modelData ? Const.depthActiveLevelBg : Const.depthInactiveLevelBg
                                border.color: root.requestedDepthLevels === modelData ? Const.depthActiveLevelBorder : Const.depthInactiveLevelBorder
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: String(modelData)
                                    color: root.requestedDepthLevels === modelData ? Const.depthActiveLevelText : Const.depthInactiveLevelText
                                    font.pixelSize: compactMode ? 9 : 11
                                    font.weight: Font.DemiBold
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.depthLevelsChanged(modelData)
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "显示 " + root.visibleDepthLevels + " / " + root.requestedDepthLevels + " 档"
                        color: Const.depthLabelText
                        font.pixelSize: compactMetaFont
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactDepthHeaderHeight
                        radius: compactMode ? 10 : 12
                        color: Const.depthInactiveLevelBg

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: compactMode ? 5 : 8
                            spacing: compactMode ? 4 : 8

                            Text { text:"档"; color:Const.depthLabelText; font.pixelSize:compactMetaFont; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                            Text { text:"价格"; color:Const.depthLabelText; font.pixelSize:compactMetaFont; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                            Text { text:"手数"; color:Const.depthLabelText; font.pixelSize:compactMetaFont; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                            Text { text:root.shareCountLabel; color:Const.depthLabelText; font.pixelSize:compactMetaFont; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                            Text { text:"金额"; color:Const.depthLabelText; font.pixelSize:compactMetaFont; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: compactMode ? 2 : 4
                        boundsBehavior: Flickable.StopAtBounds
                        model: root.depthTableRows
                        interactive: true

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AlwaysOff
                        }

                        delegate: Item {
                            property var rowData: modelData
                            width: ListView.view.width
                            height: rowData.kind === "divider" ? (compactMode ? 8 : 12) : compactBookRowHeight

                            Rectangle {
                                anchors.fill: parent
                                visible: rowData.kind === "depth"
                                radius: compactMode ? 9 : 12
                                color: rowData.isBid ? Const.depthBidRowBg : Const.depthAskRowBg
                                border.color: rowData.isBid ? Const.depthBidRowBorder : Const.depthAskRowBorder
                                border.width: 1
                            }

                            Rectangle {
                                visible: rowData.kind === "divider"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: compactMode ? 2 : 6
                                anchors.rightMargin: compactMode ? 2 : 6
                                height: 1
                                color: Const.depthAggregateBar
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: compactMode ? 5 : 8
                                spacing: compactMode ? 4 : 8
                                visible: rowData.kind === "depth"

                                Text { text:rowData.side+rowData.level; color:rowData.isBid?Const.depthBidText:Const.depthAskText; font.pixelSize:compactBookTagFont; font.weight:Font.DemiBold; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                                Text { text:root.formatPrice(rowData.price); color:Const.tradingBrightText; font.pixelSize:compactBookPriceFont; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                                Text { text:root.formatLotCount(rowData.volume); color:Const.depthInfoText; font.pixelSize:compactBookVolumeFont; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                                Text { text:root.formatShareCount(rowData.volume); color:Const.depthInfoText; font.pixelSize:compactBookVolumeFont; Layout.fillWidth:true; horizontalAlignment:Text.AlignHCenter }
                                Text { text:root.formatAmount(rowData.price,rowData.volume); color:rowData.isBid?Const.depthBidVolumeText:Const.depthAskVolumeText
                                    font.pixelSize: compactBookVolumeFont
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.depthTableRows.length === 0
                        text: "暂无盘口数据"
                        color: Const.depthLabelText
                        font.pixelSize: compactMetaFont
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // 涨跌停遮罩
                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    color: root.isLimitBoard ? (marketSnapshot.limitUp ? "#33cc0022" : "#33008822") : "transparent"
                    visible: root.isLimitBoard
                    border.color: root.isLimitBoard ? root.statusBadgeColor() : "transparent"
                    border.width: 2
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignTop
                radius: 22
                color: Const.tradingPanelBgAlt
                border.color: Const.tradingFormAreaBorder
                border.width: 1
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: compactMode ? 8 : 14
                    spacing: compactMode ? 5 : 10

                    Text {
                        text: "L2"
                        color: Const.tradingBrightText
                        font.pixelSize: compactSectionTitleFont
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactMode ? 24 : 32
                        radius: compactMode ? 10 : 12
                        color: Const.depthInactiveLevelBg

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: compactMode ? 6 : 10

                            Text { text: "时间"; color: Const.depthLabelText; font.pixelSize: compactMetaFont }
                            Item { Layout.fillWidth: true }
                            Text { text: "方向"; color: Const.depthLabelText; font.pixelSize: compactMetaFont }
                            Item { Layout.fillWidth: true }
                            Text { text: "价格"; color: Const.depthLabelText; font.pixelSize: compactMetaFont }
                            Item { Layout.fillWidth: true }
                            Text { text: "成交量"; color: Const.depthLabelText; font.pixelSize: compactMetaFont }
                        }
                    }

                    ListView {
                        id: tickListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: compactMode ? 3 : 6
                        model: root.tickRows
                        onCountChanged: { if(count>0) positionViewAtEnd() }

                        delegate: Rectangle {
                            property var tickData: modelData
                            width: ListView.view.width
                            height: compactTradeRowHeight
                            radius: compactMode ? 10 : 12
                            color: Const.tradingHeaderBg
                            border.color: tickData.direction === "buy" ? Const.depthTickBuyBorder : Const.depthTickSellBorder
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: compactMode ? 6 : 10

                                Text {
                                    text: tickData.time
                                    color: Const.depthTickNeutralText
                                    font.pixelSize: compactMetaFont
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: tickData.direction === "buy" ? "买盘" : "卖盘"
                                    color: tickData.direction === "buy" ? Const.depthBidText : Const.depthAskText
                                    font.pixelSize: compactMetaFont
                                    font.weight: Font.DemiBold
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: root.formatPrice(tickData.price)
                                    color: Const.tradingBrightText
                                    font.pixelSize: compactMetaFont
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: root.formatVolume(tickData.volume)
                                    color: Const.depthInfoText
                                    font.pixelSize: compactMetaFont
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
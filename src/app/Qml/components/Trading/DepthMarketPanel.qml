import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    signal depthLevelsChanged(int levels)

    radius: 28
    color: "#07101c"
    border.color: "#1b2c40"
    border.width: 1
    implicitHeight: compactMode ? 600 : 760

    property var marketSnapshot: ({})
    property var depthSnapshot: ({ bids: [], asks: [], totalBid: 0, totalAsk: 0 })
    property var tickRows: []
    property string activeMode: "stock"
    property string activeSymbol: "000001"
    property bool compactMode: false
    property int selectedDepthLevels: 5
    readonly property int requestedDepthLevels: selectedDepthLevels > 10 ? 10 : (selectedDepthLevels < 5 ? 5 : selectedDepthLevels)
    readonly property int compactHeaderFont: compactMode ? 15 : 20
    readonly property int compactSectionTitleFont: compactMode ? 12 : 15
    readonly property int compactStatCardHeight: compactMode ? 48 : 64
    readonly property int compactBookRowHeight: compactMode ? 24 : 30
    readonly property int compactTradeRowHeight: compactMode ? 26 : 34
    readonly property int compactMidPriceHeight: compactMode ? 26 : 34
    readonly property int compactOrderBookHeight: compactMode ? 310 : 420
    readonly property int compactPanelGap: compactMode ? 6 : 12
    readonly property int compactBookTagFont: compactMode ? 8 : 11
    readonly property int compactBookPriceFont: compactMode ? 10 : 12
    readonly property int compactBookVolumeFont: compactMode ? 8 : 11
    readonly property int compactMetaFont: compactMode ? 9 : 11
    readonly property int compactTradePanelWidth: compactMode ? 150 : 256
    readonly property int compactDepthChipHeight: compactMode ? 20 : 26
    readonly property int compactDepthHeaderHeight: compactMode ? 24 : 30
    readonly property int compactDepthSideWidth: compactMode ? 24 : 34
    readonly property int compactDepthPriceWidth: compactMode ? 40 : 60
    readonly property int compactDepthLotWidth: compactMode ? 28 : 44
    readonly property int compactDepthShareWidth: compactMode ? 38 : 56
    readonly property int compactDepthAmountWidth: compactMode ? 48 : 68
    readonly property bool l2PanelVisible: !!(depthSnapshot && depthSnapshot.live && tickRows && tickRows.length > 0)
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

    readonly property color trendColor: !hasDisplayPrice ? "#94a3b8" : (trendUp() ? "#00cc88" : "#ff6a00")

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 27
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0b1524" }
            GradientStop { position: 1.0; color: "#060c16" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: compactMode ? 16 : 24
        spacing: compactMode ? 10 : 18

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 4

                Text {
                    text: ((marketSnapshot && marketSnapshot.name ? String(marketSnapshot.name) + "  ·  " : "")
                        + (marketSnapshot && marketSnapshot.symbol ? String(marketSnapshot.symbol) : activeSymbol)
                        + "  ·  " + modeName())
                    color: "#eff6ff"
                    font.pixelSize: compactHeaderFont
                    font.weight: Font.DemiBold
                }

                Text {
                    text: quoteStateText()
                    color: "#7f96b8"
                    font.pixelSize: compactMode ? 10 : 12
                }
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                spacing: 2

                Text {
                    text: {
                        var priceText = formatPrice(currentPrice())
                        if (priceText === "--") {
                            return priceText
                        }
                        return (activeMode === "stock" || activeMode === "margin_buy" || activeMode === "margin_sell" ? "¥" : "") + priceText
                    }
                    color: root.trendColor
                    font.pixelSize: compactMode ? 20 : 28
                    font.weight: Font.Bold
                }

                Text {
                    text: trendText()
                    color: root.trendColor
                    font.pixelSize: compactMode ? 10 : 13
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: compactMode ? 6 : 10
            rowSpacing: compactMode ? 6 : 10

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: compactStatCardHeight
                radius: 16
                color: "#0c1828"
                border.color: "#1b3047"
                border.width: 1

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: compactMode ? 12 : 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: compactMode ? 4 : 6

                    Text {
                        text: "总委买"
                        color: "#7f96b8"
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: formatVolume(depthSnapshot.totalBid)
                        color: "#eff6ff"
                        font.pixelSize: compactMode ? 13 : 16
                        font.weight: Font.DemiBold
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: compactStatCardHeight
                radius: 16
                color: "#0c1828"
                border.color: "#1b3047"
                border.width: 1

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: compactMode ? 12 : 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: compactMode ? 4 : 6

                    Text {
                        text: "总委卖"
                        color: "#7f96b8"
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: formatVolume(depthSnapshot.totalAsk)
                        color: "#eff6ff"
                        font.pixelSize: compactMode ? 13 : 16
                        font.weight: Font.DemiBold
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: compactStatCardHeight
                radius: 16
                color: "#0c1828"
                border.color: "#1b3047"
                border.width: 1

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: compactMode ? 12 : 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: compactMode ? 4 : 6

                    Text {
                        text: "逐笔数"
                        color: "#7f96b8"
                        font.pixelSize: compactMetaFont
                    }

                    Text {
                        text: String(root.tickRows.length)
                        color: "#eff6ff"
                        font.pixelSize: compactMode ? 13 : 16
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

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
                color: "#091321"
                border.color: "#1b3047"
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
                            color: "#eff6ff"
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
                                color: root.requestedDepthLevels === modelData ? "#176b78" : "#0d1a2b"
                                border.color: root.requestedDepthLevels === modelData ? "#39c6d6" : "#28405d"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: String(modelData)
                                    color: root.requestedDepthLevels === modelData ? "#effbff" : "#8ea8cb"
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
                        color: "#7f96b8"
                        font.pixelSize: compactMetaFont
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactDepthHeaderHeight
                        radius: compactMode ? 10 : 12
                        color: "#0d1a2b"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: compactMode ? 5 : 8
                            spacing: compactMode ? 4 : 8

                            Text {
                                text: "档"
                                color: "#7f96b8"
                                font.pixelSize: compactMetaFont
                                Layout.preferredWidth: compactDepthSideWidth
                            }
                            Text {
                                text: "价格"
                                color: "#7f96b8"
                                font.pixelSize: compactMetaFont
                                Layout.preferredWidth: compactDepthPriceWidth
                                horizontalAlignment: Text.AlignRight
                            }
                            Text {
                                text: "手数"
                                color: "#7f96b8"
                                font.pixelSize: compactMetaFont
                                Layout.preferredWidth: compactDepthLotWidth
                                horizontalAlignment: Text.AlignRight
                            }
                            Text {
                                text: root.shareCountLabel
                                color: "#7f96b8"
                                font.pixelSize: compactMetaFont
                                Layout.preferredWidth: compactDepthShareWidth
                                horizontalAlignment: Text.AlignRight
                            }
                            Text {
                                text: "金额"
                                color: "#7f96b8"
                                font.pixelSize: compactMetaFont
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignRight
                            }
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
                                color: rowData.isBid ? "#0f201f" : "#251316"
                                border.color: rowData.isBid ? "#1b5d57" : "#6d2931"
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
                                color: "#1f5f6a"
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: compactMode ? 5 : 8
                                spacing: compactMode ? 4 : 8
                                visible: rowData.kind === "depth"

                                Text {
                                    text: rowData.side + rowData.level
                                    color: rowData.isBid ? "#5eead4" : "#fca5a5"
                                    font.pixelSize: compactBookTagFont
                                    font.weight: Font.DemiBold
                                    Layout.preferredWidth: compactDepthSideWidth
                                }
                                Text {
                                    text: root.formatPrice(rowData.price)
                                    color: "#eff6ff"
                                    font.pixelSize: compactBookPriceFont
                                    Layout.preferredWidth: compactDepthPriceWidth
                                    horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.formatLotCount(rowData.volume)
                                    color: "#9fb4d2"
                                    font.pixelSize: compactBookVolumeFont
                                    Layout.preferredWidth: compactDepthLotWidth
                                    horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.formatShareCount(rowData.volume)
                                    color: "#9fb4d2"
                                    font.pixelSize: compactBookVolumeFont
                                    Layout.preferredWidth: compactDepthShareWidth
                                    horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.formatAmount(rowData.price, rowData.volume)
                                    color: rowData.isBid ? "#8ef1d8" : "#ffb4b8"
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
                        color: "#7f96b8"
                        font.pixelSize: compactMetaFont
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            Rectangle {
                visible: root.l2PanelVisible
                Layout.preferredWidth: root.l2PanelVisible ? compactTradePanelWidth : 0
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignTop
                radius: 22
                color: "#091321"
                border.color: "#1b3047"
                border.width: 1
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: compactMode ? 8 : 14
                    spacing: compactMode ? 5 : 10

                    Text {
                        text: "L2"
                        color: "#eff6ff"
                        font.pixelSize: compactSectionTitleFont
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: compactMode ? 24 : 32
                        radius: compactMode ? 10 : 12
                        color: "#0d1a2b"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: compactMode ? 6 : 10

                            Text { text: "时间"; color: "#7f96b8"; font.pixelSize: compactMetaFont }
                            Item { Layout.fillWidth: true }
                            Text { text: "方向"; color: "#7f96b8"; font.pixelSize: compactMetaFont }
                            Item { Layout.fillWidth: true }
                            Text { text: "价格"; color: "#7f96b8"; font.pixelSize: compactMetaFont }
                            Item { Layout.fillWidth: true }
                            Text { text: "成交量"; color: "#7f96b8"; font.pixelSize: compactMetaFont }
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: compactMode ? 3 : 6
                        model: root.tickRows

                        delegate: Rectangle {
                            property var tickData: modelData
                            width: ListView.view.width
                            height: compactTradeRowHeight
                            radius: compactMode ? 10 : 12
                            color: "#0c1828"
                            border.color: tickData.direction === "buy" ? "#184f4a" : "#6c2b2f"
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: compactMode ? 6 : 10

                                Text {
                                    text: tickData.time
                                    color: "#cbd5e1"
                                    font.pixelSize: compactMetaFont
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: tickData.direction === "buy" ? "买盘" : "卖盘"
                                    color: tickData.direction === "buy" ? "#5eead4" : "#fca5a5"
                                    font.pixelSize: compactMetaFont
                                    font.weight: Font.DemiBold
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: root.formatPrice(tickData.price)
                                    color: "#eff6ff"
                                    font.pixelSize: compactMetaFont
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: root.formatVolume(tickData.volume)
                                    color: "#9fb4d2"
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
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var marketDataService: null
    property var displayPositions: []
    property string seedSymbol: ""
    property bool autoWatchSymbols: true

    color: "#0b1220"
    radius: 18
    border.color: "#1f2e45"
    border.width: 1

    property string activeSymbol: ""
    property var activeSnapshot: ({})
    property var trackedSymbols: []

    function safeNumber(value, fallback) {
        var numericValue = Number(value)
        return isNaN(numericValue) ? (fallback === undefined ? 0 : fallback) : numericValue
    }

    function normalizeSymbol(symbol) {
        return String(symbol || "").trim().toUpperCase()
    }

    function symbolDigits(symbol) {
        var normalized = normalizeSymbol(symbol)
        if (normalized.indexOf(".CFFEX") > 0 || normalized.indexOf(".SHFE") > 0
                || normalized.indexOf(".DCE") > 0 || normalized.indexOf(".CZCE") > 0
                || normalized.indexOf(".INE") > 0 || normalized.indexOf(".GFEX") > 0) {
            return 0
        }
        return 2
    }

    function formatPrice(value, symbol) {
        var numericValue = safeNumber(value, 0)
        if (numericValue <= 0) {
            return "--"
        }
        return numericValue.toFixed(symbolDigits(symbol || activeSymbol))
    }

    function formatPercent(value) {
        var numericValue = safeNumber(value, NaN)
        if (isNaN(numericValue)) {
            return "--"
        }
        var prefix = numericValue > 0 ? "+" : ""
        return prefix + numericValue.toFixed(2) + "%"
    }

    function formatAmount(value) {
        var numericValue = safeNumber(value, 0)
        if (numericValue >= 100000000) {
            return (numericValue / 100000000).toFixed(2) + "亿"
        }
        if (numericValue >= 10000) {
            return (numericValue / 10000).toFixed(2) + "万"
        }
        return numericValue.toFixed(0)
    }

    function resolveSnapshot(symbol) {
        var normalized = normalizeSymbol(symbol)
        if (!normalized || !marketDataService) {
            return ({})
        }

        if (typeof marketDataService.resolveInstrument === "function") {
            var resolved = marketDataService.resolveInstrument(normalized)
            if (resolved && Object.keys(resolved).length > 0) {
                return resolved
            }
        }

        var snapshots = marketDataService.marketSnapshots || []
        for (var index = 0; index < snapshots.length; ++index) {
            var item = snapshots[index] || ({})
            if (normalizeSymbol(item.symbol) === normalized) {
                return item
            }
        }

        return ({ symbol: normalized, name: "", price: 0, change: 0, updatedAt: "--" })
    }

    function firstNonEmptySymbol() {
        var candidates = []
        if (seedSymbol) {
            candidates.push(seedSymbol)
        }
        if (marketDataService && marketDataService.primarySymbol) {
            candidates.push(marketDataService.primarySymbol)
        }
        if (!autoWatchSymbols) {
            for (var passiveIndex = 0; passiveIndex < candidates.length; ++passiveIndex) {
                var passiveSymbol = normalizeSymbol(candidates[passiveIndex])
                if (passiveSymbol.length > 0) {
                    return passiveSymbol
                }
            }
            return ""
        }
        for (var index = 0; index < displayPositions.length; ++index) {
            var position = displayPositions[index] || ({})
            if (position.symbol) {
                candidates.push(position.symbol)
            }
        }
        if (marketDataService && marketDataService.marketSnapshots) {
            var snapshots = marketDataService.marketSnapshots
            for (var snapshotIndex = 0; snapshotIndex < snapshots.length; ++snapshotIndex) {
                var snapshot = snapshots[snapshotIndex] || ({})
                if (snapshot.symbol) {
                    candidates.push(snapshot.symbol)
                }
            }
        }

        for (var candidateIndex = 0; candidateIndex < candidates.length; ++candidateIndex) {
            var normalized = normalizeSymbol(candidates[candidateIndex])
            if (normalized.length > 0) {
                return normalized
            }
        }
        return ""
    }

    function rebuildTrackedSymbols() {
        var nextSymbols = []
        var seen = {}
        var pushSymbol = function(symbol, name) {
            var normalized = normalizeSymbol(symbol)
            if (!normalized || seen[normalized]) {
                return
            }
            seen[normalized] = true
            nextSymbols.push({
                symbol: normalized,
                name: String(name || "").trim()
            })
        }

        pushSymbol(activeSymbol)
        pushSymbol(seedSymbol)
        if (marketDataService && marketDataService.primarySymbol) {
            pushSymbol(marketDataService.primarySymbol)
        }

        if (!autoWatchSymbols) {
            trackedSymbols = nextSymbols
            return
        }

        for (var index = 0; index < displayPositions.length && nextSymbols.length < 8; ++index) {
            var position = displayPositions[index] || ({})
            pushSymbol(position.symbol, position.name)
        }

        var snapshots = marketDataService && marketDataService.marketSnapshots ? marketDataService.marketSnapshots : []
        for (var snapshotIndex = 0; snapshotIndex < snapshots.length && nextSymbols.length < 8; ++snapshotIndex) {
            var snapshot = snapshots[snapshotIndex] || ({})
            pushSymbol(snapshot.symbol, snapshot.name)
        }

        trackedSymbols = nextSymbols
    }

    function ensureSymbolTracked(symbol) {
        var normalized = normalizeSymbol(symbol)
        if (!autoWatchSymbols || !visible || !normalized || !marketDataService) {
            return
        }
        if (typeof marketDataService.ensureWatchSymbol === "function") {
            marketDataService.ensureWatchSymbol(normalized)
        }
    }

    function refreshWorkspace() {
        if (!visible) {
            rebuildTrackedSymbols()
            return
        }
        if (!activeSymbol) {
            activeSymbol = firstNonEmptySymbol()
        }
        ensureSymbolTracked(activeSymbol)
        activeSnapshot = resolveSnapshot(activeSymbol)
        rebuildTrackedSymbols()
    }

    function setActiveSymbol(symbol) {
        var normalized = normalizeSymbol(symbol)
        if (!normalized) {
            return
        }
        activeSymbol = normalized
        refreshWorkspace()
    }

    function topPositions() {
        var rows = []
        for (var index = 0; index < displayPositions.length && rows.length < 5; ++index) {
            var position = displayPositions[index] || ({})
            rows.push(position)
        }
        return rows
    }

    function totalPositionValue() {
        var total = 0
        for (var index = 0; index < displayPositions.length; ++index) {
            var position = displayPositions[index] || ({})
            total += safeNumber(position.currentValue !== undefined ? position.currentValue : position.marketValue, 0)
        }
        return total
    }

    function depthRows(sideKey) {
        var rows = activeSnapshot && activeSnapshot.depthSnapshot ? activeSnapshot.depthSnapshot[sideKey] : []
        return rows || []
    }

    function marketRangeRatio(value) {
        var low = safeNumber(activeSnapshot.low, 0)
        var high = safeNumber(activeSnapshot.high, 0)
        var numericValue = safeNumber(value, 0)
        if (high <= low || numericValue <= 0) {
            return 0.5
        }
        var ratio = (numericValue - low) / (high - low)
        if (ratio < 0) {
            return 0
        }
        if (ratio > 1) {
            return 1
        }
        return ratio
    }

    onSeedSymbolChanged: {
        if (!activeSymbol || normalizeSymbol(activeSymbol) !== normalizeSymbol(seedSymbol)) {
            setActiveSymbol(seedSymbol)
        }
    }

    onDisplayPositionsChanged: refreshWorkspace()

    Component.onCompleted: {
        if (visible) {
            activeSymbol = firstNonEmptySymbol()
            refreshWorkspace()
        }
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }
        activeSymbol = firstNonEmptySymbol()
        refreshWorkspace()
    }

    Connections {
        target: marketDataService
        enabled: root.visible && !!marketDataService

        function onMarketSnapshotsChanged() {
            refreshWorkspace()
        }

        function onPrimarySymbolChanged() {
            if (!root.activeSymbol) {
                root.activeSymbol = root.firstNonEmptySymbol()
            }
            refreshWorkspace()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                spacing: 4

                Text {
                    text: String(activeSnapshot.name || "交易工作区")
                    color: "#f8fafc"
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }

                Text {
                    text: normalizeSymbol(activeSnapshot.symbol || activeSymbol)
                    color: "#8ea3bd"
                    font.pixelSize: 12
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                radius: 999
                color: safeNumber(activeSnapshot.price, 0) > 0 ? "#0f2234" : "#2a1a1a"
                border.color: safeNumber(activeSnapshot.price, 0) > 0 ? "#24517a" : "#6b2a2a"
                border.width: 1
                implicitHeight: 32
                implicitWidth: stateLabel.width + 24

                Text {
                    id: stateLabel
                    anchors.centerIn: parent
                    text: safeNumber(activeSnapshot.price, 0) > 0 ? "行情已连接" : "等待行情"
                    color: safeNumber(activeSnapshot.price, 0) > 0 ? "#7dd3fc" : "#fca5a5"
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: trackedSymbols

                delegate: Rectangle {
                    required property var modelData

                    radius: 12
                    height: 34
                    color: normalizeSymbol(modelData.symbol) === normalizeSymbol(activeSymbol) ? "#18314e" : "#101a2a"
                    border.color: normalizeSymbol(modelData.symbol) === normalizeSymbol(activeSymbol) ? "#4f8cff" : "#26364d"
                    border.width: 1
                    width: symbolText.width + 22

                    Text {
                        id: symbolText
                        anchors.centerIn: parent
                        text: String(modelData.name || modelData.symbol)
                        color: normalizeSymbol(modelData.symbol) === normalizeSymbol(activeSymbol) ? "#dbeafe" : "#a5b4c7"
                        font.pixelSize: 12
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.setActiveSymbol(parent.modelData.symbol)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 420
                radius: 16
                color: "#0f1726"
                border.color: "#223147"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            spacing: 6

                            Text {
                                text: "最新报价"
                                color: "#8ea3bd"
                                font.pixelSize: 12
                            }

                            Text {
                                text: formatPrice(activeSnapshot.price, activeSymbol)
                                color: safeNumber(activeSnapshot.change, 0) >= 0 ? "#f87171" : "#34d399"
                                font.pixelSize: 34
                                font.weight: Font.Bold
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ColumnLayout {
                            spacing: 6

                            Text {
                                text: "涨跌幅"
                                color: "#8ea3bd"
                                font.pixelSize: 12
                            }

                            Text {
                                text: formatPercent(activeSnapshot.change)
                                color: safeNumber(activeSnapshot.change, 0) >= 0 ? "#fca5a5" : "#6ee7b7"
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        rowSpacing: 10
                        columnSpacing: 12

                        Repeater {
                            model: [
                                { label: "开盘", value: formatPrice(activeSnapshot.open, activeSymbol) },
                                { label: "昨收", value: formatPrice(activeSnapshot.preClose, activeSymbol) },
                                { label: "最高", value: formatPrice(activeSnapshot.high, activeSymbol) },
                                { label: "最低", value: formatPrice(activeSnapshot.low, activeSymbol) },
                                { label: "成交量", value: formatAmount(activeSnapshot.volume) },
                                { label: "成交额", value: formatAmount(activeSnapshot.amount) },
                                { label: "更新时间", value: String(activeSnapshot.updatedAt || "--") },
                                { label: "盘口层级", value: String((activeSnapshot.depthSnapshot && activeSnapshot.depthSnapshot.levelCount) || 0) }
                            ]

                            delegate: Rectangle {
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.preferredHeight: 66
                                radius: 12
                                color: "#101d31"
                                border.color: "#20324a"
                                border.width: 1

                                Column {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.margins: 12
                                    spacing: 6

                                    Text {
                                        text: modelData.label
                                        color: "#7c93af"
                                        font.pixelSize: 11
                                    }

                                    Text {
                                        text: modelData.value
                                        color: "#eff6ff"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 118
                        radius: 14
                        color: "#0c1626"
                        border.color: "#20314a"
                        border.width: 1

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            Text {
                                text: "日内价格区间"
                                color: "#cbd5e1"
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }

                            Rectangle {
                                width: parent.width
                                height: 8
                                radius: 999
                                color: "#13233a"

                                Rectangle {
                                    width: 2
                                    height: 20
                                    radius: 1
                                    color: "#93c5fd"
                                    x: Math.max(0, parent.width * marketRangeRatio(activeSnapshot.preClose) - width / 2)
                                    y: -6
                                }

                                Rectangle {
                                    width: 2
                                    height: 20
                                    radius: 1
                                    color: "#fbbf24"
                                    x: Math.max(0, parent.width * marketRangeRatio(activeSnapshot.open) - width / 2)
                                    y: -6
                                }

                                Rectangle {
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: safeNumber(activeSnapshot.change, 0) >= 0 ? "#ef4444" : "#10b981"
                                    border.color: "#f8fafc"
                                    border.width: 1
                                    x: Math.max(0, parent.width * marketRangeRatio(activeSnapshot.price) - width / 2)
                                    y: -3
                                }
                            }

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: "低点 " + formatPrice(activeSnapshot.low, activeSymbol)
                                    color: "#7c93af"
                                    font.pixelSize: 11
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "昨收 " + formatPrice(activeSnapshot.preClose, activeSymbol)
                                    color: "#93c5fd"
                                    font.pixelSize: 11
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "开盘 " + formatPrice(activeSnapshot.open, activeSymbol)
                                    color: "#fbbf24"
                                    font.pixelSize: 11
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "高点 " + formatPrice(activeSnapshot.high, activeSymbol)
                                    color: "#7c93af"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 14
                        color: "#0c1626"
                        border.color: "#20314a"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            Text {
                                text: "五档盘口"
                                color: "#cbd5e1"
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Repeater {
                                        model: depthRows("bids").slice(0, 5)

                                        delegate: RowLayout {
                                            required property int index
                                            required property var modelData
                                            Layout.fillWidth: true

                                            Text {
                                                text: "买" + String(index + 1)
                                                color: "#6ee7b7"
                                                font.pixelSize: 11
                                            }
                                            Item { Layout.fillWidth: true }
                                            Text {
                                                text: formatPrice(modelData.price, activeSymbol)
                                                color: "#e2e8f0"
                                                font.pixelSize: 11
                                            }
                                            Text {
                                                text: formatAmount(modelData.volume)
                                                color: "#7c93af"
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 1
                                    Layout.fillHeight: true
                                    color: "#20314a"
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Repeater {
                                        model: depthRows("asks").slice(0, 5)

                                        delegate: RowLayout {
                                            required property int index
                                            required property var modelData
                                            Layout.fillWidth: true

                                            Text {
                                                text: "卖" + String(index + 1)
                                                color: "#fca5a5"
                                                font.pixelSize: 11
                                            }
                                            Item { Layout.fillWidth: true }
                                            Text {
                                                text: formatPrice(modelData.price, activeSymbol)
                                                color: "#e2e8f0"
                                                font.pixelSize: 11
                                            }
                                            Text {
                                                text: formatAmount(modelData.volume)
                                                color: "#7c93af"
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: depthRows("bids").length === 0 && depthRows("asks").length === 0
                                text: "当前没有实时盘口，已保留行情摘要与持仓联动。"
                                color: "#7c93af"
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                radius: 16
                color: "#0f1726"
                border.color: "#223147"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    Text {
                        text: "持仓概览"
                        color: "#f8fafc"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 72
                            radius: 12
                            color: "#101d31"
                            border.color: "#20324a"
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                Text {
                                    text: "仓位条目"
                                    color: "#7c93af"
                                    font.pixelSize: 11
                                }
                                Text {
                                    text: String(displayPositions.length)
                                    color: "#eff6ff"
                                    font.pixelSize: 22
                                    font.weight: Font.Bold
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 72
                            radius: 12
                            color: "#101d31"
                            border.color: "#20324a"
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                Text {
                                    text: "市值合计"
                                    color: "#7c93af"
                                    font.pixelSize: 11
                                }
                                Text {
                                    text: formatAmount(totalPositionValue())
                                    color: "#eff6ff"
                                    font.pixelSize: 22
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        Column {
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: topPositions()

                                delegate: Rectangle {
                                    required property var modelData

                                    width: parent.width
                                    height: 78
                                    radius: 12
                                    color: normalizeSymbol(modelData.symbol) === normalizeSymbol(activeSymbol) ? "#15263c" : "#101d31"
                                    border.color: normalizeSymbol(modelData.symbol) === normalizeSymbol(activeSymbol) ? "#4f8cff" : "#20324a"
                                    border.width: 1

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 6

                                        RowLayout {
                                            width: parent.width

                                            ColumnLayout {
                                                spacing: 2
                                                Text {
                                                    text: String(modelData.name || modelData.symbol)
                                                    color: "#eff6ff"
                                                    font.pixelSize: 13
                                                    font.weight: Font.Medium
                                                }
                                                Text {
                                                    text: String(modelData.symbol || "")
                                                    color: "#7c93af"
                                                    font.pixelSize: 11
                                                }
                                            }

                                            Item { Layout.fillWidth: true }

                                            Text {
                                                text: formatPercent(modelData.pnlRate)
                                                color: safeNumber(modelData.pnl, 0) >= 0 ? "#fca5a5" : "#6ee7b7"
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                            }
                                        }

                                        RowLayout {
                                            width: parent.width

                                            Text {
                                                text: "数量 " + String(Math.round(safeNumber(modelData.quantity, 0)))
                                                color: "#cbd5e1"
                                                font.pixelSize: 11
                                            }
                                            Item { Layout.fillWidth: true }
                                            Text {
                                                text: "市值 " + formatAmount(modelData.currentValue)
                                                color: "#cbd5e1"
                                                font.pixelSize: 11
                                            }
                                            Item { Layout.fillWidth: true }
                                            Text {
                                                text: "权重 " + formatPercent(modelData.weight)
                                                color: "#cbd5e1"
                                                font.pixelSize: 11
                                            }
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.setActiveSymbol(parent.modelData.symbol)
                                    }
                                }
                            }

                            Text {
                                visible: displayPositions.length === 0
                                width: parent.width
                                text: "当前没有可展示的仓位数据。"
                                color: "#7c93af"
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }
    }
}
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Item {
    id: analysisPage

    property bool hasSelectedStrategy: false
    property var selectedStrategyDetail: ({})
    property var latestBacktestRecord: ({})
    property bool latestBacktestExpanded: true
    property bool historyChartsExpanded: false
    property bool historyListExpanded: false
    property bool historySectionExpanded: false

    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color secondaryBg: "#1E293B"
    readonly property color riseRed: "#EF4444"
    readonly property color warningAmber: "#F59E0B"
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    readonly property real spacingXLarge: 24
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    readonly property real borderRadiusXLarge: 16
    readonly property int historyPreviewCount: 3
    visible: hasSelectedStrategy
    implicitHeight: hasSelectedStrategy ? analysisColumn.implicitHeight : 0

    function formatBacktestPercentValue(value, decimals) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return number.toFixed(decimals === undefined ? 2 : decimals) + "%"
    }

    function formatBacktestNumberValue(value, decimals) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return number.toFixed(decimals === undefined ? 2 : decimals)
    }

    function formatBacktestIntegerValue(value) {
        var number = Number(value)
        if (isNaN(number)) {
            return "--"
        }
        return Math.round(number).toString()
    }

    function calculateMetricAxisBounds(history, metricKey, fallbackMin, fallbackMax) {
        if (!history || history.length === 0) {
            return { min: fallbackMin, max: fallbackMax }
        }

        var minValue = 0
        var maxValue = 0
        var initialized = false
        for (var index = 0; index < history.length; ++index) {
            var summary = history[index] && history[index].summary ? history[index].summary : ({})
            var currentValue = Number(summary[metricKey])
            if (isNaN(currentValue)) {
                continue
            }

            if (!initialized) {
                minValue = currentValue
                maxValue = currentValue
                initialized = true
            } else {
                minValue = Math.min(minValue, currentValue)
                maxValue = Math.max(maxValue, currentValue)
            }
        }

        if (!initialized) {
            return { min: fallbackMin, max: fallbackMax }
        }

        if (minValue === maxValue) {
            var singlePadding = Math.max(Math.abs(minValue) * 0.08, metricKey === "sharpeRatio" ? 0.2 : 1)
            return { min: minValue - singlePadding, max: maxValue + singlePadding }
        }

        var padding = (maxValue - minValue) * 0.1
        return { min: minValue - padding, max: maxValue + padding }
    }

    function updateHistoryMetricSeries(series, history, metricKey) {
        if (!series) {
            return
        }

        series.clear()
        if (!history) {
            return
        }

        for (var index = 0; index < history.length; ++index) {
            var summary = history[index] && history[index].summary ? history[index].summary : ({})
            var currentValue = Number(summary[metricKey])
            if (!isNaN(currentValue)) {
                series.append(index, currentValue)
            }
        }
    }

    ColumnLayout {
        id: analysisColumn
        width: parent.width
        spacing: spacingLarge

        Rectangle {
            Layout.fillWidth: true
            radius: borderRadiusXLarge
            color: secondaryBg
            border.color: Qt.rgba(71 / 255, 85 / 255, 105 / 255, 0.22)
            implicitHeight: latestBacktestColumn.implicitHeight + 40

            ColumnLayout {
                id: latestBacktestColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: spacingMedium

                property var latestBacktestItems: {
                    var latest = analysisPage.latestBacktestRecord || ({})
                    var summary = latest.summary || ({})
                    if (!latest || Object.keys(latest).length === 0) {
                        return []
                    }

                    return [
                        { label: "回测时间", value: latest.recordedAt || "--" },
                        { label: "回测范围", value: latest.universeLabel || "--" },
                        { label: "指数", value: latest.indexLabel || "--" },
                        { label: "数据源", value: latest.dataSourceMode || "--" },
                        { label: "区间", value: (latest.startDate || "--") + " ~ " + (latest.endDate || "--") },
                        { label: "总收益", value: analysisPage.formatBacktestPercentValue(summary.returns, 2) },
                        { label: "最大回撤", value: analysisPage.formatBacktestPercentValue(summary.maxDrawdown, 2) },
                        { label: "夏普比率", value: analysisPage.formatBacktestNumberValue(summary.sharpeRatio, 2) },
                        { label: "胜率", value: analysisPage.formatBacktestPercentValue(summary.winRate, 2) },
                        { label: "交易次数", value: analysisPage.formatBacktestIntegerValue(summary.tradesCount) },
                        { label: "运行天数", value: analysisPage.formatBacktestIntegerValue(summary.runningDays) },
                        { label: "净值点数", value: analysisPage.formatBacktestIntegerValue(latest.equityPointCount) }
                    ]
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "回测结果分析"
                        font.pixelSize: fontSizeLarge
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: latestBacktestColumn.latestBacktestItems.length > 0 ? "最近一次回测" : "暂无最近回测"
                        font.pixelSize: 12
                        color: textSecondary
                    }
                }

                Button {
                    text: latestBacktestExpanded ? "收起" : "展开"
                    visible: latestBacktestColumn.latestBacktestItems.length > 0
                    onClicked: latestBacktestExpanded = !latestBacktestExpanded
                }

                Text {
                    visible: latestBacktestColumn.latestBacktestItems.length === 0
                    text: "当前策略还没有可展示的回测记录。"
                    font.pixelSize: fontSizeNormal
                    color: textTertiary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                GridLayout {
                    visible: latestBacktestExpanded && latestBacktestColumn.latestBacktestItems.length > 0
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 10
                    rowSpacing: 8

                    Repeater {
                        model: latestBacktestColumn.latestBacktestItems

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            radius: 8
                            color: "#0B1220"

                            Column {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2

                                Text {
                                    text: modelData.label
                                    font.pixelSize: 11
                                    color: textTertiary
                                }

                                Text {
                                    text: modelData.value
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: textPrimary
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: borderRadiusXLarge
            color: secondaryBg
            border.color: Qt.rgba(71 / 255, 85 / 255, 105 / 255, 0.22)
            implicitHeight: historySection.implicitHeight + 40

            ColumnLayout {
                id: historySection
                anchors.fill: parent
                anchors.margins: 20
                spacing: spacingMedium

                property var backtestHistory: {
                    var detail = selectedStrategyDetail || ({})
                    var performance = detail.performanceMetrics || ({})
                    var history = performance.backtestHistory
                    return Array.isArray(history) ? history : []
                }
                property var visibleBacktestHistory: historyListExpanded
                    ? backtestHistory
                    : backtestHistory.slice(0, historyPreviewCount)

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "回测历史"
                        font.pixelSize: fontSizeLarge
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: historySection.backtestHistory.length > 0
                            ? ("最近 " + historySection.backtestHistory.length + " 条")
                            : "暂无历史"
                        font.pixelSize: 12
                        color: textSecondary
                    }

                    Button {
                        visible: historySection.backtestHistory.length > 0 && historySectionExpanded
                        text: historyChartsExpanded ? "收起图表" : "展开图表"
                        onClicked: historyChartsExpanded = !historyChartsExpanded
                    }

                    Button {
                        visible: historySection.backtestHistory.length > 0
                        text: historySectionExpanded ? "收起历史" : "展开历史"
                        onClicked: historySectionExpanded = !historySectionExpanded
                    }

                    Button {
                        visible: historySection.backtestHistory.length > historyPreviewCount
                            && historySectionExpanded
                        text: historyListExpanded ? "收起历史" : "展开全部历史"
                        onClicked: historyListExpanded = !historyListExpanded
                    }
                }

                Text {
                    visible: historySection.backtestHistory.length === 0
                    text: "这里会保留不同回测范围、不同日期区间的回测摘要，便于横向比较。"
                    font.pixelSize: fontSizeNormal
                    color: textTertiary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Loader {
                    active: historySectionExpanded
                        && historySection.backtestHistory.length > 0
                        && historyChartsExpanded
                    visible: active
                    Layout.fillWidth: true

                    sourceComponent: Rectangle {
                        radius: 10
                        color: "#0B1220"
                        border.color: "#1E293B"
                        border.width: 1
                        implicitHeight: Math.max(240, Math.min(420, width * 0.32))

                        ColumnLayout {
                            id: chartsColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            function refreshCharts() {
                                analysisPage.updateHistoryMetricSeries(historyReturnsSeries, historySection.backtestHistory, "returns")
                                analysisPage.updateHistoryMetricSeries(historyDrawdownSeries, historySection.backtestHistory, "maxDrawdown")
                                analysisPage.updateHistoryMetricSeries(historySharpeSeries, historySection.backtestHistory, "sharpeRatio")
                            }

                            Component.onCompleted: refreshCharts()

                            GridLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                columns: 3
                                columnSpacing: 12
                                rowSpacing: 12

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 8
                                    color: "#111827"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 8

                                        Text {
                                            text: "收益对比(%)"
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            color: textPrimary
                                        }

                                        ChartView {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            antialiasing: true
                                            legend.visible: false
                                            backgroundColor: "transparent"
                                            plotAreaColor: "transparent"

                                            ValueAxis {
                                                id: historyReturnsAxisX
                                                min: 0
                                                max: Math.max(1, historyReturnsSeries.count > 0 ? historyReturnsSeries.count - 1 : 1)
                                                tickCount: Math.min(6, Math.max(2, historyReturnsSeries.count > 1 ? 6 : 2))
                                                labelsColor: textTertiary
                                                gridLineColor: "#1E293B"
                                                lineVisible: false
                                            }

                                            ValueAxis {
                                                id: historyReturnsAxisY
                                                min: analysisPage.calculateMetricAxisBounds(historySection.backtestHistory, "returns", -5, 5).min
                                                max: analysisPage.calculateMetricAxisBounds(historySection.backtestHistory, "returns", -5, 5).max
                                                tickCount: 5
                                                labelsColor: textSecondary
                                                gridLineColor: "#1E293B"
                                                labelFormat: "%.1f"
                                            }

                                            LineSeries {
                                                id: historyReturnsSeries
                                                axisX: historyReturnsAxisX
                                                axisY: historyReturnsAxisY
                                                color: riseRed
                                                width: 2
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 8
                                    color: "#111827"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 8

                                        Text {
                                            text: "回撤对比(%)"
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            color: textPrimary
                                        }

                                        ChartView {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            antialiasing: true
                                            legend.visible: false
                                            backgroundColor: "transparent"
                                            plotAreaColor: "transparent"

                                            ValueAxis {
                                                id: historyDrawdownAxisX
                                                min: 0
                                                max: Math.max(1, historyDrawdownSeries.count > 0 ? historyDrawdownSeries.count - 1 : 1)
                                                tickCount: Math.min(6, Math.max(2, historyDrawdownSeries.count > 1 ? 6 : 2))
                                                labelsColor: textTertiary
                                                gridLineColor: "#1E293B"
                                                lineVisible: false
                                            }

                                            ValueAxis {
                                                id: historyDrawdownAxisY
                                                min: analysisPage.calculateMetricAxisBounds(historySection.backtestHistory, "maxDrawdown", 0, 10).min
                                                max: analysisPage.calculateMetricAxisBounds(historySection.backtestHistory, "maxDrawdown", 0, 10).max
                                                tickCount: 5
                                                labelsColor: textSecondary
                                                gridLineColor: "#1E293B"
                                                labelFormat: "%.1f"
                                            }

                                            LineSeries {
                                                id: historyDrawdownSeries
                                                axisX: historyDrawdownAxisX
                                                axisY: historyDrawdownAxisY
                                                color: warningAmber
                                                width: 2
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 8
                                    color: "#111827"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 8

                                        Text {
                                            text: "夏普对比"
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            color: textPrimary
                                        }

                                        ChartView {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            antialiasing: true
                                            legend.visible: false
                                            backgroundColor: "transparent"
                                            plotAreaColor: "transparent"

                                            ValueAxis {
                                                id: historySharpeAxisX
                                                min: 0
                                                max: Math.max(1, historySharpeSeries.count > 0 ? historySharpeSeries.count - 1 : 1)
                                                tickCount: Math.min(6, Math.max(2, historySharpeSeries.count > 1 ? 6 : 2))
                                                labelsColor: textTertiary
                                                gridLineColor: "#1E293B"
                                                lineVisible: false
                                            }

                                            ValueAxis {
                                                id: historySharpeAxisY
                                                min: analysisPage.calculateMetricAxisBounds(historySection.backtestHistory, "sharpeRatio", -1, 1).min
                                                max: analysisPage.calculateMetricAxisBounds(historySection.backtestHistory, "sharpeRatio", -1, 1).max
                                                tickCount: 5
                                                labelsColor: textSecondary
                                                gridLineColor: "#1E293B"
                                                labelFormat: "%.2f"
                                            }

                                            LineSeries {
                                                id: historySharpeSeries
                                                axisX: historySharpeAxisX
                                                axisY: historySharpeAxisY
                                                color: "#38BDF8"
                                                width: 2
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Column {
                    visible: historySectionExpanded && historySection.visibleBacktestHistory.length > 0
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: historySection.visibleBacktestHistory

                        delegate: Rectangle {
                            width: parent.width
                            radius: 10
                            color: "#0B1220"
                            border.color: "#1E293B"
                            border.width: 1
                            implicitHeight: historyContent.implicitHeight + 24

                            Column {
                                id: historyContent
                                x: 12
                                y: 12
                                width: parent.width - 24
                                spacing: 8

                                property var summary: modelData.summary || ({})

                                RowLayout {
                                    width: parent.width

                                    Text {
                                        text: modelData.recordedAt || ("历史记录 #" + String(index + 1))
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: textPrimary
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: (modelData.startDate || "--") + " ~ " + (modelData.endDate || "--")
                                        font.pixelSize: 12
                                        color: textSecondary
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: "回测范围: " + String(modelData.universeLabel || "--")
                                        + "，指数: " + String(modelData.indexLabel || "--")
                                        + "，数据源: " + String(modelData.dataSourceMode || "--")
                                    font.pixelSize: 12
                                    color: textSecondary
                                    wrapMode: Text.WordWrap
                                }

                                GridLayout {
                                    width: parent.width
                                    columns: 4
                                    columnSpacing: 8
                                    rowSpacing: 8

                                    Repeater {
                                        model: [
                                            { label: "收益", value: analysisPage.formatBacktestPercentValue(historyContent.summary.returns, 2) },
                                            { label: "回撤", value: analysisPage.formatBacktestPercentValue(historyContent.summary.maxDrawdown, 2) },
                                            { label: "夏普", value: analysisPage.formatBacktestNumberValue(historyContent.summary.sharpeRatio, 2) },
                                            { label: "胜率", value: analysisPage.formatBacktestPercentValue(historyContent.summary.winRate, 2) },
                                            { label: "交易次数", value: analysisPage.formatBacktestIntegerValue(historyContent.summary.tradesCount) },
                                            { label: "运行天数", value: analysisPage.formatBacktestIntegerValue(historyContent.summary.runningDays) },
                                            { label: "净值点数", value: analysisPage.formatBacktestIntegerValue(modelData.equityPointCount) },
                                            { label: "范围类型", value: String(modelData.selectedUniverseType || "--") }
                                        ]

                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 48
                                            radius: 8
                                            color: "#111827"

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 2

                                                Text {
                                                    text: modelData.label
                                                    font.pixelSize: 11
                                                    color: textTertiary
                                                }

                                                Text {
                                                    text: modelData.value
                                                    font.pixelSize: 13
                                                    color: textPrimary
                                                    elide: Text.ElideRight
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
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: spacingXLarge
        }
    }
}

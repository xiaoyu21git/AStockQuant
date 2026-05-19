import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    signal requestWriteBacktestMetrics(var report)

    property var factorService: null
    property string selectedFactorId: ""
    property var backtestReport: ({})
    property int factorDefinitionRevision: 0
    property bool suppressAutoAnalyze: false
    property bool showAuxiliaryMetrics: false

    readonly property color pageBackground: "#0B1220"
    readonly property color panelBackground: "#121A2B"
    readonly property color panelRaisedBackground: "#172235"
    readonly property color panelBorder: "#243247"
    readonly property color textPrimary: "#E5EEF8"
    readonly property color textSecondary: "#91A4BC"
    readonly property color accentStrong: "#F97316"
    readonly property color accentSoft: "#FDBA74"
    readonly property color positiveColor: "#22C55E"
    readonly property color negativeColor: "#EF4444"
    readonly property color riseColor: "#EF4444"
    readonly property color fallColor: "#22C55E"
    readonly property color neutralColor: "#CBD5E1"
    readonly property real cardGap: 14
    readonly property real compactBreakpoint: 860
    readonly property var activeResult: resolveActiveResult(backtestReport)
    readonly property var activeFactorQuality: extractFactorQuality(activeResult)
    readonly property bool hasActiveFactorQuality: hasKeys(activeFactorQuality)

    function normalizedListValue(value) {
        if (!value) {
            return []
        }
        if (Array.isArray(value)) {
            return value
        }
        if (typeof value.length === "number") {
            var normalized = []
            for (var index = 0; index < value.length; index++) {
                normalized.push(value[index])
            }
            return normalized
        }
        return []
    }

    function hasKeys(map) {
        if (!map || typeof map !== "object") {
            return false
        }
        for (var key in map) {
            if (Object.prototype.hasOwnProperty.call(map, key)) {
                return true
            }
        }
        return false
    }

    function resolveActiveResult(report) {
        var rawReport = report && typeof report === "object" ? report : ({})
        var results = normalizedListValue(rawReport.results)
        if (results.length === 0) {
            return rawReport
        }

        var normalizedFactorId = String(selectedFactorId || "")
        if (normalizedFactorId) {
            for (var index = 0; index < results.length; index++) {
                var candidate = results[index] || ({})
                var candidateConfig = candidate.config || ({})
                var candidateFactorId = String(candidate.factorId || candidateConfig.factorId || "")
                if (candidateFactorId === normalizedFactorId) {
                    return candidate
                }
            }
        }

        return results[0] || ({})
    }

    function extractFactorQuality(result) {
        if (!result || typeof result !== "object") {
            return ({})
        }
        var metrics = result.metrics || ({})
        var factorQuality = metrics.factorQuality || ({})
        return factorQuality && typeof factorQuality === "object" ? factorQuality : ({})
    }

    function factorDisplayName() {
        var result = activeResult || ({})
        var config = result.config || ({})
        var byReport = String(config.factorName || result.factorName || "")
        if (byReport) {
            return byReport
        }

        var factorId = String(selectedFactorId || config.factorId || result.factorId || "")
        if (factorId && factorService && typeof factorService.getFactorById === "function") {
            var factorDetail = factorService.getFactorById(factorId) || ({})
            var detailName = String(factorDetail.factorName || factorDetail.displayName || "")
            if (detailName) {
                return detailName
            }
        }

        return factorId ? ("因子 " + factorId) : "未选择因子"
    }

    function factorIdText() {
        var result = activeResult || ({})
        var config = result.config || ({})
        var factorId = String(selectedFactorId || config.factorId || result.factorId || "")
        return factorId ? factorId : "-"
    }

    function dateRangeText() {
        var result = activeResult || ({})
        var config = result.config || ({})
        var startDate = String(config.startDate || "")
        var endDate = String(config.endDate || "")
        if (startDate && endDate) {
            return startDate + " 至 " + endDate
        }
        if (startDate || endDate) {
            return startDate || endDate
        }
        return "时间范围未知"
    }

    function benchmarkText() {
        var result = activeResult || ({})
        var config = result.config || ({})
        var benchmark = String(config.benchmarkSymbol || "")
        return benchmark ? benchmark : "未设置基准"
    }

    function groupsText() {
        var metrics = activeFactorQuality || ({})
        var groups = Number(metrics.numGroups)
        if (!isFinite(groups) || groups <= 0) {
            var config = (activeResult || ({})).config || ({})
            groups = Number(config.numGroups)
        }
        if (!isFinite(groups) || groups <= 0) {
            groups = 0
        }
        return groups > 0 ? (groups.toFixed(0) + " 组") : "分组未知"
    }

    function ratingValue() {
        var raw = Number((activeFactorQuality || ({})).coreRating)
        if (!isFinite(raw)) {
            return 0
        }
        return Math.max(0, Math.min(3, Math.round(raw)))
    }

    function ratingText() {
        var label = String((activeFactorQuality || ({})).coreRatingLabel || "")
        return label ? label : "--"
    }

    function ratingTitleText() {
        var title = String((activeFactorQuality || ({})).coreRatingTitle || "")
        return title ? title : "因子质量"
    }

    function ratingSummaryText() {
        var summary = String((activeFactorQuality || ({})).coreRatingSummary || "")
        return summary
    }

    function ratingGateItems() {
        return normalizedListValue((activeFactorQuality || ({})).coreRatingChecks)
    }

    function sectionConfig(key) {
        var section = (activeFactorQuality || ({}))[key]
        return section && typeof section === "object" ? section : ({})
    }

    function auxiliarySectionSubtitle() {
        var section = sectionConfig("auxiliarySection")
        return showAuxiliaryMetrics
            ? String(section.expandedSubtitle || "")
            : String(section.collapsedSubtitle || "")
    }

    function ratingColor(value) {
        switch (Number(value)) {
        case 3:
            return positiveColor
        case 2:
            return "#38BDF8"
        case 1:
            return accentStrong
        default:
            return negativeColor
        }
    }

    function isFiniteNumber(value) {
        var numeric = Number(value)
        return isFinite(numeric)
    }

    function percentText(value, digits) {
        var numeric = Number(value)
        if (!isFinite(numeric)) {
            return "--"
        }
        return (numeric * 100).toFixed(digits) + "%"
    }

    function numberText(value, digits) {
        var numeric = Number(value)
        if (!isFinite(numeric)) {
            return "--"
        }
        return numeric.toFixed(digits)
    }

    function intText(value) {
        var numeric = Number(value)
        if (!isFinite(numeric)) {
            return "--"
        }
        return Math.round(numeric).toString()
    }

    function boolText(value) {
        return value ? "通过" : "未通过"
    }

    function metricText(metric) {
        if (!metric) {
            return "--"
        }
        switch (String(metric.format || "number")) {
        case "text":
            return String(metric.value || metric.displayText || "--")
        case "rating":
            return String(metric.displayText || "--")
        case "percent1":
            return percentText(metric.value, 1)
        case "percent2":
            return percentText(metric.value, 2)
        case "number3":
            return numberText(metric.value, 3)
        case "number2":
            return numberText(metric.value, 2)
        case "integer":
            return intText(metric.value)
        case "days":
            return intText(metric.value) + " 天"
        case "bool":
            return boolText(metric.value)
        default:
            return numberText(metric.value, 2)
        }
    }

    function metricTrend(metric) {
        if (!metric) {
            return "neutral"
        }
        if (metric.format === "rating") {
            return Number(metric.value) >= 2 ? "up" : "down"
        }
        if (metric.format === "bool") {
            return metric.value ? "up" : "down"
        }

        var numeric = Number(metric.value)
        if (!isFinite(numeric)) {
            return "neutral"
        }
        var direction = String(metric.direction || "high")
        if (direction === "neutral") {
            return "neutral"
        }
        if (direction === "low") {
            if (numeric < 0) {
                return "up"
            }
            return numeric <= Number(metric.goodThreshold || 0) ? "up" : "down"
        }
        if (direction === "center") {
            return "neutral"
        }
        return numeric >= Number(metric.goodThreshold || 0) ? "up" : "down"
    }

    function trendDisplayColor(trend) {
        if (trend === "up") {
            return riseColor
        }
        if (trend === "down") {
            return fallColor
        }
        return textPrimary
    }

    function metricSpanWidth(units, tier) {
        var containerWidth = Math.max(320, metricsColumn.width)
        if (containerWidth < compactBreakpoint) {
            return containerWidth
        }

        if (String(tier || "") === "core") {
            var coreColumns = containerWidth >= 1580 ? 5 : (containerWidth >= 1220 ? 4 : (containerWidth >= 980 ? 3 : 2))
            var coreGap = cardGap * (coreColumns - 1)
            return Math.max(196, Math.floor((containerWidth - coreGap) / coreColumns))
        }

        var totalUnits = 6
        var totalGap = cardGap * (totalUnits - 1)
        var unitWidth = Math.max(72, Math.floor((containerWidth - totalGap) / totalUnits))
        var span = Math.max(1, Number(units || 1))
        return span * unitWidth + (span - 1) * cardGap
    }

    function metricCardHeight(tier) {
        switch (String(tier || "optional")) {
        case "core":
            return 152
        case "auxiliary":
            return 108
        default:
            return 114
        }
    }

    function coreMetricItems() {
        return deduplicatedMetricItems(normalizedListValue((activeFactorQuality || ({})).coreMetrics))
    }

    function optionalMetricItems() {
        return deduplicatedMetricItems(normalizedListValue((activeFactorQuality || ({})).optionalMetrics))
    }

    function auxiliaryMetricItems() {
        return deduplicatedMetricItems(normalizedListValue((activeFactorQuality || ({})).auxiliaryMetrics))
    }

    function deduplicatedMetricItems(items) {
        var normalized = normalizedListValue(items)
        var filtered = []
        var seen = {}
        for (var index = 0; index < normalized.length; index++) {
            var metric = normalized[index]
            if (!metric || typeof metric !== "object") {
                continue
            }
            var key = String(metric.key || ("metric_" + index))
            if (seen[key]) {
                continue
            }
            seen[key] = true
            filtered.push(metric)
        }
        return filtered
    }

    function metricRowWidth(items, sectionKey) {
        var normalized = normalizedListValue(items)
        if (normalized.length === 0) {
            return 0
        }

        var totalWidth = 0
        for (var index = 0; index < normalized.length; index++) {
            var metric = normalized[index] || ({})
            totalWidth += sectionKey === "core"
                ? coreMetricCardWidth()
                : metricSpanWidth(metric.units, metric.tier)
        }
        totalWidth += cardGap * Math.max(0, normalized.length - 1)
        return totalWidth
    }

    function centeredFlowX(flowWidth, containerWidth) {
        return Math.max(0, Math.floor((Math.max(0, containerWidth - flowWidth)) / 2))
    }

    function groupChartItems() {
        return normalizedListValue((activeFactorQuality || ({})).groupCharts)
    }

    function analysisResultItems() {
        var items = normalizedListValue((backtestReport || ({})).results)
        var activeKey = activeAnalysisResultKey()
        return items.slice().sort(function(left, right) {
            var leftKey = analysisResultKey(left)
            var rightKey = analysisResultKey(right)
            if (leftKey === activeKey && rightKey !== activeKey) {
                return -1
            }
            if (rightKey === activeKey && leftKey !== activeKey) {
                return 1
            }

            var leftName = analysisResultDisplayText(left)
            var rightName = analysisResultDisplayText(right)
            if (leftName !== rightName) {
                return leftName.localeCompare(rightName, "zh-Hans-CN")
            }
            return leftKey.localeCompare(rightKey, "zh-Hans-CN")
        })
    }

    function analysisResultKey(result) {
        var candidate = result && typeof result === "object" ? result : ({})
        var config = candidate.config || ({})
        return String(candidate.factorId || config.factorId || "")
    }

    function analysisResultDisplayText(result) {
        var candidate = result && typeof result === "object" ? result : ({})
        var config = candidate.config || ({})
        var displayName = String(candidate.displayName || candidate.factorName || config.factorName || "")
        if (!displayName) {
            displayName = analysisResultKey(candidate)
        }
        return displayName ? displayName : "未命名因子"
    }

    function analysisResultSubtitle(result) {
        var candidate = result && typeof result === "object" ? result : ({})
        var config = candidate.config || ({})
        var factorId = analysisResultKey(candidate)
        var benchmark = String(config.benchmarkSymbol || "")
        if (factorId && benchmark) {
            return factorId + " · " + benchmark
        }
        if (factorId) {
            return factorId
        }
        return benchmark ? benchmark : ""
    }

    function activeAnalysisResultKey() {
        return analysisResultKey(activeResult)
    }

    function isAnalysisResultActive(result) {
        return analysisResultKey(result) === activeAnalysisResultKey()
    }

    function analysisSelectorCardWidth() {
        var containerWidth = Math.max(320, metricsColumn.width)
        var columns = Math.max(1, Math.min(6, analysisResultItems().length > 0 ? analysisResultItems().length : 6))
        var fixedWidth = Math.floor((containerWidth - cardGap * (columns - 1)) / columns)
        return Math.max(96, Math.min(120, fixedWidth))
    }

    function selectAnalysisResult(result) {
        var factorId = analysisResultKey(result)
        if (factorId) {
            selectedFactorId = factorId
        }
    }

    function coreMetricCardWidth() {
        var containerWidth = Math.max(320, metricsColumn.width)
        if (containerWidth < compactBreakpoint) {
            return containerWidth
        }
        if (containerWidth >= 980) {
            return Math.floor((containerWidth - cardGap * 4) / 5)
        }
        if (containerWidth >= 820) {
            return Math.floor((containerWidth - cardGap * 3) / 4)
        }
        return Math.floor((containerWidth - cardGap) / 2)
    }

    function returnSeriesSection() {
        var section = (activeFactorQuality || ({})).returnSeries
        return section && typeof section === "object" ? section : ({})
    }

    function returnSeriesValues(key) {
        return normalizedListValue(returnSeriesSection()[key])
    }

    function coreCurveSeries() {
        var values = cumulativeSeries(returnSeriesValues("riskAdjustedReturns"))
        if (values.length === 0) {
            values = cumulativeSeries(returnSeriesValues("costAdjustedReturns"))
        }
        if (values.length === 0) {
            values = cumulativeSeries(returnSeriesValues("rawReturns"))
        }
        return values
    }

    function seriesTerminalNetValue(key) {
        var series = cumulativeSeries(returnSeriesValues(key))
        return series.length > 0 ? Number(series[series.length - 1]) : NaN
    }

    function terminalNetValueText(key) {
        var value = seriesTerminalNetValue(key)
        if (!isFinite(value)) {
            return "--"
        }
        return numberText(value, 2) + "x"
    }

    function factorQualityNumericSeries(key) {
        var values = normalizedListValue((activeFactorQuality || ({}))[key])
        var series = []
        for (var index = 0; index < values.length; index++) {
            var numeric = Number(values[index])
            if (isFinite(numeric)) {
                series.push(numeric)
            }
        }
        return series
    }

    function cumulativeSeries(values) {
        var normalized = normalizedListValue(values)
        var cumulative = []
        var netValue = 1.0
        for (var index = 0; index < normalized.length; index++) {
            var value = Number(normalized[index])
            if (!isFinite(value)) {
                continue
            }
            netValue *= (1.0 + value)
            cumulative.push(netValue)
        }
        return cumulative
    }

    function curveMinValue(seriesList) {
        var minValue = 1.0
        for (var index = 0; index < seriesList.length; index++) {
            var series = seriesList[index]
            for (var pointIndex = 0; pointIndex < series.length; pointIndex++) {
                var pointValue = Number(series[pointIndex])
                if (isFinite(pointValue) && pointValue < minValue) {
                    minValue = pointValue
                }
            }
        }
        return minValue
    }

    function curveMaxValue(seriesList) {
        var maxValue = 1.0
        for (var index = 0; index < seriesList.length; index++) {
            var series = seriesList[index]
            for (var pointIndex = 0; pointIndex < series.length; pointIndex++) {
                var pointValue = Number(series[pointIndex])
                if (isFinite(pointValue) && pointValue > maxValue) {
                    maxValue = pointValue
                }
            }
        }
        return maxValue
    }

    function curvePointX(index, count, width, leftPadding, rightPadding) {
        if (count <= 1) {
            return leftPadding
        }
        return leftPadding + (width - leftPadding - rightPadding) * index / (count - 1)
    }

    function curvePointY(value, minValue, maxValue, height, topPadding, bottomPadding) {
        if (maxValue <= minValue) {
            return height - bottomPadding
        }
        var ratio = (value - minValue) / (maxValue - minValue)
        return height - bottomPadding - (height - topPadding - bottomPadding) * ratio
    }

    function maxAbsValue(series) {
        var normalized = normalizedListValue(series)
        var maxValue = 0
        for (var index = 0; index < normalized.length; index++) {
            var value = Math.abs(Number(normalized[index].value))
            if (isFinite(value) && value > maxValue) {
                maxValue = value
            }
        }
        return maxValue > 0 ? maxValue : 1
    }

    Rectangle {
        anchors.fill: parent
        color: pageBackground

        ScrollView {
            id: scrollView
            anchors.fill: parent
            anchors.margins: 18
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                id: metricsColumn
                width: Math.max(320, scrollView.width - 24)
                spacing: 18

                Rectangle {
                    visible: analysisResultItems().length > 0
                    width: parent.width
                    radius: 18
                    color: panelBackground
                    border.width: 1
                    border.color: panelBorder
                    height: selectorColumn.implicitHeight + 28

                    Column {
                        id: selectorColumn
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        Row {
                            width: parent.width
                            spacing: 10

                            Text {
                                text: "📚 多因子切换"
                                font.pixelSize: 18
                                font.weight: Font.Black
                                color: textPrimary
                            }

                            Item { width: 10; height: 1 }

                            Text {
                                text: "点击上方因子切换当前分析页"
                                font.pixelSize: 12
                                color: textSecondary
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        Flow {
                            width: Math.min(parent.width, metricRowWidth(coreMetricItems(), "core"))
                            x: centeredFlowX(width, parent.width)
                            spacing: cardGap

                            Repeater {
                                model: analysisResultItems()

                                delegate: Rectangle {
                                    property var candidateResult: modelData
                                    property bool selected: isAnalysisResultActive(candidateResult)

                                    width: analysisSelectorCardWidth()
                                    height: 56
                                    radius: 14
                                    color: selected ? Qt.rgba(accentStrong.r, accentStrong.g, accentStrong.b, 0.14) : panelRaisedBackground
                                    border.width: 1
                                    border.color: selected ? accentStrong : panelBorder

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 4

                                        Text {
                                            text: analysisResultDisplayText(candidateResult)
                                            width: parent.width
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            color: textPrimary
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: analysisResultSubtitle(candidateResult)
                                            width: parent.width
                                            font.pixelSize: 10
                                            color: selected ? accentSoft : textSecondary
                                            elide: Text.ElideRight
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.selectAnalysisResult(candidateResult)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    radius: 18
                    color: panelBackground
                    border.width: 1
                    border.color: panelBorder
                    height: headerColumn.implicitHeight + 28

                    Column {
                        id: headerColumn
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 14

                        Row {
                            width: parent.width
                            spacing: 12

                            Rectangle {
                                width: 104
                                height: 104
                                radius: 20
                                color: Qt.rgba(accentStrong.r, accentStrong.g, accentStrong.b, 0.14)
                                border.width: 1
                                border.color: Qt.rgba(accentSoft.r, accentSoft.g, accentSoft.b, 0.35)

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 6

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: ratingText()
                                        font.pixelSize: 24
                                        font.weight: Font.Black
                                        color: ratingColor(ratingValue())
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: ratingTitleText()
                                        font.pixelSize: 12
                                        color: textSecondary
                                    }
                                }
                            }

                            Column {
                                width: parent.width - 236
                                spacing: 8

                                Text {
                                    text: factorDisplayName()
                                    font.pixelSize: 24
                                    font.weight: Font.Black
                                    color: textPrimary
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    text: "因子 ID: " + factorIdText()
                                    font.pixelSize: 13
                                    color: textSecondary
                                }

                                Text {
                                    text: hasActiveFactorQuality
                                        ? ratingSummaryText()
                                        : "当前还没有可展示的 factorQuality 回测结果。"
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 13
                                    color: textSecondary
                                }
                            }

                            Rectangle {
                                width: 108
                                height: 40
                                radius: 12
                                color: hasActiveFactorQuality ? accentStrong : panelRaisedBackground
                                border.width: 1
                                border.color: hasActiveFactorQuality ? "#FB923C" : panelBorder
                                opacity: hasActiveFactorQuality ? 1 : 0.5

                                Text {
                                    anchors.centerIn: parent
                                    text: "写回指标"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: hasActiveFactorQuality ? "#1F1307" : textSecondary
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: hasActiveFactorQuality
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: root.requestWriteBacktestMetrics(activeResult)
                                }
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: [
                                    { label: "区间", value: dateRangeText() },
                                    { label: "基准", value: benchmarkText() },
                                    { label: "分组", value: groupsText() },
                                    { label: "定义版本", value: intText(factorDefinitionRevision) }
                                ]

                                delegate: Rectangle {
                                    radius: 999
                                    height: 34
                                    color: panelRaisedBackground
                                    border.width: 1
                                    border.color: panelBorder
                                    width: metricPill.implicitWidth + 22

                                    Text {
                                        id: metricPill
                                        anchors.centerIn: parent
                                        text: modelData.label + "  " + modelData.value
                                        font.pixelSize: 12
                                        color: textPrimary
                                    }
                                }
                            }
                        }

                        Flow {
                            visible: hasActiveFactorQuality && ratingGateItems().length > 0
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: ratingGateItems()

                                delegate: Rectangle {
                                    radius: 12
                                    height: 42
                                    color: modelData.passed ? Qt.rgba(positiveColor.r, positiveColor.g, positiveColor.b, 0.12) : Qt.rgba(negativeColor.r, negativeColor.g, negativeColor.b, 0.12)
                                    border.width: 1
                                    border.color: modelData.passed ? Qt.rgba(positiveColor.r, positiveColor.g, positiveColor.b, 0.35) : Qt.rgba(negativeColor.r, negativeColor.g, negativeColor.b, 0.35)
                                    width: gateText.implicitWidth + 26

                                    Text {
                                        id: gateText
                                        anchors.centerIn: parent
                                        text: modelData.label + "  " + modelData.actualText + "  " + modelData.thresholdText
                                        font.pixelSize: 12
                                        color: modelData.passed ? textPrimary : "#FECACA"
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: !hasActiveFactorQuality
                    width: parent.width
                    radius: 18
                    color: panelBackground
                    border.width: 1
                    border.color: panelBorder
                    height: 180

                    Column {
                        anchors.centerIn: parent
                        spacing: 10

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: selectedFactorId ? "等待当前因子的回测结果" : "先从因子库选择一个因子"
                            font.pixelSize: 22
                            font.weight: Font.Black
                            color: textPrimary
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(metricsColumn.width - 48, 560)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: selectedFactorId
                                ? "当前还没有可展示结果，请先完成一次回测。"
                                : "选择因子后，这里会显示最新分析结果。"
                            font.pixelSize: 13
                            color: textSecondary
                        }
                    }
                }

                Column {
                    visible: hasActiveFactorQuality
                    width: parent.width
                    spacing: 18

                    Column {
                        width: parent.width
                        spacing: 10

                        Text {
                            text: String(sectionConfig("coreSection").title || "")
                            font.pixelSize: 18
                            font.weight: Font.Black
                            color: textPrimary
                        }

                        Text {
                            text: String(sectionConfig("coreSection").subtitle || "")
                            font.pixelSize: 12
                            color: textSecondary
                        }

                        Flow {
                            width: Math.min(parent.width, metricRowWidth(coreMetricItems(), "core"))
                            x: centeredFlowX(width, parent.width)
                            spacing: cardGap

                            Repeater {
                                model: coreMetricItems()

                                delegate: Rectangle {
                                    width: coreMetricCardWidth()
                                    height: metricCardHeight(modelData.tier)
                                    radius: 18
                                    color: modelData.emphasize ? panelRaisedBackground : panelBackground
                                    border.width: 1
                                    border.color: modelData.emphasize ? "#FB923C" : panelBorder

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 5

                                        Text {
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: modelData.title
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            color: textSecondary
                                        }

                                        Row {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            spacing: 8

                                            Text {
                                                text: metricText(modelData)
                                                font.pixelSize: String(modelData.format || "") === "text"
                                                    ? 20
                                                    : (modelData.emphasize ? 25 : 21)
                                                font.weight: Font.Black
                                                color: modelData.format === "rating"
                                                    ? ratingColor(modelData.value)
                                                    : trendDisplayColor(metricTrend(modelData))
                                            }

                                            Text {
                                                visible: metricTrend(modelData) === "up" || metricTrend(modelData) === "down"
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: metricTrend(modelData) === "up" ? "↑" : "↓"
                                                font.pixelSize: 16
                                                color: trendDisplayColor(metricTrend(modelData))
                                            }
                                        }

                                        Rectangle {
                                            width: parent.width
                                            radius: 10
                                            color: Qt.rgba(accentStrong.r, accentStrong.g, accentStrong.b, 0.10)
                                            border.width: 1
                                            border.color: Qt.rgba(accentSoft.r, accentSoft.g, accentSoft.b, 0.18)
                                            height: coreNoteText.implicitHeight + 10

                                            Text {
                                                id: coreNoteText
                                                anchors.centerIn: parent
                                                width: parent.width - 14
                                                text: modelData.subtitle
                                                horizontalAlignment: Text.AlignHCenter
                                                wrapMode: Text.WordWrap
                                                font.pixelSize: 9
                                                color: textPrimary
                                            }
                                        }

                                        Text {
                                            visible: String(modelData.thresholdText || "") !== ""
                                            text: String(modelData.thresholdText || "")
                                            width: parent.width - 6
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 9
                                            color: accentSoft
                                        }
                                    }
                                }
                            }
                        }

                        Flow {
                            width: Math.min(parent.width, metricSpanWidth(6))
                            x: centeredFlowX(width, parent.width)
                            spacing: cardGap

                            Rectangle {
                                width: metricSpanWidth(3)
                                height: 174
                                radius: 16
                                color: panelBackground
                                border.width: 1
                                border.color: panelBorder

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 8

                                    Text {
                                        text: "核心指标达标观察"
                                        font.pixelSize: 14
                                        font.weight: Font.Black
                                        color: textPrimary
                                    }

                                    Text {
                                        text: "用紧凑进度条观察 5 个核心指标当前是否站在合理区间。"
                                        width: parent.width
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 11
                                        color: textSecondary
                                    }

                                    Repeater {
                                        model: coreMetricItems()

                                        delegate: Column {
                                            width: parent.width
                                            spacing: 4

                                            Row {
                                                width: parent.width
                                                spacing: 8

                                                Text {
                                                    width: 84
                                                    text: modelData.title
                                                    font.pixelSize: 10
                                                    color: textSecondary
                                                    elide: Text.ElideRight
                                                }

                                                Rectangle {
                                                    width: parent.width - 154
                                                    height: 8
                                                    radius: 4
                                                    color: panelRaisedBackground

                                                    Rectangle {
                                                        width: String(modelData.format || "") === "text"
                                                            ? parent.width * 0.35
                                                            : Math.max(10, parent.width * (metricTrend(modelData) === "up" ? 0.88 : (metricTrend(modelData) === "down" ? 0.42 : 0.58)))
                                                        height: parent.height
                                                        radius: parent.radius
                                                        color: String(modelData.format || "") === "text"
                                                            ? accentStrong
                                                            : trendDisplayColor(metricTrend(modelData))
                                                    }
                                                }

                                                Text {
                                                    width: 54
                                                    horizontalAlignment: Text.AlignRight
                                                    text: metricText(modelData)
                                                    font.pixelSize: 10
                                                    font.weight: Font.DemiBold
                                                    color: textPrimary
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: metricSpanWidth(3)
                                height: 174
                                radius: 16
                                color: panelBackground
                                border.width: 1
                                border.color: panelBorder

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 8

                                    Text {
                                        text: "核心曲线速览"
                                        font.pixelSize: 14
                                        font.weight: Font.Black
                                        color: textPrimary
                                    }

                                    Text {
                                        text: "左侧看风控后净值趋势，右侧只保留原始/成本后/风控后三条执行序列对照。"
                                        width: parent.width
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 11
                                        color: textSecondary
                                    }

                                    Row {
                                        width: parent.width
                                        spacing: 12

                                        Rectangle {
                                            width: (parent.width - 12) / 2
                                            height: 108
                                            radius: 12
                                            color: panelRaisedBackground

                                            Canvas {
                                                id: miniReturnCurveCanvas
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                antialiasing: true

                                                onPaint: {
                                                    var context = getContext("2d")
                                                    context.clearRect(0, 0, width, height)
                                                    var series = coreCurveSeries()
                                                    if (series.length === 0) {
                                                        context.fillStyle = "#94A3B8"
                                                        context.font = "11px sans-serif"
                                                        context.fillText("暂无净值曲线", 10, 18)
                                                        return
                                                    }

                                                    var minValue = Math.min(0.9, curveMinValue([series]))
                                                    var maxValue = Math.max(1.05, curveMaxValue([series]))
                                                    var leftPadding = 6
                                                    var rightPadding = 6
                                                    var topPadding = 10
                                                    var bottomPadding = 14

                                                    context.strokeStyle = Qt.rgba(panelBorder.r, panelBorder.g, panelBorder.b, 0.75)
                                                    context.lineWidth = 1
                                                    context.beginPath()
                                                    var baselineY = curvePointY(1.0, minValue, maxValue, height, topPadding, bottomPadding)
                                                    context.moveTo(leftPadding, baselineY)
                                                    context.lineTo(width - rightPadding, baselineY)
                                                    context.stroke()

                                                    context.strokeStyle = accentStrong
                                                    context.lineWidth = 2
                                                    context.beginPath()
                                                    for (var pointIndex = 0; pointIndex < series.length; pointIndex++) {
                                                        var x = curvePointX(pointIndex, series.length, width, leftPadding, rightPadding)
                                                        var y = curvePointY(series[pointIndex], minValue, maxValue, height, topPadding, bottomPadding)
                                                        if (pointIndex === 0) {
                                                            context.moveTo(x, y)
                                                        } else {
                                                            context.lineTo(x, y)
                                                        }
                                                    }
                                                    context.stroke()
                                                }

                                                onWidthChanged: requestPaint()
                                                onHeightChanged: requestPaint()
                                                Connections {
                                                    target: root
                                                    function onBacktestReportChanged() { miniReturnCurveCanvas.requestPaint() }
                                                    function onSelectedFactorIdChanged() { miniReturnCurveCanvas.requestPaint() }
                                                }
                                                Component.onCompleted: requestPaint()
                                            }

                                            Text {
                                                anchors.left: parent.left
                                                anchors.leftMargin: 10
                                                anchors.bottom: parent.bottom
                                                anchors.bottomMargin: 8
                                                text: "风控后净值"
                                                font.pixelSize: 10
                                                color: textSecondary
                                            }
                                        }

                                        Rectangle {
                                            width: (parent.width - 12) / 2
                                            height: 108
                                            radius: 12
                                            color: panelRaisedBackground

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 12
                                                spacing: 8

                                                Repeater {
                                                    model: [
                                                        { title: "原始研究序列", value: terminalNetValueText("rawReturns") },
                                                        { title: "成本后执行序列", value: terminalNetValueText("costAdjustedReturns") },
                                                        { title: "风控后执行序列", value: terminalNetValueText("riskAdjustedReturns") }
                                                    ]

                                                    delegate: Rectangle {
                                                        width: parent.width
                                                        height: 24
                                                        radius: 8
                                                        color: "#0F172A"

                                                        Row {
                                                            anchors.fill: parent
                                                            anchors.margins: 8
                                                            spacing: 8

                                                            Text {
                                                                width: 86
                                                                text: modelData.title
                                                                elide: Text.ElideRight
                                                                font.pixelSize: 10
                                                                color: textSecondary
                                                            }

                                                            Item { width: Math.max(0, parent.width - 160); height: 1 }

                                                            Text {
                                                                text: modelData.value
                                                                font.pixelSize: 11
                                                                font.weight: Font.DemiBold
                                                                color: textPrimary
                                                            }
                                                        }
                                                    }
                                                }

                                                Text {
                                                    width: parent.width
                                                    text: "这里只保留执行序列对照，分组梯度只在下方辅助图表中展示一次。"
                                                    wrapMode: Text.WordWrap
                                                    font.pixelSize: 10
                                                    color: textSecondary
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 10

                        Text {
                            text: String(sectionConfig("optionalSection").title || "")
                            font.pixelSize: 18
                            font.weight: Font.Black
                            color: textPrimary
                        }

                        Text {
                            text: String(sectionConfig("optionalSection").subtitle || "")
                            font.pixelSize: 12
                            color: textSecondary
                        }

                        Flow {
                            width: parent.width
                            spacing: cardGap

                            Repeater {
                                model: optionalMetricItems()

                                delegate: Rectangle {
                                    width: metricSpanWidth(modelData.units, modelData.tier)
                                    height: metricCardHeight(modelData.tier)
                                    radius: 16
                                    color: panelBackground
                                    border.width: 1
                                    border.color: panelBorder

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        spacing: 6

                                        Text {
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: modelData.title
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                            color: textSecondary
                                        }

                                        Row {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            spacing: 6

                                            Text {
                                                text: metricText(modelData)
                                                font.pixelSize: 20
                                                font.weight: Font.Black
                                                color: trendDisplayColor(metricTrend(modelData))
                                            }

                                            Text {
                                                visible: metricTrend(modelData) === "up" || metricTrend(modelData) === "down"
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: metricTrend(modelData) === "up" ? "↑" : "↓"
                                                font.pixelSize: 15
                                                color: trendDisplayColor(metricTrend(modelData))
                                            }
                                        }

                                        Text {
                                            text: modelData.subtitle
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 10
                                            color: textSecondary
                                        }

                                        Text {
                                            visible: String(modelData.thresholdText || "") !== ""
                                            text: String(modelData.thresholdText || "")
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 10
                                            color: accentSoft
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 12

                        Rectangle {
                            width: parent.width
                            radius: 16
                            color: panelBackground
                            border.width: 1
                            border.color: panelBorder
                            height: 62

                            Row {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 12

                                Column {
                                    width: parent.width - 88
                                    spacing: 4

                                    Text {
                                        text: String(sectionConfig("auxiliarySection").title || "")
                                        font.pixelSize: 18
                                        font.weight: Font.Black
                                        color: textPrimary
                                    }

                                    Text {
                                        text: auxiliarySectionSubtitle()
                                        font.pixelSize: 12
                                        color: textSecondary
                                    }
                                }

                                Rectangle {
                                    width: 48
                                    height: 34
                                    radius: 10
                                    color: panelRaisedBackground
                                    border.width: 1
                                    border.color: panelBorder

                                    Text {
                                        anchors.centerIn: parent
                                        text: showAuxiliaryMetrics ? "˄" : "˅"
                                        font.pixelSize: 18
                                        font.weight: Font.Black
                                        color: textPrimary
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: showAuxiliaryMetrics = !showAuxiliaryMetrics
                                    }
                                }
                            }
                        }

                        Column {
                            visible: showAuxiliaryMetrics
                            width: parent.width
                            spacing: 14

                            Flow {
                                width: Math.min(parent.width, metricRowWidth(auxiliaryMetricItems(), "auxiliary"))
                                x: centeredFlowX(width, parent.width)
                                spacing: cardGap

                                Repeater {
                                    model: auxiliaryMetricItems()

                                    delegate: Rectangle {
                                        width: metricSpanWidth(modelData.units, modelData.tier)
                                        height: metricCardHeight(modelData.tier)
                                        radius: 16
                                        color: panelBackground
                                        border.width: 1
                                        border.color: panelBorder

                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 14
                                            spacing: 6

                                            Text {
                                                width: parent.width
                                                horizontalAlignment: Text.AlignHCenter
                                                text: modelData.title
                                                font.pixelSize: 10
                                                font.weight: Font.DemiBold
                                                color: textSecondary
                                            }

                                            Row {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                spacing: 6

                                                Text {
                                                    text: metricText(modelData)
                                                    font.pixelSize: 18
                                                    font.weight: Font.Black
                                                    color: trendDisplayColor(metricTrend(modelData))
                                                }

                                                Text {
                                                    visible: metricTrend(modelData) === "up" || metricTrend(modelData) === "down"
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: metricTrend(modelData) === "up" ? "↑" : "↓"
                                                    font.pixelSize: 16
                                                    color: trendDisplayColor(metricTrend(modelData))
                                                }
                                            }

                                            Text {
                                                text: modelData.subtitle
                                                width: parent.width
                                                horizontalAlignment: Text.AlignHCenter
                                                wrapMode: Text.WordWrap
                                                font.pixelSize: 9
                                                color: textSecondary
                                            }

                                            Text {
                                                visible: String(modelData.thresholdText || "") !== ""
                                                text: String(modelData.thresholdText || "")
                                                width: parent.width
                                                horizontalAlignment: Text.AlignHCenter
                                                wrapMode: Text.WordWrap
                                                font.pixelSize: 10
                                                color: accentSoft
                                            }
                                        }
                                    }
                                }
                            }

                            Flow {
                                width: Math.min(parent.width, metricSpanWidth(6))
                                x: centeredFlowX(width, parent.width)
                                spacing: cardGap

                                Repeater {
                                    model: groupChartItems()

                                    delegate: Rectangle {
                                        id: groupChartCard
                                        property var chartData: modelData

                                        width: metricSpanWidth(3)
                                        height: 220
                                        radius: 16
                                        color: panelBackground
                                        border.width: 1
                                        border.color: panelBorder
                                        visible: chartData.series.length > 0

                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 14
                                            spacing: 10

                                            Text {
                                                text: chartData.title
                                                font.pixelSize: 14
                                                font.weight: Font.Black
                                                color: textPrimary
                                            }

                                            Text {
                                                text: chartData.subtitle
                                                width: parent.width
                                                wrapMode: Text.WordWrap
                                                font.pixelSize: 11
                                                color: textSecondary
                                            }

                                            Column {
                                                width: parent.width
                                                spacing: 8

                                                Repeater {
                                                    model: groupChartCard.chartData.series

                                                    delegate: Row {
                                                        width: parent.width
                                                        spacing: 10

                                                        Text {
                                                            width: 28
                                                            text: modelData.label
                                                            font.pixelSize: 11
                                                            color: textSecondary
                                                        }

                                                        Rectangle {
                                                            width: parent.width - 110
                                                            height: 18
                                                            radius: 9
                                                            color: panelRaisedBackground

                                                            Rectangle {
                                                                width: Math.max(4, parent.width * Math.abs(Number(modelData.value)) / maxAbsValue(groupChartCard.chartData.series))
                                                                height: parent.height
                                                                radius: parent.radius
                                                                color: Number(modelData.value) >= 0 ? riseColor : fallColor
                                                                anchors.verticalCenter: parent.verticalCenter
                                                            }
                                                        }

                                                        Text {
                                                            width: 62
                                                            horizontalAlignment: Text.AlignRight
                                                            text: groupChartCard.chartData.isPercent
                                                                ? percentText(modelData.value, 2)
                                                                : numberText(modelData.value, 2)
                                                            font.pixelSize: 11
                                                            color: textPrimary
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Column {
                                width: parent.width
                                spacing: 10

                                Text {
                                    text: "收益曲线"
                                    font.pixelSize: 18
                                    font.weight: Font.Black
                                    color: textPrimary
                                }

                                Text {
                                    text: "原始、多空成本后和风控后三条累计净值曲线。"
                                    font.pixelSize: 12
                                    color: textSecondary
                                }

                                Rectangle {
                                    width: parent.width
                                    height: 320
                                    radius: 16
                                    color: panelBackground
                                    border.width: 1
                                    border.color: panelBorder

                                    Canvas {
                                        id: returnCurveCanvas
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        antialiasing: true

                                        onPaint: {
                                            var context = getContext("2d")
                                            var canvasWidth = width
                                            var canvasHeight = height
                                            context.clearRect(0, 0, canvasWidth, canvasHeight)

                                            var rawCumulative = cumulativeSeries(returnSeriesValues("rawReturns"))
                                            var costAdjustedCumulative = cumulativeSeries(returnSeriesValues("costAdjustedReturns"))
                                            var riskAdjustedCumulative = cumulativeSeries(returnSeriesValues("riskAdjustedReturns"))
                                            var visibleSeries = []
                                            if (rawCumulative.length > 0) visibleSeries.push(rawCumulative)
                                            if (costAdjustedCumulative.length > 0) visibleSeries.push(costAdjustedCumulative)
                                            if (riskAdjustedCumulative.length > 0) visibleSeries.push(riskAdjustedCumulative)

                                            if (visibleSeries.length === 0) {
                                                context.fillStyle = "#94A3B8"
                                                context.font = "12px sans-serif"
                                                context.fillText("暂无收益曲线数据", 16, 24)
                                                return
                                            }

                                            var leftPadding = 54
                                            var rightPadding = 14
                                            var topPadding = 16
                                            var bottomPadding = 28
                                            var minValue = Math.min(0.8, curveMinValue(visibleSeries))
                                            var maxValue = Math.max(1.05, curveMaxValue(visibleSeries))
                                            var baselineY = curvePointY(1.0, minValue, maxValue, canvasHeight, topPadding, bottomPadding)

                                            context.strokeStyle = Qt.rgba(panelBorder.r, panelBorder.g, panelBorder.b, 0.65)
                                            context.lineWidth = 1
                                            context.beginPath()
                                            context.moveTo(leftPadding, baselineY)
                                            context.lineTo(canvasWidth - rightPadding, baselineY)
                                            context.stroke()

                                            var labels = ["原始", "成本后", "风控后"]
                                            var colors = [riseColor, accentStrong, positiveColor]
                                            var seriesList = [rawCumulative, costAdjustedCumulative, riskAdjustedCumulative]

                                            context.font = "11px sans-serif"
                                            context.fillStyle = textSecondary
                                            context.fillText("累计净值", 8, 16)

                                            for (var seriesIndex = 0; seriesIndex < seriesList.length; seriesIndex++) {
                                                var series = seriesList[seriesIndex]
                                                if (series.length === 0) {
                                                    continue
                                                }

                                                context.strokeStyle = colors[seriesIndex]
                                                context.fillStyle = colors[seriesIndex]
                                                context.lineWidth = 2
                                                context.beginPath()
                                                for (var pointIndex = 0; pointIndex < series.length; pointIndex++) {
                                                    var x = curvePointX(pointIndex, series.length, canvasWidth, leftPadding, rightPadding)
                                                    var y = curvePointY(series[pointIndex], minValue, maxValue, canvasHeight, topPadding, bottomPadding)
                                                    if (pointIndex === 0) {
                                                        context.moveTo(x, y)
                                                    } else {
                                                        context.lineTo(x, y)
                                                    }
                                                }
                                                context.stroke()

                                                for (var pointIndex2 = 0; pointIndex2 < series.length; pointIndex2++) {
                                                    var x2 = curvePointX(pointIndex2, series.length, canvasWidth, leftPadding, rightPadding)
                                                    var y2 = curvePointY(series[pointIndex2], minValue, maxValue, canvasHeight, topPadding, bottomPadding)
                                                    context.beginPath()
                                                    context.arc(x2, y2, 2.5, 0, Math.PI * 2)
                                                    context.fill()
                                                }
                                            }

                                            for (var legendIndex = 0; legendIndex < labels.length; legendIndex++) {
                                                var legendX = leftPadding + legendIndex * 110
                                                var legendY = canvasHeight - 10
                                                context.fillStyle = colors[legendIndex]
                                                context.fillRect(legendX, legendY - 8, 10, 10)
                                                context.fillStyle = textSecondary
                                                context.fillText(labels[legendIndex], legendX + 16, legendY)
                                            }
                                        }

                                        onWidthChanged: requestPaint()
                                        onHeightChanged: requestPaint()
                                        Connections {
                                            target: root
                                            function onBacktestReportChanged() {
                                                returnCurveCanvas.requestPaint()
                                            }
                                            function onSelectedFactorIdChanged() {
                                                returnCurveCanvas.requestPaint()
                                            }
                                        }
                                        Component.onCompleted: requestPaint()
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
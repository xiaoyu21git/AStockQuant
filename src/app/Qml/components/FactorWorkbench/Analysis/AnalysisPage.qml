// AnalysisPage.qml
// 因子分析页面 - 回测结果驱动版本
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: root

    signal requestWriteBacktestMetrics(var report)

    property Bridge.FactorService factorService: null
    property string selectedFactorId: ""
    property var backtestReport: ({})
    property int factorDefinitionRevision: 0
    property int selectedReportIndex: -1
    property bool suppressAutoAnalyze: false
    readonly property color positiveColor: "#EF4444"
    readonly property color negativeColor: "#10B981"
    readonly property color neutralColor: "#94A3B8"

    function hasBacktestReport() {
        return backtestReport && Object.keys(backtestReport).length > 0
    }

    function reportEntries() {
        if (!hasBacktestReport()) {
            return []
        }
        if (backtestReport.results && Array.isArray(backtestReport.results)) {
            return backtestReport.results
        }
        return [backtestReport]
    }

    function syncSelectedReportIndex() {
        var entries = reportEntries()
        if (entries.length === 0) {
            selectedReportIndex = -1
            return
        }

        var preferredFactorId = String(selectedFactorId || "")
        if (preferredFactorId.length > 0) {
            for (var i = 0; i < entries.length; i++) {
                var entryConfig = entries[i] && entries[i].config ? entries[i].config : ({})
                if (String(entryConfig.factorId || "") === preferredFactorId) {
                    selectedReportIndex = i
                    return
                }
            }
        }

        selectedReportIndex = 0
    }

    function activeReportEntry() {
        var entries = reportEntries()
        if (selectedReportIndex < 0 || selectedReportIndex >= entries.length) {
            return entries.length > 0 ? entries[0] : ({})
        }
        return entries[selectedReportIndex]
    }

    function activeConfig() {
        var entry = activeReportEntry()
        return entry && entry.config ? entry.config : ({})
    }

    function activeSummary() {
        var entry = activeReportEntry()
        return entry && entry.summary ? entry.summary : ({})
    }

    function activeIcir() {
        var entry = activeReportEntry()
        return entry && entry.icirResult ? entry.icirResult : ({})
    }

    function activeGroups() {
        var entry = activeReportEntry()
        return entry && entry.groups && Array.isArray(entry.groups) ? entry.groups : []
    }

    function activeFactorId() {
        var config = activeConfig()
        if (config.factorId !== undefined && config.factorId !== null && String(config.factorId).length > 0) {
            return String(config.factorId)
        }
        return ""
    }

    function hasMetricValue(value) {
        return value !== undefined && value !== null && value !== "" && !isNaN(Number(value))
    }

    function formatNumber(value, digits, fallbackText) {
        if (!hasMetricValue(value)) {
            return fallbackText
        }
        return Number(value).toFixed(digits)
    }

    function formatPercent(value, digits, fallbackText) {
        if (!hasMetricValue(value)) {
            return fallbackText
        }
        return (Number(value) * 100).toFixed(digits) + "%"
    }

    function formatSignedPercent(value, digits, fallbackText) {
        if (!hasMetricValue(value)) {
            return fallbackText
        }
        var numeric = Number(value) * 100
        return (numeric > 0 ? "+" : "") + numeric.toFixed(digits) + "%"
    }

    function metricTrend(value) {
        if (!hasMetricValue(value)) {
            return "neutral"
        }
        var numeric = Number(value)
        if (numeric > 0) {
            return "up"
        }
        if (numeric < 0) {
            return "down"
        }
        return "neutral"
    }

    function signedMetricColor(value) {
        if (!hasMetricValue(value)) {
            return neutralColor
        }
        var numeric = Number(value)
        if (numeric > 0) {
            return positiveColor
        }
        if (numeric < 0) {
            return negativeColor
        }
        return neutralColor
    }

    function trendAccentColor(trend) {
        if (trend === "up") {
            return positiveColor
        }
        if (trend === "down") {
            return negativeColor
        }
        return neutralColor
    }

    function getFactorName() {
        var config = activeConfig()
        if (config.factorName) {
            return String(config.factorName)
        }
        return activeFactorId() || "未命名结果"
    }

    function getICValue() {
        var icir = activeIcir()
        return formatNumber(icir.icValue, 4, "0.0000")
    }

    function getIRValue() {
        var icir = activeIcir()
        return formatNumber(icir.irValue, 4, "0.0000")
    }

    function getICPositiveRate() {
        return formatPercent(activeIcir().icPositiveRate, 1, "0.0%")
    }

    function getCoverageRate() {
        return formatPercent(activeSummary().dataCoverage, 1, "0.0%")
    }

    function getSpreadReturn() {
        return formatSignedPercent(activeSummary().spreadReturn, 2, "0.00%")
    }

    function getAnnualReturn() {
        return formatSignedPercent(activeSummary().longShortAnnualReturn, 2, "0.00%")
    }

    function getExecutionSharpeRatio() {
        return formatNumber(activeSummary().sharpeRatio, 3, "0.000")
    }

    function getMonotonicity() {
        var score = Number(activeSummary().monotonicity)
        if (!hasMetricValue(score)) {
            return "样本不足"
        }

        if (score >= 0.8) {
            return "强单调"
        }
        if (score >= 0.5) {
            return "较好"
        }
        if (score >= 0.2) {
            return "一般"
        }
        if (score > -0.2) {
            return "较弱"
        }
        if (score > -0.5) {
            return "明显分裂"
        }
        return "反向单调"
    }

    function getMonotonicityScore() {
        return formatNumber(activeSummary().monotonicity, 3, "0.000")
    }

    function getDiscrimination() {
        return formatNumber(activeSummary().discrimination, 4, "0.0000")
    }

    function getTopGroupReturn() {
        return formatSignedPercent(activeSummary().topGroupReturn, 2, "0.00%")
    }

    function getBottomGroupReturn() {
        return formatSignedPercent(activeSummary().bottomGroupReturn, 2, "0.00%")
    }

    function getMonotonicityColor() {
        var score = Number(activeSummary().monotonicity || 0)
        if (score >= 0.5) {
            return positiveColor
        }
        if (score <= -0.2) {
            return negativeColor
        }
        return neutralColor
    }

    function getValidityDays() {
        return hasMetricValue(activeSummary().validityDays) ? String(Math.round(Number(activeSummary().validityDays))) + "天" : "暂无"
    }

    function getStabilityText() {
        var icPositiveRate = Number(activeIcir().icPositiveRate || 0)
        var irValue = Number(activeIcir().irValue || 0)
        if (icPositiveRate >= 0.6 && irValue >= 0.3) {
            return "滚动表现较稳定"
        }
        if (icPositiveRate >= 0.5) {
            return "稳定性中等"
        }
        return "稳定性偏弱"
    }

    function getResultSelectorText(entry) {
        var config = entry && entry.config ? entry.config : ({})
        return String(config.factorName || config.factorId || "未命名结果")
    }

    function nullableNumber(value) {
        return hasMetricValue(value) ? Number(value) : null
    }

    function buildAshareGroupReportRows() {
        var groups = activeGroups()
        var rows = []
        for (var i = 0; i < groups.length; i++) {
            var group = groups[i] || ({})
            rows.push({
                groupId: group.groupId !== undefined ? Number(group.groupId) : (i + 1),
                groupName: String(group.groupName || ("第" + (i + 1) + "组")),
                stockCount: group.stockCount !== undefined ? Number(group.stockCount) : 0,
                minFactorValue: nullableNumber(group.minFactorValue),
                maxFactorValue: nullableNumber(group.maxFactorValue),
                return: nullableNumber(group.return),
                annualizedReturn: nullableNumber(group.annualizedReturn)
            })
        }
        return rows
    }

    function buildAshareAnalysisReport() {
        var config = activeConfig()
        var summary = activeSummary()
        var icir = activeIcir()

        return {
            template: "A_SHARE_FACTOR_RESEARCH",
            factorId: activeFactorId(),
            factorName: getFactorName(),
            period: {
                startDate: String(config.startDate || ""),
                endDate: String(config.endDate || ""),
                forwardDays: Number(config.forwardDays || 0),
                numGroups: Number(config.numGroups || 0),
                dataSourceMode: String(config.dataSourceMode || "")
            },
            summary: {
                dataCoverage: nullableNumber(summary.dataCoverage),
                topGroupReturn: nullableNumber(summary.topGroupReturn),
                bottomGroupReturn: nullableNumber(summary.bottomGroupReturn),
                spreadReturn: nullableNumber(summary.spreadReturn),
                longShortAnnualReturn: nullableNumber(summary.longShortAnnualReturn),
                monotonicity: nullableNumber(summary.monotonicity),
                discrimination: nullableNumber(summary.discrimination)
            },
            execution: {
                annualReturn: nullableNumber(summary.executionAnnualReturn),
                sharpeRatio: nullableNumber(summary.sharpeRatio),
                maxDrawdown: nullableNumber(summary.maxDrawdown),
                winRate: nullableNumber(summary.winRate),
                turnoverRate: nullableNumber(summary.turnoverRate)
            },
            icirResult: {
                icValue: nullableNumber(icir.icValue),
                irValue: nullableNumber(icir.irValue),
                icPositiveRate: nullableNumber(icir.icPositiveRate),
                icTStat: nullableNumber(icir.icTStat),
                icPValue: nullableNumber(icir.icPValue)
            },
            groups: buildAshareGroupReportRows(),
            notes: [
                "A股分层研究模板",
                "多空收益差与多空年化仅用于因子研究口径，不代表A股实盘可直接做空",
                "执行夏普等执行口径指标已移入 execution 区块，不再混入研究 summary",
                "组级风险指标仅在具备独立组收益序列时输出，当前报告不再伪造重复值"
            ]
        }
    }

    function qualificationRuleRows() {
        var summary = activeSummary()
        var icir = activeIcir()

        return [
            {
                label: "数据覆盖率",
                current: formatPercent(summary.dataCoverage, 1, "0.0%"),
                target: ">= 90.0%",
                passed: Number(summary.dataCoverage || 0) >= 0.9
            },
            {
                label: "|IC|",
                current: formatNumber(Math.abs(Number(icir.icValue || 0)), 4, "0.0000"),
                target: ">= 0.0200",
                passed: Math.abs(Number(icir.icValue || 0)) >= 0.02
            },
            {
                label: "IR",
                current: formatNumber(icir.irValue, 4, "0.0000"),
                target: ">= 0.3000",
                passed: Number(icir.irValue || 0) >= 0.3
            },
            {
                label: "IC正率",
                current: formatPercent(icir.icPositiveRate, 1, "0.0%"),
                target: ">= 50.0%",
                passed: Number(icir.icPositiveRate || 0) >= 0.5
            },
            {
                label: "多空收益差",
                current: formatSignedPercent(summary.spreadReturn, 2, "0.00%"),
                target: "> 0.00%",
                passed: Number(summary.spreadReturn || 0) > 0
            }
        ]
    }

    function passedRuleCount() {
        var rules = qualificationRuleRows()
        var count = 0
        for (var i = 0; i < rules.length; i++) {
            if (rules[i].passed) {
                count += 1
            }
        }
        return count
    }

    function meetsQualificationStandard() {
        var rules = qualificationRuleRows()
        if (rules.length === 0) {
            return false
        }
        for (var i = 0; i < rules.length; i++) {
            if (!rules[i].passed) {
                return false
            }
        }
        return true
    }

    function qualificationStatusText() {
        if (!hasBacktestReport()) {
            return "尚无回测结果，无法判断是否合格"
        }
        return meetsQualificationStandard()
            ? "当前结果满足回测合格标准"
            : ("当前结果未达标，已满足 " + passedRuleCount() + "/" + qualificationRuleRows().length + " 项")
    }

    function qualificationStatusColor() {
        if (!hasBacktestReport()) {
            return neutralColor
        }
        return meetsQualificationStandard() ? positiveColor : "#F59E0B"
    }

    function requestCurrentReportWrite() {
        if (!hasBacktestReport()) {
            return
        }
        requestWriteBacktestMetrics(activeReportEntry())
    }

    onBacktestReportChanged: {
        if (hasBacktestReport()) {
            suppressAutoAnalyze = false
        }
        syncSelectedReportIndex()
    }
    onSelectedFactorIdChanged: syncSelectedReportIndex()
    onFactorDefinitionRevisionChanged: syncSelectedReportIndex()

    Rectangle {
        anchors.fill: parent
        color: "#0F172A"

        Flickable {
            id: scrollView
            anchors.fill: parent
            anchors.margins: 16
            clip: true
            contentWidth: contentColumn.width
            contentHeight: contentColumn.height
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 8
            }

            ColumnLayout {
                id: contentColumn
                width: scrollView.width
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "📊 因子分析工作区"
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }

                        Text {
                            text: hasBacktestReport()
                                ? ("当前报告: " + getFactorName())
                                : (selectedFactorId ? ("当前因子: " + getFactorName()) : "请从因子库选择因子，或先完成一次回测")
                            font.pixelSize: 12
                            color: hasBacktestReport() ? "#38BDF8" : "#94A3B8"
                        }
                    }

                    ComboBox {
                        id: resultSelector
                        Layout.preferredWidth: 220
                        visible: reportEntries().length > 1
                        model: reportEntries()
                        currentIndex: selectedReportIndex

                        delegate: ItemDelegate {
                            width: resultSelector.width
                            text: getResultSelectorText(modelData)
                        }

                        contentItem: Text {
                            text: resultSelector.currentIndex >= 0 && resultSelector.currentIndex < reportEntries().length
                                ? getResultSelectorText(reportEntries()[resultSelector.currentIndex])
                                : "选择回测结果"
                            color: "#F1F5F9"
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            font.pixelSize: 12
                        }

                        background: Rectangle {
                            radius: 8
                            color: "#1E293B"
                            border.color: "#334155"
                        }

                        onActivated: function(index) {
                            selectedReportIndex = index
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 12
                    color: "#1E293B"
                    border.width: 1
                    border.color: qualificationStatusColor()
                    implicitHeight: 118

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "📏 回测合格标准"
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: qualificationStatusText()
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: qualificationStatusColor()
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: qualificationRuleRows()

                                delegate: Rectangle {
                                    radius: 8
                                    color: modelData.passed ? "#0F3A2A" : "#3A2A0F"
                                    border.width: 1
                                    border.color: modelData.passed ? "#10B981" : "#F59E0B"
                                    height: 34
                                    width: Math.max(170, ruleRow.implicitWidth + 18)

                                    Row {
                                        id: ruleRow
                                        anchors.centerIn: parent
                                        spacing: 6

                                        Text {
                                            text: modelData.passed ? "✓" : "!"
                                            font.pixelSize: 12
                                            color: modelData.passed ? "#10B981" : "#F59E0B"
                                        }

                                        Text {
                                            text: modelData.label + ": " + modelData.current + " / " + modelData.target
                                            font.pixelSize: 11
                                            color: "#E2E8F0"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width < 900 ? 1 : 2
                    columnSpacing: 12
                    rowSpacing: 12

                    AnalysisCard {
                        title: "IC分析"
                        value: getICValue()
                        trend: metricTrend(activeIcir().icValue)
                        description: "信息系数"
                        valueColor: "#3B82F6"
                    }

                    AnalysisCard {
                        title: "IR分析"
                        value: getIRValue()
                        trend: metricTrend(activeIcir().irValue)
                        description: "信息比率"
                        valueColor: "#10B981"
                    }

                    AnalysisCard {
                        title: "IC正率"
                        value: getICPositiveRate()
                        trend: metricTrend(activeIcir().icPositiveRate)
                        description: "IC大于0的占比"
                        valueColor: "#06B6D4"
                    }

                    AnalysisCard {
                        title: "数据覆盖率"
                        value: getCoverageRate()
                        trend: metricTrend(activeSummary().dataCoverage)
                        description: "有效样本覆盖水平"
                        valueColor: "#F59E0B"
                    }

                    AnalysisCard {
                        title: "多空收益差"
                        value: getSpreadReturn()
                        trend: metricTrend(activeSummary().spreadReturn)
                        description: "A股分层研究口径"
                        valueColor: signedMetricColor(activeSummary().spreadReturn)
                    }

                    AnalysisCard {
                        title: "多空年化"
                        value: getAnnualReturn()
                        trend: metricTrend(activeSummary().longShortAnnualReturn)
                        description: "顶底分组多空年化"
                        valueColor: signedMetricColor(activeSummary().longShortAnnualReturn)
                    }

                    AnalysisCard {
                        title: "单调得分"
                        value: getMonotonicityScore()
                        trend: metricTrend(activeSummary().monotonicity)
                        description: "分组收益相关系数"
                        valueColor: getMonotonicityColor()
                    }

                    AnalysisCard {
                        title: "区分度"
                        value: getDiscrimination()
                        trend: metricTrend(activeSummary().discrimination)
                        description: "组收益离散程度"
                        valueColor: "#F59E0B"
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width < 900 ? 1 : 2
                    columnSpacing: 12
                    rowSpacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 132
                        radius: 12
                        color: "#1E293B"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12

                            Text {
                                text: "📊 分组表现"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }

                            Text {
                                text: "单调性: " + getMonotonicity()
                                font.pixelSize: 12
                                color: getMonotonicityColor()
                            }

                            Text {
                                text: "单调得分: " + getMonotonicityScore()
                                font.pixelSize: 12
                                color: "#CBD5E1"
                            }

                            Text {
                                text: "有效期: " + getValidityDays()
                                font.pixelSize: 12
                                color: "#CBD5E1"
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 132
                        radius: 12
                        color: "#1E293B"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12

                            Text {
                                text: "📈 稳定性与换手"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }

                            Text {
                                text: getStabilityText()
                                font.pixelSize: 12
                                color: "#10B981"
                            }

                            Text {
                                text: "执行夏普: " + getExecutionSharpeRatio()
                                font.pixelSize: 12
                                color: "#CBD5E1"
                            }

                            Text {
                                text: "顶组收益: " + getTopGroupReturn()
                                font.pixelSize: 12
                                color: "#F59E0B"
                            }

                            Text {
                                text: "底组收益: " + getBottomGroupReturn()
                                font.pixelSize: 12
                                color: "#CBD5E1"
                            }

                            Text {
                                text: hasBacktestReport() ? "报告来源: A股分层研究结果" : "报告来源: 因子库静态信息"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 112
                        Layout.preferredHeight: 36
                        radius: 8
                        color: "#3B82F6"

                        Row {
                            anchors.centerIn: parent
                            spacing: 8

                            Text {
                                text: "📤"
                                font.pixelSize: 13
                                color: "white"
                            }

                            Text {
                                text: "导出报告"
                                font.pixelSize: 13
                                color: "white"
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: exportReport()
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 112
                        Layout.preferredHeight: 36
                        radius: 8
                        color: "#334155"

                        Row {
                            anchors.centerIn: parent
                            spacing: 8

                            Text {
                                text: "🔍"
                                font.pixelSize: 13
                                color: "#F1F5F9"
                            }

                            Text {
                                text: "详细分析"
                                font.pixelSize: 13
                                color: "#F1F5F9"
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: showDetailedAnalysis()
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 112
                        Layout.preferredHeight: 36
                        radius: 8
                        color: hasBacktestReport() ? "#059669" : "#475569"

                        Row {
                            anchors.centerIn: parent
                            spacing: 8

                            Text {
                                text: "📝"
                                font.pixelSize: 13
                                color: "#F1F5F9"
                            }

                            Text {
                                text: "写入指标"
                                font.pixelSize: 13
                                color: "#F1F5F9"
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: hasBacktestReport() ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                            enabled: hasBacktestReport()
                            onClicked: requestCurrentReportWrite()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: hasBacktestReport() ? "回测完成后可手动写入指标，且不要求全部合格" : "请从因子库选择因子进行分析"
                        font.pixelSize: 13
                        color: hasBacktestReport() ? "#3B82F6" : "#94A3B8"
                    }
                }
            }
        }
    }

    component AnalysisCard: Item {
        property string title: ""
        property string value: ""
        property string trend: "neutral"
        property string description: ""
        property color valueColor: "#3B82F6"

        Layout.fillWidth: true
        Layout.preferredHeight: 104

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "#1E293B"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                Text {
                    text: title
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }

                Row {
                    spacing: 8

                    Text {
                        text: value
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                        color: valueColor
                    }

                    Text {
                        visible: trend !== "neutral"
                        text: trend === "up" ? "↑" : "↓"
                        font.pixelSize: 16
                        color: trendAccentColor(trend)
                    }
                }

                Text {
                    text: description
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
            }
        }
    }

    function exportReport() {
        console.log("导出分析报告", JSON.stringify(buildAshareAnalysisReport()))
    }

    function showDetailedAnalysis() {
        console.log("显示详细分析", JSON.stringify(buildAshareAnalysisReport()))
    }
}
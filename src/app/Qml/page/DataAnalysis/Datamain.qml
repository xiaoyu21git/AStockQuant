// Datamain.qml - 数据管理主页面（重构版：消除重叠规则，使用真实数据）
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0
import AStock.Bridge 1.0
import "../../components/DataAnalysis" as DataAnalysisComponents

Item {
    id: root
    anchors.fill: parent

    property int dataSourceCount: 0
    property int currentCacheIndex: -1
    property var selectedRules: []
    property int selectedRulesRevision: 0
    property int selectedRulesCount: selectedRules ? selectedRules.length : 0
    property var selectedRuleEntries: []
    property int cleaningRulePageSize: 15
    property int cleaningRuleCurrentPage: 1
    property var cleaningRulePageEntries: []
    readonly property int cleaningRuleTotalPages: Math.max(1, Math.ceil(availableCleaningRules.length / Math.max(cleaningRulePageSize, 1)))
    property var availableCleaningRules: [
        { ruleId: "completeness", ruleName: "完整性校验", icon: "✅", cardColor: "#10b981", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "duplicateRemoval", ruleName: "重复数据删除", icon: "🗑️", cardColor: "#f97316", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "financialDateValidity", ruleName: "财务日期有效性", icon: "🗓️", cardColor: "#06b6d4", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "financialMetricSanitize", ruleName: "财务指标净化", icon: "📈", cardColor: "#14b8a6", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "reportDateAlignment", ruleName: "财报日期对齐", icon: "📅", cardColor: "#22c55e", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "survivorBias", ruleName: "生存者偏差处理", icon: "🧬", cardColor: "#14b8a6", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "adjustedPrice", ruleName: "价格复权", icon: "🔁", cardColor: "#8b5cf6", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "newStockFilter", ruleName: "新股过滤", icon: "🆕", cardColor: "#0ea5e9", defaultValue: false, ruleLevel: "可选" },
        { ruleId: "stFilter", ruleName: "ST过滤", icon: "⚠️", cardColor: "#ef4444", defaultValue: false, ruleLevel: "可选" },
        { ruleId: "priceValidity", ruleName: "价格有效性", icon: "📊", cardColor: "#8b5cf6", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "suspensionFill", ruleName: "停牌填充", icon: "⏸️", cardColor: "#6366f1", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "missingValueFill", ruleName: "缺失值处理", icon: "🔍", cardColor: "#ec4899", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "limitMoveTag", ruleName: "涨跌停标记", icon: "🏷️", cardColor: "#f59e0b", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "valuationSanitize", ruleName: "估值净化", icon: "🧮", cardColor: "#06b6d4", defaultValue: true, ruleLevel: "推荐" }
    ]
    property var reportStatus: function(message, type) {
        root.handlePanelStatusRequested(message, type)
    }
    signal sourceAdded(var sourceInfo)
    signal dataLoaded()

    function buildSelectedRuleEntries(ruleIds) {
        var entries = []
        for (var i = 0; i < availableCleaningRules.length; i++) {
            if (ruleIds && ruleIds.indexOf(availableCleaningRules[i].ruleId) !== -1) {
                entries.push(availableCleaningRules[i])
            }
        }
        return entries
    }

    function refreshCleaningRulePage() {
        var totalPages = cleaningRuleTotalPages
        if (cleaningRuleCurrentPage > totalPages) {
            cleaningRuleCurrentPage = totalPages
            return
        }
        if (cleaningRuleCurrentPage < 1) {
            cleaningRuleCurrentPage = 1
            return
        }

        var startIndex = (cleaningRuleCurrentPage - 1) * cleaningRulePageSize
        var endIndex = Math.min(startIndex + cleaningRulePageSize, availableCleaningRules.length)
        var pageEntries = []
        for (var i = startIndex; i < endIndex; i++) {
            pageEntries.push(availableCleaningRules[i])
        }
        cleaningRulePageEntries = pageEntries
    }

    function syncPreviewTabSelection() {
        if (!dataFetchController.previewModel || !previewPanel) {
            return
        }
        previewPanel.syncSelection()
    }

    function ensurePreviewVisible() {
        if (!flickable || !previewPanel) {
            return
        }

        Qt.callLater(function() {
            if (flickable && previewPanel) {
                flickable.contentY = Math.max(0, previewPanel.y - 24)
            }
        })
    }

    onSelectedRulesChanged: {
        selectedRuleEntries = buildSelectedRuleEntries(selectedRules)
    }

    onCleaningRuleCurrentPageChanged: {
        refreshCleaningRulePage()
    }

    Component.onCompleted: {
        selectedRuleEntries = buildSelectedRuleEntries(selectedRules)
        refreshCleaningRulePage()
        syncPreviewTabSelection()
        Qt.callLater(function() {
            if (dataFetchController) {
                dataFetchController.refreshDataSetInfos()
            }
        })
    }

    function resolvePanelDateValue(datePicker) {
        if (datePicker && typeof datePicker !== "undefined") {
            if (datePicker.selectedDate && datePicker.selectedDate !== "undefined") {
                return datePicker.selectedDate
            }
            if (datePicker.date && datePicker.date !== "undefined") {
                return datePicker.date
            }
            if (datePicker.text && datePicker.text !== "" && datePicker.text !== "YYYY-MM-DD") {
                return datePicker.text
            }
        }

        var today = new Date()
        return Qt.formatDate(today, "yyyy-MM-dd")
    }

    function buildCleaningRuleConfig(ruleId) {
        switch (ruleId) {
            case "completeness":
                return { "enabled": true }
            case "duplicateRemoval":
                return {
                    "enabled": true,
                    "keyFields": ["symbol", "trade_date"]
                }
            case "financialDateValidity":
                return { "enabled": true }
            case "financialMetricSanitize":
                return { "enabled": true }
            case "reportDateAlignment":
                return { "enabled": true }
            case "survivorBias":
                return { "enabled": true }
            case "suspensionFill":
                return {
                    "enabled": true,
                    "fillFields": ["open", "high", "low", "close"],
                    "maxForwardFillDays": 10,
                    "dropAfterMaxDays": true
                }
            case "missingValueFill":
                return {
                    "enabled": true,
                    "fields": ["open", "high", "low", "close", "turnover_rate", "market_cap", "circulating_market_cap"],
                    "maxLookbackDays": 5
                }
            case "adjustedPrice":
                return {
                    "enabled": true,
                    "preferAdjustedFields": true,
                    "applyFactorFallback": true
                }
            case "newStockFilter":
                return {
                    "enabled": true,
                    "minTradeDays": 60
                }
            case "stFilter":
                return { "enabled": true }
            case "priceValidity":
                return {
                    "enabled": true,
                    "minPrice": 0.01,
                    "maxPrice": 10000.0,
                    "enforceChain": true,
                    "allowZeroWhenSuspended": true
                }
            case "limitMoveTag":
                return {
                    "enabled": true,
                    "upThreshold": 9.5,
                    "downThreshold": -9.5
                }
            case "valuationSanitize":
                return { "enabled": true }
            default:
                return null
        }
    }

    function buildPanelCleaningRules(startDateValue, endDateValue) {
        void startDateValue
        void endDateValue
        var rules = {}

        for (var i = 0; i < selectedRules.length; i++) {
            var ruleId = selectedRules[i]
            var ruleConfig = buildCleaningRuleConfig(ruleId)
            if (ruleConfig) {
                rules[ruleId] = ruleConfig
            } else {
                console.log("未知规则ID:", ruleId)
            }
        }

        return rules
    }

    function handlePanelQueryRequested() {
        var provider = dataSelectionPanel.providerComboBox.currentText
        var selectedDataTypes = dataSelectionPanel.dataTypeCardsFlow.selectedDataTypes.slice()
        var startDate = resolvePanelDateValue(dataSelectionPanel.startDatePicker)
        var endDate = resolvePanelDateValue(dataSelectionPanel.endDatePicker)
        var selectedIndex = dataSelectionPanel.indexComboBox.currentIndex

        if (!provider) {
            handlePanelStatusRequested("请填写完整配置信息", "error")
            return
        }

        if (selectedDataTypes.length === 0) {
            selectedDataTypes = ["kline_daily", "financial"]
        }

        if (!startDate || !endDate) {
            handlePanelStatusRequested("请设置时间范围", "warning")
            return
        }

        // "全市场"（索引 0）走 all_market 数据源，查询全市场所有股票
        if (selectedIndex === 0) {
            handlePanelStatusRequested("⏳ 正在查询全市场数据...", "warning")
            dataFetchController.fetchDataTypesBySource("all_market", "", selectedDataTypes, startDate, endDate, {})
            return
        }

        // 其他指数（索引 1+）走 index 数据源，查询指数成分股
        if (selectedIndex > 0) {
            var indexSymbol = dataSelectionPanel.indexListModel.get(selectedIndex).symbol
            var displayName = dataSelectionPanel.indexListModel.get(selectedIndex).displayName
            handlePanelStatusRequested("⏳ 正在加载 " + displayName + " 的数据...", "warning")

            dataFetchController.fetchDataTypesBySource("index", indexSymbol, selectedDataTypes, startDate, endDate, {})
            return
        }

        // selectedIndex < 0（无选择）→ all_market fallback
        handlePanelStatusRequested("⏳ 正在查询全市场数据...", "warning")
        dataFetchController.fetchDataTypesBySource("all_market", "", selectedDataTypes, startDate, endDate, {
            market: dataSelectionPanel.marketComboBox.currentText,
            provider: provider
        })
    }

    function handlePanelProviderChosen(provider) {
        root.handlePanelStatusRequested("数据源已选择: " + provider, "info")
    }

    function handlePanelStatusRequested(message, type) {
        var normalizedMessage = ""
        if (message !== undefined && message !== null) {
            normalizedMessage = String(message)
        }

        if (typeof statusText === "undefined" || !statusText) {
            console.warn("statusText 未初始化，状态消息:", normalizedMessage)
            return
        }

        statusText.text = normalizedMessage
        clearStatusTimer.restart()
    }

    function handleExecuteDataCleaningFromCache() {
        console.log("[QML] handleExecuteDataCleaningFromCache: currentCacheIndex=" + root.currentCacheIndex + " cacheDisplayModel.count=" + cacheDisplayModel.count)
        handlePanelStatusRequested("⏳ 执行缓存数据清洗...", "warning")

        if (!dataFetchController) {
            handlePanelStatusRequested("❌ DataFetchController未初始化", "error")
            return
        }

        if (root.currentCacheIndex < 0 || root.currentCacheIndex >= cacheDisplayModel.count) {
            handlePanelStatusRequested("❌ 请先选择缓存数据", "error")
            return
        }

        var rules = buildPanelCleaningRules(
            resolvePanelDateValue(dataSelectionPanel.startDatePicker),
            resolvePanelDateValue(dataSelectionPanel.endDatePicker)
        )

        var entry = cacheDisplayModel.get(root.currentCacheIndex)
        var dataSetId = (entry && entry.id > 0) ? entry.id : -1
        if (dataSetId <= 0) {
            handlePanelStatusRequested("请先查询数据生成数据集", "error")
            return
        }
        console.log("[QML] cleanDataFromDataSet dataSetId=" + dataSetId)
        dataFetchController.cleanDataFromDataSet(dataSetId, rules)
        handlePanelStatusRequested("⏳ 正在清洗缓存数据...", "warning")
    }
    
    // 背景
    Rectangle {
        anchors.fill: parent
        color: "#0a0f1a"

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 40
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 20
            spacing: 20

            Text {
                text: "数据管理看板"
                font.pixelSize: 28
                font.bold: true
                color: "white"
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#1a1f2e"
                radius: 8

                Flickable {
                    id: flickable
                    anchors.fill: parent
                    anchors.margins: 20
                    contentWidth: contentColumn.width
                    contentHeight: contentColumn.implicitHeight
                    clip: true

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AlwaysOff
                    }

                    ScrollBar.horizontal: ScrollBar {
                        policy: ScrollBar.AlwaysOff
                    }

                    Column {
                        id: contentColumn
                        width: flickable.width
                        spacing: 15

                        Text {
                            text: "数据管理功能"
                            font.pixelSize: 20
                            font.bold: true
                            color: "white"
                            width: parent.width
                        }

                        Text {
                            text: "用于配置数据源、选择时间区间、查询指数成分与市场数据，并对结果执行清洗。"
                            font.pixelSize: 14
                            color: "#a0aec0"
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        DataAnalysisComponents.DataSelectionPanel {
                            id: dataSelectionPanel
                            width: parent.width
                            dataSourceCount: root.dataSourceCount
                            onQueryRequested: function() {
                                root.handlePanelQueryRequested()
                            }

                            onProviderChosen: function(provider) {
                                root.handlePanelProviderChosen(provider)
                            }

                            onStatusRequested: function(message, type) {
                                root.handlePanelStatusRequested(message, type)
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: progressPanelContent.implicitHeight + 28
                            radius: 10
                            color: "#111827"
                            border.width: 1
                            border.color: dataFetchController.operationInProgress ? "#38bdf8" : "#374151"
                            visible: true

                            Column {
                                id: progressPanelContent
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10

                                Row {
                                    width: parent.width
                                    spacing: 8

                                    Text {
                                        id: progressTitleText
                                        text: dataFetchController.operationPhase !== "" ? dataFetchController.operationPhase : "任务进度"
                                        font.pixelSize: 15
                                        font.bold: true
                                        color: "white"
                                    }

                                    Item {
                                        width: Math.max(0, parent.width - progressTitleText.width - progressPercentText.width - 12)
                                        height: 1
                                    }

                                    Text {
                                        id: progressPercentText
                                        text: dataFetchController.progress + "%"
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: dataFetchController.operationInProgress ? "#67e8f9" : "#cbd5e1"
                                    }
                                }

                                Rectangle {
                                    width: parent.width
                                    height: 12
                                    radius: 6
                                    color: "#1f2937"

                                    Rectangle {
                                        width: Math.max(0, parent.width * Math.max(0, Math.min(100, dataFetchController.progress)) / 100.0)
                                        height: parent.height
                                        radius: 6
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: "#0ea5e9" }
                                            GradientStop { position: 1.0; color: "#22c55e" }
                                        }
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: dataFetchController.statusMessage !== "" ? dataFetchController.statusMessage : "等待任务开始"
                                    font.pixelSize: 13
                                    color: "#cbd5e1"
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    width: parent.width
                                    visible: dataFetchController.currentProgressStock !== ""
                                    text: "当前股票: " + dataFetchController.currentProgressStock
                                    font.pixelSize: 13
                                    color: "#fbbf24"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                // 数据清洗区域 - 完整功能，与DataSourceModal对齐
                Column {
                    width: parent.width
                    spacing: 10
                    
                    // 标题行
                    Row {
                        width: parent.width
                        spacing: 10
                        
                        Text {
                            text: "🧹 数据清洗"
                            font.pixelSize: 16
                            font.bold: true
                            color: "white"
                        }
                        
                        Item {
                            width: parent.width - childrenRect.width - 20
                            height: 1
                        }
                        
                        Text {
                            text: selectedRulesCount > 0 ? "✅ " + selectedRulesCount + "项规则" : "✅ 就绪"
                            font.pixelSize: 12
                            color: selectedRulesCount > 0 ? "#10b981" : "#a0aec0"
                        }
                    }
                    
                    // 规则配置区域标题 - 与DataSourceModal对齐
                    Row {
                        width: parent.width
                        spacing: 8
                        
                        Text {
                            text: "数据处理规则配置"
                            font.pixelSize: 13
                            font.bold: true
                            color: "#a0aec0"
                        }
                        
                        Item {
                            width: parent.width - childrenRect.width - 20
                            height: 1
                        }
                        
                        Text {
                            text: "已启用 " + selectedRulesCount + " 项规则"
                            font.pixelSize: 11
                            color: selectedRulesCount > 0 ? "#10b981" : "#a0aec0"
                        }
                    }
                    
                    Row {
                        width: parent.width
                        spacing: 8
                        visible: selectedRulesCount > 0

                        Text {
                            text: "当前清洗列表"
                            font.pixelSize: 12
                            font.bold: true
                            color: "#e5e7eb"
                        }

                        Text {
                            text: "包含必选、推荐和可选规则"
                            font.pixelSize: 11
                            color: "#94a3b8"
                        }
                    }

                    // 已启用规则标签上移，优先展示当前清洗配置
                    Flow {
                        id: selectedRulesFlow
                        width: parent.width
                        spacing: 6
                        visible: selectedRulesCount > 0

                        Repeater {
                            model: root.selectedRuleEntries

                            Rectangle {
                                height: 24
                                radius: 12
                                color: Qt.lighter(modelData.cardColor, 1.4)
                                implicitWidth: ruleTagRow.implicitWidth + 12

                                Row {
                                    id: ruleTagRow
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    spacing: 4

                                    Text {
                                        text: String(modelData.icon || "")
                                        font.pixelSize: 10
                                        color: "white"
                                    }

                                    Text {
                                        text: String(modelData.ruleName || "")
                                        font.pixelSize: 11
                                        color: "white"
                                    }

                                    MouseArea {
                                        visible: String(modelData.ruleLevel || "") !== "必选"
                                        width: 12
                                        height: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        preventStealing: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            var nextRules = root.selectedRules ? root.selectedRules.slice() : []
                                            nextRules = nextRules.filter(function(ruleId) {
                                                return ruleId !== modelData.ruleId
                                            })
                                            root.selectedRules = nextRules
                                            root.selectedRulesRevision = root.selectedRulesRevision + 1
                                        }

                                        Text {
                                            text: "×"
                                            color: "#e5e7eb"
                                            font.pixelSize: 10
                                            font.bold: true
                                            anchors.centerIn: parent
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 规则卡片区域 - 分页展示 C++ 已注册规则
                    Rectangle {
                        width: parent.width
                        height: rulesCardsFlow.childrenRect.height
                        color: "transparent"

                        Flow {
                            id: rulesCardsFlow
                            width: parent.width
                            height: childrenRect.height
                            spacing: 8

                            Repeater {
                                model: root.cleaningRulePageEntries

                                delegate: RuleConfigCard {
                                    ruleId: modelData.ruleId
                                    ruleName: modelData.ruleName
                                    icon: modelData.icon
                                    cardColor: modelData.cardColor
                                    defaultValue: modelData.defaultValue
                                    ruleLevel: modelData.ruleLevel
                                    parentPage: root
                                }
                            }
                        }
                    }

                    DataAnalysisComponents.RulePaginationBar {
                        width: parent.width
                        currentPage: root.cleaningRuleCurrentPage
                        totalPages: root.cleaningRuleTotalPages
                        totalCount: root.availableCleaningRules.length

                        onPageChanged: {
                            root.cleaningRuleCurrentPage = page
                        }
                    }
                    
                    // 清洗统计卡片
                    Rectangle {
                        width: parent.width
                        height: 120
                        color: "#2d3748"
                        radius: 6
                        
                        Column {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 8
                            
                            Text {
                                text: "清洗统计"
                                font.pixelSize: 14
                                font.bold: true
                                color: "white"
                                width: parent.width
                            }
                            
                            // 统计卡片
                            Row {
                                width: parent.width
                                spacing: 10
                                
                                // 原始数据
                                Rectangle {
                                    width: (parent.width - 20) / 3
                                    height: 70
                                    color: "#374151"
                                    radius: 6
                                    
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 2
                                        
                                        Text {
                                            text: "原始记录"
                                            font.pixelSize: 10
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: dataFetchController.cleanInputRecordCount.toLocaleString()
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: "#3498db"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: "条记录"
                                            font.pixelSize: 9
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }
                                
                                // 移除数据
                                Rectangle {
                                    width: (parent.width - 20) / 3
                                    height: 70
                                    color: "#374151"
                                    radius: 6
                                    
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 2
                                        
                                        Text {
                                            text: "移除记录"
                                            font.pixelSize: 10
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: dataFetchController.cleanRemovedRecordCount.toLocaleString()
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: "#e74c3c"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: "条记录"
                                            font.pixelSize: 9
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }
                                
                                // 剩余数据
                                Rectangle {
                                    width: (parent.width - 20) / 3
                                    height: 70
                                    color: "#374151"
                                    radius: 6
                                    
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 2
                                        
                                        Text {
                                            text: "剩余记录"
                                            font.pixelSize: 10
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: dataFetchController.cleanOutputRecordCount.toLocaleString()
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: "#2ecc71"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: "条记录"
                                            font.pixelSize: 9
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    DataAnalysisComponents.DataPreviewPanel {
                        id: previewPanel
                        width: parent.width
                        previewModel: dataFetchController.previewModel
                        selectedDataTypes: dataSelectionPanel.dataTypeCardsFlow.selectedDataTypes
                    }
                    
                    Column {
                        width: parent.width
                        spacing: 8
                        
                        RowLayout {
                            width: parent.width
                            spacing: 10
                            
                            Text {
                                text: "缓存选择:"
                                font.pixelSize: 13
                                font.bold: true
                                color: "#a0aec0"
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "共 " + cacheDisplayModel.count + " 条缓存"
                                font.pixelSize: 12
                                color: "#3b82f6"
                            }
                        }

                        ComboBox {
                            id: cacheSelectionComboBox
                            width: parent.width
                            height: 36
                            model: cacheDisplayModel
                            textRole: "displayName"

                            background: Rectangle {
                                radius: 4
                                border.width: 1
                                border.color: cacheSelectionComboBox.hovered ? "#3b82f6" : "#4b5563"
                                color: "#374151"
                            }

                            contentItem: Text {
                                text: cacheSelectionComboBox.currentText || "请选择缓存数据..."
                                color: "white"
                                font.pixelSize: 13
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            popup: Popup {
                                y: cacheSelectionComboBox.height + 2
                                width: Math.min(cacheSelectionComboBox.width * 1.2, 350)
                                height: Math.min(contentItem.implicitHeight, 250)
                                padding: 1

                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: cacheSelectionComboBox.model
                                    delegate: ItemDelegate {
                                        width: parent.width
                                        height: 36
                                        text: displayName
                                        highlighted: cacheSelectionComboBox.highlightedIndex === index
                                        background: Rectangle {
                                            color: highlighted ? "#374151" : "#2d3748"
                                        }
                                        contentItem: Text {
                                            text: model.displayName
                                            color: "white"
                                            font.pixelSize: 12
                                            leftPadding: 8
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                        onClicked: {
                                            cacheSelectionComboBox.currentIndex = index
                                            cacheSelectionComboBox.popup.close()
                                            root.currentCacheIndex = index
                                        }
                                    }
                                }

                                background: Rectangle {
                                    color: "#2d3748"
                                    radius: 4
                                    border.width: 1
                                    border.color: "#4b5563"
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 10

                            Button {
                                text: "执行清洗"
                                width: (parent.width - 20) / 2
                                height: 32
                                background: Rectangle {
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: "#00b09b" }
                                        GradientStop { position: 1.0; color: "#96c93d" }
                                    }
                                    radius: 4
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    font.pixelSize: 12
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onClicked: {
                                    root.handleExecuteDataCleaningFromCache()
                                }
                            }

                            Button {
                                text: "导出数据"
                                width: (parent.width - 20) / 2
                                height: 32
                                background: Rectangle {
                                    color: "#3b82f6"
                                    radius: 4
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    font.pixelSize: 13
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onClicked: {
                                    exportCleanedData()
                                }
                            }
                        }
                    }
                }
                
                // 状态信息
                Rectangle {
                    width: parent.width
                    height: 100
                    color: "#2d3748"
                    radius: 6
                    
                    Column {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 5
                        
                        Text {
                            text: "系统状态"
                            font.pixelSize: 16
                            font.bold: true
                            color: "white"
                            width: parent.width
                        }
                        
                        Row {
                            width: parent.width
                            spacing: 10
                            
                            Text {
                                text: "数据服务: "
                                font.pixelSize: 14
                                color: "#a0aec0"
                            }
                            
                            Text {
                                text: "运行正常"
                                font.pixelSize: 14
                                font.bold: true
                                color: "#10b981"
                            }
                            
                            Item {
                                width: parent.width - childrenRect.width - 20
                                height: 1
                            }
                        }
                        
                        Row {
                            width: parent.width
                            spacing: 10
                            
                            Text {
                                text: "数据库连接: "
                                font.pixelSize: 14
                                color: "#a0aec0"
                            }
                            
                            Text {
                                text: "已连接"
                                font.pixelSize: 14
                                font.bold: true
                                color: "#10b981"
                            }
                            
                            Item {
                                width: parent.width - childrenRect.width - 20
                                height: 1
                            }
                        }
                    }
                }
                } // 结束 contentColumn
            } // 结束 Flickable
        } // 结束 Rectangle
    } // 结束 ColumnLayout
    } // 结束背景 Rectangle
    
    // 缓存显示模型
    ListModel {
        id: cacheDisplayModel
    }
    
    // DataFetchController实例 - 用于数据获取和清洗（遵循不在QML中操作数据的原则）
    DataFetchController {
        id: dataFetchController
        
        onDataCleaningStarted: function() {
            root.handlePanelStatusRequested("⏳ 开始数据清洗...", "warning")
        }
        
        onDataCleaningProgress: function(progress, message) {
            root.handlePanelStatusRequested(`⏳ ${message} (${progress}%)`, "warning")
        }

        onDataFetchProgress: function(progress, message) {
            root.handlePanelStatusRequested(`⏳ ${message} (${progress}%)`, "warning")
        }
        
        onDataCleaningCompleted: function(success, message, cleanedData) {
            console.log("[QML] onDataCleaningCompleted: success=" + success + " message=" + message + " rows=" + (cleanedData ? cleanedData.length : 0))
            if (success) {
                root.handlePanelStatusRequested(`✓ ${message}`, "success")
                if (dataFetchController) {
                    dataFetchController.refreshDataSetInfos()
                }
            } else {
                root.handlePanelStatusRequested(`❌ ${message}`, "error")
            }
        }
        
        onDataCleaningError: function(error) {
            root.handlePanelStatusRequested(`❌ ${error}`, "error")
        }

        onDataSetCleaned: function(inputId, resultId, message, inputRows, outputRows) {
            handlePanelStatusRequested("清洗完成: " + inputRows + " -> " + outputRows + " 行, 新数据集 ID=" + resultId, "success")
            dataFetchController.refreshDataSetInfos()
        }

        onDataSetInfosRefreshed: function(infos) {
            cacheDisplayModel.clear()
            for (var i = 0; i < infos.length; i++) {
                var info = infos[i]
                cacheDisplayModel.append({
                    index: i,
                    type: "dataset",
                    id: info.id,
                    displayName: info.displayName || "",
                    description: info.displayName || "",
                    cacheKey: "",
                    sourceType: info.sourceType || "",
                    rowCount: info.rowCount || 0,
                    stockCodes: info.stockCodes || [],
                    startDate: info.startDate || "",
                    endDate: info.endDate || ""
                })
            }
        }

        Component.onCompleted: {
            // 初始化完成
        }
    }

    // 状态栏
    Rectangle {
        id: statusBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32
        color: {
            if (statusText.text.includes("成功")) return "#dcfce7"
            else if (statusText.text.includes("失败")) return "#fee2e2"
            else if (statusText.text.includes("中")) return "#fef9c3"
            else return "#f3f4f6"
        }
        border.width: 1
        border.color: {
            if (statusText.text.includes("成功")) return "#86efac"
            else if (statusText.text.includes("失败")) return "#fca5a5"
            else if (statusText.text.includes("中")) return "#fde047"
            else return "#e5e7eb"
        }
        visible: false

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 6

            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: {
                    if (statusText.text.includes("成功")) return "#16a34a"
                    else if (statusText.text.includes("失败")) return "#dc2626"
                    else if (statusText.text.includes("中")) return "#ca8a04"
                    else return "#6b7280"
                }
            }

            Label {
                id: statusText
                text: ""
                font.pixelSize: 12
                color: {
                    if (statusText.text.includes("成功")) return "#166534"
                    else if (statusText.text.includes("失败")) return "#991b1b"
                    else if (statusText.text.includes("中")) return "#854d0e"
                    else return "#374151"
                }
                Layout.fillWidth: true
            }
        }
    }

    function onProviderSelected(provider) {
        updateStatus("数据源已选择: " + provider)
    }

    function addDataSource() {
        if (!validateForm()) {
            updateStatus("请填写完整配置信息", "error")
            return
        }

        var selectedIndex = dataSelectionPanel.indexComboBox.currentIndex
        var startDate = getDateValue(dataSelectionPanel.startDatePicker)
        var endDate = getDateValue(dataSelectionPanel.endDatePicker)
        var selectedDataTypes = dataSelectionPanel.dataTypeCardsFlow.selectedDataTypes.slice()

        if (selectedDataTypes.length === 0) {
            selectedDataTypes = ["kline_daily"]
        }

        if (selectedIndex >= 0) {
            var displayName = dataSelectionPanel.indexListModel.get(selectedIndex).displayName
            updateStatus("⏳ 正在查询 " + displayName + " 成分股数据...", "warning")
            loadIndexConstituents(selectedDataTypes)
            return
        }

        updateStatus("⏳ 正在查询 " + dataSelectionPanel.marketComboBox.currentText + " 数据...", "warning")
        dataFetchController.fetchDataTypesBySource("all_market", "", selectedDataTypes, startDate, endDate, {
            market: dataSelectionPanel.marketComboBox.currentText,
            provider: dataSelectionPanel.providerComboBox.currentText
        })
    }

    function updateStatus(message, type) {
        var normalizedMessage = ""
        if (message !== undefined && message !== null) {
            normalizedMessage = String(message)
        }

        if (typeof statusText === "undefined" || !statusText) {
            console.warn("statusText 未初始化，状态消息:", normalizedMessage)
            return
        }

        statusText.text = normalizedMessage
        clearStatusTimer.restart()
    }

    Timer {
        id: clearStatusTimer
        interval: 3000
        onTriggered: {
            if (!statusText.text.includes("成功") && !statusText.text.includes("失败")) {
                statusText.text = ""
            }
        }
    }

    function validateForm() {
        if (!dataSelectionPanel.providerComboBox.currentText) {
            return false
        }

        if (dataSelectionPanel.dataTypeCardsFlow.selectedDataTypesCount === 0) {
            updateStatus("请至少选择一种数据类型", "warning")
            return false
        }

        var startDateValue = getDateValue(dataSelectionPanel.startDatePicker)
        var endDateValue = getDateValue(dataSelectionPanel.endDatePicker)

        if (!startDateValue || !endDateValue) {
            updateStatus("请设置时间范围", "warning")
            return false
        }

        return true
    }

    function getDateValue(datePicker) {
        if (datePicker && typeof datePicker !== "undefined") {
            if (datePicker.selectedDate && datePicker.selectedDate !== "undefined") {
                return datePicker.selectedDate
            }
            if (datePicker.date && datePicker.date !== "undefined") {
                return datePicker.date
            }
            if (datePicker.text && datePicker.text !== "" && datePicker.text !== "YYYY-MM-DD") {
                return datePicker.text
            }
        }
        var today = new Date()
        return Qt.formatDate(today, "yyyy-MM-dd")
    }

    function getSelectedDataTypeNames() {
        var names = []
        for (var i = 0; i < dataSelectionPanel.dataTypeCardsFlow.selectedDataTypes.length; i++) {
            names.push(dataSelectionPanel.dataTypeCardsFlow.getDataTypeName(dataSelectionPanel.dataTypeCardsFlow.selectedDataTypes[i]))
        }
        return names
    }

    Connections {
        target: dataFetchController.previewModel

        function onCategoryCountsChanged() {
            syncPreviewTabSelection()
        }

        function onCurrentCategoryChanged() {
            syncPreviewTabSelection()
        }

        function onDataUpdated() {
            syncPreviewTabSelection()
            ensurePreviewVisible()
        }
    }

    Connections {
        target: dataSelectionPanel.dataTypeCardsFlow

        function onSelectedDataTypesChanged() {
            syncPreviewTabSelection()
        }
    }

    function getSelectedRulesValue() {
        return selectedRules ? selectedRules : []
    }

    function defaultSelectedRuleIds() {
        var defaults = []
        for (var i = 0; i < availableCleaningRules.length; i++) {
            if (availableCleaningRules[i].defaultValue || availableCleaningRules[i].ruleLevel === "必选") {
                defaults.push(availableCleaningRules[i].ruleId)
            }
        }
        return defaults
    }

    function isMandatoryRule(ruleId) {
        var rule = getRuleById(ruleId)
        return String(rule.ruleLevel || "") === "必选"
    }

    function normalizeSelectedRuleIds(ruleIds) {
        var normalized = []
        var seen = {}
        for (var i = 0; i < availableCleaningRules.length; i++) {
            var ruleId = availableCleaningRules[i].ruleId
            var shouldInclude = availableCleaningRules[i].ruleLevel === "必选"
            for (var j = 0; j < (ruleIds ? ruleIds.length : 0); j++) {
                if (ruleIds[j] === ruleId) {
                    shouldInclude = true
                    break
                }
            }
            if (shouldInclude && !seen[ruleId]) {
                normalized.push(ruleId)
                seen[ruleId] = true
            }
        }
        return normalized
    }

    function buildCleaningRules(startDateValue, endDateValue) {
        void startDateValue
        void endDateValue
        var rules = {}

        for (var i = 0; i < selectedRules.length; i++) {
            var ruleId = selectedRules[i]
            var ruleConfig = buildCleaningRuleConfig(ruleId)
            if (ruleConfig) {
                rules[ruleId] = ruleConfig
            } else {
                console.log("未知规则ID:", ruleId)
            }
        }

        return rules
    }
    
    function executeDataCleaning() {
        // 执行数据清洗 - 遵循不在QML中操作数据的原则
        updateStatus("⏳ 执行数据清洗...", "warning")
        console.log("开始执行数据清洗 - 使用DataFetchController")
        
        // 检查DataFetchController是否可用
        if (!dataFetchController) {
            updateStatus("❌ DataFetchController未初始化", "error")
            return
        }
        
        var rules = buildCleaningRules(
            getDateValue(dataSelectionPanel.startDatePicker),
            getDateValue(dataSelectionPanel.endDatePicker)
        )
        
        console.log("传递规则给DataFetchController:", JSON.stringify(rules))
        
        // 调用DataFetchController的异步清洗方法
        // 所有数据操作都在C++中完成，QML只传递规则
        dataFetchController.cleanDataAsync(rules)
        updateStatus("⏳ 正在清洗数据...", "warning")
    }
    
    function executeDataCleaningFromCache() {
        // 执行数据清洗 - 使用缓存索引选择数据，遵循不在QML中操作数据的原则
        root.handlePanelStatusRequested("⏳ 执行缓存数据清洗...", "warning")
        console.log("开始执行缓存数据清洗 - 使用缓存索引:", root.currentCacheIndex)
        
        // 检查DataFetchController是否可用
        if (!dataFetchController) {
            root.handlePanelStatusRequested("❌ DataFetchController未初始化", "error")
            return
        }
        
        // 检查是否已选择缓存
        if (root.currentCacheIndex < 0 || root.currentCacheIndex >= cacheDisplayModel.count) {
            root.handlePanelStatusRequested("❌ 请先选择缓存数据", "error")
            return
        }
        
        var rules = buildCleaningRules(
            getDateValue(dataSelectionPanel.startDatePicker),
            getDateValue(dataSelectionPanel.endDatePicker)
        )
        
        console.log("传递规则给DataFetchController:", JSON.stringify(rules))
        
        // 调用DataFetchController的缓存索引清洗方法
        // 所有数据操作都在C++中完成，QML只传递索引和规则
        dataFetchController.cleanDataAsync(rules)
        root.handlePanelStatusRequested("⏳ 正在清洗缓存数据...", "warning")
    }
    
        // 指数成分股相关功能函数
    function loadIndexConstituents(selectedDataTypes) {
        // 加载指数成分股 - 所有逻辑判断在C++中完成
        var selectedIndex = dataSelectionPanel.indexComboBox.currentIndex
        if (selectedIndex < 0) {
            updateStatus("❌ 请先选择一个指数", "error")
            return
        }
        
        var indexSymbol = dataSelectionPanel.indexListModel.get(selectedIndex).symbol
        var displayName = dataSelectionPanel.indexListModel.get(selectedIndex).displayName
        
        // 获取日期范围
        var startDate = getDateValue(dataSelectionPanel.startDatePicker)
        var endDate = getDateValue(dataSelectionPanel.endDatePicker)
        
        if (!startDate || !endDate) {
            updateStatus("❌ 请设置时间范围", "error")
            return
        }
        
        updateStatus("⏳ 正在加载 " + displayName + " 的数据...", "warning")
        
        // 检查是否选择了数据类型
        if (selectedDataTypes.length === 0) {
            // 如果没有选择数据类型，默认使用日线和财务数据
            updateStatus("⚠️ 未选择数据类型，默认使用日线和财务数据", "warning")
            // 调用批量接口，让C++按顺序处理所有逻辑
            dataFetchController.fetchDataTypesBySource("index", indexSymbol, ["kline_daily", "financial"], startDate, endDate, {})
        } else {
            // 对于每个选择的数据类型，调用批量接口
            dataFetchController.fetchDataTypesBySource("index", indexSymbol, selectedDataTypes, startDate, endDate, {})
        }
    }
    // 更新缓存显示模型 - 使用完整的缓存信息（包含索引、类型、ID等）
    function updateCacheDisplayModel(cacheInfos) {
        cacheDisplayModel.clear()
        for (var i = 0; i < cacheInfos.length; i++) {
            var cacheInfo = cacheInfos[i]
            var displayName = cacheInfo.displayName || "未知缓存"
            var cacheType = cacheInfo.type || "cache"
            var cacheId = cacheInfo.id || -1
            var cacheKey = cacheInfo.cacheKey || ""
            var description = cacheInfo.description || ""
            
            // 添加到显示模型
            cacheDisplayModel.append({
                displayName: displayName,
                index: i,
                type: cacheType,
                id: cacheId,
                cacheKey: cacheKey,
                description: description
            })
        }
        console.log("缓存显示模型已更新，共", cacheInfos.length, "项")
    }
    
    // 更新缓存显示模型（简化版）- 只使用缓存键字符串列表
    function updateCacheDisplayModelSimple(cacheKeys) {
        cacheDisplayModel.clear()
        for (var i = 0; i < cacheKeys.length; i++) {
            var cacheKey = cacheKeys[i]
            var displayName = "📁 缓存: " + cacheKey
            if (cacheKey.startsWith("data:stock:ALL_")) {
                displayName = "📊 数据集: " + cacheKey.substring(15)
            } else if (cacheKey.startsWith("dataset_")) {
                displayName = "📊 数据集: " + cacheKey.substring(8).replace(/_/g, " 至 ")
            }
            cacheDisplayModel.append({
                displayName: displayName,
                index: i,
                type: "cache",
                cacheKey: cacheKey
            })
        }
        console.log("缓存显示模型（简化版）已更新，共", cacheKeys.length, "项")
    }
    
    // RuleConfigCard组件定义 - 与DataSourceModal对齐
    component RuleConfigCard: Rectangle {
        id: ruleCard
        property string ruleId: ""
        property string ruleName: ""
        property string icon: ""
        property color cardColor: "#3b82f6"
        property string ruleLevel: ""
        property bool defaultValue: false
        property var parentPage: null
        property bool mandatoryRule: String(ruleLevel || "") === "必选"
        property var selectedRuleIds: parentPage ? parentPage.selectedRules : []
        property bool cardEnabled: mandatoryRule || (selectedRuleIds && selectedRuleIds.indexOf(ruleId) !== -1)
        
        width: 146
        height: 58
        radius: 8

        color: cardEnabled ? Qt.lighter(cardColor, 1.4) : (ruleCardMouseArea.containsMouse ? Qt.lighter("#1a2538", 1.2) : "#1a2538")
        border.width: cardEnabled ? 2 : 1
        border.color: cardEnabled ? cardColor : "#4b5563"
        
        MouseArea {
            id: ruleCardMouseArea
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: ruleCard.mandatoryRule ? Qt.ForbiddenCursor : Qt.PointingHandCursor
            onClicked: {
                if (!parentPage || ruleCard.mandatoryRule) {
                    return
                }

                var nextRules = parentPage.selectedRules ? parentPage.selectedRules.slice() : []
                var existingIndex = nextRules.indexOf(ruleId)
                if (existingIndex >= 0) {
                    nextRules.splice(existingIndex, 1)
                } else {
                    nextRules.push(ruleId)
                }

                parentPage.selectedRules = nextRules
                if (typeof parentPage.selectedRulesChanged === "function") {
                    parentPage.selectedRulesChanged()
                }
            }

            enabled: !ruleCard.mandatoryRule
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                Text {
                    text: ruleCard.icon
                    font.pixelSize: 14
                }

                Text {
                    text: ruleCard.ruleName
                    font.pixelSize: 12
                    font.bold: true
                    color: ruleCard.cardEnabled ? ruleCard.cardColor : "#f3f4f6"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Rectangle {
                    width: 14
                    height: 14
                    radius: 7
                    color: ruleCard.cardEnabled ? ruleCard.cardColor : "transparent"
                    border.width: 1
                    border.color: ruleCard.cardEnabled ? ruleCard.cardColor : "#9ca3af"

                    Text {
                        text: "✓"
                        color: "white"
                        font.pixelSize: 8
                        font.bold: true
                        anchors.centerIn: parent
                        visible: ruleCard.cardEnabled
                    }
                }
            }

            Component.onCompleted: {
                if (defaultValue && parentPage && parentPage.selectedRules && parentPage.selectedRules.indexOf(ruleId) === -1) {
                    parentPage.selectedRules.push(ruleId)
                    if (typeof parentPage.selectedRulesChanged === "function") {
                        parentPage.selectedRulesChanged()
                    }
                }
            }
        }
    }
}



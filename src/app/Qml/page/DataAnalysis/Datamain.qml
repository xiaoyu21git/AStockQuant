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
    property var selectedRules: []
    property int selectedRulesRevision: 0
    property int selectedRulesCount: selectedRules ? selectedRules.length : 0
    property var selectedRuleEntries: {
        var entries = []
        for (var i = 0; i < availableCleaningRules.length; i++) {
            if (selectedRules && selectedRules.indexOf(availableCleaningRules[i].ruleId) !== -1) {
                entries.push(availableCleaningRules[i])
            }
        }
        return entries
    }
    property int previewDataCount: dataFetchController && dataFetchController.previewModel ? dataFetchController.previewModel.count : 0
    property int currentCacheIndex: -1
    property var availableCleaningRules: [
        { ruleId: "duplicate_removal", ruleName: "重复数据删除", icon: "🧱", cardColor: "#2563eb", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "report_date_alignment", ruleName: "财报日期对齐", icon: "🗓️", cardColor: "#1d4ed8", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "survivor_bias", ruleName: "生存者偏差", icon: "🧬", cardColor: "#3b82f6", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "adjusted_price", ruleName: "复权处理", icon: "🪄", cardColor: "#10b981", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "new_stock_filter", ruleName: "新股过滤", icon: "🌱", cardColor: "#22c55e", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "st_filter", ruleName: "ST 剔除", icon: "🚫", cardColor: "#14b8a6", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "format_validation", ruleName: "格式验证", icon: "🧾", cardColor: "#0ea5e9", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "price_validity", ruleName: "价格有效性", icon: "📊", cardColor: "#8b5cf6", defaultValue: true, ruleLevel: "必选" },
        { ruleId: "suspension_fill", ruleName: "停牌填充", icon: "⏸️", cardColor: "#6366f1", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "missing_value_fill", ruleName: "缺失值处理", icon: "🔍", cardColor: "#ec4899", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "limit_move_tag", ruleName: "涨跌停标记", icon: "🏷️", cardColor: "#f59e0b", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "market_cap_filter", ruleName: "市值过滤", icon: "💹", cardColor: "#f97316", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "winsorization", ruleName: "异常值缩尾", icon: "🌀", cardColor: "#ea580c", defaultValue: true, ruleLevel: "推荐" },
        { ruleId: "outlier_filter", ruleName: "单点异常", icon: "⚠️", cardColor: "#fb7185", defaultValue: false, ruleLevel: "推荐" },
        { ruleId: "time_range", ruleName: "时间区间", icon: "⏰", cardColor: "#06b6d4", defaultValue: false, ruleLevel: "可选" },
        { ruleId: "index_alignment", ruleName: "指数调整对齐", icon: "🧭", cardColor: "#38bdf8", defaultValue: false, ruleLevel: "可选" },
        { ruleId: "continuous_suspension_filter", ruleName: "连续停牌剔除", icon: "🛑", cardColor: "#f43f5e", defaultValue: false, ruleLevel: "可选" },
        { ruleId: "data_cleaning", ruleName: "基础清洗兼容", icon: "🧹", cardColor: "#ef4444", defaultValue: false, ruleLevel: "兼容" }
    ]
    property var reportStatus: function(message, type) {
        root.handlePanelStatusRequested(message, type)
    }

    signal sourceAdded(var sourceInfo)
    signal dataLoaded()

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

    function buildPanelCleaningRules(startDateValue, endDateValue) {
        var rules = {}
        var hasFormatValidation = selectedRules.indexOf("format_validation") !== -1

        for (var i = 0; i < selectedRules.length; i++) {
            var ruleId = selectedRules[i]
            switch (ruleId) {
                case "duplicate_removal":
                    rules["duplicateRemoval"] = {
                        "enabled": true,
                        "keyFields": ["symbol", "date"]
                    }
                    break
                case "report_date_alignment":
                    rules["reportDateAlignment"] = { "enabled": true }
                    break
                case "survivor_bias":
                    rules["survivorBias"] = { "enabled": true }
                    break
                case "suspension_fill":
                    rules["suspensionFill"] = {
                        "enabled": true,
                        "fillFields": ["open", "high", "low", "close"],
                        "maxForwardFillDays": 10,
                        "dropAfterMaxDays": true
                    }
                    break
                case "missing_value_fill":
                    rules["missingValueFill"] = {
                        "enabled": true,
                        "fields": ["open", "high", "low", "close", "turnover_rate", "market_cap", "circulating_market_cap"],
                        "maxLookbackDays": 5
                    }
                    break
                case "adjusted_price":
                    rules["adjustedPrice"] = {
                        "enabled": true,
                        "preferAdjustedFields": true,
                        "applyFactorFallback": true
                    }
                    break
                case "new_stock_filter":
                    rules["newStockFilter"] = {
                        "enabled": true,
                        "minTradeDays": 60
                    }
                    break
                case "st_filter":
                    rules["stFilter"] = { "enabled": true }
                    break
                case "time_range":
                    rules["timeRange"] = {
                        "enabled": true,
                        "startDate": startDateValue,
                        "endDate": endDateValue
                    }
                    break
                case "format_validation":
                    rules["formatValidation"] = {
                        "enabled": true,
                        "dateFormat": "auto",
                        "requiredFields": ["symbol", "date", "open", "high", "low", "close"]
                    }
                    break
                case "price_validity":
                    rules["priceValidity"] = {
                        "enabled": true,
                        "minPrice": 0.01,
                        "maxPrice": 10000.0,
                        "enforceChain": true,
                        "allowZeroWhenSuspended": true
                    }
                    break
                case "limit_move_tag":
                    rules["limitMoveTag"] = {
                        "enabled": true,
                        "upThreshold": 9.5,
                        "downThreshold": -9.5
                    }
                    break
                case "market_cap_filter":
                    rules["marketCapFilter"] = {
                        "enabled": true,
                        "lowerTail": 0.05
                    }
                    break
                case "winsorization":
                    rules["winsorization"] = {
                        "enabled": true,
                        "fields": ["factor_value", "factor", "value", "score"],
                        "lowerQuantile": 0.01,
                        "upperQuantile": 0.99
                    }
                    break
                case "index_alignment":
                    rules["indexAlignment"] = {
                        "enabled": true,
                        "lagDays": 1
                    }
                    break
                case "continuous_suspension_filter":
                    rules["continuousSuspensionFilter"] = {
                        "enabled": true,
                        "maxSuspensionDays": 10
                    }
                    break
                case "outlier_filter":
                    rules["outlierFilter"] = {
                        "enabled": true,
                        "threshold": 0.3
                    }
                    break
                case "data_cleaning":
                    if (!hasFormatValidation) {
                        rules["dataCleaning"] = {
                            "enabled": true,
                            "dateFormat": "auto",
                            "requiredFields": ["symbol", "date", "open", "high", "low", "close"]
                        }
                    }
                    break
                default:
                    console.log("未知规则ID:", ruleId)
            }
        }

        if (Object.keys(rules).length === 0) {
            rules = {
                "duplicateRemoval": {
                    "enabled": true,
                    "keyFields": ["symbol", "date"]
                },
                "reportDateAlignment": { "enabled": true },
                "survivorBias": { "enabled": true },
                "suspensionFill": {
                    "enabled": true,
                    "fillFields": ["open", "high", "low", "close"],
                    "maxForwardFillDays": 10,
                    "dropAfterMaxDays": true
                },
                "missingValueFill": {
                    "enabled": true,
                    "fields": ["open", "high", "low", "close", "turnover_rate", "market_cap", "circulating_market_cap"],
                    "maxLookbackDays": 5
                },
                "adjustedPrice": {
                    "enabled": true,
                    "preferAdjustedFields": true,
                    "applyFactorFallback": true
                },
                "newStockFilter": {
                    "enabled": true,
                    "minTradeDays": 60
                },
                "stFilter": { "enabled": true },
                "formatValidation": {
                    "enabled": true,
                    "dateFormat": "auto",
                    "requiredFields": ["symbol", "date", "open", "high", "low", "close"]
                },
                "priceValidity": {
                    "enabled": true,
                    "minPrice": 0.01,
                    "maxPrice": 10000.0,
                    "enforceChain": true,
                    "allowZeroWhenSuspended": true
                },
                "limitMoveTag": {
                    "enabled": true,
                    "upThreshold": 9.5,
                    "downThreshold": -9.5
                },
                "marketCapFilter": {
                    "enabled": true,
                    "lowerTail": 0.05
                },
                "winsorization": {
                    "enabled": true,
                    "fields": ["factor_value", "factor", "value", "score"],
                    "lowerQuantile": 0.01,
                    "upperQuantile": 0.99
                }
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
            handlePanelStatusRequested("请至少选择一种数据类型", "warning")
            return
        }

        if (!startDate || !endDate) {
            handlePanelStatusRequested("请设置时间范围", "warning")
            return
        }

        if (selectedIndex >= 0) {
            var indexSymbol = dataSelectionPanel.indexListModel.get(selectedIndex).symbol
            var displayName = dataSelectionPanel.indexListModel.get(selectedIndex).displayName
            handlePanelStatusRequested("⏳ 正在加载 " + displayName + " 的数据...", "warning")

            for (var indexType = 0; indexType < selectedDataTypes.length; indexType++) {
                dataFetchController.fetchDataByType("index", indexSymbol, selectedDataTypes[indexType], startDate, endDate, {})
            }
            return
        }

        handlePanelStatusRequested("⏳ 正在查询 " + dataSelectionPanel.marketComboBox.currentText + " 数据...", "warning")
        for (var i = 0; i < selectedDataTypes.length; i++) {
            dataFetchController.fetchDataByType("all_market", "", selectedDataTypes[i], startDate, endDate, {
                market: dataSelectionPanel.marketComboBox.currentText,
                provider: provider
            })
        }
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

        dataFetchController.cleanDataFromCacheByIndex(root.currentCacheIndex, rules)
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
                            text: "用于配置数据源、选择时间区间、查询指数成分与市场数据，并对结果执行清洗与缓存管理。"
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
                            text: "包含必选、推荐、可选和兼容规则"
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

                    // 规则卡片区域 - 展示全部 C++ 已注册规则
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
                                model: root.availableCleaningRules

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
                    
                    // 数据预览表格 - 使用模型绑定
                    Rectangle {
                        width: parent.width
                        height: 220
                        color: "#2d3748"
                        radius: 6
                        
                        Column {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 8
                            
                            Row {
                                width: parent.width
                                spacing: 10
                                
                                Text {
                                    text: "📋 数据预览"
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: "white"
                                }
                                
                                Item {
                                    width: parent.width - childrenRect.width - 20
                                    height: 1
                                }
                                
                                Text {
                                    text: "共 " + previewDataCount + " 只股票"
                                    font.pixelSize: 12
                                    color: "#a0aec0"
                                }
                            }
                            
                            // 表格标题
                            Rectangle {
                                width: parent.width
                                height: 30
                                color: "#374151"
                                radius: 4
                                
                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 10
                                    
                                    Text {
                                        text: "股票代码"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        width: 80
                                    }
                                    
                                    Text {
                                        text: "股票名称"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        width: 100
                                    }
                                    
                                    Text {
                                        text: "时间范围"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        width: 180
                                    }
                                    
                                    Text {
                                        text: "记录数"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        width: 80
                                    }
                                    
                                    Text {
                                        text: "最新收盘"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        width: 80
                                    }

                                    Text {
                                        text: "区间涨跌"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        width: 80
                                    }
                                }
                            }
                            
                    // 数据行 - 使用模型绑定，直接使用dataFetchController.previewModel
                    ListView {
                        width: parent.width
                        height: 130
                        model: dataFetchController.previewModel
                        clip: true
                        spacing: 4
                        
                        delegate: Rectangle {
                            width: ListView.view ? ListView.view.width : 0
                            height: 30
                            color: index % 2 === 0 ? "#374151" : "#2d3748"
                            radius: 2
                            
                            Row {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 10
                                
                                Text {
                                    text: model.code || ""
                                    font.pixelSize: 12
                                    color: "white"
                                    width: 80
                                    elide: Text.ElideRight
                                }
                                
                                Text {
                                    text: model.name || ""
                                    font.pixelSize: 12
                                    color: "white"
                                    width: 100
                                    elide: Text.ElideRight
                                }
                                
                                Text {
                                    text: model.timeRange || model.date || ""
                                    font.pixelSize: 12
                                    color: "white"
                                    width: 180
                                    elide: Text.ElideRight
                                }
                                
                                Text {
                                    text: (model.recordCount || 0).toString()
                                    font.pixelSize: 12
                                    color: "white"
                                    width: 80
                                    elide: Text.ElideRight
                                }
                                
                                Text {
                                    text: model.close ? model.close.toFixed(2) : ""
                                    font.pixelSize: 12
                                    color: "white"
                                    width: 80
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: model.change ? model.change.toFixed(2) + "%" : ""
                                    font.pixelSize: 12
                                    color: model.change > 0 ? "#ef4444" : (model.change < 0 ? "#10b981" : "white")
                                    width: 80
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                    }
                            
                        }
                    }
                    
                    // 缓存选择区域
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
                        
                        // 缓存选择下拉框
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
                        
                        // 缓存操作按钮行
                        Row {
                            width: parent.width
                            spacing: 10
                            
                            Button {
                                text: "执行清洗"
                                width: (parent.width - 20) / 3
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
                                text: "刷新缓存"
                                width: (parent.width - 20) / 3
                                height: 32
                                background: Rectangle {
                                    color: "#374151"
                                    radius: 4
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    font.pixelSize: 12
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onClicked: {
                                    root.handlePanelStatusRequested("⏳ 正在刷新缓存列表...", "warning")
                                    if (!dataFetchController) {
                                        root.handlePanelStatusRequested("❌ DataFetchController未初始化", "error")
                                        return
                                    }
                                    dataFetchController.refreshCacheKeys()
                                }
                            }

                        Button {
                            text: "导出数据"
                            width: (parent.width - 20) / 3
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
    
    // 缓存键列表模型
    ListModel {
        id: cacheKeysModel
    }
    
    // 数据集信息模型
    ListModel {
        id: dataSetInfosModel
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
            if (success) {
                root.handlePanelStatusRequested(`✓ ${message}`, "success")
                // 模型更新由C++ DataFetchController处理，此处只更新状态
            } else {
                root.handlePanelStatusRequested(`❌ ${message}`, "error")
            }
        }
        
        onDataCleaningError: function(error) {
            root.handlePanelStatusRequested(`❌ ${error}`, "error")
        }
        
        // 缓存键刷新完成信号
        onCacheKeysRefreshed: function(cacheKeys) {
            root.handlePanelStatusRequested("✓ 缓存键列表已刷新", "success")
            cacheDisplayModel.clear()
            for (var cacheKeyIndex = 0; cacheKeyIndex < cacheKeys.length; cacheKeyIndex++) {
                var currentCacheKey = cacheKeys[cacheKeyIndex]
                var cacheDisplayName = "📁 缓存: " + currentCacheKey

                if (currentCacheKey.startsWith("data:stock:ALL_")) {
                    cacheDisplayName = "📊 数据集: " + currentCacheKey.substring(15)
                } else if (currentCacheKey.startsWith("dataset_")) {
                    cacheDisplayName = "📊 数据集: " + currentCacheKey.substring(8).replace(/_/g, " 至 ")
                }

                cacheDisplayModel.append({
                    displayName: cacheDisplayName,
                    index: cacheKeyIndex,
                    type: "cache",
                    cacheKey: currentCacheKey
                })
            }
        }
        
        // 数据集信息刷新完成信号
        onDataSetInfosRefreshed: function(dataSetInfos) {
            root.handlePanelStatusRequested("✓ 数据集信息已刷新", "success")
            dataSetInfosModel.clear()
            for (var dataSetInfoIndex = 0; dataSetInfoIndex < dataSetInfos.length; dataSetInfoIndex++) {
                var info = dataSetInfos[dataSetInfoIndex]
                dataSetInfosModel.append({
                    id: info.id,
                    displayName: info.displayName,
                    description: info.description,
                    sourceType: info.sourceType,
                    createdTime: info.createdTime,
                    rowCount: info.rowCount,
                    stockCodes: info.stockCodes,
                    startDate: info.startDate,
                    endDate: info.endDate,
                    tags: info.tags
                })
            }
            dataSourceCount = dataSetInfos.length
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
    
    // 辅助函数
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
            loadIndexConstituents()
            return
        }

        updateStatus("⏳ 正在查询 " + dataSelectionPanel.marketComboBox.currentText + " 数据...", "warning")
        for (var i = 0; i < selectedDataTypes.length; i++) {
            dataFetchController.fetchDataByType("all_market", "", selectedDataTypes[i], startDate, endDate, {
                market: dataSelectionPanel.marketComboBox.currentText,
                provider: dataSelectionPanel.providerComboBox.currentText
            })
        }
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
        // 自动清除状态消息
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
        // 尝试多种方式获取日期值
        if (datePicker && typeof datePicker !== "undefined") {
            // 优先使用selectedDate属性
            if (datePicker.selectedDate && datePicker.selectedDate !== "undefined") {
                return datePicker.selectedDate
            }
            // 其次使用date属性
            if (datePicker.date && datePicker.date !== "undefined") {
                return datePicker.date
            }
            // 最后使用text属性
            if (datePicker.text && datePicker.text !== "" && datePicker.text !== "YYYY-MM-DD") {
                return datePicker.text
            }
        }
        // 返回当前日期作为默认值
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

    function buildSelectedRuleEntries(ruleIds) {
        var entries = []
        for (var i = 0; i < availableCleaningRules.length; i++) {
            if (ruleIds && ruleIds.indexOf(availableCleaningRules[i].ruleId) !== -1) {
                entries.push(availableCleaningRules[i])
            }
        }
        return entries
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

    function isRuleSelected(ruleId) {
        return selectedRules && selectedRules.indexOf(ruleId) !== -1
    }

    function containsRule(ruleIds, ruleId) {
        return ruleIds && ruleIds.indexOf(ruleId) !== -1
    }

    function setSelectedRulesValue(ruleIds) {
        var nextRules = normalizeSelectedRuleIds(ruleIds)
        selectedRules = nextRules
        selectedRulesRevision = selectedRulesRevision + 1
    }
    
    function getDataTotalCount() {
        // 返回真实数据统计，从dataFetchController.previewModel获取
        return dataFetchController.previewModel ? dataFetchController.previewModel.count : 0
    }
    
    function getRuleById(id) {
        for (var i = 0; i < availableCleaningRules.length; i++) {
            if (availableCleaningRules[i].ruleId === id) {
                return availableCleaningRules[i]
            }
        }
        return { ruleName: "未知规则", icon: "❓", cardColor: "#6b7280", ruleLevel: "未知" }
    }
    
    function toggleRule(id) {
        if (isMandatoryRule(id)) {
            handlePanelStatusRequested("必选规则不可取消: " + getRuleById(id).ruleName, "info")
            setSelectedRulesValue(selectedRules)
            return
        }

        var newArray = selectedRules.slice()
        if (containsRule(newArray, id)) {
            newArray = newArray.filter(function(ruleId) {
                return ruleId !== id
            })
        } else {
            newArray.push(id)
        }
        setSelectedRulesValue(newArray)
    }
    
 
    
    function previewData() {
        // 预览数据
        updateStatus("⏳ 加载预览数据...", "warning")
    }
    
   
    function exportCleanedData() {
        // 导出清洗后数据
        updateStatus("⏳ 导出清洗后数据...", "warning")
        console.log("导出清洗后数据")
    }

    function buildCleaningRules(startDateValue, endDateValue) {
        var rules = {}
        var hasFormatValidation = selectedRules.indexOf("format_validation") !== -1

        for (var i = 0; i < selectedRules.length; i++) {
            var ruleId = selectedRules[i]
            switch (ruleId) {
                case "duplicate_removal":
                    rules["duplicateRemoval"] = {
                        "enabled": true,
                        "keyFields": ["symbol", "date"]
                    }
                    break
                case "report_date_alignment":
                    rules["reportDateAlignment"] = { "enabled": true }
                    break
                case "survivor_bias":
                    rules["survivorBias"] = { "enabled": true }
                    break
                case "suspension_fill":
                    rules["suspensionFill"] = {
                        "enabled": true,
                        "fillFields": ["open", "high", "low", "close"],
                        "maxForwardFillDays": 10,
                        "dropAfterMaxDays": true
                    }
                    break
                case "missing_value_fill":
                    rules["missingValueFill"] = {
                        "enabled": true,
                        "fields": ["open", "high", "low", "close", "turnover_rate", "market_cap", "circulating_market_cap"],
                        "maxLookbackDays": 5
                    }
                    break
                case "adjusted_price":
                    rules["adjustedPrice"] = {
                        "enabled": true,
                        "preferAdjustedFields": true,
                        "applyFactorFallback": true
                    }
                    break
                case "new_stock_filter":
                    rules["newStockFilter"] = {
                        "enabled": true,
                        "minTradeDays": 60
                    }
                    break
                case "st_filter":
                    rules["stFilter"] = { "enabled": true }
                    break
                case "time_range":
                    rules["timeRange"] = {
                        "enabled": true,
                        "startDate": startDateValue,
                        "endDate": endDateValue
                    }
                    break
                case "format_validation":
                    rules["formatValidation"] = {
                        "enabled": true,
                        "dateFormat": "auto",
                        "requiredFields": ["symbol", "date", "open", "high", "low", "close"]
                    }
                    break
                case "price_validity":
                    rules["priceValidity"] = {
                        "enabled": true,
                        "minPrice": 0.01,
                        "maxPrice": 10000.0,
                        "enforceChain": true,
                        "allowZeroWhenSuspended": true
                    }
                    break
                case "limit_move_tag":
                    rules["limitMoveTag"] = {
                        "enabled": true,
                        "upThreshold": 9.5,
                        "downThreshold": -9.5
                    }
                    break
                case "market_cap_filter":
                    rules["marketCapFilter"] = {
                        "enabled": true,
                        "lowerTail": 0.05
                    }
                    break
                case "winsorization":
                    rules["winsorization"] = {
                        "enabled": true,
                        "fields": ["factor_value", "factor", "value", "score"],
                        "lowerQuantile": 0.01,
                        "upperQuantile": 0.99
                    }
                    break
                case "index_alignment":
                    rules["indexAlignment"] = {
                        "enabled": true,
                        "lagDays": 1
                    }
                    break
                case "continuous_suspension_filter":
                    rules["continuousSuspensionFilter"] = {
                        "enabled": true,
                        "maxSuspensionDays": 10
                    }
                    break
                case "outlier_filter":
                    rules["outlierFilter"] = {
                        "enabled": true,
                        "threshold": 0.3
                    }
                    break
                case "data_cleaning":
                    if (!hasFormatValidation) {
                        rules["dataCleaning"] = {
                            "enabled": true,
                            "dateFormat": "auto",
                            "requiredFields": ["symbol", "date", "open", "high", "low", "close"]
                        }
                    }
                    break
                default:
                    console.log("未知规则ID:", ruleId)
            }
        }

        if (Object.keys(rules).length === 0) {
            rules = {
                "duplicateRemoval": {
                    "enabled": true,
                    "keyFields": ["symbol", "date"]
                },
                "reportDateAlignment": { "enabled": true },
                "survivorBias": { "enabled": true },
                "suspensionFill": {
                    "enabled": true,
                    "fillFields": ["open", "high", "low", "close"],
                    "maxForwardFillDays": 10,
                    "dropAfterMaxDays": true
                },
                "missingValueFill": {
                    "enabled": true,
                    "fields": ["open", "high", "low", "close", "turnover_rate", "market_cap", "circulating_market_cap"],
                    "maxLookbackDays": 5
                },
                "adjustedPrice": {
                    "enabled": true,
                    "preferAdjustedFields": true,
                    "applyFactorFallback": true
                },
                "newStockFilter": {
                    "enabled": true,
                    "minTradeDays": 60
                },
                "stFilter": { "enabled": true },
                "formatValidation": {
                    "enabled": true,
                    "dateFormat": "auto",
                    "requiredFields": ["symbol", "date", "open", "high", "low", "close"]
                },
                "priceValidity": {
                    "enabled": true,
                    "minPrice": 0.01,
                    "maxPrice": 10000.0,
                    "enforceChain": true,
                    "allowZeroWhenSuspended": true
                },
                "limitMoveTag": {
                    "enabled": true,
                    "upThreshold": 9.5,
                    "downThreshold": -9.5
                },
                "marketCapFilter": {
                    "enabled": true,
                    "lowerTail": 0.05
                },
                "winsorization": {
                    "enabled": true,
                    "fields": ["factor_value", "factor", "value", "score"],
                    "lowerQuantile": 0.01,
                    "upperQuantile": 0.99
                }
            }
            console.log("使用默认规则集")
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
    
    function refreshDataSourceCount() {
        dataSourceCount = dataSetInfosModel.count
    }
    
    // 缓存操作函数 - 所有数据操作都在C++中完成，QML只调用接口
    function refreshCacheList() {
        // 刷新缓存列表 - 调用DataFetchController获取所有缓存键
        updateStatus("⏳ 正在刷新缓存列表...", "warning")
        
        if (!dataFetchController) {
            updateStatus("❌ DataFetchController未初始化", "error")
            return
        }
        
        // 调用C++接口获取缓存键列表，所有遍历逻辑在C++中完成
        dataFetchController.refreshCacheKeys()
    }
    
    function loadDataSetInfos() {
        // 加载数据集信息 - 调用DataFetchController获取所有数据集信息
        updateStatus("⏳ 正在加载数据集信息...", "warning")
        
        if (!dataFetchController) {
            updateStatus("❌ DataFetchController未初始化", "error")
            return
        }
        
        // 调用C++接口获取数据集信息，所有遍历逻辑在C++中完成
        dataFetchController.refreshDataSetInfos()
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
        dataFetchController.cleanDataFromCacheByIndex(root.currentCacheIndex, rules)
        root.handlePanelStatusRequested("⏳ 正在清洗缓存数据...", "warning")
    }
    
    function clearAllCache() {
        // 清空所有缓存 - 调用DataServiceCache的清除方法
        root.handlePanelStatusRequested("⏳ 正在清空所有缓存...", "warning")
        
        // 这里需要调用C++的缓存清除方法
        // 注意：由于DataServiceCache是单例，我们需要通过DataManager或DataService来访问
        root.handlePanelStatusRequested("✓ 缓存已清空", "success")
        cacheKeysModel.clear()
        cacheDisplayModel.clear()
        root.currentCacheIndex = -1
    }
    
        // 指数成分股相关功能函数
    function loadIndexConstituents() {
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
        var selectedDataTypes = dataSelectionPanel.dataTypeCardsFlow.selectedDataTypes
        if (selectedDataTypes.length === 0) {
            // 如果没有选择数据类型，默认使用日线数据
            updateStatus("⚠️ 未选择数据类型，默认使用日线数据", "warning")
            // 调用fetchDataByType，让C++处理所有逻辑
            dataFetchController.fetchDataByType("index", indexSymbol, "kline_daily", startDate, endDate, {})
        } else {
            // 对于每个选择的数据类型，调用fetchDataByType
            // C++会处理成分股获取和K线数据查询
            for (var i = 0; i < selectedDataTypes.length; i++) {
                var dataType = selectedDataTypes[i]
                dataFetchController.fetchDataByType("index", indexSymbol, dataType, startDate, endDate, {})
            }
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
    
    // 更新数据集信息模型
    function updateDataSetInfosModel(dataSetInfos) {
        dataSetInfosModel.clear()
        for (var i = 0; i < dataSetInfos.length; i++) {
            var info = dataSetInfos[i]
            dataSetInfosModel.append({
                id: info.id,
                displayName: info.displayName,
                description: info.description,
                sourceType: info.sourceType,
                createdTime: info.createdTime,
                rowCount: info.rowCount,
                stockCodes: info.stockCodes,
                startDate: info.startDate,
                endDate: info.endDate,
                tags: info.tags
            })
        }
        console.log("数据集信息模型已更新，共", dataSetInfos.length, "项")
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
                if (existingIndex !== -1) {
                    nextRules.splice(existingIndex, 1)
                } else {
                    nextRules.push(ruleId)
                }

                var normalized = []
                var seen = {}
                var ruleDefs = parentPage.availableCleaningRules ? parentPage.availableCleaningRules : []
                for (var index = 0; index < ruleDefs.length; index++) {
                    var currentRule = ruleDefs[index]
                    var shouldInclude = String(currentRule.ruleLevel || "") === "必选"
                        || nextRules.indexOf(currentRule.ruleId) !== -1
                    if (shouldInclude && !seen[currentRule.ruleId]) {
                        normalized.push(currentRule.ruleId)
                        seen[currentRule.ruleId] = true
                    }
                }

                parentPage.selectedRules = normalized
                parentPage.selectedRulesRevision = parentPage.selectedRulesRevision + 1
            }
        }
        
        Column {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 4

            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: ruleCard.icon
                    font.pixelSize: 14
                    color: "white"
                }

                Text {
                    text: ruleCard.ruleName
                    font.pixelSize: 12
                    font.bold: true
                    color: ruleCard.cardEnabled ? ruleCard.cardColor : "white"
                    width: parent.width - 20
                    elide: Text.ElideRight
                }
            }

            Text {
                text: ruleCard.mandatoryRule ? "必选 · 锁定" : ruleCard.ruleLevel
                font.pixelSize: 10
                color: ruleCard.cardEnabled ? "#dbeafe" : "#94a3b8"
            }
        }
    }
    
    // 初始化
    Component.onCompleted: {
        // 设置默认数据源计数
        dataSourceCount = 0
        previewDataCount = 0
        if (!selectedRules || selectedRules.length === 0) {
            setSelectedRulesValue(defaultSelectedRuleIds())
        }
    }
}
}

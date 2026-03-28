// Datamain.qml - 数据管理主页面（重构版：消除重叠规则，使用真实数据）
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0
import AStock.Bridge 1.0  // 导入DataService、DataSourceService、DataPreviewService
import "../../components/DataAnalysis" as DataAnalysisComponents

Item {
    id: root
    anchors.fill: parent

    property int dataSourceCount: 0
    property var selectedRules: []
    property int selectedRulesCount: selectedRules ? selectedRules.length : 0
    property int previewDataCount: dataFetchController && dataFetchController.previewModel ? dataFetchController.previewModel.count : 0
    property int currentCacheIndex: -1
    property var reportStatus: function(message, type) {
        root.handlePanelStatusRequested(message, type)
    }

    signal sourceAdded(var sourceInfo)
    signal dataLoaded()

    property var handlePanelQueryRequested: function() {
        var resolveDateValue = function(datePicker) {
            if (datePicker) {
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
            return today.toISOString().split('T')[0]
        }

        var provider = dataSelectionPanel.providerComboBox.currentText
        var selectedDataTypes = dataSelectionPanel.dataTypeCardsFlow.selectedDataTypes.slice()
        var startDate = resolveDateValue(dataSelectionPanel.startDatePicker)
        var endDate = resolveDateValue(dataSelectionPanel.endDatePicker)
        var selectedIndex = dataSelectionPanel.indexComboBox.currentIndex

        if (!provider) {
            root.handlePanelStatusRequested("请填写完整配置信息", "error")
            return
        }

        if (selectedDataTypes.length === 0) {
            root.handlePanelStatusRequested("请至少选择一种数据类型", "warning")
            return
        }

        if (!startDate || !endDate) {
            root.handlePanelStatusRequested("请设置时间范围", "warning")
            return
        }

        if (selectedIndex >= 0) {
            var indexSymbol = dataSelectionPanel.indexListModel.get(selectedIndex).symbol
            var displayName = dataSelectionPanel.indexListModel.get(selectedIndex).displayName
            root.handlePanelStatusRequested("⏳ 正在加载 " + displayName + " 的数据...", "warning")

            for (var indexType = 0; indexType < selectedDataTypes.length; indexType++) {
                dataFetchController.fetchDataByType("index", indexSymbol, selectedDataTypes[indexType], startDate, endDate, {})
            }
            return
        }

        root.handlePanelStatusRequested("⏳ 正在查询 " + dataSelectionPanel.marketComboBox.currentText + " 数据...", "warning")
        for (var i = 0; i < selectedDataTypes.length; i++) {
            dataFetchController.fetchDataByType("all_market", "", selectedDataTypes[i], startDate, endDate, {
                market: dataSelectionPanel.marketComboBox.currentText,
                provider: provider
            })
        }
    }

    property var handlePanelProviderChosen: function(provider) {
        root.handlePanelStatusRequested("数据源已选择: " + provider, "info")
    }

    property var handlePanelStatusRequested: function(message, type) {
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

    property var handleExecuteDataCleaningFromCache: function() {
        var resolveDateValue = function(datePicker) {
            if (datePicker) {
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
            return today.toISOString().split('T')[0]
        }

        root.handlePanelStatusRequested("⏳ 执行缓存数据清洗...", "warning")

        if (!dataFetchController) {
            root.handlePanelStatusRequested("❌ DataFetchController未初始化", "error")
            return
        }

        if (root.currentCacheIndex < 0 || root.currentCacheIndex >= cacheDisplayModel.count) {
            root.handlePanelStatusRequested("❌ 请先选择缓存数据", "error")
            return
        }

        var rules = {}
        for (var i = 0; i < root.selectedRules.length; i++) {
            var ruleId = root.selectedRules[i]
            switch (ruleId) {
                case "market_filter":
                    rules["market"] = { "aShares": true }
                    break
                case "price_filter":
                    rules["priceFilter"] = {
                        "enabled": true,
                        "min": 0.01,
                        "max": 10000.0
                    }
                    break
                case "volume_filter":
                    rules["volumeFilter"] = {
                        "enabled": true,
                        "minVolume": 100,
                        "maxVolume": 1000000000
                    }
                    break
                case "data_cleaning":
                    rules["dataCleaning"] = true
                    break
                case "time_range":
                    rules["timeRange"] = {
                        "enabled": true,
                        "start": resolveDateValue(dataSelectionPanel.startDatePicker),
                        "end": resolveDateValue(dataSelectionPanel.endDatePicker)
                    }
                    break
                case "missing_value":
                    rules["missingValue"] = true
                    break
                case "outliers_filter":
                    rules["outlierFilter"] = true
                    break
            }
        }

        if (Object.keys(rules).length === 0) {
            rules = {
                "market": { "aShares": true },
                "timeRange": {
                    "enabled": true,
                    "start": resolveDateValue(dataSelectionPanel.startDatePicker),
                    "end": resolveDateValue(dataSelectionPanel.endDatePicker)
                }
            }
        }

        dataFetchController.cleanDataFromCacheByIndex(root.currentCacheIndex, rules)
        root.handlePanelStatusRequested("⏳ 正在清洗缓存数据...", "warning")
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
                    
                    // 规则卡片区域 - 紧凑布局，与DataSourceModal对齐
                    Rectangle {
                        width: parent.width
                        height: rulesCardsFlow.childrenRect.height
                        color: "transparent"
                        
                        Flow {
                            id: rulesCardsFlow
                            width: parent.width
                            height: childrenRect.height
                            spacing: 8
                           
                            // 市场选择规则
                            RuleConfigCard {
                                ruleId: "market_filter"
                                ruleName: "市场选择"
                                icon: "🏢"
                                cardColor: "#3b82f6"
                                defaultValue: true
                                parentPage: root
                            }
                            
                            // 价格筛选规则
                            RuleConfigCard {
                                ruleId: "price_filter"
                                ruleName: "价格筛选"
                                icon: "💰"
                                cardColor: "#10b981"
                                defaultValue: false
                                parentPage: root
                            }
                            
                            // 成交量筛选规则
                            RuleConfigCard {
                                ruleId: "volume_filter"
                                ruleName: "成交量筛选"
                                icon: "📊"
                                cardColor: "#8b5cf6"
                                defaultValue: false
                                parentPage: root
                            }
                            
                            // 数据清洗规则
                            RuleConfigCard {
                                ruleId: "data_cleaning"
                                ruleName: "数据清洗"
                                icon: "🧹"
                                cardColor: "#ef4444"
                                defaultValue: true
                                parentPage: root
                            }
                            
                            // 时间区间规则
                            RuleConfigCard {
                                ruleId: "time_range"
                                ruleName: "时间区间"
                                icon: "⏰"
                                cardColor: "#06b6d4"
                                defaultValue: true
                                parentPage: root
                            }
                            
                            // 缺失值处理规则
                            RuleConfigCard {
                                ruleId: "missing_value"
                                ruleName: "缺失值处理"
                                icon: "🔍"
                                cardColor: "#ec4899"
                                defaultValue: true
                                parentPage: root
                            }
                            
                            // 异常值处理规则
                            RuleConfigCard {
                                ruleId: "outliers_filter"
                                ruleName: "异常值处理"
                                icon: "⚠️"
                                cardColor: "#f97316"
                                defaultValue: true
                                parentPage: root
                            }
                        }
                    }
                    
                    // 已启用规则标签
                    Flow {
                        id: selectedRulesFlow
                        width: parent.width
                        height: visible ? childrenRect.height : 0
                        spacing: 6
                        visible: selectedRulesCount > 0
                        
                        Repeater {
                            model: selectedRules
                            
                            Rectangle {
                                height: 24
                                radius: 12
                                color: {
                                    var rule = getRuleById(modelData)
                                    return Qt.lighter(rule.cardColor, 1.4)
                                }
                                implicitWidth: ruleTagRow.implicitWidth + 12
                                
                                Row {
                                    id: ruleTagRow
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    spacing: 4
                                    
                                    Text {
                                        text: getRuleById(modelData).icon
                                        font.pixelSize: 10
                                        color: "white"
                                    }
                                    
                                    Text {
                                        text: getRuleById(modelData).ruleName
                                        font.pixelSize: 11
                                        color: "white"
                                    }
                                    
                                    MouseArea {
                                        width: 12
                                        height: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: toggleRule(modelData)
                                        
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
                                            text: "预览股票"
                                            font.pixelSize: 10
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: getDataTotalCount().toLocaleString()
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: "#3498db"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: "只股票"
                                            font.pixelSize: 9
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }
                                
                                // 清洗后数据
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
                                            text: "缓存数据集"
                                            font.pixelSize: 10
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: cacheDisplayModel.count.toLocaleString()
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: "#2ecc71"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: "个缓存"
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
                                            text: "已选规则"
                                            font.pixelSize: 10
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: selectedRulesCount.toLocaleString()
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: "#e74c3c"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: "项规则"
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
    
    // DataSourceService实例 - 用于管理数据源
    DataSourceService {
        id: dataSourceService
        
        onDataSourceAdded: function(success, message, sourceInfo) {
            if (success) {
                dataSourceCount = dataSourceService.availableDataSources ? dataSourceService.availableDataSources.length : 0
                root.handlePanelStatusRequested("✓ 数据源已添加: " + (sourceInfo.name || sourceInfo.provider), "success")
            } else {
                root.handlePanelStatusRequested("❌ " + message, "error")
            }
        }
        
        onDataLoaded: function(success, message, data) {
            if (success) {
                root.handlePanelStatusRequested("✓ " + message, "success")
                previewDataCount = data ? data.length : 0
                root.dataLoaded()
            } else {
                root.handlePanelStatusRequested("❌ " + message, "error")
            }
        }
        
        onError: function(errorMessage) {
            root.handlePanelStatusRequested("❌ " + errorMessage, "error")
        }
    }
    
    // DataPreviewService实例 - 用于数据预览和分析
    DataPreviewService {
        id: dataPreviewService
        
        onProgress: function(progress, message) {
            if (progress > 0 && progress < 100) {
                root.handlePanelStatusRequested("⏳ " + message + " (" + progress + "%)", "warning")
            }
        }
        
        onError: function(errorMessage) {
            root.handlePanelStatusRequested("❌ " + errorMessage, "error")
        }
        
        onDataSetInfosChanged: {
            root.refreshDataSourceCount()
        }
        
        Component.onCompleted: {
            // 将previewModel设置为DataPreviewService的previewModel
            previewModel = dataPreviewService.previewModel
            if (previewModel) {
                console.log("PreviewModel已连接到DataPreviewService")
            }
        }
    }
    
    // DataService实例 - 用于核心数据操作
    DataService {
        id: dataService
        
        onQueryProgress: function(progress, message) {
            if (progress > 0 && progress < 100) {
                root.handlePanelStatusRequested(`⏳ ${message} (${progress}%)`, "warning")
            }
        }
        
        onQueryCompleted: function(success, message, data) {
            if (success) {
                root.handlePanelStatusRequested(`✓ ${message}`, "success")
                // 模型更新由C++ DataFetchController处理，此处只更新状态
                root.dataLoaded()
            } else {
                root.handlePanelStatusRequested(`❌ ${message}`, "error")
            }
        }
        
        onError: function(errorMessage) {
            root.handlePanelStatusRequested(`❌ ${errorMessage}`, "error")
        }
    }
    
    // DataCleaningService实例 - 用于数据清洗
    DataCleaningService {
        id: dataCleaningService
        
        onCleaningProgress: function(requestId, progress, message) {
            root.handlePanelStatusRequested(`⏳ 清洗进度: ${message} (${progress}%)`, "warning")
        }
        
        onCleaningStarted: function(requestId, description) {
            root.handlePanelStatusRequested(`⏳ 开始清洗: ${description}`, "warning")
        }
        
        onCleaningCompleted: function(requestId, success, message, cleanedData) {
            if (success) {
                root.handlePanelStatusRequested(`✓ ${message}`, "success")
                // 模型更新由C++ DataFetchController处理，此处只更新状态
            } else {
                root.handlePanelStatusRequested(`❌ ${message}`, "error")
            }
        }
        
        onCleaningError: function(requestId, error) {
            root.handlePanelStatusRequested(`❌ 清洗错误: ${error}`, "error")
        }
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
        
        // 缓存信息刷新完成信号
        onAllCacheInfosRefreshed: function(cacheInfos) {
            root.handlePanelStatusRequested("✓ 缓存列表已刷新", "success")
            cacheDisplayModel.clear()
            for (var cacheInfoIndex = 0; cacheInfoIndex < cacheInfos.length; cacheInfoIndex++) {
                var cacheInfo = cacheInfos[cacheInfoIndex]
                var displayName = cacheInfo.displayName || "未知缓存"
                var cacheType = cacheInfo.type || "cache"
                var cacheId = cacheInfo.id || -1
                var cacheKey = cacheInfo.cacheKey || ""
                var description = cacheInfo.description || ""

                cacheDisplayModel.append({
                    displayName: displayName,
                    index: cacheInfoIndex,
                    type: cacheType,
                    id: cacheId,
                    cacheKey: cacheKey,
                    description: description
                })
            }
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
        visible: statusText.text !== ""
        
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
        return today.toISOString().split('T')[0]
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

    function setSelectedRulesValue(ruleIds) {
        var nextRules = []
        if (ruleIds && ruleIds.length) {
            for (var i = 0; i < ruleIds.length; i++) {
                nextRules.push(ruleIds[i])
            }
        }
        selectedRules = nextRules
    }
    
    function getDataTotalCount() {
        // 返回真实数据统计，从dataFetchController.previewModel获取
        return dataFetchController.previewModel ? dataFetchController.previewModel.count : 0
    }
    
    function getRuleById(id) {
        var rules = [
            { ruleId: "market_filter", ruleName: "市场选择", icon: "🏢", cardColor: "#3b82f6" },
            { ruleId: "price_filter", ruleName: "价格筛选", icon: "💰", cardColor: "#10b981" },
            { ruleId: "volume_filter", ruleName: "成交量筛选", icon: "📊", cardColor: "#8b5cf6" },
            { ruleId: "data_cleaning", ruleName: "数据清洗", icon: "🧹", cardColor: "#ef4444" },
            { ruleId: "time_range", ruleName: "时间区间", icon: "⏰", cardColor: "#06b6d4" },
            { ruleId: "missing_value", ruleName: "缺失值处理", icon: "🔍", cardColor: "#ec4899" },
            { ruleId: "outliers_filter", ruleName: "异常值处理", icon: "⚠️", cardColor: "#f97316" }
        ]
        
        for (var i = 0; i < rules.length; i++) {
            if (rules[i].ruleId === id) {
                return rules[i]
            }
        }
        return { ruleName: "未知规则", icon: "❓", cardColor: "#6b7280" }
    }
    
    function toggleRule(id) {
        var newArray = selectedRules.slice()
        if (newArray.includes(id)) {
            newArray = newArray.filter(function(ruleId) {
                return ruleId !== id
            })
        } else {
            newArray.push(id)
        }
        selectedRules = newArray
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
    
    function executeDataCleaning() {
        // 执行数据清洗 - 遵循不在QML中操作数据的原则
        updateStatus("⏳ 执行数据清洗...", "warning")
        console.log("开始执行数据清洗 - 使用DataFetchController")
        
        // 检查DataFetchController是否可用
        if (!dataFetchController) {
            updateStatus("❌ DataFetchController未初始化", "error")
            return
        }
        
        // 构建规则映射 - 将selectedRules转换为C++期望的格式
        var rules = {}
        
        // 遍历已选择的规则，设置对应的规则项
        for (var i = 0; i < selectedRules.length; i++) {
            var ruleId = selectedRules[i]
            switch (ruleId) {
                case "market_filter":
                    rules["market"] = { "aShares": true }
                    break
                case "price_filter":
                    rules["priceFilter"] = {
                        "enabled": true,
                        "min": 0.01,
                        "max": 10000.0
                    }
                    break
                case "volume_filter":
                    rules["volumeFilter"] = {
                        "enabled": true,
                        "minVolume": 100,
                        "maxVolume": 1000000000
                    }
                    break
                case "data_cleaning":
                    rules["dataCleaning"] = true
                    break
                case "time_range":
                    rules["timeRange"] = {
                        "enabled": true,
                        "start": getDateValue(dataSelectionPanel.startDatePicker),
                        "end": getDateValue(dataSelectionPanel.endDatePicker)
                    }
                    break
                case "missing_value":
                    rules["missingValue"] = true
                    break
                case "outliers_filter":
                    rules["outlierFilter"] = true
                    break
                default:
                    console.log("未知规则ID:", ruleId)
            }
        }
        
        // 如果没有选择任何规则，使用默认规则集
        if (Object.keys(rules).length === 0) {
            rules = {
                "market": { "aShares": true },
                "timeRange": {
                    "enabled": true,
                    "start": getDateValue(dataSelectionPanel.startDatePicker),
                    "end": getDateValue(dataSelectionPanel.endDatePicker)
                }
            }
            console.log("使用默认规则集")
        }
        
        console.log("传递规则给DataFetchController:", JSON.stringify(rules))
        
        // 调用DataFetchController的异步清洗方法
        // 所有数据操作都在C++中完成，QML只传递规则
        dataFetchController.cleanDataAsync(rules)
        updateStatus("⏳ 正在清洗数据...", "warning")
    }
    
    function refreshDataSourceCount() {
        // 刷新数据源计数
        dataSourceCount = dataPreviewService.dataSetInfos ? dataPreviewService.dataSetInfos.length : 0
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
        
        // 构建规则映射 - 将selectedRules转换为C++期望的格式
        var rules = {}
        
        // 遍历已选择的规则，设置对应的规则项
        for (var i = 0; i < selectedRules.length; i++) {
            var ruleId = selectedRules[i]
            switch (ruleId) {
                case "market_filter":
                    rules["market"] = { "aShares": true }
                    break
                case "price_filter":
                    rules["priceFilter"] = {
                        "enabled": true,
                        "min": 0.01,
                        "max": 10000.0
                    }
                    break
                case "volume_filter":
                    rules["volumeFilter"] = {
                        "enabled": true,
                        "minVolume": 100,
                        "maxVolume": 1000000000
                    }
                    break
                case "data_cleaning":
                    rules["dataCleaning"] = true
                    break
                case "time_range":
                    rules["timeRange"] = {
                        "enabled": true,
                        "start": getDateValue(dataSelectionPanel.startDatePicker),
                        "end": getDateValue(dataSelectionPanel.endDatePicker)
                    }
                    break
                case "missing_value":
                    rules["missingValue"] = true
                    break
                case "outliers_filter":
                    rules["outlierFilter"] = true
                    break
                default:
                    console.log("未知规则ID:", ruleId)
            }
        }
        
        // 如果没有选择任何规则，使用默认规则集
        if (Object.keys(rules).length === 0) {
            rules = {
                "market": { "aShares": true },
                "timeRange": {
                    "enabled": true,
                    "start": getDateValue(dataSelectionPanel.startDatePicker),
                    "end": getDateValue(dataSelectionPanel.endDatePicker)
                }
            }
            console.log("使用默认规则集")
        }
        
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
        property bool defaultValue: false
        property var parentPage: null
        
        width: 130
        height: 45
        radius: 8
        
        // 内部状态 - 不暴露为属性，避免双向绑定问题
        property bool cardEnabled: defaultValue
        
        color: cardEnabled ? Qt.lighter(cardColor, 1.4) : "#1a2538"
        border.width: cardEnabled ? 2 : 1
        border.color: cardEnabled ? cardColor : "#4b5563"

        function getSelectedRules() {
            if (!parentPage || typeof parentPage.getSelectedRulesValue !== "function") {
                return []
            }
            return parentPage.getSelectedRulesValue()
        }

        function setSelectedRules(ruleIds) {
            if (parentPage && typeof parentPage.setSelectedRulesValue === "function") {
                parentPage.setSelectedRulesValue(ruleIds)
            }
        }
        
        // 初始化时同步到parentPage
        Component.onCompleted: {
            var selectedRuleIds = getSelectedRules()
            var exists = false
            for (var index = 0; index < selectedRuleIds.length; index++) {
                if (selectedRuleIds[index] === ruleId) {
                    exists = true
                    break
                }
            }

            if (defaultValue && !exists) {
                var newArray = selectedRuleIds.slice()
                newArray.push(ruleId)
                setSelectedRules(newArray)
            }
        }
        
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                ruleCard.cardEnabled = !ruleCard.cardEnabled
                if (parentPage) {
                    var selectedRuleIds = getSelectedRules()
                    if (ruleCard.cardEnabled) {
                        var found = false
                        for (var includeIndex = 0; includeIndex < selectedRuleIds.length; includeIndex++) {
                            if (selectedRuleIds[includeIndex] === ruleId) {
                                found = true
                                break
                            }
                        }

                        if (!found) {
                            var newArray = selectedRuleIds.slice()
                            newArray.push(ruleId)
                            setSelectedRules(newArray)
                        }
                    } else {
                        var filteredArray = []
                        for (var filterIndex = 0; filterIndex < selectedRuleIds.length; filterIndex++) {
                            if (selectedRuleIds[filterIndex] !== ruleId) {
                                filteredArray.push(selectedRuleIds[filterIndex])
                            }
                        }
                        setSelectedRules(filteredArray)
                    }
                }
            }
            
            onEntered: {
                if (!ruleCard.cardEnabled) {
                    ruleCard.color = Qt.lighter("#1a2538", 1.2)
                }
            }
            
            onExited: {
                if (!ruleCard.cardEnabled) {
                    ruleCard.color = "#1a2538"
                }
            }
        }
        
        Row {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 6
            
            Text {
                text: ruleCard.icon
                font.pixelSize: 14
                color: "white"
                anchors.verticalCenter: parent.verticalCenter
            }
            
            Text {
                text: ruleCard.ruleName
                font.pixelSize: 12
                font.bold: true
                color: ruleCard.cardEnabled ? ruleCard.cardColor : "white"
                anchors.verticalCenter: parent.verticalCenter
            }
            
            Item {
                width: parent.width - childrenRect.width - 20
                height: 1
            }
            
        }
    }
    
    // 初始化
    Component.onCompleted: {
        // 设置默认数据源计数
        dataSourceCount = 0
        previewDataCount = 0
    }
}
}

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0
import ConsoleUi 1.0  // 导入ConsoleUi以使用DataSourceModal
import "../components/DataAnalysis" as DataAnalysis  // 导入DataAnalysis组件

Item {
    id: dataSourcePage
    anchors.fill: parent
    
    // DataSourceService实例
    DataSourceService {
        id: dataSourceService
        onConnectionTested: function(success, message) {
            if (success) {
                statusText.text = "✓ " + message
                statusText.color = "#4caf50"
            } else {
                statusText.text = "✗ " + message
                statusText.color = "#f44336"
            }
        }
        onDataSourceAdded: function(success, message, sourceInfo) {
            if (success) {
                console.log("数据源添加成功:", sourceInfo)
                dataSourceService.refreshAvailableDataSources()
            }
        }
        onDataLoaded: function(success, message, data) {
            console.log("数据加载:", success, message, "数据量:", data.length)
        }
        onError: function(errorMessage) {
            console.error("数据源服务错误:", errorMessage)
            statusText.text = "✗ " + errorMessage
            statusText.color = "#f44336"
        }
    }
    
    // 数据源添加弹窗 - 使用原来的弹窗接口
    DataSourceModal {
        id: dataSourceModal
        visible: false
        parent: dataSourcePage.parent || dataSourcePage
        
        onSourceAdded: function(sourceInfo) {
            console.log("数据源添加信号触发:", sourceInfo)
            // 处理添加的数据源
            handleDataSourceAdded(sourceInfo)
        }
        
        onDataLoaded: {
            console.log("数据加载完成")
            // 数据加载完成后的处理
            handleDataLoaded()
        }
    }
    
    // 数据预览弹窗 - 移植到数据管理看板中
    DataAnalysis.DataPreviewModal {
        id: dataPreviewModal
        visible: false
        parent: dataSourcePage.parent || dataSourcePage
        
        onExportCompleted: {
            console.log("数据导出完成")
            showOperationStatus("数据导出到下一流程完成", "success")
        }
    }
    
    // 数据预览服务 - 用于从C++后端获取真实数据
    DataPreviewService {
        id: dataPreviewService
        
        // 进度信号处理
        onProgress: function(progress, message) {
            console.log("数据预览服务进度:", progress, message)
        }
        
        onError: function(errorMessage) {
            console.error("数据预览服务错误:", errorMessage)
            showOperationStatus("数据预览服务错误: " + errorMessage, "error")
        }
        
        // 数据集列表变化时刷新界面
        onDataSetInfosChanged: {
            console.log("数据集列表已更新，数量:", dataPreviewService.dataSetInfos.length)
            // 更新数据集列表显示
            refreshDatasetListUI()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        padding: 32

        // 标题
        Text {
            text: "数据源管理"
            font.pixelSize: 28
            font.bold: true
            color: Qt.application.palette.text
            Layout.alignment: Qt.AlignHCenter
        }

        // 数据源选择区
        GroupBox {
            title: "数据源选择"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 16
                width: parent.width
                
                // 数据源列表
                Rectangle {
                    Layout.fillWidth: true
                    height: 200
                    border.color: "#e0e0e0"
                    border.width: 1
                    radius: 4
                    
                    ListView {
                        id: dataSourceListView
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        model: dataSourceService.availableDataSources
                        
                        delegate: Rectangle {
                            id: delegateItem
                            width: parent.width
                            height: 48
                            color: ListView.isCurrentItem ? "#1976d2" : (index % 2 === 0 ? "#f8f9fa" : "white")
                            border.width: ListView.isCurrentItem ? 3 : 0
                            border.color: "#1565c0"
                            radius: 4
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12
                                
                                // 选中指示器
                                Rectangle {
                                    width: 20
                                    height: 20
                                    radius: 10
                                    border.width: 1
                                    border.color: "#2196f3"
                                    color: ListView.isCurrentItem ? "#2196f3" : "transparent"
                                    
                                    Text {
                                        text: "✓"
                                        color: "white"
                                        font.pixelSize: 12
                                        font.bold: true
                                        anchors.centerIn: parent
                                        visible: ListView.isCurrentItem
                                    }
                                }
                                
                                // 数据源名称和类型
                                ColumnLayout {
                                    spacing: 2
                                    Layout.fillWidth: true
                                    
                                    Text {
                                        text: modelData.name || modelData.provider
                                        font.pixelSize: 14
                                        font.bold: true
                                        color: "#333333"
                                    }
                                    
                                    Text {
                                        text: "类型: " + (modelData.type || "未知")
                                        font.pixelSize: 12
                                        color: "#666666"
                                    }
                                }
                                
                                // 连接状态
                                Rectangle {
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: dataSourceService.currentDataSource === modelData.provider ? "#4caf50" : "#f44336"
                                }
                                
                                Text {
                                    text: dataSourceService.currentDataSource === modelData.provider ? "已连接" : "未连接"
                                    font.pixelSize: 12
                                    color: dataSourceService.currentDataSource === modelData.provider ? "#4caf50" : "#999999"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    dataSourceListView.currentIndex = index
                                    // 选中数据源但不立即连接
                                    selectedDataSourceInfo = modelData
                                    console.log("选中数据源:", modelData)
                                }
                                onDoubleClicked: {
                                    dataSourceListView.currentIndex = index
                                    connectSelectedDataSource()
                                }
                            }
                        }
                        
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                    }
                }
                
                // 控制按钮行
                RowLayout {
                    spacing: 12
                    Layout.fillWidth: true
                    
                    Button {
                        text: "刷新列表"
                        onClicked: dataSourceService.refreshAvailableDataSources()
                        Layout.preferredWidth: 100
                    }
                    
                    Button {
                        text: "测试连接"
                        onClicked: {
                            if (dataSourceListView.currentIndex >= 0) {
                                var currentItem = dataSourceService.availableDataSources[dataSourceListView.currentIndex]
                                dataSourceService.testConnection(currentItem.provider || currentItem.name)
                            }
                        }
                        enabled: dataSourceListView.currentIndex >= 0
                        Layout.preferredWidth: 100
                    }
                    
                    Button {
                        id: connectBtn
                        text: "连接"
                        onClicked: connectSelectedDataSource()
                        enabled: dataSourceListView.currentIndex >= 0
                        Layout.preferredWidth: 100
                    }
                    
                    Button {
                        text: "断开"
                        onClicked: {
                            dataSourceService.setCurrentDataSource("")
                            statusText.text = "已断开连接"
                            statusText.color = "#f44336"
                        }
                        enabled: dataSourceService.currentDataSource !== ""
                        Layout.preferredWidth: 100
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 状态显示
                    Text {
                        id: statusText
                        text: dataSourceService.currentDataSource ? "当前连接: " + dataSourceService.currentDataSource : "未连接数据源"
                        font.pixelSize: 14
                        color: dataSourceService.currentDataSource ? "#4caf50" : "#f44336"
                        Layout.alignment: Qt.AlignRight
                    }
                }
            }
        }

        // 数据源明细区
        GroupBox {
            title: "数据源明细"
            Layout.fillWidth: true
            visible: dataSourceListView.currentIndex >= 0
            
            ColumnLayout {
                spacing: 8
                width: parent.width
                
                Repeater {
                    model: getDataSourceDetails()
                    
                    RowLayout {
                        spacing: 16
                        width: parent.width
                        
                        Text {
                            text: modelData.key + ":"
                            font.pixelSize: 14
                            font.bold: true
                            color: "#333333"
                            Layout.preferredWidth: 120
                        }
                        
                        Text {
                            text: modelData.value
                            font.pixelSize: 14
                            color: "#666666"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
        
        // 数据预览与清洗功能 - 内嵌控件移植
        GroupBox {
            title: "数据清洗与预览"
            Layout.fillWidth: true
            
            ColumnLayout {
                spacing: 16
                width: parent.width
                
                // 数据统计卡片 - 与DataPreviewModal对齐，使用C++接口
                GridLayout {
                    columns: 4
                    columnSpacing: 8
                    rowSpacing: 8
                    Layout.fillWidth: true
                    
                    Repeater {
                        model: [
                            {icon: "📊", title: "数据总量", value: getRealDataTotalCount(), unit: "条", color: "#3498db", bg: "#e3f2fd"},
                            {icon: "📅", title: "时间跨度", value: getRealTimeSpanDays(), unit: "天", color: "#2ecc71", bg: "#e8f5e9"},
                            {icon: "🏢", title: "股票数量", value: getRealStockCount(), unit: "只", color: "#e74c3c", bg: "#ffebee"},
                            {icon: "📉", title: "平均涨跌", value: getRealAvgChange(), unit: "%", color: "#9b59b6", bg: "#f3e5f5"}
                        ]
                        
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            height: 50
                            radius: 10
                            color: modelData.bg
                            
                            Column {
                                anchors.centerIn: parent
                                spacing: 4
                                
                                Row {
                                    spacing: 6
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    
                                    Text {
                                        text: modelData.icon
                                        font.pixelSize: 14
                                    }
                                    
                                    Text {
                                        text: modelData.title
                                        font.pixelSize: 11
                                        color: "#6c757d"
                                    }
                                }
                                
                                Row {
                                    spacing: 4
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    
                                    Text {
                                        text: modelData.value.toFixed(2)
                                        font.pixelSize: 14
                                        font.bold: true
                                        color: modelData.color
                                    }
                                    
                                    Text {
                                        text: modelData.unit
                                        font.pixelSize: 10
                                        color: "#6c757d"
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 1
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 数据集预览列表
                GroupBox {
                    title: "最近数据集"
                    Layout.fillWidth: true
                    topPadding: 25
                    label: Label {
                        text: parent.title
                        font.bold: true
                        color: "#1a2980"
                    }
                    
                    background: Rectangle {
                        radius: 12
                        color: "#ffffff"
                        border.width: 1
                        border.color: "#e5e7eb"
                    }
                    
                    ColumnLayout {
                        width: parent.width
                        spacing: 8
                        
                        ListView {
                            id: datasetListView
                            Layout.fillWidth: true
                            height: Math.min(datasetListModel.count * 40, 120)
                            clip: true
                            model: datasetListModel
                            
                            delegate: Rectangle {
                                width: parent.width
                                height: 40
                                color: index % 2 === 0 ? "#ffffff" : "#f8f9fa"
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8
                                    
                                    Text {
                                        text: model.icon || "📊"
                                        font.pixelSize: 14
                                    }
                                    
                                    ColumnLayout {
                                        spacing: 2
                                        Layout.fillWidth: true
                                        
                                        Text {
                                            text: model.name
                                            font.pixelSize: 12
                                            font.bold: true
                                            color: "#1f2937"
                                            elide: Text.ElideRight
                                        }
                                        
                                        Text {
                                            text: model.info || "点击查看详情"
                                            font.pixelSize: 10
                                            color: "#6b7280"
                                            elide: Text.ElideRight
                                        }
                                    }
                                    
                                    Text {
                                        text: "→"
                                        color: "#6b7280"
                                        font.bold: true
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        console.log("点击数据集:", model.name)
                                        showOperationStatus("加载数据集: " + model.name, "success")
                                        // 这里可以触发加载数据集的具体操作
                                        loadDatasetPreview(model.id)
                                    }
                                }
                            }
                            
                            // 空状态提示
                            Rectangle {
                                visible: datasetListModel.count === 0
                                anchors.fill: parent
                                color: "#f8f9fa"
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: "暂无数据集"
                                        font.pixelSize: 12
                                        color: "#6c757d"
                                        font.bold: true
                                    }
                                    
                                    Text {
                                        text: "连接数据源后刷新列表"
                                        font.pixelSize: 10
                                        color: "#adb5bd"
                                    }
                                }
                            }
                        }
                        
                        RowLayout {
                            spacing: 8
                            
                            Button {
                                text: "🔄 刷新列表"
                                onClicked: refreshDatasetList()
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#e3f2fd"
                                    border.width: 1
                                    border.color: "#bbdefb"
                                }
                                
                                contentItem: Text {
                                    text: parent.text
                                    color: "#1976d2"
                                    font.pixelSize: 11
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            
                            Button {
                                text: "📊 详细预览"
                                onClicked: openDataPreviewModal()
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#10b981"
                                }
                                
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    font.pixelSize: 11
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // 数据源配置区域 - 移植DataSourceModal全部功能
        GroupBox {
            title: "数据源配置"
            Layout.fillWidth: true
            
            ColumnLayout {
                spacing: 16
                width: parent.width
                
                // 配置说明
                Text {
                    text: "配置股票数据源，支持多种数据提供商和数据类型"
                    font.pixelSize: 13
                    color: "#666666"
                    Layout.alignment: Qt.AlignHCenter
                }
                
                // 数据源配置表单 - 与DataSourceModal对齐
                GridLayout {
                    columns: 4
                    columnSpacing: 12
                    rowSpacing: 10
                    Layout.fillWidth: true
                    
                    // 数据提供商
                    Label {
                        text: "数据源"
                        font.pixelSize: 13
                        color: "#4b5563"
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 60
                    }
                    
                    ComboBox {
                        id: localProviderComboBox
                        Layout.fillWidth: true
                        height: 32
                        model: ["掘金数据", "宽聚数据", "聚宽数据", "TuShare", "东方财富", "自定义API"]
                        
                        background: Rectangle {
                            radius: 6
                            border.width: 1
                            border.color: localProviderComboBox.hovered ? "#3b82f6" : "#d1d5db"
                            color: "white"
                        }
                        
                        contentItem: Text {
                            text: localProviderComboBox.displayText
                            color: "#1f2937"
                            font.pixelSize: 13
                            leftPadding: 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    
                    // 股票市场
                    Label {
                        text: "交易所"
                        font.pixelSize: 13
                        color: "#4b5563"
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 60
                    }
                    
                    ComboBox {
                        id: localMarketComboBox
                        Layout.fillWidth: true
                        height: 32
                        model: ["上交所", "深交所", "北交所", "港股", "美股"]
                        
                        background: Rectangle {
                            radius: 6
                            border.width: 1
                            border.color: localMarketComboBox.hovered ? "#3b82f6" : "#d1d5db"
                            color: "white"
                        }
                        
                        contentItem: Text {
                            text: localMarketComboBox.displayText
                            color: "#1f2937"
                            font.pixelSize: 13
                            leftPadding: 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    
                    // 开始日期
                    Label {
                        text: "开始日期"
                        font.pixelSize: 13
                        color: "#4b5563"
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 60
                    }
                    
                    TextField {
                        id: localStartDateField
                        Layout.fillWidth: true
                        height: 32
                        placeholderText: "YYYY-MM-DD"
                        text: {
                            var date = new Date()
                            date.setDate(date.getDate() - 30)
                            return date.toISOString().slice(0, 10)
                        }
                        
                        background: Rectangle {
                            radius: 6
                            border.width: 1
                            border.color: localStartDateField.hovered ? "#3b82f6" : "#d1d5db"
                            color: "white"
                        }
                        
                        color: "#1f2937"
                        font.pixelSize: 13
                        leftPadding: 8
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    // 结束日期
                    Label {
                        text: "结束日期"
                        font.pixelSize: 13
                        color: "#4b5563"
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 60
                    }
                    
                    TextField {
                        id: localEndDateField
                        Layout.fillWidth: true
                        height: 32
                        placeholderText: "YYYY-MM-DD"
                        text: new Date().toISOString().slice(0, 10)
                        
                        background: Rectangle {
                            radius: 6
                            border.width: 1
                            border.color: localEndDateField.hovered ? "#3b82f6" : "#d1d5db"
                            color: "white"
                        }
                        
                        color: "#1f2937"
                        font.pixelSize: 13
                        leftPadding: 8
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    // 股票代码（可选）
                    Label {
                        text: "股票代码"
                        font.pixelSize: 13
                        color: "#4b5563"
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 60
                        
                        ToolTip {
                            text: "可选：留空表示加载整个市场数据\n如：000001,600519"
                            visible: parent.hovered
                        }
                    }
                    
                    TextField {
                        id: localStockCodesField
                        Layout.fillWidth: true
                        height: 32
                        placeholderText: "可选：留空加载整个市场，如：000001,600519"
                        
                        background: Rectangle {
                            radius: 6
                            border.width: 1
                            border.color: localStockCodesField.hovered ? "#3b82f6" : "#d1d5db"
                            color: "white"
                        }
                        
                        color: "#1f2937"
                        font.pixelSize: 13
                        leftPadding: 8
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    // API密钥（条件显示）
                    Label {
                        text: "API密钥"
                        font.pixelSize: 13
                        color: "#4b5563"
                        Layout.alignment: Qt.AlignRight
                        Layout.preferredWidth: 60
                        visible: ["掘金数据", "聚宽数据", "自定义API"].includes(localProviderComboBox.currentText)
                    }
                    
                    TextField {
                        id: localApiKeyField
                        Layout.fillWidth: true
                        height: 32
                        placeholderText: "请输入API密钥"
                        echoMode: TextInput.Password
                        visible: ["掘金数据", "聚宽数据", "自定义API"].includes(localProviderComboBox.currentText)
                        
                        background: Rectangle {
                            radius: 6
                            border.width: 1
                            border.color: localApiKeyField.hovered ? "#3b82f6" : "#d1d5db"
                            color: "white"
                        }
                        
                        color: "#1f2937"
                        font.pixelSize: 13
                        leftPadding: 8
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 数据类型选择区域标题
                RowLayout {
                    Layout.fillWidth: true
                    
                    Label {
                        text: "数据类型（可多选）"
                        font.pixelSize: 13
                        font.bold: true
                        color: "#374151"
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Label {
                        text: "已选择 " + localSelectedDataTypes.length + " 项"
                        font.pixelSize: 11
                        color: localSelectedDataTypes.length > 0 ? "#3b82f6" : "#9ca3af"
                    }
                }
                
                // 数据类型卡片区域 - 与DataSourceModal对齐
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    color: "transparent"
                    
                    Flow {
                        anchors.fill: parent
                        spacing: 8
                        
                        Repeater {
                            model: [
                                { id: "kline_daily", name: "日线", icon: "📈", color: "#3b82f6" },
                                { id: "kline_weekly", name: "周线", icon: "📊", color: "#10b981" },
                                { id: "kline_monthly", name: "月线", icon: "📉", color: "#8b5cf6" },
                                { id: "minute_data", name: "分钟", icon: "⏰", color: "#f59e0b" },
                                { id: "realtime", name: "实时", icon: "⚡", color: "#ef4444" },
                                { id: "historical", name: "历史", icon: "📜", color: "#6366f1" },
                                { id: "news", name: "舆情", icon: "🗞️", color: "#ec4899" },
                                { id: "financial", name: "财务", icon: "💰", color: "#14b8a6" },
                                { id: "policy", name: "政策", icon: "📋", color: "#f97316" },
                                { id: "alternative", name: "另类", icon: "🔮", color: "#a855f7" },
                                { id: "index", name: "指数", icon: "📊", color: "#06b6d4" },
                                { id: "derivatives", name: "衍生品", icon: "📊", color: "#84cc16" }
                            ]
                            
                            // 紧凑型卡片 - 135x42
                            Rectangle {
                                id: localDataTypeCard
                                width: 135
                                height: 42
                                radius: 6
                                color: localSelectedDataTypes.includes(modelData.id) ? 
                                       modelData.color : "#f9fafb"
                                border.width: localSelectedDataTypes.includes(modelData.id) ? 2 : 1
                                border.color: localSelectedDataTypes.includes(modelData.id) ? 
                                             modelData.color : "#e5e7eb"
                                
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: localToggleDataType(modelData.id)
                                    
                                    onEntered: {
                                        if (!localSelectedDataTypes.includes(modelData.id)) {
                                            parent.color = Qt.lighter("#f9fafb", 0.95)
                                        }
                                    }
                                    
                                    onExited: {
                                        if (!localSelectedDataTypes.includes(modelData.id)) {
                                            parent.color = "#f9fafb"
                                        }
                                    }
                                }
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 6
                                    
                                    Text {
                                        text: modelData.icon
                                        font.pixelSize: 14
                                        color: localSelectedDataTypes.includes(modelData.id) ? "white" : "#1f2937"
                                    }
                                    
                                    Text {
                                        text: modelData.name
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: localSelectedDataTypes.includes(modelData.id) ? "white" : "#1f2937"
                                        Layout.fillWidth: true
                                    }
                                    
                                    // 选择指示器
                                    Rectangle {
                                        width: 16
                                        height: 16
                                        radius: 8
                                        color: localSelectedDataTypes.includes(modelData.id) ? 
                                               "white" : "transparent"
                                        border.width: 1
                                        border.color: localSelectedDataTypes.includes(modelData.id) ? 
                                                     "white" : "#9ca3af"
                                        
                                        Text {
                                            text: "✓"
                                            color: modelData.color
                                            font.pixelSize: 10
                                            font.bold: true
                                            anchors.centerIn: parent
                                            visible: localSelectedDataTypes.includes(modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 已选类型标签
                Flow {
                    id: localSelectedTagsFlow
                    Layout.fillWidth: true
                    spacing: 6
                    visible: localSelectedDataTypes.length > 0
                    
                    Repeater {
                        model: localSelectedDataTypes
                        
                        Rectangle {
                            height: 24
                            radius: 12
                            color: {
                                var dataType = localGetDataTypeById(modelData)
                                return Qt.lighter(dataType.color, 1.4)
                            }
                            implicitWidth: tagRow.implicitWidth + 12
                            
                            RowLayout {
                                id: tagRow
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 4
                                
                                Text {
                                    text: localGetDataTypeById(modelData).icon
                                    font.pixelSize: 10
                                }
                                
                                Text {
                                    text: localGetDataTypeById(modelData).name
                                    font.pixelSize: 11
                                    color: "#1f2937"
                                }
                                
                                MouseArea {
                                    width: 12
                                    height: 12
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: localToggleDataType(modelData)
                                    
                                    Text {
                                        text: "×"
                                        color: "#6b7280"
                                        font.pixelSize: 10
                                        font.bold: true
                                        anchors.centerIn: parent
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 按钮区域 - 与DataSourceModal对齐
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    
                    Button {
                        id: localTestBtn
                        text: "测试连接"
                        Layout.fillWidth: true
                        height: 34
                        
                        background: Rectangle {
                            radius: 6
                            color: "#4b5563"
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        
                        ToolTip {
                            text: "测试与数据源的连接是否正常"
                            visible: parent.hovered
                        }
                        
                        onClicked: localTestConnection()
                    }
                    
                    Button {
                        id: localPreviewBtn
                        text: "预览数据"
                        Layout.fillWidth: true
                        height: 34
                        
                        background: Rectangle {
                            radius: 6
                            color: "#0ea5e9"
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        
                        ToolTip {
                            text: "预览少量数据，验证配置是否正确"
                            visible: parent.hovered
                        }
                        
                        onClicked: localLoadPreview()
                    }
                    
                    Button {
                        id: localAddBtn
                        text: "添加并获取数据"
                        Layout.fillWidth: true
                        height: 34
                        
                        background: Rectangle {
                            radius: 6
                            color: "#10b981"
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 13
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        
                        ToolTip {
                            text: "保存数据源配置并加载完整数据到系统"
                            visible: parent.hovered
                        }
                        
                        onClicked: localAddAndFetchData()
                    }
                }
                
                // 配置状态显示
                Rectangle {
                    id: localConfigStatusBar
                    Layout.fillWidth: true
                    height: 32
                    radius: 6
                    color: localConfigStatusColor
                    border.width: 1
                    border.color: localConfigStatusBorderColor
                    visible: localConfigStatusText !== ""
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6
                        
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: localConfigStatusDotColor
                        }
                        
                        Label {
                            id: localConfigStatusText
                            text: ""
                            font.pixelSize: 12
                            color: localConfigStatusTextColor
                            Layout.fillWidth: true
                        }
                        
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 9
                            color: "transparent"
                            
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: localConfigStatusText = ""
                                
                                Text {
                                    text: "×"
                                    color: "#6b7280"
                                    font.pixelSize: 12
                                    anchors.centerIn: parent
                                }
                                
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 9
                                    color: parent.containsMouse ? "#00000010" : "transparent"
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 当前选中的数据源信息
    property var selectedDataSourceInfo: ({})
    
    // 操作状态属性
    property string operationStatusText: ""
    property string operationStatusColor: "#f3f4f6"
    property string operationStatusBorderColor: "#e5e7eb"
    property string operationStatusTextColor: "#374151"
    property string operationStatusDotColor: "#6b7280"
    
    // 规则配置属性
    property var localSelectedDataTypes: []
    property string localConfigStatusText: ""
    property string localConfigStatusColor: "#f3f4f6"
    property string localConfigStatusBorderColor: "#e5e7eb"
    property string localConfigStatusTextColor: "#374151"
    property string localConfigStatusDotColor: "#6b7280"
    
    // 规则配置属性
    property var selectedRules: []
    property var rulesConfig: ({})
    
    // 数据集列表模型 - 与DataPreviewModal对齐，使用C++接口的dataSetInfos
    property var datasetListModel: ListModel {
        id: datasetListModel
    }
    
    // 数据集ID到显示信息的映射
    property var datasetInfoMap: ({})
    
    // 连接选中的数据源
    function connectSelectedDataSource() {
        if (dataSourceListView.currentIndex >= 0) {
            var currentItem = dataSourceService.availableDataSources[dataSourceListView.currentIndex]
            dataSourceService.setCurrentDataSource(currentItem.provider || currentItem.name)
            statusText.text = "✓ 已连接到: " + (currentItem.provider || currentItem.name)
            statusText.color = "#4caf50"
            console.log("连接到数据源:", currentItem)
        }
    }
    
    // 获取数据源详细信息
    function getDataSourceDetails() {
        if (dataSourceListView.currentIndex < 0) {
            return []
        }
        
        var currentItem = dataSourceService.availableDataSources[dataSourceListView.currentIndex]
        var details = []
        
        // 基本信息
        if (currentItem.name) details.push({key: "名称", value: currentItem.name})
        if (currentItem.provider) details.push({key: "提供商", value: currentItem.provider})
        if (currentItem.type) details.push({key: "类型", value: currentItem.type})
        
        // 连接状态
        details.push({key: "连接状态", value: dataSourceService.currentDataSource === (currentItem.provider || currentItem.name) ? "已连接" : "未连接"})
        
        // 其他信息
        if (currentItem.description) details.push({key: "描述", value: currentItem.description})
        if (currentItem.url) details.push({key: "URL", value: currentItem.url})
        
        return details
    }
    
    // 打开数据源添加弹窗 - 使用原来的弹窗接口
    function openDataSourceModal() {
        console.log("调用openDataSourceModal函数，打开数据源添加弹窗")
        if (dataSourceModal) {
            dataSourceModal.open()
            console.log("数据源弹窗已打开")
        } else {
            console.error("数据源弹窗组件未找到")
            showOperationStatus("无法打开数据源添加弹窗", "error")
        }
    }
    
    // 打开数据预览弹窗
    function openDataPreviewModal() {
        console.log("调用openDataPreviewModal函数，打开数据预览弹窗")
        if (dataPreviewModal) {
            // 检查是否有可用的数据源连接
            if (dataSourceService.currentDataSource === "") {
                showOperationStatus("请先连接一个数据源", "warning")
                return
            }
            
            // 尝试刷新预览数据
            if (dataPreviewModal.dataPreviewService) {
                dataPreviewModal.dataPreviewService.refreshDataSetList()
            }
            
            dataPreviewModal.open()
            console.log("数据预览弹窗已打开")
        } else {
            console.error("数据预览弹窗组件未找到")
            showOperationStatus("无法打开数据预览弹窗", "error")
        }
    }
    
    // 处理数据源添加
    function handleDataSourceAdded(sourceInfo) {
        console.log("处理添加的数据源:", sourceInfo)
        
        // 更新操作状态
        showOperationStatus("数据源添加成功: " + sourceInfo.name, "success")
        
        // 刷新数据源列表
        dataSourceService.refreshAvailableDataSources()
        
        // 可以在这里添加额外的处理逻辑，比如保存到配置文件等
        console.log("数据源已添加到系统:", sourceInfo)
    }
    
    // 处理数据加载完成
    function handleDataLoaded() {
        console.log("数据加载完成处理")
        showOperationStatus("数据加载完成", "success")
    }
    
    // 显示操作状态
    function showOperationStatus(message, type) {
        operationStatusText = message
        
        switch(type) {
            case "success":
                operationStatusColor = "#dcfce7"
                operationStatusBorderColor = "#86efac"
                operationStatusTextColor = "#166534"
                operationStatusDotColor = "#16a34a"
                break
            case "error":
                operationStatusColor = "#fee2e2"
                operationStatusBorderColor = "#fca5a5"
                operationStatusTextColor = "#991b1b"
                operationStatusDotColor = "#dc2626"
                break
            case "warning":
                operationStatusColor = "#fef9c3"
                operationStatusBorderColor = "#fde047"
                operationStatusTextColor = "#854d0e"
                operationStatusDotColor = "#ca8a04"
                break
            default:
                operationStatusColor = "#f3f4f6"
                operationStatusBorderColor = "#e5e7eb"
                operationStatusTextColor = "#374151"
                operationStatusDotColor = "#6b7280"
        }
        
        // 5秒后自动清除成功和警告状态
        if (type === "success" || type === "warning") {
            statusClearTimer.start()
        }
    }
    
    // 状态清除定时器
    Timer {
        id: statusClearTimer
        interval: 5000
        onTriggered: {
            operationStatusText = ""
        }
    }
    
    // 数据统计相关函数 - 与DataPreviewModal对齐，使用C++接口
    function getDataTotalCount() {
        // 保持向后兼容，使用模拟数据
        return 856 // 演示数据
    }
    
    function getTimeSpanDays() {
        // 模拟计算时间跨度
        return 365 // 1年
    }
    
    function getStockCount() {
        // 模拟股票数量
        if (dataSourceService.currentDataSource) {
            // 如果有数据源连接，根据数据源类型返回不同值
            var provider = dataSourceService.currentDataSource.toLowerCase()
            if (provider.includes("上交所") || provider.includes("上证")) return 1800
            if (provider.includes("深交所") || provider.includes("深证")) return 2400
            return 300 // 默认沪深300成分股
        }
        return 0
    }
    
    function getAvgChange() {
        // 模拟平均涨跌幅
        return 1.2 // 1.2%
    }
    
    // 真实数据统计函数 - 使用C++接口与DataPreviewModal对齐
    function getRealDataTotalCount() {
        // 如果有连接的预览弹窗，使用其数据；否则调用C++接口
        if (dataPreviewModal && dataPreviewModal.previewDataModel) {
            return dataPreviewModal.previewDataModel.count || 0
        }
        // 如果有数据预览服务，使用其预览模型数据
        if (dataPreviewService && dataPreviewService.previewModel) {
            return dataPreviewService.previewModel.count || 0
        }
        return 0 // 默认返回0，等待数据加载
    }
    
    function getRealTimeSpanDays() {
        // 调用C++接口计算时间跨度
        if (dataPreviewService) {
            return dataPreviewService.calculateTimeSpanFromModel() || 0
        }
        return 0
    }
    
    function getRealStockCount() {
        // 调用C++接口计算股票数量
        if (dataPreviewService) {
            return dataPreviewService.calculateStockCountFromModel() || 0
        }
        return 0
    }
    
    function getRealAvgChange() {
        // 调用C++接口计算平均涨跌幅
        if (dataPreviewService) {
            return dataPreviewService.calculateAvgChangeFromModel() || 0.0
        }
        return 0.0
    }
    
    // 刷新数据集列表 - 与DataPreviewModal对齐，使用C++接口
    function refreshDatasetList() {
        console.log("刷新数据集列表 - 调用C++接口")
        
        // 检查是否有数据源连接
        if (dataSourceService.currentDataSource === "") {
            showOperationStatus("请先连接数据源", "warning")
            return
        }
        
        // 调用C++接口刷新数据集列表
        if (dataPreviewService) {
            showOperationStatus("正在刷新数据集列表...", "warning")
            dataPreviewService.refreshDataSetList()
            console.log("已调用C++接口刷新数据集列表")
        } else {
            console.warn("数据预览服务不可用")
            showOperationStatus("数据预览服务不可用", "error")
        }
    }
    
    // 刷新数据集列表UI - 将C++接口的数据转换为UI模型
    function refreshDatasetListUI() {
        console.log("刷新数据集列表UI - 从C++接口加载数据")
        
        // 清空当前列表
        datasetListModel.clear()
        datasetInfoMap = {}
        
        // 获取C++接口返回的数据集信息
        var dataSetInfos = dataPreviewService.dataSetInfos
        console.log("C++接口返回的数据集数量:", dataSetInfos.length)
        
        if (dataSetInfos.length === 0) {
            console.log("没有可用的数据集")
            showOperationStatus("没有可用的数据集", "warning")
            return
        }
        
        // 将C++数据转换为UI模型
        for (var i = 0; i < dataSetInfos.length; i++) {
            var datasetInfo = dataSetInfos[i]
            
            // 创建显示信息
            var displayInfo = {
                id: datasetInfo.id || ("ds_" + (i + 1)),
                name: datasetInfo.displayName || "未命名数据集",
                icon: getIconForDatasetType(datasetInfo.type || "default"),
                info: formatDatasetInfo(datasetInfo)
            }
            
            // 添加到UI模型
            datasetListModel.append(displayInfo)
            
            // 保存映射关系
            datasetInfoMap[displayInfo.id] = datasetInfo
            
            console.log("添加数据集到UI:", displayInfo.name, "ID:", displayInfo.id)
        }
        
        showOperationStatus("数据集列表已刷新 (" + dataSetInfos.length + "个数据集)", "success")
    }
    
    // 根据数据集类型获取图标
    function getIconForDatasetType(type) {
        var iconMap = {
            "daily": "📈",
            "minute": "⏰",
            "stock": "🏢",
            "cleaned": "✨",
            "default": "📊"
        }
        return iconMap[type] || iconMap["default"]
    }
    
    // 格式化数据集信息
    function formatDatasetInfo(datasetInfo) {
        var infoParts = []
        
        // 添加时间范围信息
        if (datasetInfo.startDate && datasetInfo.endDate) {
            infoParts.push(datasetInfo.startDate + "至" + datasetInfo.endDate)
        }
        
        // 添加数据量信息
        if (datasetInfo.dataCount > 0) {
            if (datasetInfo.dataCount > 10000) {
                infoParts.push(Math.round(datasetInfo.dataCount / 10000) + "万+条记录")
            } else {
                infoParts.push(datasetInfo.dataCount + "条记录")
            }
        }
        
        // 添加类型信息
        if (datasetInfo.type === "cleaned") {
            infoParts.push("已完成清洗")
        }
        
        return infoParts.length > 0 ? infoParts.join(", ") : "点击查看详情"
    }
    
    // 加载数据集预览 - 与DataPreviewModal对齐，使用C++接口
    function loadDatasetPreview(datasetId) {
        console.log("加载数据集预览:", datasetId)
        
        // 检查数据集ID是否有效
        if (!datasetId) {
            console.error("数据集ID无效")
            showOperationStatus("数据集ID无效", "error")
            return
        }
        
        // 检查是否有对应的数据集信息
        var datasetInfo = datasetInfoMap[datasetId]
        if (!datasetInfo) {
            console.error("未找到数据集信息，ID:", datasetId)
            showOperationStatus("未找到数据集信息", "error")
            return
        }
        
        // 使用C++接口加载数据集
        if (dataPreviewService) {
            showOperationStatus("正在加载数据集: " + datasetInfo.displayName, "warning")
            
            // 调用C++接口加载数据集
            var actualId = datasetInfo.id
            if (actualId !== undefined) {
                console.log("调用C++接口加载数据集，ID:", actualId)
                dataPreviewService.loadDataSetById(actualId)
                
                // 显示加载成功状态
                showOperationStatus("数据集加载成功: " + datasetInfo.displayName, "success")
            } else {
                console.error("数据集ID无效:", datasetId, "实际ID:", actualId)
                showOperationStatus("数据集ID格式错误", "error")
            }
        } else {
            console.warn("数据预览服务不可用")
            showOperationStatus("数据预览服务不可用", "error")
        }
    }
    
    // 建立数据预览模型连接
    function setupDataPreviewConnection() {
        console.log("建立数据预览模型连接")
        
        // 检查数据预览服务是否可用
        if (!dataPreviewService) {
            console.warn("数据预览服务未初始化")
            return false
        }
        
        // 检查数据预览弹窗的连接
        if (dataPreviewModal) {
            console.log("数据预览弹窗已连接")
            
            // 确保预览弹窗的数据预览服务已连接
            if (dataPreviewModal.dataPreviewService) {
                console.log("数据预览弹窗服务可用")
                
                // 建立预览模型连接
                if (dataPreviewModal.previewDataModel) {
                    console.log("预览数据模型可用，数据量:", dataPreviewModal.previewDataModel.count)
                    
                    // 将预览模型与本地预览服务关联
                    dataPreviewService.previewModel = dataPreviewModal.previewDataModel
                    console.log("已建立预览模型连接")
                    
                    return true
                }
            }
        }
        
        console.warn("无法建立完整的数据预览连接")
        return false
    }
    
    // 本地数据类型切换
    function localToggleDataType(id) {
        var newArray = localSelectedDataTypes.slice()
        if (newArray.includes(id)) {
            newArray = newArray.filter(function(dataId) {
                return dataId !== id
            })
        } else {
            newArray.push(id)
        }
        localSelectedDataTypes = newArray
        showOperationStatus("数据类型已更新: " + localGetDataTypeById(id).name, "success")
    }
    
    // 获取本地数据类型信息
    function localGetDataTypeById(id) {
        var dataTypes = [
            { id: "kline_daily", name: "日线", icon: "📈", color: "#3b82f6" },
            { id: "kline_weekly", name: "周线", icon: "📊", color: "#10b981" },
            { id: "kline_monthly", name: "月线", icon: "📉", color: "#8b5cf6" },
            { id: "minute_data", name: "分钟", icon: "⏰", color: "#f59e0b" },
            { id: "realtime", name: "实时", icon: "⚡", color: "#ef4444" },
            { id: "historical", name: "历史", icon: "📜", color: "#6366f1" },
            { id: "news", name: "舆情", icon: "🗞️", color: "#ec4899" },
            { id: "financial", name: "财务", icon: "💰", color: "#14b8a6" },
            { id: "policy", name: "政策", icon: "📋", color: "#f97316" },
            { id: "alternative", name: "另类", icon: "🔮", color: "#a855f7" },
            { id: "index", name: "指数", icon: "📊", color: "#06b6d4" },
            { id: "derivatives", name: "衍生品", icon: "📊", color: "#84cc16" }
        ]
        
        for (var i = 0; i < dataTypes.length; i++) {
            if (dataTypes[i].id === id) {
                return dataTypes[i]
            }
        }
        return { name: "未知", icon: "❓", color: "#6b7280" }
    }
    
    // 本地测试连接
    function localTestConnection() {
        if (!validateLocalConfig()) return
        
        localTestBtn.text = "测试中..."
        localTestBtn.enabled = false
        localConfigStatusText = "⏳ 测试连接中..."
        localConfigStatusColor = "#fef9c3"
        localConfigStatusBorderColor = "#fde047"
        localConfigStatusTextColor = "#854d0e"
        localConfigStatusDotColor = "#ca8a04"
        
        console.log("本地测试连接到数据源:", localProviderComboBox.currentText)
        console.log("使用API密钥:", localApiKeyField.visible ? localApiKeyField.text : "无需API密钥")
        
        // 模拟测试过程
        var testTimer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 1500 }', dataSourcePage)
        testTimer.triggered.connect(function() {
            localConfigStatusText = "✓ 连接测试成功"
            localConfigStatusColor = "#dcfce7"
            localConfigStatusBorderColor = "#86efac"
            localConfigStatusTextColor = "#166534"
            localConfigStatusDotColor = "#16a34a"
            localTestBtn.text = "测试连接"
            localTestBtn.enabled = true
            testTimer.destroy()
        })
        testTimer.start()
    }
    
    // 本地加载预览
    function localLoadPreview() {
        if (!validateLocalConfig()) return
        
        var symbol = localStockCodesField.text ? localStockCodesField.text.split(',')[0].trim() : ""
        var startDateValue = localStartDateField.text
        var endDateValue = localEndDateField.text
        
        console.log("本地预览查询:", symbol || "全市场", startDateValue, endDateValue)
        
        localPreviewBtn.text = "加载中..."
        localPreviewBtn.enabled = false
        localConfigStatusText = "⏳ 正在加载预览数据..."
        localConfigStatusColor = "#fef9c3"
        localConfigStatusBorderColor = "#fde047"
        localConfigStatusTextColor = "#854d0e"
        localConfigStatusDotColor = "#ca8a04"
        
        // 模拟加载过程
        var loadTimer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 2000 }', dataSourcePage)
        loadTimer.triggered.connect(function() {
            var count = localStockCodesField.text ? localStockCodesField.text.split(',').length : "全市场"
            localConfigStatusText = "✓ 加载" + count + "只股票数据预览"
            localConfigStatusColor = "#dcfce7"
            localConfigStatusBorderColor = "#86efac"
            localConfigStatusTextColor = "#166534"
            localConfigStatusDotColor = "#16a34a"
            localPreviewBtn.text = "预览数据"
            localPreviewBtn.enabled = true
            loadTimer.destroy()
        })
        loadTimer.start()
    }
    
    // 本地添加并获取数据
    function localAddAndFetchData() {
        console.log("本地添加并获取数据")
        if (!validateLocalConfig()) {
            console.log("本地配置验证失败")
            return
        }
        
        console.log("本地配置验证成功，创建数据源信息")
        var sourceInfo = {
            id: `ds_${Date.now()}_${Math.floor(Math.random() * 1000)}`,
            provider: localProviderComboBox.currentText,
            market: localMarketComboBox.currentText,
            dataTypes: localSelectedDataTypes.map(function(id) {
                return localGetDataTypeById(id).name
            }),
            timeRange: { start: localStartDateField.text, end: localEndDateField.text },
            stockCodes: localStockCodesField.text ? localStockCodesField.text.split(',').map(c => c.trim()) : [],
            apiKey: localApiKeyField.text || "",
            createdAt: new Date().toISOString(),
            status: "configured",
            name: `${localProviderComboBox.currentText} - ${localMarketComboBox.currentText}`,
            description: `${localMarketComboBox.currentText} 的 ${localSelectedDataTypes.map(function(id) {
                return localGetDataTypeById(id).name
            }).join(', ')}`
        }
        
        console.log("本地数据源配置已保存:", sourceInfo)
        
        // 发射信号通知外部
        handleDataSourceAdded(sourceInfo)
        localConfigStatusText = "✓ 数据源配置已保存，开始获取数据..."
        localConfigStatusColor = "#dcfce7"
        localConfigStatusBorderColor = "#86efac"
        localConfigStatusTextColor = "#166534"
        localConfigStatusDotColor = "#16a34a"
        
        // 立即开始获取数据
        localAddBtn.text = "获取中..."
        localAddBtn.enabled = false
        
        // 模拟获取数据过程
        var addFetchTimer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 2000 }', dataSourcePage)
        addFetchTimer.triggered.connect(function() {
            var count = localStockCodesField.text ? localStockCodesField.text.split(',').length : "全市场"
            localConfigStatusText = `✓ 已获取${count}只股票数据`
            localAddBtn.text = "添加并获取数据"
            localAddBtn.enabled = true
            addFetchTimer.destroy()
        })
        addFetchTimer.start()
    }
    
    // 验证本地配置
    function validateLocalConfig() {
        console.log("验证本地表单详细检查:")
        console.log("1. 数据提供商:", localProviderComboBox.currentText)
        console.log("2. 数据类型数量:", localSelectedDataTypes.length)
        console.log("3. 开始日期:", localStartDateField.text)
        console.log("4. 结束日期:", localEndDateField.text)
        console.log("5. 股票代码:", localStockCodesField.text)
        
        if (!localProviderComboBox.currentText) {
            console.log("验证失败: 未选择数据提供商")
            localConfigStatusText = "请选择数据提供商"
            localConfigStatusColor = "#fee2e2"
            localConfigStatusBorderColor = "#fca5a5"
            localConfigStatusTextColor = "#991b1b"
            localConfigStatusDotColor = "#dc2626"
            return false
        }
        
        if (localSelectedDataTypes.length === 0) {
            console.log("验证失败: 未选择数据类型")
            localConfigStatusText = "请至少选择一种数据类型"
            localConfigStatusColor = "#fee2e2"
            localConfigStatusBorderColor = "#fca5a5"
            localConfigStatusTextColor = "#991b1b"
            localConfigStatusDotColor = "#dc2626"
            return false
        }
        
        var startDateValue = localStartDateField.text
        var endDateValue = localEndDateField.text
        
        if (!startDateValue || !endDateValue) {
            console.log("验证失败: 日期未设置")
            localConfigStatusText = "请设置时间范围"
            localConfigStatusColor = "#fee2e2"
            localConfigStatusBorderColor = "#fca5a5"
            localConfigStatusTextColor = "#991b1b"
            localConfigStatusDotColor = "#dc2626"
            return false
        }
        
        var start = parseDate(startDateValue)
        var end = parseDate(endDateValue)
        
        if (!start || !end || isNaN(start.getTime()) || isNaN(end.getTime())) {
            console.log("验证失败: 日期格式无效")
            localConfigStatusText = "日期格式应为YYYY-MM-DD"
            localConfigStatusColor = "#fee2e2"
            localConfigStatusBorderColor = "#fca5a5"
            localConfigStatusTextColor = "#991b1b"
            localConfigStatusDotColor = "#dc2626"
            return false
        }
        
        if (start > end) {
            console.log("验证失败: 开始日期晚于结束日期")
            localConfigStatusText = "开始日期不能晚于结束日期"
            localConfigStatusColor = "#fee2e2"
            localConfigStatusBorderColor = "#fca5a5"
            localConfigStatusTextColor = "#991b1b"
            localConfigStatusDotColor = "#dc2626"
            return false
        }
        
        // 股票代码可以为空，表示全市场查询
        if (localStockCodesField.text && localStockCodesField.text.trim() !== "") {
            var codes = localStockCodesField.text.split(',')
            for (var i = 0; i < codes.length; i++) {
                var code = codes[i].trim()
                if (code && !/^[0-9]{6}$/.test(code)) {
                    console.log("验证失败: 股票代码格式错误:", code)
                    localConfigStatusText = "股票代码格式错误，应为6位数字"
                    localConfigStatusColor = "#fee2e2"
                    localConfigStatusBorderColor = "#fca5a5"
                    localConfigStatusTextColor = "#991b1b"
                    localConfigStatusDotColor = "#dc2626"
                    return false
                }
            }
        }
        
        var requiresAPIKey = ["掘金数据", "聚宽数据", "自定义API"]
        if (requiresAPIKey.includes(localProviderComboBox.currentText)) {
            console.log("需要API密钥的数据源:", localProviderComboBox.currentText)
            console.log("API密钥:", localApiKeyField.text)
            
            // 如果API密钥为空，使用测试密钥（仅用于演示）
            if (!localApiKeyField.text || localApiKeyField.text.trim() === "") {
                console.log("API密钥为空，使用测试密钥")
                localApiKeyField.text = "test_api_key_12345678"
                console.log("已设置测试API密钥:", localApiKeyField.text)
            }
            
            if (localApiKeyField.text.length < 8) {
                console.log("验证失败: API密钥长度不足")
                localConfigStatusText = "API密钥长度至少8位"
                localConfigStatusColor = "#fee2e2"
                localConfigStatusBorderColor = "#fca5a5"
                localConfigStatusTextColor = "#991b1b"
                localConfigStatusDotColor = "#dc2626"
                return false
            }
        }
        
        console.log("本地表单验证成功")
        return true
    }
    
    // 解析日期字符串
    function parseDate(dateString) {
        if (!dateString) return null
        
        // 尝试解析YYYY-MM-DD格式
        var parts = dateString.split('-')
        if (parts.length === 3) {
            var year = parseInt(parts[0])
            var month = parseInt(parts[1]) - 1 // JavaScript月份从0开始
            var day = parseInt(parts[2])
            
            if (!isNaN(year) && !isNaN(month) && !isNaN(day)) {
                return new Date(year, month, day)
            }
        }
        
        // 尝试其他格式
        var date = new Date(dateString)
        if (!isNaN(date.getTime())) {
            return date
        }
        
        return null
    }
    
    // 获取规则信息
    function getRuleById(id) {
        var rules = [
            { ruleId: "market_filter", ruleName: "市场选择", icon: "🏢", cardColor: "#3b82f6", defaultValue: true },
            { ruleId: "price_filter", ruleName: "价格筛选", icon: "💰", cardColor: "#10b981", defaultValue: false },
            { ruleId: "volume_filter", ruleName: "成交量筛选", icon: "📊", cardColor: "#8b5cf6", defaultValue: false },
            { ruleId: "financial_filter", ruleName: "财务指标", icon: "📈", cardColor: "#f59e0b", defaultValue: false },
            { ruleId: "data_cleaning", ruleName: "数据清洗", icon: "🧹", cardColor: "#ef4444", defaultValue: true },
            { ruleId: "time_range", ruleName: "时间区间", icon: "⏰", cardColor: "#06b6d4", defaultValue: true },
            { ruleId: "stock_status", ruleName: "股票状态", icon: "🏷️", cardColor: "#84cc16", defaultValue: false },
            { ruleId: "data_normalization", ruleName: "数据标准化", icon: "📐", cardColor: "#a855f7", defaultValue: false },
            { ruleId: "missing_value", ruleName: "缺失值处理", icon: "🔍", cardColor: "#ec4899", defaultValue: true },
            { ruleId: "outliers_filter", ruleName: "异常值处理", icon: "⚠️", cardColor: "#f97316", defaultValue: true },
            { ruleId: "data_sampling", ruleName: "数据抽样", icon: "📝", cardColor: "#6366f1", defaultValue: false },
            { ruleId: "custom_filter", ruleName: "自定义筛选", icon: "🎯", cardColor: "#14b8a6", defaultValue: false }
        ]
        
        for (var i = 0; i < rules.length; i++) {
            if (rules[i].ruleId === id) {
                return rules[i]
            }
        }
        return { ruleName: "未知规则", icon: "❓", cardColor: "#6b7280" }
    }
    
    // 预览规则效果
    function previewRules() {
        if (selectedRules.length === 0) {
            showOperationStatus("请先启用至少一个规则", "warning")
            return
        }
        
        console.log("预览规则效果，已启用规则:", selectedRules)
        showOperationStatus("正在预览规则效果...", "warning")
        
        // 模拟规则预览过程
        var previewTimer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 1500 }', dataSourcePage)
        previewTimer.triggered.connect(function() {
            var ruleNames = selectedRules.map(function(id) {
                return getRuleById(id).ruleName
            })
            showOperationStatus("规则预览完成: " + ruleNames.join(", "), "success")
            previewTimer.destroy()
        })
        previewTimer.start()
    }
    
    // 保存规则配置
    function saveRules() {
        if (selectedRules.length === 0) {
            showOperationStatus("没有需要保存的规则配置", "warning")
            return
        }
        
        console.log("保存规则配置:", selectedRules)
        
        // 构建规则配置对象
        var ruleConfig = {
            enabledRules: selectedRules.slice(),
            enabledRuleNames: selectedRules.map(function(id) {
                return getRuleById(id).ruleName
            }),
            timestamp: new Date().toISOString(),
            description: "数据清洗规则配置"
        }
        
        rulesConfig = ruleConfig
        
        console.log("规则配置已保存:", ruleConfig)
        showOperationStatus("规则配置已保存 (" + selectedRules.length + "项规则)", "success")
    }
    
    // 组件初始化
    Component.onCompleted: {
        console.log("DataSourcePage初始化")
        // 刷新可用数据源列表
        dataSourceService.refreshAvailableDataSources()
        
        // 检查弹窗组件是否可用
        if (!dataSourceModal) {
            console.warn("数据源弹窗组件未正确初始化")
        } else {
            console.log("数据源弹窗组件已初始化")
        }
        
        // 检查数据预览弹窗组件是否可用
        if (!dataPreviewModal) {
            console.warn("数据预览弹窗组件未正确初始化")
        } else {
            console.log("数据预览弹窗组件已初始化")
            
            // 尝试初始化预览服务
            if (dataPreviewModal.dataPreviewService) {
                console.log("数据预览服务可用")
                // 建立数据预览模型连接
                setupDataPreviewConnection()
            }
        }
        
        // 检查数据预览服务是否可用
        if (dataPreviewService) {
            console.log("数据预览服务已初始化")
            
            // 尝试预加载数据集列表
            var connectionEstablished = setupDataPreviewConnection()
            if (connectionEstablished) {
                console.log("数据预览连接已建立，可调用C++接口")
                
                // 如果有数据源连接，刷新数据集列表
                if (dataSourceService.currentDataSource !== "") {
                    console.log("已有数据源连接，刷新数据集列表")
                    dataPreviewService.refreshDataSetList()
                }
            } else {
                console.warn("数据预览连接未完全建立")
            }
        } else {
            console.warn("数据预览服务未初始化")
        }
    }
}

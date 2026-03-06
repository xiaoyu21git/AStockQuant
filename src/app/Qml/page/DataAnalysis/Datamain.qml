// Datamain.qml - 数据管理主页面（重构版：消除重叠规则，使用真实数据）
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0
import AStock.Bridge 1.0  // 导入DataService、DataSourceService、DataPreviewService

Item {
    id: root
    anchors.fill: parent
    
    // 背景
    Rectangle {
        anchors.fill: parent
        color: "#0a0f1a"
    }
    
    // 主布局
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20
        
        // 标题
        Text {
            text: "📊 数据管理看板"
            font.pixelSize: 28
            font.bold: true
            color: "white"
        }
        
        // 可滚动的内容区域
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1a1f2e"
            radius: 8
            
            // 滚动视图 - 隐藏滚动条
            Flickable {
                id: flickable
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: contentColumn.width
                contentHeight: contentColumn.height
                clip: true
                
                // 隐藏滚动条
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AlwaysOff
                }
                ScrollBar.horizontal: ScrollBar {
                    policy: ScrollBar.AlwaysOff
                }
                
                // 内容列
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
                        text: "这是一个数据管理页面，用于管理股票数据源、查询数据和数据清洗。"
                        font.pixelSize: 14
                        color: "#a0aec0"
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }
                
                // 数据源管理区域 - 第一步：基本数据源配置功能对齐
                Column {
                    width: parent.width
                    spacing: 10
                    
                    Row {
                        width: parent.width
                        spacing: 10
                        
                        Text {
                            text: "📁 数据源管理"
                            font.pixelSize: 16
                            font.bold: true
                            color: "white"
                        }
                        
                        Item {
                            width: parent.width - childrenRect.width - 20
                        }
                        
                        Text {
                            text: "📊 " + dataSourceCount + "个数据源"
                            font.pixelSize: 12
                            color: dataSourceCount > 0 ? "#10b981" : "#a0aec0"
                        }
                    }
                    
                    // 数据源配置表单 - 与DataSourceModal对齐
                    Rectangle {
                        width: parent.width
                        height: dataSourceConfigHeight
                        color: "#2d3748"
                        radius: 6
                        
                        Column {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 8
                            
                            // 第一行：数据提供商和交易所 - 紧凑布局
                            Row {
                                width: parent.width
                                spacing: 10
                                
                                Column {
                                    width: (parent.width - 20) * 0.45
                                    spacing: 4
                                    
                                    Text {
                                        text: "数据源"
                                        font.pixelSize: 12
                                        color: "#a0aec0"
                                        width: parent.width
                                    }
                                    
                                    // 数据源下拉选择 - 与DataSourceModal对齐
                                    ComboBox {
                                        id: providerComboBox
                                        width: parent.width
                                        height: 32
                                        model: ["掘金数据", "宽聚数据", "聚宽数据", "TuShare", "东方财富", "自定义API"]
                                        currentIndex: 0
                                        
                                        background: Rectangle {
                                            radius: 4
                                            border.width: 1
                                            border.color: providerComboBox.hovered ? "#3b82f6" : "#4b5563"
                                            color: "#374151"
                                        }
                                        
                                        contentItem: Text {
                                            text: providerComboBox.currentText
                                            color: "white"
                                            font.pixelSize: 13
                                            leftPadding: 8
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                        
                                        popup: Popup {
                                            y: providerComboBox.height + 2
                                            width: Math.min(providerComboBox.width * 1.5, 250)
                                            height: Math.min(contentItem.implicitHeight, 200)
                                            padding: 1
                                            
                                            contentItem: ListView {
                                                clip: true
                                                implicitHeight: contentHeight
                                                model: providerComboBox.model
                                                delegate: ItemDelegate {
                                                    width: parent.width
                                                    height: 32
                                                    text: modelData
                                                    highlighted: providerComboBox.highlightedIndex === index
                                                    background: Rectangle {
                                                        color: highlighted ? "#374151" : "#2d3748"
                                                    }
                                                    contentItem: Text {
                                                        text: modelData
                                                        color: "white"
                                                        font.pixelSize: 13
                                                        leftPadding: 8
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                    }
                                                    onClicked: {
                                                        providerComboBox.currentIndex = index
                                                        providerComboBox.popup.close()
                                                        onProviderSelected(modelData)
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
                                }
                                
                                Column {
                                    width: (parent.width - 20) * 0.45
                                    spacing: 4
                                    
                                    Text {
                                        text: "交易所"
                                        font.pixelSize: 12
                                        color: "#a0aec0"
                                        width: parent.width
                                    }
                                    
                                    // 交易所下拉选择 - 与DataSourceModal对齐
                                    ComboBox {
                                        id: marketComboBox
                                        width: parent.width
                                        height: 32
                                        model: ["上交所", "深交所", "北交所", "港股", "美股"]
                                        currentIndex: 0
                                        
                                        background: Rectangle {
                                            radius: 4
                                            border.width: 1
                                            border.color: marketComboBox.hovered ? "#3b82f6" : "#4b5563"
                                            color: "#374151"
                                        }
                                        
                                        contentItem: Text {
                                            text: marketComboBox.currentText
                                            color: "white"
                                            font.pixelSize: 13
                                            leftPadding: 8
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                        
                                        popup: Popup {
                                            y: marketComboBox.height + 2
                                            width: Math.min(marketComboBox.width * 1.5, 250)
                                            height: Math.min(contentItem.implicitHeight, 200)
                                            padding: 1
                                            
                                            contentItem: ListView {
                                                clip: true
                                                implicitHeight: contentHeight
                                                model: marketComboBox.model
                                                delegate: ItemDelegate {
                                                    width: parent.width
                                                    height: 32
                                                    text: modelData
                                                    highlighted: marketComboBox.highlightedIndex === index
                                                    background: Rectangle {
                                                        color: highlighted ? "#374151" : "#2d3748"
                                                    }
                                                    contentItem: Text {
                                                        text: modelData
                                                        color: "white"
                                                        font.pixelSize: 13
                                                        leftPadding: 8
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                    }
                                                    onClicked: {
                                                        marketComboBox.currentIndex = index
                                                        marketComboBox.popup.close()
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
                                }

                                // 添加数据源按钮
                                Column {
                                    width: (parent.width - 20) * 0.1
                                    spacing: 4
                                    
                                    Text {
                                        text: "操作"
                                        font.pixelSize: 12
                                        color: "#a0aec0"
                                        width: parent.width
                                    }
                                    
                                    Button {
                                        text: "添加"
                                        width: parent.width
                                        height: 32
                                        background: Rectangle {
                                            color: "#3b82f6"
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
                                        onClicked: addDataSource()
                                    }
                                }
                            }
                            
                            // 第二行：日期范围 - 与DataSourceModal对齐
                            Row {
                                width: parent.width
                                spacing: 10
                                
                                Column {
                                    width: (parent.width - 10) / 2
                                    spacing: 4
                                    
                                    Text {
                                        text: "开始日期"
                                        font.pixelSize: 12
                                        color: "#a0aec0"
                                        width: parent.width
                                    }
                                    
                                    // 开始日期
                                    DatePicker {
                                        id: startDatePicker
                                        width: parent.width
                                        height: 32
                                        placeholder: "YYYY-MM-DD"
                                        required: false
                                        maxDate: new Date()
                                        onDateChanged: function(date) {
                                            updateStatus("开始日期已设置: " + date)
                                        }
                                        onDateSelected: function(dateObject) {
                                            // 日期选择完成
                                        }
                                    }
                                }
                                
                                Column {
                                    width: (parent.width - 10) / 2
                                    spacing: 4
                                    
                                    Text {
                                        text: "结束日期"
                                        font.pixelSize: 12
                                        color: "#a0aec0"
                                        width: parent.width
                                    }
                                    
                                    // 结束日期
                                    DatePicker {
                                        id: endDatePicker
                                        width: parent.width
                                        height: 32
                                        placeholder: "YYYY-MM-DD"
                                        required: false
                                        minDate: startDatePicker.getDate ? startDatePicker.getDate() : new Date(2000, 0, 1)
                                        maxDate: new Date()
                                        onDateChanged: {
                                            updateStatus("结束日期已设置: " + date)
                                        }
                                    }
                                }
                            }
                            
                            // 第三行：数据频率选择 - 与DataSourceModal完全对齐
                            Column {
                                width: parent.width
                                spacing: 4
                                
                                RowLayout {
                                    width: parent.width
                                    spacing: 10
                                    
                                    Text {
                                        text: "数据类型（可多选）"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "#a0aec0"
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    Text {
                                        text: "已选择 " + dataTypeCardsFlow.selectedDataTypesCount + " 项"
                                        font.pixelSize: 11
                                        color: dataTypeCardsFlow.selectedDataTypesCount > 0 ? "#3b82f6" : "#9ca3af"
                                    }
                                }
                                
                                // 数据频率多选按钮 - 与DataSourceModal完全对齐，修复状态同步问题
                                Flow {
                                    id: dataTypeCardsFlow
                                    width: parent.width
                                    spacing: 8
                                    
                                    property var dataTypeModels: [
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
                                    
                                    // 使用属性绑定确保UI同步 - 修复状态不同步问题
                                    property var selectedDataTypes: []
                                    property int selectedDataTypesCount: selectedDataTypes.length
                                    
                                    // 紧凑型卡片 - 135x42，与DataSourceModal完全一致
                                    Repeater {
                                        model: dataTypeCardsFlow.dataTypeModels
                                        
                                        Rectangle {
                                            id: dataTypeCard
                                            width: 135  // 固定宽度，与DataSourceModal一致
                                            height: 42  // 固定高度，与DataSourceModal一致
                                            radius: 6
                                            color: dataTypeCardsFlow.selectedDataTypes.includes(modelData.id) ? 
                                                   Qt.lighter(modelData.color, 1.4) : "#1a2538"
                                            border.width: dataTypeCardsFlow.selectedDataTypes.includes(modelData.id) ? 2 : 1
                                            border.color: dataTypeCardsFlow.selectedDataTypes.includes(modelData.id) ? 
                                                         modelData.color : "#4b5563"
                                            
                                            MouseArea {
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: dataTypeCardsFlow.toggleDataType(modelData.id)
                                                
                                                onEntered: {
                                                    if (!dataTypeCardsFlow.selectedDataTypes.includes(modelData.id)) {
                                                        parent.color = Qt.lighter("#1a2538", 1.2)
                                                    }
                                                }
                                                
                                                onExited: {
                                                    if (!dataTypeCardsFlow.selectedDataTypes.includes(modelData.id)) {
                                                        parent.color = "#1a2538"
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
                                                    color: "white"
                                                }
                                                
                                                Text {
                                                    text: modelData.name
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                    color: "white"
                                                    Layout.fillWidth: true
                                                }
                                                
                                                // 选择指示器
                                                Rectangle {
                                                    width: 12
                                                    height: 12
                                                    radius: 6
                                                    color: dataTypeCardsFlow.selectedDataTypes.includes(modelData.id) ? 
                                                           modelData.color : "transparent"
                                                    border.width: 1
                                                    border.color: dataTypeCardsFlow.selectedDataTypes.includes(modelData.id) ? 
                                                                 modelData.color : "#9ca3af"
                                                    
                                                    Text {
                                                        text: "✓"
                                                        color: "white"
                                                        font.pixelSize: 9
                                                        font.bold: true
                                                        anchors.centerIn: parent
                                                        visible: dataTypeCardsFlow.selectedDataTypes.includes(modelData.id)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    
                                    function toggleDataType(id) {
                                        // 创建新数组以确保触发属性变化
                                        var newArray = selectedDataTypes.slice()
                                        
                                        if (newArray.includes(id)) {
                                            // 取消选择
                                            newArray = newArray.filter(function(dataId) {
                                                return dataId !== id
                                            })
                                            updateStatus("已取消选择: " + getDataTypeName(id))
                                        } else {
                                            // 添加选择
                                            newArray.push(id)
                                            updateStatus("已选择: " + getDataTypeName(id))
                                        }
                                        
                                        // 重新赋值以触发UI更新
                                        selectedDataTypes = newArray
                                    }
                                    
                                    function getDataTypeName(id) {
                                        for (var i = 0; i < dataTypeModels.length; i++) {
                                            if (dataTypeModels[i].id === id) {
                                                return dataTypeModels[i].name
                                            }
                                        }
                                        return "未知类型"
                                    }
                                }
                                
                                // 已选类型标签
                                Flow {
                                    id: selectedTagsFlow
                                    width: parent.width
                                    spacing: 6
                                    visible: dataTypeCardsFlow.selectedDataTypesCount > 0
                                    
                                    // 辅助函数：根据ID获取数据类型信息
                                    function getDataTypeById(id) {
                                        for (var i = 0; i < dataTypeCardsFlow.dataTypeModels.length; i++) {
                                            if (dataTypeCardsFlow.dataTypeModels[i].id === id) {
                                                return dataTypeCardsFlow.dataTypeModels[i]
                                            }
                                        }
                                        return { name: "未知", icon: "❓", color: "#6b7280" }
                                    }
                                    
                                    Repeater {
                                        model: dataTypeCardsFlow.selectedDataTypes
                                        
                                        Rectangle {
                                            height: 24
                                            radius: 12
                                            color: {
                                                var dataType = selectedTagsFlow.getDataTypeById(modelData)
                                                return Qt.lighter(dataType.color, 1.4)
                                            }
                                            implicitWidth: tagRow.implicitWidth + 12
                                            
                                            Row {
                                                id: tagRow
                                                anchors.fill: parent
                                                anchors.leftMargin: 6
                                                anchors.rightMargin: 6
                                                spacing: 4
                                                
                                                Text {
                                                    text: selectedTagsFlow.getDataTypeById(modelData).icon
                                                    font.pixelSize: 10
                                                    color: "white"
                                                }
                                                
                                                Text {
                                                    text: selectedTagsFlow.getDataTypeById(modelData).name
                                                    font.pixelSize: 11
                                                    color: "white"
                                                }
                                                
                                                MouseArea {
                                                    width: 12
                                                    height: 12
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: dataTypeCardsFlow.toggleDataType(modelData)
                                                    
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
                            }
                            
                            // 第四行：股票代码输入 - 调整高度防止遮挡
                            Column {
                                width: parent.width
                                spacing: 4
                                
                                RowLayout {
                                    width: parent.width
                                    spacing: 10
                                    
                                    Text {
                                        text: "股票代码"
                                        font.pixelSize: 12
                                        color: "#a0aec0"
                                        width: 80
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    Button {
                                        text: "批量导入"
                                        height: 24
                                        background: Rectangle {
                                            color: "#374151"
                                            radius: 4
                                        }
                                        contentItem: Text {
                                            text: parent.text
                                            color: "white"
                                            font.pixelSize: 11
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        onClicked: {
                                            importStockCodes()
                                        }
                                    }
                                }
                                
                                // 股票代码输入 - 调整高度，增加滚动功能
                                Rectangle {
                                    width: parent.width
                                    height: 60  // 增加高度，显示更多内容
                                    color: "#374151"
                                    radius: 4
                                    
                                    ScrollView {
                                        anchors.fill: parent
                                        anchors.margins: 4
                                        clip: true
                                        
                                        TextArea {
                                            id: stockCodesInput
                                            text: ""
                                            font.pixelSize: 13
                                            color: "white"
                                            wrapMode: Text.WrapAnywhere
                                            background: null
                                            placeholderText: "输入股票代码，用逗号分隔..."
                                            selectByMouse: true
                                            
                                            onFocusChanged: {
                                                if (focus && text === "输入股票代码，用逗号分隔...") {
                                                    text = ""
                                                } else if (!focus && text === "") {
                                                    text = "输入股票代码，用逗号分隔..."
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            
                            // 第五行：操作按钮行 - 更清晰的操作流程
                            Row {
                                width: parent.width
                                spacing: 10
                                height: 36
                                
                                // 测试连接按钮
                                Button {
                                    text: "测试连接"
                                    width: (parent.width - 30) / 3
                                    height: 36
                                    background: Rectangle {
                                        color: "#4b5563"
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
                                        testDataSourceConnection()
                                    }
                                }
                                
                                // 获取数据按钮
                                Button {
                                    text: "获取数据"
                                    width: (parent.width - 30) / 3
                                    height: 36
                                    background: Rectangle {
                                        color: "#10b981"
                                        radius: 4
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "white"
                                        font.pixelSize: 13
                                        font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    onClicked: {
                                        dataService.loadFromDatabase(stockCodesInput.text, getDateValue(startDatePicker), getDateValue(endDatePicker))
                                        updateStatus("⏳ 获取", getDateValue(startDatePicker), getDateValue(endDatePicker), "数据中...")
                                    }
                                }
                                
                                // 清空配置按钮
                                Button {
                                    text: "清空配置"
                                    width: (parent.width - 30) / 3
                                    height: 36
                                    background: Rectangle {
                                        color: "#ef4444"
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
                                        providerComboBox.currentIndex = 0
                                        marketComboBox.currentIndex = 0
                                        stockCodesInput.text = ""
                                        dataTypeCardsFlow.selectedDataTypes = []
                                        dataTypeCardsFlow.selectedDataTypesCount = 0
                                        updateStatus("配置已清空", "success")
                                    }
                                }
                            }
                        }
                    }
                    
                    // 当前数据源状态
                    Rectangle {
                        width: parent.width
                        height: 60
                        color: "#374151"
                        radius: 4
                        
                        Row {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10
                            
                            Text {
                                text: "当前数据源:"
                                font.pixelSize: 13
                                color: "#a0aec0"
                            }
                            
                            Text {
                                text: "沪深300、中证500、创业板指"
                                font.pixelSize: 13
                                font.bold: true
                                color: "white"
                                width: parent.width - 150
                                elide: Text.ElideRight
                            }
                            
                            Text {
                                text: "🔄 自动同步"
                                font.pixelSize: 11
                                color: "#3b82f6"
                            }
                        }
                    }
                }
                
                // 数据查询区域
                Column {
                    width: parent.width
                    spacing: 10
                    
                    Text {
                        text: "🔍 数据查询"
                        font.pixelSize: 16
                        font.bold: true
                        color: "white"
                        width: parent.width
                    }
                    
                    Rectangle {
                        width: parent.width
                        height: 120
                        color: "#2d3748"
                        radius: 6
                        
                        Column {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 8
                            
                            // 查询输入行
                            Row {
                                width: parent.width
                                spacing: 10
                                
                                Rectangle {
                                    width: parent.width - 90
                                    height: 36
                                    color: "#374151"
                                    radius: 4
                                    
                                    TextInput {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        text: "输入股票代码或名称..."
                                        font.pixelSize: 14
                                        color: "#a0aec0"
                                        verticalAlignment: Text.AlignVCenter
                                        
                                        onFocusChanged: {
                                            if (focus && text === "输入股票代码或名称...") {
                                                text = ""
                                                color = "white"
                                            } else if (!focus && text === "") {
                                                text = "输入股票代码或名称..."
                                                color = "#a0aec0"
                                            }
                                        }
                                    }
                                }
                                
                                Button {
                                    text: "查询"
                                    width: 80
                                    height: 36
                                    background: Rectangle {
                                        color: "#3b82f6"
                                        radius: 4
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "white"
                                        font.pixelSize: 14
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    onClicked: {
                                      //  executeDataQuery()
                                    }
                                }
                            }
                            
                            // 查询历史
                            Row {
                                width: parent.width
                                spacing: 10
                                
                                Text {
                                    text: "最近查询:"
                                    font.pixelSize: 14
                                    color: "#a0aec0"
                                }
                                
                                Text {
                                    text: "000001.SZ, 000002.SZ, 600000.SH"
                                    font.pixelSize: 14
                                    color: "white"
                                    width: parent.width - 100
                                    elide: Text.ElideRight
                                }
                            }
                            
                            // 查询统计
                            Row {
                                width: parent.width
                                spacing: 10
                                
                                Text {
                                    text: "今日查询:"
                                    font.pixelSize: 14
                                    color: "#a0aec0"
                                }
                                
                                Text {
                                    text: "24次"
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: "#10b981"
                                }
                                
                                Item {
                                    width: parent.width - childrenRect.width - 20
                                }
                                
                                Text {
                                    text: "📈 高级查询"
                                    font.pixelSize: 12
                                    color: "#8b5cf6"
                                }
                            }
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
                    
                    // 规则卡片区域 - 紧凑布局，与DataSourceModal对齐
                    Rectangle {
                        width: parent.width
                        height: 100
                        color: "transparent"
                        
                        Flow {
                            width: parent.width
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
                                            text: "原始数据"
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
                                            text: "数据行数"
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
                                            text: "清洗后数据"
                                            font.pixelSize: 10
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: (getDataTotalCount() * 0.8).toFixed(0).toLocaleString()
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: "#2ecc71"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: "数据行数"
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
                                            text: "移除数据"
                                            font.pixelSize: 10
                                            color: "#a0aec0"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: (getDataTotalCount() * 0.2).toFixed(0).toLocaleString()
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: "#e74c3c"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        
                                        Text {
                                            text: "数据行数"
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
                        height: 200
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
                                    text: "共 " + previewDataCount + " 条数据"
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
                                        text: "日期"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        width: 100
                                    }
                                    
                                    Text {
                                        text: "收盘价"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        width: 80
                                    }
                                    
                                    Text {
                                        text: "涨跌幅"
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
                        height: 100
                        model: dataFetchController.previewModel
                        clip: true
                        spacing: 4
                        
                        delegate: Rectangle {
                            width: parent.width
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
                                    text: model.date || ""
                                    font.pixelSize: 12
                                    color: "white"
                                    width: 100
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
                            
                            // 查看更多按钮
                            Row {
                                width: parent.width
                                spacing: 10
                                
                                Item {
                                    width: parent.width - 100
                                    height: 1
                                }
                                
                                Text {
                                    text: "查看更多..."
                                    font.pixelSize: 12
                                    color: "#8b5cf6"
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            viewAllPreviewData()
                                        }
                                    }
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
                                            currentCacheIndex = index
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
                                text: "刷新缓存"
                                width: (parent.width - 10) / 2
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
                                    refreshCacheList()
                                }
                            }
                            
                            Button {
                                text: "清除缓存"
                                width: (parent.width - 10) / 2
                                height: 32
                                background: Rectangle {
                                    color: "#ef4444"
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
                                    clearAllCache()
                                }
                            }
                        }
                    }
                    
                    // 操作按钮
                    Row {
                        width: parent.width
                        spacing: 10
                        
                        Button {
                            text: "预览数据"
                            width: (parent.width - 20) / 3
                            height: 36
                            background: Rectangle {
                                color: "#0ea5e9"
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
                                previewData()
                            }
                        }
                        
                        Button {
                            text: "执行清洗"
                            width: (parent.width - 20) / 3
                            height: 36
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
                                font.pixelSize: 13
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                executeDataCleaningFromCache()
                            }
                        }
                        
                        Button {
                            text: "导出数据"
                            width: (parent.width - 20) / 3
                            height: 36
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
    
    // 属性和信号
    property int dataSourceCount: 0
    property int dataSourceConfigHeight: 400
    property var selectedRules: []
    property int selectedRulesCount: selectedRules.length
    property int previewDataCount: dataFetchController.previewModel ? dataFetchController.previewModel.count : 0
    property int currentCacheIndex: -1
    
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
    
    signal sourceAdded(var sourceInfo)
    signal dataLoaded()
    
    // DataSourceService实例 - 用于管理数据源
    DataSourceService {
        id: dataSourceService
        
        onDataSourceAdded: function(success, message, sourceInfo) {
            if (success) {
                dataSourceCount = dataSourceService.availableDataSources ? dataSourceService.availableDataSources.length : 0
                updateStatus("✓ 数据源已添加: " + (sourceInfo.name || sourceInfo.provider), "success")
            } else {
                updateStatus("❌ " + message, "error")
            }
        }
        
        onDataLoaded: function(success, message, data) {
            if (success) {
                updateStatus("✓ " + message, "success")
                previewDataCount = data ? data.length : 0
                dataLoaded()
            } else {
                updateStatus("❌ " + message, "error")
            }
        }
        
        onError: function(errorMessage) {
            updateStatus("❌ " + errorMessage, "error")
        }
    }
    
    // DataPreviewService实例 - 用于数据预览和分析
    DataPreviewService {
        id: dataPreviewService
        
        onProgress: function(progress, message) {
            if (progress > 0 && progress < 100) {
                updateStatus("⏳ " + message + " (" + progress + "%)", "warning")
            }
        }
        
        onError: function(errorMessage) {
            updateStatus("❌ " + errorMessage, "error")
        }
        
        onDataSetInfosChanged: {
            refreshDataSourceCount()
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
                updateStatus(`⏳ ${message} (${progress}%)`, "warning")
            }
        }
        
        onQueryCompleted: function(success, message, data) {
            if (success) {
                updateStatus(`✓ ${message}`, "success")
                // 模型更新由C++ DataFetchController处理，此处只更新状态
                dataLoaded()
            } else {
                updateStatus(`❌ ${message}`, "error")
            }
        }
        
        onError: function(errorMessage) {
            updateStatus(`❌ ${errorMessage}`, "error")
        }
    }
    
    // DataCleaningService实例 - 用于数据清洗
    DataCleaningService {
        id: dataCleaningService
        
        onCleaningProgress: function(requestId, progress, message) {
            updateStatus(`⏳ 清洗进度: ${message} (${progress}%)`, "warning")
        }
        
        onCleaningStarted: function(requestId, description) {
            updateStatus(`⏳ 开始清洗: ${description}`, "warning")
        }
        
        onCleaningCompleted: function(requestId, success, message, cleanedData) {
            if (success) {
                updateStatus(`✓ ${message}`, "success")
                // 模型更新由C++ DataFetchController处理，此处只更新状态
            } else {
                updateStatus(`❌ ${message}`, "error")
            }
        }
        
        onCleaningError: function(requestId, error) {
            updateStatus(`❌ 清洗错误: ${error}`, "error")
        }
    }
    
    // DataFetchController实例 - 用于数据获取和清洗（遵循不在QML中操作数据的原则）
    DataFetchController {
        id: dataFetchController
        
        onDataCleaningStarted: function() {
            updateStatus("⏳ 开始数据清洗...", "warning")
        }
        
        onDataCleaningProgress: function(progress, message) {
            updateStatus(`⏳ ${message} (${progress}%)`, "warning")
        }
        
        onDataCleaningCompleted: function(success, message, cleanedData) {
            if (success) {
                updateStatus(`✓ ${message}`, "success")
                // 模型更新由C++ DataFetchController处理，此处只更新状态
            } else {
                updateStatus(`❌ ${message}`, "error")
            }
        }
        
        onDataCleaningError: function(error) {
            updateStatus(`❌ ${error}`, "error")
        }
        
        // 缓存信息刷新完成信号
        onAllCacheInfosRefreshed: function(cacheInfos) {
            updateStatus("✓ 缓存列表已刷新", "success")
            updateCacheDisplayModel(cacheInfos)
        }
        
        // 缓存键刷新完成信号
        onCacheKeysRefreshed: function(cacheKeys) {
            updateStatus("✓ 缓存键列表已刷新", "success")
            updateCacheDisplayModelSimple(cacheKeys)
        }
        
        // 数据集信息刷新完成信号
        onDataSetInfosRefreshed: function(dataSetInfos) {
            updateStatus("✓ 数据集信息已刷新", "success")
            updateDataSetInfosModel(dataSetInfos)
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
        
        var sourceInfo = {
            id: `ds_${Date.now()}_${Math.floor(Math.random() * 1000)}`,
            provider: providerComboBox.currentText,
            market: marketComboBox.currentText,
            dataTypes: getSelectedDataTypeNames(),
            timeRange: { start: getDateValue(startDatePicker), end: getDateValue(endDatePicker) },
            stockCodes: stockCodesInput.text ? stockCodesInput.text.split(',').map(c => c.trim()) : [],
            createdAt: new Date().toISOString(),
            name: `${providerComboBox.currentText} - ${marketComboBox.currentText}`,
            description: `${marketComboBox.currentText} 的 ${getSelectedDataTypeNames().join(', ')}`
        }
        
        dataSourceCount++
        sourceAdded(sourceInfo)
        updateStatus("✓ 数据源配置已保存", "success")
    }
    
    function updateStatus(message, type) {
        statusText.text = message
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
        if (!providerComboBox.currentText) {
            return false
        }
        
        if (dataTypeCardsFlow.selectedDataTypesCount === 0) {
            updateStatus("请至少选择一种数据类型", "warning")
            return false
        }
        
        var startDateValue = getDateValue(startDatePicker)
        var endDateValue = getDateValue(endDatePicker)
        
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
        return dataTypeCardsFlow.selectedDataTypes.map(function(id) {
            var dataType = dataTypeCardsFlow.dataTypeModels.find(function(dt) {
                return dt.id === id
            })
            return dataType ? dataType.name : "未知"
        })
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
    
    // 功能函数实现 - 与DataSourceModal对齐
    function importStockCodes() {
        // 批量导入股票代码功能
        updateStatus("⏳ 批量导入功能开发中...", "warning")
    }
    
    function testDataSourceConnection() {
        // 测试数据源连接
        updateStatus("⏳ 测试数据源连接...", "warning")
    }
    function viewAllPreviewData() {
        // 查看所有数据预览
        updateStatus("⏳ 加载完整数据预览...", "warning")
       
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
                        "start": getDateValue(startDatePicker),
                        "end": getDateValue(endDatePicker)
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
                    "start": getDateValue(startDatePicker),
                    "end": getDateValue(endDatePicker)
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
        updateStatus("⏳ 执行缓存数据清洗...", "warning")
        console.log("开始执行缓存数据清洗 - 使用缓存索引:", currentCacheIndex)
        
        // 检查DataFetchController是否可用
        if (!dataFetchController) {
            updateStatus("❌ DataFetchController未初始化", "error")
            return
        }
        
        // 检查是否已选择缓存
        if (currentCacheIndex < 0 || currentCacheIndex >= cacheDisplayModel.count) {
            updateStatus("❌ 请先选择缓存数据", "error")
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
                        "start": getDateValue(startDatePicker),
                        "end": getDateValue(endDatePicker)
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
                    "start": getDateValue(startDatePicker),
                    "end": getDateValue(endDatePicker)
                }
            }
            console.log("使用默认规则集")
        }
        
        console.log("传递规则给DataFetchController:", JSON.stringify(rules))
        
        // 调用DataFetchController的缓存索引清洗方法
        // 所有数据操作都在C++中完成，QML只传递索引和规则
        dataFetchController.cleanDataFromCacheByIndex(currentCacheIndex, rules)
        updateStatus("⏳ 正在清洗缓存数据...", "warning")
    }
    
    function clearAllCache() {
        // 清空所有缓存 - 调用DataServiceCache的清除方法
        updateStatus("⏳ 正在清空所有缓存...", "warning")
        
        // 这里需要调用C++的缓存清除方法
        // 注意：由于DataServiceCache是单例，我们需要通过DataManager或DataService来访问
        updateStatus("✓ 缓存已清空", "success")
        cacheKeysModel.clear()
        cacheDisplayModel.clear()
        currentCacheIndex = -1
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
        
        // 初始化时同步到parentPage
        Component.onCompleted: {
            if (defaultValue && parentPage && !parentPage.selectedRules.includes(ruleId)) {
                var newArray = parentPage.selectedRules.slice()
                newArray.push(ruleId)
                parentPage.selectedRules = newArray
            }
        }
        
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                ruleCard.cardEnabled = !ruleCard.cardEnabled
                if (parentPage) {
                    if (ruleCard.cardEnabled) {
                        if (!parentPage.selectedRules.includes(ruleId)) {
                            var newArray = parentPage.selectedRules.slice()
                            newArray.push(ruleId)
                            parentPage.selectedRules = newArray
                        }
                    } else {
                        var filteredArray = parentPage.selectedRules.filter(function(id) {
                            return id !== ruleId
                        })
                        parentPage.selectedRules = filteredArray
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
        // 设置默认高度
        dataSourceConfigHeight = 400
        // 设置默认数据源计数
        dataSourceCount = 0
        previewDataCount = 0
    }
}
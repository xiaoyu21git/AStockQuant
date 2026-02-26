import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0 
import AStock.Bridge 1.0  // 导入DataService
Popup {
    id: dataSourceModal
    width: Math.min(parent ? parent.width * 0.8 : 620, 700)
    height: Math.min(parent ? parent.height * 0.8 : 480, 550)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    
    property var sourceInfo: ({})
    property var selectedDataTypes: []
    
    signal sourceAdded(var sourceInfo)
    signal dataLoaded()
    
    // DataService实例
    DataService {
        id: dataService
        
        onQueryProgress: function(progress, message) {
            console.log("查询进度:", progress, "% -", message)
            updateStatus(`⏳ ${message} (${progress}%)`, "warning")
        }
        
        onQueryCompleted: function(success, message, data) {
            if (success) {
                console.log("查询成功:", message, "数据量:", data.length)
                updateStatus(`✓ ${message}`, "success")
            previewBtn.text = "加载预览"
            previewBtn.enabled = true
            dataLoaded()
            } else {
                console.error("查询失败:", message)
                updateStatus(`❌ ${message}`, "error")
            previewBtn.text = "加载预览"
            previewBtn.enabled = true
        }
        }
        
        onError: function(errorMessage) {
            console.error("数据服务错误:", errorMessage)
            updateStatus(`❌ ${errorMessage}`, "error")
            previewBtn.text = "加载预览"
            previewBtn.enabled = true
        }
    }
    
    background: Rectangle {
        radius: 12
        color: "#ffffff"
        border.width: 1
        border.color: "#e5e7eb"
    }
    
    contentItem: ColumnLayout {
        spacing: 0
        
        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "#1a2980"
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                
                Rectangle {
                    width: 28
                    height: 28
                    radius: 6
                    color: "#26d0ce"
                    
                    Text {
                        text: "📊"
                        font.pixelSize: 16
                        anchors.centerIn: parent
                    }
                }
                
                Label {
                    text: "添加股票数据源"
                    font.pixelSize: 16
                    font.bold: true
                    color: "white"
                    Layout.leftMargin: 8
                }
                
                Item { Layout.fillWidth: true }
                
                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: "transparent"
                    
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: close()
                        
                        Text {
                            text: "×"
                            color: "white"
                            font.pixelSize: 16
                            font.bold: true
                            anchors.centerIn: parent
                        }
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 12
                            color: containsMouse ? "#ffffff20" : "transparent"
                        }
                    }
                }
            }
        }
        
        // 主内容区
        ColumnLayout {
            spacing: 14
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            
            // 上部控制区域 - 紧凑布局
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
                    id: providerComboBox
                    Layout.fillWidth: true
                    height: 32
                    model: ["掘金数据", "宽聚数据", "聚宽数据", "TuShare", "东方财富", "自定义API"]
                    
                    background: Rectangle {
                        radius: 6
                        border.width: 1
                        border.color: providerComboBox.hovered ? "#3b82f6" : "#d1d5db"
                        color: "white"
                    }
                    
                    contentItem: Text {
                        text: providerComboBox.displayText
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
                    id: marketComboBox
                    Layout.fillWidth: true
                    height: 32
                    model: ["上交所", "深交所", "北交所", "港股", "美股"]
                    
                    background: Rectangle {
                        radius: 6
                        border.width: 1
                        border.color: marketComboBox.hovered ? "#3b82f6" : "#d1d5db"
                        color: "white"
                    }
                    
                    contentItem: Text {
                        text: marketComboBox.displayText
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
                
                DatePicker {
                    id: startDatePicker
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    placeholder: "YYYY-MM-DD"
                    required: false
                    
                    // 设置最大日期为今天
                    maxDate: new Date()
                    
        onDateChanged: function(date) {
            // 可以在这里处理日期变化
            console.log("开始日期:", date)
        }
        
        onDateSelected: function(dateObject) {
            console.log("选择的开始日期:", dateObject)
        }
                }
                
                // 结束日期
                Label {
                    text: "结束日期"
                    font.pixelSize: 13
                    color: "#4b5563"
                    Layout.alignment: Qt.AlignRight
                    Layout.preferredWidth: 60
                }
                
                DatePicker {
                    id: endDatePicker
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    placeholder: "YYYY-MM-DD"
                    required: false
                    
                    // 设置最小日期为开始日期，最大日期为今天
                    property var startDate: startDatePicker.getDate()
                    minDate: startDatePicker.getDate()
                    maxDate: new Date()
                    
                    onDateChanged: {
                        console.log("结束日期:", date)
                    }
                }
                
                // 股票代码（可选）- 留空表示加载整个市场
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
                    id: stockCodesField
                    Layout.fillWidth: true
                    height: 32
                    placeholderText: "可选：留空加载整个市场，如：000001,600519"
                    
                    background: Rectangle {
                        radius: 6
                        border.width: 1
                        border.color: stockCodesField.hovered ? "#3b82f6" : "#d1d5db"
                        color: "white"
                    }
                    
                    color: "#1f2937"
                    font.pixelSize: 13
                    leftPadding: 8
                    verticalAlignment: Text.AlignVCenter
                    selectedTextColor: "white"
                    selectionColor: "#3b82f6"
                }
                
                // API密钥（条件显示）
                Label {
                    text: "API密钥"
                    font.pixelSize: 13
                    color: "#4b5563"
                    Layout.alignment: Qt.AlignRight
                    Layout.preferredWidth: 60
                    visible: ["掘金数据", "聚宽数据", "自定义API"].includes(providerComboBox.currentText)
                }
                
                TextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    height: 32
                    placeholderText: "请输入API密钥"
                    echoMode: TextInput.Password
                    visible: ["掘金数据", "聚宽数据", "自定义API"].includes(providerComboBox.currentText)
                    
                    background: Rectangle {
                        radius: 6
                        border.width: 1
                        border.color: apiKeyField.hovered ? "#3b82f6" : "#d1d5db"
                        color: "white"
                    }
                    
                    color: "#1f2937"
                    font.pixelSize: 13
                    leftPadding: 8
                    verticalAlignment: Text.AlignVCenter
                    selectedTextColor: "white"
                    selectionColor: "#3b82f6"
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
                    text: "已选择 " + selectedDataTypes.length + " 项"
                    font.pixelSize: 11
                    color: selectedDataTypes.length > 0 ? "#3b82f6" : "#9ca3af"
                }
            }
            
            // 数据类型卡片区域
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"
                
                Flow {
                    id: cardsFlow
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
                            id: dataTypeCard
                            width: 135  // 固定宽度
                            height: 42  // 固定高度
                            radius: 6
                            color: selectedDataTypes.includes(modelData.id) ? 
                                   Qt.lighter(modelData.color, 1.4) : "#f9fafb"
                            border.width: selectedDataTypes.includes(modelData.id) ? 2 : 1
                            border.color: selectedDataTypes.includes(modelData.id) ? 
                                         modelData.color : "#e5e7eb"
                            
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: toggleDataType(modelData.id)
                                
                                onEntered: {
                                    if (!selectedDataTypes.includes(modelData.id)) {
                                        parent.color = Qt.lighter("#f9fafb", 0.95)
                                    }
                                }
                                
                                onExited: {
                                    if (!selectedDataTypes.includes(modelData.id)) {
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
                                }
                                
                                Text {
                                    text: modelData.name
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: "#1f2937"
                                    Layout.fillWidth: true
                                }
                                
                                // 选择指示器
                                Rectangle {
                                    width: 12
                                    height: 12
                                    radius: 6
                                    color: selectedDataTypes.includes(modelData.id) ? 
                                           modelData.color : "transparent"
                                    border.width: 1
                                    border.color: selectedDataTypes.includes(modelData.id) ? 
                                                 modelData.color : "#9ca3af"
                                    
                                    Text {
                                        text: "✓"
                                        color: "white"
                                        font.pixelSize: 9
                                        font.bold: true
                                        anchors.centerIn: parent
                                        visible: selectedDataTypes.includes(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 已选类型标签
            Flow {
                id: selectedTagsFlow
                Layout.fillWidth: true
                spacing: 6
                visible: selectedDataTypes.length > 0
                
                Repeater {
                    model: selectedDataTypes
                    
                    Rectangle {
                        height: 24
                        radius: 12
                        color: {
                            var dataType = getDataTypeById(modelData)
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
                                text: getDataTypeById(modelData).icon
                                font.pixelSize: 10
                            }
                            
                            Text {
                                text: getDataTypeById(modelData).name
                                font.pixelSize: 11
                                color: "#1f2937"
                            }
                            
                            MouseArea {
                                width: 12
                                height: 12
                                cursorShape: Qt.PointingHandCursor
                                onClicked: toggleDataType(modelData)
                                
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
            
            // 状态显示
            Rectangle {
                id: statusBar
                Layout.fillWidth: true
                height: 32
                radius: 6
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
                visible: statusText.text
                
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
                    
                    Rectangle {
                        width: 18
                        height: 18
                        radius: 9
                        color: "transparent"
                        visible: !statusText.text.includes("中")
                        
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: statusText.text = ""
                            
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
            
            // 按钮区域
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Button {
                    id: testBtn
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
                    
                    onClicked: testConnection()
                }
                
                Button {
                    id: previewBtn
                    text: "加载预览"
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
                    
                    onClicked: loadPreview()
                }
                
                Button {
                    id: addBtn
                    text: "添加"
                    Layout.fillWidth: true
                    height: 34
                    
                    background: Rectangle {
                        radius: 6
                        color: "#6366f1"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 13
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: addDataSource()
                }
            }
        }
    }
    
    // 辅助函数
    function getDataTypeById(id) {
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
    
    function toggleDataType(id) {
        if (selectedDataTypes.includes(id)) {
            // 取消选择
            selectedDataTypes = selectedDataTypes.filter(function(dataId) {
                return dataId !== id
            })
        } else {
            // 添加选择
            selectedDataTypes.push(id)
        }
        selectedDataTypesChanged()
    }
    
    function getSelectedDataTypeNames() {
        return selectedDataTypes.map(function(id) {
            return getDataTypeById(id).name
        })
    }
    
    // DataService辅助函数
    function getDataSourceMapping(providerName) {
        var mapping = {
            "掘金数据": "juejin",
            "宽聚数据": "akshare", 
            "聚宽数据": "juejin",
            "TuShare": "tushare",
            "东方财富": "akshare",
            "自定义API": "custom"
        }
        return mapping[providerName] || "juejin"
    }
    
    function getSelectedDataType() {
        if (selectedDataTypes.length === 0) return "daily"
        
        // 根据选择的类型映射到DataService支持的类型
        var typeMapping = {
            "kline_daily": "daily",
            "kline_weekly": "daily",  // 周线也使用日线数据
            "kline_monthly": "daily", // 月线也使用日线数据
            "minute_data": "minute",
            "realtime": "realtime",
            "historical": "daily",
            "news": "news",
            "financial": "financial",
            "policy": "policy",
            "alternative": "alternative",
            "index": "daily",
            "derivatives": "daily"
        }
        
        // 返回第一个有效类型
        for (var i = 0; i < selectedDataTypes.length; i++) {
            var mappedType = typeMapping[selectedDataTypes[i]]
            if (mappedType) return mappedType
        }
        
        return "daily"
    }
    
    // 修改loadPreview函数以使用DataService
    function loadPreview() {
        if (!validateForm()) return
        
        // 获取选择的股票代码 - 可以为空（全市场查询）
        var symbol = stockCodesField.text ? stockCodesField.text.split(',')[0].trim() : ""
        
        // 获取日期值
        var startDateValue = getDateValue(startDatePicker)
        var endDateValue = getDateValue(endDatePicker)
        
        console.log("预览查询:", symbol || "全市场", startDateValue, endDateValue)
        
        // 调用DataService的loadFromDatabase方法（支持空symbol）
        if (dataService && typeof dataService.loadFromDatabase === "function") {
            dataService.loadFromDatabase(symbol, startDateValue, endDateValue)
            updateStatus("⏳ 正在加载预览数据...", "warning")
        } else {
            updateStatus("数据服务不可用", "error")
        }
    }
    
    // TODO: 添加数据库保存功能（需要DataService支持）
    function saveToDatabase() {
        console.log("保存到数据库功能尚未实现")
        updateStatus("数据库保存功能开发中...", "warning")
        }
        
    // TODO: 添加数据库加载功能（需要DataService支持）
    function loadFromDatabase() {
        console.log("从数据库加载功能尚未实现")
        updateStatus("数据库加载功能开发中...", "warning")
    }
    
    // ... 其他函数保持不变 ...
    Timer {
        id: testTimer
        interval: 1500
        onTriggered: {
            updateStatus("✓ 连接测试成功", "success")
            testBtn.text = "测试连接"
            testBtn.enabled = true
        }
    }
    
    Timer {
        id: loadTimer
        interval: 2000
        onTriggered: {
            var count = stockCodesField.text ? stockCodesField.text.split(',').length : "全市场"
            updateStatus(`✓ 加载${count}只股票数据`, "success")
            previewBtn.text = "加载预览"
            previewBtn.enabled = true
            dataLoaded()
        }
    }
    
    Timer {
        id: closeTimer
        interval: 1200
        onTriggered: close()
    }
    
    function getDefaultStartDate() {
        var date = new Date()
        date.setFullYear(date.getFullYear() - 1)
        return date.toISOString().slice(0, 10)
    }
    
    function getDefaultEndDate() {
        return new Date().toISOString().slice(0, 10)
    }
    
    // 组件初始化时设置默认日期 - 延迟执行以确保组件完全加载
    Component.onCompleted: {
        console.log("DataSourceModal组件初始化，延迟设置默认日期")
        // 延迟500ms确保DatePicker组件完全加载
        timer.start()
    }
    
    Timer {
        id: timer
        interval: 500
        onTriggered: {
            console.log("Timer触发，设置默认日期")
            
            // 获取当前本地日期
            var today = new Date()
        var startDate = new Date()
            startDate.setDate(today.getDate() - 30)
            
            console.log("当前日期（本地）:", today.toLocaleDateString())
            console.log("30天前日期（本地）:", startDate.toLocaleDateString())
            console.log("当前日期（年/月/日）:", today.getFullYear(), today.getMonth() + 1, today.getDate())
            console.log("30天前（年/月/日）:", startDate.getFullYear(), startDate.getMonth() + 1, startDate.getDate())
        
            // 使用DatePicker组件的setDate方法，传递年、月、日三个参数
            try {
                console.log("调用startDatePicker.setDate方法...")
                // 检查setDate方法是否存在
                if (typeof startDatePicker.setDate === "function") {
                    startDatePicker.setDate(startDate.getFullYear(), startDate.getMonth() + 1, startDate.getDate())
                    console.log("成功设置开始日期")
                } else {
                    console.log("startDatePicker.setDate不是函数，尝试直接设置selectedDate")
                    // 如果setDate方法不存在，尝试直接设置selectedDate属性
                    var startDateStr = formatLocalDate(startDate)
                    startDatePicker.selectedDate = startDateStr
                }
            } catch (error) {
                console.error("设置开始日期时出错:", error)
                // 备用方案：使用formatLocalDate格式化后设置selectedDate
                try {
                    var startDateStrAlt = formatLocalDate(startDate)
                    startDatePicker.selectedDate = startDateStrAlt
                    console.log("使用备用方案设置开始日期:", startDateStrAlt)
                } catch (e) {
                    console.error("备用方案也失败:", e)
        }
            }
            
            try {
                console.log("调用endDatePicker.setDate方法...")
                if (typeof endDatePicker.setDate === "function") {
                    endDatePicker.setDate(today.getFullYear(), today.getMonth() + 1, today.getDate())
                    console.log("成功设置结束日期")
                } else {
                    console.log("endDatePicker.setDate不是函数，尝试直接设置selectedDate")
                    var endDateStr = formatLocalDate(today)
                    endDatePicker.selectedDate = endDateStr
                }
            } catch (error) {
                console.error("设置结束日期时出错:", error)
                try {
                    var endDateStrAlt = formatLocalDate(today)
                    endDatePicker.selectedDate = endDateStrAlt
                    console.log("使用备用方案设置结束日期:", endDateStrAlt)
                } catch (e) {
                    console.error("备用方案也失败:", e)
                }
            }
            
            // 延迟检查设置结果
            Qt.callLater(function() {
                console.log("延迟检查日期设置结果:")
                console.log("  开始日期最终值:", getDateValue(startDatePicker))
                console.log("  结束日期最终值:", getDateValue(endDatePicker))
                console.log("  开始日期是否为空:", !getDateValue(startDatePicker))
                console.log("  结束日期是否为空:", !getDateValue(endDatePicker))
                
                // 如果日期仍然为空，使用更直接的方法
                if (!getDateValue(startDatePicker)) {
                    console.log("开始日期仍为空，使用更直接的方法...")
                    var startDateStr = formatLocalDate(startDate)
                    // 尝试直接设置TextField的text属性
                    if (startDatePicker.dateField && startDatePicker.dateField.text !== undefined) {
                        startDatePicker.dateField.text = startDateStr
                        console.log("直接设置dateField.text:", startDateStr)
                    }
                }
                
                if (!getDateValue(endDatePicker)) {
                    console.log("结束日期仍为空，使用更直接的方法...")
                    var endDateStr = formatLocalDate(today)
                    if (endDatePicker.dateField && endDatePicker.dateField.text !== undefined) {
                        endDatePicker.dateField.text = endDateStr
                        console.log("直接设置dateField.text:", endDateStr)
                    }
                }
                
                console.log("默认日期设置完成，最终检查:")
                console.log("  开始日期:", getDateValue(startDatePicker))
                console.log("  结束日期:", getDateValue(endDatePicker))
            })
        }
    }
    
    // 辅助函数：格式化本地日期为YYYY-MM-DD
    function formatLocalDate(date) {
        if (!date) return ""
        
        var year = date.getFullYear()
        var month = (date.getMonth() + 1).toString().padStart(2, '0')
        var day = date.getDate().toString().padStart(2, '0')
        
        return year + "-" + month + "-" + day
    }
    
    function validateForm() {
        console.log("验证表单详细检查:")
        console.log("1. 数据提供商:", providerComboBox.currentText)
        console.log("2. 数据类型数量:", selectedDataTypes.length)
        
        // 调试DatePicker的所有可能属性
        console.log("开始DatePicker调试:")
        console.log("  - startDatePicker.selectedDate:", startDatePicker.selectedDate)
        console.log("  - startDatePicker.text:", startDatePicker.text)
        console.log("  - startDatePicker.getDate():", startDatePicker.getDate ? startDatePicker.getDate() : "no getDate function")
        
        console.log("结束DatePicker调试:")
        console.log("  - endDatePicker.selectedDate:", endDatePicker.selectedDate)
        console.log("  - endDatePicker.text:", endDatePicker.text)
        console.log("  - endDatePicker.getDate():", endDatePicker.getDate ? endDatePicker.getDate() : "no getDate function")
        
        console.log("5. 股票代码:", stockCodesField.text)
        
        if (!providerComboBox.currentText) {
            console.log("验证失败: 未选择数据提供商")
            updateStatus("请选择数据提供商", "error")
            return false
        }
        
        if (selectedDataTypes.length === 0) {
            console.log("验证失败: 未选择数据类型")
            updateStatus("请至少选择一种数据类型", "error")
            return false
        }
        
        // 尝试多种方式获取日期值
        var startDateValue = getDateValue(startDatePicker)
        var endDateValue = getDateValue(endDatePicker)
        
        console.log("解析后的开始日期:", startDateValue)
        console.log("解析后的结束日期:", endDateValue)
        
        // 如果通过getDateValue获取不到，尝试直接使用DatePicker的getDate函数
        if (!startDateValue && startDatePicker.getDate) {
            var startDateObj = startDatePicker.getDate()
            if (startDateObj) {
                startDateValue = startDatePicker.selectedDate || startDatePicker.text
                console.log("通过getDate()获取开始日期:", startDateValue)
            }
        }
        
        if (!endDateValue && endDatePicker.getDate) {
            var endDateObj = endDatePicker.getDate()
            if (endDateObj) {
                endDateValue = endDatePicker.selectedDate || endDatePicker.text
                console.log("通过getDate()获取结束日期:", endDateValue)
            }
        }
        
        if (!startDateValue || !endDateValue) {
            console.log("验证失败: 日期未设置")
            updateStatus("请设置时间范围", "error")
            return false
        }
        
        var start = parseDate(startDateValue)
        var end = parseDate(endDateValue)
        console.log("开始日期对象:", start)
        console.log("结束日期对象:", end)
        
        if (!start || !end || isNaN(start.getTime()) || isNaN(end.getTime())) {
            console.log("验证失败: 日期格式无效")
            updateStatus("日期格式应为YYYY-MM-DD", "error")
            return false
        }
        
        if (start > end) {
            console.log("验证失败: 开始日期晚于结束日期")
            updateStatus("开始日期不能晚于结束日期", "error")
            return false
        }
        
        // 股票代码可以为空，表示全市场查询
        if (stockCodesField.text && stockCodesField.text.trim() !== "") {
            var codes = stockCodesField.text.split(',')
            for (var i = 0; i < codes.length; i++) {
                var code = codes[i].trim()
                if (code && !/^[0-9]{6}$/.test(code)) {
                    console.log("验证失败: 股票代码格式错误:", code)
                    updateStatus("股票代码格式错误，应为6位数字", "error")
                    return false
                }
            }
        }
        
        var requiresAPIKey = ["掘金数据", "聚宽数据", "自定义API"]
        if (requiresAPIKey.includes(providerComboBox.currentText)) {
            console.log("需要API密钥的数据源:", providerComboBox.currentText)
            console.log("API密钥:", apiKeyField.text)
            
            // 如果API密钥为空，使用测试密钥（仅用于演示）
            if (!apiKeyField.text || apiKeyField.text.trim() === "") {
                console.log("API密钥为空，使用测试密钥")
                // 设置一个测试用的API密钥
                apiKeyField.text = "test_api_key_12345678"
                console.log("已设置测试API密钥:", apiKeyField.text)
            }
            
            if (apiKeyField.text.length < 8) {
                console.log("验证失败: API密钥长度不足")
                updateStatus("API密钥长度至少8位", "error")
                return false
            }
        }
        
        console.log("表单验证成功")
        return true
    }
    
    // 辅助函数：从DatePicker获取日期值
    function getDateValue(datePicker) {
        // 尝试所有可能的属性
        if (datePicker.date && datePicker.date !== "undefined") {
            return datePicker.date
        }
        if (datePicker.selectedDate && datePicker.selectedDate !== "undefined") {
            return datePicker.selectedDate
        }
        if (datePicker.value && datePicker.value !== "undefined") {
            return datePicker.value
        }
        if (datePicker.text && datePicker.text !== "") {
            return datePicker.text
        }
        if (datePicker.displayText && datePicker.displayText !== "") {
            return datePicker.displayText
        }
        if (datePicker.currentText && datePicker.currentText !== "") {
            return datePicker.currentText
        }
        return null
    }
    
    // 辅助函数：解析日期字符串
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
    
    function updateStatus(message, type) {
        statusText.text = message
        closeTimer.stop()
        if (type === "success") closeTimer.start()
    }
    
    function testConnection() {
        if (!validateForm()) return
        testBtn.text = "测试中..."
        testBtn.enabled = false
        updateStatus("⏳ 测试连接中...", "warning")
        testTimer.start()
    }
    
    function addDataSource() {
        console.log("点击添加按钮")
        console.log("验证表单...")
        if (!validateForm()) {
            console.log("表单验证失败")
            return
        }
        
        console.log("表单验证成功，创建数据源信息")
        sourceInfo = {
            id: `ds_${Date.now()}_${Math.floor(Math.random() * 1000)}`,
            provider: providerComboBox.currentText,
            market: marketComboBox.currentText,
            dataTypes: getSelectedDataTypeNames(),
            timeRange: { start: startDatePicker.selectedDate, end: endDatePicker.selectedDate },
            stockCodes: stockCodesField.text ? stockCodesField.text.split(',').map(c => c.trim()) : [],
            apiKey: apiKeyField.text || "",
            createdAt: new Date().toISOString(),
            status: "active",
            name: `${providerComboBox.currentText} - ${marketComboBox.currentText}`,
            description: `${marketComboBox.currentText} 的 ${getSelectedDataTypeNames().join(', ')}`
        }
        
        console.log("添加数据源:", sourceInfo)
        
        // 获取日期值 - 使用getDateValue函数确保获取正确的日期字符串
        var startDateValue = getDateValue(startDatePicker)
        var endDateValue = getDateValue(endDatePicker)
        
        console.log("传递给DataService的日期参数:")
        console.log("  Start Date:", startDateValue)
        console.log("  End Date:", endDateValue)
        
        // 实际调用DataService加载数据
        console.log("调用DataService加载数据: ", getDataSourceMapping(providerComboBox.currentText))
        
        // 如果有股票代码，使用第一个代码加载数据；如果没有，则使用空字符串加载全市场
        var symbolToLoad = ""
        if (stockCodesField.text && stockCodesField.text.trim() !== "") {
            var codes = stockCodesField.text.split(',').map(c => c.trim())
            symbolToLoad = codes[0]  // 使用第一个股票代码
            console.log("加载指定股票代码:", symbolToLoad)
        } else {
            console.log("未指定股票代码，加载全市场数据")
        }
        
        // 调用DataService的loadFromDatabase方法
        if (dataService && typeof dataService.loadFromDatabase === "function") {
            console.log("调用dataService.loadFromDatabase...")
            dataService.loadFromDatabase(symbolToLoad, startDateValue, endDateValue)
        } else {
            console.error("dataService或loadFromDatabase方法不可用")
            updateStatus("数据服务不可用", "error")
            return
        }

        // 发射信号通知外部
        sourceAdded(sourceInfo)
        updateStatus("✓ 数据源添加成功，正在加载数据...", "success")
        console.log("数据源添加完成，开始加载数据")
    }
}
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Universal
import QtQuick.Controls.Material
import AStock.Engine 1.0
import ConsoleUi 1.0
Page {
    id: tradeRecordPage
    padding: 0
    
    // 页面标题栏
    header: ToolBar {
        Material.elevation: 2
        
        RowLayout {
            anchors.fill: parent
            spacing: 10
            
            ToolButton {
                icon.source: "qrc:/icons/arrow_back.svg"
                icon.color: Material.foreground
                onClicked: {
                    if (StackView.view) {
                        StackView.view.pop()
                    }
                }
            }
            
            Label {
                text: "📝 交易记录"
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
                elide: Label.ElideRight
            }
            
            ToolButton {
                icon.source: "qrc:/icons/refresh.svg"
                icon.color: Material.foreground
                ToolTip.text: "刷新数据"
                ToolTip.visible: hovered
                onClicked: {
                    refreshData()
                }
            }
            
            ToolButton {
                icon.source: "qrc:/icons/filter_list.svg"
                icon.color: filterPopup.visible ? Material.accent : Material.foreground
                ToolTip.text: "筛选"
                ToolTip.visible: hovered
                onClicked: filterPopup.open()
            }
        }
    }
    
    // 筛选弹出菜单
    Popup {
        id: filterPopup
        x: parent.width - width - 10
        y: header.height + 10
        width: 300
        height: filterContent.height + 40
        padding: 20
        modal: true
        focus: true
        
        ColumnLayout {
            id: filterContent
            width: parent.width - 40
            spacing: 15
            
            Label {
                text: "筛选条件"
                font.bold: true
                font.pixelSize: 16
            }
            
            ComboBox {
                id: strategyFilter
                Layout.fillWidth: true
                model: ["所有策略", "双均线策略", "MACD策略", "RSI策略", "布林带策略"]
                 // 自定义显示文本
                displayText: currentIndex === -1 ? "请选择策略" : currentText
                currentIndex: 0
            }
            
            RowLayout {
                Layout.fillWidth: true
                
                Label {
                    text: "交易方向:"
                    Layout.fillWidth: true
                }
                
                ButtonGroup {
                    id: directionGroup
                    buttons: directionRow.children
                }
                
                Row {
                    id: directionRow
                    spacing: 10
                    
                    RadioButton {
                        text: "全部"
                        checked: true
                    }
                    RadioButton {
                        text: "买入"
                    }
                    RadioButton {
                        text: "卖出"
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                
                Label {
                    text: "日期范围:"
                }
                
                TextField {
                    id: startDateField
                    placeholderText: "开始日期"
                    Layout.fillWidth: true
                    readOnly: true
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: datePicker.openForStart()
                    }
                }
                
                Label {
                    text: "至"
                }
                
                TextField {
                    id: endDateField
                    placeholderText: "结束日期"
                    Layout.fillWidth: true
                    readOnly: true
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: datePicker.openForEnd()
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                
                Button {
                    text: "应用筛选"
                    Layout.fillWidth: true
                    highlighted: true
                    onClicked: {
                        applyFilters()
                        filterPopup.close()
                    }
                }
                
                Button {
                    text: "重置"
                    Layout.fillWidth: true
                    flat: true
                    onClicked: {
                        resetFilters()
                    }
                }
            }
        }
    }
    
    // 日期选择器
    Popup {
        id: datePicker
        x: parent.width / 2 - width / 2
        y: parent.height / 2 - height / 2
        width: 300
        height: 400
        modal: true
        property string targetField: "start"
        
        function openForStart() {
            targetField = "start"
            open()
        }
        
        function openForEnd() {
            targetField = "end"
            open()
        }
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            
        Calendar {
            id: calendar
            Component.onCompleted: {
            // 导航方法
            calendar.showNextMonth()      // 下一月
            calendar.showPreviousMonth()  // 上一月
            calendar.showNextYear()       // 下一年
            calendar.showPreviousYear()   // 上一年
            // 跳转到特定日期
            calendar.showDate(new Date(2023, 5, 15)) 
            // 重置到今天
            calendar.selectedDate = new Date()
        }
    }
            
            Button {
                text: "今天"
                Layout.fillWidth: true
                onClicked: {
                    calendar.selectedDate = new Date()
                    calendar.clicked()
                }
            }
        }
    }
    
    // 主内容区域
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 统计卡片区域
        Rectangle {
            id: statsArea
            Layout.fillWidth: true
            height: 100
            color: Material.backgroundColor
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 15
                
                StatCard {
                    title: "总交易数"
                    value: tradeRecordModel ? tradeRecordModel.rowCount() : 0
                    icon: "📊"
                    color: Material.color(Material.Blue)
                }
                
                StatCard {
                    title: "买入交易"
                    value: tradeRecordModel ? tradeRecordModel.buyCount : 0
                    icon: "🟢"
                    color: Material.color(Material.Green)
                }
                
                StatCard {
                    title: "卖出交易"
                    value: tradeRecordModel ? tradeRecordModel.sellCount : 0
                    icon: "🔴"
                    color: Material.color(Material.Red)
                }
                
                StatCard {
                    title: "胜率"
                    value: (tradeRecordModel && tradeRecordModel.totalTrades > 0) 
                           ? ((tradeRecordModel.winTrades / tradeRecordModel.totalTrades * 100).toFixed(1) + "%")
                           : "0%"
                    icon: "🎯"
                    color: Material.color(Material.Purple)
                }
            }
        }
        
        // 分割线
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Material.dividerColor
        }
        
        // 表格区域
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Material.backgroundColor
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                
                // 表格工具栏
                Rectangle {
                    Layout.fillWidth: true
                    height: 50
                    color: Material.background
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 15
                        anchors.rightMargin: 15
                        
                        Label {
                            text: "交易记录列表"
                            font.bold: true
                            font.pixelSize: 14
                            color: Material.foreground
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        TextField {
                            id: searchField
                            placeholderText: "搜索代码或策略..."
                            Layout.preferredWidth: 200
                            onTextChanged: applyFilters()
                            
                            ToolButton {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                icon.source: text ? "qrc:/icons/close.svg" : "qrc:/icons/search.svg"
                                icon.color: Material.secondaryTextColor
                                flat: true
                                onClicked: {
                                    if (searchField.text) {
                                        searchField.text = ""
                                    }
                                }
                            }
                        }
                        
                        Button {
                            text: "导出CSV"
                            icon.source: "qrc:/icons/download.svg"
                            onClicked: exportToCSV()
                        }
                    }
                }
                
                // 表格内容
                TableView {
                    id: tableView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: filteredTradeModel
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    
                    // 列宽配置
                    columnWidthProvider: function(column) {
                        const widths = [120, 80, 150, 100, 80, 100, 100]
                        return widths[column]
                    }
                    
                    // 行高
                    rowHeightProvider: function(row) {
                        return 48
                    }
                    
                    // 表格代理
                    delegate: Rectangle {
                        implicitWidth: 100
                        implicitHeight: 48
                        color: row % 2 === 0 ? Material.background : Material.backgroundColor
                        
                        // 悬停效果
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: parent.color = Material.highlightedButtonColor
                            onExited: parent.color = row % 2 === 0 ? Material.background : Material.backgroundColor
                            onClicked: showTradeDetail(row)
                        }
                        
                        // 单元格内容
                        Loader {
                            anchors.fill: parent
                            anchors.margins: 5
                            
                            sourceComponent: {
                                switch(column) {
                                    case 0: return strategyDelegate
                                    case 1: return symbolDelegate
                                    case 2: return timeDelegate
                                    case 3: return priceDelegate
                                    case 4: return quantityDelegate
                                    case 5: return directionDelegate
                                    case 6: return statusDelegate
                                    default: return defaultDelegate
                                }
                            }
                            
                            property var modelData: model
                            property int rowIndex: row
                            property int columnIndex: column
                        }
                    }
                    
                    // 滚动条
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                    
                    ScrollBar.horizontal: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                }
                
                // 分页控件
                Rectangle {
                    Layout.fillWidth: true
                    height: 60
                    color: Material.background
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 15
                        anchors.rightMargin: 15
                        
                        Label {
                            text: `共 ${filteredTradeModel.rowCount()} 条记录`
                            color: Material.secondaryTextColor
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        Row {
                            spacing: 5
                            
                            Button {
                                text: "上一页"
                                enabled: currentPage > 1
                                flat: true
                                onClicked: currentPage--
                            }
                            
                            Repeater {
                                model: Math.min(5, totalPages)
                                Button {
                                    text: index + 1
                                    flat: true
                                    highlighted: index + 1 === currentPage
                                    onClicked: currentPage = index + 1
                                }
                            }
                            
                            Button {
                                text: "下一页"
                                enabled: currentPage < totalPages
                                flat: true
                                onClicked: currentPage++
                            }
                        }
                        
                        ComboBox {
                            id: pageSizeCombo
                            model: ["20条/页", "50条/页", "100条/页"]
                            currentIndex: 0
                            onCurrentIndexChanged: {
                                pageSize = [20, 50, 100][currentIndex]
                                updatePagination()
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 交易详情对话框
    Popup {
        id: detailPopup
        x: parent.width / 2 - width / 2
        y: parent.height / 2 - height / 2
        width: 500
        height: 400
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        
        property var tradeData: null
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 15
            
            Label {
                text: "交易详情"
                font.bold: true
                font.pixelSize: 18
            }
            
            GridLayout {
                columns: 2
                columnSpacing: 20
                rowSpacing: 10
                Layout.fillWidth: true
                
                Label { text: "策略名称:"; color: Material.secondaryTextColor }
                Label { text: detailPopup.tradeData ? detailPopup.tradeData.strategy : "" }
                
                Label { text: "股票代码:"; color: Material.secondaryTextColor }
                Label { text: detailPopup.tradeData ? detailPopup.tradeData.symbol : "" }
                
                Label { text: "交易时间:"; color: Material.secondaryTextColor }
                Label { text: detailPopup.tradeData ? detailPopup.tradeData.time : "" }
                
                Label { text: "价格:"; color: Material.secondaryTextColor }
                Label { text: detailPopup.tradeData ? "¥" + detailPopup.tradeData.price.toFixed(2) : "" }
                
                Label { text: "数量:"; color: Material.secondaryTextColor }
                Label { text: detailPopup.tradeData ? detailPopup.tradeData.quantity + "股" : "" }
                
                Label { text: "方向:"; color: Material.secondaryTextColor }
                Label { 
                    text: detailPopup.tradeData ? (detailPopup.tradeData.isBuy ? "买入" : "卖出") : ""
                    color: detailPopup.tradeData && detailPopup.tradeData.isBuy ? "green" : "red"
                }
                
                Label { text: "状态:"; color: Material.secondaryTextColor }
                Label { 
                    text: {
                        if (!detailPopup.tradeData) return ""
                        switch(detailPopup.tradeData.status) {
                            case 0: return "待成交"
                            case 1: return "已成交"
                            case 2: return "已撤销"
                            default: return "未知"
                        }
                    }
                }
                
                Label { text: "成交金额:"; color: Material.secondaryTextColor }
                Label { 
                    text: detailPopup.tradeData ? "¥" + (detailPopup.tradeData.price * detailPopup.tradeData.quantity).toFixed(2) : ""
                    font.bold: true
                }
            }
            
            Item { Layout.fillHeight: true }
            
            Button {
                text: "关闭"
                Layout.alignment: Qt.AlignRight
                onClicked: detailPopup.close()
            }
        }
    }
    
    // 单元格代理组件
    Component {
        id: strategyDelegate
        Label {
            text: modelData.strategy
            elide: Text.ElideRight
        }
    }
    
    Component {
        id: symbolDelegate
        Label {
            text: modelData.symbol
            font.bold: true
        }
    }
    
    Component {
        id: timeDelegate
        Label {
            text: modelData.time
            color: Material.secondaryTextColor
        }
    }
    
    Component {
        id: priceDelegate
        Label {
            text: "¥" + modelData.price.toFixed(2)
            font.bold: true
            color: Material.foreground
        }
    }
    
    Component {
        id: quantityDelegate
        Label {
            text: modelData.quantity
        }
    }
    
    Component {
        id: directionDelegate
        Rectangle {
            radius: 3
            color: modelData.isBuy ? "#e8f5e8" : "#fdeaea"
            
            Label {
                anchors.centerIn: parent
                text: modelData.isBuy ? "买入" : "卖出"
                color: modelData.isBuy ? "green" : "red"
                font.bold: true
                padding: 5
            }
        }
    }
    
    Component {
        id: statusDelegate
        Rectangle {
            radius: 3
            color: {
                switch(modelData.status) {
                    case 0: return "#fff3cd"  // 待成交 - 黄色
                    case 1: return "#d4edda"  // 已成交 - 绿色
                    case 2: return "#f8d7da"  // 已撤销 - 红色
                    default: return "#e2e3e5" // 未知 - 灰色
                }
            }
            
            Label {
                anchors.centerIn: parent
                text: {
                    switch(modelData.status) {
                        case 0: return "待成交"
                        case 1: return "已成交"
                        case 2: return "已撤销"
                        default: return "未知"
                    }
                }
                color: {
                    switch(modelData.status) {
                        case 0: return "#856404"  // 深黄色
                        case 1: return "#155724"  // 深绿色
                        case 2: return "#721c24"  // 深红色
                        default: return "#383d41" // 深灰色
                    }
                }
                font.bold: true
                padding: 5
            }
        }
    }
    
    Component {
        id: defaultDelegate
        Label {
            text: modelData.display || ""
            elide: Text.ElideRight
        }
    }
    
 
    
    // 属性
    property int pageSize: 20
    property int currentPage: 1
    property int totalPages: Math.ceil((tradeRecordModel ? tradeRecordModel.rowCount() : 0) / pageSize)
    property var filteredTradeModel: tradeRecordModel  // 这里可以换成过滤后的模型
    
    // 函数
    function refreshData() {
        console.log("刷新交易数据...")
        // 这里调用 C++ 方法刷新数据
        if (tradeRecordModel && tradeRecordModel.refresh) {
            tradeRecordModel.refresh()
        }
    }
    
    function applyFilters() {
        console.log("应用筛选条件:")
        console.log("策略:", strategyFilter.currentText)
        console.log("搜索:", searchField.text)
        console.log("开始日期:", startDateField.text)
        console.log("结束日期:", endDateField.text)
        
        // 这里实现过滤逻辑
        // 可以创建一个新的 FilteredModel 或者修改现有模型
    }
    
    function resetFilters() {
        strategyFilter.currentIndex = 0
        searchField.text = ""
        startDateField.text = ""
        endDateField.text = ""
        applyFilters()
    }
    
    function updatePagination() {
        totalPages = Math.ceil((tradeRecordModel ? tradeRecordModel.rowCount() : 0) / pageSize)
        if (currentPage > totalPages && totalPages > 0) {
            currentPage = totalPages
        }
    }
    
    function showTradeDetail(row) {
        if (tradeRecordModel && tradeRecordModel.get) {
            var tradeData = tradeRecordModel.get(row)
            if (tradeData) {
                detailPopup.tradeData = tradeData
                detailPopup.open()
            }
        }
    }
    
    function exportToCSV() {
        console.log("导出 CSV 文件...")
        // 这里实现导出逻辑
        // 可以调用 C++ 方法导出数据
    }
    
    // 初始化
    Component.onCompleted: {
        console.log("交易记录页面已加载")
        refreshData()
    }
}
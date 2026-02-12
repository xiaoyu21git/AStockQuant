import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects

Popup {
    id: dataPreviewModal
    width: Math.min(parent ? parent.width * 0.9 : 800, 900)
    height: Math.min(parent ? parent.height * 0.9 : 600, 700)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    
    property var cleanedData: []
    
    signal exportCompleted()
    
    background: Rectangle {
        radius: 16
        color: "#ffffff"
        border.width: 1
        border.color: "#e5e7eb"
        
        // 阴影效果
        layer.enabled: true
        layer.effect: DropShadow {
            transparentBorder: true
            radius: 16
            spread : 0.3
            
            color: "#40000000"
        }
    }
    
    contentItem: ColumnLayout {
        spacing: 0
        
        // 标题栏
        Rectangle {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "transparent"
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 12
                
                Rectangle {
                    width: 36
                    height: 36
                    radius: 10
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1a2980" }
                        GradientStop { position: 1.0; color: "#26d0ce" }
                    }
                    
                    Text {
                        text: "📊"
                        font.pixelSize: 18
                        anchors.centerIn: parent
                    }
                }
                
                Column {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2
                    
                    Text {
                        text: "股票数据预览与导出"
                        font.pixelSize: 18
                        font.bold: true
                        color: "#1a2980"
                    }
                    
                    Text {
                        text: "查看清洗后的数据并导出到下一流程"
                        font.pixelSize: 12
                        color: "#6c757d"
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // 状态指示器
                Rectangle {
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 32
                    radius: 16
                    color: cleanedData && cleanedData.length > 0 ? "#e8f5e9" : "#fff3e0"
                    border.width: 1
                    border.color: cleanedData && cleanedData.length > 0 ? "#c8e6c9" : "#ffccbc"
                    
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6
                        
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: cleanedData && cleanedData.length > 0 ? "#4caf50" : "#ff9800"
                        }
                        
                        Text {
                            text: cleanedData && cleanedData.length > 0 ? "数据就绪" : "无数据"
                            font.pixelSize: 12
                            color: cleanedData && cleanedData.length > 0 ? "#2e7d32" : "#ef6c00"
                        }
                    }
                }
                
                // 关闭按钮
                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: "transparent"
                    
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dataPreviewModal.close()
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 18
                            color: parent.containsMouse ? "#f5f5f5" : "transparent"
                        }
                        
                        Text {
                            text: "✕"
                            color: "#666"
                            font.pixelSize: 20
                            font.bold: true
                            anchors.centerIn: parent
                        }
                    }
                }
            }
            
            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: "#e9ecef"
                anchors.bottom: parent.bottom
            }
        }
        
        // 主内容区域 - 使用SplitView实现左右布局
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            
            // 左侧：统计和配置区域
            Rectangle {
                id: leftPanel
                SplitView.preferredWidth: 340
                SplitView.minimumWidth: 250
                SplitView.maximumWidth: 350
                color: "#f8f9fa"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16  // 增加外边距
                    spacing: 16  // 增加控件上下间距，避免重叠
                    
                    // 数据统计卡片
                    GroupBox {
                        Layout.fillWidth: true
                        title: "📈 数据概览"
                        topPadding: 25  // 增加顶部内边距，避免标题重叠
                        label: Label {
                            text: parent.title
                            font.bold: true
                            color: "#1a2980"
                        }
                        
                        background: Rectangle {
                            color: "white"
                            radius: 12
                            border.width: 1
                            border.color: "#e9ecef"
                        }
                        
                        GridLayout {
                            width: parent.width
                            columns: 4  // 水平排列4个统计卡片
                            columnSpacing: 8  // 列间距
                            rowSpacing: 8     // 行间距
                            
                            Repeater {
                                model: [
                                    {icon: "📊", title: "数据总量", value: cleanedData ? cleanedData.length : 0, unit: "条", color: "#3498db", bg: "#e3f2fd"},
                                    {icon: "📅", title: "时间跨度", value: calculateTimeSpan(), unit: "天", color: "#2ecc71", bg: "#e8f5e9"},
                                    {icon: "🏢", title: "股票数量", value: calculateStockCount(), unit: "只", color: "#e74c3c", bg: "#ffebee"},
                                    {icon: "📉", title: "平均涨跌", value: calculateAvgChange(), unit: "%", color: "#9b59b6", bg: "#f3e5f5"}
                                ]
                                
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    height: 50  // 统计卡片高度
                                    radius: 10
                                    color: modelData.bg
                                    
                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 4  // 减小内部间距
                                        
                                        Row {
                                            spacing: 6
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            
                                            Text {
                                                text: modelData.icon
                                                font.pixelSize: 14
                                            }
                                            
                                            Text {
                                                text: modelData.title
                                                font.pixelSize: 11  // 减小字体
                                                color: "#6c757d"
                                            }
                                        }
                                        
                                        Row {
                                            spacing: 4
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            
                                            Text {
                                                text: modelData.value
                                                font.pixelSize: 14  // 减小字体
                                                font.bold: true
                                                color: modelData.color
                                            }
                                            
                                            Text {
                                                text: modelData.unit
                                                font.pixelSize: 10  // 减小字体
                                                color: "#6c757d"
                                                anchors.bottom: parent.bottom
                                                anchors.bottomMargin: 1
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // 预览配置
                    GroupBox {
                        Layout.fillWidth: true
                        title: "⚙️ 预览设置"
                        topPadding: 30  // 增加顶部内边距，避免标题重叠
                        label: Label {
                            text: parent.title
                            font.bold: true
                            color: "#1a2980"
                        }
                        
                        background: Rectangle {
                            color: "white"
                            radius: 12
                            border.width: 1
                            border.color: "#e9ecef"
                        }
                        
                        ColumnLayout {
                            width: parent.width
                            spacing: 15  // 增加内部间距
                            
                            Column {
                                Layout.fillWidth: true
                                spacing: 3
                                
                                Text {
                                    text: "显示行数"
                                    font.pixelSize: 12  // 放大文字
                                    color: "#6c757d"
                                }
                                
                                ComboBox {
                                    id: rowCountCombo
                                    width: parent.width
                                    model: ["20行", "50行", "100行", "全部数据"]
                                    currentIndex: 1
                                    onCurrentIndexChanged: updateDataTable()
                                    
                                    background: Rectangle {
                                        radius: 8
                                        color: "#f8f9fa"
                                        border.width: 1
                                        border.color: "#dee2e6"
                                    }
                                }
                            }
                            
                            Column {
                                Layout.fillWidth: true
                                spacing: 3
                                
                                Text {
                                    text: "排序方式"
                                    font.pixelSize: 12  // 放大文字
                                    color: "#6c757d"
                                }
                                
                                ComboBox {
                                    id: sortCombo
                                    width: parent.width
                                    model: ["按日期 ▼", "按代码 A-Z", "按涨跌幅 ▼"]
                                    onCurrentIndexChanged: updateDataTable()
                                    
                                    background: Rectangle {
                                        radius: 8
                                        color: "#f8f9fa"
                                        border.width: 1
                                        border.color: "#dee2e6"
                                    }
                                }
                            }
                            
                            Column {
                                Layout.fillWidth: true
                                spacing: 3
                                
                                Text {
                                    text: "搜索股票"
                                    font.pixelSize: 12  // 放大文字
                                    color: "#6c757d"
                                }
                                
                                TextField {
                                    id: searchField
                                    width: parent.width
                                    placeholderText: "输入代码或名称..."
                                    onTextChanged: searchTimer.restart()
                                    
                                    background: Rectangle {
                                        radius: 8
                                        color: "#f8f9fa"
                                        border.width: 1
                                        border.color: "#dee2e6"
                                    }
                                    
                                    Rectangle {
                                        width: 24
                                        height: 24
                                        radius: 12
                                        color: "#e9ecef"
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        
                                        Text {
                                            text: "🔍"
                                            font.pixelSize: 12
                                            anchors.centerIn: parent
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // 导出配置
                    GroupBox {
                        Layout.fillWidth: true
                        title: "📤 导出设置"
                        topPadding: 30  // 增加顶部内边距，避免标题重叠
                        label: Label {
                            text: parent.title
                            font.bold: true
                            color: "#1a2980"
                        }
                        
                        background: Rectangle {
                            color: "white"
                            radius: 12
                            border.width: 1
                            border.color: "#e9ecef"
                        }
                        
                        ColumnLayout {
                            width: parent.width
                            spacing: 15  // 增加内部间距
                            
                            Column {
                                Layout.fillWidth: true
                                spacing: 3
                                
                                Text {
                                    text: "导出格式"
                                    font.pixelSize: 12  // 放大文字
                                    color: "#6c757d"
                                }
                                
                                ComboBox {
                                    id: exportFormatCombo
                                    width: parent.width
                                    model: ["CSV (.csv)", "Excel (.xlsx)", "JSON (.json)", "数据库"]
                                    
                                    background: Rectangle {
                                        radius: 8
                                        color: "#f8f9fa"
                                        border.width: 1
                                        border.color: "#dee2e6"
                                    }
                                }
                            }
                            
                            Column {
                                Layout.fillWidth: true
                                spacing: 3
                                
                                Text {
                                    text: "文件名"
                                    font.pixelSize: 12  // 放大文字
                                    color: "#6c757d"
                                }
                                
                                TextField {
                                    id: exportFilenameField
                                    width: parent.width
                                    placeholderText: "输入文件名..."
                                    text: "cleaned_data_" + Qt.formatDateTime(new Date(), "yyyyMMdd")
                                    
                                    background: Rectangle {
                                        radius: 8
                                        color: "#f8f9fa"
                                        border.width: 1
                                        border.color: "#dee2e6"
                                    }
                                }
                            }
                            
                            Column {
                                Layout.fillWidth: true
                                spacing: 3
                                
                                Text {
                                    text: "目标系统"
                                    font.pixelSize: 12  // 放大文字
                                    color: "#6c757d"
                                }
                                
                                ComboBox {
                                    id: nextProcessCombo
                                    width: parent.width
                                    model: ["📈 量化分析", "📊 策略回测", "📱 数据仪表盘"]
                                    
                                    background: Rectangle {
                                        radius: 8
                                        color: "#f8f9fa"
                                        border.width: 1
                                        border.color: "#dee2e6"
                                    }
                                }
                            }
                            
                            Button {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 45
                                text: "预览导出结果"
                                onClicked: {
                                    var rowCount = dataTable.count
                                    previewMessage.text = "📋 导出信息预览\n\n" +
                                                        "• 数据量: " + rowCount + " 条记录\n" +
                                                        "• 导出格式: " + exportFormatCombo.currentText + "\n" +
                                                        "• 文件名: " + exportFilenameField.text + "\n" +
                                                        "• 目标系统: " + nextProcessCombo.currentText
                                    previewMessage.open()
                                }
                                
                                background: Rectangle {
                                    radius: 8
                                    color: "#6c757d"
                                    
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8
                                        opacity: parent.parent.pressed ? 0.2 : parent.parent.hovered ? 0.1 : 0
                                        color: "white"
                                    }
                                }
                                
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: true
                                }
                            }
                        }
                    }
                    
                    Item { Layout.fillHeight: true }
                    
                    // 底部导出按钮
                    Button {
                        id: exportButton
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        text: "🚀 开始导出到下一流程"
                        enabled: cleanedData && cleanedData.length > 0
                        
                        onClicked: {
                            if (!cleanedData || cleanedData.length === 0) {
                                warningMessage.text = "⚠️ 导出失败\n\n当前没有可导出的数据，请先运行数据清理流程。"
                                warningMessage.open()
                                return
                            }
                            
                            exportButton.enabled = false
                            exportButton.text = "⏳ 导出中..."
                            
                            exportProgressPopup.show()
                            exportTimer.start()
                        }
                        
                        background: Rectangle {
                            radius: 10
                            gradient: Gradient {
                                GradientStop { 
                                    position: 0.0; 
                                    color: exportButton.enabled ? "#00b09b" : "#b0b0b0" 
                                }
                                GradientStop { 
                                    position: 1.0; 
                                    color: exportButton.enabled ? "#96c93d" : "#c8c8c8" 
                                }
                            }
                            
                            Rectangle {
                                anchors.fill: parent
                                radius: 10
                                opacity: parent.parent.pressed ? 0.3 : parent.parent.hovered && parent.parent.enabled ? 0.2 : 0
                                color: "white"
                            }
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.bold: true
                            font.pixelSize: 14
                        }
                    }
                }
            }
            
            // 右侧：数据表格区域
            Rectangle {
                id: rightPanel
                SplitView.fillWidth: true
                color: "white"
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    
                    // 表格标题栏
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        color: "#f8f9fa"
                        border.width: 1
                        border.color: "#e9ecef"
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            
                            Text {
                                text: "📋 数据预览表格"
                                font.pixelSize: 14
                                font.bold: true
                                color: "#1a2980"
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Text {
                                text: "当前显示: " + dataTable.count + " / " + (cleanedData ? cleanedData.length : 0) + " 条"
                                font.pixelSize: 12
                                color: "#6c757d"
                            }
                        }
                    }
                    
                    // 表格列标题
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        color: "#1a2980"
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 5
                            
                            Text {
                                text: "日期"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 90
                            }
                            
                            Text {
                                text: "代码"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 80
                            }
                            
                            Text {
                                text: "名称"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 75
                            }
                            
                            Text {
                                text: "开盘"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 70
                                horizontalAlignment: Text.AlignRight
                            }
                            
                            Text {
                                text: "收盘"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 70
                                horizontalAlignment: Text.AlignRight
                            }
                            
                            Text {
                                text: "涨跌幅"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 80
                                horizontalAlignment: Text.AlignRight
                            }
                            
                            Text {
                                text: "成交量"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 90
                                horizontalAlignment: Text.AlignRight
                            }
                            
                            Text {
                                text: "操作"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 60
                                horizontalAlignment: Text.AlignCenter
                            }
                        }
                    }
                    
                    // 表格内容
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        
                        ListView {
                            id: dataTable
                            anchors.fill: parent
                            model: ListModel {}
                            boundsBehavior: Flickable.StopAtBounds
                            
                            delegate: Rectangle {
                                width: dataTable.width
                                height: 44
                                color: index % 2 === 0 ? "#ffffff" : "#f8f9fa"
                                
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onContainsMouseChanged: {
                                        if (containsMouse) {
                                            parent.color = "#e3f2fd"
                                        } else {
                                            parent.color = index % 2 === 0 ? "#ffffff" : "#f8f9fa"
                                        }
                                    }
                                }
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16
                                    anchors.rightMargin: 16
                                    spacing: 10
                                    
                                    Text {
                                        text: date || "-"
                                        Layout.preferredWidth: 90
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                    
                                    Text {
                                        text: code || "-"
                                        Layout.preferredWidth: 80
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "#1a2980"
                                    }
                                    
                                    Text {
                                        text: name || "-"
                                        Layout.preferredWidth: 70
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                    
                                    Text {
                                        text: open ? open.toFixed(2) : "-"
                                        Layout.preferredWidth: 70
                                        horizontalAlignment: Text.AlignRight
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                    }
                                    
                                    Text {
                                        text: close ? close.toFixed(2) : "-"
                                        Layout.preferredWidth: 70
                                        horizontalAlignment: Text.AlignRight
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                    }
                                    
                                    Text {
                                        text: {
                                            if (change === undefined || change === null) return "-"
                                            var changeVal = change
                                            return (changeVal > 0 ? "▲ " : changeVal < 0 ? "▼ " : "") + 
                                                   Math.abs(changeVal).toFixed(2) + "%"
                                        }
                                        color: {
                                            if (change === undefined || change === null) return "#6c757d"
                                            return change > 0 ? "#00b09b" : 
                                                   change < 0 ? "#e74c3c" : "#6c757d"
                                        }
                                        font.bold: true
                                        Layout.preferredWidth: 80
                                        horizontalAlignment: Text.AlignRight
                                        font.pixelSize: 12
                                    }
                                    
                                    Text {
                                        text: volume ? (volume / 10000).toFixed(2) + "万" : "-"
                                        Layout.preferredWidth: 90
                                        horizontalAlignment: Text.AlignRight
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                        color: "#6c757d"
                                    }
                                    
                                    Button {
                                        Layout.preferredWidth: 50
                                        Layout.preferredHeight: 28
                                        text: "详情"
                                        
                                        background: Rectangle {
                                            radius: 4
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
                                        
                                        onClicked: {
                                            console.log("查看详情:", code, name)
                                        }
                                    }
                                }
                                
                                // 分隔线
                                Rectangle {
                                    width: parent.width
                                    height: 1
                                    color: "#f0f0f0"
                                    anchors.bottom: parent.bottom
                                }
                            }
                            
                            // 空状态提示
                            Rectangle {
                                visible: dataTable.count === 0
                                anchors.fill: parent
                                color: "#f8f9fa"
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 16
                                    
                                    Text {
                                        text: cleanedData && cleanedData.length > 0 ? "🔍 未找到匹配数据" : "📊 暂无数据"
                                        font.pixelSize: 16
                                        color: "#6c757d"
                                        font.bold: true
                                    }
                                    
                                    Text {
                                        text: cleanedData && cleanedData.length > 0 ? 
                                              "尝试修改搜索条件或调整显示行数" : 
                                              "请先运行数据清理流程获取数据"
                                        font.pixelSize: 12
                                        color: "#adb5bd"
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                        }
                    }
                    
                    // 底部状态栏
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        color: "#f8f9fa"
                        border.width: 1
                        border.color: "#e9ecef"
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            
                            Text {
                                text: "📄 数据源: 股票数据清理系统"
                                font.pixelSize: 11
                                color: "#6c757d"
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Text {
                                text: "🔄 最后更新: " + Qt.formatDateTime(new Date(), "hh:mm:ss")
                                font.pixelSize: 11
                                color: "#6c757d"
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 统计计算函数
    function calculateTimeSpan() {
        if (!cleanedData || cleanedData.length === 0) return 0
        
        var dates = []
        for (var i = 0; i < cleanedData.length; i++) {
            if (cleanedData[i].date) {
                dates.push(cleanedData[i].date)
            }
        }
        
        var uniqueDates = Array.from(new Set(dates))
        return uniqueDates.length
    }
    
    function calculateStockCount() {
        if (!cleanedData || cleanedData.length === 0) return 0
        
        var codes = []
        for (var i = 0; i < cleanedData.length; i++) {
            if (cleanedData[i].code) {
                codes.push(cleanedData[i].code)
            }
        }
        
        var uniqueCodes = Array.from(new Set(codes))
        return uniqueCodes.length
    }
    
    function calculateAvgChange() {
        if (!cleanedData || cleanedData.length === 0) return "0.00"
        
        var sum = 0
        var count = 0
        for (var i = 0; i < cleanedData.length; i++) {
            var change = cleanedData[i].change || 0
            sum += change
            count++
        }
        
        return (sum / count).toFixed(2)
    }
    
    // 更新数据表格
    function updateDataTable() {
        dataTable.model.clear()
        
        if (!cleanedData || cleanedData.length === 0) {
            return
        }
        
        // 应用搜索过滤
        var filteredData = cleanedData
        var searchText = searchField.text.toLowerCase().trim()
        
        if (searchText) {
            filteredData = []
            for (var i = 0; i < cleanedData.length; i++) {
                var item = cleanedData[i]
                if ((item.code && item.code.toLowerCase().includes(searchText)) || 
                    (item.name && item.name.toLowerCase().includes(searchText))) {
                    filteredData.push(item)
                }
            }
        }
        
        // 应用排序
        var sortType = sortCombo.currentIndex
        filteredData.sort(function(a, b) {
            switch(sortType) {
                case 0: // 按日期
                    var dateA = a.date || ""
                    var dateB = b.date || ""
                    return dateB.localeCompare(dateA)
                case 1: // 按代码
                    var codeA = a.code || ""
                    var codeB = b.code || ""
                    return codeA.localeCompare(codeB)
                case 2: // 按涨跌幅
                    var changeA = a.change || 0
                    var changeB = b.change || 0
                    return changeB - changeA
                default:
                    return 0
            }
        })
        
        // 应用行数限制
        var rowCountText = rowCountCombo.currentText
        var limit = rowCountText === "全部数据" ? filteredData.length : parseInt(rowCountText)
        var displayData = filteredData.slice(0, limit)
        
        // 添加到列表模型
        for (var j = 0; j < displayData.length; j++) {
            var item = displayData[j]
            dataTable.model.append({
                date: item.date || "",
                code: item.code || "",
                name: item.name || "",
                open: item.open || 0,
                close: item.close || 0,
                change: item.change || 0,
                volume: item.volume || 0
            })
        }
    }
    
    // 当cleanedData变化时更新表格
    onCleanedDataChanged: {
        updateDataTable()
    }
    
    // 打开弹窗时更新表格
    onOpened: {
        updateDataTable()
    }
    
    // 搜索防抖定时器
    Timer {
        id: searchTimer
        interval: 300
        onTriggered: updateDataTable()
    }
    
    // 导出进度弹窗 - 使用Popup避免上下文问题
    Popup {
        id: exportProgressPopup
        width: 400
        height: 200
        modal: true
        focus: false  // 避免焦点冲突
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        visible: false
        
        property alias progress: exportProgressBar.value
        property alias statusText: exportStatusText.text
        property alias progressPercent: progressPercentText.text
        
        background: Rectangle {
            radius: 16
            color: "white"
            border.width: 1
            border.color: "#e0e0e0"
            
            layer.enabled: true
            layer.effect: DropShadow {
                transparentBorder: true
                radius: 16
                spread: 0.3
                color: "#40000000"
            }
        }
        
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 20
            
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                
                Rectangle {
                    width: 40
                    height: 40
                    radius: 20
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#00b09b" }
                        GradientStop { position: 1.0; color: "#96c93d" }
                    }
                    
                    Text {
                        text: "⏳"
                        font.pixelSize: 18
                        anchors.centerIn: parent
                    }
                }
                
                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    
                    Text {
                        text: "正在导出数据"
                        font.pixelSize: 16
                        font.bold: true
                        color: "#1a2980"
                    }
                    
                    Text {
                        text: "请稍候，正在处理您的数据..."
                        font.pixelSize: 12
                        color: "#6c757d"
                    }
                }
            }
            
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                RowLayout {
                    Text {
                        id: exportStatusText
                        text: "准备导出..."
                        font.pixelSize: 12
                        color: "#6c757d"
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Text {
                        id: progressPercentText
                        text: "0%"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#00b09b"
                    }
                }
                
                ProgressBar {
                    id: exportProgressBar
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    value: 0
                    
                    background: Rectangle {
                        implicitHeight: 8
                        color: "#e9ecef"
                        radius: 4
                    }
                    
                    contentItem: Rectangle {
                        implicitHeight: 8
                        color: "#00b09b"
                        radius: 4
                        width: exportProgressBar.visualPosition * parent.width
                    }
                }
            }
            
            Text {
                text: "📊 " + dataTable.count + " 条数据正在导出到 " + nextProcessCombo.currentText
                font.pixelSize: 11
                color: "#adb5bd"
                Layout.alignment: Qt.AlignHCenter
            }
        }
        
        function show() {
            exportProgressBar.value = 0
            exportStatusText.text = "准备导出..."
            progressPercentText.text = "0%"
            open()
        }
        
        function updateProgress(progress, status) {
            exportProgressBar.value = progress
            exportStatusText.text = status
            progressPercentText.text = Math.round(progress * 100) + "%"
        }
    }
    
    // 导出计时器
    Timer {
        id: exportTimer
        interval: 200
        repeat: true
        property int step: 0
        
        onTriggered: {
            step++
            var progress = step / 10
            
            exportProgressPopup.updateProgress(progress, 
                step <= 3 ? "准备数据..." :
                step <= 6 ? "格式化数据..." :
                step <= 8 ? "保存文件..." :
                "完成导出")
            
            if (step >= 10) {
                stop()
                
                Qt.callLater(function() {
                    exportProgressPopup.close()
                    dataPreviewModal.exportCompleted()
                    exportButton.text = "✅ 导出完成"
                    
                    // 显示完成消息
                    completeMessage.text = "🎉 数据导出成功！\n\n" +
                                          "✓ 已导出 " + dataTable.count + " 条数据\n" +
                                          "✓ 导出格式: " + exportFormatCombo.currentText + "\n" +
                                          "✓ 文件名: " + exportFilenameField.text + "\n" +
                                          "✓ 目标系统: " + nextProcessCombo.currentText + "\n\n" +
                                          "数据已准备好进行下一流程分析。"
                    completeMessage.open()
                })
            }
        }
    }
    
    // 预览消息弹窗 - 使用Popup避免上下文问题
    Popup {
        id: previewMessage
        width: 420
        height: 200
        modal: true
        focus: false  // 避免焦点冲突
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        visible: false
        
        property string text: ""
        
        background: Rectangle {
            radius: 16
            color: "white"
            border.width: 1
            border.color: "#e0e0e0"
            
            layer.enabled: true
            layer.effect: DropShadow {
                transparentBorder: true
                radius: 16
                spread: 0.3
                color: "#40000000"
            }
        }
        
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 20
            
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                
                Rectangle {
                    width: 40
                    height: 40
                    radius: 20
                    color: "#e3f2fd"
                    
                    Text {
                        text: "📋"
                        font.pixelSize: 18
                        anchors.centerIn: parent
                    }
                }
                
                Text {
                    text: "导出预览"
                    font.pixelSize: 18
                    font.bold: true
                    color: "#1a2980"
                }
            }
            
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                
                Text {
                    text: previewMessage.text
                    color: "#6c757d"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                }
            }
            
            Button {
                text: "确定"
                Layout.alignment: Qt.AlignRight
                onClicked: previewMessage.close()
                
                background: Rectangle {
                    radius: 8
                    color: "#1a2980"
                }
                
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: true
                }
            }
        }
    }
    
    // 完成消息弹窗 - 使用Popup避免上下文问题
    Popup {
        id: completeMessage
        width: 450
        height: 240
        modal: true
        focus: false  // 避免焦点冲突
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        visible: false
        
        property string text: ""
        
        background: Rectangle {
            radius: 16
            color: "white"
            border.width: 1
            border.color: "#e0e0e0"
            
            layer.enabled: true
            layer.effect: DropShadow {
                transparentBorder: true
                radius: 16
                spread: 0.3
                color: "#40000000"
            }
        }
        
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 20
            
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                
                Rectangle {
                    width: 48
                    height: 48
                    radius: 24
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#00b09b" }
                        GradientStop { position: 1.0; color: "#96c93d" }
                    }
                    
                    Text {
                        text: "✓"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 24
                        anchors.centerIn: parent
                    }
                }
                
                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    
                    Text {
                        text: "导出完成"
                        font.pixelSize: 20
                        font.bold: true
                        color: "#00b09b"
                    }
                    
                    Text {
                        text: "数据已成功导出到下一流程"
                        font.pixelSize: 12
                        color: "#6c757d"
                    }
                }
            }
            
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                
                Text {
                    text: completeMessage.text
                    color: "#6c757d"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    lineHeight: 1.4
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                
                Button {
                    text: "继续导出"
                    Layout.fillWidth: true
                    onClicked: {
                        completeMessage.close()
                        exportButton.enabled = true
                        exportButton.text = "🚀 开始导出到下一流程"
                        exportTimer.step = 0
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: "#e3f2fd"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "#1976d2"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.bold: true
                    }
                }
                
                Button {
                    text: "完成"
                    Layout.fillWidth: true
                    onClicked: {
                        completeMessage.close()
                        exportButton.enabled = true
                        exportButton.text = "🚀 开始导出到下一流程"
                        exportTimer.step = 0
                        dataPreviewModal.close()
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: "#1a2980"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.bold: true
                    }
                }
            }
        }
    }
    
    // 警告消息弹窗 - 使用Popup避免上下文问题
    Popup {
        id: warningMessage
        width: 380
        height: 180
        modal: true
        focus: false  // 避免焦点冲突
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        visible: false
        
        property string text: ""
        
        background: Rectangle {
            radius: 16
            color: "white"
            border.width: 1
            border.color: "#e0e0e0"
            
            layer.enabled: true
            layer.effect: DropShadow {
                transparentBorder: true
                radius: 16
                spread: 0.3
                color: "#40000000"
            }
        }
        
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 20
            
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                
                Rectangle {
                    width: 40
                    height: 40
                    radius: 20
                    color: "#fff3e0"
                    
                    Text {
                        text: "⚠️"
                        font.pixelSize: 18
                        anchors.centerIn: parent
                    }
                }
                
                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    
                    Text {
                        text: "系统提示"
                        font.pixelSize: 18
                        font.bold: true
                        color: "#ff9800"
                    }
                    
                    Text {
                        text: "无法执行操作"
                        font.pixelSize: 12
                        color: "#6c757d"
                    }
                }
            }
            
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                
                Text {
                    text: warningMessage.text
                    color: "#6c757d"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            
            Button {
                text: "确定"
                Layout.alignment: Qt.AlignHCenter
                onClicked: warningMessage.close()
                
                background: Rectangle {
                    radius: 8
                    color: "#ff9800"
                }
                
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: true
                }
            }
        }
    }
}
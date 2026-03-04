import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import AStock.Bridge 1.0

Popup {
    id: dataPreviewModal
    width: Math.min(parent ? parent.width * 0.9 : 800, 900)
    height: Math.min(parent ? parent.height * 0.9 : 600, 700)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    
    // 使用外部传递的预览数据模型
    property var previewDataModel: dataCleaningService ? dataCleaningService.previewDataModel : null
    property var dataCleaningService: null
    
    signal exportCompleted()
    
    // 数据预览服务 - 用于从缓存加载清洗结果
    DataPreviewService {
        id: dataPreviewService
        
        // 将预览模型与预览服务关联
        previewModel: previewDataModel
        
        onPreviewGenerated: function(success, message, stats) {
            console.log("DataPreviewModal: 数据预览生成，成功:", success, "消息:", message, "统计:", JSON.stringify(stats))
            // 注意：现在数据已由C++层的DataPreviewService直接更新到预览模型
            // 无需在QML层手动获取和设置数据
            console.log("DataPreviewModal: 数据已由C++服务直接更新到预览模型")
        }
    }
    
    contentItem: ColumnLayout {
        spacing: 0
        
        // 标题栏 - 与DataSourceModal对齐
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
                    text: "股票数据预览与导出"
                    font.pixelSize: 16
                    font.bold: true
                    color: "white"
                    Layout.leftMargin: 8
                }
                
                Item { Layout.fillWidth: true }
                
                // 状态指示器 - 保持原有功能但调整样式
                Rectangle {
                    width: 110
                    height: 24
                    radius: 12
                    color: "#ffffff20"
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 4
                        
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: previewDataModel.count > 0 ? "#00b09b" : "#f59e0b"
                        }
                        
                        Label {
                            text: previewDataModel.count > 0 ? "数据就绪" : "无数据"
                            font.pixelSize: 10
                            color: "white"
                            Layout.fillWidth: true
                        }
                    }
                }
                
                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: "transparent"
                    
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dataPreviewModal.close()
                        
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
                            color: parent.containsMouse ? "#ffffff20" : "transparent"
                        }
                    }
                }
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
        radius: 12
        color: "#ffffff"
        border.width: 1
        border.color: "#e5e7eb"
    }
                        
                        GridLayout {
                            width: parent.width
                            columns: 4  // 水平排列4个统计卡片
                            columnSpacing: 8  // 列间距
                            rowSpacing: 8     // 行间距
                            
                            Repeater {
                                model: [
                                    {icon: "📊", title: "数据总量", value: previewDataModel.count, unit: "条", color: "#3498db", bg: "#e3f2fd"},
                                    {icon: "📅", title: "时间跨度", value: dataPreviewService.calculateTimeSpanFromModel(), unit: "天", color: "#2ecc71", bg: "#e8f5e9"},
                                    {icon: "🏢", title: "股票数量", value: dataPreviewService.calculateStockCountFromModel(), unit: "只", color: "#e74c3c", bg: "#ffebee"}, //股票数量数字不对
                                    {icon: "📉", title: "平均涨跌", value: dataPreviewService.calculateAvgChangeFromModel(), unit: "%", color: "#9b59b6", bg: "#f3e5f5"}
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
                                                text: modelData.value.toFixed(2)
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
                    
                    // 缓存数据集选择
                    GroupBox {
                        Layout.fillWidth: true
                        title: "💾 缓存数据集"
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
                                    text: "选择数据集"
                                    font.pixelSize: 12  // 放大文字
                                    color: "#6c757d"
                                }
                                
                                ComboBox {
                                    id: datasetCombo
                                    width: parent.width
                                    model: dataPreviewService.dataSetInfos
                                    textRole: "displayName"
                                    
                                    onActivated: {
                                        var selectedDataset = dataPreviewService.dataSetInfos[currentIndex]
                                        if (selectedDataset && selectedDataset.id !== undefined) {
                                            console.log("DataPreviewModal: 选择数据集:", selectedDataset.displayName, "ID:", selectedDataset.id)
                                            dataPreviewService.loadDataSetById(selectedDataset.id)
                                        }
                                    }
                                    
                                    background: Rectangle {
                                        radius: 8
                                        color: "#f8f9fa"
                                        border.width: 1
                                        border.color: "#dee2e6"
                                    }
                                }
                                
                                Button {
                                    width: parent.width
                                    height: 28
                                    text: "🔄 刷新列表"
                                    onClicked: dataPreviewService.refreshDataSetList()

                                    background: Rectangle {
                                        radius: 8
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
                            }

                            // 当dataSetInfos属性变化时自动更新ComboBox
                            Connections {
                                target: dataPreviewService
                                function onDataSetInfosChanged() {
                                    console.log("DataPreviewModal: 数据集列表已更新，数量:", dataPreviewService.dataSetInfos.length)
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
                                    onCurrentIndexChanged: filterData()
                                    
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
                                    onCurrentIndexChanged: filterData()
                                    
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
                            spacing: 5  // 增加内部间距
                            
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
                                Layout.preferredHeight: 38
                                text: "预览导出结果"
                                onClicked: {
                                    var rowCount = dataTable.count
                                    previewMessage.text = "📋 导出信息预览\n\n" +
                                                        "• 数据量: " + rowCount + " 条记录\n" +
                                                        "• 导出格式: " + exportFormatCombo.currentText + "\n" +
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
                    
                    // 状态栏 - 与DataSourceModal对齐
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
                    
                    // 底部导出按钮 - 与DataSourceModal对齐
                    Button {
                        id: exportButton
                        Layout.fillWidth: true
                        height: 34
                        text: "🚀 开始导出到下一流程"
                        enabled: previewDataModel.count > 0
                        
                        onClicked: {
                            if (previewDataModel.count === 0) {
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
                            radius: 6
                            color: exportButton.enabled ? "#10b981" : "#b0b0b0"
                            
                            Rectangle {
                                anchors.fill: parent
                                radius: 6
                                opacity: parent.parent.pressed ? 0.3 : parent.parent.hovered && parent.parent.enabled ? 0.2 : 0
                                color: "white"
                            }
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 13
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
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
                                text: "当前显示: " + dataTable.count + " / " + previewDataModel.count + " 条"
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
                                Layout.preferredWidth: 80
                                horizontalAlignment: Text.AlignRight
                            }
                            
                            Text {
                                text: "代码"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 80
                                horizontalAlignment: Text.AlignRight
                            }
                            
                            Text {
                                text: "名称"
                                color: "white"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.preferredWidth: 65
                                horizontalAlignment: Text.AlignRight
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
                            model: previewDataModel  // 直接绑定到PreviewDataModel
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
                                        text: model.date || "-"
                                        Layout.preferredWidth: 90
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                    
                                    Text {
                                        text: model.code || "-"
                                        Layout.preferredWidth: 80
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "#1a2980"
                                        horizontalAlignment: Text.AlignCenter
                                    }
                                    
                                    Text {
                                        text: model.name 
                                        Layout.preferredWidth: 70
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignCenter
                                    }
                                    
                                    Text {
                                        text: model.open ? model.open.toFixed(2) : "-"
                                        Layout.preferredWidth: 70
                                        horizontalAlignment: Text.AlignRight
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                        
                                    }
                                    
                                    Text {
                                        text: model.close ? model.close.toFixed(2) : "-"
                                        Layout.preferredWidth: 70
                                        horizontalAlignment: Text.AlignRight
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                        
                                    }
                                    
                                    Text {
                                        text: {
                                            if (model.change === undefined || model.change === null) return "-"
                                            var changeVal = model.change
                                            return (changeVal > 0 ? "▲ " : changeVal < 0 ? "▼ " : "") + 
                                                   Math.abs(changeVal).toFixed(2) + "%"
                                        }
                                        color: {
                                            if (model.change === undefined || model.change === null) return "#6c757d"
                                            return model.change > 0 ? "#00b09b" : 
                                                   model.change < 0 ? "#e74c3c" : "#6c757d"
                                        }
                                        font.bold: true
                                        Layout.preferredWidth: 80
                                        horizontalAlignment: Text.AlignRight
                                        font.pixelSize: 12
                                    }
                                    
                                    Text {
                                        text: model.volume ? (model.volume / 10000).toFixed(2) + "万" : "-"
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
                                            console.log("查看详情:", model.code, model.name)
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
                                        text: previewDataModel.count > 0 ? "🔍 未找到匹配数据" : "📊 暂无数据"
                                        font.pixelSize: 16
                                        color: "#6c757d"
                                        font.bold: true
                                    }
                                    
                                    Text {
                                        text: previewDataModel.count > 0 ? 
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
    // 简单的搜索过滤函数
    function filterData() {
        try {
            var searchText = searchField.text.toLowerCase().trim()
            console.log("DataPreviewModal: 应用搜索过滤，关键词:", searchText)
            
            // 这里只是更新状态，实际的过滤需要通过代理模型或其他方式实现
            // 目前先保持简单，直接显示所有数据
            // 未来可以添加SortFilterProxyModel来实现真正的过滤
            
            // 更新显示状态
            var displayCount = Math.min(previewDataModel.count, getDisplayLimit())
            console.log("DataPreviewModal: 显示数据量:", displayCount, "，总数据量:", previewDataModel.count)
        } catch (error) {
            console.error("DataPreviewModal: filterData函数发生错误:", error)
        }
    }
    
    // 获取显示行数限制
    function getDisplayLimit() {
        var rowCountText = rowCountCombo.currentText
        return rowCountText === "全部数据" ? previewDataModel.count : parseInt(rowCountText)
    }
    
    
    
    // 搜索防抖定时器
    Timer {
        id: searchTimer
        interval: 300
        onTriggered: filterData()
    }
    
    // 导出进度弹窗
    Popup {
        id: exportProgressPopup
        width: 400
        height: 200
        modal: true
        focus: false
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
                                          "✓ 文件名: " + "data_export_" + Qt.formatDateTime(new Date(), "yyyyMMdd_hhmmss") + 
                                            (exportFormatCombo.currentText.includes("CSV") ? ".csv" : 
                                             exportFormatCombo.currentText.includes("Excel") ? ".xlsx" :
                                             exportFormatCombo.currentText.includes("JSON") ? ".json" : ".txt") + "\n" +
                                          "✓ 目标系统: " + nextProcessCombo.currentText + "\n\n" +
                                          "数据已准备好进行下一流程分析。"
                    completeMessage.open()
                })
            }
        }
    }
    
    // 预览消息弹窗
    Popup {
        id: previewMessage
        width: 420
        height: 200
        modal: true
        focus: false
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
    
    // 完成消息弹窗
    Popup {
        id: completeMessage
        width: 450
        height: 240
        modal: true
        focus: false
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
    
    // 警告消息弹窗
    Popup {
        id: warningMessage
        width: 380
        height: 180
        modal: true
        focus: false
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
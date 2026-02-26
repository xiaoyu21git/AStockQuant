import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0

Popup {
    id: dataCleaningModal
    width: 850
    height: 650
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    
    property var rules: ({})
    
    // 数据统计属性 - 从C++获取
    property int originalDataCount: 0
    property int filteredDataCount: 0
    property int cleanedDataCount: 0
    property int removedDataCount: 0
    property string processingTime: "0.0s"
    
    // C++模型引用 - 通过DataFetchController访问
    property var dataFetchController: null
    property var cleaningResultModel: dataFetchController ? dataFetchController.cleaningResultModel : null
    
    signal cleaningRequested(var rules)
    signal cleaningCompleted()
    
    background: Rectangle {
        radius: 20
        color: "white"
        border.width: 1
        border.color: "#e0e0e0"
    }
    
    contentItem: ColumnLayout {
        spacing: 0
        
        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 50
            color: "#1a2980"
            radius: 20
            z: 1
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                
                Label {
                    text: "股票数据清洗"
                    font.pixelSize: 22
                    font.bold: true
                    color: "white"
                }
                
                Item {
                    Layout.fillWidth: true
                }
                
                Rectangle {
                    width: 140
                    height: 30
                    radius: 15
                    color: "#00b09b"
                    
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 5
                        
                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            color: "white"
                        }
                        
                        Label {
                            text: "就绪"
                            font.pixelSize: 12
                            color: "white"
                        }
                    }
                }
                
                Button {
                    text: "×"
                    font.pixelSize: 24
                    flat: true
                    onClicked: dataCleaningModal.close()
                    
                    background: Rectangle {
                        color: "transparent"
                    }
                }
            }
        }
        
        // 内容区域 - 使用ColumnLayout作为直接子元素
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8  // 略微减小整体间距
            Layout.margins: 12  // 略微减小边距
            
            // 清洗统计 - 优化：缩小文字比例
            GroupBox {
                title: "清洗统计"
                Layout.fillWidth: true
                Layout.preferredHeight: 90  // 略微降低高度
                Layout.maximumHeight: 90
                clip: true
                
                // 标题文字也缩小
                label: Text {
                    text: "清洗统计"
                    font.pixelSize: 13  // 缩小标题
                    color: "#1a2980"
                    leftPadding: 10
                    topPadding: 5
                }
                
                Grid {
                    anchors.centerIn: parent
                    columns: 4
                    rowSpacing: 8  // 减小间距
                    columnSpacing: 12  // 减小间距
                    
                    Repeater {
                        model: [
                            {title: "原始数据", value: originalDataCount.toString(), unit: "数据行数", color: "#3498db"},
                            {title: "清洗后数据", value: cleanedDataCount.toString(), unit: "数据行数", color: "#2ecc71"},
                            {title: "移除数据", value: removedDataCount.toString(), unit: "数据行数", color: "#e74c3c"},
                            {title: "处理时间", value: processingTime, unit: "清洗用时", color: "#9b59b6"}
                        ]
                        
                        Rectangle {
                            width: 90  // 略微缩小宽度
                            height: 75  // 略微缩小高度
                            radius: 6
                            color: "#f8f9fa"
                            border.width: 1
                            border.color: "#e9ecef"
                            
                            Column {
                                anchors.centerIn: parent
                                spacing: 3  // 大幅减小内部间距
                                
                                Label {
                                    text: modelData.title
                                    font.pixelSize: 10  // 缩小字体
                                    color: "#6c757d"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                                
                                Label {
                                    text: modelData.value
                                    font.pixelSize: 18  // 缩小主数字
                                    font.bold: true
                                    color: modelData.color
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                                
                                Label {
                                    text: modelData.unit
                                    font.pixelSize: 8  // 缩小单位
                                    color: "#6c757d"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }
                        }
                    }
                }
            }
            
            // 清洗状态 - 优化：缩小文字和间距
            GroupBox {
                title: "清洗状态"
                Layout.fillWidth: true
                Layout.preferredHeight: 85  // 略微降低高度
                Layout.maximumHeight: 85
                clip: true
                
                label: Text {
                    text: "清洗状态"
                    font.pixelSize: 13  // 缩小标题
                    color: "#1a2980"
                    leftPadding: 10
                    topPadding: 5
                }
                
                Column {
                    anchors.fill: parent
                    anchors.margins: 8  // 减小内边距
                    spacing: 5  // 减小间距
                    
                    // 进度条和百分比 - 优化间距
                    RowLayout {
                        width: parent.width
                        spacing: 5  // 大幅减小间距
                        
                        ProgressBar {
                            id: progressBar
                            Layout.fillWidth: true
                            Layout.preferredHeight: 8  // 降低进度条高度
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
                                width: progressBar.visualPosition * parent.width
                            }
                        }
                        
                        Text {
                            id: progressPercentText
                            text: "0%"
                            font.pixelSize: 11  // 缩小字体
                            font.bold: true
                            color: "#00b09b"
                            width: 40  // 减小宽度
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    
                    // 状态描述 - 优化文字大小和间距
                    Column {
                        width: parent.width
                        spacing: 2  // 大幅减小间距
                        
                        Text {
                            id: progressText
                            text: "等待开始清洗"
                            font.pixelSize: 12  // 缩小字体
                            color: "#3498db"
                            width: parent.width
                            wrapMode: Text.WordWrap
                            maximumLineCount: 1  // 限制为单行
                            elide: Text.ElideRight
                        }
                        
                        Text {
                            id: progressDetail
                            text: "就绪状态"
                            font.pixelSize: 10  // 缩小字体
                            color: "#6c757d"
                            width: parent.width
                            wrapMode: Text.WordWrap
                            maximumLineCount: 1  // 限制为单行
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            
            // 清洗结果预览 - 优化：缩小文字
            GroupBox {
                title: "清洗结果预览"
                Layout.fillWidth: true
                Layout.preferredHeight: 160  // 略微降低高度
                Layout.maximumHeight: 160
                clip: true
                
                label: Text {
                    text: "清洗结果预览"
                    font.pixelSize: 13  // 缩小标题
                    color: "#1a2980"
                    leftPadding: 10
                    topPadding: 5
                }
                
                Column {
                    anchors.fill: parent
                    spacing: 3  // 减小间距
                    
                    // 表格容器
                    Rectangle {
                        width: parent.width
                        height: 130  // 略微减小高度
                        radius: 6
                        border.width: 1
                        border.color: "#e0e0e0"
                        clip: true
                        
                        ListView {
                            id: cleaningResultList
                            anchors.fill: parent
                            model: cleaningResultModel
                            clip: true
                            
                            // 表头
                            header: Rectangle {
                                width: cleaningResultList.width
                                height: 28  // 降低表头高度
                                color: "#1a2980"
                                
                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 4
                                    anchors.rightMargin: 4
                                    spacing: 5  // 减小间距
                                    
                                    Text {
                                        text: "日期"
                                        color: "white"
                                        font.pixelSize: 10  // 缩小字体
                                        font.bold: true
                                        width: 70  // 减小宽度
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: "代码"
                                        color: "white"
                                        font.pixelSize: 10
                                        font.bold: true
                                        width: 60
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: "名称"
                                        color: "white"
                                        font.pixelSize: 10
                                        font.bold: true
                                        width: 90
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: "收盘价"
                                        color: "white"
                                        font.pixelSize: 10
                                        font.bold: true
                                        width: 60
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: "涨跌幅"
                                        color: "white"
                                        font.pixelSize: 10
                                        font.bold: true
                                        width: 60
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: "成交量"
                                        color: "white"
                                        font.pixelSize: 10
                                        font.bold: true
                                        width: 60
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                            
                            delegate: Rectangle {
                                width: cleaningResultList.width
                                height: 24  // 降低行高
                                color: index % 2 === 0 ? "#ffffff" : "#f8f9fa"
                                
                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 4
                                    anchors.rightMargin: 4
                                    spacing: 5
                                    
                                    Text {
                                        text: model.date
                                        width: 70
                                        font.pixelSize: 9  // 缩小字体
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: model.code
                                        width: 60
                                        font.pixelSize: 9
                                        font.bold: true
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: model.name
                                        width: 90
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: model.close.toFixed(2)
                                        width: 60
                                        font.pixelSize: 9
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: (model.change > 0 ? "+" : "") + model.change.toFixed(2) + "%"
                                        color: model.change > 0 ? "#00b09b" : model.change < 0 ? "#e74c3c" : "#6c757d"
                                        width: 60
                                        font.pixelSize: 9
                                        font.bold: true
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    Text {
                                        text: (model.volume / 10000).toFixed(2) + "万"
                                        width: 60
                                        font.pixelSize: 9
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }
                    }
                    
                    // 数据统计行 - 优化文字大小
                    Row {
                        width: parent.width
                        spacing: 8
                        
                        Text {
                            text: "总记录数: " + (cleaningResultModel ? cleaningResultModel.count : 0)
                            color: "#6c757d"
                            font.pixelSize: 9  // 缩小字体
                        }
                        
                        Text {
                            text: "显示前 " + (cleaningResultModel ? cleaningResultModel.count : 0) + " 条"
                            color: "#6c757d"
                            font.pixelSize: 9  // 缩小字体
                        }
                    }
                }
            }
            
            // 按钮区域 - 优化：缩小按钮
            RowLayout {
                Layout.fillWidth: true
                spacing: 15  // 减小间距
                Layout.topMargin: 5  // 增加上边距微调
                
                Button {
                    text: "预览清洗效果"
                    Layout.fillWidth: true
                    height: 38  // 降低按钮高度
                    
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12  // 缩小按钮文字
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    background: Rectangle {
                        radius: 6
                        color: "#6c757d"
                    }
                    
                    onClicked: {
                        if (cleaningResultModel && cleaningResultModel.count > 0) {
                            console.log("显示已清洗的数据，条数:", cleaningResultModel.count)
                            previewMessageText.text = "显示清洗后的数据 (" + cleaningResultModel.count + "条)"
                            previewMessage.open()
                        } else {
                            noCleaningDataPrompt.open()
                        }
                    }
                }
                
                Button {
                    id: executeButton
                    text: "执行数据清洗"
                    Layout.fillWidth: true
                    height: 38  // 降低按钮高度
                    
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12  // 缩小按钮文字
                        font.bold: true
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    background: Rectangle {
                        radius: 6
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#00b09b" }
                            GradientStop { position: 1.0; color: "#96c93d" }
                        }
                    }
                    
                    onClicked: {
                        if (executeButton.text === "查看清洗结果") {
                            console.log("打开清洗结果详情")
                            if (completeMessage) {
                                completeMessage.open()
                            }
                        } else {
                            startCleaning()
                        }
                    }
                }
            }
        }
    }
    
    // 函数定义（保持不变）
    function updateStatsFromMessage(message) {
        console.log("收到统计消息:", message)
        
        var detailedMatch = message.match(/数据清洗完成: 共(\d+)条，有效(\d+)条，跳过(\d+)条，保留(\d+)条，移除(\d+)条/)
        if (detailedMatch) {
            originalDataCount = parseInt(detailedMatch[1]) || 0
            cleanedDataCount = parseInt(detailedMatch[4]) || 0
            removedDataCount = parseInt(detailedMatch[5]) || 0
            console.log("详细格式: 原始=" + originalDataCount + ", 清洗后=" + cleanedDataCount + ", 移除=" + removedDataCount)
            return
        }
        
        var completionMatch = message.match(/处理(\d+)条.*保留(\d+)条.*移除(\d+)条/)
        if (completionMatch) {
            originalDataCount = parseInt(completionMatch[1]) || 0
            cleanedDataCount = parseInt(completionMatch[2]) || 0
            removedDataCount = parseInt(completionMatch[3]) || 0
            console.log("完成格式: 原始=" + originalDataCount + ", 清洗后=" + cleanedDataCount + ", 移除=" + removedDataCount)
            return
        }
        
        var progressMatch = message.match(/正在清洗: (\d+)\/(\d+).*有效: (\d+).*保留: (\d+).*移除: (\d+)/)
        if (progressMatch) {
            cleanedDataCount = parseInt(progressMatch[4]) || 0
            removedDataCount = parseInt(progressMatch[5]) || 0
            console.log("进度格式: 清洗后=" + cleanedDataCount + ", 移除=" + removedDataCount)
            return
        }
        
        var statsMatch = message.match(/进度: \d+%.*已处理: (\d+).*保留: (\d+).*移除: (\d+)/)
        if (statsMatch) {
            cleanedDataCount = parseInt(statsMatch[2]) || 0
            removedDataCount = parseInt(statsMatch[3]) || 0
            console.log("统计格式: 清洗后=" + cleanedDataCount + ", 移除=" + removedDataCount)
            return
        }
        
        var startMatch = message.match(/开始数据清洗，共(\d+)条记录/)
        if (startMatch) {
            originalDataCount = parseInt(startMatch[1]) || 0
            console.log("开始格式: 总记录=" + originalDataCount)
            return
        }
        
        if (message.includes("数据清洗完成")) {
            console.log("清洗完成消息")
            return
        }
        
        console.log("未识别的消息格式:", message)
    }
    
    function updateProgress(progress, message) {
        var validProgress = Math.max(0, Math.min(100, progress))
        
        progressBar.value = validProgress / 100
        progressPercentText.text = validProgress + "%"
        
        if (message && message.length > 0) {
            if (message.includes(validProgress + "%") || message.includes("进度")) {
                progressText.text = message
                progressDetail.text = ""
            } else {
                progressText.text = message
                progressDetail.text = "当前进度: " + validProgress + "%"
            }
        } else {
            progressText.text = "数据清洗中..."
            progressDetail.text = "已完成: " + validProgress + "%"
        }
        
        console.log("清洗进度: " + validProgress + "%, 消息: " + (message || ""))
        
        if (validProgress === 100) {
            console.log("数据清洗完成")
            progressText.text = "数据清洗完成"
            progressDetail.text = "总共处理 " + originalDataCount + " 条数据"
            executeButton.text = "查看清洗结果"
            executeButton.background.gradient = null
            executeButton.background.color = "#4caf50"
        }
        
        if (message && message.length > 0) {
            try {
                updateStatsFromMessage(message)
            } catch (error) {
                console.error("解析统计信息时出错:", error)
            }
        }
        
        return validProgress
    }
    
    function updateStats(originalCount, filteredCount, cleanedCount, processTime) {
        console.log("统计信息: 原始=" + (originalCount || 0) + 
                   ", 筛选=" + (filteredCount || 0) + 
                   ", 清洗=" + (cleanedCount || 0) + 
                   ", 时间=" + (processTime || "0.0s"))
    }
    
    function showCleaningResults(cleanedData) {
        console.log("清洗结果数据条数:", cleanedData ? cleanedData.length : 0)
    }
    
    function showCompletionMessage() {
        console.log("数据清洗完成")
        console.log("清洗统计: 原始=" + originalDataCount + ", 清洗后=" + cleanedDataCount + ", 移除=" + removedDataCount + ", 时间=" + processingTime)
        
        try {
            cleaningCompleted()
            console.log("清洗完成信号已发送")
        } catch (error) {
            console.error("发送清洗完成信号时出错:", error)
        }
    }
    
    function startCleaning() {
        console.log("开始数据清洗")
        console.log("清洗开始，使用规则:", JSON.stringify(rules))
        cleaningRequested(rules)
    }
    
    // Popup 定义（保持不变，但可适当缩小内部文字）
    Popup {
        id: previewMessage
        width: 280  // 略微缩小
        height: 110
        modal: true
        closePolicy: Popup.CloseOnEscape
        
        background: Rectangle {
            radius: 10
            color: "white"
            border.width: 1
            border.color: "#e0e0e0"
        }
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 15
            
            Label {
                text: "预览提示"
                font.bold: true
                font.pixelSize: 14  // 缩小
                Layout.alignment: Qt.AlignHCenter
            }
            
            Label {
                id: previewMessageText
                text: "显示已清洗的数据"
                font.pixelSize: 12  // 缩小
                Layout.alignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            
            Button {
                text: "确定"
                Layout.alignment: Qt.AlignHCenter
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 12
                }
                onClicked: previewMessage.close()
            }
        }
    }
    
    Popup {
        id: noCleaningDataPrompt
        width: 320
        height: 160
        modal: true
        closePolicy: Popup.CloseOnEscape
        
        background: Rectangle {
            radius: 10
            color: "white"
            border.width: 1
            border.color: "#e0e0e0"
        }
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 8
            
            Label {
                text: "提示"
                font.bold: true
                font.pixelSize: 15
                Layout.alignment: Qt.AlignHCenter
            }
            
            Label {
                text: "当前没有清洗后的数据，请先执行数据清洗。"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            
            Label {
                text: "您希望："
                font.bold: true
                font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                
                Button {
                    text: "先清洗再预览"
                    Layout.fillWidth: true
                    height: 32
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.bold: true
                    }
                    
                    background: Rectangle {
                        radius: 5
                        color: "#00b09b"
                    }
                    
                    onClicked: {
                        noCleaningDataPrompt.close()
                        startCleaning()
                    }
                }
                
                Button {
                    text: "预览原始数据"
                    Layout.fillWidth: true
                    height: 32
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.bold: true
                    }
                    
                    background: Rectangle {
                        radius: 5
                        color: "#6c757d"
                    }
                    
                    onClicked: {
                        noCleaningDataPrompt.close()
                        cleaningRequested(rules)
                    }
                }
            }
            
            Button {
                text: "取消"
                Layout.alignment: Qt.AlignHCenter
                flat: true
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 11
                }
                onClicked: noCleaningDataPrompt.close()
            }
        }
    }
    
    Popup {
        id: completeMessage
        width: 350
        height: 100
        modal: true
        closePolicy: Popup.CloseOnEscape
        
        background: Rectangle {
            radius: 10
            color: "white"
            border.width: 1
            border.color: "#e0e0e0"
        }
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 15
            
            Label {
                text: "清洗完成"
                font.bold: true
                font.pixelSize: 14
                Layout.alignment: Qt.AlignHCenter
            }
            
            Label {
                id: completeMessageText
                font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
            }
            
            Button {
                text: "确定"
                Layout.alignment: Qt.AlignHCenter
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 12
                }
                onClicked: completeMessage.close()
            }
        }
    }
}
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
                        text: "🧹"
                        font.pixelSize: 16
                        anchors.centerIn: parent
                    }
                }
                
                Label {
                    text: "股票数据清洗"
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
                            color: "#00b09b"
                        }
                        
                        Label {
                            text: "就绪"
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
                        onClicked: dataCleaningModal.close()
                        
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
            
            // 按钮区域 - 执行按钮（已移除预览功能）
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
                    startCleaning()
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
            executeButton.text = "执行数据清洗"
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
    
    
    // 清洗完成提示弹窗
    
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
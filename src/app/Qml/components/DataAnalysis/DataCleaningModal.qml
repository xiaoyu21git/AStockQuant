import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Engine 1.0

Popup {
    id: dataCleaningModal
    width: 900
    height: 700
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    
    property var rules: ({})
    
    // 数据统计属性 - 从C++获取
    property int originalDataCount: 0
    property int filteredDataCount: 0
    property int cleanedDataCount: 0
    property string processingTime: "0.0s"
    
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
    
    // 从消息中解析统计信息的函数
    function updateStatsFromMessage(message) {
        // 解析原始数据数量
        var originalMatch = message.match(/原始 (\d+) 条/)
        if (originalMatch) {
            originalDataCount = parseInt(originalMatch[1])
        }
        
        // 解析清洗后数据数量
        var cleanedMatch = message.match(/清洗后 (\d+) 条/)
        if (cleanedMatch) {
            cleanedDataCount = parseInt(cleanedMatch[1])
        }
        
        // 解析移除数据数量
        var removedMatch = message.match(/移除 (\d+) 条/)
        if (removedMatch) {
            var removedCount = parseInt(removedMatch[1])
            filteredDataCount = originalDataCount - removedCount
        }
        
        // 解析处理时间
        var timeMatch = message.match(/(\d+)ms/)
        if (timeMatch) {
            var ms = parseInt(timeMatch[1])
            processingTime = (ms / 1000).toFixed(2) + "s"
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
        
        // 内容区域
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: parent.width
            clip: true
            
            ColumnLayout {
                width: parent.width
                spacing: 5
                //padding: 30
                
                // 清洗统计 - 修复溢出问题
GroupBox {
    title: "清洗统计"
    Layout.fillWidth: true
    Layout.preferredHeight: 100  // ✅ 设置固定高度
    Layout.maximumHeight: 100    // ✅ 限制最大高度
    clip: true  // ✅ 裁剪溢出内容
    // 关键设置：限制内容宽度
    contentWidth: parent.width
    contentHeight: contentColumn.implicitHeight
    // ✅ 使用 Grid 确保4个卡片均匀分布
    Grid {
        anchors.centerIn: parent
        columns: 4
        rowSpacing: 15 
        Repeater {
            model: [
                {title: "原始数据", value: originalDataCount.toString(), unit: "数据行数", color: "#3498db"},
                {title: "筛选后数据", value: filteredDataCount.toString(), unit: "数据行数", color: "#2ecc71"},
                {title: "清洗后数据", value: cleanedDataCount.toString(), unit: "数据行数", color: "#e74c3c"},
                {title: "处理时间", value: processingTime, unit: "清洗用时", color: "#9b59b6"}
            ]
            
            Rectangle {
                width: 95
                height: 85  // ✅ 调整高度以适应父容器
                radius: 8
                color: "#f8f9fa"
                border.width: 1
                border.color: "#e9ecef"
                Column {
                    anchors.centerIn: parent
                    spacing: 6  // ✅ 减少内部间距
                    
                    Label {
                        text: modelData.title
                        font.pixelSize: 12  // ✅ 减小字体
                        color: "#6c757d"
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Label {
                        text: modelData.value
                        font.pixelSize: 22  // ✅ 减小字体
                        font.bold: true
                        color: modelData.color
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Label {
                        text: modelData.unit
                        font.pixelSize: 10  // ✅ 减小字体
                        color: "#6c757d"
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }
    }
}
                // 清洗进度 - 修复溢出问题
GroupBox {
    title: "清洗进度"
    Layout.fillWidth: true
    Layout.preferredHeight: 80  // ✅ 设置固定高度
    Layout.maximumHeight: 80    // ✅ 限制最大高度
    clip: true  // ✅ 裁剪溢出内容
    
    Column {
        anchors.fill: parent
        anchors.margins: 5  // ✅ 添加内边距
        spacing: 5
        
        // 进度文本行
        Row {
            width: parent.width
            spacing: 5
            
            Text {
                id: progressText
                text: "等待开始清洗"
                font.pixelSize: 13  // ✅ 减小字体
                color: "#6c757d"
                width: parent.width - 50
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter
            }
            
            Text {
                id: progressPercent
                text: "0%"
                font.pixelSize: 13  // ✅ 减小字体
                color: "#3498db"
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        
        // 进度条容器
        Item {
            width: parent.width
            height: 20
            
            // 进度条背景
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 10  // ✅ 减小高度
                radius: 5
                color: "#e9ecef"
                
                // 进度条前景
                Rectangle {
                    id: progressBar
                    width: 0
                    height: parent.height
                    radius: parent.radius
                    
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#00b09b" }
                        GradientStop { position: 1.0; color: "#96c93d" }
                    }
                    
                    Behavior on width {
                        NumberAnimation { duration: 500 }
                    }
                }
            }
        }
    }
}
                // 清洗结果预览 - 修复溢出问题
GroupBox {
    title: "清洗结果预览"
    Layout.fillWidth: true
    Layout.preferredHeight: 170  // ✅ 设置固定高度
    Layout.maximumHeight: 170    // ✅ 限制最大高度
    clip: true  // ✅ 裁剪溢出内容
    
    Column {
        anchors.fill: parent
        // anchors.margins: 5  // ✅ 减小内边距
        spacing: 5
        
        // 表格容器
        Rectangle {
            width: parent.width
            height: 140  // ✅ 减小高度
            radius: 8
            border.width: 1
            border.color: "#e0e0e0"
            clip: true
            
            ListView {
                id: cleaningResultList
                anchors.fill: parent
                model: dataFetchController.cleaningResultModel  // ✅ 使用C++模型
                clip: true
                
                // 表头
                header: Rectangle {
                    width: cleaningResultList.width
                    height: 35  // ✅ 减小高度
                    color: "#1a2980"
                    
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 5  // ✅ 减小边距
                        anchors.rightMargin: 5
                        spacing: 8  // ✅ 减小间距
                        
                        Text {
                            text: "日期"
                            color: "white"
                            font.pixelSize: 11  // ✅ 减小字体
                            font.bold: true
                            width: 80  // ✅ 减小宽度
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "代码"
                            color: "white"
                            font.pixelSize: 11
                            font.bold: true
                            width: 70
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "名称"
                            color: "white"
                            font.pixelSize: 11
                            font.bold: true
                            width: 100  // ✅ 减小宽度
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "收盘价"
                            color: "white"
                            font.pixelSize: 11
                            font.bold: true
                            width: 70
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "涨跌幅"
                            color: "white"
                            font.pixelSize: 11
                            font.bold: true
                            width: 70
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "成交量"
                            color: "white"
                            font.pixelSize: 11
                            font.bold: true
                            width: 70
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                
                delegate: Rectangle {
                    width: cleaningResultList.width
                    height: 30  // ✅ 减小行高
                    color: index % 2 === 0 ? "#ffffff" : "#f8f9fa"
                    
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 5
                        anchors.rightMargin: 5
                        spacing: 15
                        
                        Text {
                            text: model.date
                            width: 80
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: model.code
                            width: 70
                            font.pixelSize: 11
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: model.name
                            width: 100
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: model.close.toFixed(2)
                            width: 70
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: (model.change > 0 ? "+" : "") + model.change.toFixed(2) + "%"
                            color: model.change > 0 ? "#00b09b" : model.change < 0 ? "#e74c3c" : "#6c757d"
                            width: 70
                            font.pixelSize: 11
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: (model.volume / 10000).toFixed(2) + "万"
                            width: 70
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
        
        // 数据统计行
        Row {
            width: parent.width
            spacing: 10
            
            Text {
                text: "总记录数: " + (cleaningResultList.model ? cleaningResultList.model.count : 0)
                color: "#6c757d"
                font.pixelSize: 11
            }
            
            Text {
                text: "显示前 " + (cleaningResultList.model ? cleaningResultList.model.count : 0) + " 条"
                color: "#6c757d"
                font.pixelSize: 11
            }
        }
    }
}
                // 按钮区域
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 20
                    
                    Button {
                        text: "预览清洗效果"
                        Layout.fillWidth: true
                        height: 45
                        
                        background: Rectangle {
                            radius: 8
                            color: "#6c757d"
                        }
                        
                        onClicked: {
                            // 触发清洗请求信号
                            cleaningRequested(rules)
                            
                            // 显示预览消息
                            previewMessage.open()
                        }
                    }
                    
                    Button {
                        id: executeButton
                        text: "执行数据清洗"
                        Layout.fillWidth: true
                        height: 45
                        
                        background: Rectangle {
                            radius: 8
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#00b09b" }
                                GradientStop { position: 1.0; color: "#96c93d" }
                            }
                        }
                        
                        onClicked: {
                            executeButton.enabled = false
                            executeButton.text = "清洗中..."
                            
                            // 调用C++异步清洗方法
                            dataFetchController.cleanDataAsync(rules)
                        }
                    }
                }
            }
        }
    }
    
    // 创建DataFetchController实例
    DataFetchController {
        id: dataFetchController
        
        // 连接C++进度信号
        onDataCleaningProgress: {
            progressBar.width = (progress / 100) * (progressBar.parent.width)
            progressPercent.text = progress + "%"
            progressText.text = message
            
            // 从消息中解析统计信息
            updateStatsFromMessage(message)
        }
        
        // 连接C++完成信号
        onDataCleaningCompleted: {
            if (success) {
                executeButton.text = "清洗完成"
                executeButton.enabled = true
                
                // 从消息中解析统计信息
                updateStatsFromMessage(message)
                
                // 显示完成消息
                completeMessageText.text = message
                completeMessage.open()
                
                // 触发清洗完成信号
                dataCleaningModal.cleaningCompleted()
            }
        }
        
        // 连接C++错误信号
        onDataCleaningError: {
            executeButton.text = "执行数据清洗"
            executeButton.enabled = true
            
            // 显示错误消息
            completeMessageText.text = "清洗失败: " + error
            completeMessage.open()
        }
    }
    
    // 统计标签
    property alias filteredCountLabel: filteredCountLabel
    property alias cleanedCountLabel: cleanedCountLabel
    property alias processTimeLabel: processTimeLabel
    
    Label {
        id: filteredCountLabel
        visible: false
        text: "0"
    }
    
    Label {
        id: cleanedCountLabel
        visible: false
        text: "0"
    }
    
    Label {
        id: processTimeLabel
        visible: false
        text: "0.0s"
    }
    
    Popup {
        id: previewMessage
        width: 300
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
            anchors.margins: 20
            
            Label {
                text: "系统提示"
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            
            Label {
                text: "清洗效果预览已更新"
                Layout.alignment: Qt.AlignHCenter
            }
            
            Button {
                text: "确定"
                Layout.alignment: Qt.AlignHCenter
                onClicked: previewMessage.close()
            }
        }
    }
    
    Popup {
        id: completeMessage
        width: 400
        height: 120
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
            anchors.margins: 20
            
            Label {
                text: "清洗完成"
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            
            Label {
                id: completeMessageText
                Layout.alignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
            }
            
            Button {
                text: "确定"
                Layout.alignment: Qt.AlignHCenter
                onClicked: completeMessage.close()
            }
        }
    }
}
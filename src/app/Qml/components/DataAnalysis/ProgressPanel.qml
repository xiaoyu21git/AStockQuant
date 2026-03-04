// ProgressPanel.qml - 进度面板组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as Theme

Rectangle {
    id: progressPanel
    
    // 属性
    property string title: "进度"
    property int progress: 0
    property int originalCount: 0
    property int processedCount: 0
    property int removedCount: 0
    property string progressColor: "#4CAF50"
    property bool showControls: true
    
    // 尺寸
    width: 400
    height: 180
    radius: 8
    color: Theme.darkCard
    border.color: Theme.darkBorder
    border.width: 1
    
    // 内容
    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        
        // 标题
        Text {
            text: title
            font.pixelSize: 16
            font.bold: true
            color: Theme.darkText
        }
        
        // 进度条
        Column {
            width: parent.width
            spacing: 4
            
            // 进度条背景
            Rectangle {
                width: parent.width
                height: 8
                radius: 4
                color: Theme.darkBorder
                
                // 进度条前景
                Rectangle {
                    id: progressBar
                    width: parent.width * (progress / 100)
                    height: 8
                    radius: 4
                    color: progressColor
                    
                    Behavior on width {
                        NumberAnimation { duration: 500 }
                    }
                }
            }
            
            // 进度文本
            Row {
                width: parent.width
                spacing: 10
                
                Text {
                    id: progressText
                    text: progress === 0 ? "准备开始..." : 
                          progress < 100 ? "处理中..." : "处理完成"
                    font.pixelSize: 12
                    color: Theme.darkTextSecondary
                }
                
                Text {
                    id: progressPercentage
                    text: progress + "%"
                    font.pixelSize: 12
                    font.bold: true
                    color: progressColor
                }
            }
        }
        
        // 统计信息
        Row {
            width: parent.width
            spacing: 20
            
            // 原始数据
            Column {
                spacing: 2
                
                Text {
                    text: "原始数据"
                    font.pixelSize: 11
                    color: Theme.darkTextSecondary
                }
                
                Text {
                    id: originalCountText
                    text: originalCount + " 条"
                    font.pixelSize: 14
                    font.bold: true
                    color: Theme.darkText
                }
            }
            
            // 处理后数据
            Column {
                spacing: 2
                
                Text {
                    text: "处理后数据"
                    font.pixelSize: 11
                    color: Theme.darkTextSecondary
                }
                
                Text {
                    id: processedCountText
                    text: processedCount + " 条"
                    font.pixelSize: 14
                    font.bold: true
                    color: progressColor
                }
            }
            
            // 移除数据
            Column {
                spacing: 2
                
                Text {
                    text: "移除数据"
                    font.pixelSize: 11
                    color: Theme.darkTextSecondary
                }
                
                Text {
                    id: removedCountText
                    text: removedCount + " 条"
                    font.pixelSize: 14
                    font.bold: true
                    color: "#F44336"
                }
            }
        }
        
        // 控制按钮（可选）
        Row {
            width: parent.width
            spacing: 10
            visible: showControls
            
            // 开始按钮
            Rectangle {
                width: 80
                height: 28
                radius: 6
                color: progress === 0 ? progressColor : Theme.darkBorder
                enabled: progress === 0
                
                Text {
                    anchors.centerIn: parent
                    text: "开始"
                    font.pixelSize: 12
                    font.bold: true
                    color: progress === 0 ? "white" : Theme.darkTextSecondary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        console.log("开始处理")
                        progressPanel.startClicked()
                    }
                }
            }
            
            // 取消按钮
            Rectangle {
                width: 80
                height: 28
                radius: 6
                color: Theme.darkBorder
                
                Text {
                    anchors.centerIn: parent
                    text: "取消"
                    font.pixelSize: 12
                    color: Theme.darkText
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        console.log("取消处理")
                        progressPanel.cancelClicked()
                    }
                }
            }
        }
    }
    
    // 信号
    signal startClicked()
    signal cancelClicked()
    
    // 方法
    function reset() {
        progress = 0
        originalCount = 0
        processedCount = 0
        removedCount = 0
    }
    
    function updateProgress(value, processed, removed) {
        progress = value
        processedCount = processed
        removedCount = removed
    }
}
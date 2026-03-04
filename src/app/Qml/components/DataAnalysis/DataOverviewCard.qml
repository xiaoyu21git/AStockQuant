// DataOverviewCard.qml - 数据总览卡片，显示数据状态概览
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: 300
    height: 180
    
    // 属性
    property string title: "数据总览"
    property string subtitle: "数据状态概览"
    property color backgroundColor: "#0f1738"
    property color borderColor: "#2d3a7c"
    property color textColor: "white"
    property color textSecondaryColor: "#b0b0b0"
    property color accentColor: "#00bcd4"
    property color successColor: "#4caf50"
    property color warningColor: "#ff9800"
    property color dangerColor: "#f44336"
    
    // 数据状态
    property int totalDataSources: 4
    property int activeDataSources: 3
    property int totalRecords: 12500
    property int cleanedRecords: 9800
    property real dataQuality: 0.92
    property string lastUpdate: "2026-01-15 14:30"
    property bool isProcessing: false
    property int processingProgress: 65
    
    // 信号
    signal cardClicked()
    signal viewDetailsClicked()
    signal refreshClicked()
    
    // 背景
    Rectangle {
        id: cardBackground
        anchors.fill: parent
        color: root.backgroundColor
        radius: 8
        border.color: root.borderColor
        border.width: 1
        
        // 悬停效果
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            
            onEntered: {
                cardBackground.border.color = Qt.lighter(root.borderColor, 1.3)
            }
            
            onExited: {
                cardBackground.border.color = root.borderColor
            }
            
            onClicked: {
                root.cardClicked()
            }
        }
    }
    
    // 内容
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        
        // 标题栏
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            
            Text {
                text: root.title
                font.pixelSize: 16
                font.bold: true
                color: root.textColor
            }
            
            Item { Layout.fillWidth: true }
            
            // 刷新按钮
            Rectangle {
                width: 24
                height: 24
                radius: 4
                color: "transparent"
                border.color: root.textSecondaryColor
                border.width: 1
                
                Text {
                    anchors.centerIn: parent
                    text: "↻"
                    font.pixelSize: 12
                    color: root.textSecondaryColor
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.refreshClicked()
                    }
                }
            }
        }
        
        // 副标题
        Text {
            text: root.subtitle
            font.pixelSize: 12
            color: root.textSecondaryColor
            Layout.fillWidth: true
        }
        
        // 数据源状态
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            
            Text {
                text: "数据源:"
                font.pixelSize: 12
                color: root.textSecondaryColor
            }
            
            Item { Layout.fillWidth: true }
            
            Text {
                text: root.activeDataSources + "/" + root.totalDataSources + " 活跃"
                font.pixelSize: 12
                font.bold: true
                color: root.activeDataSources === root.totalDataSources ? root.successColor : 
                       root.activeDataSources > 0 ? root.warningColor : root.dangerColor
            }
        }
        
        // 数据记录状态
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            
            Text {
                text: "数据记录:"
                font.pixelSize: 12
                color: root.textSecondaryColor
            }
            
            Item { Layout.fillWidth: true }
            
            Text {
                text: root.cleanedRecords.toLocaleString() + "/" + root.totalRecords.toLocaleString()
                font.pixelSize: 12
                font.bold: true
                color: root.accentColor
            }
        }
        
        // 数据质量
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            
            Text {
                text: "数据质量:"
                font.pixelSize: 12
                color: root.textSecondaryColor
            }
            
            Item { Layout.fillWidth: true }
            
            // 质量指示器
            Rectangle {
                width: 60
                height: 16
                radius: 8
                color: {
                    if (root.dataQuality >= 0.9) return Qt.darker(root.successColor, 1.2)
                    if (root.dataQuality >= 0.7) return Qt.darker(root.warningColor, 1.2)
                    return Qt.darker(root.dangerColor, 1.2)
                }
                
                Text {
                    anchors.centerIn: parent
                    text: (root.dataQuality * 100).toFixed(1) + "%"
                    font.pixelSize: 10
                    font.bold: true
                    color: "white"
                }
            }
        }
        
        // 处理进度（如果有处理中）
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            visible: root.isProcessing
            
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    text: "处理进度:"
                    font.pixelSize: 10
                    color: root.textSecondaryColor
                }
                
                Item { Layout.fillWidth: true }
                
                Text {
                    text: root.processingProgress + "%"
                    font.pixelSize: 10
                    font.bold: true
                    color: root.accentColor
                }
            }
            
            // 进度条
            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: Qt.darker(root.backgroundColor, 1.5)
                
                Rectangle {
                    width: parent.width * (root.processingProgress / 100)
                    height: 4
                    radius: 2
                    color: root.accentColor
                    
                    Behavior on width {
                        NumberAnimation { duration: 300 }
                    }
                }
            }
        }
        
        // 最后更新时间
        Text {
            text: "更新: " + root.lastUpdate
            font.pixelSize: 10
            color: root.textSecondaryColor
            Layout.fillWidth: true
        }
        
        // 查看详情按钮
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            radius: 6
            color: Qt.darker(root.backgroundColor, 1.2)
            border.color: root.accentColor
            border.width: 1
            
            Text {
                anchors.centerIn: parent
                text: "查看详情"
                font.pixelSize: 12
                color: root.accentColor
            }
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.viewDetailsClicked()
                }
            }
        }
    }
    
    // 脉冲动画（如果有处理中）
    Rectangle {
        anchors.fill: parent
        radius: 8
        color: "transparent"
        border.color: root.accentColor
        border.width: 2
        opacity: 0
        visible: root.isProcessing
        
        SequentialAnimation on opacity {
            running: root.isProcessing
            loops: Animation.Infinite
            
            NumberAnimation {
                from: 0
                to: 0.3
                duration: 1000
            }
            NumberAnimation {
                from: 0.3
                to: 0
                duration: 1000
            }
        }
    }
    
    // API方法
    function updateDataStatus(sources, records, quality, processing) {
        if (sources) {
            root.totalDataSources = sources.total || 4
            root.activeDataSources = sources.active || 3
        }
        
        if (records) {
            root.totalRecords = records.total || 12500
            root.cleanedRecords = records.cleaned || 9800
        }
        
        if (quality !== undefined) {
            root.dataQuality = quality
        }
        
        if (processing !== undefined) {
            root.isProcessing = processing.isProcessing || false
            root.processingProgress = processing.progress || 0
        }
        
        root.lastUpdate = new Date().toLocaleString(Qt.locale(), "yyyy-MM-dd hh:mm")
    }
    
    function startProcessing() {
        root.isProcessing = true
        root.processingProgress = 0
        
        // 模拟进度更新
        var progressTimer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 100; running: true; repeat: true }', root)
        progressTimer.triggered.connect(function() {
            if (root.processingProgress < 100) {
                root.processingProgress += 1
            } else {
                root.isProcessing = false
                progressTimer.destroy()
            }
        })
    }
    
    function completeProcessing() {
        root.isProcessing = false
        root.processingProgress = 100
        root.lastUpdate = new Date().toLocaleString(Qt.locale(), "yyyy-MM-dd hh:mm")
    }
}
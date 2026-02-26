// components/DataAnalysis/EmbeddedWorkflow.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: 1200
    height: 320
    
    // 可配置属性
    property int currentStep: 1
    property int totalSteps: 5
    property bool showProgressBar: true
    property bool showNavigation: true
    property bool compactMode: false
    
    // 颜色主题
    property color backgroundColor: "#0f1738"
    property color cardBackground: "#1a237e"
    property color activeColor: "#00bcd4"
    property color completedColor: "#4caf50"
    property color pendingColor: "#3949ab"
    property color textColor: "white"
    property color textSecondaryColor: "#b0b0b0"
    
    // 步骤数据模型
    property var steps: [
        {
            "title": "数据整合",
            "description": "多源数据整合与清洗",
            "icon": "database",
            "status": "completed"
        },
        {
            "title": "因子分析", 
            "description": "特征工程与因子开发",
            "icon": "chart-bar",
            "status": "active"
        },
        {
            "title": "策略回测",
            "description": "历史测试与绩效分析",
            "icon": "history",
            "status": "pending"
        },
        {
            "title": "风险管理",
            "description": "风险建模与组合优化",
            "icon": "shield-alt",
            "status": "pending"
        },
        {
            "title": "报告部署", 
            "description": "生成报告与实盘部署",
            "icon": "file-alt",
            "status": "pending"
        }
    ]
    
    // 信号
    signal stepActivated(int stepIndex)
    signal stepStarted(int stepIndex)
    signal stepDetailRequested(int stepIndex)
    signal navigationAction(string action)
    
    // 背景
    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor
        radius: 12
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15
            
            // 步骤容器
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                // 连接线
                Rectangle {
                    id: connectionLine
                    width: parent.width * 0.9
                    height: 3
                    anchors.centerIn: parent
                    color: "#2d3a7c"
                    
                    // 进度线
                    Rectangle {
                        width: connectionLine.width * ((currentStep - 1) / totalSteps)
                        height: 3
                        color: root.activeColor
                        
                        Behavior on width {
                            NumberAnimation { duration: 300 }
                        }
                    }
                    
                    // 连接点
                    Repeater {
                        model: totalSteps
                        
                        Rectangle {
                            width: 16
                            height: 16
                            radius: 8
                            x: (connectionLine.width / (totalSteps - 1)) * index - 8
                            y: -6.5
                            
                            color: {
                                if (index < currentStep - 1) return root.completedColor
                                if (index === currentStep - 1) return root.activeColor
                                return root.pendingColor
                            }
                            
                            border.color: root.backgroundColor
                            border.width: 3
                            
                            // 步骤编号
                            Text {
                                anchors.centerIn: parent
                                text: index + 1
                                font.pixelSize: 10
                                font.bold: true
                                color: "white"
                            }
                            
                            // 当前步骤的脉冲效果
                            Rectangle {
                                anchors.centerIn: parent
                                width: 24
                                height: 24
                                radius: 12
                                color: "transparent"
                                border.color: index === currentStep - 1 ? root.activeColor : "transparent"
                                border.width: 2
                                opacity: index === currentStep - 1 ? 0.5 : 0
                                
                                SequentialAnimation on scale {
                                    running: index === currentStep - 1
                                    loops: Animation.Infinite
                                    
                                    NumberAnimation {
                                        from: 1.0
                                        to: 1.3
                                        duration: 1000
                                    }
                                    NumberAnimation {
                                        from: 1.3
                                        to: 1.0
                                        duration: 1000
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 步骤卡片
                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    
                    Repeater {
                        model: steps
                        
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: parent.height
                            
                            property bool isActive: index === currentStep - 1
                            property bool isCompleted: index < currentStep - 1
                            property bool isPending: index > currentStep - 1
                            
                            // 步骤卡片
                            Rectangle {
                                id: stepCard
                                width: 200
                                height: 180
                                anchors.centerIn: parent
                                radius: 10
                                color: {
                                    if (isActive) return Qt.lighter(root.cardBackground, 1.3)
                                    if (isCompleted) return Qt.lighter(root.cardBackground, 1.1)
                                    return root.cardBackground
                                }
                                border.color: {
                                    if (isActive) return root.activeColor
                                    if (isCompleted) return root.completedColor
                                    return Qt.darker(root.cardBackground, 1.2)
                                }
                                border.width: isActive ? 2 : 1
                                
                                // 悬停效果
                                scale: mouseArea.containsMouse ? 1.05 : 1.0
                                Behavior on scale {
                                    NumberAnimation { duration: 200 }
                                }
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 12
                                    width: parent.width * 0.9
                                    
                                    // 图标
                                    Rectangle {
                                        width: 50
                                        height: 50
                                        radius: 25
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        
                                        color: {
                                            if (isActive) return root.activeColor
                                            if (isCompleted) return root.completedColor
                                            return Qt.darker(root.cardBackground, 1.5)
                                        }
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: getIconText(modelData.icon)
                                            font.pixelSize: 20
                                            font.bold: true
                                            color: "white"
                                        }
                                    }
                                    
                                    // 标题
                                    Text {
                                        text: modelData.title
                                        font.pixelSize: 16
                                        font.bold: true
                                        color: root.textColor
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.WrapAnywhere
                                    }
                                    
                                    // 描述
                                    Text {
                                        text: modelData.description
                                        font.pixelSize: 12
                                        color: root.textSecondaryColor
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                    
                                    // 状态标签
                                    Rectangle {
                                        width: 70
                                        height: 20
                                        radius: 10
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        
                                        color: {
                                            if (isActive) return Qt.rgba(0, 188/255, 212/255, 0.2)
                                            if (isCompleted) return Qt.rgba(76/255, 175/255, 80/255, 0.2)
                                            return Qt.rgba(57/255, 73/255, 171/255, 0.2)
                                        }
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: {
                                                if (isActive) return "进行中"
                                                if (isCompleted) return "已完成"
                                                return "待开始"
                                            }
                                            font.pixelSize: 10
                                            font.bold: true
                                            color: {
                                                if (isActive) return root.activeColor
                                                if (isCompleted) return root.completedColor
                                                return root.textSecondaryColor
                                            }
                                        }
                                    }
                                }
                                
                                // 点击区域
                                MouseArea {
                                    id: mouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    
                                    onClicked: {
                                        root.stepActivated(index + 1)
                                        root.currentStep = index + 1
                                    }
                                }
                            }
                            
                            // 快速操作按钮
                            Row {
                                anchors.top: stepCard.bottom
                                anchors.topMargin: 8
                                anchors.horizontalCenter: parent.horizontalCenter
                                spacing: 5
                                visible: isActive || mouseArea.containsMouse
                                
                                // 开始按钮
                                Rectangle {
                                    width: 80
                                    height: 28
                                    radius: 6
                                    color: root.activeColor
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: "开始"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                    }
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.stepStarted(index + 1)
                                        }
                                    }
                                }
                                
                                // 详情按钮
                                Rectangle {
                                    width: 80
                                    height: 28
                                    radius: 6
                                    color: "transparent"
                                    border.color: root.textSecondaryColor
                                    border.width: 1
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: "详情"
                                        font.pixelSize: 12
                                        color: root.textSecondaryColor
                                    }
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.stepDetailRequested(index + 1)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 进度条
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                visible: showProgressBar
                
                RowLayout {
                    anchors.fill: parent
                    
                    Text {
                        text: "进度:"
                        font.pixelSize: 14
                        color: root.textColor
                    }
                    
                    // 进度条
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        radius: 4
                        color: Qt.darker(root.cardBackground, 1.5)
                        
                        Rectangle {
                            width: parent.width * ((currentStep - 1) / totalSteps)
                            height: parent.height
                            radius: 4
                            color: root.activeColor
                            
                            Behavior on width {
                                NumberAnimation { duration: 300 }
                            }
                        }
                    }
                    
                    Text {
                        text: Math.round(((currentStep - 1) / totalSteps) * 100) + "%"
                        font.pixelSize: 14
                        font.bold: true
                        color: root.activeColor
                    }
                }
            }
            
            // 导航控制
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                visible: showNavigation
                
                Item { Layout.fillWidth: true }
                
                // 上一步按钮
                Rectangle {
                    width: 100
                    height: 36
                    radius: 6
                    color: currentStep > 1 ? Qt.darker(root.cardBackground, 1.2) : Qt.darker(root.cardBackground, 1.5)
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        
                        Text {
                            text: "←"
                            font.pixelSize: 14
                            color: currentStep > 1 ? root.textColor : root.textSecondaryColor
                        }
                        
                        Text {
                            text: "上一步"
                            font.pixelSize: 14
                            color: currentStep > 1 ? root.textColor : root.textSecondaryColor
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: currentStep > 1 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled: currentStep > 1
                        onClicked: {
                            currentStep--
                            navigationAction("prev")
                        }
                    }
                }
                
                // 下一步按钮
                Rectangle {
                    width: 100
                    height: 36
                    radius: 6
                    color: currentStep < totalSteps ? root.activeColor : Qt.darker(root.activeColor, 1.5)
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        
                        Text {
                            text: currentStep < totalSteps ? "下一步" : "完成"
                            font.pixelSize: 14
                            font.bold: true
                            color: "white"
                        }
                        
                        Text {
                            text: currentStep < totalSteps ? "→" : "✓"
                            font.pixelSize: 14
                            color: "white"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: currentStep < totalSteps ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled: currentStep < totalSteps
                        onClicked: {
                            currentStep++
                            navigationAction("next")
                        }
                    }
                }
                
                // 完成按钮
                Rectangle {
                    width: 100
                    height: 36
                    radius: 6
                    color: "transparent"
                    border.color: root.completedColor
                    border.width: 1
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        
                        Text {
                            text: "✓"
                            font.pixelSize: 14
                            color: root.completedColor
                        }
                        
                        Text {
                            text: "完成"
                            font.pixelSize: 14
                            color: root.completedColor
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            navigationAction("complete")
                        }
                    }
                }
                
                Item { Layout.fillWidth: true }
            }
        }
    }
    
    // 辅助函数：获取图标文本
    function getIconText(iconName) {
        var iconMap = {
            "database": "📊",
            "chart-bar": "📈", 
            "history": "⏱️",
            "shield-alt": "🛡️",
            "file-alt": "📄"
        }
        return iconMap[iconName] || "○"
    }
    
    // API方法
    function goToStep(stepNumber) {
        if (stepNumber >= 1 && stepNumber <= totalSteps) {
            currentStep = stepNumber
            stepActivated(stepNumber)
        }
    }
    
    function markStepCompleted(stepNumber) {
        if (stepNumber >= 1 && stepNumber <= totalSteps) {
            if (stepNumber === currentStep && currentStep < totalSteps) {
                currentStep++
            }
        }
    }
    
    function reset() {
        currentStep = 1
    }
    
    // 键盘快捷键
    Keys.onPressed: {
        if (event.key === Qt.Key_Left && currentStep > 1) {
            currentStep--
            navigationAction("prev")
            event.accepted = true
        } else if (event.key === Qt.Key_Right && currentStep < totalSteps) {
            currentStep++
            navigationAction("next")
            event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            navigationAction("complete")
            event.accepted = true
        }
    }
    
    // 延迟初始化以避免组件未完全加载时崩溃
    Component.onCompleted: {
        console.log("EmbeddedWorkflow组件初始化完成")
        // 延迟执行焦点获取，避免初始化冲突
        focusTimer.start()
    }
    
    Timer {
        id: focusTimer
        interval: 200
        onTriggered: {
            console.log("开始延迟焦点获取")
            try {
                forceActiveFocus()
                console.log("焦点获取完成")
            } catch (error) {
                console.error("焦点获取时发生错误:", error)
            }
        }
    }
}
// components/GlobalWorkflow.qml
// 全局工作流程组件，可以在所有页面中显示和控制
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: 800
    height: minimized ? 60 : 120
    
    // 可配置属性
    property int currentStep: 1
    property int totalSteps: 5
    property bool minimized: false
    property bool showControls: true
    
    // 高度动画
    Behavior on height {
        NumberAnimation { duration: 300; easing.type: Easing.InOutQuad }
    }
    
    // 颜色主题
    property color backgroundColor: "#0f1738"
    property color activeColor: "#00bcd4"
    property color completedColor: "#4caf50"
    property color pendingColor: "#3949ab"
    property color textColor: "white"
    property color textSecondaryColor: "#b0b0b0"
    
    // 步骤数据模型
    property var steps: [
        {
            "title": "数据准备",
            "description": "数据源管理、清洗、标准化",
            "icon": "database",
            "status": "active",
            "inPoint": true
        },
        {
            "title": "策略开发", 
            "description": "因子分析、策略创建、参数优化",
            "icon": "chart-bar",
            "status": "pending",
            "inPoint": false
        },
        {
            "title": "回测验证",
            "description": "历史回测、绩效分析、过拟合检验",
            "icon": "history",
            "status": "pending",
            "inPoint": false
        },
        {
            "title": "风险管理",
            "description": "风险配置、压力测试、合规检查",
            "icon": "shield-alt",
            "status": "pending",
            "inPoint": true
        },
        {
            "title": "实盘部署", 
            "description": "实盘执行、监控、绩效跟踪",
            "icon": "rocket",
            "status": "pending",
            "inPoint": true
        }
    ]
    
    // 信号
    signal stepActivated(int stepIndex)
    signal stepStarted(int stepIndex)
    signal navigationAction(string action)
    signal toggleMinimized()
    
    // 背景 - 添加高度动画
    Rectangle {
        id: workflowBackground
        anchors.fill: parent
        color: root.backgroundColor
        radius: 8
        border.color: Qt.darker(root.backgroundColor, 1.2)
        border.width: 1
        
        // 高度动画
        Behavior on height {
            NumberAnimation { duration: 300; easing.type: Easing.InOutQuad }
        }
        
        // 最小化时的高度变化
        states: [
            State {
                name: "expanded"
                when: !minimized
                PropertyChanges {
                    target: workflowBackground
                    height: 120
                }
            },
            State {
                name: "collapsed"
                when: minimized
                PropertyChanges {
                    target: workflowBackground
                    height: 60
                }
            }
        ]
        
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
            
            // 标题栏
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                visible: !minimized
                
                Text {
                    text: "量化交易工作流程"
                    font.pixelSize: 16
                    font.bold: true
                    color: root.textColor
                }
                
                Item { Layout.fillWidth: true }
                
                // 进度显示
                Text {
                    text: "进度: " + Math.round(((currentStep - 1) / totalSteps) * 100) + "%"
                    font.pixelSize: 14
                    color: root.activeColor
                }
            }
            
            // 步骤条（最小化时只显示进度条）
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: minimized ? 20 : 60
                
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
                    
                    // 连接点（最小化时不显示）
                    Repeater {
                        model: minimized ? 0 : totalSteps
                        
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
                            
                            // 步骤提示（悬停时显示）
                            Rectangle {
                                id: stepTooltip
                                width: 120
                                height: 60
                                radius: 6
                                color: root.backgroundColor
                                border.color: root.activeColor
                                border.width: 1
                                visible: false
                                z: 1000
                                
                                x: -50
                                y: -70
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    width: parent.width * 0.9
                                    
                                    Text {
                                        text: steps[index].title
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: root.textColor
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    
                                    Text {
                                        text: steps[index].description
                                        font.pixelSize: 10
                                        color: root.textSecondaryColor
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                
                                onEntered: {
                                    if (!minimized) {
                                        stepTooltip.visible = true
                                    }
                                }
                                
                                onExited: {
                                    stepTooltip.visible = false
                                }
                                
                                onClicked: {
                                    if (!minimized) {
                                        root.stepActivated(index + 1)
                                        root.currentStep = index + 1
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 步骤标签（最小化时不显示）
                RowLayout {
                    anchors.top: connectionLine.bottom
                    anchors.topMargin: 20
                    anchors.left: connectionLine.left
                    anchors.right: connectionLine.right
                    visible: !minimized
                    
                    Repeater {
                        model: steps
                        
                        Text {
                            Layout.fillWidth: true
                            text: modelData.title
                            font.pixelSize: 11
                            font.bold: index === currentStep - 1
                            color: {
                                if (index < currentStep - 1) return root.completedColor
                                if (index === currentStep - 1) return root.activeColor
                                return root.textSecondaryColor
                            }
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            
            // 控制按钮
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                visible: showControls && !minimized
                
                Item { Layout.fillWidth: true }
                
                // 上一步按钮
                Rectangle {
                    width: 80
                    height: 28
                    radius: 6
                    color: currentStep > 1 ? Qt.darker(root.backgroundColor, 1.2) : Qt.darker(root.backgroundColor, 1.5)
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        
                        Text {
                            text: "←"
                            font.pixelSize: 12
                            color: currentStep > 1 ? root.textColor : root.textSecondaryColor
                        }
                        
                        Text {
                            text: "上一步"
                            font.pixelSize: 12
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
                
                // 当前步骤按钮
                Rectangle {
                    width: 120
                    height: 28
                    radius: 6
                    color: root.activeColor
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        
                        Text {
                            text: getIconText(steps[currentStep-1].icon)
                            font.pixelSize: 12
                            color: "white"
                        }
                        
                        Text {
                            text: "开始" + steps[currentStep-1].title
                            font.pixelSize: 12
                            font.bold: true
                            color: "white"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.stepStarted(currentStep)
                        }
                    }
                }
                
                // 下一步按钮
                Rectangle {
                    width: 80
                    height: 28
                    radius: 6
                    color: currentStep < totalSteps ? root.activeColor : Qt.darker(root.activeColor, 1.5)
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        
                        Text {
                            text: currentStep < totalSteps ? "下一步" : "完成"
                            font.pixelSize: 12
                            font.bold: true
                            color: "white"
                        }
                        
                        Text {
                            text: currentStep < totalSteps ? "→" : "✓"
                            font.pixelSize: 12
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
                
                Item { Layout.fillWidth: true }
            }
            
            // 状态信息（最小化时显示简略信息）
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 20
                
                Text {
                    text: minimized ? 
                          "步骤 " + currentStep + "/" + totalSteps + ": " + steps[currentStep-1].title :
                          "当前步骤: " + steps[currentStep-1].title + " - " + steps[currentStep-1].description
                    font.pixelSize: minimized ? 11 : 12
                    color: root.textSecondaryColor
                    elide: Text.ElideRight
                }
                
                Item { Layout.fillWidth: true }
                
                // 数据入库点标记
                Rectangle {
                    width: minimized ? 60 : 80
                    height: 18
                    radius: 9
                    color: steps[currentStep-1].inPoint ? "#f59e0b" : "transparent"
                    border.color: steps[currentStep-1].inPoint ? "#f59e0b" : "transparent"
                    border.width: 1
                    visible: steps[currentStep-1].inPoint
                    
                    Text {
                        anchors.centerIn: parent
                        text: minimized ? "入库点" : "数据入库点"
                        font.pixelSize: 9
                        font.bold: true
                        color: steps[currentStep-1].inPoint ? "white" : "transparent"
                    }
                }
                
                // 收起按钮 - 放在状态信息行右侧
                Rectangle {
                    id: expandCollapseButton
                    width: 24
                    height: 24
                    radius: 4
                    color: minimized ? Qt.darker(root.activeColor, 1.2) : Qt.darker(root.backgroundColor, 1.2)
                    
                    // 按钮边框和悬停效果
                    border.color: minimized ? root.activeColor : root.textSecondaryColor
                    border.width: 1
                    
                    // 按钮图标
                    Rectangle {
                        id: buttonIcon
                        width: 12
                        height: 2
                        radius: 1
                        color: minimized ? root.activeColor : root.textColor
                        anchors.centerIn: parent
                        
                        // 动画变换
                        states: [
                            State {
                                name: "expanded"
                                when: !minimized
                                PropertyChanges {
                                    target: buttonIcon
                                    rotation: 0
                                    width: 12
                                    height: 2
                                }
                                PropertyChanges {
                                    target: buttonIcon2
                                    rotation: 0
                                    opacity: 1
                                }
                            },
                            State {
                                name: "collapsed"
                                when: minimized
                                PropertyChanges {
                                    target: buttonIcon
                                    rotation: 45
                                    width: 10
                                    height: 2
                                }
                                PropertyChanges {
                                    target: buttonIcon2
                                    rotation: -45
                                    opacity: 1
                                }
                            }
                        ]
                        
                        transitions: [
                            Transition {
                                from: "*"
                                to: "*"
                                NumberAnimation { properties: "rotation, width, height"; duration: 200; easing.type: Easing.InOutQuad }
                            }
                        ]
                    }
                    
                    // 第二个图标线（用于创建X形状）
                    Rectangle {
                        id: buttonIcon2
                        width: 12
                        height: 2
                        radius: 1
                        color: minimized ? root.activeColor : root.textColor
                        anchors.centerIn: parent
                        opacity: minimized ? 1 : 0
                        
                        transitions: [
                            Transition {
                                from: "*"
                                to: "*"
                                NumberAnimation { properties: "rotation, opacity"; duration: 200; easing.type: Easing.InOutQuad }
                            }
                        ]
                    }
                    
                    // 悬停效果
                    MouseArea {
                        id: buttonMouseArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        
                        onEntered: {
                            expandCollapseButton.color = minimized ? 
                                Qt.lighter(root.activeColor, 1.2) : 
                                Qt.darker(root.backgroundColor, 1.5)
                        }
                        
                        onExited: {
                            expandCollapseButton.color = minimized ? 
                                Qt.darker(root.activeColor, 1.2) : 
                                Qt.darker(root.backgroundColor, 1.2)
                        }
                        
                        onClicked: {
                            root.toggleMinimized()
                        }
                    }
                    
                    // 按钮提示文本
                    Rectangle {
                        id: buttonTooltip
                        width: 80
                        height: 30
                        radius: 4
                        color: root.backgroundColor
                        border.color: root.activeColor
                        border.width: 1
                        visible: false
                        z: 1000
                        
                        x: -50
                        y: 30
                        
                        Text {
                            anchors.centerIn: parent
                            text: minimized ? "展开工作流程" : "收起工作流程"
                            font.pixelSize: 10
                            color: root.textColor
                        }
                    }
                    
                    // 悬停时显示提示
                    Connections {
                        target: buttonMouseArea
                        function onEntered() {
                            buttonTooltip.visible = true
                        }
                        function onExited() {
                            buttonTooltip.visible = false
                        }
                    }
                }
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
            "rocket": "🚀"
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
    
    function updateStepStatus(stepIndex, status) {
        if (stepIndex >= 1 && stepIndex <= totalSteps) {
            steps[stepIndex-1].status = status
            stepsChanged() // 触发属性更改通知
        }
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
            stepStarted(currentStep)
            event.accepted = true
        } else if (event.key === Qt.Key_M) {
            toggleMinimized()
            event.accepted = true
        }
    }
    
    // 延迟初始化以避免组件未完全加载时崩溃
    Component.onCompleted: {
        console.log("GlobalWorkflow组件初始化完成")
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
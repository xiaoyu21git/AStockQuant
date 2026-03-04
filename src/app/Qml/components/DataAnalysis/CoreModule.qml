// CoreModule.qml - 核心功能模块组件，与导航配合
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: 280
    height: 180
    
    // 属性
    property string moduleId: ""
    property string title: "核心模块"
    property string description: "模块描述"
    property string icon: "📊"
    property color backgroundColor: "#0f1738"
    property color borderColor: "#2d3a7c"
    property color textColor: "white"
    property color textSecondaryColor: "#b0b0b0"
    property color accentColor: "#00bcd4"
    property bool isActive: false
    property bool isCurrent: false
    property int stepNumber: 0 // 0表示不关联特定步骤
    
    // 状态
    property var status: {
        "tasks": [
            { name: "任务1", status: "completed" },
            { name: "任务2", status: "running" }
        ],
        "progress": 60,
        "lastUpdate": "刚刚"
    }
    
    // 操作
    property var actions: [
        { id: "open", label: "打开模块", icon: "🚀", primary: true },
        { id: "config", label: "配置", icon: "⚙️", primary: false }
    ]
    
    // 信号
    signal moduleClicked()
    signal actionTriggered(string actionId)
    signal taskClicked(string taskName)
    
    // 背景
    Rectangle {
        id: moduleBackground
        anchors.fill: parent
        color: root.backgroundColor
        radius: 12
        border.color: {
            if (root.isCurrent) return Qt.lighter(root.accentColor, 1.5)
            if (root.isActive) return root.accentColor
            return root.borderColor
        }
        border.width: {
            if (root.isCurrent) return 3
            return 2
        }
        
        // 悬停效果
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            
            onEntered: {
                if (!root.isCurrent) {
                    moduleBackground.border.color = Qt.lighter(root.accentColor, 1.3)
                }
            }
            
            onExited: {
                if (!root.isCurrent) {
                    moduleBackground.border.color = root.isActive ? root.accentColor : root.borderColor
                }
            }
            
            onClicked: {
                root.moduleClicked()
            }
        }
    }
    
    // 内容
    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8
        
        // 标题行
        Row {
            width: parent.width
            spacing: 10
            
            // 图标
            Rectangle {
                width: 36
                height: 36
                radius: 8
                color: root.isActive ? Qt.darker(root.accentColor, 1.5) : Qt.darker(root.backgroundColor, 1.5)
                
                Text {
                    anchors.centerIn: parent
                    text: root.icon
                    font.pixelSize: 18
                    color: root.textColor
                }
            }
            
            // 标题和描述
            Column {
                width: parent.width - 46
                spacing: 2
                
                Text {
                    text: root.title
                    font.pixelSize: 16
                    font.bold: true
                    color: root.textColor
                    width: parent.width
                    elide: Text.ElideRight
                }
                
                Text {
                    text: root.description
                    font.pixelSize: 12
                    color: root.textSecondaryColor
                    width: parent.width
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }
        
        // 进度条
        Rectangle {
            width: parent.width
            height: 6
            radius: 3
            color: root.borderColor
            
            Rectangle {
                width: parent.width * (root.status.progress / 100)
                height: 6
                radius: 3
                color: root.accentColor
            }
        }
        
        // 任务状态
        Row {
            width: parent.width
            spacing: 8
            
            // 任务计数
            Rectangle {
                width: 60
                height: 24
                radius: 4
                color: Qt.darker(root.backgroundColor, 1.5)
                
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    Text {
                        text: "📋"
                        font.pixelSize: 12
                        color: root.textSecondaryColor
                    }
                    
                    Text {
                        text: root.status.tasks.filter(t => t.status === "completed").length + "/" + root.status.tasks.length
                        font.pixelSize: 12
                        font.bold: true
                        color: root.textColor
                    }
                }
            }
            
            // 进度百分比
            Rectangle {
                width: 60
                height: 24
                radius: 4
                color: Qt.darker(root.backgroundColor, 1.5)
                
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    Text {
                        text: "📈"
                        font.pixelSize: 12
                        color: root.textSecondaryColor
                    }
                    
                    Text {
                        text: root.status.progress + "%"
                        font.pixelSize: 12
                        font.bold: true
                        color: root.textColor
                    }
                }
            }
            
            // 最后更新
            Rectangle {
                width: parent.width - 128
                height: 24
                radius: 4
                color: Qt.darker(root.backgroundColor, 1.5)
                
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    Text {
                        text: "🕒"
                        font.pixelSize: 12
                        color: root.textSecondaryColor
                    }
                    
                    Text {
                        text: root.status.lastUpdate
                        font.pixelSize: 12
                        color: root.textSecondaryColor
                        width: parent.width - 20
                        elide: Text.ElideRight
                    }
                }
            }
        }
        
        // 操作按钮
        Row {
            width: parent.width
            spacing: 8
            
            Repeater {
                model: root.actions
                
                Rectangle {
                    width: (parent.width - 8) / root.actions.length
                    height: 28
                    radius: 6
                    
                    color: {
                        if (modelData.primary) {
                            return root.isActive ? root.accentColor : Qt.darker(root.accentColor, 1.5)
                        } else {
                            return Qt.darker(root.backgroundColor, 1.5)
                        }
                    }
                    
                    border.color: root.borderColor
                    border.width: 1
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        
                        Text {
                            text: modelData.icon
                            font.pixelSize: 12
                            color: modelData.primary ? "white" : root.textSecondaryColor
                        }
                        
                        Text {
                            text: modelData.label
                            font.pixelSize: 12
                            font.bold: modelData.primary
                            color: modelData.primary ? "white" : root.textSecondaryColor
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.actionTriggered(modelData.id)
                        }
                    }
                }
            }
        }
    }
    
    // 状态指示器
    Rectangle {
        width: 16
        height: 16
        radius: 8
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        
        color: {
            if (root.isCurrent) return Qt.lighter(root.accentColor, 1.2)
            if (root.isActive) return root.accentColor
            return "transparent"
        }
        
        border.color: root.backgroundColor
        border.width: 2
        
        // 当前步骤的脉冲效果
        Rectangle {
            anchors.centerIn: parent
            width: 24
            height: 24
            radius: 12
            color: "transparent"
            border.color: root.isCurrent ? root.accentColor : "transparent"
            border.width: 2
            opacity: root.isCurrent ? 0.5 : 0
            
            SequentialAnimation on scale {
                running: root.isCurrent
                loops: Animation.Infinite
                
                NumberAnimation {
                    from: 1.0
                    to: 1.2
                    duration: 1000
                }
                NumberAnimation {
                    from: 1.2
                    to: 1.0
                    duration: 1000
                }
            }
        }
    }
    
    // 工具提示
    Rectangle {
        id: tooltip
        width: 200
        height: 120
        radius: 8
        color: root.backgroundColor
        border.color: root.accentColor
        border.width: 1
        visible: false
        z: 1000
        
        x: -90
        y: -130
        
        Column {
            anchors.centerIn: parent
            spacing: 6
            width: parent.width * 0.9
            
            Text {
                text: root.title
                font.pixelSize: 14
                font.bold: true
                color: root.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }
            
            Text {
                text: root.description
                font.pixelSize: 11
                color: root.textSecondaryColor
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            
            // 任务列表
            Column {
                width: parent.width
                spacing: 2
                
                Repeater {
                    model: Math.min(3, root.status.tasks.length)
                    
                    Row {
                        width: parent.width
                        spacing: 6
                        
                        Text {
                            text: {
                                var task = root.status.tasks[index]
                                if (task.status === "completed") return "✅"
                                if (task.status === "running") return "🔄"
                                return "⏳"
                            }
                            font.pixelSize: 10
                            color: root.textSecondaryColor
                        }
                        
                        Text {
                            text: root.status.tasks[index].name
                            font.pixelSize: 10
                            color: root.textSecondaryColor
                            width: parent.width - 20
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
    
    // 悬停时显示工具提示
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        
        onEntered: {
            tooltip.visible = true
        }
        
        onExited: {
            tooltip.visible = false
        }
    }
    
    // API方法
    function activate() {
        root.isActive = true
        root.isCurrent = true
    }
    
    function deactivate() {
        root.isActive = false
        root.isCurrent = false
    }
    
    function setAsCurrent() {
        root.isCurrent = true
        root.isActive = true
    }
    
    function updateStatus(newStatus) {
        root.status = newStatus
    }
    
    function addTask(taskName, taskStatus) {
        var tasks = root.status.tasks
        tasks.push({ name: taskName, status: taskStatus })
        var newStatus = {}
        for (var key in root.status) {
            newStatus[key] = root.status[key]
        }
        newStatus.tasks = tasks
        root.status = newStatus
    }
    
    function updateTask(taskName, newStatus) {
        var tasks = root.status.tasks
        for (var i = 0; i < tasks.length; i++) {
            if (tasks[i].name === taskName) {
                tasks[i].status = newStatus
                break
            }
        }
        var newStatusObj = {}
        for (var key in root.status) {
            newStatusObj[key] = root.status[key]
        }
        newStatusObj.tasks = tasks
        
        // 重新计算进度
        var completedTasks = 0
        for (var j = 0; j < tasks.length; j++) {
            if (tasks[j].status === "completed") {
                completedTasks++
            }
        }
        var progress = Math.round((completedTasks / tasks.length) * 100)
        newStatusObj.progress = progress
        
        root.status = newStatusObj
    }
    
    function setLastUpdate(timeText) {
        var newStatus = {}
        for (var key in root.status) {
            newStatus[key] = root.status[key]
        }
        newStatus.lastUpdate = timeText
        root.status = newStatus
    }
    
    // 模拟操作执行
    function executeAction(actionId) {
        console.log("执行核心模块操作:", root.moduleId, "动作:", actionId)
        
        // 触发信号
        root.actionTriggered(actionId)
        
        // 根据动作类型执行不同的行为
        switch(actionId) {
            case "open":
                console.log("打开模块:", root.title)
                root.moduleClicked()
                break
            case "config":
                console.log("配置模块:", root.title)
                break
            case "run":
                console.log("运行模块:", root.title)
                // 模拟运行过程
                root.updateStatus({
                    tasks: root.status.tasks,
                    progress: 0,
                    lastUpdate: "运行中..."
                })
                
                // 模拟进度更新
                var progress = 0
                var timer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 100; running: true; repeat: true }', root)
                timer.triggered.connect(function() {
                    progress += 5
                    if (progress <= 100) {
                        root.updateStatus({
                            tasks: root.status.tasks,
                            progress: progress,
                            lastUpdate: "运行中..." + progress + "%"
                        })
                    } else {
                        timer.stop()
                        timer.destroy()
                        // 创建新的任务数组，将所有任务标记为完成
                        var completedTasks = []
                        for (var k = 0; k < root.status.tasks.length; k++) {
                            var task = root.status.tasks[k]
                            completedTasks.push({
                                name: task.name,
                                status: "completed"
                            })
                        }
                        root.updateStatus({
                            tasks: completedTasks,
                            progress: 100,
                            lastUpdate: "刚刚完成"
                        })
                        console.log("模块运行完成:", root.title)
                    }
                })
                break
        }
    }
}
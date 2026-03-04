// ProcessNode.qml - 流程节点组件，统一数据源添加和因子切换的交互方式
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: 120
    height: 140
    
    // 属性
    property string nodeId: ""
    property string title: "流程节点"
    property string description: "节点描述"
    property string icon: "○"
    property color nodeColor: "#00bcd4"
    property color backgroundColor: "#0f1738"
    property color textColor: "white"
    property color textSecondaryColor: "#b0b0b0"
    property bool isActive: false
    property bool isCompleted: false
    property bool isCurrent: false
    property int stepNumber: 1
    property string actionType: "modal" // modal, page, inline, external
    
    // 信号
    signal nodeClicked()
    signal actionTriggered(string actionType)
    signal statusChanged(bool isCompleted)
    
    // 背景
    Rectangle {
        id: nodeBackground
        anchors.fill: parent
        color: root.backgroundColor
        radius: 8
        border.color: {
            if (root.isCurrent) return Qt.lighter(root.nodeColor, 1.5)
            if (root.isActive) return root.nodeColor
            if (root.isCompleted) return "#4caf50"
            return "#2d3a7c"
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
                    nodeBackground.border.color = Qt.lighter(root.nodeColor, 1.3)
                }
            }
            
            onExited: {
                if (!root.isCurrent) {
                    if (root.isActive) {
                        nodeBackground.border.color = root.nodeColor
                    } else if (root.isCompleted) {
                        nodeBackground.border.color = "#4caf50"
                    } else {
                        nodeBackground.border.color = "#2d3a7c"
                    }
                }
            }
            
            onClicked: {
                root.nodeClicked()
            }
        }
    }
    
    // 内容
    Column {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        
        // 步骤编号
        Rectangle {
            width: 24
            height: 24
            radius: 12
            anchors.horizontalCenter: parent.horizontalCenter
            
            color: {
                if (root.isCurrent) return Qt.lighter(root.nodeColor, 1.2)
                if (root.isActive) return root.nodeColor
                if (root.isCompleted) return "#4caf50"
                return "#3949ab"
            }
            
            border.color: root.backgroundColor
            border.width: 2
            
            Text {
                anchors.centerIn: parent
                text: root.stepNumber
                font.pixelSize: 12
                font.bold: true
                color: "white"
            }
            
            // 当前步骤的脉冲效果
            Rectangle {
                anchors.centerIn: parent
                width: 32
                height: 32
                radius: 16
                color: "transparent"
                border.color: root.isCurrent ? root.nodeColor : "transparent"
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
        
        // 图标
        Text {
            text: root.icon
            font.pixelSize: 20
            color: root.textColor
            anchors.horizontalCenter: parent.horizontalCenter
        }
        
        // 标题
        Text {
            text: root.title
            font.pixelSize: 12
            font.bold: true
            color: root.textColor
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 描述
        Text {
            text: root.description
            font.pixelSize: 10
            color: root.textSecondaryColor
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 操作按钮
        Rectangle {
            width: parent.width
            height: 24
            radius: 4
            anchors.horizontalCenter: parent.horizontalCenter
            
            color: {
                if (root.isCurrent) return Qt.darker(root.nodeColor, 1.2)
                if (root.isActive) return Qt.darker(root.nodeColor, 1.5)
                return Qt.darker(root.backgroundColor, 1.5)
            }
            
            border.color: root.nodeColor
            border.width: 1
            
            Text {
                anchors.centerIn: parent
                text: getActionText(root.actionType)
                font.pixelSize: 10
                font.bold: true
                color: root.isActive || root.isCurrent ? "white" : root.textSecondaryColor
            }
            
            MouseArea {
                anchors.fill: parent
                cursorShape: (root.isActive || root.isCurrent) ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled: root.isActive || root.isCurrent
                onClicked: {
                    root.actionTriggered(root.actionType)
                }
            }
        }
    }
    
    // 状态指示器
    Rectangle {
        width: 12
        height: 12
        radius: 6
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        
        color: {
            if (root.isCompleted) return "#4caf50"
            if (root.isActive) return root.nodeColor
            return "transparent"
        }
        
        border.color: root.backgroundColor
        border.width: 2
        
        // 完成状态的勾号
        Text {
            anchors.centerIn: parent
            text: root.isCompleted ? "✓" : ""
            font.pixelSize: 8
            font.bold: true
            color: "white"
            visible: root.isCompleted
        }
    }
    
    // 工具提示
    Rectangle {
        id: tooltip
        width: 150
        height: 80
        radius: 6
        color: root.backgroundColor
        border.color: root.nodeColor
        border.width: 1
        visible: false
        z: 1000
        
        x: -65
        y: -90
        
        Column {
            anchors.centerIn: parent
            spacing: 4
            width: parent.width * 0.9
            
            Text {
                text: root.title
                font.pixelSize: 12
                font.bold: true
                color: root.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }
            
            Text {
                text: root.description
                font.pixelSize: 10
                color: root.textSecondaryColor
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            
            Text {
                text: "操作类型: " + getActionTypeText(root.actionType)
                font.pixelSize: 9
                color: root.nodeColor
                anchors.horizontalCenter: parent.horizontalCenter
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
    
    // 辅助函数：获取操作文本
    function getActionText(actionType) {
        var actionMap = {
            "modal": "打开弹窗",
            "page": "切换页面", 
            "inline": "展开内容",
            "external": "外部链接"
        }
        return actionMap[actionType] || "开始操作"
    }
    
    // 辅助函数：获取操作类型文本
    function getActionTypeText(actionType) {
        var typeMap = {
            "modal": "弹窗操作",
            "page": "页面切换",
            "inline": "内联展开",
            "external": "外部系统"
        }
        return typeMap[actionType] || "未知类型"
    }
    
    // API方法
    function activate() {
        root.isActive = true
        root.isCurrent = true
        root.isCompleted = false
    }
    
    function complete() {
        root.isActive = false
        root.isCurrent = false
        root.isCompleted = true
        root.statusChanged(true)
    }
    
    function reset() {
        root.isActive = false
        root.isCurrent = false
        root.isCompleted = false
    }
    
    function setAsCurrent() {
        root.isCurrent = true
        root.isActive = true
        root.isCompleted = false
    }
    
    function setActionType(type) {
        if (["modal", "page", "inline", "external"].includes(type)) {
            root.actionType = type
        }
    }
    
    // 模拟操作执行
    function executeAction() {
        if (root.isActive || root.isCurrent) {
            console.log("执行流程节点操作:", root.nodeId, "类型:", root.actionType)
            
            // 根据操作类型执行不同的行为
            switch(root.actionType) {
                case "modal":
                    console.log("打开弹窗进行操作")
                    break
                case "page":
                    console.log("切换到相关页面")
                    break
                case "inline":
                    console.log("展开内联内容")
                    break
                case "external":
                    console.log("打开外部链接")
                    break
            }
            
            // 触发信号
            root.actionTriggered(root.actionType)
            
            // 如果是当前步骤，标记为完成
            if (root.isCurrent) {
                var timer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 500; running: true }', root)
                timer.triggered.connect(function() {
                    root.complete()
                    timer.destroy()
                })
            }
        }
    }
}
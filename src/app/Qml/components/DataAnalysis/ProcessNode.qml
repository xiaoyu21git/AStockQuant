// ProcessNode.qml - 流程节点组件，统一数据源添加和因子切换的交互方式
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: 94
    height: 60
    scale: root.isCurrent ? 1.03 : ((root.isActive || root.isCompleted) ? 0.95 : 0.91)
    opacity: root.isCurrent ? 1.0 : ((root.isActive || root.isCompleted) ? 0.9 : 0.82)
    z: root.isCurrent ? 3 : (root.isActive || root.isCompleted ? 2 : 1)
    
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
    property bool showDescription: false
    property int stepNumber: 1
    property string actionType: "modal" // modal, page, inline, external
    readonly property bool hoverActive: nodeHoverHandler.hovered
    
    // 信号
    signal nodeClicked()
    signal actionTriggered(string actionType)
    signal statusChanged(bool isCompleted)

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: 10
        color: Qt.rgba(0.36, 0.88, 0.95, 0.08)
        border.color: Qt.rgba(0.62, 0.96, 1, 0.34)
        border.width: 1
        visible: root.isCurrent
        opacity: root.isCurrent ? 0.55 : 0
        z: -1

        SequentialAnimation on scale {
            running: root.isCurrent
            loops: Animation.Infinite

            NumberAnimation {
                from: 0.98
                to: 1.045
                duration: 1350
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: 1.045
                to: 0.98
                duration: 1350
                easing.type: Easing.InOutSine
            }
        }

        SequentialAnimation on opacity {
            running: root.isCurrent
            loops: Animation.Infinite

            NumberAnimation {
                from: 0.28
                to: 0.58
                duration: 1350
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: 0.58
                to: 0.28
                duration: 1350
                easing.type: Easing.InOutSine
            }
        }
    }
    
    // 背景
    Rectangle {
        id: nodeBackground
        anchors.fill: parent
        color: {
            if (root.isCurrent) return Qt.lighter(root.backgroundColor, 1.18)
            if (root.isActive || root.isCompleted) return Qt.lighter(root.backgroundColor, 1.06)
            return root.backgroundColor
        }
        radius: 7
        border.color: {
            if (root.isCurrent) return Qt.lighter(root.nodeColor, 1.5)
            if (root.hoverActive) return Qt.lighter(root.nodeColor, 1.25)
            if (root.isActive) return root.nodeColor
            if (root.isCompleted) return "#4caf50"
            return "#2d3a7c"
        }
        border.width: {
            if (root.isCurrent) return 2
            return 1
        }

        Behavior on color {
            ColorAnimation { duration: 160 }
        }

        Behavior on border.color {
            ColorAnimation { duration: 160 }
        }

        Behavior on border.width {
            NumberAnimation { duration: 120 }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 6
            color: root.isCurrent ? Qt.rgba(1, 1, 1, 0.035) : "transparent"
            border.color: root.isCurrent ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
            border.width: root.isCurrent ? 1 : 0
        }
        
        HoverHandler {
            id: nodeHoverHandler
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: {
                root.nodeClicked()
            }
        }
    }

    Item {
        width: parent.width - 14
        height: 10
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3
        visible: root.isCurrent
        clip: true
        opacity: root.hoverActive ? 0.95 : 0.72
        z: 2

        Repeater {
            model: 2

            Rectangle {
                required property int index
                width: Math.max(28, parent.width * 0.55)
                height: 8
                y: 1
                radius: 4
                color: "transparent"

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.28; color: Qt.rgba(0.56, 0.93, 0.98, 0.03) }
                    GradientStop { position: 0.58; color: Qt.rgba(0.56, 0.93, 0.98, 0.24) }
                    GradientStop { position: 0.78; color: Qt.rgba(1, 1, 1, 0.16) }
                    GradientStop { position: 1.0; color: "transparent" }
                }

                NumberAnimation on x {
                    from: -width - (index * 18)
                    to: parent.width + (index * 10)
                    duration: 2200 + (index * 260)
                    loops: Animation.Infinite
                    running: root.isCurrent
                }
            }
        }
    }
    
    // 内容
    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 3
        
        // 步骤编号
        Rectangle {
            width: 20
            height: 20
            radius: 10
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
                font.pixelSize: 10
                font.bold: true
                color: "white"
            }
            
            // 当前步骤的脉冲效果
            Rectangle {
                anchors.centerIn: parent
                width: 26
                height: 26
                radius: 13
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
            font.pixelSize: 15
            color: root.isCurrent ? root.textColor : Qt.lighter(root.textSecondaryColor, 1.1)
            anchors.horizontalCenter: parent.horizontalCenter
        }
        
        // 标题
        Text {
            text: root.title
            font.pixelSize: 10
            font.bold: true
            color: root.isCurrent ? root.textColor : Qt.lighter(root.textSecondaryColor, 1.2)
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 1
            elide: Text.ElideRight
        }
        
        // 描述
        Text {
            text: root.description
            font.pixelSize: 9
            color: root.textSecondaryColor
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 1
            elide: Text.ElideRight
            visible: root.showDescription && !!root.description
        }
    }

    Rectangle {
        width: parent ? parent.width - 12 : root.width - 12
        height: 16
        radius: 4
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 5
        visible: root.isCurrent && root.hoverActive
        color: Qt.darker(root.nodeColor, 1.2)
        border.color: root.nodeColor
        border.width: 1
        z: 4

        Text {
            anchors.centerIn: parent
            text: getActionText(root.actionType)
            font.pixelSize: 8
            font.bold: true
            color: "white"
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            enabled: root.isCurrent
            onClicked: {
                root.actionTriggered(root.actionType)
            }
        }
    }
    
    // 状态指示器
    Rectangle {
        width: 10
        height: 10
        radius: 5
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 4
        
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
            font.pixelSize: 7
            font.bold: true
            color: "white"
            visible: root.isCompleted
        }
    }
    
    // 工具提示
    Rectangle {
        id: tooltip
        width: 140
        height: 72
        radius: 6
        color: root.backgroundColor
        border.color: root.nodeColor
        border.width: 1
        visible: root.hoverActive
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

    Behavior on scale {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }

    Behavior on opacity {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }
    
    // 辅助函数：获取操作文本
    function getActionText(actionType) {
        var actionMap = {
            "modal": "弹窗",
            "page": "页面", 
            "inline": "展开",
            "external": "外链"
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
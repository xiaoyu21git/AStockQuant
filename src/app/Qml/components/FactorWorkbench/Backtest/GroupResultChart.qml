// GroupResultChart.qml
// 分组结果图表组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

/**
 * 分组结果图表组件
 * 可视化展示因子分组回测结果
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property var groupResults: []
    property string chartType: "bar"  // bar, line, scatter
    
    // ============ UI ============
    
    Rectangle {
        anchors.fill: parent
        radius: 12
        color: "#1E293B"
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            
            // 标题
            Text {
                text: "📊 分组绩效对比"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            // 图表类型选择
            RowLayout {
                spacing: 12
                
                Text {
                    text: "图表类型:"
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
                
                // 柱状图
                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 32
                    radius: 6
                    color: chartType === "bar" ? "#3B82F6" : "#334155"
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        
                        Text {
                            text: "📊"
                            font.pixelSize: 12
                            color: chartType === "bar" ? "white" : "#F1F5F9"
                        }
                        
                        Text {
                            text: "柱状图"
                            font.pixelSize: 12
                            color: chartType === "bar" ? "white" : "#F1F5F9"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chartType = "bar"
                    }
                }
                
                // 折线图
                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 32
                    radius: 6
                    color: chartType === "line" ? "#3B82F6" : "#334155"
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        
                        Text {
                            text: "📈"
                            font.pixelSize: 12
                            color: chartType === "line" ? "white" : "#F1F5F9"
                        }
                        
                        Text {
                            text: "折线图"
                            font.pixelSize: 12
                            color: chartType === "line" ? "white" : "#F1F5F9"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chartType = "line"
                    }
                }
                
                // 散点图
                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 32
                    radius: 6
                    color: chartType === "scatter" ? "#3B82F6" : "#334155"
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        
                        Text {
                            text: "🔘"
                            font.pixelSize: 12
                            color: chartType === "scatter" ? "white" : "#F1F5F9"
                        }
                        
                        Text {
                            text: "散点图"
                            font.pixelSize: 12
                            color: chartType === "scatter" ? "white" : "#F1F5F9"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chartType = "scatter"
                    }
                }
                
                Item { Layout.fillWidth: true }
            }
            
            // 图表区域
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: "#0F172A"
                
                // 图表内容
                Item {
                    anchors.fill: parent
                    anchors.margins: 16
                    
                    // 坐标轴
                    // Y轴（收益）
                    Column {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 40
                        spacing: 0
                        
                        Repeater {
                            model: 5
                            
                            Text {
                                width: parent.width
                                height: 20
                                text: (4 - index) * 25 + "%"
                                font.pixelSize: 10
                                color: "#94A3B8"
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                    
                    // X轴（组别）
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 40
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 30
                        spacing: 0
                        
                        Repeater {
                            model: groupResults.length
                            
                            Text {
                                width: Math.max(40, (parent.width - 40) / groupResults.length)
                                height: parent.height
                                text: groupResults[index] ? (groupResults[index].groupName || ("组" + (index + 1))) : ""
                                font.pixelSize: 10
                                color: "#94A3B8"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                    
                    // 图表主体
                    Item {
                        anchors.left: parent.left
                        anchors.leftMargin: 40
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 30
                        
                        // 网格线
                        Repeater {
                            model: 5
                            
                            Rectangle {
                                width: parent.width
                                height: 1
                                y: parent.height * (index / 4)
                                color: "#334155"
                                opacity: 0.5
                            }
                        }
                        
                        // 数据点
                        Repeater {
                            model: groupResults
                            
                            Item {
                                property real xPos: (index + 0.5) * (parent.width / groupResults.length)
                                property real yPos: parent.height * (1 - (modelData.return || 0) / 1.0) // 假设最大收益100%
                                property real barWidth: parent.width / groupResults.length * 0.6
                                
                                // 柱状图
                                Rectangle {
                                    visible: chartType === "bar"
                                    width: barWidth
                                    height: parent.height - yPos
                                    x: xPos - width / 2
                                    y: yPos
                                    radius: 2
                                    color: getBarColor(index, modelData.return)
                                    
                                    // 数值标签
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.top
                                        anchors.bottomMargin: 4
                                        text: modelData.return ? (modelData.return * 100).toFixed(1) + "%" : "N/A"
                                        font.pixelSize: 10
                                        color: "#F1F5F9"
                                    }
                                }
                                
                                // 折线图点
                                Rectangle {
                                    visible: chartType === "line"
                                    width: 8
                                    height: 8
                                    radius: 4
                                    x: xPos - width / 2
                                    y: yPos - height / 2
                                    color: getBarColor(index, modelData.return)
                                    border.width: 2
                                    border.color: "#0F172A"
                                    
                                    // 数值标签
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.top
                                        anchors.bottomMargin: 4
                                        text: modelData.return ? (modelData.return * 100).toFixed(1) + "%" : "N/A"
                                        font.pixelSize: 10
                                        color: "#F1F5F9"
                                    }
                                }
                                
                                // 散点图点
                                Rectangle {
                                    visible: chartType === "scatter"
                                    width: 12
                                    height: 12
                                    radius: 6
                                    x: xPos - width / 2
                                    y: yPos - height / 2
                                    color: getBarColor(index, modelData.return)
                                    opacity: 0.8
                                    
                                    // 数值标签
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.top
                                        anchors.bottomMargin: 4
                                        text: modelData.return ? (modelData.return * 100).toFixed(1) + "%" : "N/A"
                                        font.pixelSize: 10
                                        color: "#F1F5F9"
                                    }
                                }
                            }
                        }
                        
                        // 折线图连接线
                        Canvas {
                            visible: chartType === "line"
                            anchors.fill: parent
                            
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                
                                if (groupResults.length < 2) return
                                
                                ctx.strokeStyle = "#3B82F6"
                                ctx.lineWidth = 2
                                ctx.lineCap = "round"
                                ctx.lineJoin = "round"
                                
                                ctx.beginPath()
                                
                                for (var i = 0; i < groupResults.length; i++) {
                                    var xPos = (i + 0.5) * (width / groupResults.length)
                                    var yPos = height * (1 - (groupResults[i].return || 0) / 1.0)
                                    
                                    if (i === 0) {
                                        ctx.moveTo(xPos, yPos)
                                    } else {
                                        ctx.lineTo(xPos, yPos)
                                    }
                                }
                                
                                ctx.stroke()
                            }
                        }
                    }
                    
                    // 图例
                    Row {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 8
                        spacing: 12
                        
                        // 高收益组
                        Row {
                            spacing: 6
                            
                            Rectangle {
                                width: 12
                                height: 12
                                radius: 2
                                color: "#EF4444"
                            }
                            
                            Text {
                                text: "高收益组"
                                font.pixelSize: 10
                                color: "#94A3B8"
                            }
                        }
                        
                        // 中等收益组
                        Row {
                            spacing: 6
                            
                            Rectangle {
                                width: 12
                                height: 12
                                radius: 2
                                color: "#3B82F6"
                            }
                            
                            Text {
                                text: "中等收益组"
                                font.pixelSize: 10
                                color: "#94A3B8"
                            }
                        }
                        
                        // 低收益组
                        Row {
                            spacing: 6
                            
                            Rectangle {
                                width: 12
                                height: 12
                                radius: 2
                                color: "#10B981"
                            }
                            
                            Text {
                                text: "低收益组"
                                font.pixelSize: 10
                                color: "#94A3B8"
                            }
                        }
                    }
                }
            }
            
            // 统计摘要
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                radius: 8
                color: "#0F172A"
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 16
                    
                    // 最高收益组
                    ColumnLayout {
                        spacing: 2
                        
                        Text {
                            text: "最高收益组"
                            font.pixelSize: 10
                            color: "#94A3B8"
                        }
                        
                        Text {
                            text: getTopGroupInfo()
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: "#EF4444"
                        }
                    }
                    
                    // 最低收益组
                    ColumnLayout {
                        spacing: 2
                        
                        Text {
                            text: "最低收益组"
                            font.pixelSize: 10
                            color: "#94A3B8"
                        }
                        
                        Text {
                            text: getBottomGroupInfo()
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: "#10B981"
                        }
                    }
                    
                    // 收益差距
                    ColumnLayout {
                        spacing: 2
                        
                        Text {
                            text: "收益差距"
                            font.pixelSize: 10
                            color: "#94A3B8"
                        }
                        
                        Text {
                            text: getReturnSpread()
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: "#F59E0B"
                        }
                    }
                    
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    
    // 获取柱状图颜色
    function getBarColor(index, returnValue) {
        if (!returnValue) return "#64748B"
        
        // 根据收益值确定颜色
        if (returnValue > 0.1) return "#EF4444"  // 高收益
        if (returnValue > 0) return "#F97316"    // 中等收益
        if (returnValue > -0.1) return "#22C55E" // 轻微回落
        return "#10B981"                         // 负收益
    }
    
    // 获取最高收益组信息
    function getTopGroupInfo() {
        if (groupResults.length === 0) return "N/A"
        
        var maxReturn = -Infinity
        var topGroup = ""
        
        for (var i = 0; i < groupResults.length; i++) {
            var group = groupResults[i]
            if (group.return > maxReturn) {
                maxReturn = group.return
                topGroup = group.groupName || ("组" + (i + 1))
            }
        }
        
        return topGroup + " (" + (maxReturn * 100).toFixed(1) + "%)"
    }
    
    // 获取最低收益组信息
    function getBottomGroupInfo() {
        if (groupResults.length === 0) return "N/A"
        
        var minReturn = Infinity
        var bottomGroup = ""
        
        for (var i = 0; i < groupResults.length; i++) {
            var group = groupResults[i]
            if (group.return < minReturn) {
                minReturn = group.return
                bottomGroup = group.groupName || ("组" + (i + 1))
            }
        }
        
        return bottomGroup + " (" + (minReturn * 100).toFixed(1) + "%)"
    }
    
    // 获取收益差距
    function getReturnSpread() {
        if (groupResults.length === 0) return "N/A"
        
        var maxReturn = -Infinity
        var minReturn = Infinity
        
        for (var i = 0; i < groupResults.length; i++) {
            var returnValue = groupResults[i].return || 0
            if (returnValue > maxReturn) maxReturn = returnValue
            if (returnValue < minReturn) minReturn = returnValue
        }
        
        var spread = maxReturn - minReturn
        return (spread * 100).toFixed(1) + "%"
    }
    
    // 更新图表
    function updateChart() {
        // 强制重绘
        canvas.requestPaint()
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("分组结果图表初始化完成")
    }
}
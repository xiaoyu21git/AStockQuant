// FactorCardEnhanced.qml
// 增强版因子卡片组件，支持迷你图表和更多交互特性
// 遵循即时反馈、智能辅助、极简路径三大原则
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 6.0

/**
 * 增强版因子卡片组件
 * 支持迷你图表、分组收益预览、实时反馈等特性
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    // 因子基本数据
    property string factorId: ""
    property string factorName: ""
    property string displayName: ""
    property string majorCategory: "动量类"
    property string subCategory: "趋势动量"
    property string description: ""
    property string creator: ""
    property string createDate: ""
    
    // 性能指标
    property real icValue: 0.0
    property real irValue: 0.0
    property int validityDays: 20
    property real turnoverRate: 32  // %/年
    property bool isRecommended: false
    property bool isFavorite: false
    property string status: "ACTIVE"
    property var tags: []
    
    // 图表数据
    property var groupReturns: [0.12, 0.08, 0.05, 0.02, 0.01, -0.01, -0.03, -0.05, -0.08, -0.12]
    property bool showMiniChart: true
    property bool showGroupReturns: false
    
    // 交互状态
    property bool hovered: false
    property bool selected: false
    
    // 即时反馈设置
    property bool enableRealTimeFeedback: true
    property int feedbackDelay: 50  // ms
    
    // 信号
    signal clicked()
    signal doubleClicked()
    signal favoriteToggled(bool favorite)
    signal previewRequested()
    signal analyzeRequested()
    signal addToPortfolio()
    signal editRequested()
    signal deleteRequested()
    
    // ============ 私有属性 ============
    
    readonly property string categoryColorStr: getFactorColor(majorCategory)
    readonly property color categoryColor: Qt.color(categoryColorStr)
    readonly property string statusColorStr: getStatusColor(status)
    readonly property color statusColor: Qt.color(statusColorStr)
    readonly property string categoryIcon: getFactorIcon(majorCategory)
    readonly property string statusIcon: getStatusIcon(status)
    
    // 迷你图表数据
    property var chartData: calculateChartData()
    
    // ============ 视觉属性 ============
    
    implicitWidth: 190  // 缩小宽度约1/3，更紧凑
    implicitHeight: 260  // 保持高度不变
    radius: 10
    color: {
        if (selected) return Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.15)
        if (hovered) return "#334155"  // bgTertiary
        return "#1E293B"  // bgSecondary
    }
    border.color: selected ? categoryColor : "#475569"  // borderDefault
    border.width: selected ? 2 : 1
    
    layer.enabled: true
    layer.effect: DropShadow {
        horizontalOffset: 0
        verticalOffset: 2
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.2)
        spread: 0.1
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        onEntered: {
            hovered = true
            hoverAnimation.start()
        }

        onExited: {
            hovered = false
            hoverAnimation.start()
        }

        onClicked: {
            clickAnimation.start()
            root.clicked()
        }

        onDoubleClicked: {
            root.doubleClicked()
        }
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12  // 缩小边距
        spacing: 8  // 缩小间距
        
        // 标题行（图标、标题、状态）
        RowLayout {
            spacing: 8
            
            // 类别图标（缩小）
            Rectangle {
                width: 32
                height: 32
                radius: 6
                color: Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: categoryIcon
                    font.pixelSize: 14
                }
            }
            
                // 标题和元信息（缩小高度）
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2  // 缩小间距
                    
                    // 标题行（单行显示，更紧凑）
                    RowLayout {
                        spacing: 6
                        
                        Text {
                            text: displayName || factorName
                            font.pixelSize: 16  // 缩小字体
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        
                        // 状态徽章
                        Rectangle {
                            visible: status !== "ACTIVE"
                            width: statusText.width + 10
                            height: 18
                            radius: 9
                            color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.2)
                            
                            Text {
                                id: statusText
                                anchors.centerIn: parent
                                text: getStatusText(status)
                                font.pixelSize: 9
                                font.weight: Font.Medium
                                color: statusColor
                            }
                        }
                        
                        // 收藏按钮
                        Text {
                            text: isFavorite ? "⭐" : "☆"
                            font.pixelSize: 14  // 缩小字体
                            color: isFavorite ? "#F59E0B" : "#64748B"
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    favoriteToggled(!isFavorite)
                                    
                                    // 即时反馈动画
                                    if (enableRealTimeFeedback) {
                                        favoriteAnimation.start()
                                    }
                                }
                            }
                            
                            ScaleAnimator {
                                id: favoriteAnimation
                                target: parent
                                from: 1.0
                                to: 1.3
                                duration: feedbackDelay
                                easing.type: Easing.OutBack
                                running: false
                                
                                onFinished: {
                                    reverseAnimation.start()
                                }
                            }
                            
                            ScaleAnimator {
                                id: reverseAnimation
                                target: favoriteAnimation.target
                                from: 1.3
                                to: 1.0
                                duration: feedbackDelay
                                easing.type: Easing.InBack
                                running: false
                            }
                        }
                    }
                    
                    // 类别和创建信息（单行显示）
                    RowLayout {
                        spacing: 8
                        
                        Text {
                            text: majorCategory + " · " + subCategory
                            font.pixelSize: 11  // 缩小字体
                            color: categoryColor
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 创建信息
                        Text {
                            text: creator + " · " + createDate
                            font.pixelSize: 9  // 缩小字体
                            color: "#64748B"
                            visible: creator && createDate
                        }
                    }
                }
        }
        
        // 描述区域（缩小字体和区域大小）
        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 32  // 缩小区域高度
            text: description
            font.pixelSize: 11  // 缩小字体
            color: "#94A3B8"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 性能指标行（调整为2行，每行2个指标）
        ColumnLayout {
            spacing: 4
            
            // 第一行：IC和IR
            RowLayout {
                spacing: 8
                
                // IC值（带趋势指示）
                PerformanceMetric {
                    label: "IC"
                    value: icValue
                    format: "%.3f"
                    metricColor: categoryColor
                    showTrend: true
                    trendDirection: icValue > 0.03 ? "up" : icValue < 0.02 ? "down" : "neutral"
                    tooltip: "信息系数，衡量因子预测能力"
                }
                
                // IR值
                PerformanceMetric {
                    label: "IR"
                    value: irValue
                    format: "%.2f"
                    metricColor: categoryColor
                    tooltip: "信息比率，衡量因子稳定性"
                }
            }
            
            // 第二行：换手率和有效期
            RowLayout {
                spacing: 8
                
                // 换手率
                PerformanceMetric {
                    label: "换手率"
                    value: turnoverRate
                    format: "%.0f"
                    unit: "%/年"
                    metricColor: categoryColor
                    tooltip: "年化换手率，衡量交易频率"
                }
                
                // 有效期
                PerformanceMetric {
                    label: "有效期"
                    value: validityDays
                    format: "%d"
                    unit: "天"
                    metricColor: categoryColor
                    tooltip: "因子有效持续时间"
                }
            }
        }
        
        // 迷你图表（悬停时显示）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: showMiniChart ? 40 : 0
            visible: showMiniChart
            
            Rectangle {
                anchors.fill: parent
                radius: 6
                color: "#0F172A"
                
                // 图表标题
                Text {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 6
                    text: "分组收益预览"
                    font.pixelSize: 10
                    color: "#94A3B8"
                }
                
                // 迷你图表
                Canvas {
                    id: miniChart
                    anchors.fill: parent
                    anchors.margins: 20
                    
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        
                        if (!chartData || chartData.length === 0) return
                        
                        var maxValue = Math.max.apply(null, chartData.map(Math.abs))
                        var scale = height / 2 / maxValue
                        var barWidth = width / chartData.length
                        
                        // 绘制零线
                        ctx.strokeStyle = "#475569"
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(0, height / 2)
                        ctx.lineTo(width, height / 2)
                        ctx.stroke()
                        
                        // 绘制柱状图
                        for (var i = 0; i < chartData.length; i++) {
                            var value = chartData[i]
                            var barHeight = Math.abs(value) * scale
                            var x = i * barWidth + barWidth * 0.1
                            var y = value > 0 ? height / 2 - barHeight : height / 2
                            var barColor = value > 0 ? 
                                          Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.8) :
                                          Qt.rgba(0.96, 0.26, 0.21, 0.8)  // 红色表示负收益
                            
                            ctx.fillStyle = barColor
                            ctx.fillRect(x, y, barWidth * 0.8, barHeight)
                        }
                    }
                    
                    Component.onCompleted: requestPaint()
                }
                
                // 悬停时显示详细信息
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    
                    onEntered: {
                        chartTooltip.visible = true
                        chartTooltip.x = mouseX
                        chartTooltip.y = mouseY
                    }
                    
                    onExited: {
                        chartTooltip.visible = false
                    }
                    
                    onPositionChanged: {
                        chartTooltip.x = mouseX
                        chartTooltip.y = mouseY
                        
                        // 计算当前分组
                        var groupIndex = Math.floor(mouseX / (width / groupReturns.length))
                        if (groupIndex >= 0 && groupIndex < groupReturns.length) {
                            chartTooltip.text = "分组 " + (groupIndex + 1) + ": " + 
                                               (groupReturns[groupIndex] * 100).toFixed(1) + "%"
                        }
                    }
                }
                
                // 图表工具提示
                Rectangle {
                    id: chartTooltip
                    width: tooltipText.width + 16
                    height: tooltipText.height + 8
                    radius: 4
                    color: "#1E293B"
                    border.color: categoryColor
                    border.width: 1
                    visible: false
                    z: 1000
                    
                    Text {
                        id: tooltipText
                        anchors.centerIn: parent
                        text: "悬停查看分组收益"
                        font.pixelSize: 10
                        color: "#F1F5F9"
                    }
                }
            }
        }
        
        // 标签和操作按钮
        RowLayout {
            spacing: 12
            
            // 标签区域
            Flow {
                Layout.fillWidth: true
                spacing: 4
                
                Repeater {
                    model: tags && tags.length > 0 ? tags : ["动量", "技术指标", "趋势"]
                    
                    delegate: Rectangle {
                        height: 20
                        radius: 10
                        color: Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.2)
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 10
                            font.weight: Font.Medium
                            color: categoryColor
                            leftPadding: 8
                            rightPadding: 8
                        }
                    }
                }
                
                // 推荐标签
                Rectangle {
                    visible: isRecommended
                    height: 20
                    radius: 10
                    color: "#10B98120"  // success color with alpha
                    
                    Row {
                        spacing: 4
                        anchors.centerIn: parent
                        
                        Text {
                            text: "🔥"
                            font.pixelSize: 10
                            color: "#10B981"
                        }
                        
                        Text {
                            text: "推荐"
                            font.pixelSize: 10
                            font.weight: Font.Medium
                            color: "#10B981"
                            rightPadding: 8
                        }
                    }
                }
            }
            
            // 快捷操作按钮
            Row {
                spacing: 4
                
                // 预览按钮
                ActionButton {
                    icon: "👁️"
                    tooltip: "预览因子"
                    buttonColor: "#3B82F6"
                    onClicked: previewRequested()
                }
                
                // 分析按钮
                ActionButton {
                    icon: "📊"
                    tooltip: "详细分析"
                    buttonColor: "#10B981"
                    onClicked: analyzeRequested()
                }
                
                // 添加到组合按钮
                ActionButton {
                    icon: "➕"
                    tooltip: "添加到组合"
                    buttonColor: "#F59E0B"
                    onClicked: addToPortfolio()
                }
                
                // 编辑按钮
                ActionButton {
                    icon: "✏️"
                    tooltip: "编辑因子"
                    buttonColor: "#8B5CF6"
                    onClicked: editRequested()
                }
                
                // 删除按钮
                ActionButton {
                    icon: "🗑️"
                    tooltip: "删除因子"
                    buttonColor: "#EF4444"
                    onClicked: deleteRequested()
                }
            }
        }
    }
    
    // ============ 工具函数 ============
    
    // 根据因子大类获取颜色
    function getFactorColor(majorCategory) {
        if (!majorCategory) return "#94A3B8";
        
        switch (majorCategory) {
            case "动量类": return "#3B82F6";
            case "价值类": return "#F59E0B";
            case "质量类": return "#10B981";
            case "成长类": return "#8B5CF6";
            case "情绪类": return "#EC4899";
            case "波动类": return "#EF4444";
            case "流动性类": return "#06B6D4";
            case "预期类": return "#F97316";
            case "恐慌类": return "#8B4513";
            default: return "#94A3B8";
        }
    }
    
    // 根据因子大类获取图标
    function getFactorIcon(majorCategory) {
        switch (majorCategory) {
            case "动量类": return "📊";
            case "价值类": return "💰";
            case "质量类": return "📈";
            case "成长类": return "🚀";
            case "情绪类": return "🧠";
            case "波动类": return "📉";
            case "流动性类": return "💧";
            case "预期类": return "🔮";
            case "恐慌类": return "🛡️";
            default: return "📊";
        }
    }
    
    // 根据状态获取颜色
    function getStatusColor(status) {
        switch (status.toUpperCase()) {
            case "ACTIVE": return "#10B981";
            case "EXPERIMENTAL": return "#F59E0B";
            case "DEPRECATED": return "#EF4444";
            case "PENDING": return "#3B82F6";
            default: return "#64748B";
        }
    }
    
    // 根据状态获取文本
    function getStatusText(status) {
        switch (status.toUpperCase()) {
            case "ACTIVE": return "活跃";
            case "EXPERIMENTAL": return "实验";
            case "DEPRECATED": return "废弃";
            case "PENDING": return "待审核";
            default: return "未知";
        }
    }
    
    // 根据状态获取图标
    function getStatusIcon(status) {
        switch (status.toUpperCase()) {
            case "ACTIVE": return "✅";
            case "EXPERIMENTAL": return "🔬";
            case "DEPRECATED": return "🗑️";
            case "PENDING": return "⏳";
            default: return "❓";
        }
    }
    
    // 计算图表数据
    function calculateChartData() {
        if (groupReturns && groupReturns.length > 0) {
            return groupReturns
        }
        
        // 生成模拟数据
        var data = []
        for (var i = 0; i < 10; i++) {
            data.push((icValue * 2) * (1 - i / 10))
        }
        return data
    }
    
    // ============ 动画效果 ============
    
    // 悬停动画
    ScaleAnimator {
        id: hoverAnimation
        target: root
        from: hovered ? 1.0 : 1.02
        to: hovered ? 1.02 : 1.0
        duration: 150
        easing.type: Easing.OutQuad
        running: false
    }
    
    // 点击动画
    SequentialAnimation {
        id: clickAnimation
        
        ScaleAnimator {
            target: root
            from: 1.0
            to: 0.98
            duration: 50
            easing.type: Easing.InQuad
        }
        
        ScaleAnimator {
            target: root
            from: 0.98
            to: 1.0
            duration: 100
            easing.type: Easing.OutElastic
        }
    }
    
    // ============ 自定义组件 ============
    
    // 性能指标组件
    component PerformanceMetric: Rectangle {
        property string label: ""
        property real value: 0
        property string format: "%.2f"
        property string unit: ""
        property color metricColor: "#3B82F6"
        property bool showTrend: false
        property string trendDirection: "neutral"  // "up", "down", "neutral"
        property string tooltip: ""
        
        implicitWidth: metricContent.width + 12
        implicitHeight: 28
        radius: 6
        color: {
            // 确保metricColor是有效的颜色对象
            var colorObj = metricColor
            if (typeof colorObj === 'string') {
                // 如果是字符串，尝试转换为颜色对象
                try {
                    colorObj = Qt.color(colorObj)
                } catch(e) {
                    colorObj = Qt.color("#3B82F6") // 默认颜色
                }
            }
            
            // 检查colorObj是否有r,g,b属性
            if (colorObj && typeof colorObj.r !== 'undefined') {
                return Qt.rgba(colorObj.r, colorObj.g, colorObj.b, 0.1)
            }
            
            // 默认颜色
            return Qt.rgba(0.231, 0.510, 0.965, 0.1) // #3B82F6 with alpha
        }
        
        Row {
            id: metricContent
            anchors.centerIn: parent
            spacing: 4
            
            Text {
                text: label + ":"
                font.pixelSize: 11
                color: "#94A3B8"
            }
            
            Text {
                text: {
                    var formatted = format.replace("%d", Math.round(value)).replace("%.2f", value.toFixed(2)).replace("%.3f", value.toFixed(3))
                    return formatted + (unit ? " " + unit : "")
                }
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: metricColor
            }
            
            // 趋势指示器
            Text {
                visible: showTrend
                text: trendDirection === "up" ? "↑" : 
                      trendDirection === "down" ? "↓" : "→"
                font.pixelSize: 11
                color: trendDirection === "up" ? "#10B981" : 
                       trendDirection === "down" ? "#EF4444" : "#94A3B8"
            }
        }
        
        // 工具提示
        MouseArea {
            anchors.fill: parent
            hoverEnabled: tooltip !== ""
            acceptedButtons: Qt.NoButton
            
            onEntered: {
                if (tooltip) {
                    metricTooltip.visible = true
                }
            }
            
            onExited: {
                metricTooltip.visible = false
            }
        }
        
        Rectangle {
            id: metricTooltip
            width: tooltipText.width + 16
            height: tooltipText.height + 8
            radius: 4
            color: "#1E293B"
            border.color: parent.metricColor
            border.width: 1
            visible: false
            z: 1000
            
            x: parent.width / 2 - width / 2
            y: -height - 5
            
            Text {
                id: tooltipText
                anchors.centerIn: parent
                text: parent.parent.tooltip
                font.pixelSize: 10
                color: "#F1F5F9"
                wrapMode: Text.Wrap
                width: 150
            }
        }
    }
    
    // 操作按钮组件
    component ActionButton: Rectangle {
        property string icon: ""
        property string tooltip: ""
        property color buttonColor: "#3B82F6"
        signal clicked()
        
        width: 28
        height: 28
        radius: 6
        color: {
            // 安全地访问 categoryColor
            var catColor = root.categoryColor
            if (catColor && typeof catColor.r !== 'undefined') {
                return Qt.rgba(catColor.r, catColor.g, catColor.b, 0.2)
            }
            return Qt.rgba(0.231, 0.510, 0.965, 0.2) // 默认颜色 #3B82F6 with alpha
        }
        
        Text {
            anchors.centerIn: parent
            text: icon
            font.pixelSize: 12
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            
            onClicked: {
                parent.clicked()
                
                // 按钮点击动画
                buttonClickAnimation.start()
            }
        }
        
        ScaleAnimator {
            id: buttonClickAnimation
            target: parent
            from: 1.0
            to: 0.9
            duration: 50
            easing.type: Easing.InQuad
            running: false
            
            onFinished: {
                buttonRestoreAnimation.start()
            }
        }
        
        ScaleAnimator {
            id: buttonRestoreAnimation
            target: buttonClickAnimation.target
            from: 0.9
            to: 1.0
            duration: 100
            easing.type: Easing.OutElastic
            running: false
        }
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化图表数据
        chartData = calculateChartData()
        
        // 请求重绘图表
        if (miniChart) {
            miniChart.requestPaint()
        }
    }
}
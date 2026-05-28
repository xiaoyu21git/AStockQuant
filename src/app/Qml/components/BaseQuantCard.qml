// BaseQuantCard.qml
// 统一量化卡片基础组件，包含所有通用功能
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 6.0
import "./Base" as BaseComponents

/**
 * 统一量化卡片基础组件
 * 提供因子、策略等量化实体的通用卡片功能
 * 遵循统一视觉语言和交互模式
 */
Rectangle {
    id: root

    BaseComponents.Constants {
        id: baseConstants
    }
    
    // ============ 基础属性 ============
    
    // 实体基本信息
    property string entityId: ""              // 实体ID（因子ID/策略ID等）
    property string displayName: ""           // 显示名称
    property string entityType: ""            // 实体类型：factor, strategy, portfolio等
    property string category: ""              // 类别（因子大类/策略类型）
    property string subCategory: ""           // 子类别
    property string description: ""           // 描述
    property string creator: ""               // 创建者
    property string createDate: ""            // 创建日期
    property string status: "ACTIVE"          // 状态：ACTIVE, EXPERIMENTAL, DEPRECATED, RUNNING, STOPPED, PAUSED等
    property bool isFavorite: false           // 是否收藏
    property bool isRecommended: false        // 是否推荐
    property var tags: []                     // 标签数组
    property color categoryColor: baseConstants.accentBlue  // 类别颜色
    
    // 性能指标（抽象属性，由子类具体化）
    property var performanceMetrics: []       // 性能指标数组，格式：[{label, value, format, unit, color, showTrend, trendDirection, tooltip}]
    property var additionalMetrics: []        // 附加指标数组，格式同performanceMetrics
    
    // 图表数据
    property var chartData: []                // 图表数据
    property bool showMiniChart: false        // 是否显示迷你图表
    property bool showGroupReturns: false     // 是否显示分组收益
    
        // 布局配置
    property int cardWidth: 190               // 卡片宽度
    property int cardHeight: 260              // 卡片高度
    property int spacingSmall: baseConstants.spacingSmall
    property int spacingMedium: baseConstants.spacingMedium
    property int spacingLarge: baseConstants.spacingLarge
    property int spacingXLarge: 16            // 大间距，用于卡片内边距
    property int borderRadius: baseConstants.borderRadiusLarge
    
    // 可配置的区域高度
    property int descriptionHeight: 32        // 描述区域高度
    property int miniChartHeight: 40          // 迷你图表高度
    property int performanceMetricHeight: 28  // 性能指标高度
    
    // 交互状态
    property bool hovered: false
    property bool selected: false
    property bool showActions: true           // 是否显示操作按钮
    
    // 交互控制
    property bool enableRealTimeFeedback: true
    property bool enableCardClick: true       // 是否启用卡片点击
    property int feedbackDelay: 50            // ms
    
    // ============ 信号 ============
    
    signal clicked()
    signal doubleClicked()
    signal favoriteToggled(bool favorite)
    signal entitySelected(string entityId)
    signal actionRequested(string action)     // 通用操作请求：preview, analyze, edit, delete, start, stop, pause, optimize等
    
    // ============ 私有属性 ============
    
    readonly property color statusColor: resolveColor(getStatusColor(status), baseConstants.textTertiary)
    readonly property string statusText: getStatusText(status)
    readonly property string categoryIcon: getCategoryIcon(category)
    readonly property color resolvedCategoryColor: resolveColor(categoryColor, baseConstants.accentBlue)
    
    // ============ 视觉属性 ============
    
    implicitWidth: cardWidth
    implicitHeight: cardHeight
    radius: borderRadius
    color: {
        if (selected) return Qt.rgba(resolvedCategoryColor.r, resolvedCategoryColor.g, resolvedCategoryColor.b, 0.15)
        if (hovered) return baseConstants.tertiaryBg
        return baseConstants.secondaryBg
    }
    border.color: selected ? resolvedCategoryColor : baseConstants.borderColor
    border.width: selected ? 2 : 1
    
    layer.enabled: true
    layer.effect: DropShadow {
        horizontalOffset: 0
        verticalOffset: 2
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.2)
        spread: 0.1
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: spacingXLarge  // 从spacingLarge改为spacingXLarge以增加控件与边框的距离
        spacing: spacingMedium
        
        // 标题行（图标、标题、状态）
        RowLayout {
            spacing: spacingSmall
            
            // 类别图标
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
            
            // 标题和元信息
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                
                // 标题行
                RowLayout {
                    spacing: 4
                    
                    Text {
                        text: displayName || "未命名"
                        font.pixelSize: baseConstants.fontSizeLarge
                        font.weight: Font.DemiBold
                        color: baseConstants.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    
                    // 状态徽章
                    Rectangle {
                        visible: status !== "ACTIVE" && status !== "RUNNING"
                        width: statusBadgeText.width + 8
                        height: 18
                        radius: 9
                        color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.2)
                        
                        Text {
                            id: statusBadgeText
                            anchors.centerIn: parent
                            text: statusText
                            font.pixelSize: 9
                            font.weight: Font.Medium
                            color: statusColor
                        }
                    }
                    
                    // 收藏按钮
                    Text {
                        text: isFavorite ? "⭐" : "☆"
                        font.pixelSize: 14
                        color: isFavorite ? baseConstants.warningAmber : "#FFFFFF"
                        
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
                
                // 类别和创建信息
                RowLayout {
                    spacing: 6
                    
                    Text {
                        text: category + (subCategory ? " · " + subCategory : "")
                        font.pixelSize: 12
                        color: categoryColor
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 创建信息
                    Text {
                        text: creator + (createDate ? " · " + createDate : "")
                        font.pixelSize: 11
                        color: "#FFFFFF"
                        visible: creator || createDate
                    }
                }
            }
        }
        
        // 描述区域
        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            text: description || "暂无描述"
            font.pixelSize: 13
            color: "#FFFFFF"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 性能指标区域
        ColumnLayout {
            spacing: spacingSmall
            
            // 主性能指标
            PerformanceMetricsGrid {
                Layout.fillWidth: true
                metrics: performanceMetrics
                columns: 2
            }
            
            // 附加指标（如果有）
            PerformanceMetricsGrid {
                Layout.fillWidth: true
                metrics: additionalMetrics
                columns: 2
                visible: additionalMetrics && additionalMetrics.length > 0
            }
        }
        
    // 迷你图表区域 - 减小高度避免超出窗口
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: showMiniChart && chartData && chartData.length > 0 ? 32 : 0  // 减小高度
        visible: showMiniChart && chartData && chartData.length > 0
        
        MiniChart {
            anchors.fill: parent
            chartData: root.chartData
            chartColor: categoryColor
            showGroupReturns: root.showGroupReturns
        }
    }
        
        // 标签和操作按钮区域
        ColumnLayout {
            spacing: spacingSmall
            
            // 标签区域
            Flow {
                Layout.fillWidth: true
                spacing: 4
                
                Repeater {
                    model: tags && tags.length > 0 ? tags : ["量化"]
                    
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
                    color: Qt.rgba(0.063, 0.725, 0.506, 0.2)  // #10B981 with alpha
                    
                    Row {
                        spacing: 4
                        anchors.centerIn: parent
                        
                        Text {
                            text: "🔥"
                            font.pixelSize: 10
                            color: baseConstants.profitGreen
                        }
                        
                        Text {
                            text: "推荐"
                            font.pixelSize: 10
                            font.weight: Font.Medium
                            color: baseConstants.profitGreen
                            rightPadding: 8
                        }
                    }
                }
            }
            
            // 操作按钮区域（由子类实现）
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: showActions ? 32 : 0
                visible: showActions
                
                // 子类应该重写这个区域，添加具体的操作按钮
                // 例如：预览、分析、启动、停止、优化等
            }
        }
    }
    
    // ============ 自定义组件 ============
    
    // 性能指标网格组件
    component PerformanceMetricsGrid: GridLayout {
        property var metrics: []
        property int columns: 2
        
        rows: Math.ceil(metrics.length / columns)
        // columns属性已有默认值，不需要再次设置
        columnSpacing: spacingMedium
        rowSpacing: spacingMedium
        
        Repeater {
            model: parent.metrics
            
            delegate: PerformanceMetric {
                Layout.fillWidth: true
                label: modelData.label || ""
                value: modelData.value !== undefined && modelData.value !== null ? modelData.value : 0
                format: modelData.format || "%.2f"
                unit: modelData.unit || ""
                metricColor: modelData.color || categoryColor
                showTrend: modelData.showTrend || false
                trendDirection: modelData.trendDirection || "neutral"
                tooltip: modelData.tooltip || ""
            }
        }
    }
    
    // 性能指标组件
    component PerformanceMetric: Rectangle {
        property string label: ""
        property var value: 0
        property string format: "%.2f"
        property string unit: ""
        property color metricColor: categoryColor
        property bool showTrend: false
        property string trendDirection: "neutral"  // "up", "down", "neutral"
        property string tooltip: ""
        
        implicitWidth: metricContent.width + 12
        implicitHeight: 28
        radius: 6
        color: Qt.rgba(metricColor.r, metricColor.g, metricColor.b, 0.1)
        
        Row {
            id: metricContent
            anchors.centerIn: parent
            spacing: 4
            
            Text {
                text: label + ":"
                font.pixelSize: baseConstants.fontSizeSmall
                color: "#FFFFFF"
            }
            
            Text {
                text: root.formatMetricValue(value, format, unit)
                font.pixelSize: baseConstants.fontSizeSmall + 1
                font.weight: Font.DemiBold
                color: metricColor
            }
            
            // 趋势指示器
            Text {
                visible: showTrend
                text: trendDirection === "up" ? "↑" : 
                      trendDirection === "down" ? "↓" : "→"
                  font.pixelSize: baseConstants.fontSizeSmall
                  color: trendDirection === "up" ? baseConstants.profitGreen : 
                      trendDirection === "down" ? baseConstants.lossRed : baseConstants.textTertiary
            }
        }
        
        // 工具提示
        MouseArea {
            anchors.fill: parent
            hoverEnabled: tooltip !== ""
            
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
            color: baseConstants.secondaryBg
            border.color: metricColor
            border.width: 1
            visible: false
            z: 1000
            
            x: parent.width / 2 - width / 2
            y: -height - 5
            
            Text {
                id: tooltipText
                anchors.centerIn: parent
                text: parent.parent.tooltip
                font.pixelSize: baseConstants.fontSizeSmall
                color: baseConstants.textPrimary
                wrapMode: Text.Wrap
                width: 150
            }
        }
    }
    
    // 迷你图表组件
    component MiniChart: Rectangle {
        property var chartData: []
        property color chartColor: baseConstants.accentBlue
        property bool showGroupReturns: false
        width: parent.width-60
        height: 32
        anchors.horizontalCenter: parent.horizontalCenter
        radius: 6
        color: baseConstants.primaryBg
        
        // 图表标题
        Text {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 6
            text: showGroupReturns ? "分组收益预览" : "走势预览"
            font.pixelSize: 10
            color: baseConstants.textTertiary
        }
        
        // 图表Canvas
        Canvas {
            id: chartCanvas
            anchors.fill: parent
            anchors.margins: 20
            
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                
                if (!chartData || chartData.length === 0) return
                
                var maxValue = Math.max.apply(null, chartData.map(Math.abs))
                var scale = height / 2 / (maxValue || 1)
                var barWidth = width / chartData.length
                
                // 绘制零线
                ctx.strokeStyle = baseConstants.borderLight
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(0, height / 2)
                ctx.lineTo(width, height / 2)
                ctx.stroke()
                
                // 绘制柱状图或折线图
                for (var i = 0; i < chartData.length; i++) {
                    var value = chartData[i]
                    var barHeight = Math.abs(value) * scale
                    var x = i * barWidth + barWidth * 0.1
                    var y = value > 0 ? height / 2 - barHeight : height / 2
                    var barColor = value > 0 ?
                                  Qt.rgba(0.96, 0.26, 0.21, 0.8) :
                                  Qt.rgba(0.063, 0.725, 0.506, 0.8)  // A股红涨绿跌
                    
                    ctx.fillStyle = barColor
                    ctx.fillRect(x, y, barWidth * 0.8, barHeight)
                }
            }
            
            Component.onCompleted: requestPaint()
        }
        
        // 悬停交互
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            
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
                var groupIndex = Math.floor(mouseX / (width / chartData.length))
                if (groupIndex >= 0 && groupIndex < chartData.length) {
                    tooltipText.text = (showGroupReturns ? "分组 " + (groupIndex + 1) : "点 " + (groupIndex + 1)) + ": " + 
                                       (chartData[groupIndex] * 100).toFixed(1) + "%"
                }
            }
        }
        
        // 图表工具提示
        Rectangle {
            id: chartTooltip
            width: tooltipText.width + 16
            height: tooltipText.height + 8
            radius: 4
            color: baseConstants.secondaryBg
            border.color: chartColor
            border.width: 1
            visible: false
            z: 1000
            
            Text {
                id: tooltipText
                anchors.centerIn: parent
                text: "悬停查看详情"
                font.pixelSize: 10
                color: baseConstants.textPrimary
            }
        }
    }
    
    // ============ 工具函数 ============
    
    // 根据状态获取颜色
    function getStatusColor(status) {
        var normalizedStatus = status ? status.toString().toUpperCase() : "UNKNOWN"
        switch (normalizedStatus) {
            case "ACTIVE":
            case "RUNNING": return baseConstants.profitGreen;
            case "WAIT_OPEN":
            case "PENDING": return baseConstants.accentBlue;
            case "STARTING": return baseConstants.accentBlue;
            case "PAUSED":
            case "STOPPING":
            case "EXPERIMENTAL":
            case "TESTING": return baseConstants.warningAmber;
            case "STOPPED":
            case "INACTIVE":
            case "ERROR":
            case "DEPRECATED": return baseConstants.lossRed;
            case "ARCHIVED": return baseConstants.textTertiary;
            default: return baseConstants.textTertiary;
        }
    }

    function resolveColor(candidate, fallback) {
        if (candidate === undefined || candidate === null) {
            return fallback
        }
        return candidate
    }

    function formatMetricValue(value, format, unit) {
        var formatString = format === undefined || format === null || format === ""
            ? "%s"
            : String(format)
        var suffix = unit ? " " + unit : ""

        if (formatString.indexOf("%s") >= 0) {
            var textValue = value === undefined || value === null ? "" : String(value)
            return formatString.replace("%s", textValue) + suffix
        }

        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            var fallbackText = value === undefined || value === null ? "" : String(value)
            return fallbackText + suffix
        }

        if (formatString.indexOf("%d") >= 0) {
            return formatString.replace("%d", String(Math.round(numericValue))) + suffix
        }

        var floatMatch = /%(?:\.([0-9]+))?f/.exec(formatString)
        if (floatMatch) {
            var precision = floatMatch[1] !== undefined ? Number(floatMatch[1]) : 6
            return formatString.replace(floatMatch[0], numericValue.toFixed(precision)) + suffix
        }

        return String(numericValue) + suffix
    }
    
    // 根据状态获取文本
    function getStatusText(status) {
        var normalizedStatus = status ? status.toString().toUpperCase() : "UNKNOWN"
        switch (normalizedStatus) {
            case "ACTIVE": return "活跃";
            case "RUNNING": return "运行中";
            case "STARTING": return "启动中";
            case "WAIT_OPEN": return "待开盘";
            case "STOPPED": return "已停止";
            case "STOPPING": return "停止中";
            case "INACTIVE": return "已停用";
            case "ERROR": return "异常";
            case "DEPRECATED": return "已废弃";
            case "PAUSED": return "已暂停";
            case "EXPERIMENTAL": return "实验";
            case "PENDING": return "待处理";
            case "TESTING": return "测试中";
            case "ARCHIVED": return "已归档";
            default: return "未知";
        }
    }
    
    // 根据类别获取图标
    function getCategoryIcon(category) {
        switch (category) {
            case "动量类":
            case "动量因子":
            case "趋势策略": return "📊";
            case "价值类":
            case "价值因子":
            case "价值策略": return "💰";
            case "质量类":
            case "质量因子":
            case "质量策略": return "📈";
            case "成长类":
            case "成长因子":
            case "成长策略": return "🚀";
            case "情绪类":
            case "情绪因子":
            case "情绪策略": return "🧠";
            case "波动类":
            case "低波因子":
            case "低波动因子":
            case "波动策略": return "📉";
            case "流动性类":
            case "流动性因子": return "💧";
            case "预期类":
            case "宏观": return "🌦️";
            case "行业": return "🏭";
            case "规模因子": return "📐";
            case "红利因子": return "🎁";
            case "技术因子": return "🧮";
            case "自定义因子":
            case "自定义": return "🧩";
            case "恐慌类": return "🛡️";
            default: return "📊";
        }
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
    
    // ============ 鼠标交互 ============
    
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: enableCardClick ? Qt.PointingHandCursor : Qt.ArrowCursor
        enabled: enableCardClick
        
        onEntered: {
            hovered = true
            if (enableRealTimeFeedback) {
                hoverAnimation.start()
            }
        }
        
        onExited: {
            hovered = false
            if (enableRealTimeFeedback) {
                hoverAnimation.start()
            }
        }
        
        onClicked: {
            if (enableCardClick) {
                if (enableRealTimeFeedback) {
                    clickAnimation.start()
                }
                root.clicked()
                root.entitySelected(entityId)
            }
        }
        
        onDoubleClicked: {
            if (enableCardClick) {
                root.doubleClicked()
            }
        }
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化完成逻辑
    }
}
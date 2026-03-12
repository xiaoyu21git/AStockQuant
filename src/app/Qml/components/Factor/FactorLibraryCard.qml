// FactorLibraryCard.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 6.0
import "../.." as ConsoleUi

/**
 * 因子库卡片组件
 * 显示单个因子的信息，用于因子库浏览页面
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    // 因子数据
    property string factorName: ""
    property string displayName: ""
    property string majorCategory: "动量类"
    property string subCategory: "趋势动量"
    property string description: ""
    
    // 性能指标
    property real icValue: 0.0
    property real irValue: 0.0
    property int validityDays: 20
    property real turnoverRate: 800  // %/年
    property bool isRecommended: false
    property bool isFavorite: false
    property string status: "ACTIVE"
    
    // 标签
    property var tags: []
    
    // 交互状态
    property bool hovered: false
    property bool selected: false
    
    // 信号
    signal clicked()
    signal favoriteToggled(bool favorite)
    signal detailsRequested()
    signal useRequested()
    
    // ============ 私有属性 ============
    
    readonly property color categoryColor: getFactorColor(majorCategory)
    readonly property color statusColor: getStatusColor(status)
    readonly property string categoryIcon: getFactorIcon(majorCategory)
    readonly property string statusIcon: getStatusIcon(status)
    
    // ============ 视觉属性 ============
    
    implicitWidth: 320
    implicitHeight: 180
    radius: 16  // borderRadiusXl
    color: {
        if (selected) return Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.1)
        if (hovered) return "#334155"  // bgTertiary
        return "#1E293B"  // bgSecondary
    }
    border.color: selected ? categoryColor : "#475569"  // borderDefault
    border.width: selected ? 2 : 1
    
    layer.enabled: true
    layer.effect: DropShadow {
        horizontalOffset: 0
        verticalOffset: 1
        radius: 3
        color: Qt.rgba(0, 0, 0, 0.1)
        spread: 0.2
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16  // spacing4
        spacing: 12  // spacing3
        
        // 标题行
        RowLayout {
            spacing: 12  // spacing3
            
            // 类别图标
            Rectangle {
                width: 32
                height: 32
                radius: 8  // borderRadiusMd
                color: Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: categoryIcon
                    font.pixelSize: 16  // fontSizeLg
                }
            }
            
            // 标题和子标题
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4  // spacing1
                
                Text {
                    text: displayName || factorName
                    font.pixelSize: 16  // fontSizeLg
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"  // textPrimary
                    elide: Text.ElideRight
                }
                
                Text {
                    text: majorCategory + " | " + subCategory
                    font.pixelSize: 12  // fontSizeSm
                    color: categoryColor
                }
            }
            
            // 状态指示器
            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: statusColor
            }
        }
        
        // 描述
        Text {
            Layout.fillWidth: true
            text: description
            font.pixelSize: 14  // fontSizeMd
            color: "#94A3B8"  // textSecondary
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 性能指标
        RowLayout {
            spacing: 16  // spacing4
            
            // IC值
            MetricBadge {
                label: "IC"
                value: formatIC(icValue)
                badgeColor: categoryColor
                tooltip: "信息系数"
            }
            
            // IR值
            MetricBadge {
                label: "IR"
                value: irValue.toFixed(2)
                badgeColor: categoryColor
                tooltip: "信息比率"
            }
            
            // 有效期
            MetricBadge {
                label: "有效期"
                value: validityDays + "天"
                badgeColor: categoryColor
                tooltip: "因子有效期"
            }
            
            // 换手率
            MetricBadge {
                label: "换手率"
                value: turnoverRate + "%/年"
                badgeColor: categoryColor
                tooltip: "年化换手率"
            }
        }
        
        // 标签和操作按钮
        RowLayout {
            spacing: 12  // spacing3
            
            // 标签区域
            Flow {
                Layout.fillWidth: true
                spacing: 4  // spacing1
                
                Repeater {
                    model: ["行业", "大盘", "高频", "低频"]  // 示例标签数据
                    
                    delegate: Rectangle {
                        height: 20
                        radius: 9999  // borderRadiusFull
                        color: Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.2)
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 10  // fontSizeXs
                            font.weight: Font.Medium
                            color: categoryColor
                            leftPadding: 8  // spacing2
                            rightPadding: 8  // spacing2
                        }
                    }
                }
                
                // 推荐标签
                Rectangle {
                    visible: isRecommended
                    height: 20
                    radius: 9999  // borderRadiusFull
                    color: Qt.rgba(0.298, 0.686, 0.314, 0.2)  // statusSuccess with alpha
                    
                    Row {
                        spacing: 4  // spacing1
                        anchors.centerIn: parent
                        
                        Text {
                            text: "👍"
                            font.pixelSize: 10  // fontSizeXs
                            color: "#4caf50"  // statusSuccess
                        }
                        
                        Text {
                            text: "推荐"
                            font.pixelSize: 10  // fontSizeXs
                            font.weight: Font.Medium
                            color: "#4caf50"  // statusSuccess
                            rightPadding: 8  // spacing2
                        }
                    }
                }
            }
            
            // 操作按钮
            Row {
                spacing: 8  // spacing2
                
                // 收藏按钮
                Rectangle {
                    width: 32
                    height: 32
                    radius: 8  // borderRadiusMd
                    color: isFavorite ? Qt.rgba(1.0, 0.6, 0.0, 0.2)  // statusWarning with alpha
                                     : "transparent"
                    border.color: isFavorite ? "#ff9800"  // statusWarning
                                            : "#64748B"  // borderLight
                    border.width: 1
                    
                    Text {
                        anchors.centerIn: parent
                        text: isFavorite ? "⭐" : "☆"
                        font.pixelSize: 14  // fontSizeMd
                        color: isFavorite ? "#ff9800"  // statusWarning
                                         : "#94A3B8"  // textSecondary
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: favoriteToggled(!isFavorite)
                    }
                }
                
                // 详情按钮
                Rectangle {
                    width: 32
                    height: 32
                    radius: 8  // borderRadiusMd
                    color: "transparent"
                    border.color: "#64748B"  // borderLight
                    border.width: 1
                    
                    Text {
                        anchors.centerIn: parent
                        text: "ℹ️"
                        font.pixelSize: 14  // fontSizeMd
                        color: "#94A3B8"  // textSecondary
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: detailsRequested()
                    }
                }
                
                // 使用按钮
                Rectangle {
                    width: 32
                    height: 32
                    radius: 8  // borderRadiusMd
                    color: "#3B82F6"  // 替代 gradientBlue
                    
                    Text {
                        anchors.centerIn: parent
                        text: "▶"
                        font.pixelSize: 14  // fontSizeMd
                        color: "white"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: useRequested()
                    }
                }
            }
        }
    }
    
    // ============ 工具函数 ============
    
    // 根据因子大类获取颜色
    function getFactorColor(majorCategory) {
        return FactorDesignSystem.getFactorColor(majorCategory);
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
            case "ACTIVE": return "#4caf50";  // successColor
            case "EXPERIMENTAL": return "#ff9800";  // warningColor
            case "DEPRECATED": return "#f44336";  // dangerColor
            case "PENDING": return "#2196f3";  // infoColor
            default: return "#64748B";  // textTertiary
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
    
    // 格式化IC值
    function formatIC(value) {
        if (value === undefined || value === null) return "N/A";
        return value.toFixed(3);
    }
    
    // ============ 鼠标交互 ============
    
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        
        onEntered: hovered = true
        onExited: hovered = false
        onClicked: root.clicked()
    }
    
    // ============ 状态动画 ============
    
    states: [
        State {
            name: "hovered"
            when: hovered
            PropertyChanges { target: root; scale: 1.02 }
        },
        State {
            name: "selected"
            when: selected
            PropertyChanges { target: root; border.width: 2 }
        }
    ]
    
    transitions: Transition {
        NumberAnimation {
            properties: "scale, border.width"
            duration: 200  // durationFast
            easing.type: Easing.InOutQuad  // easingStandard
        }
    }
    
    // ============ 子组件 ============
    
    /**
     * 性能指标徽章组件
     */
    component MetricBadge: Rectangle {
        property string label: ""
        property string value: ""
        property color badgeColor: "#94A3B8"  // textSecondary
        property string tooltip: ""
        
        implicitWidth: metricRow.width + 12  // spacing3
        implicitHeight: 24
        radius: 8  // borderRadiusMd
        color: Qt.rgba(badgeColor.r, badgeColor.g, badgeColor.b, 0.1)
        
        Row {
            id: metricRow
            anchors.centerIn: parent
            spacing: 4  // spacing1
            
            Text {
                text: label + ":"
                font.pixelSize: 10  // fontSizeXs
                color: "#94A3B8"  // textSecondary
            }
            
            Text {
                text: value
                font.pixelSize: 10  // fontSizeXs
                font.weight: Font.Medium
                color: badgeColor
            }
        }
        
        // 工具提示
        MouseArea {
            anchors.fill: parent
            hoverEnabled: tooltip !== ""
            
            onEntered: {
                if (tooltip) {
                    tooltipTimer.start()
                }
            }
            
            onExited: {
                tooltipTimer.stop()
                tooltipPopup.close()
            }
        }
        
        Timer {
            id: tooltipTimer
            interval: 500
            onTriggered: showTooltip()
        }
        
        Popup {
            id: tooltipPopup
            x: parent.width / 2
            y: -height - 5
            width: 200
            height: tooltipText.contentHeight + 12  // spacing3
            padding: 8  // spacing2
            
            background: Rectangle {
                color: "#1E293B"  // bgSecondary
                border.color: "#475569"  // borderDefault
                border.width: 1
                radius: 8  // borderRadiusMd
                
                layer.enabled: true
                layer.effect: DropShadow {
                    horizontalOffset: 0
                    verticalOffset: 2
                    radius: 8
                    color: Qt.rgba(0, 0, 0, 0.2)
                    spread: 0.2
                }
            }
            
            Text {
                id: tooltipText
                width: parent.width - 16  // 2 * spacing2
                text: tooltip
                font.pixelSize: 12  // fontSizeSm
                color: "#F1F5F9"  // textPrimary
                wrapMode: Text.WordWrap
            }
        }
        
        function showTooltip() {
            if (tooltip) {
                tooltipPopup.open()
            }
        }
    }
}
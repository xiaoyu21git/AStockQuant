// FactorCard.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects 6.0

/**
 * 基础因子卡片组件
 * 简洁版本，用于因子选择和预览
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    property string factorId: ""
    property string factorName: ""
    property string displayName: ""
    property string majorCategory: "动量类"
    property string subCategory: "趋势动量"
    property string description: ""
    property string icon: getFactorIcon(majorCategory)
    property color typeColor: getFactorColor(majorCategory)
    property real icValue: 0.0
    property real irValue: 0.0
    property int validityDays: 20
    property bool isFavorite: false
    property bool isSelected: false
    property bool isRecommended: false
    
    // 信号
    signal clicked()
    signal favoriteToggled(bool isFavorite)
    signal detailRequested()
    
    // ============ 视觉属性 ============
    
    implicitWidth: 240
    implicitHeight: 120
    radius: 16  // borderRadiusXl
    color: {
        if (isSelected) return Qt.rgba(typeColor.r, typeColor.g, typeColor.b, 0.1)
        return "#1E293B"  // bgSecondary
    }
    border.color: isSelected ? typeColor : "#475569"  // borderDefault
    border.width: isSelected ? 2 : 1
    
    layer.enabled: true
    layer.effect: DropShadow {
        horizontalOffset: 0
        verticalOffset: 1
        radius: 3
        color: Qt.rgba(0, 0, 0, 0.1)
        samples: radius * 2
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16  // spacing4
        spacing: 8  // spacing2
        
        // 标题行
        RowLayout {
            spacing: 8  // spacing2
            
            // 类别图标
            Rectangle {
                width: 24
                height: 24
                radius: 4  // borderRadiusSm
                color: Qt.rgba(typeColor.r, typeColor.g, typeColor.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: icon
                    font.pixelSize: 14  // fontSizeMd
                }
            }
            
            // 标题
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                
                Text {
                    text: displayName || factorName
                    font.pixelSize: 14  // fontSizeMd
                    font.weight: Font.Medium
                    color: "#F1F5F9"  // textPrimary
                    elide: Text.ElideRight
                }
                
                Text {
                    text: majorCategory
                    font.pixelSize: 10  // fontSizeXs
                    color: typeColor
                    elide: Text.ElideRight
                }
            }
            
            // 收藏图标
            Text {
                text: isFavorite ? "⭐" : "☆"
                font.pixelSize: 12  // fontSizeSm
                color: isFavorite ? "#ff9800" : "#64748B"  // statusWarning : textTertiary
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: favoriteToggled(!isFavorite)
                }
            }
        }
        
        // 描述
        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: description
            font.pixelSize: 12  // fontSizeSm
            color: "#94A3B8"  // textSecondary
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 性能指标
        RowLayout {
            spacing: 12  // spacing3
            
            // IC值
            MetricBadge {
                label: "IC"
                value: formatIC(icValue)
                badgeColor: typeColor
            }
            
            // 有效期
            MetricBadge {
                label: "有效期"
                value: validityDays + "天"
                badgeColor: typeColor
            }
            
            // 推荐标签
            Rectangle {
                visible: isRecommended
                width: 40
                height: 16
                radius: 4  // borderRadiusSm
                color: Qt.rgba(0.298, 0.686, 0.314, 0.2)  // statusSuccess with alpha
                
                Text {
                    anchors.centerIn: parent
                    text: "推荐"
                    font.pixelSize: 10  // fontSizeXs
                    font.weight: Font.Medium
                    color: "#4caf50"  // statusSuccess
                }
            }
        }
    }
    
    // ============ 鼠标交互 ============
    
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
        onDoubleClicked: detailRequested()
    }
    
    // ============ 工具函数 ============
    
    // 根据因子大类获取颜色
    function getFactorColor(majorCategory) {
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
            default: return "#94A3B8";  // textSecondary
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
    
    // 格式化IC值
    function formatIC(value) {
        if (value === undefined || value === null) return "N/A";
        return value.toFixed(3);
    }
    
    // ============ 子组件 ============
    
    /**
     * 性能指标徽章组件
     */
    component MetricBadge: Rectangle {
        property string label: ""
        property string value: ""
        property color badgeColor: "#94A3B8"  // textSecondary
        
        implicitWidth: metricText.contentWidth + 8  // spacing2
        implicitHeight: 16
        radius: 4  // borderRadiusSm
        color: Qt.rgba(badgeColor.r, badgeColor.g, badgeColor.b, 0.1)
        
        Text {
            id: metricText
            anchors.centerIn: parent
            text: label + ": " + value
            font.pixelSize: 10  // fontSizeXs
            font.weight: Font.Medium
            color: badgeColor
        }
    }
}
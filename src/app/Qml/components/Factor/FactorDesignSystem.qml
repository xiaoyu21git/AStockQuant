// FactorDesignSystem.qml
pragma Singleton
import QtQuick 2.15

/**
 * 统一设计系统
 * 提供统一的颜色、字体、间距等设计常量
 * 合并了原来的 FactorDesignSystem 和 DataAnalysis/theme.qml
 */
QtObject {
    id: root
    
    // ============ 颜色系统 ============
    
    // 主要颜色 (来自 theme.qml)
    readonly property color primaryColor: "#3949ab"
    readonly property color secondaryColor: "#1a237e"
    readonly property color accentColor: "#00bcd4"
    readonly property color blackColor: "#000000"
    readonly property color whiteColor: "#ffffff"
    
    // 深色主题颜色 (来自 theme.qml)
    readonly property color darkBg: "#0d1533"
    readonly property color darkCard: "#121c44" 
    readonly property color darkText: "#e0e0e0"
    readonly property color darkBorder: "#2a3560"
    readonly property color darkTextSecondary: "#a0a0a0"  // 添加暗色主题次要文本颜色
    
    // 状态颜色 (来自 theme.qml)
    readonly property color successColor: "#4caf50"
    readonly property color warningColor: "#ff9800"
    readonly property color dangerColor: "#f44336"
    readonly property color infoColor: "#2196f3"
    
    // 文本颜色 (来自原 FactorDesignSystem)
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color textInverse: "#0F172A"
    
    // 背景颜色 (来自原 FactorDesignSystem)
    readonly property color bgPrimary: "#0F172A"
    readonly property color bgSecondary: "#1E293B"
    readonly property color bgTertiary: "#334155"
    readonly property color bgQuaternary: "#475569"
    
    // 因子类型颜色 (来自原 FactorDesignSystem)
    readonly property color factorMomentum: "#3B82F6"    // 动量类 - 蓝色
    readonly property color factorValue: "#F59E0B"       // 价值类 - 橙色
    readonly property color factorQuality: "#10B981"     // 质量类 - 绿色
    readonly property color factorGrowth: "#8B5CF6"      // 成长类 - 紫色
    readonly property color factorSentiment: "#EC4899"   // 情绪类 - 粉色
    readonly property color factorVolatility: "#EF4444"  // 波动类 - 红色
    readonly property color factorLiquidity: "#06B6D4"   // 流动性类 - 青色
    readonly property color factorExpectation: "#F97316" // 预期类 - 橙红色
    readonly property color factorPanic: "#8B4513"       // 恐慌类 - 棕色
    
    // 状态颜色 (来自原 FactorDesignSystem，兼容 theme.qml 命名)
    readonly property color statusSuccess: successColor
    readonly property color statusWarning: warningColor
    readonly property color statusError: dangerColor
    readonly property color statusInfo: infoColor
    readonly property color statusNeutral: "#64748B"
    
    // 边框颜色
    readonly property color borderDefault: "#475569"
    readonly property color borderLight: "#64748B"
    readonly property color borderFocus: "#3B82F6"
    
    // 渐变 (来自原 FactorDesignSystem)
    readonly property var gradientBlue: Gradient {
        GradientStop { position: 0.0; color: "#3B82F6" }
        GradientStop { position: 1.0; color: "#1D4ED8" }
    }
    
    readonly property var gradientGreen: Gradient {
        GradientStop { position: 0.0; color: "#10B981" }
        GradientStop { position: 1.0; color: "#047857" }
    }
    
    readonly property var gradientRed: Gradient {
        GradientStop { position: 0.0; color: "#EF4444" }
        GradientStop { position: 1.0; color: "#B91C1C" }
    }
    
    readonly property var gradientPurple: Gradient {
        GradientStop { position: 0.0; color: "#8B5CF6" }
        GradientStop { position: 1.0; color: "#7C3AED" }
    }
    
    // ============ 字体系统 ============
    
    // 字体大小
    readonly property int fontSizeXs: 10
    readonly property int fontSizeSm: 12
    readonly property int fontSizeMd: 14
    readonly property int fontSizeLg: 16
    readonly property int fontSizeXl: 18
    readonly property int fontSize2xl: 20
    readonly property int fontSize3xl: 24
    readonly property int fontSize4xl: 30
    
    // 字重
    readonly property int fontWeightNormal: Font.Normal
    readonly property int fontWeightMedium: Font.Medium
    readonly property int fontWeightDemiBold: Font.DemiBold
    readonly property int fontWeightBold: Font.Bold
    
    // 字体族 (Qt 6.2+ 不再支持逗号分隔的字体列表，改为单一字体)
    readonly property string fontFamily: "Microsoft YaHei"
    readonly property string fontFamilyMono: "Consolas"
    
    // 兼容 theme.qml 的字体族
    readonly property string fontFamilyList: "'Segoe UI', Tahoma, Geneva, Verdana, sans-serif"
    
    // ============ 间距系统 ============
    
    // 基本间距（基于8px网格）
    readonly property real spacing0: 0
    readonly property real spacing1: 4    // 0.25rem
    readonly property real spacing2: 8    // 0.5rem
    readonly property real spacing3: 12   // 0.75rem
    readonly property real spacing4: 16   // 1rem
    readonly property real spacing5: 20   // 1.25rem
    readonly property real spacing6: 24   // 1.5rem
    readonly property real spacing8: 32   // 2rem
    readonly property real spacing10: 40  // 2.5rem
    readonly property real spacing12: 48  // 3rem
    
    // ============ 圆角系统 ============
    
    readonly property real borderRadiusNone: 0
    readonly property real borderRadiusSm: 4    // 0.25rem
    readonly property real borderRadiusMd: 8    // 0.5rem
    readonly property real borderRadiusLg: 12   // 0.75rem
    readonly property real borderRadiusXl: 16   // 1rem
    readonly property real borderRadius2xl: 20  // 1.25rem
    readonly property real borderRadius3xl: 24  // 1.5rem
    readonly property real borderRadiusFull: 9999
    
    // 兼容 theme.qml 的圆角属性
    readonly property int borderRadiusSmall: borderRadiusSm
    readonly property int borderRadiusMedium: borderRadiusMd
    readonly property int borderRadiusLarge: borderRadiusLg
    readonly property int borderRadiusCircle: borderRadiusFull
    
    // ============ 阴影系统 ============
    
    readonly property var shadowSm: [
        { offsetX: 0, offsetY: 1, blurRadius: 2, color: Qt.rgba(0, 0, 0, 0.05) },
        { offsetX: 0, offsetY: 1, blurRadius: 3, color: Qt.rgba(0, 0, 0, 0.1) }
    ]
    
    readonly property var shadowMd: [
        { offsetX: 0, offsetY: 4, blurRadius: 6, color: Qt.rgba(0, 0, 0, 0.1) },
        { offsetX: 0, offsetY: 10, blurRadius: 15, color: Qt.rgba(0, 0, 0, 0.1) }
    ]
    
    readonly property var shadowLg: [
        { offsetX: 0, offsetY: 10, blurRadius: 15, color: Qt.rgba(0, 0, 0, 0.1) },
        { offsetX: 0, offsetY: 20, blurRadius: 30, color: Qt.rgba(0, 0, 0, 0.15) }
    ]
    
    // 字符串阴影 (来自 theme.qml)
    readonly property string cardShadow: "0 4px 12px  rgba(0, 0, 0, 0.2)"
    readonly property string hoverShadow: "0 8px 20px rgba(0, 0, 0, 0.3)"
    
    // ============ 动画系统 ============
    
    readonly property int durationFast: 150
    readonly property int durationNormal: 300
    readonly property int durationSlow: 500
    readonly property var easingStandard: Easing.InOutQuad
    readonly property var easingDecelerate: Easing.OutCubic
    readonly property var easingAccelerate: Easing.InCubic
    
    // 兼容 theme.qml 的 transitionDuration
    readonly property int transitionDuration: 300
    
    // ============ 工具函数 ============
    
    // 根据因子大类获取颜色
    function getFactorColor(majorCategory) {
        switch (majorCategory) {
            case "动量类": return factorMomentum;
            case "价值类": return factorValue;
            case "质量类": return factorQuality;
            case "成长类": return factorGrowth;
            case "情绪类": return factorSentiment;
            case "波动类": return factorVolatility;
            case "流动性类": return factorLiquidity;
            case "预期类": return factorExpectation;
            case "恐慌类": return factorPanic;
            default: return textSecondary;
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
        switch (status) {
            case "ACTIVE": return statusSuccess;
            case "DEPRECATED": return statusError;
            case "EXPERIMENTAL": return statusWarning;
            case "INACTIVE": return statusNeutral;
            case "NEEDS_REVIEW": return statusWarning;
            default: return statusNeutral;
        }
    }
    
    // 根据状态获取图标
    function getStatusIcon(status) {
        switch (status) {
            case "ACTIVE": return "✅";
            case "DEPRECATED": return "❌";
            case "EXPERIMENTAL": return "🧪";
            case "INACTIVE": return "⏸️";
            case "NEEDS_REVIEW": return "⚠️";
            default: return "❓";
        }
    }
    
    // 格式化IC值
    function formatIC(value) {
        if (value === undefined || value === null) return "N/A";
        return value.toFixed(3);
    }
    
    // 格式化百分比
    function formatPercent(value, decimals) {
        if (value === undefined || value === null) return "N/A";
        var dec = decimals || 1;
        return (value * 100).toFixed(dec) + "%";
    }
    
    // 格式化数字（添加千位分隔符）
    function formatNumber(value, decimals) {
        if (value === undefined || value === null) return "N/A";
        var dec = decimals || 0;
        var parts = value.toFixed(dec).split(".");
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ",");
        return parts.join(".");
    }
    
    // 获取性能评级颜色
    function getPerformanceColor(icValue) {
        if (icValue >= 0.05) return statusSuccess;
        if (icValue >= 0.03) return "#10B981"; // 浅绿
        if (icValue >= 0.02) return statusWarning;
        if (icValue >= 0.01) return "#F59E0B"; // 浅橙
        return statusError;
    }
    
    // 获取拥挤度颜色
    function getCrowdingColor(crowdingDegree) {
        if (crowdingDegree < 30) return statusSuccess;
        if (crowdingDegree < 60) return statusWarning;
        return statusError;
    }
    
    // 获取阴影 (来自 theme.qml)
    function getShadow(level) {
        switch(level) {
            case 0: return ""
            case 1: return cardShadow
            case 2: return hoverShadow
            default: return cardShadow
        }
    }
    
    // 获取圆角 (来自 theme.qml)
    function getRadius(size) {
        switch(size) {
            case "small": return borderRadiusSmall
            case "medium": return borderRadiusMedium
            case "large": return borderRadiusLarge
            case "circle": return borderRadiusCircle
            default: return borderRadiusMedium
        }
    }
    
    // // 应用阴影
    function applyShadow(element, shadowType) {
        var shadows = shadowType || shadowSm;
        for (var i = 0; i < shadows.length; i++) {
            var shadow = shadows[i];
            // 简化实现，避免复杂的对象创建
            if (element.layer && element.layer.effect) {
                element.layer.effect.horizontalOffset = shadow.offsetX;
                element.layer.effect.verticalOffset = shadow.offsetY;
                element.layer.effect.radius = shadow.blurRadius;
                element.layer.effect.color = shadow.color;
                element.layer.effect.spread = shadow.blurRadius * 0.2;
            }
        }
    }
    
    // ============ 组件样式 ============
    
    // 按钮样式
    property Component buttonPrimary: Rectangle {
        radius: borderRadiusMd
        gradient: Gradient {
            GradientStop { position: 0.0; color: factorMomentum }
            GradientStop { position: 1.0; color: "#1D4ED8" }
        }
        
        property alias text: buttonText.text
        property alias icon: buttonIcon.text
        
        implicitWidth: 100
        implicitHeight: 40
        
        Row {
            anchors.centerIn: parent
            spacing: spacing2
            
            Text {
                id: buttonIcon
                font.pixelSize: fontSizeMd
                color: "white"
                visible: text !== ""
            }
            
            Text {
                id: buttonText
                font.pixelSize: fontSizeMd
                font.weight: Font.Medium
                color: "white"
            }
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            
            onEntered: parent.opacity = 0.9
            onExited: parent.opacity = 1.0
            onPressed: parent.opacity = 0.8
            onReleased: parent.opacity = 0.9
        }
    }
    
    // 标签样式
    property Component tag: Rectangle {
        radius: borderRadiusFull
        color: factorMomentum
        implicitWidth: tagText.contentWidth + spacing4
        implicitHeight: 24
        
        Text {
            id: tagText
            anchors.centerIn: parent
            text: modelData
            font.pixelSize: fontSizeXs
            font.weight: Font.Medium
            color: "white"
        }
    }
    
    // 卡片样式
    property Component card: Rectangle {
        radius: borderRadiusXl
        color: bgSecondary
        border.color: borderDefault
        border.width: 1
        
        layer.enabled: true
        layer.effect: DropShadow {
            horizontalOffset: 0
            verticalOffset: 1
            radius: 3
            color: Qt.rgba(0, 0, 0, 0.1)
            spread: radius * 0.2
        }
    }
    
    // 输入框样式
    property Component inputField: Rectangle {
        radius: borderRadiusMd
        color: bgTertiary
        border.color: borderDefault
        border.width: 1
        implicitHeight: 40
        
        property alias text: input.text
        property alias placeholderText: placeholder.text
        
        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: spacing3
            anchors.rightMargin: spacing3
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: fontSizeMd
            color: textPrimary
            selectionColor: factorMomentum
            selectedTextColor: "white"
            
            Text {
                id: placeholder
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                font: input.font
                color: textTertiary
                visible: !input.text && !input.activeFocus
            }
        }
        
        // 焦点状态
        states: [
            State {
                name: "focused"
                when: input.activeFocus
                PropertyChanges { target: parent; border.color: borderFocus }
            }
        ]
        
        transitions: Transition {
            PropertyAnimation { properties: "border.color"; duration: durationFast }
        }
    }
    
    // ============ 图标库 ============
    
    readonly property var icons: ({
        momentum: "📊",
        value: "💰",
        quality: "📈",
        growth: "🚀",
        sentiment: "🧠",
        volatility: "📉",
        liquidity: "💧",
        expectation: "🔮",
        panic: "🛡️",
        
        favorite: "⭐",
        star: "★",
        heart: "❤️",
        fire: "🔥",
        trend: "📈",
        chart: "📊",
        bell: "🔔",
        gear: "⚙️",
        search: "🔍",
        filter: "🎛️",
        sort: "↕️",
        add: "➕",
        edit: "✏️",
        delete: "🗑️",
        copy: "📋",
        download: "📥",
        upload: "📤",
        refresh: "🔄",
        play: "▶️",
        pause: "⏸️",
        stop: "⏹️",
        check: "✅",
        warning: "⚠️",
        error: "❌",
        info: "ℹ️",
        question: "❓",
        
        // 方向
        up: "↑",
        down: "↓",
        left: "←",
        right: "→",
        
        // 时间
        clock: "⏰",
        calendar: "📅",
        timer: "⏱️"
    })
}
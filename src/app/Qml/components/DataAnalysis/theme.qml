// Theme.qml - QML 主题单例
// 直接使用颜色代码，不依赖其他模块
pragma Singleton
import QtQuick 2.15

QtObject {
    id: theme
    
    // ============ 颜色系统 ============
    
    // 主要颜色 (来自原 theme.qml)
    readonly property color primaryColor: "#3949ab"
    readonly property color secondaryColor: "#1a237e"
    readonly property color accentColor: "#00bcd4"
    readonly property color blackColor: "#000000"
    readonly property color whiteColor: "#ffffff"
    
    // 深色主题颜色 (来自原 theme.qml)
    readonly property color darkBg: "#0d1533"
    readonly property color darkCard: "#121c44" 
    readonly property color darkText: "#e0e0e0"
    readonly property color darkBorder: "#2a3560"
    readonly property color darkTextSecondary: "#a0a0a0"  // 添加暗色主题次要文本颜色
    
    // 状态颜色 (来自原 theme.qml)
    readonly property color successColor: "#4caf50"
    readonly property color warningColor: "#ff9800"
    readonly property color dangerColor: "#f44336"
    readonly property color infoColor: "#2196f3"
    
    // ============ 文本颜色 ============
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    
    // ============ 背景颜色 ============
    readonly property color bgPrimary: "#0F172A"
    readonly property color bgSecondary: "#1E293B"
    readonly property color bgTertiary: "#334155"
    
    // ============ 阴影系统 ============
    readonly property string cardShadow: "0 4px 12px rgba(0, 0, 0, 0.2)"
    readonly property string hoverShadow: "0 8px 20px rgba(0, 0, 0, 0.3)"
    
    // ============ 字体系统 ============
    readonly property string fontFamily: "'Segoe UI', Tahoma, Geneva, Verdana, sans-serif"
    
    // ============ 圆角系统 ============
    readonly property int borderRadiusSmall: 4
    readonly property int borderRadiusMedium: 6
    readonly property int borderRadiusLarge: 10
    readonly property int borderRadiusCircle: 50
    
    // ============ 动画系统 ============
    readonly property int transitionDuration: 300
    
    // ============ 方法 ============
    
    // 方法：获取阴影
    function getShadow(level) {
        switch(level) {
            case 0: return ""
            case 1: return cardShadow
            case 2: return hoverShadow
            default: return cardShadow
        }
    }
    
    // 方法：获取圆角
    function getRadius(size) {
        switch(size) {
            case "small": return borderRadiusSmall
            case "medium": return borderRadiusMedium
            case "large": return borderRadiusLarge
            case "circle": return borderRadiusCircle
            default: return borderRadiusMedium
        }
    }
    
    // ============ 工具函数 ============
    
    // 获取因子颜色
    function getFactorColor(majorCategory) {
        // 简单的颜色映射
        var colors = {
            "动量类": "#3B82F6",
            "价值类": "#F59E0B",
            "质量类": "#10B981",
            "成长类": "#8B5CF6",
            "情绪类": "#EC4899",
            "波动类": "#EF4444",
            "流动性类": "#06B6D4",
            "预期类": "#F97316",
            "恐慌类": "#8B4513"
        };
        return colors[majorCategory] || textSecondary;
    }
    
    // 获取因子图标
    function getFactorIcon(majorCategory) {
        var icons = {
            "动量类": "📊",
            "价值类": "💰",
            "质量类": "📈",
            "成长类": "🚀",
            "情绪类": "🧠",
            "波动类": "📉",
            "流动性类": "💧",
            "预期类": "🔮",
            "恐慌类": "🛡️"
        };
        return icons[majorCategory] || "📊";
    }
    
    // 格式化百分比
    function formatPercent(value, decimals) {
        if (value === undefined || value === null) return "N/A"
        var dec = decimals || 1
        return (value * 100).toFixed(dec) + "%"
    }
    
    // 格式化数字
    function formatNumber(value, decimals) {
        if (value === undefined || value === null) return "N/A"
        var dec = decimals || 0
        var parts = value.toFixed(dec).split(".")
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ",")
        return parts.join(".")
    }
}
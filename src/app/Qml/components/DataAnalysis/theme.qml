// Theme.qml - QML 主题单例
pragma Singleton
import QtQuick 2.15

QtObject {
    id: theme
    
    // 主要颜色
    readonly property color primaryColor: "#3949ab"
    readonly property color secondaryColor: "#1a237e"
    readonly property color accentColor: "#00bcd4"
    readonly property color blackColor: "#000000"
    readonly property color whiteColor: "#ffffff"
    // 深色主题
    readonly property color darkBg: "#0d1533"
    readonly property color darkCard: "#121c44"
    readonly property color darkText: "#e0e0e0"
    readonly property color darkBorder: "#2a3560"
    
    // 状态颜色
    readonly property color successColor: "#4caf50"
    readonly property color warningColor: "#ff9800"
    readonly property color dangerColor: "#f44336"
    readonly property color infoColor: "#2196f3"
    
    // 阴影
    readonly property string cardShadow: "0 4px 12px rgba(0, 0, 0, 0.2)"
    readonly property string hoverShadow: "0 8px 20px rgba(0, 0, 0, 0.3)"
    
    // 字体
    readonly property string fontFamily: "'Segoe UI', Tahoma, Geneva, Verdana, sans-serif"
    
    // 圆角
    readonly property int borderRadiusSmall: 4
    readonly property int borderRadiusMedium: 6
    readonly property int borderRadiusLarge: 10
    readonly property int borderRadiusCircle: 50
    
    // 动画
    readonly property int transitionDuration: 300
    
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
}
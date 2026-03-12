// utils/Constants.qml
//pragma Singleton
import QtQuick 2.15

QtObject {
    // 颜色系统
    readonly property color primaryBg: "#0a0f1a"
    readonly property color secondaryBg: "#121828"
    readonly property color tertiaryBg: "#1a2235"
    readonly property color cardBg: "#222c44"
    readonly property color accentBlue: "#3b82f6"
    readonly property color accentBlueLight: "#60a5fa"
    readonly property color accentBlueDark: "#1d4ed8"
    readonly property color profitGreen: "#10b981"
    readonly property color profitGreenLight: "#34d399"
    readonly property color lossRed: "#ef4444"
    readonly property color lossRedLight: "#f87171"
    readonly property color warningAmber: "#f59e0b"
    readonly property color textPrimary: "#f1f5f9"
    readonly property color textSecondary: "#94a3b8"
    readonly property color textTertiary: "#64748b"
    readonly property color borderColor: "#2d3748"
    readonly property color borderLight: "#475569"
    
    // 渐变定义
    readonly property Gradient primaryGradient: Gradient {
        GradientStop { position: 0.0; color: accentBlue }
        GradientStop { position: 1.0; color: accentBlueDark }
    }

    readonly property Gradient successGradient: Gradient {
        GradientStop { position: 0.0; color: profitGreen }
        GradientStop { position: 1.0; color: "#059669" }
    }

    readonly property Gradient dangerGradient: Gradient {
        GradientStop { position: 0.0; color: lossRed }
        GradientStop { position: 1.0; color: "#dc2626" }
    }

    readonly property Gradient warningGradient: Gradient {
        GradientStop { position: 0.0; color: warningAmber }
        GradientStop { position: 1.0; color: "#d97706" }
    }

    readonly property Gradient purpleGradient: Gradient {
        GradientStop { position: 0.0; color: "#8b5cf6" }
        GradientStop { position: 1.0; color: "#7c3aed" }
    }
    
    // 字体大小
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeNormal: 13
    readonly property int fontSizeMedium: 14
    readonly property int fontSizeLarge: 16
    readonly property int fontSizeXLarge: 18
    
    // 间距
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 12
    readonly property int spacingLarge: 16
    readonly property int spacingXLarge: 20
    
    // 圆角
    readonly property int borderRadiusSmall: 4
    readonly property int borderRadiusMedium: 8
    readonly property int borderRadiusLarge: 12
    readonly property int borderRadiusXLarge: 16
    
    // 阴影
    readonly property string shadowSm: "0 1px 3px rgba(0, 0, 0, 0.4)"
    readonly property string shadowMd: "0 4px 6px rgba(0, 0, 0, 0.5)"
    readonly property string shadowLg: "0 10px 25px rgba(0, 0, 0, 0.6)"
}
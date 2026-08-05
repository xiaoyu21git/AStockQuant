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

    // ── 交易面板专用（色值不变，语义化命名）──

    // Panel backgrounds
    readonly property color tradingPanelBg: "#091120"
    readonly property color tradingPanelBgAlt: "#091321"
    readonly property color tradingPanelGradientStart: "#0d1728"
    readonly property color tradingPanelGradientEnd: "#08101d"
    readonly property color tradingHeaderBg: "#0c1828"
    readonly property color tradingFormAreaBg: "#091321"
    readonly property color tradingOrderItemBg: "#0b1625"
    readonly property color tradingPageBg: "#0F172A"
    readonly property color tradingSkeletonBg: "#0d1728"
    readonly property color tradingSkeletonBgAlt: "#0d2236"

    // Panel borders
    readonly property color tradingPanelBorder: "#1f3148"
    readonly property color tradingPanelBorderAlt: "#1c314b"
    readonly property color tradingHeaderBorder: "#1e3147"
    readonly property color tradingFormAreaBorder: "#1b3047"
    readonly property color tradingSkeletonBorder: "#21354c"
    readonly property color tradingOrderItemBorder: "#164b5c"
    readonly property color tradingOrderItemCancelledBorder: "#5a2a2a"

    // Input fields
    readonly property color tradingInputBg: "#0f2238"
    readonly property color tradingInputBorder: "#20364f"
    readonly property color tradingInputActiveBorder: "#214362"

    // Button / chip backgrounds
    readonly property color tradingButtonBg: "#10243a"
    readonly property color tradingButtonBorder: "#214362"
    readonly property color tradingChipBorder: "#1d446d"
    readonly property color tradingAvailableCapitalBorder: "#1d446d"

    // Tab bar
    readonly property color tradingTabActiveBorder: "#14f1ff"
    readonly property color tradingTabInactiveBg: "#0f1b2d"
    readonly property color tradingTabInactiveBorder: "#1d3147"
    readonly property color tradingTabActiveText: "#03111a"
    readonly property color tradingTabInactiveText: "#b2c5de"

    // Action buttons
    readonly property color tradingBuyRed: "#cc0022"
    readonly property color tradingSellGreen: "#00cc88"
    readonly property color tradingDisabledBtnBg: "#334155"
    readonly property color tradingDisabledBtnText: "#94a3b8"
    readonly property color tradingCloseBtnText: "#ffccd5"
    readonly property color tradingPurpleBtn: "#8b5cf6"
    readonly property color tradingPurpleBtnDark: "#7c3aed"
    readonly property color tradingBlueBtn: "#3b82f6"
    readonly property color tradingAmberBtn: "#f59e0b"
    readonly property color tradingGrayBtn: "#475569"

    // Order list action buttons
    readonly property color tradingCancelText: "#ff8888"
    readonly property color tradingCancelBg: "#3f1d24"
    readonly property color tradingCheckpointBg: "#15334a"
    readonly property color tradingCheckpointText: "#67E8F9"
    readonly property color tradingResumeBg: "#3a2a14"
    readonly property color tradingResumeText: "#FBBF24"

    // Text / labels
    readonly property color tradingTitleText: "#f8fafc"
    readonly property color tradingLabelSecondary: "#8ba4c7"
    readonly property color tradingLabelTertiary: "#7ea1c5"
    readonly property color tradingLabelLight: "#89a2c8"
    readonly property color tradingValueText: "#e2e8f0"
    readonly property color tradingBrightText: "#eff6ff"
    readonly property color tradingAccentCyan: "#0ff"
    readonly property color tradingLightBlue: "#dbeafe"
    readonly property color tradingEmptyText: "#4a6a8a"

    // Toast / notification
    readonly property color tradingToastSuccessBg: "#041f24"
    readonly property color tradingToastErrorBg: "#3b0d0d"
    readonly property color tradingToastErrorBorder: "#ff6b6b"
    readonly property color tradingToastSuccessText: "#b6feff"
    readonly property color tradingToastErrorText: "#ffd5d5"

    // Search popup
    readonly property color tradingSearchPopupBg: "#080E1A"
    readonly property color tradingSearchPopupBorder: "#38BDF8"
    readonly property color tradingSearchPopupHover: "#1E3A5F"
    readonly property color tradingSearchPopupText: "#E2E8F0"

    // Quick close button
    readonly property color tradingQuickCloseBg: "#3f1d24"
    readonly property color tradingQuickCloseBorder: "#fda4af"
    readonly property color tradingQuickCloseText: "#ffe4e6"
    readonly property color tradingQuickCloseDisabledBg: "#1f2937"
    readonly property color tradingQuickCloseDisabledBorder: "#334155"
    readonly property color tradingQuickCloseDisabledText: "#94a3b8"

    // Status badges
    readonly property color tradingStatusExecScheduling: "#f59e0b"
    readonly property color tradingStatusPreTradeRisk: "#fb7185"
    readonly property color tradingStatusBrokerSubmission: "#38bdf8"
    readonly property color tradingStatusDefault: "#94a3b8"
    readonly property color tradingStatusError: "#ef4444"
    readonly property color tradingStatusSuccess: "#10b981"
    readonly property color tradingStatusWarning: "#f59e0b"
    readonly property color tradingStatusInfo: "#3b82f6"

    // Order list
    readonly property color tradingOrderListBg: "#08111e"
    readonly property color tradingOrderListBorder: "#182a40"
    readonly property color tradingOrderText: "#8aaeff"
    readonly property color tradingOrderDetailText: "#5f85a8"

    // Depth panel (bid/ask visualization)
    readonly property color depthPanelBg: "#07101c"
    readonly property color depthPanelBorder: "#1b2c40"
    readonly property color depthGradientStart: "#0b1524"
    readonly property color depthGradientEnd: "#060c16"
    readonly property color depthLabelText: "#7f96b8"
    readonly property color depthInfoText: "#9fb4d2"
    readonly property color depthActiveLevelBg: "#176b78"
    readonly property color depthActiveLevelBorder: "#39c6d6"
    readonly property color depthActiveLevelText: "#effbff"
    readonly property color depthInactiveLevelBg: "#0d1a2b"
    readonly property color depthInactiveLevelBorder: "#28405d"
    readonly property color depthInactiveLevelText: "#8ea8cb"
    readonly property color depthBidRowBg: "#0f201f"
    readonly property color depthBidRowBorder: "#1b5d57"
    readonly property color depthAskRowBg: "#251316"
    readonly property color depthAskRowBorder: "#6d2931"
    readonly property color depthAggregateBar: "#1f5f6a"
    readonly property color depthBidText: "#5eead4"
    readonly property color depthAskText: "#fca5a5"
    readonly property color depthBidVolumeText: "#8ef1d8"
    readonly property color depthAskVolumeText: "#ffb4b8"
    readonly property color depthTickBuyBorder: "#184f4a"
    readonly property color depthTickSellBorder: "#6c2b2f"
    readonly property color depthTickNeutralText: "#cbd5e1"
    readonly property color depthLimitDownGreen: "#008822"

    // Chart workspace
    readonly property color chartBg: "#0b1220"
    readonly property color chartBorder: "#1f2e45"
    readonly property color chartLabelText: "#8ea3bd"
    readonly property color chartPanelBg: "#0f1726"
    readonly property color chartPanelBorder: "#223147"
    readonly property color chartCardBg: "#101d31"
    readonly property color chartCardBorder: "#20324a"
    readonly property color chartCardAltBorder: "#20314a"
    readonly property color chartInfoText: "#7c93af"
    readonly property color chartPriceBadgeUpBg: "#0f2234"
    readonly property color chartPriceBadgeDownBg: "#2a1a1a"
    readonly property color chartPriceBadgeUpBorder: "#24517a"
    readonly property color chartPriceBadgeDownBorder: "#6b2a2a"
    readonly property color chartPriceBadgeUpText: "#7dd3fc"
    readonly property color chartTabActiveBg: "#18314e"
    readonly property color chartTabInactiveBg: "#101a2a"
    readonly property color chartTabActiveBorder: "#4f8cff"
    readonly property color chartTabInactiveBorder: "#26364d"
    readonly property color chartTabText: "#a5b4c7"
    readonly property color chartBidLevelText: "#93c5fd"
    readonly property color chartAskLevelText: "#fbbf24"
    readonly property color chartOrderBookBg: "#0c1626"
    readonly property color chartOrderBookBorder: "#20314a"
    readonly property color chartOrderBookLevelBg: "#13233a"
    readonly property color chartProfitText: "#6ee7b7"
    readonly property color chartLossText: "#fca5a5"
    readonly property color chartActivePositionBg: "#15263c"
    readonly property color chartNeutralText: "#cbd5e1"
}
import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: detailCard
    radius: 16  // borderRadiusXLarge
    color: "#1E293B"  // secondaryBg
    border.color: "#475569"  // borderColor
    
    // 属性
    property string strategyName: "双均线策略"
    property string strategyType: "趋势跟踪 · 股票 · 日内交易"
    property string status: "running"
    property int runningDays: 45
    property int tradesCount: 128
    property string position: "$45,680"
    property string dailyPnL: "+$1,245"
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color profitGreen: "#10B981"
    readonly property color lossRed: "#EF4444"
    readonly property color warningAmber: "#F59E0B"
    
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 16
    
    readonly property real spacingSmall: 4
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    
    readonly property real borderRadiusMedium: 8
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        
        // 头部
        RowLayout {
            ColumnLayout {
                spacing: spacingSmall
                
                Text {
                    text: detailCard.strategyName
                    font.pixelSize: fontSizeLarge
                    font.weight: Font.DemiBold
                    color: textPrimary
                }
                
                Text {
                    text: detailCard.strategyType
                    font.pixelSize: fontSizeNormal
                    color: textTertiary
                }
            }
            
            Item { Layout.fillWidth: true }
            
            // 状态标签
            Rectangle {
                width: 60
                height: 24
                radius: borderRadiusMedium
                color: {
                    if (status === "running") return Qt.rgba(16/255, 185/255, 129/255, 0.15);
                    if (status === "paused") return Qt.rgba(245/255, 158/255, 11/255, 0.15);
                    return Qt.rgba(239/255, 68/255, 68/255, 0.15);
                }
                
                Text {
                    anchors.centerIn: parent
                    text: {
                        if (status === "running") return "运行中";
                        if (status === "paused") return "已暂停";
                        return "已停止";
                    }
                    font.pixelSize: fontSizeSmall
                    font.weight: Font.DemiBold
                    color: {
                        if (status === "running") return profitGreen;
                        if (status === "paused") return warningAmber;
                        return lossRed;
                    }
                }
            }
        }
        
        // 统计指标
        GridLayout {
            columns: 2
            columnSpacing: spacingLarge
            rowSpacing: spacingLarge
            Layout.topMargin: spacingLarge
            Layout.bottomMargin: spacingLarge
            
            // 运行时间
            StatItem {
                label: "运行时间"
                value: detailCard.runningDays.toString()
                unit: "天"
                valueColor: textPrimary
            }
            
            // 交易次数
            StatItem {
                label: "交易次数"
                value: detailCard.tradesCount.toString()
                valueColor: textPrimary
            }
            
            // 当前持仓
            StatItem {
                label: "当前持仓"
                value: detailCard.position
                valueColor: profitGreen
            }
            
            // 今日盈亏
            StatItem {
                label: "今日盈亏"
                value: detailCard.dailyPnL
                valueColor: detailCard.dailyPnL.startsWith("+") ? profitGreen : lossRed
            }
        }
    }
    
    // StatItem 组件
    component StatItem: Rectangle {
        property string label: ""
        property string value: ""
        property string unit: ""
        property color valueColor: "#F1F5F9"
        
        implicitHeight: 70
        radius: borderRadiusMedium
        color: Qt.rgba(148/255, 163/255, 184/255, 0.1)
        
        Column {
            anchors.centerIn: parent
            spacing: spacingSmall
            
            Text {
                text: parent.label
                font.pixelSize: fontSizeSmall
                color: textTertiary
                anchors.horizontalCenter: parent.horizontalCenter
            }
            
            Row {
                spacing: 2
                anchors.horizontalCenter: parent.horizontalCenter
                
                Text {
                    text: parent.value
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: parent.valueColor
                }
                
                Text {
                    text: parent.unit
                    font.pixelSize: fontSizeSmall
                    color: textTertiary
                    anchors.baseline: parent.baseline
                    visible: parent.unit !== ""
                }
            }
        }
    }
}
// StrategyCard.qml
// 策略卡片组件，继承BaseQuantCard，集成策略控制功能和参数面板
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 6.0
import "./Base" as BaseComponents
import "./Strategy" as StrategyComponents

/**
 * 策略卡片组件
 * 继承BaseQuantCard，添加策略特有功能和控制面板
 * 集成策略启动/停止/优化控制、参数调整和实时状态
 */
BaseQuantCard {
    id: strategyCard

    BaseComponents.Constants {
        id: baseConstants
    }
    
    // 与因子卡片完全对齐的视觉属性
    radius: 10  // 与因子卡片保持一致
    color: selected ? Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.15) : "#1E293B"
    border.color: selected ? categoryColor : "#334155"  // 与因子卡片的边框颜色对齐
    
    // 增强文字颜色对比度（与因子卡片一致）
    property color textColorEnhanced: "#F1F5F9"  // 因子卡片使用 #F1F5F9
    property color textSecondaryEnhanced: "#FFFFFF"  // 将次要文字颜色改为白色
    
    // 禁用卡片整体点击，只响应按钮点击
    enableRealTimeFeedback: true
    enableCardClick: false  // 禁用卡片整体点击，只响应按钮点击
    
    // 覆盖基类的显示属性以对齐因子卡片
    showActions: true  // 始终显示操作按钮
    showMiniChart: true
    showGroupReturns: false
    
    // ============ 策略特有属性 ============
    
    // 覆盖父类的实体类型
    entityType: "strategy"
    
    // 策略特有属性
    property string strategyId: ""              // 策略ID
    property string strategyName: ""            // 策略名称
    property string strategyType: "趋势策略"     // 策略类型
    
    // 性能指标
    property real returns: 0.0                  // 累计收益率
    property real sharpeRatio: 0.0              // 夏普比率
    property real maxDrawdown: 0.0              // 最大回撤
    property real winRate: 0.0                  // 胜率
    
    // 实时状态
    property int runningDays: 0                 // 运行天数
    property int tradesCount: 0                 // 交易次数
    property real dailyPnL: 0                   // 今日盈亏
    property real position: 0                   // 持仓规模
    
    // 控制参数
    property var controlParameters: []          // 控制参数数组，格式：[{name, value, min, max, unit, color}]
    property bool showControlPanel: false       // 是否显示控制面板
    property bool showParameterPanel: false     // 是否显示参数面板
    
    // ============ 特有信号 ============
    
    signal startClicked()                       // 启动策略
    signal stopClicked()                        // 停止策略
    signal pauseClicked()                       // 暂停策略
    signal optimizeClicked()                    // 优化策略
    signal parameterChanged(int index, real value)  // 参数改变
    signal showParametersToggled(bool show)     // 显示/隐藏参数面板
    
    // ============ 初始化属性 ============
    
    // 将策略属性映射到基类属性
    entityId: strategyId
    displayName: strategyName
    category: strategyType
    
    // 设置类别颜色
    categoryColor: getStrategyTypeColor(strategyType)
    
    // 性能指标配置（覆盖父类）
    performanceMetrics: [
        {
            label: "收益率",
            value: returns,
            format: "%.2f",
            unit: "%",
            color: returns >= 0 ? baseConstants.profitGreen : baseConstants.lossRed,
            tooltip: "累计收益率"
        },
        {
            label: "夏普比率",
            value: sharpeRatio,
            format: "%.2f",
            color: categoryColor,
            tooltip: "风险调整后收益"
        },
        {
            label: "最大回撤",
            value: maxDrawdown,
            format: "%.2f",
            unit: "%",
            color: maxDrawdown >= 0 ? baseConstants.lossRed : categoryColor,
            tooltip: "最大历史亏损"
        },
        {
            label: "胜率",
            value: winRate,
            format: "%.1f",
            unit: "%",
            color: categoryColor,
            tooltip: "交易胜率"
        }
    ]
    
    // 附加指标配置
    additionalMetrics: [
        {
            label: "运行天数",
            value: runningDays,
            format: "%d",
            unit: "天",
            color: categoryColor,
            tooltip: "策略已运行天数"
        },
        {
            label: "交易次数",
            value: tradesCount,
            format: "%d",
            unit: "次",
            color: categoryColor,
            tooltip: "累计交易次数"
        },
        {
            label: "今日盈亏",
            value: dailyPnL,
            format: dailyPnL >= 0 ? "+$.0f" : "-$0",
            color: dailyPnL >= 0 ? baseConstants.profitGreen : baseConstants.lossRed,
            tooltip: "当日盈亏金额"
        },
        {
            label: "持仓",
            value: position,
            format: "$%.0f",
            color: categoryColor,
            tooltip: "当前持仓规模"
        }
    ]
    
    // 图表数据配置
    chartData: calculateStrategyChartData()
    
    // ============ 卡片布局调整以对齐因子卡片 ============
    
    // 覆盖基类的操作按钮区域 - 实现与因子卡片相同的布局方式
    Item {
        id: strategyActions
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: spacingXLarge  // 从spacingMedium改为spacingXLarge以增加控件与边框的距离
        anchors.bottomMargin: spacingMedium
        height: 32
        
        // 操作按钮区域（模仿因子卡片的布局）
        Row {
            anchors.centerIn: parent
            spacing: 4
            
            // 启动按钮
            Rectangle {
                width: 28
                height: 28
                radius: 6
                color: Qt.rgba(status === "RUNNING" ? baseConstants.warningAmber.r : baseConstants.profitGreen.r, 
                              status === "RUNNING" ? baseConstants.warningAmber.g : baseConstants.profitGreen.g, 
                              status === "RUNNING" ? baseConstants.warningAmber.b : baseConstants.profitGreen.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: status === "RUNNING" ? "⏸" : "▶"
                    font.pixelSize: 12
                    color: status === "RUNNING" ? baseConstants.warningAmber : baseConstants.profitGreen
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (status === "RUNNING") {
                            pauseClicked()
                        } else {
                            startClicked()
                        }
                    }
                }
            }
            
            // 停止按钮（仅在运行或暂停时显示）
            Rectangle {
                visible: status === "RUNNING" || status === "PAUSED"
                width: 28
                height: 28
                radius: 6
                color: Qt.rgba(baseConstants.lossRed.r, baseConstants.lossRed.g, baseConstants.lossRed.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: "■"
                    font.pixelSize: 12
                    color: baseConstants.lossRed
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: stopClicked()
                }
            }
            
            // 优化按钮
            Rectangle {
                width: 28
                height: 28
                radius: 6
                color: Qt.rgba(baseConstants.accentBlue.r, baseConstants.accentBlue.g, baseConstants.accentBlue.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: "⚙"
                    font.pixelSize: 12
                    color: baseConstants.accentBlue
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: optimizeClicked()
                }
            }
            
            // 参数面板切换按钮
            Rectangle {
                width: 28
                height: 28
                radius: 6
                color: showParameterPanel ? Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.2) : 
                                         Qt.rgba(baseConstants.textTertiary.r, baseConstants.textTertiary.g, baseConstants.textTertiary.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: showParameterPanel ? "▲" : "▼"
                    font.pixelSize: 12
                    color: showParameterPanel ? categoryColor : baseConstants.textTertiary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: showParametersToggled(!showParameterPanel)
                }
            }
        }
    }
    
    // 参数面板区域（优化显示位置）
    Rectangle {
        id: paramPanel
        anchors.bottom: strategyActions.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: spacingXLarge  // 从spacingMedium改为spacingXLarge以增加控件与边框的距离
        anchors.bottomMargin: spacingSmall
        height: showParameterPanel && controlParameters && controlParameters.length > 0 ? paramGrid.height + 20 : 0
        radius: 6
        color: baseConstants.tertiaryBg
        visible: showParameterPanel && controlParameters && controlParameters.length > 0
        opacity: showParameterPanel ? 1.0 : 0.0
        
        Behavior on height {
            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
        }
        
        Behavior on opacity {
            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
        }
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
            
            Text {
                text: "策略参数"
                font.pixelSize: baseConstants.fontSizeSmall
                font.weight: Font.Medium
                color: baseConstants.textPrimary
                Layout.alignment: Qt.AlignLeft
            }
            
            GridLayout {
                id: paramGrid
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Layout.fillWidth: true
                
                Repeater {
                    model: controlParameters
                    
                    delegate: ColumnLayout {
                        spacing: 2
                        
                        RowLayout {
                            Text {
                                text: modelData.name
                                font.pixelSize: baseConstants.fontSizeSmall - 1
                                color: baseConstants.textSecondary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            
                            Text {
                                text: modelData.value + (modelData.unit || "")
                                font.pixelSize: baseConstants.fontSizeSmall - 1
                                font.weight: Font.Medium
                                color: getParamColor(modelData.color || "blue")
                            }
                        }
                        
                        // 简化滑块
                        Rectangle {
                            Layout.fillWidth: true
                            height: 4
                            radius: 2
                            color: baseConstants.borderLight
                            
                            Rectangle {
                                width: parent.width * ((modelData.value - modelData.min) / (modelData.max - modelData.min))
                                height: parent.height
                                radius: 2
                                color: getParamColor(modelData.color || "blue")
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 已移除顶部的参数面板触发器，功能按钮区域已集成参数面板切换按钮
    
    // ============ 工具函数 ============
    
    // 根据策略类型获取颜色
    function getStrategyTypeColor(strategyType) {
        switch (strategyType) {
            case "趋势策略":
            case "动量策略": return baseConstants.accentBlue;
            case "价值策略":
            case "均值回归": return baseConstants.warningAmber;
            case "质量策略":
            case "基本面策略": return baseConstants.profitGreen;
            case "成长策略":
            case "高增长策略": return Qt.color("#8B5CF6");
            case "情绪策略":
            case "市场情绪策略": return Qt.color("#EC4899");  // 粉色
            case "波动策略":
            case "套利策略": return baseConstants.lossRed;
            case "组合策略":
            case "多因子策略": return Qt.color("#06B6D4");  // 青色
            default: return baseConstants.accentBlue;
        }
    }
    
    // 根据参数颜色字符串获取颜色
    function getParamColor(colorStr) {
        switch (colorStr) {
            case "blue": return baseConstants.accentBlue;
            case "green": return baseConstants.profitGreen;
            case "red": return baseConstants.lossRed;
            case "amber": return baseConstants.warningAmber;
            case "purple": return Qt.color("#8B5CF6");
            default: return baseConstants.accentBlue;
        }
    }
    
    // 计算策略图表数据
    function calculateStrategyChartData() {
        // 基于收益率生成模拟图表数据
        var data = []
        var baseValue = returns / 100 / 30  // 将收益率转换为每日收益
        
        // 生成30天的模拟数据
        for (var i = 0; i < 30; i++) {
            var dayOffset = i - 15  // 中心点在15天
            var value = baseValue * Math.exp(-dayOffset * dayOffset / 100) * (1 + Math.random() * 0.2 - 0.1)
            data.push(value)
        }
        
        return data
    }
    
    // ============ 信号处理 ============
    
    // 重写基类的actionRequested信号处理
    onActionRequested: function(action) {
        switch (action) {
            case "start": startClicked(); break;
            case "stop": stopClicked(); break;
            case "pause": pauseClicked(); break;
            case "optimize": optimizeClicked(); break;
            case "toggleParams": showParametersToggled(!showParameterPanel); break;
            default: console.log("未知操作:", action);
        }
    }
    
    // ============ 状态指示器 ============
    
    // 状态指示器集成在标题行
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: 12
        height: 12
        radius: 6
        color: statusColor
        
        // 状态闪烁动画（如果是运行中状态）
        SequentialAnimation on opacity {
            running: status === "RUNNING"
            loops: Animation.Infinite
            PropertyAnimation { to: 0.5; duration: 1000 }
            PropertyAnimation { to: 1.0; duration: 1000 }
        }
    }
}
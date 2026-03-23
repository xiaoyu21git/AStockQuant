// StrategyBacktestPage.qml
// 策略回测页面 - 动态参数版本
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import "../../components/FactorWorkbench/Creation/components" as PluginComponents
import "../../utils/RiskBacktestMetaLoader.js" as RiskBacktestMeta

/**
 * 策略回测页面组件
 * 提供交易策略历史表现回测功能
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property Bridge.GlobalDataService globalDataService: null
    property Bridge.FactorService factorService: null
    property Bridge.StrategyBacktestController strategyBacktestController: null
    property string selectedStrategyId: ""
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    
    // 回测结果
    property var backtestResult: ({})
    property var performanceStats: ({})
    property var riskMetrics: ({})
    
    // ============ 动态参数配置 ============
    
    // 动态参数生成器
    property var dynamicParamConfigs: []
    property var dynamicParamValues: ({})
    property bool parametersLoaded: false
    
    // 插件化组件注册表
    PluginComponents.ParamComponents {
        id: paramComponents
        Component.onCompleted: {
            console.log("策略回测参数组件初始化完成")
            // 注册所有组件
            if (typeof paramComponents.registerAllComponents === 'function') {
                paramComponents.registerAllComponents()
            }
            // 初始化动态参数
            root.initDynamicParams()
        }
    }
    
    // ============ UI ============
    
    // 主布局
    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 24
        spacing: 0
        
        // 标题区域
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            spacing: 4
            
            Text {
                text: "📈 策略回测工作区（交易策略验证）"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            Text {
                text: "测试交易策略的盈利能力，评估策略的风险收益特征"
                font.pixelSize: 12
                color: "#94A3B8"
                wrapMode: Text.WordWrap
            }
            
            // 分隔线
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#334155"
                Layout.topMargin: 8
            }
        }
        
        // 滚动区域（主要内容）
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            // 隐藏滚动条样式
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            // 滚动区域内容
            ColumnLayout {
                id: contentLayout
                width: scrollView.width - 20  // 为滚动条留出空间
                spacing: 16
                
                // 策略配置面板（动态参数版本）
                Rectangle {
                    id: configPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: 400
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        
                        Text {
                            text: "⚙️ 策略配置"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        // 策略选择
                        RowLayout {
                            spacing: 16
                            
                            ColumnLayout {
                                spacing: 4
                                
                                Text {
                                    text: "选择策略"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                ComboBox {
                                    id: strategyComboBox
                                    Layout.preferredWidth: 200
                                    model: ["均线交叉策略", "动量策略", "反转策略", "多因子策略"]
                                    currentIndex: 0
                                    
                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }
                                    
                                    contentItem: Text {
                                        text: parent.displayText
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        horizontalAlignment: Text.AlignLeft
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    
                                    popup: Popup {
                                        width: parent.width
                                        y: parent.height
                                        padding: 4
                                        
                                        contentItem: ListView {
                                            implicitHeight: contentHeight
                                            model: parent.parent.model
                                            currentIndex: parent.parent.highlightedIndex
                                            clip: true
                                            interactive: false
                                            
                                            delegate: ItemDelegate {
                                                width: parent.width
                                                height: 32
                                                
                                                Text {
                                                    text: modelData
                                                    color: parent.highlighted ? "#3B82F6" : "#F1F5F9"
                                                    font.pixelSize: 12
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    anchors.left: parent.left
                                                    anchors.leftMargin: 8
                                                }
                                                
                                                background: Rectangle {
                                                    color: parent.highlighted ? "#0F172A" : "transparent"
                                                    radius: 4
                                                }
                                            }
                                        }
                                        
                                        background: Rectangle {
                                            color: "#1E293B"
                                            border.width: 1
                                            border.color: "#334155"
                                            radius: 6
                                        }
                                    }
                                }
                            }
                            
                            Item { Layout.fillWidth: true }
                        }
                        
                        // 动态参数配置区域
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 300
                            radius: 8
                            color: "#0F172A"
                            border.width: 1
                            border.color: "#334155"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8
                                
                                Text {
                                    text: "📊 回测参数配置"
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: "配置回测周期、资金管理、交易成本等参数"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                
                                // 动态参数生成器实例
                                PluginComponents.DynamicParamGenerator {
                                    id: dynamicParamGenerator
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    paramRegistry: paramComponents.paramRegistry
                                    configs: root.dynamicParamConfigs
                                    values: root.dynamicParamValues
                                    
                                    // 当参数值变化时更新
                                    onParamsChanged: function(newValues) {
                                        console.log("回测动态参数变化:", newValues)
                                        root.dynamicParamValues = newValues
                                    }
                                }
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: parametersLoaded ? "参数配置已加载" : "正在加载回测参数配置..."
                                    font.pixelSize: 12
                                    color: parametersLoaded ? "#10B981" : "#F59E0B"
                                }
                            }
                        }
                    }
                }
                
                // 回测控制面板
                Rectangle {
                    id: controlPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        
                        // 回测控制
                        RowLayout {
                            spacing: 12
                            
                            // 回测按钮
                            Rectangle {
                                id: backtestButton
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 40
                                radius: 8
                                color: isBacktesting ? "#334155" : "#3B82F6"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: isBacktesting ? "⏸️" : "▶️"
                                        font.pixelSize: 14
                                        color: isBacktesting ? "#94A3B8" : "white"
                                    }
                                    
                                    Text {
                                        text: isBacktesting ? "回测中..." : "开始回测"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: isBacktesting ? "#94A3B8" : "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: !isBacktesting
                                    onClicked: startBacktest()
                                }
                            }
                            
                            // 进度条
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 8
                                radius: 4
                                color: "#334155"
                                visible: isBacktesting
                                
                                Rectangle {
                                    width: parent.width * (backtestProgress / 100)
                                    height: parent.height
                                    radius: 4
                                    color: "#3B82F6"
                                }
                            }
                            
                            // 进度文本
                            Text {
                                text: isBacktesting ? backtestProgress + "%" : ""
                                font.pixelSize: 12
                                color: "#94A3B8"
                                visible: isBacktesting
                            }
                            
                            // 状态文本
                            Text {
                                text: backtestStatus
                                font.pixelSize: 12
                                color: isBacktesting ? "#F59E0B" : "#94A3B8"
                            }
                            
                            Item { Layout.fillWidth: true }
                        }
                        
                        // 提示信息
                        Text {
                            text: "点击开始回测按钮，系统将使用历史数据验证策略表现"
                            font.pixelSize: 10
                            color: "#64748B"
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
                
                // 回测结果区域
                Rectangle {
                    id: resultPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: 500
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        
                        // 结果标题
                        RowLayout {
                            spacing: 8
                            
                            Text {
                                text: "📊 策略回测结果"
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            // 结果状态
                            Text {
                                text: getResultStatusText()
                                font.pixelSize: 12
                                color: getResultStatusColor()
                                
                                function getResultStatusText() {
                                    if (isBacktesting) return "回测中..."
                                    if (backtestResult && Object.keys(backtestResult).length > 0) 
                                        return "策略: " + (strategyComboBox.currentText || "未命名策略")
                                    return "请配置策略进行回测"
                                }
                                
                                function getResultStatusColor() {
                                    if (isBacktesting) return "#F59E0B"
                                    if (backtestResult && Object.keys(backtestResult).length > 0) 
                                        return "#3B82F6"
                                    return "#94A3B8"
                                }
                            }
                        }
                        
                        // 结果网格
                        GridLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 220
                            columns: 3
                            columnSpacing: 16
                            rowSpacing: 16
                            
                            // 总收益
                            ResultCard {
                                title: "总收益"
                                value: backtestResult.totalReturn ? (backtestResult.totalReturn * 100).toFixed(1) + "%" : "--"
                                description: "Total Return"
                                trend: backtestResult.totalReturn > 0 ? "up" : (backtestResult.totalReturn < 0 ? "down" : "neutral")
                            }
                            
                            // 年化收益
                            ResultCard {
                                title: "年化收益"
                                value: backtestResult.annualReturn ? (backtestResult.annualReturn * 100).toFixed(1) + "%" : "--"
                                description: "Annual Return"
                                trend: backtestResult.annualReturn > 0 ? "up" : (backtestResult.annualReturn < 0 ? "down" : "neutral")
                            }
                            
                            // 夏普比率
                            ResultCard {
                                title: "夏普比率"
                                value: backtestResult.sharpeRatio ? backtestResult.sharpeRatio.toFixed(2) : "--"
                                description: "Sharpe Ratio"
                                trend: backtestResult.sharpeRatio > 0 ? "up" : (backtestResult.sharpeRatio < 0 ? "down" : "neutral")
                            }
                            
                            // 最大回撤
                            ResultCard {
                                title: "最大回撤"
                                value: backtestResult.maxDrawdown ? (backtestResult.maxDrawdown * 100).toFixed(1) + "%" : "--"
                                description: "Max Drawdown"
                                trend: "down"  // 最大回撤总是负向指标
                            }
                            
                            // 胜率
                            ResultCard {
                                title: "胜率"
                                value: backtestResult.winRate ? (backtestResult.winRate * 100).toFixed(1) + "%" : "--"
                                description: "Win Rate"
                                trend: backtestResult.winRate > 0.5 ? "up" : (backtestResult.winRate < 0.5 ? "down" : "neutral")
                            }
                            
                            // 盈亏比
                            ResultCard {
                                title: "盈亏比"
                                value: backtestResult.profitLossRatio ? backtestResult.profitLossRatio.toFixed(2) : "--"
                                description: "Profit/Loss Ratio"
                                trend: backtestResult.profitLossRatio > 1 ? "up" : (backtestResult.profitLossRatio < 1 ? "down" : "neutral")
                            }
                        }
                        
                        // 交易统计
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            radius: 8
                            color: "#0F172A"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8
                                
                                Text {
                                    text: "📈 交易统计"
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }
                                
                                GridLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    columns: 3
                                    columnSpacing: 16
                                    rowSpacing: 8
                                    
                                    StatItem {
                                        label: "总交易次数"
                                        value: backtestResult.totalTrades || 0
                                    }
                                    
                                    StatItem {
                                        label: "盈利交易"
                                        value: backtestResult.winningTrades || 0
                                        valueColor: "#10B981"
                                    }
                                    
                                    StatItem {
                                        label: "亏损交易"
                                        value: backtestResult.losingTrades || 0
                                        valueColor: "#EF4444"
                                    }
                                    
                                    StatItem {
                                        label: "平均盈利"
                                        value: backtestResult.averageWin ? (backtestResult.averageWin * 100).toFixed(2) + "%" : "--"
                                        valueColor: "#10B981"
                                    }
                                    
                                    StatItem {
                                        label: "平均亏损"
                                        value: backtestResult.averageLoss ? (backtestResult.averageLoss * 100).toFixed(2) + "%" : "--"
                                        valueColor: "#EF4444"
                                    }
                                    
                                    StatItem {
                                        label: "交易天数"
                                        value: backtestResult.tradingDays || 0
                                    }
                                }
                            }
                        }
                        
                        // 控制按钮
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            spacing: 12
                            
                            // 查看详细结果
                            Rectangle {
                                Layout.preferredWidth: 120
                                Layout.fillHeight: true
                                radius: 6
                                color: "#334155"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    
                                    Text {
                                        text: "📈"
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                    }
                                    
                                    Text {
                                        text: "详细结果"
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: showDetailedResults()
                                }
                            }
                            
                            // 参数优化
                            Rectangle {
                                Layout.preferredWidth: 100
                                Layout.fillHeight: true
                                radius: 6
                                color: "#334155"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    
                                    Text {
                                        text: "⚙️"
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                    }
                                    
                                    Text {
                                        text: "优化"
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: showOptimization()
                                }
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            // 导出结果
                            Rectangle {
                                Layout.preferredWidth: 100
                                Layout.fillHeight: true
                                radius: 6
                                color: "#3B82F6"
                                enabled: backtestResult && Object.keys(backtestResult).length > 0
                                opacity: enabled ? 1.0 : 0.5
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    
                                    Text {
                                        text: "📤"
                                        font.pixelSize: 12
                                        color: "white"
                                    }
                                    
                                    Text {
                                        text: "导出"
                                        font.pixelSize: 12
                                        color: "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: parent.enabled
                                    onClicked: exportResults()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 组件定义 ============
    
    // 结果卡片组件
    component ResultCard: Item {
        property string title: ""
        property string value: ""
        property string description: ""
        property string trend: "neutral"  // up, down, neutral
        
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#0F172A"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4
                
                Text {
                    text: title
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }
                
                Row {
                    spacing: 6
                    
                    Text {
                        text: value
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: getValueColor()
                    }
                    
                    // 趋势指示器
                    Text {
                        visible: trend !== "neutral" && value !== "--"
                        text: trend === "up" ? "↑" : "↓"
                        font.pixelSize: 14
                        color: trend === "up" ? "#10B981" : "#EF4444"
                    }
                }
                
                Text {
                    text: description
                    font.pixelSize: 10
                    color: "#94A3B8"
                }
            }
        }
        
        // 根据数值获取颜色
        function getValueColor() {
            if (value === "--") return "#94A3B8"
            if (trend === "up") return "#10B981"
            if (trend === "down") return "#EF4444"
            return "#F1F5F9"
        }
    }
    
    // 统计项组件
    component StatItem: Item {
        property string label: ""
        property string value: ""
        property string valueColor: "#F1F5F9"
        
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 2
            
            Text {
                text: label
                font.pixelSize: 10
                color: "#94A3B8"
            }
            
            Text {
                text: value
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: valueColor
            }
        }
    }
    
    // ============ 动态参数方法 ============
    
    // 初始化动态参数配置
    function initDynamicParams() {
        console.log("初始化策略回测动态参数配置")
        
        // 生成动态参数配置
        generateDynamicParamConfigs()
        
        // 初始化动态值
        initDynamicValues()
    }
    
    // 生成动态参数配置（从JSON文件动态加载）
    function generateDynamicParamConfigs() {
        console.log("开始动态加载策略回测参数配置")
        
        // 从配置文件加载
        RiskBacktestMeta.loadMetaFile("qrc:/config/views/risk_backtest_params.json", function(meta) {
            if (meta) {
                console.log("成功加载策略回测参数配置")
                
                // 清空现有配置
                dynamicParamConfigs = []
                
                // 只加载回测相关的参数
                var backtestParamConfigs = RiskBacktestMeta.getParameterConfigs("backtest")
                
                // 转换为动态参数生成器所需的格式
                backtestParamConfigs.forEach(function(paramConfig) {
                    var config = {
                        id: paramConfig.id,
                        type: paramConfig.type,
                        label: paramConfig.label,
                        description: paramConfig.description,
                        default: paramConfig.default,
                        category: paramConfig.category,
                        group: paramConfig.category || "回测配置"
                    }
                    
                    // 根据类型添加特定属性
                    switch (paramConfig.type) {
                        case "slider":
                            config.min = paramConfig.min
                            config.max = paramConfig.max
                            config.step = paramConfig.step || 0.01
                            config.unit = paramConfig.unit || ""
                            break
                        case "select":
                            config.type = "select"
                            config.options = paramConfig.options || []
                            config.multiple = paramConfig.multiple || false
                            break
                        case "toggle":
                            config.type = "toggle"
                            config.trueLabel = paramConfig.trueLabel || "是"
                            config.falseLabel = paramConfig.falseLabel || "否"
                            break
                    }
                    
                    // 处理可见性条件
                    if (paramConfig.visibleWhen) {
                        config.visibleWhen = paramConfig.visibleWhen
                    }
                    
                    dynamicParamConfigs.push(config)
                })
                
                console.log("策略回测动态参数配置加载完成，数量:", dynamicParamConfigs.length)
                
                // 初始化动态值
                initDynamicValues()
                
                // 设置动态参数生成器的配置
                if (dynamicParamGenerator) {
                    dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, [])
                }
                
                parametersLoaded = true
            } else {
                console.error("加载策略回测参数配置失败，使用默认配置")
                generateFallbackParamConfigs()
            }
        })
    }
    
    // 后备参数配置（当动态加载失败时使用）
    function generateFallbackParamConfigs() {
        dynamicParamConfigs = []
        
        // 基础回测参数
        dynamicParamConfigs.push({
            id: "backtestPeriod",
            type: "select",
            label: "回测周期",
            description: "选择回测的时间周期",
            options: [
                { value: "1year", label: "最近1年" },
                { value: "3year", label: "最近3年" },
                { value: "5year", label: "最近5年" },
                { value: "full", label: "全周期" }
            ],
            default: "3year",
            category: "period",
            group: "时间周期配置"
        })
        
        dynamicParamConfigs.push({
            id: "initialCapital",
            type: "slider",
            label: "初始资金",
            description: "回测的初始资金金额（万元）",
            min: 10,
            max: 1000,
            step: 10,
            default: 100,
            unit: "万元",
            category: "capital",
            group: "资金管理"
        })
        
        dynamicParamConfigs.push({
            id: "commissionRate",
            type: "slider",
            label: "交易佣金",
            description: "每笔交易的佣金费率",
            min: 0.0001,
            max: 0.005,
            step: 0.0001,
            default: 0.001,
            unit: "%",
            category: "cost",
            group: "交易成本"
        })
        
        dynamicParamConfigs.push({
            id: "slippageRate",
            type: "slider",
            label: "滑点率",
            description: "交易执行时的价格滑点率",
            min: 0,
            max: 0.01,
            step: 0.0001,
            default: 0.002,
            unit: "%",
            category: "cost",
            group: "交易成本"
        })
        
        dynamicParamConfigs.push({
            id: "maxPositions",
            type: "slider",
            label: "最大持仓数",
            description: "同时持有的最大股票数量",
            min: 1,
            max: 50,
            step: 1,
            default: 10,
            unit: "只",
            category: "position",
            group: "仓位管理"
        })
        
        dynamicParamConfigs.push({
            id: "positionPercent",
            type: "slider",
            label: "单股仓位比例",
            description: "单一个股最大持仓占账户总资产的比例",
            min: 0.01,
            max: 0.5,
            step: 0.01,
            default: 0.1,
            unit: "%",
            category: "position",
            group: "仓位管理"
        })
        
        dynamicParamConfigs.push({
            id: "rebalanceDays",
            type: "slider",
            label: "调仓周期",
            description: "策略调仓的天数间隔",
            min: 1,
            max: 30,
            step: 1,
            default: 5,
            unit: "天",
            category: "frequency",
            group: "调仓频率"
        })
        
        dynamicParamConfigs.push({
            id: "stopLossPercent",
            type: "slider",
            label: "止损比例",
            description: "单个头寸的最大亏损比例",
            min: 0.01,
            max: 0.3,
            step: 0.01,
            default: 0.1,
            unit: "%",
            category: "risk",
            group: "风险控制"
        })
        
        dynamicParamConfigs.push({
            id: "takeProfitPercent",
            type: "slider",
            label: "止盈比例",
            description: "单个头寸的目标盈利比例",
            min: 0.05,
            max: 0.5,
            step: 0.01,
            default: 0.2,
            unit: "%",
            category: "risk",
            group: "风险控制"
        })
        
        dynamicParamConfigs.push({
            id: "enableShortSelling",
            type: "toggle",
            label: "允许卖空",
            description: "是否允许卖空操作",
            default: false,
            category: "advanced",
            group: "高级选项"
        })
        
        console.log("使用后备策略回测参数配置，数量:", dynamicParamConfigs.length)
        parametersLoaded = true
    }
    
    // 初始化动态参数值
    function initDynamicValues() {
        var values = {}
        dynamicParamConfigs.forEach(function(config) {
            if (config.default !== undefined) {
                values[config.id] = config.default
            }
        })
        dynamicParamValues = values
        
        // 更新动态参数生成器的值
        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValues(values)
        }
        
        console.log("初始化策略回测动态参数值完成:", values)
    }
    
    // ============ 内部函数 ============
    // （以下函数保持不变，同原代码）
    // ... 保持原 startBacktest, getBacktestConfig, simulateBacktest, generateMockResult, updateResults, 
    // showDetailedResults, showOptimization, exportResults, getStrategyIdByName, showToast 等函数 ...
    
    // 更新结果
    function updateResults(result) {
        backtestResult = result
        
        // 提取绩效统计
        if (result) {
            performanceStats = {
                totalReturn: result.totalReturn,
                annualReturn: result.annualReturn,
                sharpeRatio: result.sharpeRatio
            }
            
            riskMetrics = {
                maxDrawdown: result.maxDrawdown,
                winRate: result.winRate,
                profitLossRatio: result.profitLossRatio
            }
        }
        
        // 强制重新计算
        backtestResult = JSON.parse(JSON.stringify(backtestResult || {}))
    }
}

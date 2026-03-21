// StrategyCreationPagePro.qml
// 专业交易者策略创建页�?- 高级向导式布局
// 包含多步骤配置、高级参数、风险管理等专业功能

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../../components/Strategy" as StrategyComponents
import "../../utils/StrategyMetaLoader.js" as StrategyLoader
import "../../components/FactorWorkbench/Creation/components" as PluginComponents
import "../../components/FactorWorkbench/Creation" as FactorCreation
import "../../components/Risk" as RiskComponents
import "../../components/Factor" as FactorComponents

Rectangle {
    id: root
    
    // ============ 页面属�?============
    
    property var strategyService: null
    property var strategyBacktestController: null
    
    // 当前步骤 (1-3)
    property int currentStep: 1
    readonly property int totalSteps: 3
    
    // 策略数据
    property string selectedStrategyType: "trend_following"
    property string selectedStrategyName: getStrategyTypeName(selectedStrategyType)
    property string strategyName: ""
    property string strategyDescription: ""
    property var strategyParameters: ({})
    property var strategyTags: []
    property string assetType: "stock"
    property string timeFrame: "daily"
    property string riskLevel: "medium"  // low, medium, high
    property string optimizationMethod: "genetic"  // genetic, grid, bayesian
    property bool enableAdvancedOptions: false
    
    // 回测设置
    property int backtestYears: 3
    property string backtestStartDate: ""
    property string backtestEndDate: ""
    property bool useCustomDateRange: false
    property string benchmark: "沪深300"
    property double transactionCost: 0.0015  // 交易成本
    
    // 风险管理设置
    property double maxDrawdownLimit: 0.2  // 最大回撤限�?
    property double positionSizingMethod: 1  // 仓位管理方法 (1=固定, 2=凯利, 3=等权�?
    property double maxPositionPercent: 80  // 最大仓位百分比
    property double stopLossPercent: 10  // 止损百分�?
    property double takeProfitPercent: 20  // 止盈百分�?
    
    // 插件化组件注册表
    PluginComponents.ParamComponents {
        id: paramComponents
    }
    
    // 动态参数生成器引用（在步骤3中设置）
    property var dynamicParamGenerator: null
    
    // 元数据相�?
    property var strategySchemas: null
    property var currentSchema: null
    property bool schemasLoaded: false
    property bool parametersValid: false
    property string validationMessage: ""
    property var strategyTypesList: []
    
    // 高级选项
    property bool enableWalkForward: false  // 滚动窗口优化
    property bool enableMonteCarlo: false  // 蒙特卡洛模拟
    property int monteCarloSamples: 1000  // 蒙特卡洛样本�?
    property bool enableOutOfSample: false  // 样本外测�?
    property double outOfSampleRatio: 0.3  // 样本外比�?
    
    // 信号
    signal strategyCreated(var strategyData)
    signal backClicked()
    signal strategyTypeChanged(string strategyType)
    
    // ============ 主布局 ============
    
    color: "#0f172a"  // 深色背景
    
    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        // 隐藏滚动条
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: scrollView.width
            spacing: 10
            
            // 头部：标题和进度步骤
            ColumnLayout {
                anchors.fill: parent
                Layout.topMargin: 15
                spacing: 12
                
                // 标题
                Text {
                    text: "专业策略创建向导"
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    color: "#f1f5f9"
                }
                
                // 子标题
                Text {
                    text: "创建并优化专业级量化交易策略"
                    font.pixelSize: 16
                    color: "#94a3b8"
                    wrapMode: Text.WordWrap
                }
                
                // 高级进度步骤指示器
                RowLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill:parent
                    Layout.topMargin: 10
                    spacing: 10
                    
                    Repeater {
                        model: totalSteps
                        delegate: ColumnLayout {
                            spacing: 5
                            
                            // 步骤圆圈
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36
                                radius: 18
                                color: {
                                    if (index + 1 < currentStep) return "#10b981"  // 已完成
                                    if (index + 1 === currentStep) return "#3b82f6" // 当前步骤
                                    return "#334155"  // 未开始
                                }
                                border.width: index + 1 === currentStep ? 2 : 0
                                border.color: "#60a5fa"
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: index + 1
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: index + 1 <= currentStep ? "white" : "#94a3b8"
                                }
                            }
                            
                            // 步骤标签
                            Text {
                                text: getStepLabel(index + 1)
                                font.pixelSize: 12
                                font.weight: index + 1 === currentStep ? Font.Medium : Font.Normal
                                color: index + 1 <= currentStep ? "#f1f5f9" : "#64748b"
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
            
            // 主内容区
            Rectangle {
                anchors.fill:parent
                Layout.minimumHeight: 500
                radius: 12
                color: "#1e293b"
                border.width: 1
                border.color: "#334155"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 3
                    
                    // 步骤标题和描述
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        
                        Text {
                            text: getStepTitle(currentStep)
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            color: "#f1f5f9"
                        }
                        
                        Text {
                            text: getStepDescription(currentStep)
                            font.pixelSize: 16
                            color: "#94a3b8"
                            wrapMode: Text.WordWrap
                        }
                    }
                    
                    // 步骤内容区域
                    Loader {
                        id: stepContentLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: getStepComponent(currentStep)
                    }
                    
                    // 导航按钮和状态信息
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        
                    // 验证状态
                    Rectangle {
                        id: validationInfo
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        radius: 4
                        color: parametersValid ? "#10b98120" : "#ef444420"
                        border.color: parametersValid ? "#10b981" : "#ef4444"
                        border.width: 1
                        visible: validationMessage !== ""
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.centerIn: parent
                            spacing: 12
                            
                            Text {
                                text: parametersValid ? "✓" : "⚠️"
                                font.pixelSize: 16
                            }
                            
                            Text {
                                text: validationMessage
                                font.pixelSize: 12
                                color: parametersValid ? "#10b981" : "#ef4444"
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            
                            // 立即回测按钮（仅在策略创建成功时显示）
                            Button {
                                id: immediateBacktestBtn
                                text: "立即回测"
                                visible: validationMessage.indexOf("✅ 策略创建成功") !== -1
                                
                                background: Rectangle {
                                    implicitWidth: 100
                                    implicitHeight: 32
                                    radius: 6
                                    color: "#3b82f6"
                                }
                                
                                contentItem: Text {
                                    text: immediateBacktestBtn.text
                                    color: "white"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                
                                onClicked: {
                                    if (root.strategyBacktestController && root.strategyName) {
                                        console.log("立即回测策略:", root.strategyName)
                                        // 构建回测数据
                                        var backtestData = root.buildCompleteStrategyData()
                                        // 调用回测控制器
                                        root.strategyBacktestController.startBacktest(backtestData)
                                        // 显示回测开始消息
                                        root.validationMessage = "🚀 回测已启动，请查看回测页面..."
                                    } else {
                                        root.validationMessage = "⚠️ 回测控制器未初始化或策略名称为空"
                                    }
                                }
                            }
                        }
                    }
                        
                        // 按钮区域
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            
                            // 返回按钮
                            Button {
                                id: backBtn
                                text: "返回"
                                visible: currentStep > 1
                                
                                background: Rectangle {
                                    implicitWidth: 120
                                    implicitHeight: 48
                                    radius: 8
                                    color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: backBtn.text
                                    color: "#f1f5f9"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                
                                onClicked: prevStep()
                            }
                            
                        
                            
                            // 步骤4（最后一步）显示两个创建按钮
                            RowLayout {
                                spacing: 8
                                visible: currentStep === totalSteps
                                
                                // 创建按钮（仅保存）
                                Button {
                                    id: createBtn
                                    text: "创建"
                                    enabled: isStepValid(currentStep)
                                    
                                    background: Rectangle {
                                        implicitWidth: 100
                                        implicitHeight: 48
                                        radius: 8
                                        color: createBtn.enabled ? "#3b82f6" : "#334155"
                                    }
                                    
                                    contentItem: Text {
                                        text: createBtn.text
                                        color: createBtn.enabled ? "white" : "#94a3b8"
                                        font.pixelSize: 16
                                        font.weight: Font.Medium
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    
                                    onClicked: createStrategy(false)
                                }
                                
                                // 创建并开始回测按钮
                                Button {
                                    id: createAndBacktestBtn
                                    text: "创建并开始回测"
                                    enabled: isStepValid(currentStep)
                                    
                                    background: Rectangle {
                                        implicitWidth: 140
                                        implicitHeight: 48
                                        radius: 8
                                        color: createAndBacktestBtn.enabled ? "#10b981" : "#334155"
                                    }
                                    
                                    contentItem: Text {
                                        text: createAndBacktestBtn.text
                                        color: createAndBacktestBtn.enabled ? "white" : "#94a3b8"
                                        font.pixelSize: 16
                                        font.weight: Font.Medium
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    
                                    onClicked: createStrategy(true)
                                }
                            }
                            
                            // 非最后步骤的下一步按钮
                            Button {
                                id: nextBtn
                                text: "下一步"
                                enabled: isStepValid(currentStep)
                                visible: currentStep < totalSteps
                                
                                background: Rectangle {
                                    implicitWidth: 140
                                    implicitHeight: 48
                                    radius: 8
                                    color: nextBtn.enabled ? "#3b82f6" : "#334155"
                                }
                                
                                contentItem: Text {
                                    text: nextBtn.text
                                    color: nextBtn.enabled ? "white" : "#94a3b8"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                
                                onClicked: nextStep()
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 步骤组件 ============
    
    // 步骤1: 策略类型选择与基本信息（左右布局合并）
    Component {
        id: step1Component
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 16
            // 左右布局容器
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 24
                
                // 左侧：策略类型选择（1/4宽度）
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.25
                    Layout.minimumWidth: 180
                    spacing: 16
                    
                    Text {
                        text: "选择策略类型"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        color: "#f1f5f9"
                    }
                    
                    // 类型选择列表 - 使用紧凑卡片
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 200
                        Layout.preferredHeight: 300
                        Layout.maximumHeight: 350
                        clip: true
                        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        
                        ColumnLayout {
                            id: strategyListColumn
                            width: parent.width
                            spacing: 2
                            
                            // 策略类型卡片组件
                            Component {
                                id: strategyTypeCard
                                
                                Rectangle {
                                    id: cardRoot
        property string typeId: ""
        property string displayName: ""
        property string description: ""
        property bool isSelected: root.selectedStrategyType === typeId
                                    
                                    width: parent.width
                                    height: 48
                                    radius: 6
                                    color: isSelected ? "#1e40af" : "#1e293b"
                                    border.width: isSelected ? 2 : 1
                                    border.color: isSelected ? "#3b82f6" : "#475569"
                                    
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 12
                                        
                                        // 左侧图标
                                        Rectangle {
                                            Layout.preferredWidth: 28
                                            Layout.preferredHeight: 28
                                            radius: 6
                                            color: isSelected ? "#3b82f6" : "#334155"
                                            border.width: 1
                                            border.color: isSelected ? "#60a5fa" : "#475569"
                                            
                                            Text {
                                                anchors.centerIn: parent
                                                text: getStrategyIcon(cardRoot.typeId)
                                                font.pixelSize: 14
                                                color: isSelected ? "white" : "#cbd5e1"
                                            }
                                        }
                                        
                                        // 策略名称和简要描述
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            spacing: 2
                                            
                                            Text {
                                                text: cardRoot.displayName
                                                font.pixelSize: 13
                                                font.weight: isSelected ? Font.DemiBold : Font.Medium
                                                color: isSelected ? "white" : "#f1f5f9"
                                                elide: Text.ElideRight
                                            }
                                            
                                            Text {
                                                text: getBriefDescription(cardRoot.typeId)
                                                font.pixelSize: 10
                                                color: isSelected ? "#dbeafe" : "#94a3b8"
                                                elide: Text.ElideRight
                                                maximumLineCount: 1
                                            }
                                        }
                                        
                                        // 选中指示器
                                        Rectangle {
                                            visible: isSelected
                                            Layout.preferredWidth: 12
                                            Layout.preferredHeight: 12
                                            radius: 6
                                            color: "#10b981"
                                            border.width: 2
                                            border.color: "white"
                                        }
                                    }
                                    
                                    // 获取策略图标
                                    function getStrategyIcon(typeId) {
                                        switch(typeId) {
                                            case "trend_following": return "📈";
                                            case "mean_reversion": return "🔄";
                                            case "momentum": return "🚀";
                                            case "arbitrage": return "⚖️";
                                            case "machine_learning": return "🤖";
                                            case "multi_factor": return "🧩";
                                            case "high_frequency": return "⚡";
                                            case "event_driven": return "📰";
                                            case "custom": return "🛠️";
                                            default: return "📊";
                                        }
                                    }
                                    
                                    // 获取简要描述
                                    function getBriefDescription(typeId) {
                                        switch(typeId) {
                                            case "trend_following": return "跟随价格趋势交易";
                                            case "mean_reversion": return "价格偏离均值后回归";
                                            case "momentum": return "跟随强势股票动量";
                                            case "arbitrage": return "利用价差套利交易";
                                            case "machine_learning": return "AI预测价格走势";
                                            case "multi_factor": return "多维度综合评分";
                                            case "high_frequency": return "高频数据快速交易";
                                            case "event_driven": return "事件驱动交易机会";
                                            case "custom": return "用户自定义策略";
                                            default: return "策略类型";
                                        }
                                    }
                                }
                            }
                            
                                // 趋势跟踪策略
                                Loader {
                                    id: trendCard
                                    sourceComponent: strategyTypeCard
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 48
                                    onLoaded: {
                                    item.typeId = "trend_following"
                                    item.displayName = "趋势跟踪策略"
                                    item.description = "跟随价格趋势交易"
                                }
                            
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "trend_following") {
                                            // 如果已经选中，取消选择
                                            root.selectedStrategyType = ""
                                        } else {
                                            // 否则选择该类型
                                            root.selectedStrategyType = "trend_following"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                            
                            // 均值回归策略
                            Loader {
                                id: meanReversionCard
                                sourceComponent: strategyTypeCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onLoaded: {
                                    item.typeId = "mean_reversion"
                                    item.displayName = "均值回归策略"
                                    item.description = "价格偏离均值后回归"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "mean_reversion") {
                                            root.selectedStrategyType = ""
                                        } else {
                                            root.selectedStrategyType = "mean_reversion"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                            
                            // 动量策略
                            Loader {
                                id: momentumCard
                                sourceComponent: strategyTypeCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onLoaded: {
                                    item.typeId = "momentum"
                                    item.displayName = "动量策略"
                                    item.description = "跟随强势股票动量"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "momentum") {
                                            root.selectedStrategyType = ""
                                        } else {
                                            root.selectedStrategyType = "momentum"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                            
                            // 套利策略
                            Loader {
                                id: arbitrageCard
                                sourceComponent: strategyTypeCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onLoaded: {
                                    item.typeId = "arbitrage"
                                    item.displayName = "套利策略"
                                    item.description = "利用价差套利交易"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "arbitrage") {
                                            root.selectedStrategyType = ""
                                        } else {
                                            root.selectedStrategyType = "arbitrage"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                            
                            // 机器学习策略
                            Loader {
                                id: mlCard
                                sourceComponent: strategyTypeCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onLoaded: {
                                    item.typeId = "machine_learning"
                                    item.displayName = "机器学习策略"
                                    item.description = "AI预测价格走势"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "machine_learning") {
                                            root.selectedStrategyType = ""
                                        } else {
                                            root.selectedStrategyType = "machine_learning"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                            
                            // 多因子策略
                            Loader {
                                id: multiFactorCard
                                sourceComponent: strategyTypeCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onLoaded: {
                                    item.typeId = "multi_factor"
                                    item.displayName = "多因子策略"
                                    item.description = "多维度综合评分"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "multi_factor") {
                                            root.selectedStrategyType = ""
                                        } else {
                                            root.selectedStrategyType = "multi_factor"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                            
                            // 高频策略
                            Loader {
                                id: hfCard
                                sourceComponent: strategyTypeCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onLoaded: {
                                    item.typeId = "high_frequency"
                                    item.displayName = "高频策略"
                                    item.description = "高频数据快速交易"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "high_frequency") {
                                            root.selectedStrategyType = ""
                                        } else {
                                            root.selectedStrategyType = "high_frequency"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                            
                            // 事件驱动策略
                            Loader {
                                id: eventCard
                                sourceComponent: strategyTypeCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onLoaded: {
                                    item.typeId = "event_driven"
                                    item.displayName = "事件驱动策略"
                                    item.description = "事件驱动交易机会"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "event_driven") {
                                            root.selectedStrategyType = ""
                                        } else {
                                            root.selectedStrategyType = "event_driven"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                            
                            // 自定义策略
                            Loader {
                                id: customCard
                                sourceComponent: strategyTypeCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onLoaded: {
                                    item.typeId = "custom"
                                    item.displayName = "自定义策略"
                                    item.description = "用户自定义策略"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.selectedStrategyType === "custom") {
                                            root.selectedStrategyType = ""
                                        } else {
                                            root.selectedStrategyType = "custom"
                                        }
                                        root.strategyTypeChanged(root.selectedStrategyType)
                                    }
                                }
                            }
                        }
                    }
                    
                    // 策略类型描述
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 100
                        Layout.minimumHeight: 80
                        radius: 8
                        color: "#0f172a"
                        border.width: 1
                        border.color: "#334155"
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6
                            
                            // Text {
                            //     text: "策略类型描述"
                            //     font.pixelSize: 13
                            //     font.weight: Font.Medium
                            //     color: "#f1f5f9"
                            // }
                            
                            Text {
                                id: strategyTypeDesc
                                text: getStrategyTypeDescription(root.selectedStrategyType)
                                font.pixelSize: 12
                                color: "#94a3b8"
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                            }
                        }
                    }
                }
                
                // 右侧：策略基本信息（3/4宽度） - 顶部对齐，占满剩余空间
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.75
                    Layout.minimumWidth: 600
                    spacing: 5
                    Layout.alignment: Qt.AlignTop
                    
                    Text {
                        text: "策略基本信息"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        color: "#f1f5f9"
                        Layout.fillWidth: true
                    }
                    
                    // 策略名称
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        
                        Text {
                            text: "策略名称 *"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "#f1f5f9"
                        }
                        
                        TextField {
                            id: strategyNameField
                            Layout.fillWidth: true
                            placeholderText: "请输入策略名称（如：双均线趋势跟踪策略）"
                            text: root.strategyName
                            onTextChanged: root.strategyName = text
                            
                            background: Rectangle {
                                implicitHeight: 42
                                radius: 6
                                color: "#0f172a"
                                border.width: 1
                                border.color: "#334155"
                            }
                            
                            color: "#f1f5f9"
                            font.pixelSize: 14
                            padding: 10
                        }
                    }
                    
                    // 策略描述
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        
                        Text {
                            text: "策略描述 *"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "#f1f5f9"
                        }
                        
                        TextArea {
                            id: strategyDescField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            placeholderText: "详细描述策略的核心逻辑、入场条件、出场条件等..."
                            text: root.strategyDescription
                            onTextChanged: root.strategyDescription = text
                            wrapMode: Text.WordWrap
                            
                            background: Rectangle {
                                radius: 6
                                color: "#0f172a"
                                border.width: 1
                                border.color: "#334155"
                            }
                            
                            color: "#f1f5f9"
                            font.pixelSize: 14
                            padding: 10
                        }
                    }
                    
                    // 基本属性网格
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 12
                        
                        // 资产类型
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            
                            Text {
                                text: "资产类型"
                                font.pixelSize: 13
                                color: "#cbd5e1"
                            }
                            
                            ComboBox {
                                id: assetTypeCombo
                                Layout.fillWidth: true
                                model: ["股票", "期货", "加密货币", "外汇", "期权", "ETF", "债券", "商品"]
                                currentIndex: 0
                                onActivated: root.assetType = ["stock", "futures", "crypto", "forex", "options", "etf", "bond", "commodity"][currentIndex]
                                
                                background: Rectangle {
                                    implicitHeight: 36
                                    radius: 6
                                    color: "#0f172a"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: assetTypeCombo.displayText
                                    color: "#f1f5f9"
                                    font.pixelSize: 13
                                    padding: 8
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        // 时间框架
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            
                            Text {
                                text: "时间框架"
                                font.pixelSize: 13
                                color: "#cbd5e1"
                            }
                            
                            ComboBox {
                                id: timeFrameCombo
                                Layout.fillWidth: true
                                model: ["高频(1分钟)", "日内(5分钟)", "短期(15分钟)", "中期(1小时)", "长期(日线)", "超长期(周线)"]
                                currentIndex: 4
                                onActivated: root.timeFrame = ["1min", "5min", "15min", "1hour", "daily", "weekly"][currentIndex]
                                
                                background: Rectangle {
                                    implicitHeight: 36
                                    radius: 6
                                    color: "#0f172a"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: timeFrameCombo.displayText
                                    color: "#f1f5f9"
                                    font.pixelSize: 13
                                    padding: 8
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        // 风险等级
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            
                            Text {
                                text: "风险等级"
                                font.pixelSize: 13
                                color: "#cbd5e1"
                            }
                            
                            ComboBox {
                                id: riskLevelCombo
                                Layout.fillWidth: true
                                model: ["保守型", "稳健型", "进取型", "激进型"]
                                currentIndex: 1
                                onActivated: root.riskLevel = ["low", "medium", "high", "aggressive"][currentIndex]
                                
                                background: Rectangle {
                                    implicitHeight: 36
                                    radius: 6
                                    color: "#0f172a"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: riskLevelCombo.displayText
                                    color: "#f1f5f9"
                                    font.pixelSize: 13
                                    padding: 8
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        // 优化方法
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            
                            Text {
                                text: "优化方法"
                                font.pixelSize: 13
                                color: "#cbd5e1"
                            }
                            
                            ComboBox {
                                id: optimizationCombo
                                Layout.fillWidth: true
                                model: ["遗传算法", "网格搜索", "贝叶斯优化", "随机搜索"]
                                currentIndex: 0
                                onActivated: root.optimizationMethod = ["genetic", "grid", "bayesian", "random"][currentIndex]
                                
                                background: Rectangle {
                                    implicitHeight: 36
                                    radius: 6
                                    color: "#0f172a"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: optimizationCombo.displayText
                                    color: "#f1f5f9"
                                    font.pixelSize: 13
                                    padding: 8
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                    
                    // 标签输入
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        
                        Text {
                            text: "策略标签"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "#f1f5f9"
                        }
                        
                        TextField {
                            id: tagsField
                            Layout.fillWidth: true
                            placeholderText: "输入标签，用逗号分隔（如：趋势跟踪，技术分析，A股）"
                            onEditingFinished: {
                                var tags = tagsField.text.split(',').map(function(tag) {
                                    return tag.trim();
                                }).filter(function(tag) {
                                    return tag.length > 0;
                                });
                                root.strategyTags = tags;
                            }
                            
                            background: Rectangle {
                                implicitHeight: 42
                                radius: 6
                                color: "#0f172a"
                                border.width: 1
                                border.color: "#334155"
                            }
                            
                            color: "#f1f5f9"
                            font.pixelSize: 14
                            padding: 10
                        }
                        
                        // 标签预览
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            
                            Repeater {
                                model: root.strategyTags
                                
                                delegate: Rectangle {
                                    height: 28
                                    width: Math.min(100, tagText.width + 16)
                                    radius: 14
                                    color: Qt.rgba(59/255, 130/255, 246/255, 0.1)
                                    border.width: 1
                                    border.color: "#3b82f6"
                                    
                                    Text {
                                        id: tagText
                                        anchors.centerIn: parent
                                        text: modelData
                                        font.pixelSize: 11
                                        color: "#60a5fa"
                                        padding: 4
                                    }
                                }
                            }
                        }
                    }
                    
                    // 占位Item，确保布局顶部对齐后下方有空间
                    Item {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
    
    Component {
        id: step3Component
        
        ScrollView {
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth  // 确保内容宽度自适应
            
            // 隐藏滚动条
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            
            ColumnLayout {
                anchors.fill: parent      // ✅ 填满整个 ScrollView
                spacing: 16               // 减小间距，更紧凑
                anchors.margins: 12       // 整体边距
                
                // 参数配置标题
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6            // 减小间距
                    
                    Text {
                        text: "策略参数配置"
                        font.pixelSize: 18  // 从22减小到18
                        font.weight: Font.DemiBold
                        color: "#f1f5f9"
                    }
                    
                    Text {
                        text: "配置策略的核心参数和算法设置"
                        font.pixelSize: 13  // 从16减小到13
                        color: "#94a3b8"
                        wrapMode: Text.WordWrap
                    }
                }
                
                // 动态参数生成器
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 360  // 从400减小到360
                    radius: 10                 // 减小圆角
                    color: "#0f172a"
                    border.width: 1
                    border.color: "#334155"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12    // 减小边距
                        spacing: 12            // 减小间距
                        
                        Text {
                            text: "参数配置面板"
                            font.pixelSize: 16  // 从18减小到16
                            font.weight: Font.Medium
                            color: "#f1f5f9"
                        }
                        
                        // 使用现有的动态参数生成器
                        PluginComponents.DynamicParamGenerator {
                            id: dynamicGenerator
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            itemSpacing: 10      // 减小间距
                            
                            paramRegistry: paramComponents
                            
                            onParamsChanged: function(newValues) {
                                root.strategyParameters = newValues
                                root.updateValidationState()
                            }
                            
                            onValidationChanged: function(allValid, errors) {
                                root.parametersValid = allValid
                                root.validationMessage = allValid ? "参数验证通过" : 
                                    "存在验证错误: " + Object.keys(errors).length + "项错误"
                            }
                            
                            Component.onCompleted: {
                                console.log("动态参数生成器初始化完成")
                                loadParamConfigs()
                            }
                            
                            function loadParamConfigs() {
                                console.log("加载参数配置，策略类型:", root.selectedStrategyType)
                                var paramConfigs = buildParamConfigs(root.selectedStrategyType)
                                console.log("参数配置数量:", paramConfigs.length)
                                reloadConfigs(paramConfigs, [])
                            }
                            
                            function buildParamConfigs(strategyType) {
                                var configs = []
                                
                                // ============ 通用回测参数 ============
                                configs.push({
                                    id: "initialCapital",
                                    type: "slider",
                                    label: "初始资金",
                                    description: "回测起始资金金额",
                                    default: 1000000,
                                    min: 10000,
                                    max: 10000000,
                                    step: 10000,
                                    unit: "元",
                                    category: "通用参数"
                                })
                                
                                configs.push({
                                    id: "commission",
                                    type: "slider",
                                    label: "手续费率",
                                    description: "单边交易手续费率",
                                    default: 0.0003,
                                    min: 0,
                                    max: 0.001,
                                    step: 0.0001,
                                    unit: "%",
                                    category: "通用参数"
                                })
                                
                                configs.push({
                                    id: "slippage",
                                    type: "slider",
                                    label: "滑点成本",
                                    description: "预期成交价格与报价的差距",
                                    default: 0.001,
                                    min: 0,
                                    max: 0.01,
                                    step: 0.0001,
                                    unit: "%",
                                    category: "通用参数"
                                })
                                
                                configs.push({
                                    id: "maxPosition",
                                    type: "slider",
                                    label: "最大持仓",
                                    description: "最大持仓占资金比例",
                                    default: 80,
                                    min: 10,
                                    max: 100,
                                    step: 5,
                                    unit: "%",
                                    category: "通用参数"
                                })
                                
                                configs.push({
                                    id: "orderType",
                                    type: "select",
                                    label: "订单类型",
                                    description: "策略使用的订单类型",
                                    options: ["限价单", "市价单"],
                                    default: "限价单",
                                    category: "通用参数"
                                })
                                
                                // ============ 策略特定参数 ============
                                if (strategyType === "trend_following") {
                                    configs.push({
                                        id: "fastPeriod",
                                        type: "slider",
                                        label: "快线周期",
                                        description: "短期移动平均线周期",
                                        default: 5,
                                        min: 2,
                                        max: 50,
                                        step: 1,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "slowPeriod",
                                        type: "slider",
                                        label: "慢线周期",
                                        description: "长期移动平均线周期",
                                        default: 20,
                                        min: 5,
                                        max: 200,
                                        step: 5,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "stopLoss",
                                        type: "slider",
                                        label: "止损比例",
                                        description: "止损触发比例",
                                        default: 5,
                                        min: 1,
                                        max: 20,
                                        step: 0.5,
                                        unit: "%",
                                        category: "核心参数"
                                    })
                                } else if (strategyType === "mean_reversion") {
                                    configs.push({
                                        id: "lookbackPeriod",
                                        type: "slider",
                                        label: "回顾周期",
                                        description: "计算均值和标准差的回顾周期",
                                        default: 20,
                                        min: 5,
                                        max: 100,
                                        step: 1,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "entryThreshold",
                                        type: "slider",
                                        label: "入场阈值",
                                        description: "价格偏离均值多少标准差时入场",
                                        default: 2.0,
                                        min: 1.0,
                                        max: 4.0,
                                        step: 0.1,
                                        unit: "",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "exitThreshold",
                                        type: "slider",
                                        label: "出场阈值",
                                        description: "价格回归到均值多少标准差时出场",
                                        default: 0.5,
                                        min: 0.1,
                                        max: 1.5,
                                        step: 0.1,
                                        unit: "",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "gridLevels",
                                        type: "slider",
                                        label: "网格层数",
                                        description: "网格交易的层数设置",
                                        default: 10,
                                        min: 3,
                                        max: 20,
                                        step: 1,
                                        unit: "层",
                                        category: "核心参数"
                                    })
                                } else if (strategyType === "momentum") {
                                    configs.push({
                                        id: "momentumPeriod",
                                        type: "slider",
                                        label: "动量周期",
                                        description: "计算动量的周期",
                                        default: 20,
                                        min: 5,
                                        max: 250,
                                        step: 1,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "selectionRatio",
                                        type: "slider",
                                        label: "选股比例",
                                        description: "选择动量最强股票的百分比",
                                        default: 20,
                                        min: 5,
                                        max: 50,
                                        step: 1,
                                        unit: "%",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "rebalancingPeriod",
                                        type: "slider",
                                        label: "调仓周期",
                                        description: "重新筛选和调整仓位的周期",
                                        default: 5,
                                        min: 1,
                                        max: 30,
                                        step: 1,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                } else if (strategyType === "arbitrage") {
                                    configs.push({
                                        id: "lookbackDays",
                                        type: "slider",
                                        label: "回看天数",
                                        description: "计算协整关系和历史标准差的天数",
                                        default: 60,
                                        min: 20,
                                        max: 200,
                                        step: 1,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "entryZScore",
                                        type: "slider",
                                        label: "入场Z值",
                                        description: "价差偏离多少标准差时入场",
                                        default: 2.0,
                                        min: 1.0,
                                        max: 3.0,
                                        step: 0.1,
                                        unit: "",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "exitZScore",
                                        type: "slider",
                                        label: "出场Z值",
                                        description: "价差回归到多少标准差时出场",
                                        default: 0.5,
                                        min: 0.1,
                                        max: 1.5,
                                        step: 0.1,
                                        unit: "",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "hedgeRatio",
                                        type: "slider",
                                        label: "对冲比例",
                                        description: "配对中对冲头寸的比例",
                                        default: 1.0,
                                        min: 0.5,
                                        max: 2.0,
                                        step: 0.1,
                                        unit: "",
                                        category: "核心参数"
                                    })
                                } else if (strategyType === "machine_learning") {
                                    configs.push({
                                        id: "featureWindow",
                                        type: "slider",
                                        label: "特征窗口",
                                        description: "特征提取的时间窗口",
                                        default: 60,
                                        min: 10,
                                        max: 250,
                                        step: 1,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "predictionDays",
                                        type: "slider",
                                        label: "预测天数",
                                        description: "预测未来价格的天数",
                                        default: 1,
                                        min: 1,
                                        max: 10,
                                        step: 1,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "trainingDays",
                                        type: "slider",
                                        label: "训练天数",
                                        description: "模型训练使用的历史数据天数",
                                        default: 1000,
                                        min: 500,
                                        max: 5000,
                                        step: 100,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "confidenceThreshold",
                                        type: "slider",
                                        label: "置信阈值",
                                        description: "模型预测置信度阈值",
                                        default: 60,
                                        min: 50,
                                        max: 90,
                                        step: 1,
                                        unit: "%",
                                        category: "核心参数"
                                    })
                                } else if (strategyType === "multi_factor") {
                                    configs.push({
                                        id: "factorTypes",
                                        type: "select",
                                        label: "因子类型",
                                        description: "使用的因子类型",
                                        options: ["价值", "质量", "成长", "动量", "规模", "波动率", "流动性", "情绪"],
                                        default: ["价值", "质量", "成长", "动量"],
                                        multiple: true,
                                        category: "核心参数"
                                    })
                                    
                                    configs.push({
                                        id: "rebalancingPeriod",
                                        type: "slider",
                                        label: "调仓周期",
                                        description: "策略调仓的周期",
                                        default: 20,
                                        min: 5,
                                        max: 60,
                                        step: 5,
                                        unit: "天",
                                        category: "核心参数"
                                    })
                                } else if (strategyType === "high_frequency") {
                                    configs.push({
                                        id: "timeframe",
                                        type: "select",
                                        label: "时间框架",
                                        description: "高频交易的时间框架",
                                        options: ["1分钟", "5分钟", "15分钟", "30分钟", "1小时"],
                                        default: "5分钟",
                                        category: "核心参数"
                                    })
                                } else if (strategyType === "event_driven") {
                                    configs.push({
                                        id: "eventTypes",
                                        type: "select",
                                        label: "事件类型",
                                        description: "关注的事件类型",
                                        options: ["财报发布", "并购公告", "分红公告", "高管变动", "政策发布", "产品发布"],
                                        default: ["财报发布", "并购公告"],
                                        multiple: true,
                                        category: "核心参数"
                                    })
                                } else if (strategyType === "custom") {
                                    // 自定义策略不需要特殊参数，用户自己定义代码
                                    configs.push({
                                        id: "customCode",
                                        type: "input",
                                        label: "自定义代码",
                                        description: "请输入自定义策略代码",
                                        default: "# 自定义策略代码",
                                        multiline: true,
                                        placeholder: "在这里编写您的自定义策略代码...",
                                        category: "核心参数"
                                    })
                                }
                                
                                return configs
                            }
                        }
                        
                        // 参数统计信息
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            
                            Text {
                                text: "已配置参数: " + (dynamicGenerator ? dynamicGenerator.configsList.length : 0)
                                font.pixelSize: 12  // 从14减小到12
                                color: "#94a3b8"
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Text {
                                text: root.parametersValid ? "✓ 参数验证通过" : "⚠️ 参数需要验证"
                                font.pixelSize: 12  // 从14减小到12
                                font.weight: Font.Medium
                                color: root.parametersValid ? "#10b981" : "#ef4444"
                            }
                        }
                    }
                }
                
                // 高级参数选项
                Rectangle {
                    Layout.fillWidth: true
                    Layout.minimumHeight: root.enableAdvancedOptions ? 200 : 60  // 减小高度
                    radius: 10
                    color: "#0f172a"
                    border.width: 1
                    border.color: "#334155"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12
                        
                        // 标题和切换
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            
                            Text {
                                text: "高级参数选项"
                                font.pixelSize: 16  // 从18减小到16
                                font.weight: Font.Medium
                                color: "#f1f5f9"
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Switch {
                                id: advancedParamsSwitch
                                checked: root.enableAdvancedOptions
                                onCheckedChanged: root.enableAdvancedOptions = checked
                                
                                indicator: Rectangle {
                                    implicitWidth: 44   // 从52减小
                                    implicitHeight: 24  // 从28减小
                                    radius: 12
                                    color: parent.checked ? "#3b82f6" : "#334155"
                                    border.width: 1
                                    border.color: parent.checked ? "#3b82f6" : "#475569"
                                    
                                    Rectangle {
                                        x: parent.checked ? parent.width - width - 2 : 2
                                        y: 2
                                        width: 20   // 从24减小
                                        height: 20  // 从24减小
                                        radius: 10
                                        color: "white"
                                        Behavior on x {
                                            NumberAnimation { duration: 200 }
                                        }
                                    }
                                }
                            }
                        }
                        
                        // 高级选项内容
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            visible: root.enableAdvancedOptions
                            
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 16
                                rowSpacing: 12
                                
                                // 参数优化范围
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    
                                    Text {
                                        text: "参数优化范围"
                                        font.pixelSize: 12  // 从14减小
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["窄范围", "中等范围", "宽范围", "自定义"]
                                        currentIndex: 1
                                        
                                        background: Rectangle {
                                            implicitHeight: 36  // 从40减小
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: parent.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 12  // 从14减小
                                            padding: 8
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                                
                                // 参数敏感性分析
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    
                                    Text {
                                        text: "敏感性分析"
                                        font.pixelSize: 12
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["关闭", "基础", "详细", "全面"]
                                        currentIndex: 1
                                        
                                        background: Rectangle {
                                            implicitHeight: 36
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: parent.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 12
                                            padding: 8
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                                
                                // 参数约束
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    
                                    Text {
                                        text: "参数约束"
                                        font.pixelSize: 12
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["无约束", "简单约束", "复杂约束"]
                                        currentIndex: 0
                                        
                                        background: Rectangle {
                                            implicitHeight: 36
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: parent.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 12
                                            padding: 8
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                                
                                // 参数初始化方式
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    
                                    Text {
                                        text: "初始化方式"
                                        font.pixelSize: 12
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["随机", "网格", "经验", "上次最优"]
                                        currentIndex: 0
                                        
                                        background: Rectangle {
                                            implicitHeight: 36
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: parent.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 12
                                            padding: 8
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                            }
                            
                            // 自定义参数脚本
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Text {
                                    text: "自定义参数脚本"
                                    font.pixelSize: 12
                                    color: "#cbd5e1"
                                }
                                
                                TextArea {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 70  // 从80减小
                                    placeholderText: "可输入自定义参数配置脚本..."
                                    wrapMode: Text.WordWrap
                                    
                                    background: Rectangle {
                                        radius: 6
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#334155"
                                    }
                                    
                                    color: "#f1f5f9"
                                    font.pixelSize: 12  // 从14减小
                                    padding: 10
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 步骤4: 风险管理和回测配置 - 左右合并结构
    Component {
        id: step4Component
        
        ScrollView {
            anchors.fill: parent
            clip: true
            
            // 隐藏滚动条
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            
            ColumnLayout {
                width: parent.width
                spacing: 16
                
                // 步骤标题和策略摘要
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    // 策略摘要信息
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 80
                        radius: 8
                        color: "#0f172a"
                        border.width: 1
                        border.color: "#334155"
                        
                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            columns: 4
                            columnSpacing: 16
                            rowSpacing: 4
                            
                            Text {
                                text: "策略类型:"
                                font.pixelSize: 11
                                color: "#94a3b8"
                            }
                            
                            Text {
                                text: root.selectedStrategyName
                                font.pixelSize: 11
                                color: "#f1f5f9"
                            }
                            
                            Text {
                                text: "风险等级:"
                                font.pixelSize: 11
                                color: "#94a3b8"
                            }
                            
                            Text {
                                text: getRiskLevelName(root.riskLevel)
                                font.pixelSize: 11
                                color: getRiskLevelColor(root.riskLevel)
                            }
                            
                            Text {
                                text: "回测周期:"
                                font.pixelSize: 11
                                color: "#94a3b8"
                            }
                            
                            Text {
                                text: root.backtestYears + "年"
                                font.pixelSize: 11
                                color: "#f1f5f9"
                            }
                            
                            Text {
                                text: "参数数量:"
                                font.pixelSize: 11
                                color: "#94a3b8"
                            }
                            
                            Text {
                                text: Object.keys(root.strategyParameters).length + "个"
                                font.pixelSize: 11
                                color: "#f1f5f9"
                            }
                        }
                    }
                }
                
                // 左右布局容器 - 主要高度区域
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 16
                    
                    // 左侧：风险管理与回测设置
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: parent.width * 0.5 - 8
                        spacing: 16
                        
                        // 基础风险管理 - 占主要高度
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 260
                            radius: 10
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#334155"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12
                                
                                Text {
                                    text: "基础风险管理"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: "#f1f5f9"
                                }
                                
                                // 使用动态参数生成器
                                PluginComponents.DynamicParamGenerator {
                                    id: basicRiskGenerator
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    itemSpacing: 10
                                    
                                    paramRegistry: paramComponents
                                    
                                    onParamsChanged: function(newValues) {
                                        // 更新根组件的属性
                                        if (newValues.stopLossPercent !== undefined) root.stopLossPercent = newValues.stopLossPercent
                                        if (newValues.takeProfitPercent !== undefined) root.takeProfitPercent = newValues.takeProfitPercent
                                        if (newValues.maxDrawdownLimit !== undefined) root.maxDrawdownLimit = newValues.maxDrawdownLimit
                                        if (newValues.maxPositionPercent !== undefined) root.maxPositionPercent = newValues.maxPositionPercent
                                        root.updateValidationState()
                                    }
                                    
                                    onValidationChanged: function(allValid, errors) {
                                        root.parametersValid = allValid
                                        root.validationMessage = allValid ? "参数验证通过" : 
                                            "存在验证错误: " + Object.keys(errors).length + "项错误"
                                    }
                                    
                                    Component.onCompleted: {
                                        console.log("基础风险管理参数生成器初始化完成")
                                        loadParamConfigs()
                                    }
                                    
                                    function loadParamConfigs() {
                                        console.log("加载基础风险管理参数配置")
                                        var paramConfigs = buildBasicRiskParamConfigs()
                                        console.log("参数配置数量:", paramConfigs.length)
                                        reloadConfigs(paramConfigs, [])
                                    }
                                    
                                    function buildBasicRiskParamConfigs() {
                                        var configs = []
                                        
                                        // 止损比例
                                        configs.push({
                                            id: "stopLossPercent",
                                            type: "slider",
                                            label: "止损比例",
                                            description: "止损触发比例（百分比）",
                                            default: 10,
                                            min: 1,
                                            max: 20,
                                            step: 0.5,
                                            unit: "%",
                                            category: "基础风险管理"
                                        })
                                        
                                        // 止盈比例
                                        configs.push({
                                            id: "takeProfitPercent",
                                            type: "slider",
                                            label: "止盈比例",
                                            description: "止盈触发比例（百分比）",
                                            default: 20,
                                            min: 5,
                                            max: 30,
                                            step: 1,
                                            unit: "%",
                                            category: "基础风险管理"
                                        })
                                        
                                        // 最大回撤限制
                                        configs.push({
                                            id: "maxDrawdownLimit",
                                            type: "slider",
                                            label: "最大回撤限制",
                                            description: "最大回撤触发限制（百分比）",
                                            default: 20,
                                            min: 5,
                                            max: 30,
                                            step: 1,
                                            unit: "%",
                                            category: "基础风险管理"
                                        })
                                        
                                        // 最大仓位百分比
                                        configs.push({
                                            id: "maxPositionPercent",
                                            type: "slider",
                                            label: "最大仓位百分比",
                                            description: "最大持仓占资金比例（百分比）",
                                            default: 80,
                                            min: 20,
                                            max: 100,
                                            step: 5,
                                            unit: "%",
                                            category: "基础风险管理"
                                        })
                                        
                                        return configs
                                    }
                                }
                            }
                        }
                        
                        // 基础回测设置 - 占主要高度
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 260
                            radius: 10
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#334155"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12
                                
                                Text {
                                    text: "基础回测设置"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: "#f1f5f9"
                                }
                                
                                GridLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    columns: 2
                                    columnSpacing: 16
                                    rowSpacing: 12
                                    
                                    // 回测周期
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        
                                        Text {
                                            text: "回测周期"
                                            font.pixelSize: 12
                                            color: "#cbd5e1"
                                        }
                                        
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            
                                            Slider {
                                                id: backtestYearsSlider
                                                Layout.fillWidth: true
                                                from: 1
                                                to: 10
                                                value: root.backtestYears
                                                stepSize: 1
                                                onValueChanged: root.backtestYears = value
                                            }
                                            
                                            Text {
                                                text: root.backtestYears + "年"
                                                font.pixelSize: 12
                                                color: "#94a3b8"
                                                Layout.minimumWidth: 40
                                            }
                                        }
                                    }
                                    
                                    // 基准指数
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        
                                        Text {
                                            text: "基准指数"
                                            font.pixelSize: 12
                                            color: "#cbd5e1"
                                        }
                                        
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: ["沪深300", "上证指数", "深证成指", "创业板指", "中证500", "中证1000", "自定义"]
                                            currentIndex: 0
                                            onActivated: root.benchmark = currentText
                                            
                                            background: Rectangle {
                                                implicitHeight: 32
                                                radius: 4
                                                color: "#0f172a"
                                                border.width: 1
                                                border.color: "#334155"
                                            }
                                            
                                            contentItem: Text {
                                                text: parent.displayText
                                                color: "#f1f5f9"
                                                font.pixelSize: 12
                                                padding: 6
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }
                                    
                                    // 交易成本
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        
                                        Text {
                                            text: "交易成本"
                                            font.pixelSize: 12
                                            color: "#cbd5e1"
                                        }
                                        
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            
                                            Slider {
                                                Layout.fillWidth: true
                                                from: 0.0005
                                                to: 0.005
                                                value: root.transactionCost
                                                stepSize: 0.0001
                                                onValueChanged: root.transactionCost = value
                                            }
                                            
                                            Text {
                                                text: (root.transactionCost * 100).toFixed(2) + "%"
                                                font.pixelSize: 12
                                                color: "#94a3b8"
                                                Layout.minimumWidth: 50
                                            }
                                        }
                                    }
                                    
                                    // 是否使用自定义日期范围
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            
                                            CheckBox {
                                                id: customDateCheck
                                                checked: root.useCustomDateRange
                                                onCheckedChanged: root.useCustomDateRange = checked
                                                
                                                indicator: Rectangle {
                                                    implicitWidth: 16
                                                    implicitHeight: 16
                                                    radius: 3
                                                    border.width: 1
                                                    border.color: customDateCheck.checked ? "#3b82f6" : "#64748b"
                                                    
                                                    Rectangle {
                                                        anchors.fill: parent
                                                        anchors.margins: 3
                                                        radius: 2
                                                        color: customDateCheck.checked ? "#3b82f6" : "transparent"
                                                    }
                                                }
                                            }
                                            
                                            Text {
                                                text: "使用自定义日期范围"
                                                font.pixelSize: 12
                                                color: "#cbd5e1"
                                            }
                                        }
                                    }
                                }
                                
                                // 自定义日期范围
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    visible: root.useCustomDateRange
                                    
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        
                                        Text {
                                            text: "开始日期"
                                            font.pixelSize: 12
                                            color: "#cbd5e1"
                                        }
                                        
                                        TextField {
                                            Layout.fillWidth: true
                                            placeholderText: "YYYY-MM-DD"
                                            text: root.backtestStartDate
                                            onTextChanged: root.backtestStartDate = text
                                            
                                            background: Rectangle {
                                                implicitHeight: 32
                                                radius: 4
                                                color: "#0f172a"
                                                border.width: 1
                                                border.color: "#334155"
                                            }
                                        }
                                    }
                                    
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        
                                        Text {
                                            text: "结束日期"
                                            font.pixelSize: 12
                                            color: "#cbd5e1"
                                        }
                                        
                                        TextField {
                                            Layout.fillWidth: true
                                            placeholderText: "YYYY-MM-DD"
                                            text: root.backtestEndDate
                                            onTextChanged: root.backtestEndDate = text
                                            
                                            background: Rectangle {
                                                implicitHeight: 32
                                                radius: 4
                                                color: "#0f172a"
                                                border.width: 1
                                                border.color: "#334155"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // 右侧：仓位管理与高级选项
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: parent.width * 0.5 - 8
                        spacing: 16
                        
                        // 仓位管理与策略摘要组合区域
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#334155"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12
                                
                                // 仓位管理部分
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 180
                                    spacing: 8
                                    
                                    Text {
                                        text: "仓位管理"
                                        font.pixelSize: 16
                                        font.weight: Font.Medium
                                        color: "#f1f5f9"
                                    }
                                    
                                    // 仓位管理方法选择 - 使用动态参数生成器
                                    PluginComponents.DynamicParamGenerator {
                                        id: positionSizingGenerator
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        itemSpacing: 10
                                        
                                        paramRegistry: paramComponents
                                        
                                        onParamsChanged: function(newValues) {
                                            if (newValues.positionSizingMethod !== undefined) {
                                                root.positionSizingMethod = newValues.positionSizingMethod
                                            }
                                            root.updateValidationState()
                                        }
                                        
                                        Component.onCompleted: {
                                            console.log("仓位管理参数生成器初始化完成")
                                            loadPositionSizingConfigs()
                                        }
                                        
                                        function loadPositionSizingConfigs() {
                                            var paramConfigs = buildPositionSizingConfigs()
                                            reloadConfigs(paramConfigs, [])
                                        }
                                        
                                        function buildPositionSizingConfigs() {
                                            var configs = []
                                            
                                            // 仓位管理方法
                                            configs.push({
                                                id: "positionSizingMethod",
                                                type: "select",
                                                label: "仓位管理方法",
                                                description: "选择仓位管理方法",
                                                options: [
                                                    {value: 1, label: "固定仓位"},
                                                    {value: 2, label: "凯利公式"},
                                                    {value: 3, label: "等权重"}
                                                ],
                                                default: 1,
                                                category: "仓位管理"
                                            })
                                            
                                            return configs
                                        }
                                    }
                                    
                                    // 仓位管理说明
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 24
                                        radius: 4
                                        color: "#1e293b"
                                        
                                        Text {
                                            anchors.fill: parent
                                            anchors.margins: 4
                                            text: getPositionSizingDescription(root.positionSizingMethod)
                                            font.pixelSize: 10
                                            color: "#94a3b8"
                                            wrapMode: Text.WordWrap
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                                
                            }
                        }
                        
                        // 高级回测选项
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#334155"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12
                                
                                // 标题和切换
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    
                                    Text {
                                        text: "高级回测选项"
                                        font.pixelSize: 16
                                        font.weight: Font.Medium
                                        color: "#f1f5f9"
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    Switch {
                                        checked: root.enableAdvancedOptions
                                        onCheckedChanged: root.enableAdvancedOptions = checked
                                    }
                                }
                                
                                // 高级回测内容
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 16
                                    visible: root.enableAdvancedOptions
                                    
                                    // 滚动窗口优化
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            
                                            CheckBox {
                                                id: walkForwardCheck
                                                checked: root.enableWalkForward
                                                onCheckedChanged: root.enableWalkForward = checked
                                            }
                                            
                                            Text {
                                                text: "滚动窗口优化"
                                                font.pixelSize: 12
                                                color: "#cbd5e1"
                                                Layout.fillWidth: true
                                            }
                                        }
                                        
                                        // 滚动窗口设置
                                        ColumnLayout {
                                            spacing: 4
                                            visible: root.enableWalkForward
                                            
                                            Text {
                                                text: "窗口长度"
                                                font.pixelSize: 11
                                                color: "#94a3b8"
                                            }
                                            
                                            ComboBox {
                                                Layout.fillWidth: true
                                                model: ["1个月", "2个月", "3个月", "自定义"]
                                                currentIndex: 1
                                                
                                                background: Rectangle {
                                                    implicitHeight: 28
                                                    radius: 4
                                                    color: "#0f172a"
                                                    border.width: 1
                                                    border.color: "#334155"
                                                }
                                                
                                                contentItem: Text {
                                                    text: parent.displayText
                                                    color: "#f1f5f9"
                                                    font.pixelSize: 11
                                                    padding: 4
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                            }
                                        }
                                    }
                                    
                                    // 蒙特卡洛模拟
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            
                                            CheckBox {
                                                id: monteCarloCheck
                                                checked: root.enableMonteCarlo
                                                onCheckedChanged: root.enableMonteCarlo = checked
                                            }
                                            
                                            Text {
                                                text: "蒙特卡洛模拟"
                                                font.pixelSize: 12
                                                color: "#cbd5e1"
                                                Layout.fillWidth: true
                                            }
                                        }
                                        
                                        // 样本数设置
                                        ColumnLayout {
                                            spacing: 4
                                            visible: root.enableMonteCarlo
                                            
                                            Text {
                                                text: "样本数"
                                                font.pixelSize: 11
                                                color: "#94a3b8"
                                            }
                                            
                                            RowLayout {
                                                spacing: 8
                                                
                                                Slider {
                                                    Layout.fillWidth: true
                                                    from: 100
                                                    to: 10000
                                                    value: root.monteCarloSamples
                                                    stepSize: 100
                                                    onValueChanged: root.monteCarloSamples = value
                                                }
                                                
                                                Text {
                                                    text: root.monteCarloSamples
                                                    font.pixelSize: 11
                                                    color: "#94a3b8"
                                                    Layout.minimumWidth: 40
                                                }
                                            }
                                        }
                                    }
                                    
                                    // 样本外测试
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            
                                            CheckBox {
                                                id: outOfSampleCheck
                                                checked: root.enableOutOfSample
                                                onCheckedChanged: root.enableOutOfSample = checked
                                            }
                                            
                                            Text {
                                                text: "样本外测试"
                                                font.pixelSize: 12
                                                color: "#cbd5e1"
                                                Layout.fillWidth: true
                                            }
                                        }
                                        
                                        // 样本外比例
                                        ColumnLayout {
                                            spacing: 4
                                            visible: root.enableOutOfSample
                                            
                                            Text {
                                                text: "样本外比例"
                                                font.pixelSize: 11
                                                color: "#94a3b8"
                                            }
                                            
                                            RowLayout {
                                                spacing: 8
                                                
                                                Slider {
                                                    Layout.fillWidth: true
                                                    from: 0.1
                                                    to: 0.5
                                                    value: root.outOfSampleRatio
                                                    stepSize: 0.05
                                                    onValueChanged: root.outOfSampleRatio = value
                                                }
                                                
                                                Text {
                                                    text: (root.outOfSampleRatio * 100).toFixed(0) + "%"
                                                    font.pixelSize: 11
                                                    color: "#94a3b8"
                                                    Layout.minimumWidth: 40
                                                }
                                            }
                                        }
                                    }
                                    
                                    // 高级回测说明
                                    Text {
                                        text: "高级回测选项提供更严格的策略验证，包括滚动窗口优化、蒙特卡洛模拟和样本外测试，确保策略的稳健性和泛化能力"
                                        font.pixelSize: 11
                                        color: "#64748b"
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // ============ 功能函数 ============
    
    // 获取步骤标签
    function getStepLabel(step) {
        var labels = ["选择类型与基本信息", "参数配置", "风险与回测"]
        return labels[step - 1] || ""
    }
    
    // 获取步骤标题
    function getStepTitle(step) {
        var titles = [
            "选择策略类型与配置基本信息",
            "参数配置与优化",
            "风险管理与回测配置"
        ]
        return titles[step - 1] || ""
    }
    
    // 获取步骤描述
    function getStepDescription(step) {
        var descriptions = [
            "选择适合您的交易风格的策略类型并填写策略基本信息",
            "配置策略的核心参数和算法设置，支持动态参数生成和优化",
            "配置策略的风险控制规则和回测环境设置，确保策略的稳健性"
        ]
        return descriptions[step - 1] || ""
    }
    
    // 获取步骤组件
    function getStepComponent(step) {
        switch(step) {
            case 1: return step1Component
            case 2: return step3Component
            case 3: return step4Component
            default: return step1Component
        }
    }
    
    // 检查步骤是否有?
    function isStepValid(step) {
        switch(step) {
            case 1: return root.selectedStrategyType !== "" && 
                          root.strategyName.trim() !== "" && 
                          root.strategyDescription.trim() !== ""
            case 2: return root.parametersValid && 
                          Object.keys(root.strategyParameters).length > 0
            case 3: return true  // 风险管理与回测步骤总是有效?
            default: return false
        }
    }
    
    // 获取策略类型名称
    function getStrategyTypeName(typeId) {
        var typeNames = {
            "trend_following": "趋势跟踪策略",
            "mean_reversion": "均值回归策略",
            "momentum": "动量策略",
            "arbitrage": "套利策略",
            "machine_learning": "机器学习策略",
            "multi_factor": "多因子策略",
            "high_frequency": "高频策略",
            "event_driven": "事件驱动策略",
            "custom": "自定义策略"
        }
        return typeNames[typeId] || typeId
    }
    
    // 获取策略类型描述
    function getStrategyTypeDescription(typeId) {
        var descriptions = {
            "trend_following": "基于价格趋势判断的交易策略，在上升趋势中买入，下降趋势中卖出。适合趋势明显的市场环境",
            "mean_reversion": "基于价格偏离均值后回归的交易策略，在价格过低时买入，过高时卖出。适合震荡市场",
            "momentum": "基于价格动量的交易策略，跟随强势股票上涨，避开弱势股票。适合有明显趋势的市场",
            "arbitrage": "基于价格差异的套利交易，利用不同市场或品种间的价差获利。风险相对较低",
            "machine_learning": "基于机器学习模型预测的交易策略，使用算法识别市场模式和预测价格走势",
            "multi_factor": "基于多个因子综合评分的交易策略，综合考虑多个维度选择股票",
            "high_frequency": "基于高频数据的交易策略，要求低延迟和快速执行。适合机构投资者",
            "event_driven": "基于特定事件（如财报发布、并购公告）的交易策略，利用事件对价格的影响获利",
            "custom": "用户自定义代码的交易策略，灵活支持各种复杂逻辑和算法"
        }
        return descriptions[typeId] || "暂无描述"
    }
    
    // 获取风险等级名称
    function getRiskLevelName(level) {
        var names = {
            "low": "保守型",
            "medium": "稳健型",
            "high": "进取型",
            "aggressive": "激进型"
        }
        return names[level] || level
    }
    
    // 获取风险等级颜色
    function getRiskLevelColor(level) {
        var colors = {
            "low": "#10b981",
            "medium": "#3b82f6",
            "high": "#f59e0b",
            "aggressive": "#ef4444"
        }
        return colors[level] || "#94a3b8"
    }
    
    // 获取仓位管理方法描述
    function getPositionSizingDescription(method) {
        var descriptions = {
            1: "固定仓位：每次交易使用固定的资金比例，简单易用但不够灵活",
            2: "凯利公式：基于胜率和盈亏比计算最优仓位，理论最优但风险较高",
            3: "等权重：投资组合中每个标的权重相等，分散风险但可能不够高效",
            4: "风险平价：根据标的风险水平调整权重，追求风险均衡但计算较复杂",
            5: "动态调整：根据市场状况和策略表现动态调整仓位，灵活但需要持续监控和调整"
        }
        return descriptions[method] || ""
    }
    
    // 上一�?
    function prevStep() {
        if (currentStep > 1) {
            currentStep--
            updateValidationState()
        }
    }
    
    // 下一�?
    function nextStep() {
        if (currentStep < totalSteps) {
            if (validateCurrentStep()) {
                currentStep++
                updateValidationState()
            }
        } else {
            createStrategy()
        }
    }
    
    // 验证当前步骤
    function validateCurrentStep() {
        switch(currentStep) {
            case 1:
                if (!root.selectedStrategyType || root.selectedStrategyType === "") {
                    root.validationMessage = "请选择策略类型"
                    return false
                }
                break
                
            case 2:
                if (!root.strategyName || root.strategyName.trim() === "") {
                    root.validationMessage = "请输入策略名称"
                    return false
                }
                if (!root.strategyDescription || root.strategyDescription.trim() === "") {
                    root.validationMessage = "请输入策略描述"
                    return false
                }
                break
                
            case 3:
                if (!root.parametersValid || Object.keys(root.strategyParameters).length === 0) {
                    root.validationMessage = "请配置有效的策略参数"
                    return false
                }
                break
        }
        
        root.validationMessage = "验证通过"
        return true
    }
    
    // 更新验证状�?
    function updateValidationState() {
        // 检查当前步骤的有效�?
        var isValid = isStepValid(currentStep)
        root.parametersValid = isValid
        
        if (isValid) {
            root.validationMessage = "✓ 当前步骤验证通过"
        } else {
            root.validationMessage = "⚠️ 请完成当前步骤的必填项"
        }
    }
    
    // 创建策略
    function createStrategy(startBacktest) {
        // 验证所有步?
        for (var i = 1; i <= totalSteps; i++) {
            if (!isStepValid(i)) {
                currentStep = i
                updateValidationState()
                return
            }
        }
        
        // 构建策略数据
        var strategyData = buildCompleteStrategyData()
        
        console.log("创建专业策略:", strategyData, "startBacktest:", startBacktest)
        
        // 显示成功消息
        if (startBacktest) {
            root.validationMessage = "✅ 策略创建成功！正在启动回测..."
        } else {
            root.validationMessage = "✅ 策略创建成功！正在保存..."
        }
        root.parametersValid = true
        
        // 触发创建信号
        root.strategyCreated(strategyData)
        
        // 如果需要开始回测，调用回测控制器
        if (startBacktest && root.strategyBacktestController) {
            console.log("开始策略回测:", root.strategyName)
            root.strategyBacktestController.startBacktest(strategyData)
            root.validationMessage = "🚀 策略回测已启动，请查看回测页面..."
        }
        
        // 重置表单
        resetForm()
    }
    
    // 构建完整的策略数�?
    function buildCompleteStrategyData() {
        var currentDate = new Date()
        var dateStr = currentDate.toISOString().split('T')[0]
        
        var strategyData = {
            // 基本信息
            name: root.strategyName,
            displayName: root.strategyName,
            strategyType: root.selectedStrategyType,
            typeName: root.selectedStrategyName,
            description: root.strategyDescription,
            
            // 基本属�?
            assetType: root.assetType,
            timeFrame: root.timeFrame,
            riskLevel: root.riskLevel,
            optimizationMethod: root.optimizationMethod,
            
            // 回测设置
            backtestYears: root.backtestYears,
            backtestStartDate: root.backtestStartDate,
            backtestEndDate: root.backtestEndDate,
            benchmark: root.benchmark,
            transactionCost: root.transactionCost,
            
            // 风险管理
            maxDrawdownLimit: root.maxDrawdownLimit,
            positionSizingMethod: root.positionSizingMethod,
            maxPositionPercent: root.maxPositionPercent,
            stopLossPercent: root.stopLossPercent,
            takeProfitPercent: root.takeProfitPercent,
            
            // 高级选项
            enableAdvancedOptions: root.enableAdvancedOptions,
            enableWalkForward: root.enableWalkForward,
            enableMonteCarlo: root.enableMonteCarlo,
            monteCarloSamples: root.monteCarloSamples,
            enableOutOfSample: root.enableOutOfSample,
            outOfSampleRatio: root.outOfSampleRatio,
            
            // 元数�?
            status: "stopped",
            createdDate: dateStr,
            returns: "+0.0%",
            maxDrawdown: "-0.0%",
            sharpeRatio: "0.0",
            winRate: "0.0%",
            tags: root.strategyTags,
            
            // 参数数据
            parameters: root.strategyParameters,
            parameterCount: Object.keys(root.strategyParameters).length
        }
        
        return strategyData
    }
    
    // 重置表单
    function resetForm() {
        root.strategyName = ""
        root.strategyDescription = ""
        root.selectedStrategyType = "trend_following"
        root.strategyTags = []
        root.assetType = "stock"
        root.timeFrame = "daily"
        root.riskLevel = "medium"
        root.optimizationMethod = "genetic"
        root.backtestYears = 3
        root.backtestStartDate = ""
        root.backtestEndDate = ""
        root.benchmark = "沪深300"
        root.transactionCost = 0.0015
        root.maxDrawdownLimit = 0.2
        root.positionSizingMethod = 1
        root.maxPositionPercent = 80
        root.stopLossPercent = 10
        root.takeProfitPercent = 20
        root.enableAdvancedOptions = false
        root.enableWalkForward = false
        root.enableMonteCarlo = false
        root.monteCarloSamples = 1000
        root.enableOutOfSample = false
        root.outOfSampleRatio = 0.3
        root.strategyParameters = {}
        root.currentStep = 1
        root.parametersValid = false
        root.validationMessage = ""
    }
    
    // ============ 初始�?============
    
    Component.onCompleted: {
        console.log("专业策略创建页面初始化完成")
        
        // 注册参数组件
        paramComponents.registerAllComponents()
        
        // 设置默认?
        resetForm()
        
        // 更新验证状?
        updateValidationState()
        
        // 初始加载完成后，确保卡片选中状态正确
        // 不再需要手动更新，因为isSelected属性是动态绑定的
    }
    
    // ============ 属性变化监�?============
    
    onSelectedStrategyTypeChanged: {
        console.log("策略类型变化:", selectedStrategyType)
        updateValidationState()
        
        // 如果当前在步骤3（参数配置），重新加载参数
        if (currentStep === 3) {
            reloadStrategyParameters()
        }
    }
    
    onStrategyNameChanged: {
        updateValidationState()
    }
    
    onStrategyDescriptionChanged: {
        updateValidationState()
    }
    
    onStrategyParametersChanged: {
        console.log("参数变化:", Object.keys(strategyParameters).length, "个参数")
        updateValidationState()
    }
}

// StrategyCreationPagePro.qml
// 专业策略创建页面 - 优化重构版本
// 使用组件化架构，支持多语言、表单校验和步骤切换动画

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import AStock.Bridge 1.0  // 导入C++桥接模块
import "../../utils/StrategyCreationUtils.js" as Utils
import "../../components/Strategy/Creation" as StrategyComponents

Page {
    id: root
    
    // ============ 页面属性 ============
    
    property alias currentStep: stepIndicator.currentStep
    property bool isCreating: false
    property string creationStatus: ""
    
    // 信号
    signal backClicked()
    signal requestBacktest(string strategyId, string strategyName)
    
    // 数据容器
    property string selectedStrategyType: "trend_following"
    property string strategyName: ""
    property string strategyDescription: ""
    property string assetType: "stock"
    property string timeFrame: "daily"
    property string riskLevel: "medium"
    property string optimizationMethod: "genetic"
    property var strategyTags: []
    
    property var strategyParameters: ({})
    property bool parametersValid: false
    property bool enableAdvancedOptions: false
    
    property int backtestYears: 3
    property string benchmark: Utils.StrategyCreationUtils.tr('strategyCreation.defaultBenchmark')
    property double transactionCost: 0.0015
    property double maxDrawdownLimit: 0.2
    property int positionSizingMethod: 1
    property double maxPositionPercent: 80
    property double stopLossPercent: 10
    property double takeProfitPercent: 20
    property double slippageCost: 0.001
    
    property bool enableWalkForward: false
    property bool enableMonteCarlo: false
    property int monteCarloSamples: 1000
    property bool enableOutOfSample: false
    property double outOfSampleRatio: 0.3
    
    // C++服务引用
    property var strategyService: StrategyService
    
    // ============ 主布局 ============
    
    background: Rectangle {
        color: "#0f172a"
    }
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        
        // 步骤内容区域
        Rectangle {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            
            StackLayout {
                id: stepStack
                anchors.fill: parent
                anchors.margins: 20
                currentIndex: stepIndicator.currentStep - 1
                
                // 步骤1: 策略类型与基本信息
                Rectangle {
                    id: step1Content
                    color: "transparent"
                    
                    RowLayout {
                        anchors.fill: parent
                        spacing: 20
                        
                        // 左侧: 策略类型选择
                        StrategyComponents.StrategyTypeSelector {
                            id: strategyTypeSelector
                            Layout.fillHeight: true
                            Layout.preferredWidth: parent.width * 0.25
                            
                            onStrategyTypeChanged: function(strategyType) {
                                root.selectedStrategyType = strategyType
                            }
                        }
                        
                        // 右侧: 策略基本信息
                        StrategyComponents.StrategyBasicInfo {
                            id: strategyBasicInfo
                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            
                            onValidationChanged: function(isValid) {
                                step1Valid = isValid
                            }
                        }
                    }
                }
                
                // 步骤2: 参数配置
                StrategyComponents.StrategyParamConfig {
                    id: step2Content
                    selectedStrategyType: root.selectedStrategyType
                    
                    onParametersChanged: function(newParameters) {
                        root.strategyParameters = newParameters
                    }
                    
                    onValidationChanged: function(allValid, errors) {
                        step2Valid = allValid
                    }
                    
                    onAdvancedOptionsChanged: function(enabled) {
                        root.enableAdvancedOptions = enabled
                    }
                }
                
                // 步骤3: 风险管理与回测
                StrategyComponents.StrategyRiskConfig {
                    id: step3Content
                    
                    stopLossPercent: root.stopLossPercent
                    takeProfitPercent: root.takeProfitPercent
                    maxDrawdownLimit: root.maxDrawdownLimit
                    maxPositionPercent: root.maxPositionPercent
                    positionSizingMethod: root.positionSizingMethod
                    backtestYears: root.backtestYears
                    benchmark: root.benchmark
                    transactionCost: root.transactionCost
                    slippageCost: root.slippageCost
                    enableAdvancedOptions: root.enableAdvancedOptions
                    enableWalkForward: root.enableWalkForward
                    enableMonteCarlo: root.enableMonteCarlo
                    monteCarloSamples: root.monteCarloSamples
                    enableOutOfSample: root.enableOutOfSample
                    outOfSampleRatio: root.outOfSampleRatio
                    
                    Component.onCompleted: {
                        // 设置摘要信息
                        step3Content.setSummaryInfo(
                            root.selectedStrategyType,
                            root.strategyName,
                            root.riskLevel,
                            Object.keys(root.strategyParameters).length
                        )
                    }
                    
                    onStopLossPercentChanged: function(value) { 
                        if (value !== undefined && value !== null) root.stopLossPercent = value 
                    }
                    onTakeProfitPercentChanged: function(value) { 
                        if (value !== undefined && value !== null) root.takeProfitPercent = value 
                    }
                    onMaxDrawdownLimitChanged: function(value) { 
                        if (value !== undefined && value !== null) root.maxDrawdownLimit = value 
                    }
                    onMaxPositionPercentChanged: function(value) { 
                        if (value !== undefined && value !== null) root.maxPositionPercent = value 
                    }
                    onPositionSizingMethodChanged: function(value) { 
                        if (value !== undefined && value !== null) root.positionSizingMethod = value 
                    }
                    onBacktestYearsChanged: function(value) { 
                        if (value !== undefined && value !== null) root.backtestYears = value 
                    }
                    onBenchmarkChanged: function(value) { 
                        if (value !== undefined && value !== null) root.benchmark = value 
                    }
                    onTransactionCostChanged: function(value) { 
                        if (value !== undefined && value !== null) root.transactionCost = value 
                    }
                    onSlippageCostChanged: function(value) { 
                        if (value !== undefined && value !== null) root.slippageCost = value 
                    }
                    onEnableAdvancedOptionsChanged: function(enabled) {
                        if (enabled !== undefined && enabled !== null) {
                            root.enableAdvancedOptions = enabled 
                            // 通知第二步同步状态
                            if (step2Content) {
                                step2Content.enableAdvancedOptions = enabled
                            }
                        }
                    }
                    onEnableWalkForwardChanged: function(enabled) { 
                        if (enabled !== undefined && enabled !== null) root.enableWalkForward = enabled 
                    }
                    onEnableMonteCarloChanged: function(enabled) { 
                        if (enabled !== undefined && enabled !== null) root.enableMonteCarlo = enabled 
                    }
                    onMonteCarloSamplesChanged: function(value) { 
                        if (value !== undefined && value !== null) root.monteCarloSamples = value 
                    }
                    onEnableOutOfSampleChanged: function(enabled) { 
                        if (enabled !== undefined && enabled !== null) root.enableOutOfSample = enabled 
                    }
                    onOutOfSampleRatioChanged: function(value) { 
                        if (value !== undefined && value !== null) root.outOfSampleRatio = value 
                    }
                }
            }
        }
        
        // 底部操作栏
        Rectangle {
            id: footer
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: "#1e293b"
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16
                
                // 取消按钮
                Button {
                    id: cancelButton
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 40
                    text: Utils.StrategyCreationUtils.tr('common.cancel')
                    onClicked: {
                        root.backClicked()
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: "#334155"
                        border.width: 1
                        border.color: "#475569"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "#f1f5f9"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 步骤验证状态
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 8
                    color: "#334155"
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        
                        Rectangle {
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            radius: 10
                            color: stepIndicator.currentStepValid ? "#10b981" : "#ef4444"
                            
                            Text {
                                anchors.centerIn: parent
                                text: stepIndicator.currentStepValid ? "✓" : "⚠"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                color: "white"
                            }
                        }
                        
                        Text {
                            text: stepIndicator.currentStepValid ? 
                                  Utils.StrategyCreationUtils.tr('strategyCreation.validationPassed') : 
                                  Utils.StrategyCreationUtils.tr('strategyCreation.validationRequired')
                            font.pixelSize: 13
                            color: stepIndicator.currentStepValid ? "#10b981" : "#ef4444"
                        }
                        
                        Item { Layout.fillWidth: true }
                    }
                }
                
                // 上一步按钮
                Button {
                    id: prevButton
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 40
                    text: Utils.StrategyCreationUtils.tr('common.previous')
                    visible: stepIndicator.currentStep > 1
                    enabled: stepIndicator.currentStep > 1
                    onClicked: {
                        if (stepIndicator.currentStep > 1) {
                            stepIndicator.currentStep--
                        }
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? "#334155" : "#475569"
                        border.width: 1
                        border.color: parent.enabled ? "#475569" : "#64748b"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? "#f1f5f9" : "#94a3b8"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 创建按钮（仅在第3步显示）
                Button {
                    id: createButton
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 40
                    text: Utils.StrategyCreationUtils.tr('strategyCreation.create')
                    visible: stepIndicator.currentStep === 3
                    enabled: stepIndicator.currentStepValid
                    onClicked: {
                        createStrategy(false)
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? "#3b82f6" : "#475569"
                        border.width: 1
                        border.color: parent.enabled ? "#3b82f6" : "#64748b"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 创建并回测按钮
                Button {
                    id: createAndBacktestButton
                    Layout.preferredWidth: 160
                    Layout.preferredHeight: 40
                    text: Utils.StrategyCreationUtils.tr('strategyCreation.createAndBacktest')
                    visible: stepIndicator.currentStep === 3
                    enabled: stepIndicator.currentStepValid
                    onClicked: {
                        createStrategy(true)
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? "#10b981" : "#475569"
                        border.width: 1
                        border.color: parent.enabled ? "#10b981" : "#64748b"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                // 下一步按钮（仅在前两步显示）
                Button {
                    id: nextButton
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 40
                    text: Utils.StrategyCreationUtils.tr('common.next')
                    visible: stepIndicator.currentStep < 3
                    enabled: stepIndicator.currentStepValid
                    onClicked: {
                        if (stepIndicator.currentStep < 3) {
                            stepIndicator.currentStep++
                        }
                    }
                    
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? "#3b82f6" : "#475569"
                        border.width: 1
                        border.color: parent.enabled ? "#3b82f6" : "#64748b"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
    
    // ============ 内部状态 ============
    
    property bool step1Valid: false
    property bool step2Valid: false
    property bool step3Valid: true  // 风险管理步骤总是有效
    
    // 步骤指示器组件
    QtObject {
        id: stepIndicator
        property int currentStep: 1
        property bool currentStepValid: {
            switch(currentStep) {
                case 1: return step1Valid
                case 2: return step2Valid
                case 3: return step3Valid
                default: return false
            }
        }
    }
    
    // ============ 功能函数 ============
    
    // 创建策略
    function createStrategy(immediateBacktest = false) {
        console.log("开始创建策略...")
        
        // 构建完整的策略数据
        var context = {
            strategyName: strategyBasicInfo.strategyName,
            strategyDescription: strategyBasicInfo.strategyDescription,
            selectedStrategyType: selectedStrategyType,
            strategyTags: strategyBasicInfo.getTagsList(),
            assetType: strategyBasicInfo.getAssetTypeValue(),
            timeFrame: strategyBasicInfo.getTimeFrameValue(),
            riskLevel: strategyBasicInfo.getRiskLevelValue(),
            optimizationMethod: strategyBasicInfo.getOptimizationMethodValue(),
            
            backtestYears: backtestYears,
            benchmark: benchmark,
            transactionCost: transactionCost,
            maxDrawdownLimit: maxDrawdownLimit,
            positionSizingMethod: positionSizingMethod,
            maxPositionPercent: maxPositionPercent,
            stopLossPercent: stopLossPercent,
            takeProfitPercent: takeProfitPercent,
            
            enableAdvancedOptions: enableAdvancedOptions,
            enableWalkForward: enableWalkForward,
            enableMonteCarlo: enableMonteCarlo,
            monteCarloSamples: monteCarloSamples,
            enableOutOfSample: enableOutOfSample,
            outOfSampleRatio: outOfSampleRatio,
            
            strategyParameters: strategyParameters,
            parametersValid: parametersValid
        }
        
        // 从通用参数中提取commission和slippage值
        if (strategyParameters && strategyParameters.commission !== undefined) {
            context.commission = strategyParameters.commission
        }
        if (strategyParameters && strategyParameters.slippage !== undefined) {
            context.slippage = strategyParameters.slippage
        }
        
        var strategyData = Utils.StrategyCreationUtils.buildCompleteStrategyData(context)
        
        console.log("策略数据构建完成:", JSON.stringify(strategyData, null, 2))
        
        // 设置创建状态
        isCreating = true
        creationStatus = immediateBacktest ? 
            Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreatedBacktest') : 
            Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreatedSuccess')
        
        // 检查StrategyService是否可用
        if (!strategyService) {
            console.error("StrategyService 未初始化，无法创建策略")
            showErrorDialog("策略服务未初始化，请重启应用程序")
            isCreating = false
            return
        }
        
        // 将前端数据结构映射到后端所需格式
        var backendStrategyData = {
            "strategy_name": strategyData.name,
            "strategy_type": mapStrategyTypeToBackend(strategyData.strategyType),
            "description": strategyData.description,
            "asset_type": strategyData.assetType,
            "time_frame": strategyData.timeFrame,
            "risk_level": strategyData.riskLevel,
            "parameters": strategyData.parameters,
            
            // 元数据
            "status": "DRAFT",
            "version": "1.0",
            "language": "Python",
            "author": "System",
            
            // 回测相关
            "backtest_settings": {
                "years": backtestYears,
                "benchmark": benchmark,
                "transaction_cost": transactionCost,
                "max_drawdown_limit": maxDrawdownLimit,
                "position_sizing_method": positionSizingMethod,
                "max_position_percent": maxPositionPercent,
                "stop_loss_percent": stopLossPercent,
                "take_profit_percent": takeProfitPercent
            },
            
            // 高级选项
            "advanced_options": {
                "enable_walk_forward": enableWalkForward,
                "enable_monte_carlo": enableMonteCarlo,
                "monte_carlo_samples": monteCarloSamples,
                "enable_out_of_sample": enableOutOfSample,
                "out_of_sample_ratio": outOfSampleRatio
            },
            
            // 标签
            "tags": strategyTags
        }
        
        console.log("调用StrategyService创建策略...", JSON.stringify(backendStrategyData, null, 2))
        
        // 调用C++服务创建策略
        var strategyId = strategyService.createStrategy(backendStrategyData)
        
        if (strategyId && strategyId !== "") {
            console.log("策略创建成功，ID:", strategyId)
            
            // 注意：不需要手动调用syncWithDatabase，因为StrategyService.createStrategy()方法
            // 内部已经会发送dataChanged信号，StrategyLibraryPage会监听这个信号并自动更新
            
            // 如果有立即回测需求
            if (immediateBacktest) {
                console.log("准备启动回测...")
                // 这里可以触发回测逻辑
            }
            
            // 显示成功消息
            showSuccessDialog(strategyId)
        } else {
            console.error("策略创建失败")
            showErrorDialog("策略创建失败，请检查参数")
        }
        
        isCreating = false
    }
    
    // 映射策略类型到后端类型
    function mapStrategyTypeToBackend(frontendType) {
        var mapping = {
            "trend_following": "TREND",
            "mean_reversion": "MEAN_REVERSION", 
            "momentum": "ALPHA",
            "arbitrage": "ARBITRAGE",
            "machine_learning": "ALPHA",
            "multi_factor": "ALPHA",
            "high_frequency": "HFT",
            "event_driven": "ALPHA",
            "custom": "CUSTOM"
        }
        return mapping[frontendType] || "CUSTOM"
    }
    
    // 显示成功对话框
    function showSuccessDialog(strategyId) {
        successDialog.strategyId = strategyId
        successDialog.strategyName = strategyBasicInfo.strategyName
        successDialog.open()
    }
    
    // 显示错误对话框
    function showErrorDialog(message) {
        errorDialog.errorMessage = message
        errorDialog.open()
    }
    
    // 重置表单
    function resetForm() {
        stepIndicator.currentStep = 1
        isCreating = false
        creationStatus = ""
        
        // 重置各组件
        strategyTypeSelector.reset()
        strategyBasicInfo.reset()
        
        var resetData = Utils.StrategyCreationUtils.resetFormData()
        
        // 应用重置数据
        selectedStrategyType = resetData.selectedStrategyType
        strategyName = resetData.strategyName
        strategyDescription = resetData.strategyDescription
        strategyTags = resetData.strategyTags
        assetType = resetData.assetType
        timeFrame = resetData.timeFrame
        riskLevel = resetData.riskLevel
        optimizationMethod = resetData.optimizationMethod
        backtestYears = resetData.backtestYears
        benchmark = resetData.benchmark
        transactionCost = resetData.transactionCost
        maxDrawdownLimit = resetData.maxDrawdownLimit
        positionSizingMethod = resetData.positionSizingMethod
        maxPositionPercent = resetData.maxPositionPercent
        stopLossPercent = resetData.stopLossPercent
        takeProfitPercent = resetData.takeProfitPercent
        enableAdvancedOptions = resetData.enableAdvancedOptions
        enableWalkForward = resetData.enableWalkForward
        enableMonteCarlo = resetData.enableMonteCarlo
        monteCarloSamples = resetData.monteCarloSamples
        enableOutOfSample = resetData.enableOutOfSample
        outOfSampleRatio = resetData.outOfSampleRatio
        strategyParameters = resetData.strategyParameters
        parametersValid = resetData.parametersValid
    }
    
    // ============ 定时器和动画 ============
    
    Timer {
        id: creationTimer
        interval: 2000
        onTriggered: {
            isCreating = false
            creationStatus = ""
            console.log("策略创建完成")
            
            // 显示成功消息
            createSuccessDialog.open()
        }
    }
    
    // 创建成功对话框 (旧版本，保留兼容性)
    Dialog {
        id: createSuccessDialog
        title: Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreationSuccessDialogTitle')
        standardButtons: Dialog.Ok
        modal: true
        
        width: 400
        height: 200
        
        contentItem: ColumnLayout {
            spacing: 16
            
            Rectangle {
                Layout.preferredWidth: 60
                Layout.preferredHeight: 60
                radius: 30
                color: "#10b981"
                
                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: "white"
                }
            }
            
            Text {
                Layout.fillWidth: true
                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreatedSuccessDialogMessage')
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#f1f5f9"
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: Utils.StrategyCreationUtils.tr('strategyCreation.strategyCreatedSuccessDialogSubtitle')
                font.pixelSize: 13
                color: "#94a3b8"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
    
    // 策略创建成功对话框 (集成C++服务后使用)
    Dialog {
        id: successDialog
        title: "策略创建成功"
        standardButtons: Dialog.Ok
        modal: true
        
        width: 550
        height: 300
        
        property string strategyId: ""
        property string strategyName: ""
        
        contentItem: ColumnLayout {
            spacing: 20
            
            Rectangle {
                Layout.preferredWidth: 70
                Layout.preferredHeight: 70
                radius: 35
                color: "#10b981"
                
                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    font.pixelSize: 32
                    font.weight: Font.Bold
                    color: "white"
                }
            }
            
            Text {
                Layout.fillWidth: true
                text: "策略创建成功!"
                font.pixelSize: 20
                font.weight: Font.Bold
                color: "#10b981"
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: "策略名称: <b>" + successDialog.strategyName + "</b>"
                font.pixelSize: 14
                color: "#f1f5f9"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.RichText
            }
            
            Text {
                Layout.fillWidth: true
                text: "策略ID: " + successDialog.strategyId
                font.pixelSize: 12
                color: "#94a3b8"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: "策略已保存到数据库，接下来您想做什么？"
                font.pixelSize: 13
                color: "#cbd5e1"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            
                            // 操作按钮行
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Layout.topMargin: 10
                                
                                // 返回策略库按钮
                                Rectangle {
                                    Layout.preferredWidth: 120
                                    Layout.preferredHeight: 40
                                    radius: 6
                                    color: "#334155"
                                    
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        
                                        Text {
                                            text: "📋"
                                            font.pixelSize: 12
                                            color: "#F1F5F9"
                                        }
                                        
                                        Text {
                                            text: "返回策略库"
                                            font.pixelSize: 12
                                            color: "#F1F5F9"
                                        }
                                    }
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            console.log("点击返回策略库按钮")
                                            successDialog.close()
                                            // 关闭对话框
                                            successDialog.accepted()
                                        }
                                    }
                                }
                                
                                Item { Layout.fillWidth: true }
                                
                                // 开始回测按钮
                                Rectangle {
                                    Layout.preferredWidth: 120
                                    Layout.preferredHeight: 40
                                    radius: 6
                                    color: "#3B82F6"
                                    
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        
                                        Text {
                                            text: "🔄"
                                            font.pixelSize: 12
                                            color: "white"
                                        }
                                        
                                        Text {
                                            text: "开始回测"
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            color: "white"
                                        }
                                    }
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            successDialog.close()
                                            root.requestBacktest(successDialog.strategyId, successDialog.strategyName)
                                        }
                                    }
                                }
                            }
        }
        
        onAccepted: {
            // 默认操作：返回到策略库
            console.log("对话框确认按钮点击，返回到策略库")
            successDialog.close()
            root.backClicked()
        }
    }
    
    // 错误对话框
    Dialog {
        id: errorDialog
        title: "策略创建失败"
        standardButtons: Dialog.Ok
        modal: true
        
        width: 450
        height: 200
        
        property string errorMessage: ""
        
        contentItem: ColumnLayout {
            spacing: 20
            
            Rectangle {
                Layout.preferredWidth: 60
                Layout.preferredHeight: 60
                radius: 30
                color: "#ef4444"
                
                Text {
                    anchors.centerIn: parent
                    text: "⚠"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: "white"
                }
            }
            
            Text {
                Layout.fillWidth: true
                text: "策略创建失败"
                font.pixelSize: 18
                font.weight: Font.Bold
                color: "#ef4444"
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: errorDialog.errorMessage || "未知错误"
                font.pixelSize: 13
                color: "#f1f5f9"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
    
    // ============ 初始化和信号连接 ============
    
Component.onCompleted: {
    console.log("StrategyCreationPagePro 初始化完成")
    
    // 初始化数据
    resetForm()
    
    // StrategyService已经通过property绑定，直接使用
    if (strategyService) {
        console.log("StrategyService 初始化成功")
        // 可以在这里初始化服务
        strategyService.initialize()
    } else {
        console.warn("StrategyService 未找到")
    }
    
    // 连接信号
    strategyBasicInfo.validationChanged.connect(function(isValid) {
        step1Valid = isValid
    })
}
}
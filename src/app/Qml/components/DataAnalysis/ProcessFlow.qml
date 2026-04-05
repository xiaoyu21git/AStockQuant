// ProcessFlow.qml - 流程流水线组件，统一展示数据清洗到回测到风险管理到实盘的流程
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: 800
    height: 114
    
    // 属性
    property string title: "量化交易流程"
    property string subtitle: "从数据清洗到实盘部署的完整流程"
    property color backgroundColor: "#0f1738"
    property color borderColor: "#2d3a7c"
    property color textColor: "white"
    property color textSecondaryColor: "#b0b0b0"
    property color activeColor: "#00bcd4"
    property color completedColor: "#4caf50"
    property color pendingColor: "#3949ab"
    property bool waterFlowEnabled: true
    property int linkedStep: 1
    
    // 当前步骤
    property int currentStep: 1
    property int totalSteps: 5
    
    // 步骤数据
    property var steps: [
        {
            id: "data-prep",
            title: "数据准备",
            description: "数据源管理、清洗、标准化",
            icon: "📊",
            actionType: "page", // 数据准备进入页面而不是弹窗
            color: "#00bcd4",
            isEntryPoint: true
        },
        {
            id: "strategy-dev", 
            title: "策略开发",
            description: "因子分析、策略创建、参数优化",
            icon: "📈",
            actionType: "page", // 因子分析需要切换页面
            color: "#9c27b0",
            isEntryPoint: false
        },
        {
            id: "backtest",
            title: "回测验证",
            description: "历史回测、绩效分析、过拟合检验",
            icon: "⏱️",
            actionType: "inline", // 回测可以在当前页面展开
            color: "#ff9800",
            isEntryPoint: false
        },
        {
            id: "risk-mgmt",
            title: "风险管理",
            description: "风险配置、压力测试、合规检查",
            icon: "🛡️",
            actionType: "page", // 风险管理需要专门页面
            color: "#f44336",
            isEntryPoint: true
        },
        {
            id: "live-deploy", 
            title: "实盘部署",
            description: "实盘执行、监控、绩效跟踪",
            icon: "🚀",
            actionType: "external", // 实盘部署可能连接外部系统
            color: "#4caf50",
            isEntryPoint: true
        }
    ]
    
    // 信号
    signal stepActivated(int stepIndex)
    signal stepActionTriggered(int stepIndex, string actionType)
    signal flowCompleted()

    onLinkedStepChanged: {
        if (linkedStep >= 1 && linkedStep <= totalSteps && currentStep !== linkedStep) {
            currentStep = linkedStep
        }
    }
    
    // 背景
    Rectangle {
        id: flowBackground
        anchors.fill: parent
        color: root.backgroundColor
        radius: 8
        border.color: root.borderColor
        border.width: 1
    }
    
    // 内容
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        
        // 标题栏
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 18
            
            Text {
                text: root.title
                font.pixelSize: 14
                font.bold: true
                color: root.textColor
            }
            
            Item { Layout.fillWidth: true }
            
            // 进度显示
            Text {
                text: "进度: " + Math.round(((root.currentStep - 1) / root.totalSteps) * 100) + "%"
                font.pixelSize: 12
                color: root.activeColor
            }
        }
        
        // 副标题
        Text {
            text: root.subtitle
            font.pixelSize: 10
            color: root.textSecondaryColor
            Layout.fillWidth: true
            maximumLineCount: 1
            elide: Text.ElideRight
            visible: false
        }
        
        // 流程节点行
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            
            // 连接线
            Rectangle {
                id: flowTrack
                Layout.fillWidth: true
                height: 2
                y: 12
                color: root.borderColor
                radius: 1
                
                // 进度线
                Rectangle {
                    id: progressLine
                    width: parent.width * ((root.currentStep - 1) / root.totalSteps)
                    height: 3
                    color: root.activeColor
                    radius: 2

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Qt.rgba(1, 1, 1, 0.08)
                    }

                    Item {
                        id: waterFlowViewport
                        anchors.fill: parent
                        clip: true
                        visible: root.waterFlowEnabled && progressLine.width > 24

                        Repeater {
                            model: 3

                            Rectangle {
                                required property int index
                                width: Math.max(40, waterFlowViewport.width * 0.32)
                                height: 7
                                y: -2
                                radius: height / 2
                                color: "transparent"
                                opacity: 0.42 - (index * 0.08)

                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: "transparent" }
                                    GradientStop { position: 0.18; color: Qt.rgba(0.56, 0.93, 0.98, 0.02) }
                                    GradientStop { position: 0.48; color: Qt.rgba(0.56, 0.93, 0.98, 0.30) }
                                    GradientStop { position: 0.7; color: Qt.rgba(1, 1, 1, 0.18) }
                                    GradientStop { position: 1.0; color: "transparent" }
                                }

                                NumberAnimation on x {
                                    from: -width - (index * 42)
                                    to: waterFlowViewport.width + (index * 16)
                                    duration: 2600 + (index * 260)
                                    loops: Animation.Infinite
                                    running: waterFlowViewport.visible
                                }
                            }
                        }
                    }
                    
                    Behavior on width {
                        NumberAnimation { duration: 300 }
                    }
                }
            }
            
            // 流程节点
            Repeater {
                model: root.steps.length
                
                ProcessNode {
                    id: processNode
                    width: 94
                    height: 60
                    readonly property var stepData: root.steps[index] || ({})
                    
                    nodeId: stepData.id || ""
                    title: stepData.title || ""
                    description: stepData.description || ""
                    icon: stepData.icon || ""
                    nodeColor: stepData.color || root.pendingColor
                    backgroundColor: root.backgroundColor
                    textColor: root.textColor
                    textSecondaryColor: root.textSecondaryColor
                    stepNumber: index + 1
                    actionType: stepData.actionType || ""
                    
                    isActive: index < root.currentStep - 1
                    isCompleted: index < root.currentStep - 1
                    isCurrent: index === root.currentStep - 1
                    
                    onNodeClicked: {
                        console.log("流程节点点击:", stepData.id)
                        if (index <= root.currentStep - 1) {
                            root.currentStep = index + 1
                            root.stepActivated(index + 1)
                        }
                    }
                    
                    onActionTriggered: function(actionType) {
                        console.log("流程节点操作触发:", stepData.id, "类型:", actionType)
                        root.stepActionTriggered(index + 1, actionType)
                        
                        // 根据操作类型执行不同的行为
                        handleStepAction(index + 1, actionType)
                    }
                    
                    onStatusChanged: function(isCompleted) {
                        console.log("流程节点状态改变:", stepData.id, "完成:", isCompleted)
                        if (isCompleted && index === root.currentStep - 1 && root.currentStep < root.totalSteps) {
                            root.currentStep++
                        }
                        
                        // 检查是否所有步骤都完成
                        if (root.currentStep > root.totalSteps) {
                            root.flowCompleted()
                        }
                    }
                }
            }
        }
        
        // 状态信息 - 删除"当前步骤"文字描述，只保留入库点标记
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            
            Item { Layout.fillWidth: true }
            
            // 入库点标记
            Rectangle {
                width: 68
                height: 12
                radius: 9
                color: root.currentStep <= root.totalSteps && root.steps[root.currentStep-1].isEntryPoint ? "#f59e0b" : "transparent"
                border.color: root.currentStep <= root.totalSteps && root.steps[root.currentStep-1].isEntryPoint ? "#f59e0b" : "transparent"
                border.width: 1
                visible: root.currentStep <= root.totalSteps && root.steps[root.currentStep-1].isEntryPoint
                
                Text {
                    anchors.centerIn: parent
                    text: "数据入库点"
                    font.pixelSize: 7
                    font.bold: true
                    color: root.currentStep <= root.totalSteps && root.steps[root.currentStep-1].isEntryPoint ? "white" : "transparent"
                }
            }
            
            Item { Layout.fillWidth: true }
        }
    }
    
    // 处理步骤操作
    function handleStepAction(stepIndex, actionType) {
        console.log("处理步骤操作:", stepIndex, "类型:", actionType)
        
        var stepData = root.steps[stepIndex-1]
        
        switch(actionType) {
            case "page":
                console.log("切换到" + stepData.title + "页面")
                navigateToPageForStep(stepIndex)
                break

            case "modal":
                console.log("打开弹窗进行" + stepData.title + "操作")
                showModalForStep(stepIndex)
                break
                
            case "inline":
                console.log("展开" + stepData.title + "内联内容")
                expandInlineForStep(stepIndex)
                break
                
            case "external":
                console.log("打开" + stepData.title + "外部系统")
                openExternalForStep(stepIndex)
                break
        }
    }
    
    // 模拟弹窗操作
    function showModalForStep(stepIndex) {
        console.log("为步骤" + stepIndex + "显示弹窗")
        
        // 这里可以触发具体的弹窗显示逻辑
        switch(stepIndex) {
            case 1: // 数据准备
                console.log("显示数据源添加弹窗")
                // 触发数据源添加弹窗
                if (typeof dashboardPage !== 'undefined' && dashboardPage.showAddDataSourcePopup) {
                    dashboardPage.showAddDataSourcePopup()
                }
                break
            case 3: // 回测验证
                console.log("显示回测配置弹窗")
                break
        }
    }
    
    // 模拟页面导航
    function navigateToPageForStep(stepIndex) {
        console.log("导航到步骤" + stepIndex + "的页面")
        
        // 这里可以触发具体的页面导航逻辑
        switch(stepIndex) {
            case 1: // 数据准备
                console.log("导航到数据准备页面")
                break
            case 2: // 策略开发
                console.log("导航到因子分析页面")
                // 触发因子分析页面导航
                if (typeof dashboardPage !== 'undefined' && dashboardPage.factorAnalysisModule) {
                    dashboardPage.factorAnalysisModule.cardClicked()
                }
                break
            case 4: // 风险管理
                console.log("导航到风险管理页面")
                // 触发风险管理页面导航
                if (typeof dashboardPage !== 'undefined' && dashboardPage.riskManagementModule) {
                    dashboardPage.riskManagementModule.cardClicked()
                }
                break
        }
    }
    
    // 模拟内联展开
    function expandInlineForStep(stepIndex) {
        console.log("展开步骤" + stepIndex + "的内联内容")
        
        // 这里可以触发具体的内联展开逻辑
        switch(stepIndex) {
            case 3: // 回测验证
                console.log("展开回测配置面板")
                break
        }
    }
    
    // 模拟外部系统打开
    function openExternalForStep(stepIndex) {
        console.log("打开步骤" + stepIndex + "的外部系统")
        
        // 这里可以触发具体的外部系统打开逻辑
        switch(stepIndex) {
            case 5: // 实盘部署
                console.log("打开实盘交易系统")
                break
        }
    }
    
    // API方法
    function goToStep(stepNumber) {
        if (stepNumber >= 1 && stepNumber <= root.totalSteps) {
            root.currentStep = stepNumber
            root.stepActivated(stepNumber)
        }
    }
    
    function markStepCompleted(stepNumber) {
        if (stepNumber >= 1 && stepNumber <= root.totalSteps) {
            // 找到对应的节点并标记为完成
            for (var i = 0; i < root.steps.length; i++) {
                if (i === stepNumber - 1) {
                    // 这里需要更新节点的状态
                    // 由于节点是动态创建的，我们需要通过其他方式更新
                    console.log("标记步骤" + stepNumber + "为完成")
                    
                    if (stepNumber === root.currentStep && root.currentStep < root.totalSteps) {
                        root.currentStep++
                    }
                    break
                }
            }
        }
    }
    
    function reset() {
        root.currentStep = 1
        // 重置所有节点的状态
        console.log("重置流程到第一步")
    }
    
    function updateStepActionType(stepIndex, actionType) {
        if (stepIndex >= 1 && stepIndex <= root.totalSteps) {
            root.steps[stepIndex-1].actionType = actionType
            // 触发属性更改通知
            var temp = root.steps
            root.steps = []
            root.steps = temp
        }
    }
    
    function getCurrentStepData() {
        if (root.currentStep >= 1 && root.currentStep <= root.totalSteps) {
            return root.steps[root.currentStep-1]
        }
        return null
    }
    
    function isStepEntryPoint(stepIndex) {
        if (stepIndex >= 1 && stepIndex <= root.totalSteps) {
            return root.steps[stepIndex-1].isEntryPoint
        }
        return false
    }
    
    // 初始化
    Component.onCompleted: {
        console.log("ProcessFlow组件初始化完成，总步骤数:", root.totalSteps)
    }
}
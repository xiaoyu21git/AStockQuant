// FactorAnalysisPage.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../components/Factor" as FactorComponents

/**
 * 因子分析页面 - 新建因子和因子分析功能
 * 提供因子创建、参数配置、分析测试等功能
 */
Item {
    id: root
    
    // ============ 页面属性 ============
    
    property bool showCreationWizard: false
    property var currentStep: 1
    property int totalSteps: 4
    
    // 当前创建的因子数据
    property var newFactorData: {
        "step1": {},  // 基本信息
        "step2": {},  // 参数配置
        "step3": {},  // 测试验证
        "step4": {}   // 保存确认
    }
    
    // ============ 页面布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 页面标题栏
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "#1E293B"  // bgSecondary
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                
                // 返回按钮
                Rectangle {
                    width: 40
                    height: 40
                    radius: 8
                    color: "#334155"  // bgTertiary
                    
                    Text {
                        anchors.centerIn: parent
                        text: "←"
                        font.pixelSize: 16
                        color: "#F1F5F9"  // textPrimary
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (showCreationWizard && currentStep > 1) {
                                currentStep--
                            } else if (showCreationWizard) {
                                showCreationWizard = false
                            } else {
                                // 返回上一级页面
                                console.log("返回上一级页面")
                            }
                        }
                    }
                }
                
                // 页面标题
                Text {
                    text: showCreationWizard ? "新建因子向导" : "因子分析"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"  // textPrimary
                }
                
                Item {
                    Layout.fillWidth: true
                }
                
                // 新建因子按钮
                Rectangle {
                    width: 120
                    height: 40
                    radius: 8
                    color: "#3B82F6"  // factorMomentum
                    visible: !showCreationWizard
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        
                        Text {
                            text: "➕"
                            font.pixelSize: 14
                            color: "white"
                        }
                        
                        Text {
                            text: "新建因子"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "white"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            showCreationWizard = true
                            currentStep = 1
                            resetNewFactorData()
                        }
                    }
                }
                
                // 向导进度显示
                Row {
                    spacing: 8
                    visible: showCreationWizard
                    
                    Repeater {
                        model: totalSteps
                        
                        delegate: Rectangle {
                            width: 40
                            height: 40
                            radius: 20
                            color: index + 1 <= currentStep ? "#3B82F6" : "#334155"
                            
                            Text {
                                anchors.centerIn: parent
                                text: index + 1
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: index + 1 <= currentStep ? "white" : "#94A3B8"
                            }
                            
                            Text {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: 20
                                text: getStepName(index + 1)
                                font.pixelSize: 10
                                color: "#94A3B8"
                            }
                        }
                    }
                }
            }
        }
        
        // 主内容区域
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0F172A"  // bgPrimary
            
            // 因子分析主页面
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                visible: !showCreationWizard
                
                // 快速开始卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    radius: 16
                    color: "#1E293B"  // bgSecondary
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12
                        
                        Text {
                            text: "🚀 快速开始因子分析"
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: "创建和测试自定义因子，优化投资策略性能"
                            font.pixelSize: 14
                            color: "#94A3B8"
                            wrapMode: Text.WordWrap
                        }
                        
                        Row {
                            spacing: 12
                            
                            // 模板因子
                            Rectangle {
                                width: 160
                                height: 100
                                radius: 8
                                color: "#334155"
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "📊"
                                        font.pixelSize: 20
                                        color: "#3B82F6"
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "从模板开始"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "#F1F5F9"
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "使用预设模板"
                                        font.pixelSize: 10
                                        color: "#94A3B8"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: startFromTemplate()
                                }
                            }
                            
                            // 自定义因子
                            Rectangle {
                                width: 160
                                height: 100
                                radius: 8
                                color: "#334155"
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "🎨"
                                        font.pixelSize: 20
                                        color: "#8B5CF6"
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "完全自定义"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "#F1F5F9"
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "从头开始创建"
                                        font.pixelSize: 10
                                        color: "#94A3B8"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: startCustomCreation()
                                }
                            }
                            
                            // 导入因子
                            Rectangle {
                                width: 160
                                height: 100
                                radius: 8
                                color: "#334155"
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "📥"
                                        font.pixelSize: 20
                                        color: "#10B981"
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "导入外部因子"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "#F1F5F9"
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "从文件导入"
                                        font.pixelSize: 10
                                        color: "#94A3B8"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: importExternalFactor()
                                }
                            }
                        }
                    }
                }
                
                // 最近分析的因子
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 16
                    color: "#1E293B"  // bgSecondary
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        
                        Text {
                            text: "📈 最近分析的因子"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        // 占位内容
                        Column {
                            anchors.centerIn: parent
                            spacing: 16
                            
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "📊"
                                font.pixelSize: 40
                                color: "#64748B"
                            }
                            
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "暂无最近分析的因子"
                                font.pixelSize: 16
                                font.weight: Font.Medium
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "创建一个因子开始分析"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                        }
                    }
                }
            }
            
            // 新建因子向导
            Rectangle {
                id: wizardContainer
                anchors.fill: parent
                color: "#0F172A"
                visible: showCreationWizard
                
                StackLayout {
                    id: wizardStack
                    anchors.fill: parent
                    anchors.margins: 16
                    currentIndex: currentStep - 1
                    
                    // 步骤1: 基本信息
                    Rectangle {
                        radius: 16
                        color: "#1E293B"
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 16
                            
                            Text {
                                text: "📝 步骤1: 基本信息"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: "填写因子的基本信息和类别"
                                font.pixelSize: 14
                                color: "#94A3B8"
                            }
                            
                            // 因子名称
                            Column {
                                spacing: 8
                                
                                Text {
                                    text: "因子名称"
                                    font.pixelSize: 14
                                    color: "#F1F5F9"
                                }
                                
                                Rectangle {
                                    width: 300
                                    height: 40
                                    radius: 8
                                    color: "#334155"
                                    
                                    TextInput {
                                        id: factorNameInput
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 14
                                        color: "#F1F5F9"
                                        text: ""
                                        
                                        Text {
                                            anchors.fill: parent
                                            verticalAlignment: Text.AlignVCenter
                                            text: "输入因子名称..."
                                            font: parent.font
                                            color: "#64748B"
                                            visible: !parent.text && !parent.activeFocus
                                        }
                                    }
                                }
                            }
                            
                            // 因子类别选择
                            Column {
                                spacing: 8
                                
                                Text {
                                    text: "因子类别"
                                    font.pixelSize: 14
                                    color: "#F1F5F9"
                                }
                                
                                Row {
                                    spacing: 12
                                    
                                    Repeater {
                                        model: ["动量类", "价值类", "质量类", "成长类", "情绪类"]
                                        
                                        delegate: Rectangle {
                                            id: categoryItem
                                            width: 80
                                            height: 32
                                            radius: 8
                                            color: getCategoryBackground(modelData)
                                            border.width: getCategoryBorderWidth(modelData)
                                            border.color: getCategoryBorderColor(modelData)
                                            
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData
                                                font.pixelSize: 12
                                                color: getCategoryTextColor(modelData)
                                            }
                                            
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    selectCategory(modelData)
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                // 显示当前选择的类别
                                Text {
                                    text: getCategorySelectionText()
                                    font.pixelSize: 12
                                    color: newFactorData.step1.category ? "#10B981" : "#94A3B8"
                                    visible: true
                                }
                            }
                            
                            // 描述
                            Column {
                                spacing: 8
                                
                                Text {
                                    text: "因子描述"
                                    font.pixelSize: 14
                                    color: "#F1F5F9"
                                }
                                
                                Rectangle {
                                    width: 300
                                    height: 80
                                    radius: 8
                                    color: "#334155"
                                    
                                    TextArea {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        font.pixelSize: 14
                                        color: "#F1F5F9"
                                        wrapMode: Text.WordWrap
                                        placeholderText: "描述因子的作用和计算逻辑..."
                                    }
                                }
                            }
                        }
                    }
                    
                    // 步骤2: 参数配置 - 使用专门的参数配置组件
                    Rectangle {
                        id: step2Container
                        radius: 16
                        color: "#1E293B"
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 16
                            
                            // 步骤标题
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                
                                Text {
                                    text: "⚙️ 步骤2: 参数配置"
                                    font.pixelSize: 18
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: "配置因子的计算参数"
                                    font.pixelSize: 14
                                    color: "#94A3B8"
                                }
                                
                                // 因子类别提示
                                Text {
                                    text: "当前因子类别: " + (newFactorData.step1.category ? "「" + newFactorData.step1.category + "」" : "未选择")
                                    font.pixelSize: 14
                                    color: newFactorData.step1.category ? "#F59E0B" : "#94A3B8"
                                    visible: true
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            
                            // 使用专门的参数配置组件
                            FactorComponents.FactorTypeParameterPage {
                                id: typeParameterPage
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                factorCategory: newFactorData.step1.category || ""
                                
                                onParameterChanged: function(paramName, value) {
                                    console.log("因子参数变化:", paramName, "=", value)
                                    // 保存参数到步骤2数据
                                    if (!newFactorData.step2) newFactorData.step2 = {}
                                    newFactorData.step2[paramName] = value
                                }
                                
                                onAllParametersChanged: function(parameters) {
                                    console.log("所有参数更新:", JSON.stringify(parameters))
                                    // 保存所有参数到步骤2数据
                                    newFactorData.step2 = parameters
                                }
                            }
                        }
                        
                        // 确保当步骤2显示时，参数页面能正确加载
                        onVisibleChanged: {
                            if (visible) {
                                console.log("步骤2显示，当前因子类别:", newFactorData.step1.category)
                                // 强制更新参数页面
                                typeParameterPage.factorCategory = newFactorData.step1.category || ""
                            }
                        }
                    }
                    
                    // 步骤3: 测试验证
                    Rectangle {
                        radius: 16
                        color: "#1E293B"
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 16
                            
                            Text {
                                text: "🧪 步骤3: 测试验证"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: "测试因子性能并验证结果"
                                font.pixelSize: 14
                                color: "#94A3B8"
                            }
                            
                            // 占位内容
                            Column {
                                anchors.centerIn: parent
                                spacing: 16
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "📊"
                                    font.pixelSize: 40
                                    color: "#64748B"
                                }
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "测试验证"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "在这里测试因子的性能表现"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                            }
                        }
                    }
                    
                    // 步骤4: 保存确认
                    Rectangle {
                        radius: 16
                        color: "#1E293B"
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 16
                            
                            Text {
                                text: "✅ 步骤4: 保存确认"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: "确认并保存因子配置"
                                font.pixelSize: 14
                                color: "#94A3B8"
                            }
                            
                            // 占位内容
                            Column {
                                anchors.centerIn: parent
                                spacing: 16
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "🎉"
                                    font.pixelSize: 40
                                    color: "#64748B"
                                }
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "保存确认"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "确认并保存因子到因子库"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                            }
                        }
                    }
                }
                
                // 向导操作按钮
                Row {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12
                    
                    // 上一步
                    Rectangle {
                        width: 100
                        height: 40
                        radius: 8
                        color: "#334155"
                        visible: currentStep > 1
                        
                        Text {
                            anchors.centerIn: parent
                            text: "上一步"
                            font.pixelSize: 14
                            color: "#F1F5F9"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (currentStep > 1) currentStep--
                            }
                        }
                    }
                    
                    // 取消
                    Rectangle {
                        width: 100
                        height: 40
                        radius: 8
                        color: "#334155"
                        visible: true
                        
                        Text {
                            anchors.centerIn: parent
                            text: "取消"
                            font.pixelSize: 14
                            color: "#F1F5F9"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                showCreationWizard = false
                                resetNewFactorData()
                            }
                        }
                    }
                    
                    // 下一步/完成
                    Rectangle {
                        width: 120
                        height: 40
                        radius: 8
                        color: currentStep === totalSteps ? "#4caf50" : "#3B82F6"
                        
                        Text {
                            anchors.centerIn: parent
                            text: currentStep === totalSteps ? "完成" : "下一步"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "white"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (currentStep < totalSteps) {
                                    currentStep++
                                } else {
                                    saveNewFactor()
                                    showCreationWizard = false
                                    showSuccessMessage()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 工具函数 ============
    
    // 获取步骤名称
    function getStepName(step) {
        switch (step) {
            case 1: return "基本信息"
            case 2: return "参数配置"
            case 3: return "测试验证"
            case 4: return "保存确认"
            default: return "步骤" + step
        }
    }
    
    // 获取类别颜色
    function getCategoryColor(category) {
        switch (category) {
            case "动量类": return "#3B82F6"
            case "价值类": return "#F59E0B"
            case "质量类": return "#10B981"
            case "成长类": return "#8B5CF6"
            case "情绪类": return "#EC4899"
            default: return "#334155"
        }
    }
    
    // 重置新因子数据
    function resetNewFactorData() {
        newFactorData = {
            "step1": {},
            "step2": {},
            "step3": {},
            "step4": {}
        }
    }
    
    // 从模板开始
    function startFromTemplate() {
        showCreationWizard = true
        currentStep = 1
        newFactorData.step1.template = "preset"
        console.log("从模板开始创建因子")
    }
    
    // 自定义创建
    function startCustomCreation() {
        showCreationWizard = true
        currentStep = 1
        newFactorData.step1.template = "custom"
        console.log("自定义创建因子")
    }
    
    // 导入外部因子
    function importExternalFactor() {
        console.log("导入外部因子")
        // TODO: 实现导入功能
    }
    
    // 保存新因子
    function saveNewFactor() {
        console.log("保存新因子:", JSON.stringify(newFactorData))
        // TODO: 调用后端API保存因子
    }
    
    // 显示成功消息
    function showSuccessMessage() {
        console.log("因子创建成功!")
        // TODO: 显示成功消息提示
    }
    
    // ============ 步骤1: 因子类别选择辅助函数 ============
    
    // 获取类别背景颜色
    function getCategoryBackground(category) {
        return newFactorData.step1.category === category ? getCategoryColor(category) : "#334155"
    }
    
    // 获取类别边框宽度
    function getCategoryBorderWidth(category) {
        return newFactorData.step1.category === category ? 2 : 1
    }
    
    // 获取类别边框颜色
    function getCategoryBorderColor(category) {
        return newFactorData.step1.category === category ? getCategoryColor(category) : "#475569"
    }
    
    // 获取类别文字颜色
    function getCategoryTextColor(category) {
        return newFactorData.step1.category === category ? "white" : "#94A3B8"
    }
    
    // 选择类别
    function selectCategory(category) {
        newFactorData.step1.category = category
        console.log("选择因子类别:", category)
        
        // 确保步骤2的参数页面能立即响应类别变化
        if (currentStep === 2) {
            typeParameterPage.factorCategory = category
        }
    }
    
    // 获取类别选择文本
    function getCategorySelectionText() {
        return newFactorData.step1.category ? "已选择: " + newFactorData.step1.category : "请选择一个类别"
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        resetNewFactorData()
        console.log("因子分析页面加载完成")
    }
}
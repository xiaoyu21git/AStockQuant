// CreationPagePluginIntegrated_fix.qml
// 修复动态参数无法创建的问题

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import ConsoleUi 1.0
import "../../Factor" as FactorComponents
import "./components" as PluginComponents

Rectangle {
    id: root
    color: "#0F172A"
    
    // ============ 页面属性 ============
    
    property var factorParamController
    property var factorService: null
    property var factorDataModel: null
    property var editingFactorData: ({})
    readonly property bool factorMutationInProgress: !!(factorService && factorService.mutationInProgress)
    readonly property string editingFactorId: editingFactorData && editingFactorData.factorId ? String(editingFactorData.factorId) : ""
    readonly property bool editMode: editingFactorId !== ""
    property bool applyingEditingFactorData: false
    
    // 当前步骤
    property int currentStep: 0
    readonly property int totalSteps: 3
    
    // 因子数据
    property int selectedType: -1
    property string selectedTypeName: getTypeName(selectedType)
    property string factorName: ""
    property string factorDescription: ""
    property var factorParameters: ({})
    property var factorTags: []
    
    // 插件化组件注册表（静态声明，替代动态创建）
    PluginComponents.ParamComponents {
        id: paramComponents
    }
    property var currentParamConfigs: []
    property bool parametersValid: false
    property string validationMessage: ""
    property bool generatorValidationPassed: true
    property int generatorValidationErrorCount: 0
    readonly property var previewParameters: buildSubmittedParameters(false)
    
    // 信号
    signal factorCreated(var factorData)
    signal backClicked()
    signal typeChanged(int type)
    signal toastRequested(string message)
    
    // ============ 主布局 ============
    
    ScrollView {
        id: scrollView
        anchors.fill: parent
        anchors.margins: 5
        clip: true
        
        // 隐藏滚动条
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        
        ColumnLayout {
            width: scrollView.width - 20
            spacing: 10
            
            // 标题
            Text {
                text: root.editMode ? "✏️ 编辑因子" : "📝 创建新因子"
                font.pixelSize: 22
                font.weight: Font.Bold
                color: "#F1F5F9"
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 5
            }
            
            
            
            // 内容区域
            Rectangle {
                Layout.fillWidth: true
                Layout.minimumHeight: 420
                radius: 10
                color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    // 步骤标题
                    Text {
                        text: getStepTitle(currentStep)
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                    
                    // 步骤描述
                    Text {
                        text: getStepDescription(currentStep)
                        font.pixelSize: 13
                        color: "#94A3B8"
                        wrapMode: Text.WordWrap
                    }
                    
                    // 步骤内容
                    Loader {
                        id: stepContentLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: getStepComponent(currentStep)
                    }
                    
                    // 导航按钮 - 使用项目标准按钮样式
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        
                        // 返回按钮
                        Rectangle {
                            id: backButton
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 40
                            radius: 8
                            color: currentStep > 0 ? (root.factorMutationInProgress ? "#1E293B" : "#334155") : "transparent"
                            border.width: currentStep > 0 ? 1 : 0
                            border.color: "#475569"
                            visible: currentStep > 0
                            opacity: root.factorMutationInProgress ? 0.55 : 1
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "←"
                                    font.pixelSize: 14
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: "返回"
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: root.factorMutationInProgress ? Qt.ForbiddenCursor : Qt.PointingHandCursor
                                enabled: currentStep > 0 && !root.factorMutationInProgress
                                onClicked: {
                                    if (currentStep > 0) {
                                        clearStepDraft(currentStep)
                                        currentStep--
                                    }
                                }
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 下一步/创建按钮
                        Rectangle {
                            id: nextButton
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 40
                            radius: 8
                            color: (isStepValid(currentStep) && !root.factorMutationInProgress) ? "#3B82F6" : "#334155"
                            opacity: root.factorMutationInProgress ? 0.7 : 1
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: currentStep === totalSteps - 1 ? "✓" : "→"
                                    font.pixelSize: 14
                                    color: (isStepValid(currentStep) && !root.factorMutationInProgress) ? "white" : "#94A3B8"
                                }
                                
                                Text {
                                    text: currentStep === totalSteps - 1
                                        ? (root.editMode ? "保存修改" : "创建因子")
                                        : "下一步"
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    color: (isStepValid(currentStep) && !root.factorMutationInProgress) ? "white" : "#94A3B8"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: root.factorMutationInProgress ? Qt.ForbiddenCursor : Qt.PointingHandCursor
                                enabled: isStepValid(currentStep) && !root.factorMutationInProgress
                                onClicked: {
                                    if (currentStep < totalSteps - 1) {
                                        currentStep++
                                    } else {
                                        if (root.editMode) {
                                            updateFactor()
                                        } else {
                                            createFactor()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 步骤组件 ============
    
    // 步骤1: 选择因子类型
    Component {
        id: step1Component
        ScrollView {
            anchors.fill: parent
            clip: true
            
            // 隐藏滚动条
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            
            ColumnLayout {
                width: parent.width - 20
                spacing: 12
                
                // 类型选择网格 - 从 C++ 类型目录读取展示元数据
                GridLayout {
                    id: typeGrid
                    columns: 10  // 10列布局，充分利用宽度
                    columnSpacing: 8
                    rowSpacing: 8
                    Layout.fillWidth: true
                    
                    // 确保每列均匀分配宽度
                    //uniformCellWidths: true

                    Repeater {
                        model: root.factorUiMetaList()

                        delegate: FactorTypeCard {
                            required property var modelData

                            typeId: Number(modelData.factorType)
                            displayName: String(modelData.displayName || "")
                            description: String(modelData.cardDescription || "")
                            icon: String(modelData.icon || "")
                            color: String(modelData.color || "#94A3B8")
                            isSelected: root.selectedType === typeId
                            onClicked: {
                                root.selectedType = typeId
                                root.typeChanged(typeId)
                            }
                        }
                    }
                }
            }
        }
    }
    
 // 步骤2: 基本信息与参数配置（合并版）
    Component {
        id: step2Component
        
        // 使用ScrollView并显示滚动条
        ScrollView {
            id: step2ScrollView
            anchors.fill: parent
            clip: true
            contentHeight: contentColumn.height  // 关键：设置内容高度
            
            // 显示滚动条
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
            
            // 主内容列
            Column {
                id: contentColumn
                width: step2ScrollView.availableWidth
                spacing: 16
                height: childrenRect.height  // 确保高度正确计算
                
                // 保存父级引用
                property var rootRef: root
                property var paramComponentsRef: paramComponents
                
                // 基本信息区域
                    Rectangle {
                        id: infoCard
                        width: parent.width
                        height: 308
                        radius: 8
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                        
                        Column {
                            width: parent.width - 24
                            anchors.centerIn: parent
                            spacing: 10
                            
                            Text {
                                width: parent.width
                                text: "📝 基本信息"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#F1F5F9"
                            }
                            
                            // 因子名称
                            Column {
                                width: parent.width
                                spacing: 4
                                
                                Text {
                                    width: parent.width
                                    text: "因子名称 *"
                                    font.pixelSize: 12
                                    color: "#CBD5E1"
                                }
                                
                                TextField {
                                    id: factorNameField
                                    width: parent.width
                                    height: 25
                                    placeholderText: contentColumn.rootRef.factorUiMeta(contentColumn.rootRef.selectedType).placeholderName || "请输入因子名称"
                                    text: contentColumn.rootRef.factorName
                                    onTextChanged: contentColumn.rootRef.factorName = text
                                }
                            }
                            
                            // 因子描述
                            Column {
                                width: parent.width
                                spacing: 4
                                
                                Text {
                                    width: parent.width
                                    text: "因子描述 *"
                                    font.pixelSize: 12
                                    color: "#CBD5E1"
                                }
                                
                                TextArea {
                                    id: factorDescriptionField
                                    width: parent.width
                                    height: 60
                                    placeholderText: contentColumn.rootRef.factorUiMeta(contentColumn.rootRef.selectedType).placeholderDesc || "描述因子的计算方法、应用场景等..."
                                    wrapMode: Text.WordWrap
                                    text: contentColumn.rootRef.factorDescription
                                    onTextChanged: contentColumn.rootRef.factorDescription = text
                                }
                            }

                        }
                    }
                    
                    // 参数配置区域
                    Rectangle {
                        id: paramCard
                        width: parent.width
                        implicitHeight: paramCardColumn.implicitHeight + 24
                        height: implicitHeight
                        radius: 8
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                        
                        Column {
                            id: paramCardColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10
                            
                            Text {
                                width: parent.width
                                text: "⚙️ 参数配置"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#F1F5F9"
                            }
                            
                            // 参数说明
                            Text {
                                width: parent.width
                                text: "通用参数（所有因子类型共享）和核心参数（" + contentColumn.rootRef.selectedTypeName + " 类型特定）"
                                font.pixelSize: 11
                                color: "#94A3B8"
                                wrapMode: Text.WordWrap
                            }
                            
                            // 参数内容区域
                            Rectangle {
                                id: paramContentArea
                                width: parent.width
                                implicitHeight: dynamicGenerator.implicitHeight
                                height: implicitHeight
                                color: "transparent"
                                
                                PluginComponents.DynamicParamGenerator {
                                    id: dynamicGenerator
                                    width: parent.width
                                    height: implicitHeight
                                    itemSpacing: 8
                                    maxColumns: 3
                                    property string lastLoadedConfigDigest: ""
                                    property string lastLoadedValueDigest: ""

                                    function toDigest(value) {
                                        if (value === undefined || value === null) {
                                            return ""
                                        }
                                        try {
                                            return JSON.stringify(value)
                                        } catch (error) {
                                            return String(value)
                                        }
                                    }
                                    
                                    // 传入参数组件注册表实例
                                    paramRegistry: contentColumn.paramComponentsRef
                                    
                                    // 参数值变化
                                    onParamsChanged: function(newValues) {
                                        contentColumn.rootRef.factorParameters = contentColumn.rootRef.mergeSchemaParameterValues(
                                            contentColumn.rootRef.factorParameters,
                                            newValues,
                                            dynamicGenerator.configs
                                        )
                                    }
                                    
                                    // 验证状态变化
                                    onValidationChanged: function(allValid, errors) {
                                        contentColumn.rootRef.generatorValidationPassed = allValid
                                        contentColumn.rootRef.generatorValidationErrorCount = Object.keys(errors).length
                                        contentColumn.rootRef.updateValidationState()
                                    }
                                    
                                    // 加载参数配置方法
                                    function loadParamConfigs() {
                                        console.log("DynamicParamGenerator.loadParamConfigs 被调用")
                                        console.log("paramComponentsRef:", contentColumn.paramComponentsRef ? "已设置" : "未设置")
                                        console.log("currentParamConfigs:", Array.isArray(contentColumn.rootRef.currentParamConfigs) ? contentColumn.rootRef.currentParamConfigs.length : 0)
                                        
                                        if (!contentColumn.paramComponentsRef) {
                                            console.error("paramComponentsRef 未设置!")
                                            return
                                        }

                                        var newConfigs = Array.isArray(contentColumn.rootRef.currentParamConfigs)
                                                ? contentColumn.rootRef.currentParamConfigs
                                                : []
                                        var currentValues = contentColumn.rootRef.factorParameters || ({})
                                        var configDigest = toDigest(newConfigs)
                                        var valueDigest = toDigest(currentValues)

                                        if (configDigest === lastLoadedConfigDigest
                                                && valueDigest === lastLoadedValueDigest) {
                                            console.log("DynamicParamGenerator 跳过重复加载")
                                            return
                                        }

                                        lastLoadedConfigDigest = configDigest
                                        lastLoadedValueDigest = valueDigest
                                        
                                        console.log("转换后的参数配置数量:", newConfigs.length)
                                        
                                        // 使用 reloadConfigs 方法重新加载
                                        reloadConfigs(
                                            newConfigs,
                                            [],
                                            currentValues
                                        )
                                    }
                                    
                                    Component.onCompleted: {
                                        console.log("DynamicParamGenerator 在步骤2中初始化完成")
                                        // 延迟加载配置
                                        Qt.callLater(loadParamConfigs)
                                    }
                                }
                            }
                        }
                    }
                    
                    // 验证状态
                    Rectangle {
                        id: validationCard
                        width: parent.width
                        height: 40
                        radius: 6
                        color: contentColumn.rootRef.parametersValid ? "#10B98120" : "#EF444420"
                        border.color: contentColumn.rootRef.parametersValid ? "#10B981" : "#EF4444"
                        border.width: 1
                        
                        Row {
                            width: parent.width - 16
                            height: parent.height
                            anchors.centerIn: parent
                            spacing: 8
                            
                            Text {
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                text: contentColumn.rootRef.parametersValid ? "✔️" : "⚠️"
                                font.pixelSize: 14
                                color: contentColumn.rootRef.parametersValid ? "#10B981" : "#EF4444"
                            }
                            
                            Text {
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                text: contentColumn.rootRef.validationMessage || "等待参数输入..."
                                font.pixelSize: 12
                                color: contentColumn.rootRef.parametersValid ? "#10B981" : "#EF4444"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                    
                    // 监听参数配置变化
                    Connections {
                        target: root
                        function onCurrentParamConfigsChanged() {
                            console.log("检测到 currentParamConfigs 变化")
                            if (dynamicGenerator) {
                                Qt.callLater(dynamicGenerator.loadParamConfigs)
                            }
                        }
                    }
                }
            }
    }
    // 步骤3: 预览确认
    Component {
        id: step3Component
        
        ScrollView {
            id: step3ScrollView
            anchors.fill: parent
            clip: true
            contentHeight: contentColumn.height  // 关键：设置内容高度
            
            // 显示滚动条
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
            
            // 主内容列
            Column {
                id: contentColumn
                width: step3ScrollView.availableWidth
                spacing: 20
                // 因子摘要卡片 - 动态高度
                Rectangle {
                    id: factorSummaryCard
                    width: parent.width - 40
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: factorSummaryColumn.height + 24  // 动态高度
                    radius: 12
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    Column {
                        id: factorSummaryColumn
                        width: parent.width - 32
                        anchors.centerIn: parent
                        spacing: 12
                        
                        // 标题行
                        Row {
                            width: parent.width
                            spacing: 12
                            
                            Text {
                                text: "📊"
                                font.pixelSize: 24
                            }
                            
                            Column {
                                width: parent.width - 40
                                spacing: 6
                                
                                Text {
                                    width: parent.width
                                    text: root.factorName || "未命名因子"
                                    font.pixelSize: 18
                                    font.weight: Font.Bold
                                    color: "#F1F5F9"
                                    elide: Text.ElideRight
                                }
                                
                                Rectangle {
                                    width: 85
                                    height: 28
                                    radius: 14
                                    color: getTypeColor(root.selectedType)
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: root.selectedTypeName || "未知类型"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: "white"
                                    }
                                }
                            }
                        }
                        
                        // 描述区域
                        Rectangle {
                            width: parent.width
                            height: 80
                            radius: 8
                            color: "#1E293B"
                            border.width: 1
                            border.color: "#334155"
                            
                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 8
                                clip: true
                                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                                
                                Text {
                                    width: parent.width - 16
                                    text: root.factorDescription || "无描述"
                                    font.pixelSize: 13
                                    color: "#CBD5E1"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        
                        // 参数摘要 - 动态高度
                        Column {
                            id: paramSummaryColumn
                            width: parent.width
                            spacing: 8
                            visible: Object.keys(root.previewParameters).length > 0
                            
                            Text {
                                width: parent.width
                                text: "📋 参数配置摘要:"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#F1F5F9"
                            }
                            
                            Rectangle {
                                id: paramSummaryRect
                                width: parent.width
                                height: paramSummaryFlow.height + 24  // 动态高度
                                radius: 8
                                color: "#1E293B"
                                border.width: 1
                                border.color: "#334155"
                                
                                // 使用Flow布局，自动换行
                                Flow {
                                    id: paramSummaryFlow
                                    width: parent.width - 24
                                    anchors.centerIn: parent
                                    spacing: 16
                                    
                                    Repeater {
                                        model: Object.keys(root.previewParameters)
                                        
                                        delegate: Column {
                                            width: (paramSummaryFlow.width - 16) / 2  // 动态计算宽度
                                            spacing: 2
                                            
                                            Text {
                                                width: parent.width
                                                text: modelData + ":"
                                                font.pixelSize: 12
                                                color: "#CBD5E1"
                                                elide: Text.ElideRight
                                            }
                                            
                                            Text {
                                                width: parent.width
                                                text: root.formatParameterSummaryValue(root.previewParameters[modelData])
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: "#F59E0B"
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 创建说明卡片
                Rectangle {
                    width: parent.width - 40
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 120
                    radius: 12
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    Column {
                        width: parent.width - 32
                        anchors.centerIn: parent
                        spacing: 12
                        
                        Text {
                            width: parent.width
                            text: "✅ 创建说明"
                            font.pixelSize: 16
                            font.weight: Font.Medium
                            color: "#F1F5F9"
                        }
                        
                        Column {
                            width: parent.width
                            spacing: 6
                            
                            Text {
                                width: parent.width
                                text: "✔️ 因子将保存到因子库中"
                                font.pixelSize: 13
                                color: "#94A3B8"
                            }
                            
                            Text {
                                width: parent.width
                                text: "✔️ 可以立即进行回测分析"
                                font.pixelSize: 13
                                color: "#94A3B8"
                            }
                            
                            Text {
                                width: parent.width
                                text: "✔️ 支持添加到策略中使用"
                                font.pixelSize: 13
                                color: "#94A3B8"
                            }
                        }
                    }
                }
                // 底部间距
                Item {
                    width: parent.width
                    height: 20
                }
            }
        }
    }
    
    // ============ 工具函数 ============
    
    // 获取步骤标签
    function getStepLabel(stepIndex) {
        var labels = ["选择类型", "配置因子", "预览确认"]
        return labels[stepIndex] || ""
    }
    
    // 获取步骤标题
    function getStepTitle(stepIndex) {
        var titles = [
            root.editMode ? "调整因子类型" : "选择因子类型",
            root.editMode ? "编辑因子信息与参数" : "配置因子信息与参数", 
            root.editMode ? "预览并保存修改" : "预览并确认"
        ]
        return titles[stepIndex] || ""
    }
    
    // 获取步骤描述
    function getStepDescription(stepIndex) {
        var descriptions = [
            root.editMode
                ? "可以调整当前因子的类型，切换类型后会重新加载该类型的参数配置"
                : "选择适合您策略的因子类型，不同类型的因子有不同的参数配置",
            root.editMode
                ? "修改因子基本信息和参数配置，保存后会覆盖原有定义"
                : "填写因子基本信息并配置参数，这些参数将影响因子的计算",
            root.editMode ? "确认所有信息无误后保存修改" : "确认所有信息无误后创建因子"
        ]
        return descriptions[stepIndex] || ""
    }
    
    // 获取步骤组件
    function getStepComponent(stepIndex) {
        switch(stepIndex) {
            case 0: return step1Component
            case 1: return step2Component
            case 2: return step3Component
            default: return step1Component
        }
    }
    
    // 检查步骤是否有效
    function isStepValid(stepIndex) {
        switch(stepIndex) {
            case 0: return hasSelectedType()
            case 1: return root.factorName.trim() !== "" && root.factorDescription.trim() !== "" && root.parametersValid
            case 2: return true  // 最后一步总是有效的
            default: return false
        }
    }

    function hasSelectedType() {
        return factorTypeEnumId(root.selectedType) >= 0
    }
    
    // 获取类型名称
    function normalizeTypeId(typeId) {
        var meta = factorUiMeta(typeId)
        return meta.id !== undefined && meta.id !== null ? String(meta.id) : ""
    }

    function factorTypeEnumId(typeId) {
        var meta = factorUiMeta(typeId)
        if (meta.factorType === undefined || meta.factorType === null) {
            return -1
        }
        return Number(meta.factorType)
    }

    function factorUiMetaList() {
        if (!Bridge.FactorMetaService || typeof Bridge.FactorMetaService.getAllFactorUiMeta !== "function") {
            return []
        }
        var items = Bridge.FactorMetaService.getAllFactorUiMeta()
        return Array.isArray(items) ? items : []
    }

    function factorUiMeta(typeId) {
        if (!Bridge.FactorMetaService || typeof Bridge.FactorMetaService.getFactorUiMeta !== "function") {
            return ({})
        }
        var meta = Bridge.FactorMetaService.getFactorUiMeta(typeId)
        return meta && typeof meta === "object" ? meta : ({})
    }

    function factorParameterConfigs(typeId) {
        if (!Bridge.FactorMetaService || typeof Bridge.FactorMetaService.getFactorParameterConfigs !== "function") {
            return []
        }
        var configs = Bridge.FactorMetaService.getFactorParameterConfigs(typeId)
        return Array.isArray(configs) ? configs : []
    }

    function getTypeName(typeId) {
        var meta = factorUiMeta(typeId)
        return meta.displayName !== undefined && meta.displayName !== null
            ? String(meta.displayName)
            : String(typeId || "")
    }
    
    // 获取类型颜色
    function getTypeColor(typeId) {
        var meta = factorUiMeta(typeId)
        return meta.color !== undefined && meta.color !== null ? String(meta.color) : "#94A3B8"
    }
    
    // 生成默认内容
    function generateDefaultContent(factorType) {
        console.log("为因子类型生成默认内容:", factorType)

        var defaultContent = factorUiMeta(factorType)
        if (!defaultContent || Object.keys(defaultContent).length === 0) {
            console.warn("未找到因子类型的默认内容配置:", factorType)
            return
        }

        // 如果当前因子名称为空，则使用默认名称
        if (!root.factorName || root.factorName.trim() === "") {
            root.factorName = String(defaultContent.displayName || "")
            console.log("自动设置因子名称:", defaultContent.displayName)
        }

        // 如果当前因子描述为空，则使用默认描述
        if (!root.factorDescription || root.factorDescription.trim() === "") {
            root.factorDescription = String(defaultContent.description || "")
            console.log("自动设置因子描述:", defaultContent.description)
        }
    }
    
    // 获取参数分类
    function getParameterCategories() {
        var categories = {
            "common": {
                name: "通用参数",
                description: "所有因子类型共享的基础参数",
                color: "#3B82F6",
                icon: "🔧"
            },
            "core": {
                name: "核心参数",
                description: root.selectedTypeName + " 类型特定的关键参数",
                color: "#10B981",
                icon: "⚡"
            }
        }
        return categories
    }

    function loadParamConfigsForType(factorType) {
        var normalizedFactorType = factorTypeEnumId(factorType)
        console.log("开始加载因子类型参数配置:", normalizedFactorType)

        if (normalizedFactorType < 0) {
            root.currentParamConfigs = []
            return
        }

        if (!root.applyingEditingFactorData && root.currentStep === 0) {
            root.currentStep = 1
        }

        var configs = factorParameterConfigs(normalizedFactorType)
        root.currentParamConfigs = Array.isArray(configs) ? configs : []
        console.log("参数配置加载完成，参数数量:", root.currentParamConfigs.length)
    }
    
    // 更新验证状态
    function updateValidationState() {
        var normalizedParameters = buildSubmittedParameters(false)

        // 检查是否有参数
        var hasParameters = Object.keys(normalizedParameters).length > 0

        if (!hasSelectedType()) {
            root.parametersValid = false
            root.validationMessage = "请选择因子类型"
            return
        }

        if (!hasParameters) {
            root.parametersValid = false
            root.validationMessage = "请配置参数"
            return
        }
        
        // 检查必填参数
        var requiredParams = []
        var activeConfigs = Array.isArray(root.currentParamConfigs) ? root.currentParamConfigs : []
        for (var configIndex = 0; configIndex < activeConfigs.length; configIndex++) {
            var config = activeConfigs[configIndex]
            if (config && config.id && config.required === true) {
                requiredParams.push(config.id)
            }
        }
        
        var allRequiredFilled = requiredParams.every(function(key) {
            return isParameterFilled(normalizedParameters[key])
        })

        if (!root.generatorValidationPassed) {
            root.parametersValid = false
            root.validationMessage = "存在验证错误 " + root.generatorValidationErrorCount + " 个"
            return
        }
        
        root.parametersValid = hasParameters && allRequiredFilled
        root.validationMessage = root.parametersValid ? 
            "✔️ 参数验证通过" : 
            "⚠️ 请填写所有必填参数"
    }

    function isParameterFilled(value) {
        if (value === undefined || value === null) {
            return false
        }

        if (typeof value === "string") {
            return value.trim() !== ""
        }

        if (Array.isArray(value)) {
            return value.length > 0
        }

        if (typeof value === "object") {
            return Object.keys(value).length > 0
        }

        return true
    }

    function normalizeSubmittedParameterValue(value, dropEmptyValues) {
        return normalizeSubmittedParameterValueWithKey(value, dropEmptyValues, "")
    }

    function normalizeSubmittedParameterValueWithKey(value, dropEmptyValues, currentKey) {
        if (value === undefined || value === null) {
            return undefined
        }

        if (typeof value === "string") {
            var trimmedValue = value.trim()

            if (normalizeTypeId(root.selectedType) === "custom" && currentKey === "variables") {
                if (trimmedValue === "") {
                    return dropEmptyValues ? undefined : []
                }

                try {
                    return normalizeSubmittedParameterValueWithKey(JSON.parse(trimmedValue), dropEmptyValues, currentKey)
                } catch (error) {
                    return trimmedValue
                }
            }

            if (dropEmptyValues && trimmedValue === "") {
                return undefined
            }

            var loweredValue = trimmedValue.toLowerCase()
            if (loweredValue === "true") {
                return true
            }
            if (loweredValue === "false") {
                return false
            }
            return trimmedValue
        }

        if (Array.isArray(value)) {
            var normalizedArray = []
            for (var arrayIndex = 0; arrayIndex < value.length; arrayIndex++) {
                var normalizedItem = normalizeSubmittedParameterValueWithKey(value[arrayIndex], dropEmptyValues, currentKey)
                if (normalizedItem !== undefined) {
                    normalizedArray.push(normalizedItem)
                }
            }
            return dropEmptyValues && normalizedArray.length === 0 ? undefined : normalizedArray
        }

        if (typeof value === "object") {
            var normalizedObject = {}
            for (var key in value) {
                var normalizedProperty = normalizeSubmittedParameterValueWithKey(value[key], dropEmptyValues, key)
                if (normalizedProperty !== undefined) {
                    normalizedObject[key] = normalizedProperty
                }
            }
            return dropEmptyValues && Object.keys(normalizedObject).length === 0 ? undefined : normalizedObject
        }

        return value
    }

    function isMeaningfulParameterValue(value) {
        if (value === undefined || value === null) {
            return false
        }

        if (typeof value === "string") {
            return value.trim() !== ""
        }

        if (Array.isArray(value)) {
            return value.length > 0
        }

        if (typeof value === "object") {
            return Object.keys(value).length > 0
        }

        return true
    }

    function buildSubmittedParameters(dropEmptyValues) {
        var normalizedParameters = normalizeSubmittedParameterValue(root.factorParameters, dropEmptyValues)
        return normalizedParameters || {}
    }

    function mergeSchemaParameterValues(existingValues, newValues, activeConfigs) {
        var mergedValues = {}
        var visibleParamIds = ({})
        var currentValues = existingValues || ({})
        var updatedValues = newValues || ({})
        var configs = activeConfigs || []

        for (var configIndex = 0; configIndex < configs.length; configIndex++) {
            var config = configs[configIndex]
            if (config && config.id) {
                visibleParamIds[config.id] = true
            }
        }

        for (var existingKey in currentValues) {
            if (!visibleParamIds[existingKey]) {
                mergedValues[existingKey] = cloneEditableValue(currentValues[existingKey])
            }
        }

        for (var newKey in updatedValues) {
            mergedValues[newKey] = cloneEditableValue(updatedValues[newKey])
        }

        return mergedValues
    }

    function cloneEditableValue(value) {
        if (value === undefined || value === null) {
            return value
        }

        if (Array.isArray(value)) {
            return value.slice()
        }

        if (typeof value === "object") {
            return Object.assign({}, value)
        }

        return value
    }

    function resolveConfigOptionValue(rawValue, options) {
        if (!Array.isArray(options)) {
            return rawValue
        }

        var candidate = rawValue
        if (candidate && typeof candidate === "object") {
            if (candidate.value !== undefined) {
                candidate = candidate.value
            } else if (candidate.label !== undefined) {
                candidate = candidate.label
            }
        }

        var candidateText = String(candidate === undefined || candidate === null ? "" : candidate).trim()
        for (var index = 0; index < options.length; index++) {
            var option = options[index]
            if (!option) {
                continue
            }

            if (typeof option !== "object") {
                if (option === candidate || String(option).trim() === candidateText) {
                    return option
                }
                continue
            }

            if (option.value === candidate || String(option.value).trim() === candidateText) {
                return option.value
            }
        }

        return rawValue
    }

    function normalizeConfigValue(rawValue, config) {
        if (!config || rawValue === undefined || rawValue === null) {
            return cloneEditableValue(rawValue)
        }

        if (config.serializeAsJson) {
            if (typeof rawValue === "string") {
                return rawValue
            }
            try {
                return JSON.stringify(rawValue, null, 2)
            } catch (error) {
                return String(rawValue)
            }
        }

        if (config.type === "multiselect" && Array.isArray(config.options)) {
            var sourceValues = Array.isArray(rawValue)
                    ? rawValue
                    : (rawValue === "" ? [] : [rawValue])
            var normalizedValues = []
            for (var arrayIndex = 0; arrayIndex < sourceValues.length; arrayIndex++) {
                var normalizedItem = resolveConfigOptionValue(sourceValues[arrayIndex], config.options)
                if (normalizedValues.indexOf(normalizedItem) < 0) {
                    normalizedValues.push(normalizedItem)
                }
            }
            return normalizedValues
        }

        if (config.type === "select" && Array.isArray(config.options)) {
            return resolveConfigOptionValue(rawValue, config.options)
        }

        return cloneEditableValue(rawValue)
    }

    function normalizeEditableParameterValues(parameters, factorType) {
        var normalizedParameters = cloneEditableValue(parameters || ({}))
        var normalizedType = factorTypeEnumId(factorType)
        var configs = Array.isArray(root.currentParamConfigs) && root.currentParamConfigs.length > 0
                ? root.currentParamConfigs
                : factorParameterConfigs(normalizedType)

        if (!Array.isArray(configs) || configs.length === 0) {
            return normalizedParameters
        }

        var configMap = ({})
        for (var configIndex = 0; configIndex < configs.length; configIndex++) {
            var config = configs[configIndex]
            if (config && config.id) {
                configMap[config.id] = config
            }
        }

        for (var key in normalizedParameters) {
            if (!configMap.hasOwnProperty(key)) {
                continue
            }
            normalizedParameters[key] = normalizeConfigValue(normalizedParameters[key], configMap[key])
        }

        return normalizedParameters
    }

    function hasParameterPayload(parameters) {
        return parameters && typeof parameters === "object" && Object.keys(parameters).length > 0
    }

    function applyEditingFactorData() {
        if (!root.editMode) {
            return
        }

        var factorData = root.editingFactorData || ({})
        var rawFactorType = factorData.factorType !== undefined && factorData.factorType !== null
            ? factorData.factorType
            : null
        var nextType = factorTypeEnumId(rawFactorType)
        var missingTypeInfo = nextType < 0
            || (factorData.factorType === undefined || factorData.factorType === null)

        if ((!hasParameterPayload(factorData.parameters) || missingTypeInfo)
                && root.factorService
                && typeof root.factorService.getFactorById === "function") {
            var detailFactor = root.factorService.getFactorById(root.editingFactorId) || ({})
            if (detailFactor && typeof detailFactor === "object") {
                factorData = Object.assign({}, factorData, detailFactor)
            rawFactorType = factorData.factorType !== undefined && factorData.factorType !== null
                ? factorData.factorType
                : null
            nextType = factorTypeEnumId(rawFactorType)
            }
        }

        root.applyingEditingFactorData = true

        var existingParameters = cloneEditableValue(factorData.parameters || ({}))
        existingParameters = normalizeEditableParameterValues(existingParameters, rawFactorType)
        root.factorParameters = existingParameters || {}
        if (nextType >= 0) {
            root.selectedType = -1
            root.selectedType = nextType
        }

        root.factorName = String(factorData.displayName || factorData.factorName || "")
        root.factorDescription = String(factorData.description || "")
        root.factorTags = Array.isArray(factorData.tags) ? factorData.tags.slice() : []
        root.currentStep = 1
        root.generatorValidationPassed = true
        root.generatorValidationErrorCount = 0
        root.updateValidationState()

        root.applyingEditingFactorData = false
    }

    function formatParameterSummaryValue(value) {
        if (value === undefined || value === null) {
            return "-"
        }

        if (typeof value === "object") {
            try {
                return JSON.stringify(value)
            } catch (error) {
                return String(value)
            }
        }

        return String(value)
    }

    function resolveFactorMutationFailureMessage(defaultMessage) {
        var fallback = defaultMessage || "❌ 操作失败"
        if (!root.factorService || !root.factorService.lastOperationReport) {
            return fallback
        }

        var report = root.factorService.lastOperationReport
        var detail = report.message !== undefined && report.message !== null
            ? String(report.message).trim()
            : ""

        return detail.length > 0 ? ("❌ " + detail) : fallback
    }
    
    // 新增因子
    function createFactor() {
        if (root.factorMutationInProgress) {
            showToast("⏳ 因子服务正在处理写操作，请稍候")
            return
        }

        // 1. 验证表单
        if (!validateForm()) {
            return
        }
        
        // 2. 获取当前时间
        var currentDate = new Date()
        var dateStr = currentDate.toISOString().split('T')[0]
        
        // 3. 构建完整的因子数据
        var factorData = buildCompleteFactorData(dateStr)

        console.log("创建因子数据:", factorData)

        // 4. 通过FactorService新增因子
        if (root.factorService) {
            console.log("调用 factorService.addFactor...")
            var factorId = root.factorService.addFactor(factorData)

            if (factorId && factorId !== "") {
                console.log("✔️ 因子创建成功，ID:", factorId)

                var createdFactorData = Object.assign({}, factorData)
                createdFactorData.factorId = factorId
                createdFactorData.operation = "create"
                root.factorCreated(createdFactorData)
            } else {
                console.log("❌ 因子创建失败")
                showToast(resolveFactorMutationFailureMessage("❌ 因子创建失败"))
                resetForm()
            }
        } else {
            console.log("❌ factorService 未初始化")
            showToast("❌ 因子服务未初始化，无法保存因子")
            resetForm()
        }
    }

    // 编辑因子
    function updateFactor() {
        if (root.factorMutationInProgress) {
            showToast("⏳ 因子服务正在处理写操作，请稍候")
            return
        }

        if (!root.editMode || !root.editingFactorId) {
            showToast("❌ 当前不是编辑状态，无法保存修改")
            return
        }

        if (!validateForm()) {
            return
        }

        var currentDate = new Date()
        var dateStr = currentDate.toISOString().split('T')[0]
        var factorData = buildCompleteFactorData(dateStr)

        console.log("更新因子数据:", factorData)

        if (root.factorService) {
            console.log("调用 factorService.updateFactor...", root.editingFactorId)
            var updateSuccess = root.factorService.updateFactor(root.editingFactorId, factorData)

            if (updateSuccess) {
                console.log("✔️ 因子更新成功，ID:", root.editingFactorId)

                var updatedFactorData = Object.assign({}, factorData)
                updatedFactorData.factorId = root.editingFactorId
                updatedFactorData.operation = "update"
                root.factorCreated(updatedFactorData)
            } else {
                console.log("❌ 因子更新失败")
                showToast(resolveFactorMutationFailureMessage("❌ 因子更新失败"))
            }
        } else {
            console.log("❌ factorService 未初始化")
            showToast("❌ 因子服务未初始化，无法保存修改")
        }
    }
    
    // 表单验证
    function validateForm() {
        // 1. 检查必填字段
        if (!root.factorName || root.factorName.trim() === "") {
            showToast("❌ 请输入因子名称")
            return false
        }
        
        if (!root.factorDescription || root.factorDescription.trim() === "") {
            showToast("❌ 请输入因子描述")
            return false
        }
        
        if (!hasSelectedType()) {
            showToast("❌ 请选择因子类型")
            return false
        }
        
        // 2. 检查参数有效性
        if (!root.parametersValid) {
            showToast("❌ 请配置有效的参数")
            return false
        }
        
        return true
    }
    
    // 构建完整的因子数据
    function buildCompleteFactorData(dateStr) {
        var originalFactor = root.editingFactorData || ({})
        var submittedParameters = buildSubmittedParameters(true)
        var normalizedSelectedType = normalizeTypeId(root.selectedType)
        var normalizedTypeName = getTypeName(root.selectedType)
        var normalizedFactorTypeId = factorTypeEnumId(root.selectedType)
        var typeDisplayTag = normalizedTypeName

        // 构建完整的因子数据 - 与数据库表结构匹配
        var factorData = {
            factorName: root.factorName.toLowerCase().replace(/\s+/g, '_'),
            displayName: root.factorName.trim(),
            factorType: normalizedFactorTypeId,
            description: root.factorDescription.trim(),
            icValue: originalFactor.icValue !== undefined ? originalFactor.icValue : 0.0,
            irValue: originalFactor.irValue !== undefined ? originalFactor.irValue : 0.0,
            validityDays: originalFactor.validityDays !== undefined ? originalFactor.validityDays : 30,
            isRecommended: originalFactor.isRecommended === true,
            isFavorite: originalFactor.isFavorite === true,
            status: originalFactor.status || "ACTIVE",
            tags: Array.from(new Set(root.factorTags.concat([typeDisplayTag]).filter(function(tag) {
                return tag !== undefined && tag !== null && String(tag).trim() !== ""
            }))),
            creator: originalFactor.creator || "system",
            groupReturns: Array.isArray(originalFactor.groupReturns) ? originalFactor.groupReturns : [],
            parameters: submittedParameters
        }

        if (root.editMode && originalFactor.createDate) {
            factorData.createDate = originalFactor.createDate
        }

        if (root.editMode) {
            factorData.factorId = root.editingFactorId
        }

        if (originalFactor.turnoverRate !== undefined && originalFactor.turnoverRate !== null) {
            var preservedTurnoverRate = Number(originalFactor.turnoverRate)
            if (!isNaN(preservedTurnoverRate)) {
                factorData.turnoverRate = preservedTurnoverRate
            }
        }
        
        return factorData
    }

    function clearStepDraft(stepIndex) {
        if (stepIndex === 1) {
            root.factorName = ""
            root.factorDescription = ""
            root.factorParameters = {}
            root.factorTags = []
            root.generatorValidationPassed = true
            root.generatorValidationErrorCount = 0
            root.updateValidationState()
        }
    }
    
    // 重置表单
    function resetForm() {
        root.factorName = ""
        root.factorDescription = ""
        root.selectedType = -1
        root.factorParameters = {}
        root.factorTags = []
        root.currentStep = 0
        root.currentParamConfigs = []
        root.generatorValidationPassed = true
        root.generatorValidationErrorCount = 0
        root.updateValidationState()
    }
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
        root.toastRequested(message)
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("CreationPagePluginIntegrated 初始化完成")

        if (root.editMode) {
            applyEditingFactorData()
        }
    }

    onEditingFactorDataChanged: {
        if (root.editMode) {
            applyEditingFactorData()
        }
    }

    onFactorServiceChanged: {
        if (hasSelectedType()) {
            loadParamConfigsForType(root.selectedType)
        }
    }
    
    // 监听因子类型变化
    onSelectedTypeChanged: {
        console.log("因子类型变化:", selectedType)
        
        if (hasSelectedType()) {
            root.generatorValidationPassed = true
            root.generatorValidationErrorCount = 0
            
            // 加载对应类型的参数配置
            loadParamConfigsForType(selectedType)

            if (!root.applyingEditingFactorData && currentStep === 0) {
                currentStep = 1
            }
            
            if (!root.applyingEditingFactorData) {
                root.factorParameters = {}

                // 自动生成默认内容
                generateDefaultContent(selectedType)
            }

            root.updateValidationState()
        }
    }
    
    // 监听参数变化
    onFactorParametersChanged: {
        console.log("参数变化:", Object.keys(root.factorParameters).length, "个参数")
        updateValidationState()
    }
    Rectangle {
        anchors.fill: parent
        visible: root.factorMutationInProgress
        z: 200
        color: "#020617"
        opacity: 0.32
    }

    MouseArea {
        anchors.fill: parent
        visible: root.factorMutationInProgress
        enabled: root.factorMutationInProgress
        z: 201
        cursorShape: Qt.BusyCursor
    }

    Column {
        anchors.centerIn: parent
        visible: root.factorMutationInProgress
        z: 202
        spacing: 10

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: root.factorMutationInProgress
            width: 42
            height: 42
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "因子服务正在处理写操作，请稍候"
            font.pixelSize: 14
            font.bold: true
            color: "#F8FAFC"
        }
    }
}
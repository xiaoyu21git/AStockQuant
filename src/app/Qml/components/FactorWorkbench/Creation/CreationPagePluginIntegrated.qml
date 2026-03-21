// CreationPagePluginIntegrated_fix.qml
// 修复动态参数无法创建的问题

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import ConsoleUi 1.0
import "../../Factor" as FactorComponents
import "../../../utils/FactorSchemaLoader.js" as SchemaLoader
import "./components" as PluginComponents

Rectangle {
    id: root
    color: "#0F172A"
    
    // ============ 页面属性 ============
    
    property var globalDataService
    property var factorParamController
    property Bridge.FactorService factorService: null
    property var factorDataModel: null
    
    // 当前步骤
    property int currentStep: 0
    readonly property int totalSteps: 3
    
    // 因子数据
    property string selectedType: ""
    property string selectedTypeName: getTypeName(selectedType)
    property string factorName: ""
    property string factorDescription: ""
    property var factorParameters: ({})
    property var factorTags: []
    
    // 默认内容生成
    property var defaultContentMap: ({
        "value": {
            name: "价值因子",
            description: "基于市盈率、市净率等估值指标构建的价值因子",
            placeholderName: "例如：低估值组合因子",
            placeholderDesc: "描述价值因子的计算方法、应用场景等..."
        },
        "momentum": {
            name: "动量因子",
            description: "基于价格动量、收益率趋势构建的动量因子",
            placeholderName: "例如：60日动量因子",
            placeholderDesc: "描述动量因子的计算方法、应用场景等..."
        },
        "quality": {
            name: "质量因子",
            description: "基于财务健康、盈利能力构建的质量因子",
            placeholderName: "例如：高ROE质量因子",
            placeholderDesc: "描述质量因子的计算方法、应用场景等..."
        },
        "growth": {
            name: "成长因子",
            description: "基于营收、利润增长率构建的成长因子",
            placeholderName: "例如：高增长潜力因子",
            placeholderDesc: "描述成长因子的计算方法、应用场景等..."
        },
        "size": {
            name: "规模因子",
            description: "基于市值规模、流通市值构建的规模因子",
            placeholderName: "例如：小市值因子",
            placeholderDesc: "描述规模因子的计算方法、应用场景等..."
        },
        "low_volatility": {
            name: "低波因子",
            description: "基于波动率、贝塔值构建的低波因子",
            placeholderName: "例如：低波动率组合",
            placeholderDesc: "描述低波因子的计算方法、应用场景等..."
        },
        "dividend": {
            name: "红利因子",
            description: "基于股息率、股息支付率构建的红利因子",
            placeholderName: "例如：高股息率组合",
            placeholderDesc: "描述红利因子的计算方法、应用场景等..."
        },
        "technical": {
            name: "技术因子",
            description: "基于RSI、MACD等技术指标构建的技术因子",
            placeholderName: "例如：RSI超卖信号",
            placeholderDesc: "描述技术因子的计算方法、应用场景等..."
        },
        "macro_sector": {
            name: "宏观/行业因子",
            description: "基于行业轮动、宏观周期构建的因子",
            placeholderName: "例如：行业轮动因子",
            placeholderDesc: "描述宏观/行业因子的计算方法、应用场景等..."
        },
        "liquidity": {
            name: "流动性因子",
            description: "基于换手率、买卖价差构建的流动性因子",
            placeholderName: "例如：高流动性组合",
            placeholderDesc: "描述流动性因子的计算方法、应用场景等..."
        },
        "sentiment": {
            name: "情绪因子",
            description: "基于新闻情感、社交媒体构建的情绪因子",
            placeholderName: "例如：市场情绪指标",
            placeholderDesc: "描述情绪因子的计算方法、应用场景等..."
        },
        "custom": {
            name: "自定义因子",
            description: "用户自定义表达式构建的因子",
            placeholderName: "例如：自定义组合因子",
            placeholderDesc: "描述自定义因子的计算方法、应用场景等..."
        }
    })
    
    // 插件化组件注册表（静态声明，替代动态创建）
    PluginComponents.ParamComponents {
        id: paramComponents
    }
    property var factorSchemas: null
    property var currentSchema: null
    property bool schemasLoaded: false
    property bool parametersValid: false
    property string validationMessage: ""
    
    // 信号
    signal factorCreated(var factorData)
    signal backClicked()
    signal typeChanged(string type)
    
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
                text: "📝 创建新因子"
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
                            color: currentStep > 0 ? "#334155" : "transparent"
                            border.width: currentStep > 0 ? 1 : 0
                            border.color: "#475569"
                            visible: currentStep > 0
                            
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
                                cursorShape: Qt.PointingHandCursor
                                enabled: currentStep > 0
                                onClicked: {
                                    if (currentStep > 0) currentStep--
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
                            color: isStepValid(currentStep) ? "#3B82F6" : "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: currentStep === totalSteps - 1 ? "✓" : "→"
                                    font.pixelSize: 14
                                    color: isStepValid(currentStep) ? "white" : "#94A3B8"
                                }
                                
                                Text {
                                    text: currentStep === totalSteps - 1 ? "创建因子" : "下一步"
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    color: isStepValid(currentStep) ? "white" : "#94A3B8"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: isStepValid(currentStep)
                                onClicked: {
                                    if (currentStep < totalSteps - 1) {
                                        currentStep++
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
                
                // 类型选择网格 - 显示所有12个因子类型，10列布局充分利用宽度
                GridLayout {
                    id: typeGrid
                    columns: 10  // 10列布局，充分利用宽度
                    columnSpacing: 8
                    rowSpacing: 8
                    Layout.fillWidth: true
                    
                    // 确保每列均匀分配宽度
                    //uniformCellWidths: true
                    
                    // 价值因子
                    FactorTypeCard {
                        typeId: "value"
                        displayName: "价值因子"
                        description: "市盈率、市净率等估值指标"
                        icon: "💰"
                        color: "#F59E0B"
                        isSelected: root.selectedType === "value"
                        onClicked: {
                            root.selectedType = "value"
                            root.typeChanged("value")
                            loadSchemaForType("value")
                        }
                    }
                    
                    // 动量因子
                    FactorTypeCard {
                        typeId: "momentum"
                        displayName: "动量因子"
                        description: "价格动量、收益率趋势"
                        icon: "📈"
                        color: "#3B82F6"
                        isSelected: root.selectedType === "momentum"
                        onClicked: {
                            root.selectedType = "momentum"
                            root.typeChanged("momentum")
                            loadSchemaForType("momentum")
                        }
                    }
                    
                    // 规模因子
                    FactorTypeCard {
                        typeId: "size"
                        displayName: "规模因子"
                        description: "市值规模、流通市值"
                        icon: "📏"
                        color: "#8B5CF6"
                        isSelected: root.selectedType === "size"
                        onClicked: {
                            root.selectedType = "size"
                            root.typeChanged("size")
                            loadSchemaForType("size")
                        }
                    }
                    
                    // 质量因子
                    FactorTypeCard {
                        typeId: "quality"
                        displayName: "质量因子"
                        description: "财务健康、盈利能力"
                        icon: "🏆"
                        color: "#10B981"
                        isSelected: root.selectedType === "quality"
                        onClicked: {
                            root.selectedType = "quality"
                            root.typeChanged("quality")
                            loadSchemaForType("quality")
                        }
                    }
                    
                    // 低波因子
                    FactorTypeCard {
                        typeId: "low_volatility"
                        displayName: "低波因子"
                        description: "波动率、贝塔值"
                        icon: "📉"
                        color: "#06B6D4"
                        isSelected: root.selectedType === "low_volatility"
                        onClicked: {
                            root.selectedType = "low_volatility"
                            root.typeChanged("low_volatility")
                            loadSchemaForType("low_volatility")
                        }
                    }
                    
                    // 成长因子
                    FactorTypeCard {
                        typeId: "growth"
                        displayName: "成长因子"
                        description: "营收、利润增长率"
                        icon: "🚀"
                        color: "#8B5CF6"
                        isSelected: root.selectedType === "growth"
                        onClicked: {
                            root.selectedType = "growth"
                            root.typeChanged("growth")
                            loadSchemaForType("growth")
                        }
                    }
                    
                    // 红利因子
                    FactorTypeCard {
                        typeId: "dividend"
                        displayName: "红利因子"
                        description: "股息率、股息支付率"
                        icon: "💵"
                        color: "#EC4899"
                        isSelected: root.selectedType === "dividend"
                        onClicked: {
                            root.selectedType = "dividend"
                            root.typeChanged("dividend")
                            loadSchemaForType("dividend")
                        }
                    }
                    
                    // 技术因子
                    FactorTypeCard {
                        typeId: "technical"
                        displayName: "技术因子"
                        description: "RSI、MACD等技术指标"
                        icon: "📊"
                        color: "#EF4444"
                        isSelected: root.selectedType === "technical"
                        onClicked: {
                            root.selectedType = "technical"
                            root.typeChanged("technical")
                            loadSchemaForType("technical")
                        }
                    }
                    
                    // 宏观/行业因子
                    FactorTypeCard {
                        typeId: "macro_sector"
                        displayName: "宏观/行业"
                        description: "行业轮动、宏观周期"
                        icon: "🌐"
                        color: "#F97316"
                        isSelected: root.selectedType === "macro_sector"
                        onClicked: {
                            root.selectedType = "macro_sector"
                            root.typeChanged("macro_sector")
                            loadSchemaForType("macro_sector")
                        }
                    }
                    
                    // 流动性因子
                    FactorTypeCard {
                        typeId: "liquidity"
                        displayName: "流动性因子"
                        description: "换手率、买卖价差"
                        icon: "💧"
                        color: "#8B5CF6"
                        isSelected: root.selectedType === "liquidity"
                        onClicked: {
                            root.selectedType = "liquidity"
                            root.typeChanged("liquidity")
                            loadSchemaForType("liquidity")
                        }
                    }
                    
                    // 情绪因子
                    FactorTypeCard {
                        typeId: "sentiment"
                        displayName: "情绪因子"
                        description: "新闻情感、社交媒体"
                        icon: "😊"
                        color: "#EC4899"
                        isSelected: root.selectedType === "sentiment"
                        onClicked: {
                            root.selectedType = "sentiment"
                            root.typeChanged("sentiment")
                            loadSchemaForType("sentiment")
                        }
                    }
                    
                    // 自定义因子
                    FactorTypeCard {
                        typeId: "custom"
                        displayName: "自定义"
                        description: "用户自定义表达式"
                        icon: "🛠"
                        color: "#94A3B8"
                        isSelected: root.selectedType === "custom"
                        onClicked: {
                            root.selectedType = "custom"
                            root.typeChanged("custom")
                            loadSchemaForType("custom")
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
                        height: 220
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
                                    width: parent.width
                                    height: 25
                                    placeholderText: contentColumn.rootRef.defaultContentMap[contentColumn.rootRef.selectedType] ? 
                                        contentColumn.rootRef.defaultContentMap[contentColumn.rootRef.selectedType].placeholderName : "请输入因子名称"
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
                                    width: parent.width
                                    height: 60
                                    placeholderText: contentColumn.rootRef.defaultContentMap[contentColumn.rootRef.selectedType] ? 
                                        contentColumn.rootRef.defaultContentMap[contentColumn.rootRef.selectedType].placeholderDesc : "描述因子的计算方法、应用场景等..."
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
                        height: dynamicGenerator.implicitHeight + 80  // 动态高度
                        radius: 8
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                        
                        Column {
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
                                width: parent.width
                                height: parent.height - 50
                                color: "transparent"
                                
                                PluginComponents.DynamicParamGenerator {
                                    id: dynamicGenerator
                                    anchors.fill: parent
                                    itemSpacing: 8
                                    
                                    // 传入参数组件注册表实例
                                    paramRegistry: contentColumn.paramComponentsRef
                                    
                                    // 参数值变化
                                    onParamsChanged: function(newValues) {
                                        contentColumn.rootRef.factorParameters = newValues
                                        contentColumn.rootRef.updateValidationState()
                                    }
                                    
                                    // 验证状态变化
                                    onValidationChanged: function(allValid, errors) {
                                        contentColumn.rootRef.parametersValid = allValid
                                        contentColumn.rootRef.validationMessage = allValid ? "参数验证通过" : 
                                            "存在验证错误" + Object.keys(errors).length + "个"
                                    }
                                    
                                    // 加载参数配置方法
                                    function loadParamConfigs() {
                                        console.log("DynamicParamGenerator.loadParamConfigs 被调用")
                                        console.log("paramComponentsRef:", contentColumn.paramComponentsRef ? "已设置" : "未设置")
                                        console.log("currentSchema:", contentColumn.rootRef.currentSchema ? "已加载" : "未加载")
                                        
                                        if (!contentColumn.paramComponentsRef) {
                                            console.error("paramComponentsRef 未设置!")
                                            return
                                        }
                                        
                                        if (!contentColumn.rootRef.currentSchema) {
                                            console.warn("currentSchema 为空")
                                            return
                                        }
                                        
                                        // 转换schema为配置
                                        var newConfigs = contentColumn.paramComponentsRef.schemaToConfigs(
                                            contentColumn.rootRef.currentSchema
                                        )
                                        
                                        console.log("转换后的参数配置数量:", newConfigs.length)
                                        
                                        // 使用 reloadConfigs 方法重新加载
                                        reloadConfigs(newConfigs, [])
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
                    
                    // 监听 currentSchema 变化
                    Connections {
                        target: root
                        function onCurrentSchemaChanged() {
                            console.log("检测到 currentSchema 变化")
                            if (dynamicGenerator && root.currentSchema) {
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
                            visible: Object.keys(root.factorParameters).length > 0
                            
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
                                        model: Object.keys(root.factorParameters)
                                        
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
                                                text: root.factorParameters[modelData]
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
            "选择因子类型",
            "配置因子信息与参数", 
            "预览并确认"
        ]
        return titles[stepIndex] || ""
    }
    
    // 获取步骤描述
    function getStepDescription(stepIndex) {
        var descriptions = [
            "选择适合您策略的因子类型，不同类型的因子有不同的参数配置",
            "填写因子基本信息并配置参数，这些参数将影响因子的计算",
            "确认所有信息无误后创建因子"
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
            case 0: return root.selectedType !== ""
            case 1: return root.factorName.trim() !== "" && root.factorDescription.trim() !== "" && root.parametersValid
            case 2: return true  // 最后一步总是有效的
            default: return false
        }
    }
    
    // 获取类型名称
    function getTypeName(typeId) {
        var typeNames = {
            "value": "价值因子",
            "momentum": "动量因子",
            "size": "规模因子",
            "quality": "质量因子",
            "low_volatility": "低波因子",
            "growth": "成长因子",
            "dividend": "红利因子",
            "technical": "技术因子",
            "macro_sector": "宏观/行业",
            "liquidity": "流动性因子",
            "sentiment": "情绪因子",
            "custom": "自定义"
        }
        return typeNames[typeId] || typeId
    }
    
    // 获取类型颜色
    function getTypeColor(typeId) {
        var colors = {
            "value": "#F59E0B",
            "momentum": "#3B82F6",
            "size": "#8B5CF6",
            "quality": "#10B981",
            "low_volatility": "#06B6D4",
            "growth": "#8B5CF6",
            "dividend": "#EC4899",
            "technical": "#EF4444",
            "macro_sector": "#F97316",
            "liquidity": "#8B5CF6",
            "sentiment": "#EC4899",
            "custom": "#94A3B8"
        }
        return colors[typeId] || "#94A3B8"
    }
    
    // 生成默认内容
    function generateDefaultContent(factorType) {
        console.log("为因子类型生成默认内容:", factorType)
        
        if (!root.defaultContentMap[factorType]) {
            console.warn("未找到因子类型的默认内容配置:", factorType)
            return
        }
        
        var defaultContent = root.defaultContentMap[factorType]
        
        // 如果当前因子名称为空，则使用默认名称
        if (!root.factorName || root.factorName.trim() === "") {
            root.factorName = defaultContent.name
            console.log("自动设置因子名称:", defaultContent.name)
        }
        
        // 如果当前因子描述为空，则使用默认描述
        if (!root.factorDescription || root.factorDescription.trim() === "") {
            root.factorDescription = defaultContent.description
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
    
    // ============ 插件化架构函数 ============
    
    // 加载因子配置
    function loadFactorSchemas() {
        console.log("开始加载因子参数配置...")
        SchemaLoader.FactorSchemaLoader.loadFactorSchemas(function(schemas) {
            if (schemas) {
                root.factorSchemas = schemas
                root.schemasLoaded = true
                console.log("因子配置加载完成，包含类型数量:", 
                           Object.keys(schemas.factorSchemas || {}).length)
                console.log("可用的因子类型:", Object.keys(schemas.factorSchemas || {}))
            } else {
                console.warn("因子配置加载失败，使用默认配置")
                root.factorSchemas = SchemaLoader.FactorSchemaLoader.defaultSchemas
                root.schemasLoaded = true
            }
        })
    }
    
    // 为指定类型加载schema
    function loadSchemaForType(factorType) {
        console.log("开始加载因子类型schema:", factorType)
        console.log("factorSchemas 状态:", root.factorSchemas ? "已加载" : "未加载")
        console.log("schemasLoaded 状态:", root.schemasLoaded)
        
        if (!root.factorSchemas) {
            console.warn("因子配置未加载，无法加载类型:", factorType)
            console.log("尝试使用默认配置...")
            
            // 尝试使用默认配置
            var defaultSchemas = SchemaLoader.FactorSchemaLoader.defaultSchemas
            if (defaultSchemas && defaultSchemas.factorSchemas && defaultSchemas.factorSchemas[factorType]) {
                root.factorSchemas = defaultSchemas
                root.schemasLoaded = true
                console.log("使用默认配置成功")
            } else {
                console.error("默认配置也找不到类型:", factorType)
                root.currentSchema = null
                return
            }
        }
        
        // 使用getMergedSchema获取合并后的schema（包含通用参数和特定参数）
        var mergedSchema = SchemaLoader.FactorSchemaLoader.getMergedSchema(root.factorSchemas, factorType)
        if (mergedSchema && mergedSchema.properties) {
            root.currentSchema = mergedSchema
            console.log("合并后的schema加载成功，包含参数数量:", 
                       Object.keys(mergedSchema.properties || {}).length)
            console.log("参数列表:", Object.keys(mergedSchema.properties || {}))
            
            // 测试参数配置转换
            if (paramComponents && typeof paramComponents.schemaToConfigs === "function") {
                var configs = paramComponents.schemaToConfigs(mergedSchema)
                console.log("转换后的参数配置数量:", configs.length)
                console.log("配置详情:", JSON.stringify(configs))
            }
        } else {
            console.warn("未找到因子类型schema:", factorType)
            console.log("可用的因子类型:", Object.keys(root.factorSchemas.factorSchemas || {}))
            root.currentSchema = null
        }
    }
    
    // 更新验证状态
    function updateValidationState() {
        // 检查是否有参数
        var hasParameters = Object.keys(root.factorParameters).length > 0
        
        // 检查必填参数
        var requiredParams = []
        if (root.currentSchema && root.currentSchema.properties) {
            for (var key in root.currentSchema.properties) {
                var prop = root.currentSchema.properties[key]
                if (prop.required === true) {
                    requiredParams.push(key)
                }
            }
        }
        
        var allRequiredFilled = requiredParams.every(function(key) {
            return root.factorParameters[key] !== undefined && 
                   root.factorParameters[key] !== null && 
                   root.factorParameters[key] !== ""
        })
        
        root.parametersValid = hasParameters && allRequiredFilled
        root.validationMessage = root.parametersValid ? 
            "✔️ 参数验证通过" : 
            "⚠️ 请填写所有必填参数"
    }
    
    // 创建因子
    function createFactor() {
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
        
        // 4. 通过FactorService添加因子
        if (root.factorService) {
            console.log("调用 factorService.addFactor...")
            var factorId = root.factorService.addFactor(factorData)
            
            if (factorId && factorId !== "") {
                console.log("✔️ 因子创建成功，ID:", factorId)
                showToast("✔️ 因子 '" + root.factorName + "' 创建成功并已添加到因子库中")
                
                // 触发因子创建信号
                root.factorCreated(factorData)
                
                // 重置表单
                resetForm()
            } else {
                console.log("❌ 因子创建失败")
                showToast("❌ 因子创建失败，请检查数据库连接")
            }
        } else {
            console.log("❌ factorService 未初始化")
            showToast("❌ 因子服务未初始化，无法创建因子")
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
        
        if (!root.selectedType || root.selectedType === "") {
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
        // 构建完整的因子数据 - 与数据库表结构匹配
        var factorData = {
            factorName: root.factorName.toLowerCase().replace(/\s+/g, '_'),
            displayName: root.factorName,
            majorCategory: root.selectedTypeName,
            subCategory: getSubCategory(root.selectedType),
            description: root.factorDescription,
            icValue: 0.0,
            irValue: 0.0,
            validityDays: 30,
            turnoverRate: 0.25,
            isRecommended: false,
            isFavorite: false,
            status: "active",
            tags: root.factorTags.concat([root.selectedTypeName]),
            creator: "system",
            createDate: dateStr,
            groupReturns: [],
            parameters: root.factorParameters
        }
        
        return factorData
    }
    
    // 获取子类别
    function getSubCategory(typeId) {
        var subCategories = {
            "value": "估值",
            "momentum": "趋势动量",
            "size": "市值规模",
            "quality": "盈利能力",
            "low_volatility": "波动率",
            "growth": "营收增长",
            "dividend": "股息",
            "technical": "技术指标",
            "macro_sector": "宏观行业",
            "liquidity": "市场微观结构",
            "sentiment": "行为金融",
            "custom": "自定义"
        }
        return subCategories[typeId] || "其他"
    }
    
    // 重置表单
    function resetForm() {
        root.factorName = ""
        root.factorDescription = ""
        root.selectedType = ""
        root.factorParameters = {}
        root.factorTags = []
        root.currentStep = 0
        root.currentSchema = null
        root.parametersValid = false
        root.validationMessage = ""
    }
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
        // 这里可以实现toast提示组件
        // 暂时只记录到控制台
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("CreationPagePluginIntegrated 初始化完成")
        
        // 加载因子配置
        loadFactorSchemas()
    }
    
    // 监听因子类型变化
    onSelectedTypeChanged: {
        console.log("因子类型变化:", selectedType)
        
        // 重置参数
        if (selectedType !== "") {
            root.factorParameters = {}
            root.parametersValid = false
            root.validationMessage = "请配置参数"
            
            // 加载对应类型的schema
            loadSchemaForType(selectedType)
            
            // 自动生成默认内容
            generateDefaultContent(selectedType)
        }
    }
    
    // 监听参数变化
    onFactorParametersChanged: {
        console.log("参数变化:", Object.keys(root.factorParameters).length, "个参数")
        updateValidationState()
    }
}
// CreationPage.qml
// 因子创建页面 - 使用内联创建界面，不使用对话框
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

/**
 * 因子创建页面 - 使用内联创建界面，不使用对话框
 * 从原始FactorWorkbench_original_backup.qml中提取的创建模式部分
 */
Item {
    id: root
    
    // ============ 页面属性 ============
    
    property var globalDataService
    property var factorParamController
    property Bridge.FactorService factorService: null  // 使用新的FactorService类型
    property var factorDataModel: null  // 添加因子数据模型属性
    property string selectedType: ""
    property string selectedTypeName: getTypeName(selectedType)  // 添加类型名称属性
    
    // 监听 factorService 变化
    onFactorServiceChanged: {
        console.log("factorService 属性发生变化")
    }
    
    // 监听 factorDataModel 变化
    onFactorDataModelChanged: {
        console.log("factorDataModel 属性发生变化")
    }
    
    // 动量因子参数
    property string momentumWindow: "20"
    property string momentumMethod: "简单收益率"
    property string momentumLookback: "60"
    
    // 价值因子参数
    property string valueIndicator: "市盈率(PE)"
    property bool valueNeutral: true
    property string valueQuantile: "10"
    
    // 技术指标参数
    property string technicalType: "移动平均线"
    property string technicalPeriod: "14"
    property string technicalSignal: "9"
    
    // ============ 信号 ============
    
    signal factorCreated(var factorData)
    signal backClicked()
    
    // ============ 主布局 ============
    
    StackLayout {
        id: createStackLayout
        anchors.fill: parent
        anchors.margins: 24
        currentIndex: creationStep
        
        property int creationStep: 0
        
        // 步骤1: 选择因子类型
        Rectangle {
            id: typeSelectionStep
            color: "transparent"
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 16
                
                // 创建模式标题
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    
                    ColumnLayout {
                        spacing: 4
                        
                        Text {
                            text: "📝 创建新因子"
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Text {
                            text: "选择因子类型开始创建"
                            font.pixelSize: 14
                            color: "#94A3B8"
                        }
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 返回按钮
                    Rectangle {
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 36
                        radius: 8
                        color: "#334155"
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            
                            Text {
                                text: "⬅️"
                                font.pixelSize: 14
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: "返回"
                                font.pixelSize: 14
                                color: "#F1F5F9"
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.backClicked()
                        }
                    }
                }
                
                // 类型选择区域
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 16
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 16
                        
                        Text {
                            text: "选择因子类型"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        // 类型选择网格
                        GridLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 16
                            
                            // 动量类
                            FactorTypeCard {
                                typeId: "momentum"
                                typeName: "动量类"
                                description: "价格动量、收益率等趋势因子"
                                icon: "📈"
                                color: "#3B82F6"
                                selected: root.selectedType === "momentum"
                                onClicked: root.selectedType = "momentum"
                            }
                            
                            // 价值类
                            FactorTypeCard {
                                typeId: "value"
                                typeName: "价值类"
                                description: "估值、市盈率、市净率等"
                                icon: "💰"
                                color: "#10B981"
                                selected: root.selectedType === "value"
                                onClicked: root.selectedType = "value"
                            }
                            
                            // 技术指标
                            FactorTypeCard {
                                typeId: "technical"
                                typeName: "技术指标"
                                description: "移动平均线、RSI等技术指标"
                                icon: "📊"
                                color: "#8B5CF6"
                                selected: root.selectedType === "technical"
                                onClicked: root.selectedType = "technical"
                            }
                            
                            // 质量类
                            FactorTypeCard {
                                typeId: "quality"
                                typeName: "质量类"
                                description: "财务质量、盈利能力指标"
                                icon: "🏆"
                                color: "#F59E0B"
                                selected: root.selectedType === "quality"
                                onClicked: root.selectedType = "quality"
                            }
                            
                            // 情绪类
                            FactorTypeCard {
                                typeId: "sentiment"
                                typeName: "情绪类"
                                description: "市场情绪、新闻情绪因子"
                                icon: "😊"
                                color: "#EC4899"
                                selected: root.selectedType === "sentiment"
                                onClicked: root.selectedType = "sentiment"
                            }
                            
                            // 自定义
                            FactorTypeCard {
                                typeId: "custom"
                                typeName: "自定义"
                                description: "自定义表达式创建因子"
                                icon: "🛠️"
                                color: "#64748B"
                                selected: root.selectedType === "custom"
                                onClicked: root.selectedType = "custom"
                            }
                        }
                        
                        // 下一步按钮
                        Rectangle {
                            Layout.alignment: Qt.AlignRight
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 40
                            radius: 8
                            color: root.selectedType !== "" ? "#3B82F6" : "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 8
                                
                                Text {
                                    text: "➡️"
                                    font.pixelSize: 14
                                    color: root.selectedType !== "" ? "white" : "#94A3B8"
                                }
                                
                                Text {
                                    text: "下一步"
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    color: root.selectedType !== "" ? "white" : "#94A3B8"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: root.selectedType !== ""
                                onClicked: {
                                    console.log("选择因子类型:", root.selectedType)
                                    if (root.selectedType !== "") {
                                        // 进入参数配置步骤
                                        console.log("切换到参数配置步骤")
                                        createStackLayout.creationStep = 1
                                    } else {
                                        console.log("警告: selectedType为空，无法进入下一步")
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // 步骤2: 配置参数
        Rectangle {
            id: parameterConfigStep
            color: "#0F172A"
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 16
                
                // 步骤标题
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    
                    ColumnLayout {
                        spacing: 4
                        
                        Text {
                            text: "⚙️ 配置 " + getTypeName(root.selectedType) + " 因子参数"
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Text {
                            text: "填写因子信息和详细参数"
                            font.pixelSize: 14
                            color: "#94A3B8"
                        }
                    }
                    
                    Item { Layout.fillWidth: true }
                }
                
                // 参数配置容器 - 添加滚动布局
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 16
                    color: "#1E293B"
                    
                    // 滚动视图 - 隐藏滚动条
                    Flickable {
                        id: configFlickable
                        anchors.fill: parent
                        anchors.margins: 20
                        contentWidth: parent.width - 40
                        contentHeight: configColumnLayout.height
                        clip: true
                        
                        // 隐藏滚动条
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AlwaysOff  // 始终隐藏垂直滚动条
                        }
                        ScrollBar.horizontal: ScrollBar {
                            policy: ScrollBar.AlwaysOff  // 始终隐藏水平滚动条
                        }
                        
                        ColumnLayout {
                            id: configColumnLayout
                            width: parent.width
                            spacing: 24
                            
                            // 基本信息区域
                            ColumnLayout {
                                spacing: 4
                                
                                Text {
                                    text: "📝 基本信息"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }
                                
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: "#334155"
                                }
                                
                                // 因子名称
                                ColumnLayout {
                                    spacing: 8
                                    
                                    Text {
                                        text: "因子名称"
                                        font.pixelSize: 14
                                        color: "#F1F5F9"
                                    }
                                    
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 40
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                        
                                        TextInput {
                                            id: inlineNameField
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            font.pixelSize: 14
                                            color: "#F1F5F9"
                                            verticalAlignment: Text.AlignVCenter
                                            
                                            Text {
                                                anchors.fill: parent
                                                anchors.leftMargin: 2
                                                verticalAlignment: Text.AlignVCenter
                                                text: "请输入有意义的因子名称"
                                                font: inlineNameField.font
                                                color: "#94A3B8"
                                                visible: !inlineNameField.text && !inlineNameField.activeFocus
                                            }
                                        }
                                    }
                                }
                                
                                // 因子描述
                                ColumnLayout {
                                    spacing: 8
                                    
                                    Text {
                                        text: "因子描述"
                                        font.pixelSize: 14
                                        color: "#F1F5F9"
                                    }
                                    
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 80
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                        
                                        TextInput {
                                            id: inlineDescField
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            font.pixelSize: 14
                                            color: "#F1F5F9"
                                            wrapMode: Text.WordWrap
                                            
                                            Text {
                                                anchors.fill: parent
                                                anchors.leftMargin: 2
                                                anchors.topMargin: 12
                                                text: "描述因子的目的、计算方法等"
                                                font: inlineDescField.font
                                                color: "#94A3B8"
                                                visible: !inlineDescField.text && !inlineDescField.activeFocus
                                            }
                                        }
                                    }
                                }
                            }
                            
                            // 参数配置区域
                            ColumnLayout {
                                spacing: 4
                                
                                Text {
                                    text: "⚙️ " + getTypeName(root.selectedType) + " 参数配置"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }
                                
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: "#334155"
                                }
                                
                                // 动态参数配置
                                Loader {
                                    id: parameterLoader
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 200
                                    sourceComponent: getParameterComponent(root.selectedType)
                                }
                            }
                            
                            // 操作按钮区域 - 保持在底部
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 60  // 为按钮预留空间
                            }
                        }
                    }
                    
                    // 操作按钮 - 固定在底部
                    RowLayout {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 20
                        spacing: 12
                        
                        // 上一步按钮
                        Rectangle {
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 40
                            radius: 8
                            color: "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 8
                                
                                Text {
                                    text: "⬅️"
                                    font.pixelSize: 14
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: "上一步"
                                    font.pixelSize: 14
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: createStackLayout.creationStep = 0
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 创建按钮
                        Rectangle {
                            id: inlineCreateButton
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 40
                            radius: 8
                            color: inlineNameField.text !== "" && inlineDescField.text !== "" ? "#3B82F6" : "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 8
                                
                                Text {
                                    text: "✅"
                                    font.pixelSize: 14
                                    color: inlineNameField.text !== "" && inlineDescField.text !== "" ? "white" : "#94A3B8"
                                }
                                
                                Text {
                                    text: "创建因子"
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    color: inlineNameField.text !== "" && inlineDescField.text !== "" ? "white" : "#94A3B8"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: inlineNameField.text !== "" && inlineDescField.text !== ""
                                onClicked: createInlineFactor()
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 核心函数 ============
    
    // 获取类型名称
    function getTypeName(typeId) {
        var typeNames = {
            "momentum": "动量类",
            "value": "价值类", 
            "quality": "质量类",
            "technical": "技术指标",
            "sentiment": "情绪类",
            "macro": "宏观类",
            "risk": "风险类",
            "custom": "自定义"
        }
        return typeNames[typeId] || typeId
    }
    
    // 获取参数组件
    function getParameterComponent(typeId) {
        switch(typeId) {
            case "momentum":
                return momentumParameters
            case "value":
                return valueParameters
            case "technical":
                return technicalParameters
            case "quality":
                return qualityParameters
            case "sentiment":
                return sentimentParameters
            case "custom":
                return customParameters
            default:
                return genericParameters
        }
    }
    
    // 创建内联因子 - 通过FactorService添加到因子库
    function createInlineFactor() {
        // 1. 表单验证
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
        console.log("调用 factorService.addFactor...")
        var factorId = root.factorService.addFactor(factorData)
        
        if (factorId && factorId !== "") {
            console.log("✅ 因子创建成功，ID:", factorId)
            showToast("✅ 因子 '" + inlineNameField.text + "' 创建成功并已添加到因子库！")
            
            // 触发因子创建信号
            root.factorCreated(factorData)
            
            // 重置表单
            resetForm()
        } else {
            console.log("❌ 因子创建失败")
            showToast("❌ 因子创建失败，请检查数据库连接")
        }
    }
    
    // 获取子类别
    function getSubCategory(typeId) {
        var subCategories = {
            "momentum": "趋势动量",
            "value": "估值",
            "technical": "技术指标",
            "quality": "盈利能力",
            "sentiment": "新闻情绪",
            "custom": "自定义"
        }
        return subCategories[typeId] || "其他"
    }
    
    // 表单验证
    function validateForm() {
        // 1. 检查必填字段
        if (!inlineNameField.text || inlineNameField.text.trim() === "") {
            showToast("❌ 请输入因子名称")
            return false
        }
        
        if (!inlineDescField.text || inlineDescField.text.trim() === "") {
            showToast("❌ 请输入因子描述")
            return false
        }
        
        if (!root.selectedType || root.selectedType === "") {
            showToast("❌ 请选择因子类型")
            return false
        }
        
        // 2. 检查名称长度
        if (inlineNameField.text.length < 2 || inlineNameField.text.length > 50) {
            showToast("❌ 因子名称长度应在2-50个字符之间")
            return false
        }
        
        // 3. 检查描述长度
        if (inlineDescField.text.length < 10 || inlineDescField.text.length > 500) {
            showToast("❌ 因子描述长度应在10-500个字符之间")
            return false
        }
        
        // 4. 检查参数有效性（根据类型）
        if (!validateParameters()) {
            return false
        }
        
        return true
    }
    
    // 验证参数
    function validateParameters() {
        switch(root.selectedType) {
            case "momentum":
                // 检查动量参数
                var window = parseInt(root.momentumWindow)
                var lookback = parseInt(root.momentumLookback)
                if (isNaN(window) || window < 1 || window > 250) {
                    showToast("❌ 窗口期应在1-250之间")
                    return false
                }
                if (isNaN(lookback) || lookback < 1 || lookback > 500) {
                    showToast("❌ 回溯期应在1-500之间")
                    return false
                }
                break
                
            case "value":
                // 检查价值参数
                var quantile = parseInt(root.valueQuantile)
                if (isNaN(quantile) || quantile < 1 || quantile > 100) {
                    showToast("❌ 分位数应在1-100之间")
                    return false
                }
                break
                
            case "technical":
                // 检查技术参数
                var period = parseInt(root.technicalPeriod)
                var signal = parseInt(root.technicalSignal)
                if (isNaN(period) || period < 1 || period > 250) {
                    showToast("❌ 周期应在1-250之间")
                    return false
                }
                if (isNaN(signal) || signal < 1 || signal > 250) {
                    showToast("❌ 信号线应在1-250之间")
                    return false
                }
                break
        }
        
        return true
    }
    
    // 构建完整的因子数据
    function buildCompleteFactorData(dateStr) {
        // 获取类型特定的参数
        var typeParams = getTypeSpecificParameters()
        
        // 构建完整的因子数据 - 与数据库表结构匹配
        var factorData = {
            factorName: inlineNameField.text.toLowerCase().replace(/\s+/g, '_'),
            displayName: inlineNameField.text,
            majorCategory: root.selectedTypeName,
            subCategory: getSubCategory(root.selectedType),
            description: inlineDescField.text,
            icValue: 0.0,
            irValue: 0.0,
            validityDays: 30,
            turnoverRate: 0.25,  // 25% 转换为小数形式 0.25
            isRecommended: false,
            isFavorite: false,
            status: "active",  // 使用小写，与数据库默认值匹配
            tags: [root.selectedTypeName],
            creator: "system",  // 使用默认值，与数据库默认值匹配
            createDate: dateStr,
            groupReturns: [],  // 空数组，FactorRepository会处理
            parameters: typeParams  // 添加类型特定的参数
        }
        
        return factorData
    }
    
    // 获取类型特定的参数
    function getTypeSpecificParameters() {
        var params = {}
        
        switch(root.selectedType) {
            case "momentum":
                params.window = parseInt(root.momentumWindow)
                params.method = root.momentumMethod
                params.lookback = parseInt(root.momentumLookback)
                break
                
            case "value":
                params.indicator = root.valueIndicator
                params.neutral = root.valueNeutral
                params.quantile = parseInt(root.valueQuantile)
                break
                
            case "technical":
                params.type = root.technicalType
                params.period = parseInt(root.technicalPeriod)
                params.signal = parseInt(root.technicalSignal)
                break
                
            case "quality":
                params.metric = "ROE"  // 默认值
                params.threshold = 0.1
                break
                
            case "sentiment":
                params.source = "新闻"
                params.sentimentType = "积极"
                break
                
            case "custom":
                params.expression = ""
                params.variables = []
                break
        }
        
        return params
    }
    
    // 重置表单
    function resetForm() {
        inlineNameField.text = ""
        inlineDescField.text = ""
        root.selectedType = ""
        createStackLayout.creationStep = 0
        
        // 重置参数
        root.momentumWindow = "20"
        root.momentumMethod = "简单收益率"
        root.momentumLookback = "60"
        root.valueIndicator = "市盈率(PE)"
        root.valueNeutral = true
        root.valueQuantile = "10"
        root.technicalType = "移动平均线"
        root.technicalPeriod = "14"
        root.technicalSignal = "9"
    }
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
        // 这里可以实现toast提示组件
        // 暂时只记录到控制台
    }
    
    // ============ 参数配置组件 ============
    
    // 动量因子参数组件
    Component {
        id: momentumParameters
        
        ColumnLayout {
            spacing: 12
            
            // 窗口期
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "窗口期"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: momentumWindowInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: root.momentumWindow
                        
                        onTextChanged: root.momentumWindow = text
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "20"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 计算方法
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "计算方法"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: momentumMethodInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: root.momentumMethod
                        
                        onTextChanged: root.momentumMethod = text
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "简单收益率"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 回溯期
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "回溯期"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: momentumLookbackInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: root.momentumLookback
                        
                        onTextChanged: root.momentumLookback = text
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "60"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
        }
    }
    
    // 价值因子参数组件
    Component {
        id: valueParameters
        
        ColumnLayout {
            spacing: 12
            
            // 估值指标
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "估值指标"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: valueIndicatorInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: root.valueIndicator
                        
                        onTextChanged: root.valueIndicator = text
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "市盈率(PE)"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 行业中性化
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "行业中性化"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                RowLayout {
                    spacing: 12
                    
                    Rectangle {
                        width: 40
                        height: 20
                        radius: 10
                        color: "#3B82F6"
                        
                        Rectangle {
                            id: valueNeutralCircle
                            x: root.valueNeutral ? 22 : 2
                            width: 16
                            height: 16
                            radius: 8
                            color: "white"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.valueNeutral = !root.valueNeutral
                                valueNeutralCircle.x = root.valueNeutral ? 22 : 2
                            }
                        }
                    }
                    
                    Text {
                        text: root.valueNeutral ? "开启" : "关闭"
                        font.pixelSize: 14
                        color: root.valueNeutral ? "#10B981" : "#94A3B8"
                    }
                    
                    Item { Layout.fillWidth: true }
                }
            }
            
            // 分位数
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "分位数"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: valueQuantileInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: root.valueQuantile
                        
                        onTextChanged: root.valueQuantile = text
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "10"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
        }
    }
    
    // 技术指标参数组件
    Component {
        id: technicalParameters
        
        ColumnLayout {
            spacing: 12
            
            // 指标类型
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "指标类型"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: technicalTypeInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: root.technicalType
                        
                        onTextChanged: root.technicalType = text
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "移动平均线"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 周期
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "周期"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: technicalPeriodInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: root.technicalPeriod
                        
                        onTextChanged: root.technicalPeriod = text
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "14"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 信号线
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "信号线"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: technicalSignalInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: root.technicalSignal
                        
                        onTextChanged: root.technicalSignal = text
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "9"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
        }
    }
    
    // 质量因子参数组件
    Component {
        id: qualityParameters
        
        ColumnLayout {
            spacing: 12
            
            // 质量指标
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "质量指标"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: qualityMetricInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: "ROE"
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "ROE"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 时间框架
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "时间框架"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: qualityTimeframeInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: "年度"
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "年度"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 质量阈值
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "质量阈值(%)"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: qualityThresholdInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: "10.0"
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "10.0"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
        }
    }
    
    // 情绪因子参数组件
    Component {
        id: sentimentParameters
        
        ColumnLayout {
            spacing: 12
            
            // 情绪来源
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "情绪来源"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: sentimentSourceInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: "新闻"
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "新闻"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 回溯天数
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "回溯天数"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: sentimentLookbackInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: "30"
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "30"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 情绪指标
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "情绪指标"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: sentimentMetricInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        text: "情绪得分"
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "情绪得分"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
        }
    }
    
    // 自定义因子参数组件
    Component {
        id: customParameters
        
        ColumnLayout {
            spacing: 12
            
            // 自定义表达式
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "自定义表达式"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 80
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: customExpressionInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        wrapMode: Text.WordWrap
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            anchors.topMargin: 12
                            text: "例如: close / ma(close, 20)"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
            
            // 变量列表
            ColumnLayout {
                spacing: 4
                
                Text {
                    text: "变量列表"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#0F172A"
                    border.width: 1
                    border.color: "#334155"
                    
                    TextInput {
                        id: customVariablesInput
                        anchors.fill: parent
                        anchors.margins: 12
                        font.pixelSize: 14
                        color: "#F1F5F9"
                        verticalAlignment: Text.AlignVCenter
                        
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            verticalAlignment: Text.AlignVCenter
                            text: "例如: close, volume, ma"
                            font: parent.font
                            color: "#94A3B8"
                            visible: !parent.text && !parent.activeFocus
                        }
                    }
                }
            }
        }
    }
    
    // 通用参数组件
    Component {
        id: genericParameters
        
        ColumnLayout {
            spacing: 12
            
            Text {
                text: "通用参数配置"
                font.pixelSize: 14
                color: "#F1F5F9"
            }
            
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                radius: 8
                color: "#0F172A"
                border.width: 1
                border.color: "#334155"
                
                Text {
                    anchors.centerIn: parent
                    text: "选择因子类型后\n显示对应的参数配置"
                    font.pixelSize: 14
                    color: "#94A3B8"
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}

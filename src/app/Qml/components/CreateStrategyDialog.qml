import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Rectangle {
    id: createDialog
    implicitWidth: 600
    implicitHeight: 700
    radius: 20
    color: "#1E293B"
    border.color: "#334155"
    
    // 属性
    property bool isOpen: false
    
    // 信号
    signal strategyCreated(var strategyData)
    signal closed()
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color accentBlue: "#3B82F6"
    readonly property color accentGreen: "#10B981"
    readonly property color accentPurple: "#8B5CF6"
    readonly property color accentYellow: "#F59E0B"
    readonly property color dangerRed: "#EF4444"
    readonly property color tertiaryBg: "#334155"
    readonly property color borderLight: "#475569"
    
    // 表单数据
    property string strategyName: ""
    property string strategyType: "趋势跟踪"
    property string assetType: "股票"
    property string timeFrame: "短线"
    property string description: ""
    
    // 验证状态
    property bool nameValid: false
    
    // 当前选中的模板
    property string selectedTemplate: ""
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 标题栏
        DialogHeader {
            Layout.fillWidth: true
            title: "新建策略"
            subtitle: "从模板开始或自定义策略配置"
            onCloseClicked: closeDialog()
        }
        
        // 表单内容
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            ColumnLayout {
                width: parent.width
                spacing: 24
                // 如需边距请用外层 Item/Rectangle 包裹并设置 anchors.margins
                
                // 策略模板选择
                ColumnLayout {
                    spacing: 8
                    
                    Text {
                        text: "选择策略模板（可选）"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: textPrimary
                    }
                    
                    // 模板网格
                    GridLayout {
                        columns: 2
                        rowSpacing: 12
                        columnSpacing: 12
                        
                        // 双均线策略模板
                        TemplateCard {
                            badgeText: "热门"
                            icon: "📈"
                            title: "双均线趋势策略"
                            description: "基于MA5/MA20金叉死叉的趋势跟踪策略，适合新手"
                            isSelected: selectedTemplate === "moving_avg"
                            onClicked: {
                                selectedTemplate = "moving_avg"
                                applyTemplate("moving_avg")
                            }
                        }
                        
                        // RSI策略模板
                        TemplateCard {
                            icon: "🔄"
                            title: "RSI超卖反弹"
                            description: "RSI低于30买入，高于70卖出，适合震荡市"
                            isSelected: selectedTemplate === "rsi"
                            onClicked: {
                                selectedTemplate = "rsi"
                                applyTemplate("rsi")
                            }
                        }
                        
                        // 布林带策略模板
                        TemplateCard {
                            icon: "🎯"
                            title: "布林带突破"
                            description: "突破布林带上轨做多，跌破下轨做空"
                            isSelected: selectedTemplate === "bollinger"
                            onClicked: {
                                selectedTemplate = "bollinger"
                                applyTemplate("bollinger")
                            }
                        }
                        
                        // 机器学习策略模板
                        TemplateCard {
                            icon: "🤖"
                            title: "机器学习预测"
                            description: "基于LSTM的股价预测，适合高级用户"
                            isSelected: selectedTemplate === "ml"
                            onClicked: {
                                selectedTemplate = "ml"
                                applyTemplate("ml")
                            }
                        }
                    }
                }
                
                // 策略名称
                FormTextField {
                    id: nameField
                    label: "策略名称 *"
                    placeholder: "例如：双均线趋势策略"
                    Layout.fillWidth: true
                    
                    onTextChanged: {
                        strategyName = text
                        nameValid = text.trim().length > 0
                        updatePreview()
                    }
                }
                
                // 策略类型
                ChipSelector {
                    label: "策略类型"
                    options: ["趋势跟踪", "均值回归", "动量策略", "套利策略", "机器学习"]
                    selectedOption: strategyType
                    Layout.fillWidth: true
                    
                    onOptionSelected: {
                        strategyType = option
                        updatePreview()
                    }
                }
                
                // 资产类型
                ChipSelector {
                    label: "资产类型"
                    options: ["股票", "期货", "加密货币", "外汇", "期权"]
                    selectedOption: assetType
                    Layout.fillWidth: true
                    
                    onOptionSelected: {
                        assetType = option
                        updatePreview()
                    }
                }
                
                // 时间框架
                ChipSelector {
                    label: "时间框架"
                    options: ["短线", "日内", "中线", "长线", "高频"]
                    selectedOption: timeFrame
                    Layout.fillWidth: true
                    
                    onOptionSelected: {
                        timeFrame = option
                        updatePreview()
                    }
                }
                
                // 策略描述模板（点击模板后显示）
                Rectangle {
                    id: descriptionTemplate
                    Layout.fillWidth: true
                    Layout.preferredHeight: templateContent.height + 32
                    radius: 12
                    color: "#0F172A"
                    border.color: accentPurple
                    border.width: 1
                    visible: selectedTemplate !== ""
                    
                    ColumnLayout {
                        id: templateContent
                        width: parent.width
                        anchors.centerIn: parent
                        spacing: 8
                        // 如需边距请用外层 Item/Rectangle 包裹并设置 anchors.margins
                        
                        // 模板标题
                        Text {
                            text: "模板内容预览"
                            font.pixelSize: 13
                            font.weight: Font.Bold
                            color: accentPurple
                        }
                        
                        // 模板描述区域
                        Text {
                            Layout.fillWidth: true
                            text: getTemplateDescription(selectedTemplate)
                            font.pixelSize: 12
                            color: textSecondary
                            wrapMode: Text.WordWrap
                            lineHeight: 1.4
                        }
                        
                        // 操作按钮
                        RowLayout {
                            spacing: 8
                            Layout.alignment: Qt.AlignRight
                            
                            // 复制模板按钮
                            Rectangle {
                                width: 80
                                height: 28
                                radius: 6
                                color: Qt.rgba(139/255, 92/255, 246/255, 0.1)
                                border.color: Qt.rgba(139/255, 92/255, 246/255, 0.3)
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: "📋 复制"
                                    font.pixelSize: 11
                                    color: "#A78BFA"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: copyTemplateToClipboard()
                                }
                            }
                            
                            // 使用模板按钮
                            Rectangle {
                                width: 80
                                height: 28
                                radius: 6
                                color: Qt.rgba(16/255, 185/255, 129/255, 0.1)
                                border.color: Qt.rgba(16/255, 185/255, 129/255, 0.3)
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: "✅ 使用"
                                    font.pixelSize: 11
                                    color: "#34D399"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: applyTemplateToDescription()
                                }
                            }
                        }
                    }
                }
                
                // 策略描述
                FormTextArea {
                    id: descField
                    label: "策略描述"
                    placeholder: "描述策略的核心逻辑、入场条件、出场条件、风险控制等...\n\n您可以：\n1. 点击上方模板查看示例\n2. 使用模板快速填充\n3. 自定义修改描述内容"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    
                    onTextChanged: {
                        description = text
                        updatePreview()
                    }
                }
                
                // 策略预览
                Rectangle {
                    id: previewBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: previewContent.height + 24
                    radius: 12
                    color: "#0F172A"
                    border.color: accentBlue
                    border.width: 2
                    visible: strategyName !== "" || description !== ""
                    
                    ColumnLayout {
                        id: previewContent
                        width: parent.width
                        anchors.centerIn: parent
                        spacing: 8
                        // 如需边距请用外层 Item/Rectangle 包裹并设置 anchors.margins
                        
                        Text {
                            text: "策略配置预览"
                            font.pixelSize: 13
                            color: textSecondary
                        }
                        
                        // 策略名称预览
                        Text {
                            Layout.fillWidth: true
                            text: strategyName ? "<b>" + strategyName + "</b>" : ""
                            font.pixelSize: 14
                            color: textPrimary
                            wrapMode: Text.WordWrap
                        }
                        
                        // 策略描述预览
                        Text {
                            Layout.fillWidth: true
                            text: description ? (description.length > 150 ? description.substring(0, 150) + "..." : description) : ""
                            font.pixelSize: 13
                            color: textSecondary
                            wrapMode: Text.WordWrap
                            lineHeight: 1.4
                        }
                        
                        // 标签预览
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            
                            Rectangle {
                                width: tag1.width + 16
                                height: 24
                                radius: 12
                                color: Qt.rgba(139/255, 92/255, 246/255, 0.1)
                                
                                Text {
                                    id: tag1
                                    anchors.centerIn: parent
                                    text: strategyType
                                    font.pixelSize: 11
                                    color: "#A78BFA"
                                }
                            }
                            
                            Rectangle {
                                width: tag2.width + 16
                                height: 24
                                radius: 12
                                color: Qt.rgba(16/255, 185/255, 129/255, 0.1)
                                
                                Text {
                                    id: tag2
                                    anchors.centerIn: parent
                                    text: assetType
                                    font.pixelSize: 11
                                    color: "#34D399"
                                }
                            }
                            
                            Rectangle {
                                width: tag3.width + 16
                                height: 24
                                radius: 12
                                color: Qt.rgba(245/255, 158/255, 11/255, 0.1)
                                
                                Text {
                                    id: tag3
                                    anchors.centerIn: parent
                                    text: timeFrame
                                    font.pixelSize: 11
                                    color: "#FBBF24"
                                }
                            }
                        }
                    }
                }
                
                Item { Layout.fillHeight: true }
                
                // 操作按钮
                RowLayout {
                    spacing: 12
                    Layout.alignment: Qt.AlignHCenter
                    
                    // 取消按钮
                    Rectangle {
                        width: 100
                        height: 44
                        radius: 12
                        color: tertiaryBg
                        border.color: borderLight
                        
                        Text {
                            anchors.centerIn: parent
                            text: "取消"
                            font.pixelSize: 14
                            color: textSecondary
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: closeDialog()
                        }
                    }
                    
                    // 创建按钮
                    Rectangle {
                        width: 120
                        height: 44
                        radius: 12
                        color: nameValid ? accentGreen : "#64748B"
                        
                        Text {
                            anchors.centerIn: parent
                            text: "创建策略"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "white"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: nameValid
                            onClicked: createStrategy()
                        }
                    }
                }
            }
        }
    }
    
    // === 组件定义 ===
    
    // 对话框标题栏组件
    component DialogHeader: Rectangle {
        property string title: ""
        property string subtitle: ""
        signal closeClicked()
        
        implicitHeight: 80
        color: tertiaryBg
        border.color: borderLight
        border.width: 0
        //border.bottomWidth: 1
        
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            
            ColumnLayout {
                spacing: 2
                
                Text {
                    text: parent.parent.title
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    color: textPrimary
                }
                
                Text {
                    text: parent.parent.subtitle
                    font.pixelSize: 14
                    color: textSecondary
                }
            }
            
            Item { Layout.fillWidth: true }
            
            // 关闭按钮
            Rectangle {
                width: 36
                height: 36
                radius: 18
                color: "transparent"
                
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    font.pixelSize: 20
                    color: textTertiary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: parent.parent.closeClicked()
                }
            }
        }
    }
    
    // 模板卡片组件
    component TemplateCard: Rectangle {
        property string badgeText: ""
        property string icon: ""
        property string title: ""
        property string description: ""
        property bool isSelected: false
        
        signal clicked()
        
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        radius: 12
        color: isSelected ? Qt.rgba(59/255, 130/255, 246/255, 0.05) : "#0F172A"
        border.color: isSelected ? accentBlue : borderLight
        border.width: isSelected ? 2 : 1
        
        // 徽章
        Rectangle {
            visible: parent.badgeText !== ""
            x: parent.width - width - 12
            y: -8
            width: badgeText.width + 16
            height: 20
            radius: 10
            color: accentBlue
            
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: parent.parent.badgeText
                font.pixelSize: 11
                font.weight: Font.Medium
                color: "white"
            }
        }
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4
            
            Text {
                text: parent.parent.icon
                font.pixelSize: 20
            }
            
            Text {
                text: parent.parent.title
                font.pixelSize: 14
                font.weight: Font.Medium
                color: textPrimary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            
            Text {
                text: parent.parent.description
                font.pixelSize: 12
                color: textSecondary
                wrapMode: Text.Wrap
                lineHeight: 1.3
                Layout.fillWidth: true
            }
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }
    
    // 表单输入框组件
    component FormTextField: ColumnLayout {
        property string label: ""
        property string placeholder: ""
        property alias text: textInput.text

        signal textEdited(string text)

        spacing: 4

        Text {
            text: parent.parent.label
            font.pixelSize: 14
            font.weight: Font.Medium
            color: textPrimary
            Layout.alignment: Qt.AlignLeft
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            radius: 12
            color: "#0F172A"
            border.color: borderLight

            TextInput {
                id: textInput
                anchors.fill: parent
                anchors.margins: 12
                font.pixelSize: 14
                color: textPrimary
                verticalAlignment: TextInput.AlignVCenter

                onTextChanged: parent.parent.textEdited(text)
            }

            // 占位符
            Text {
                anchors.fill: parent
                anchors.margins: 12
                text: parent.parent.placeholder
                font.pixelSize: 14
                color: textTertiary
                verticalAlignment: Text.AlignVCenter
                visible: textInput.text === ""
            }
        }
    }
    
    // 多行文本域组件
    component FormTextArea: ColumnLayout {
        property string label: ""
        property string placeholder: ""
        property alias text: textEdit.text

        signal textEdited(string text)

        spacing: 4

        Text {
            text: parent.parent.label
            font.pixelSize: 14
            font.weight: Font.Medium
            color: textPrimary
            Layout.alignment: Qt.AlignLeft
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: parent.parent.preferredHeight || 120
            radius: 12
            color: "#0F172A"
            border.color: borderLight

            TextEdit {
                id: textEdit
                anchors.fill: parent
                anchors.margins: 12
                font.pixelSize: 14
                color: textPrimary
                wrapMode: TextEdit.WordWrap

                onTextChanged: parent.parent.textEdited(text)
            }

            // 占位符
            Text {
                anchors.fill: parent
                anchors.margins: 12
                text: parent.parent.placeholder
                font.pixelSize: 14
                color: textTertiary
                wrapMode: Text.WordWrap
                visible: textEdit.text === ""
            }
        }
    }
    
    // 选项选择器组件
    component ChipSelector: ColumnLayout {
        property string label: ""
        property var options: []
        property string selectedOption: ""
        
        signal optionSelected(string option)
        
        spacing: 4
        
        Text {
            text: parent.parent.label
            font.pixelSize: 14
            font.weight: Font.Medium
            color: textPrimary
            Layout.alignment: Qt.AlignLeft
        }
        
        Flow {
            spacing: 8
            Layout.fillWidth: true
            
            Repeater {
                model: parent.parent.options
                
                delegate: Rectangle {
                    width: 90
                    height: 36
                    radius: 18
                    color: parent.parent.parent.selectedOption === modelData ? 
                           Qt.rgba(59/255, 130/255, 246/255, 0.15) : tertiaryBg
                    border.color: parent.parent.parent.selectedOption === modelData ? accentBlue : borderLight
                    border.width: parent.parent.parent.selectedOption === modelData ? 2 : 1
                    
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 13
                        color: parent.parent.parent.selectedOption === modelData ? accentBlue : textSecondary
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: parent.parent.parent.optionSelected(modelData)
                    }
                }
            }
        }
    }
    
    // === 函数定义 ===
    
    // 获取模板描述
    function getTemplateDescription(templateId) {
        var templates = {
            "moving_avg": "基于快速移动平均线（MA5）与慢速移动平均线（MA20）的交叉信号，当MA5上穿MA20时买入（金叉），当MA5下穿MA20时卖出（死叉）。适合趋势明显的市场行情。\n\n入场：连续2日MA5>MA20，成交量放大20%\n出场：MA5<MA20或亏损8%\n风控：单股仓位≤15%，总仓位≤80%",
            
            "rsi": "基于RSI（相对强弱指标）的超买超卖现象，在RSI低于30时买入（超卖），在RSI高于70时卖出（超买）。适合震荡市中的短线交易。\n\n入场：RSI连续2天<30，价格在布林带下轨\n出场：RSI>70或持仓5天\n风控：单笔风险≤2%，止损5%",
            
            "bollinger": "利用布林带指标识别价格突破，当价格突破上轨时做多，跌破下轨时做空。适合趋势明显的行情。\n\n入场：价格突破布林带上轨+成交量放大30%\n出场：价格回到中轨下方或达到2倍ATR止盈\n风控：杠杆≤3倍，硬止损8%",
            
            "ml": "使用LSTM神经网络预测未来价格方向，基于预测结果进行交易决策。适合对算法有一定了解的用户。\n\n输入特征：过去60分钟OHLC、成交量、技术指标\n模型输出：未来30分钟上涨概率\n信号：上涨概率>65%做多，下跌概率>65%做空"
        }
        
        return templates[templateId] || "请选择模板"
    }
    
    // 应用模板
    function applyTemplate(templateId) {
        var templates = {
            "moving_avg": {
                name: "双均线趋势策略",
                type: "趋势跟踪",
                asset: "股票",
                timeframe: "短线"
            },
            "rsi": {
                name: "RSI超卖反弹策略",
                type: "均值回归",
                asset: "股票",
                timeframe: "短线"
            },
            "bollinger": {
                name: "布林带突破策略",
                type: "趋势跟踪",
                asset: "期货",
                timeframe: "日内"
            },
            "ml": {
                name: "LSTM股价预测策略",
                type: "机器学习",
                asset: "加密货币",
                timeframe: "日内"
            }
        }
        
        var template = templates[templateId]
        if (template) {
            nameField.text = template.name
            strategyType = template.type
            assetType = template.asset
            timeFrame = template.timeframe
            
            // 触发验证
            nameField.textChanged(template.name)
        }
    }
    
    // 复制模板到剪贴板
    function copyTemplateToClipboard() {
        var templateText = getTemplateDescription(selectedTemplate)
        // 在实际应用中，这里应该使用Qt的Clipboard API
        console.log("复制模板到剪贴板：", templateText)
        // 可以添加一个提示，比如显示一个Toast消息
    }
    
    // 应用模板到描述区域
    function applyTemplateToDescription() {
        var fullDescription = getFullTemplateDescription(selectedTemplate)
        descField.text = fullDescription
        description = fullDescription
        updatePreview()
    }
    
    // 获取完整的模板描述
    function getFullTemplateDescription(templateId) {
        var templates = {
            "moving_avg": `## 策略概述
基于快速移动平均线（MA5）与慢速移动平均线（MA20）的交叉信号，构建的趋势跟踪策略。当短期均线上穿长期均线时产生买入信号，当短期均线下穿长期均线时产生卖出信号。

## 核心逻辑
1. 计算指标：
   - 快速移动平均线（MA5）：计算最近5个交易日的收盘价平均值
   - 慢速移动平均线（MA20）：计算最近20个交易日的收盘价平均值

2. 交易信号：
   - 买入信号：MA5从下方上穿MA20，形成"金叉"
   - 卖出信号：MA5从上方下穿MA20，形成"死叉"

3. 仓位管理：
   - 初始仓位：总资金的80%
   - 持仓期间：只持有多头仓位，不进行做空操作

## 入场条件
1. 连续2个交易日MA5 > MA20
2. 当前价格距离MA20不超过5%
3. 成交量较前5日平均增加20%以上

## 出场条件
1. 主要出场：MA5 < MA20 连续2个交易日
2. 止损出场：持仓亏损达到8%
3. 止盈出场：盈利达到20%

## 风险控制
1. 最大回撤控制：单次交易最大亏损不超过总资金的3%
2. 仓位控制：单只股票持仓不超过总资金的15%
3. 分散投资：同时持有不超过8只不同行业的股票
4. 止损纪律：严格执行止损规则`,
            
            "rsi": `## 策略概述
基于RSI（相对强弱指标）的超买超卖现象，在RSI低于30时买入，高于70时卖出。适合震荡市中的短线交易。

## 核心逻辑
1. 使用RSI(14)作为主要指标
2. RSI < 30：市场超卖，买入信号
3. RSI > 70：市场超买，卖出信号

## 入场条件
1. RSI连续2天低于30
2. 价格处于布林带下轨附近
3. 成交量萎缩后开始放大

## 风险控制
1. 单笔交易风险控制在2%以内
2. 持仓不超过5个交易日
3. 每日检查市场波动率`,
            
            "bollinger": `## 策略概述
利用布林带指标识别价格突破，当价格突破上轨时做多，跌破下轨时做空。适合趋势明显的行情。

## 核心逻辑
1. 布林带参数：20日均线，2倍标准差
2. 价格突破上轨：做多信号
3. 价格跌破下轨：做空信号

## 入场条件
1. 价格突破布林带上轨且收盘在上轨上方
2. 成交量较前一日增加30%以上
3. RSI不超过80（防止假突破）

## 出场条件
1. 价格回到布林带中轨下方
2. 或达到止盈目标（2倍ATR）`,
            
            "ml": `## 策略概述
使用LSTM神经网络预测未来价格方向，基于预测结果进行交易决策。适合对算法有一定了解的用户。

## 核心逻辑
1. 输入特征：过去60分钟OHLC数据、成交量、技术指标
2. 模型输出：未来30分钟上涨概率
3. 交易信号：上涨概率>65%做多，下跌概率>65%做空

## 技术要求
1. 需要Python环境
2. 需要TensorFlow/PyTorch库
3. 需要GPU加速训练

## 风险控制
1. 最大杠杆2倍
2. 每日重新训练模型
3. 设置硬止损8%`
        }
        
        return templates[templateId] || ""
    }
    
    // 更新预览
    function updatePreview() {
        // 预览区域根据策略名称和描述自动显示/隐藏
        // 在QML中，我们使用visible属性绑定
    }
    
    // 工具函数
    function openDialog() {
        isOpen = true;
        resetForm();
    }
    
    function closeDialog() {
        if (strategyName !== "" || description !== "") {
            // 在实际应用中，这里可以添加确认对话框
            console.log("有未保存的更改，确认关闭？")
        }
        isOpen = false;
        closed();
    }
    
    function resetForm() {
        strategyName = "";
        strategyType = "趋势跟踪";
        assetType = "股票";
        timeFrame = "短线";
        description = "";
        selectedTemplate = "";
        nameValid = false;
        
        nameField.text = "";
        descField.text = "";
    }
    
    function createStrategy() {
        if (!nameValid) {
            console.log("策略名称不能为空");
            return;
        }
        
        var strategyData = {
            name: strategyName,
            type: strategyType,
            asset: assetType,
            timeframe: timeFrame,
            description: description,
            template: selectedTemplate,
            status: "stopped",
            created_at: new Date().toISOString(),
            returns: "+0.0%",
            maxDrawdown: "-0.0%",
            sharpeRatio: "0.0",
            winRate: "0.0%",
            tags: [strategyType, assetType, timeFrame]
        };
        
        strategyCreated(strategyData);
        closeDialog(); // 现在是单击就会关闭对话框
    }
}
// StrategyCreationPage.qml
// 通用策略创建页面 - 向导式布局，标准化版
// 支持多种策略类型和参数配置

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    
    // ============ 页面属性 ============
    
    // 当前步骤 (1-3)
    property int currentStep: 1
    property int totalSteps: 3
    
    // 策略数据
    property string selectedStrategyType: "trend"
    property string strategyName: ""
    property string strategyDescription: ""
    property string assetType: "stock"
    property string timeFrame: "daily"
    property int backtestYears: 3
    
    // 策略参数
    property var strategyParameters: ({})
    
    // 验证状态
    property bool parametersValid: false
    property string validationMessage: ""
    property bool validationSuccess: false
    
    // 信号
    signal strategyCreated(var strategyData)
    signal backClicked()
    
    // ============ 主布局 ============
    
    color: "#0f172a"  // 暗色背景
    
    ScrollView {
        id: scrollView
        anchors.fill: parent
        anchors.margins: 20
        clip: true
        
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        
        ColumnLayout {
            width: scrollView.width
            spacing: 20
            
            // 头部：标题和进度步骤
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                
                // 标题
                Text {
                    text: "新建策略"
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    color: "#f1f5f9"
                }
                
                // 子标题
                Text {
                    text: "创建并配置您的量化交易策略"
                    font.pixelSize: 14
                    color: "#94a3b8"
                }  
            }
            
            // 主内容区
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 2
                columnSpacing: 24
                rowSpacing: 20
                
                // 左侧：策略类型选择
                Rectangle {
                    id: strategyTypesPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: 400
                    Layout.minimumHeight: 400
                    color: "#1e293b"
                    radius: 12
                    border.width: 1
                    border.color: "#334155"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 16
                        
                        // 类型选择标题
                        Text {
                            text: "选择策略类型"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#f1f5f9"
                        }
                        
                        // 类型选择区域
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 12
                            
                            // 趋势跟踪策略
                            StrategyTypeCard {
                                icon: "📈"
                                name: "趋势跟踪"
                                description: "跟踪市场趋势，追涨杀跌，适合单边行情"
                                isSelected: selectedStrategyType === "trend"
                                onClicked: selectedStrategyType = "trend"
                            }
                            
                            // 均值回归策略
                            StrategyTypeCard {
                                icon: "🔄"
                                name: "均值回归"
                                description: "认为价格会回归均值，适合震荡行情"
                                isSelected: selectedStrategyType === "mean_reversion"
                                onClicked: selectedStrategyType = "mean_reversion"
                            }
                            
                            // 动量策略
                            StrategyTypeCard {
                                icon: "⚡"
                                name: "动量策略"
                                description: "跟随价格动量，买入强势卖出弱势"
                                isSelected: selectedStrategyType === "momentum"
                                onClicked: selectedStrategyType = "momentum"
                            }
                            
                            // 统计套利策略
                            StrategyTypeCard {
                                icon: "💱"
                                name: "统计套利"
                                description: "利用相关资产价差进行对冲套利"
                                isSelected: selectedStrategyType === "arbitrage"
                                onClicked: selectedStrategyType = "arbitrage"
                            }
                            
                            // 机器学习策略
                            StrategyTypeCard {
                                icon: "🤖"
                                name: "机器学习"
                                description: "使用AI模型预测价格走势"
                                isSelected: selectedStrategyType === "ml"
                                onClicked: selectedStrategyType = "ml"
                            }
                        }
                    }
                }
                
                // 右侧：参数配置面板
                Rectangle {
                    id: configPanel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#1e293b"
                    radius: 12
                    border.width: 1
                    border.color: "#334155"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 24
                        
                        // 步骤1: 基本信息
                        ColumnLayout {
                            id: step1Content
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 16
                            visible: currentStep === 1
                            
                            // 步骤标题
                            Text {
                                text: "策略基本信息"
                                font.pixelSize: 20
                                font.weight: Font.DemiBold
                                color: "#f1f5f9"
                            }
                            
                            // 步骤描述
                            Text {
                                text: "填写策略的基本信息和核心逻辑"
                                font.pixelSize: 14
                                color: "#94a3b8"
                                wrapMode: Text.WordWrap
                            }
                            
                            // 策略名称输入
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Text {
                                    text: "策略名称 *"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: "#cbd5e1"
                                }
                                
                                TextField {
                                    id: strategyNameField
                                    Layout.fillWidth: true
                                    placeholderText: "例如：双均线金叉策略"
                                    text: root.strategyName
                                    onTextChanged: root.strategyName = text
                                    
                                    background: Rectangle {
                                        implicitHeight: 40
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
                            
                            // 策略描述输入
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Text {
                                    text: "策略描述 *"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: "#cbd5e1"
                                }
                                
                                TextArea {
                                    id: strategyDescField
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 100
                                    placeholderText: "描述策略的核心逻辑、入场条件、出场条件、适用市场等..."
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
                            
                            // 资产类型和时间框架选择
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 16
                                rowSpacing: 16
                                
                                // 资产类型选择
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    
                                    Text {
                                        text: "资产类型"
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        id: assetTypeCombo
                                        Layout.fillWidth: true
                                        model: ["股票", "期货", "加密货币"]
                                        currentIndex: 0
                                        onActivated: {
                                            var types = ["stock", "futures", "crypto"]
                                            root.assetType = types[currentIndex]
                                        }
                                        
                                        background: Rectangle {
                                            implicitHeight: 40
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: assetTypeCombo.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 14
                                            padding: 10
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        
                                        popup: Popup {
                                            y: assetTypeCombo.height
                                            width: assetTypeCombo.width
                                            implicitHeight: contentItem.implicitHeight
                                            padding: 1
                                            
                                            contentItem: ListView {
                                                clip: true
                                                implicitHeight: contentHeight
                                                model: assetTypeCombo.popup.visible ? assetTypeCombo.delegateModel : null
                                                
                                                ScrollIndicator.vertical: ScrollIndicator { }
                                            }
                                            
                                            background: Rectangle {
                                                color: "#0f172a"
                                                border.width: 1
                                                border.color: "#334155"
                                                radius: 6
                                            }
                                        }
                                    }
                                }
                                
                                // 时间框架选择
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    
                                    Text {
                                        text: "时间框架"
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        color: "#cbd5e1"
                                    }
                                    
                                    ComboBox {
                                        id: timeFrameCombo
                                        Layout.fillWidth: true
                                        model: ["日内", "日线", "周线"]
                                        currentIndex: 1
                                        onActivated: {
                                            var frames = ["intraday", "daily", "weekly"]
                                            root.timeFrame = frames[currentIndex]
                                        }
                                        
                                        background: Rectangle {
                                            implicitHeight: 40
                                            radius: 6
                                            color: "#0f172a"
                                            border.width: 1
                                            border.color: "#334155"
                                        }
                                        
                                        contentItem: Text {
                                            text: timeFrameCombo.displayText
                                            color: "#f1f5f9"
                                            font.pixelSize: 14
                                            padding: 10
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        
                                        popup: Popup {
                                            y: timeFrameCombo.height
                                            width: timeFrameCombo.width
                                            implicitHeight: contentItem.implicitHeight
                                            padding: 1
                                            
                                            contentItem: ListView {
                                                clip: true
                                                implicitHeight: contentHeight
                                                model: timeFrameCombo.popup.visible ? timeFrameCombo.delegateModel : null
                                                
                                                ScrollIndicator.vertical: ScrollIndicator { }
                                            }
                                            
                                            background: Rectangle {
                                                color: "#0f172a"
                                                border.width: 1
                                                border.color: "#334155"
                                                radius: 6
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        
                        // 步骤2: 参数配置
                        ColumnLayout {
                            id: step2Content
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 16
                            visible: currentStep === 2
                            
                            // 步骤标题
                            Text {
                                text: "策略参数配置"
                                font.pixelSize: 20
                                font.weight: Font.DemiBold
                                color: "#f1f5f9"
                            }
                            
                            // 步骤描述
                            Text {
                                text: "配置策略的核心参数和回测环境"
                                font.pixelSize: 14
                                color: "#94a3b8"
                                wrapMode: Text.WordWrap
                            }
                            
                            // 参数网格
                            GridLayout {
                                id: parameterGrid
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                columns: 2
                                columnSpacing: 16
                                rowSpacing: 16
                                
                                // 参数卡片将通过JS动态生成
                            }
                            
                            // 回测周期滑块
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Text {
                                    text: "回测周期"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: "#cbd5e1"
                                }
                                
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    
                                    // 滑块
                                    Slider {
                                        id: backtestYearsSlider
                                        Layout.fillWidth: true
                                        from: 1
                                        to: 5
                                        value: root.backtestYears
                                        stepSize: 1
                                        onValueChanged: root.backtestYears = value
                                        
                                        background: Rectangle {
                                            x: backtestYearsSlider.leftPadding
                                            y: backtestYearsSlider.topPadding + backtestYearsSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 200
                                            implicitHeight: 6
                                            width: backtestYearsSlider.availableWidth
                                            height: implicitHeight
                                            radius: 3
                                            color: "#334155"
                                            
                                            Rectangle {
                                                width: backtestYearsSlider.visualPosition * parent.width
                                                height: parent.height
                                                color: "#3b82f6"
                                                radius: 3
                                            }
                                        }
                                        
                                        handle: Rectangle {
                                            x: backtestYearsSlider.leftPadding + backtestYearsSlider.visualPosition * (backtestYearsSlider.availableWidth - width)
                                            y: backtestYearsSlider.topPadding + backtestYearsSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 20
                                            implicitHeight: 20
                                            radius: 10
                                            color: "#3b82f6"
                                            border.color: "#60a5fa"
                                            border.width: 2
                                        }
                                    }
                                    
                                    // 显示值
                                    Text {
                                        id: yearsValue
                                        text: root.backtestYears + "年"
                                        font.pixelSize: 12
                                        color: "#cbd5e1"
                                        Layout.minimumWidth: 40
                                    }
                                }
                            }
                        }
                        
                        // 步骤3: 预览确认
                        ColumnLayout {
                            id: step3Content
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 16
                            visible: currentStep === 3
                            
                            // 步骤标题
                            Text {
                                text: "预览确认"
                                font.pixelSize: 20
                                font.weight: Font.DemiBold
                                color: "#f1f5f9"
                            }
                            
                            // 步骤描述
                            Text {
                                text: "确认策略配置并创建"
                                font.pixelSize: 14
                                color: "#94a3b8"
                                wrapMode: Text.WordWrap
                            }
                            
                            // 预览网格
                            GridLayout {
                                id: previewGrid
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                columns: 2
                                columnSpacing: 16
                                rowSpacing: 16
                                
                                // 预览信息将通过JS动态生成
                            }
                        }
                        
                        // 按钮区域
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            
                            // 上一步按钮
                            Button {
                                id: prevBtn
                                text: "上一步"
                                visible: currentStep > 1
                                
                                background: Rectangle {
                                    implicitWidth: 100
                                    implicitHeight: 40
                                    radius: 6
                                    color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: prevBtn.text
                                    color: "#f1f5f9"
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                
                                onClicked: prevStep()
                            }
                            
                            // 间距填充
                            Item {
                                Layout.fillWidth: true
                            }
                            
                            // 下一步/创建按钮
                            Button {
                                id: nextBtn
                                text: currentStep === 3 ? "创建策略" : "下一步"
                                
                                background: Rectangle {
                                    implicitWidth: 120
                                    implicitHeight: 40
                                    radius: 6
                                    color: "#3b82f6"
                                }
                                
                                contentItem: Text {
                                    text: nextBtn.text
                                    color: "white"
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                
                                onClicked: nextStep()
                            }
                        }
                        
                        // 验证信息
                        Rectangle {
                            id: validationInfo
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            radius: 6
                            color: validationSuccess ? "#10b98120" : "#ef444420"
                            border.color: validationSuccess ? "#10b981" : "#ef4444"
                            border.width: 1
                            visible: validationMessage !== ""
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8
                                
                                // 图标
                                Text {
                                    text: validationSuccess ? "✅" : "⚠️"
                                    font.pixelSize: 16
                                }
                                
                                // 消息文本
                                Text {
                                    id: validationText
                                    text: validationMessage
                                    font.pixelSize: 12
                                    color: validationSuccess ? "#10b981" : "#ef4444"
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
    
    // ============ 组件定义 ============
    
    // 策略类型卡片组件
    component StrategyTypeCard: Rectangle {
        property string icon: ""
        property string name: ""
        property string description: ""
        property bool isSelected: false
        
        Layout.fillWidth: true
        Layout.preferredHeight: 80
        radius: 8
        color: isSelected ? "#0f172a" : "#0f172a"
        border.width: 1
        border.color: isSelected ? "#3b82f6" : "#334155"
        
        RowLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            
            // 图标
            Text {
                text: icon
                font.pixelSize: 24
                Layout.alignment: Qt.AlignTop
            }
            
            // 名称和描述
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                
                Text {
                    text: name
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: "#f1f5f9"
                }
                
                Text {
                    text: description
                    font.pixelSize: 12
                    color: "#94a3b8"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
        
        signal clicked()
    }
    
    // 参数卡片组件
    component ParameterCard: Rectangle {
        property string paramName: ""
        property string paramValue: ""
        property string paramDescription: ""
        
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        radius: 8
        color: "#0f172a"
        border.width: 1
        border.color: "#334155"
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8
            
            // 参数名称
            Text {
                text: paramName
                font.pixelSize: 13
                font.weight: Font.Medium
                color: "#cbd5e1"
                Layout.fillWidth: true
            }
            
            // 参数值
            Text {
                text: paramValue
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#3b82f6"
            }
            
            // 参数描述
            Text {
                text: paramDescription
                font.pixelSize: 12
                color: "#94a3b8"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    
    // 参数组标题组件
    component ParameterGroupTitle: Rectangle {
        property string groupTitle: ""
        
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 4
            
            // 组标题
            Text {
                text: groupTitle
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#f1f5f9"
                Layout.fillWidth: true
            }
            
            // 分隔线
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#334155"
            }
        }
    }
    
    // ============ JavaScript逻辑 ============
    
    // 策略参数示例数据
    property var strategyExamples: ({
        "trend": {
            "name": "趋势跟踪策略",
            "desc": "跟踪市场趋势，追涨杀跌，适合单边行情",
            "params": [
                {"id": "fastPeriod", "name": "快线周期", "type": "number", "value": 5, "min": 2, "max": 50, "step": 1, "desc": "快线移动平均线周期"},
                {"id": "slowPeriod", "name": "慢线周期", "type": "number", "value": 20, "min": 5, "max": 200, "step": 1, "desc": "慢线移动平均线周期"},
                {"id": "stopLoss", "name": "止损比例", "type": "percent", "value": 5, "min": 1, "max": 20, "step": 0.5, "desc": "止损触发比例"}
            ]
        },
        "mean_reversion": {
            "name": "均值回归策略",
            "desc": "认为价格会回归均值，适合震荡行情",
            "params": [
                {"id": "lookbackPeriod", "name": "回顾周期", "type": "number", "value": 20, "min": 5, "max": 100, "step": 1, "desc": "计算均值和标准差的回顾周期"},
                {"id": "entryThreshold", "name": "入场阈值", "type": "number", "value": 2.0, "min": 1.0, "max": 4.0, "step": 0.1, "desc": "价格偏离均值多少标准差时入场"},
                {"id": "exitThreshold", "name": "出场阈值", "type": "number", "value": 0.5, "min": 0.1, "max": 1.5, "step": 0.1, "desc": "价格回归到均值多少标准差时出场"},
                {"id": "gridLevels", "name": "网格层数", "type": "number", "value": 10, "min": 3, "max": 20, "step": 1, "desc": "网格交易的层数设置"}
            ]
        },
        "momentum": {
            "name": "动量策略",
            "desc": "跟随价格动量，买入强势卖出弱势",
            "params": [
                {"id": "momentumPeriod", "name": "动量周期", "type": "number", "value": 20, "min": 5, "max": 250, "step": 1, "desc": "计算动量的周期"},
                {"id": "selectionRatio", "name": "选股比例", "type": "percent", "value": 20, "min": 5, "max": 50, "step": 1, "desc": "选择动量最强股票的百分比"},
                {"id": "rebalancingPeriod", "name": "调仓周期", "type": "number", "value": 5, "min": 1, "max": 30, "step": 1, "desc": "重新筛选和调整仓位的周期"}
            ]
        },
        "arbitrage": {
            "name": "统计套利策略",
            "desc": "利用相关资产价差进行对冲套利",
            "params": [
                {"id": "lookbackDays", "name": "回看天数", "type": "number", "value": 60, "min": 20, "max": 200, "step": 1, "desc": "计算协整关系和历史标准差的天数"},
                {"id": "entryZScore", "name": "入场Z值", "type": "number", "value": 2.0, "min": 1.0, "max": 3.0, "step": 0.1, "desc": "价差偏离多少标准差时入场"},
                {"id": "exitZScore", "name": "出场Z值", "type": "number", "value": 0.5, "min": 0.1, "max": 1.5, "step": 0.1, "desc": "价差回归到多少标准差时出场"},
                {"id": "hedgeRatio", "name": "对冲比例", "type": "number", "value": 1.0, "min": 0.5, "max": 2.0, "step": 0.1, "desc": "配对中对冲头寸的比例"}
            ]
        },
        "ml": {
            "name": "机器学习策略",
            "desc": "使用AI模型预测价格走势",
            "params": [
                {"id": "featureWindow", "name": "特征窗口", "type": "number", "value": 60, "min": 10, "max": 250, "step": 1, "desc": "特征提取的时间窗口"},
                {"id": "predictionDays", "name": "预测天数", "type": "number", "value": 1, "min": 1, "max": 10, "step": 1, "desc": "预测未来价格的天数"},
                {"id": "trainingDays", "name": "训练天数", "type": "number", "value": 1000, "min": 500, "max": 5000, "step": 100, "desc": "模型训练使用的历史数据天数"},
                {"id": "confidenceThreshold", "name": "置信阈值", "type": "percent", "value": 60, "min": 50, "max": 90, "step": 1, "desc": "模型预测置信度阈值（百分比）"}
            ]
        }
    })
    
    // 通用参数
    property var commonParams: [
        {"id": "initialCapital", "name": "初始资金", "type": "money", "value": 1000000, "min": 10000, "max": 10000000, "step": 10000, "desc": "回测起始资金"},
        {"id": "commission", "name": "手续费率", "type": "percent", "value": 0.0003, "min": 0, "max": 0.05, "step": 0.0001, "desc": "交易手续费费率（百分比）"},
        {"id": "slippage", "name": "滑点成本", "type": "percent", "value": 0.001, "min": 0, "max": 0.05, "step": 0.0001, "desc": "交易滑点成本（百分比）"},
        {"id": "maxPosition", "name": "最大持仓", "type": "percent", "value": 80, "min": 10, "max": 100, "step": 5, "desc": "最大持仓占资金比例（百分比）"},
        {"id": "orderType", "name": "订单类型", "type": "select", "value": "限价单", "options": ["限价单", "市价单"], "desc": "策略使用的订单类型"}
    ]
    
    // ============ 功能函数 ============
    
    // 设置活动步骤
    function setActiveStep(step) {
        currentStep = step
        
        // 更新参数显示
        if (step === 2) {
            loadStrategyParams()
        } else if (step === 3) {
            updatePreview()
        }
        
        // 隐藏验证消息
        validationMessage = ""
    }
    
    // 上一步
    function prevStep() {
        if (currentStep > 1) {
            currentStep--
            validationMessage = ""
        }
    }
    
    // 下一步
    function nextStep() {
        if (currentStep === 1) {
            if (validateStep1()) {
                currentStep++
                loadStrategyParams()
            }
        } else if (currentStep === 2) {
            if (validateStep2()) {
                currentStep++
                updatePreview()
            }
        } else if (currentStep === 3) {
            createStrategy()
        }
    }
    
    // 验证第一步
    function validateStep1() {
        if (!strategyNameField.text.trim()) {
            showValidation("请填写策略名称", false)
            return false
        }
        
        if (!strategyDescField.text.trim()) {
            showValidation("请填写策略描述", false)
            return false
        }
        
        return true
    }
    
    // 验证第二步
    function validateStep2() {
        // 检查参数是否有效
        var hasParameters = Object.keys(strategyParameters).length > 0
        
        if (!hasParameters) {
            showValidation("请配置策略参数", false)
            return false
        }
        
        return true
    }
    
    // 创建参数组标题
    function createParameterGroupTitle(title) {
        var component = Qt.createComponent("ParameterGroupTitle.qml")
        if (component.status === Component.Ready) {
            var groupTitle = component.createObject(parameterGrid, {
                "groupTitle": title
            })
            // 设置组标题跨越两列
            groupTitle.Layout.columnSpan = 2
        }
    }
    
    // 加载策略参数
    function loadStrategyParams() {
        // 清空参数网格
        var children = parameterGrid.children
        for (var i = children.length - 1; i >= 0; i--) {
            children[i].destroy()
        }
        
        // 获取当前策略的参数
        var strategy = strategyExamples[selectedStrategyType]
        
        // 创建通用参数组标题
        createParameterGroupTitle("通用参数")
        
        // 创建通用参数卡片
        commonParams.forEach(function(param) {
            createParameterCard(param)
        })
        
        // 创建策略特定参数组标题
        createParameterGroupTitle("核心参数")
        
        // 创建策略特定参数卡片
        strategy.params.forEach(function(param) {
            createParameterCard(param)
        })
    }
    
    // 创建参数卡片
    function createParameterCard(param) {
        var component = Qt.createComponent("ParameterCard.qml")
        if (component.status === Component.Ready) {
            var card = component.createObject(parameterGrid, {
                "paramName": param.name,
                "paramValue": param.type === 'percent' ? param.value + '%' : 
                              param.type === 'money' ? formatMoney(param.value) : param.value,
                "paramDescription": param.desc
            })
            
            // 保存参数到策略参数对象
            strategyParameters[param.id] = param.value
        }
    }
    
    // 更新预览
    function updatePreview() {
        // 清空预览网格
        var children = previewGrid.children
        for (var i = children.length - 1; i >= 0; i--) {
            children[i].destroy()
        }
        
        var strategy = strategyExamples[selectedStrategyType]
        
        // 创建预览卡片
        var previewData = [
            {"name": "策略类型", "value": strategy.name},
            {"name": "策略描述", "value": strategy.desc, "isDescription": true},
            {"name": "资产类型", "value": getAssetTypeName(assetType)},
            {"name": "时间框架", "value": getTimeFrameName(timeFrame)},
            {"name": "回测周期", "value": backtestYears + "年"},
            {"name": "初始资金", "value": formatMoney(1000000)}
        ]
        
        previewData.forEach(function(item) {
            var component = Qt.createComponent("ParameterCard.qml")
            if (component.status === Component.Ready) {
                var card = component.createObject(previewGrid, {
                    "paramName": item.name,
                    "paramValue": item.value,
                    "paramDescription": item.isDescription ? "" : item.name + "配置"
                })
            }
        })
    }
    
    // 格式化金额
    function formatMoney(value) {
        if (value >= 1000000) {
            return (value / 1000000).toFixed(1) + 'M'
        } else if (value >= 1000) {
            return (value / 1000).toFixed(1) + 'K'
        }
        return value.toString()
    }
    
    // 获取资产类型名称
    function getAssetTypeName(type) {
        var names = {
            "stock": "股票",
            "futures": "期货",
            "crypto": "加密货币"
        }
        return names[type] || type
    }
    
    // 获取时间框架名称
    function getTimeFrameName(frame) {
        var names = {
            "intraday": "日内",
            "daily": "日线",
            "weekly": "周线"
        }
        return names[frame] || frame
    }
    
    // 显示验证消息
    function showValidation(message, isSuccess) {
        validationMessage = message
        validationSuccess = isSuccess
        
        // 3秒后自动隐藏
        hideValidationTimer.restart()
    }
    
    // 创建策略
    function createStrategy() {
        // 构建策略数据
        var strategyData = {
            "name": strategyName,
            "description": strategyDescription,
            "type": selectedStrategyType,
            "strategyType": strategyExamples[selectedStrategyType].name,
            "assetType": assetType,
            "timeFrame": timeFrame,
            "backtestYears": backtestYears,
            "parameters": strategyParameters,
            "createdDate": new Date().toISOString().split('T')[0]
        }
        
        // 显示成功消息
        showValidation("策略创建成功！即将开始回测...", true)
        
        // 触发创建信号
        strategyCreated(strategyData)
        
        // 重置表单
        resetForm()
    }
    
    // 重置表单
    function resetForm() {
        strategyName = ""
        strategyDescription = ""
        selectedStrategyType = "trend"
        assetType = "stock"
        timeFrame = "daily"
        backtestYears = 3
        strategyParameters = {}
        currentStep = 1
        
        // 重置输入字段
        strategyNameField.text = ""
        strategyDescField.text = ""
        assetTypeCombo.currentIndex = 0
        timeFrameCombo.currentIndex = 1
        backtestYearsSlider.value = 3
    }
    
    // ============ 定时器 ============
    
    Timer {
        id: hideValidationTimer
        interval: 3000
        onTriggered: {
            validationMessage = ""
        }
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化资产类型和时间框架
        assetType = "stock"
        timeFrame = "daily"
        
        // 加载初始参数
        loadStrategyParams()
    }
    
    // ============ 属性变化监听 ============
    
    onSelectedStrategyTypeChanged: {
        // 当策略类型改变时，重新加载参数
        if (currentStep === 2) {
            loadStrategyParams()
        }
    }
}
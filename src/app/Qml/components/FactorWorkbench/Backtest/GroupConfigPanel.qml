// GroupConfigPanel.qml
// 分组配置面板组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

/**
 * 分组配置面板组件
 * 提供因子分组的高级配置选项
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property var defaultConfig: ({})
    property var currentConfig: ({})
    
    // 配置变更信号
    signal configChanged(var config)
    
    // ============ UI ============
    
    Rectangle {
        anchors.fill: parent
        radius: 12
        color: "#1E293B"
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16
            
            // 标题
            Text {
                text: "⚙️ 分组配置"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            // 分组方法选择
            ColumnLayout {
                spacing: 8
                
                Text {
                    text: "分组方法"
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
                
                RowLayout {
                    spacing: 12
                    
                    // 分位数分组
                    RadioButton {
                        id: quantileRadio
                        text: "分位数分组"
                        checked: true
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 12
                            color: parent.checked ? "#3B82F6" : "#94A3B8"
                        }
                        
                        indicator: Rectangle {
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            border.width: 2
                            border.color: parent.checked ? "#3B82F6" : "#64748B"
                            
                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: "#3B82F6"
                                visible: parent.parent.checked
                            }
                        }
                        
                        onCheckedChanged: if (checked) updateConfig()
                    }
                    
                    // 等值分组
                    RadioButton {
                        id: equalValueRadio
                        text: "等值分组"
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 12
                            color: parent.checked ? "#3B82F6" : "#94A3B8"
                        }
                        
                        indicator: Rectangle {
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            border.width: 2
                            border.color: parent.checked ? "#3B82F6" : "#64748B"
                            
                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: "#3B82F6"
                                visible: parent.parent.checked
                            }
                        }
                        
                        onCheckedChanged: if (checked) updateConfig()
                    }
                    
                    // 自定义分组
                    RadioButton {
                        id: customRadio
                        text: "自定义分组"
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 12
                            color: parent.checked ? "#3B82F6" : "#94A3B8"
                        }
                        
                        indicator: Rectangle {
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            border.width: 2
                            border.color: parent.checked ? "#3B82F6" : "#64748B"
                            
                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: "#3B82F6"
                                visible: parent.parent.checked
                            }
                        }
                        
                        onCheckedChanged: if (checked) updateConfig()
                    }
                }
            }
            
            // 分组数量配置
            ColumnLayout {
                spacing: 8
                
                Text {
                    text: "分组数量"
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
                
                RowLayout {
                    spacing: 12
                    
                    // 分组数量滑块
                    Slider {
                        id: groupCountSlider
                        Layout.fillWidth: true
                        from: 2
                        to: 20
                        value: 10
                        stepSize: 1
                        
                        background: Rectangle {
                            x: parent.leftPadding
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            implicitWidth: 200
                            implicitHeight: 4
                            width: parent.availableWidth
                            height: implicitHeight
                            radius: 2
                            color: "#334155"
                            
                            Rectangle {
                                width: parent.width * (parent.parent.value - parent.parent.from) / (parent.parent.to - parent.parent.from)
                                height: parent.height
                                color: "#3B82F6"
                                radius: 2
                            }
                        }
                        
                        handle: Rectangle {
                            x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            color: "#3B82F6"
                            border.color: "#1E40AF"
                        }
                        
                        onValueChanged: updateConfig()
                    }
                    
                    // 分组数量显示
                    Text {
                        text: groupCountSlider.value.toFixed(0) + "组"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        Layout.preferredWidth: 60
                    }
                }
            }
            
            // 回测策略选择
            ColumnLayout {
                spacing: 8
                
                Text {
                    text: "回测策略"
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
                
                RowLayout {
                    spacing: 12
                    
                    // 等权重策略
                    RadioButton {
                        id: equalWeightRadio
                        text: "等权重"
                        checked: true
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 12
                            color: parent.checked ? "#3B82F6" : "#94A3B8"
                        }
                        
                        indicator: Rectangle {
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            border.width: 2
                            border.color: parent.checked ? "#3B82F6" : "#64748B"
                            
                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: "#3B82F6"
                                visible: parent.parent.checked
                            }
                        }
                        
                        onCheckedChanged: if (checked) updateConfig()
                    }
                    
                    // 因子权重策略
                    RadioButton {
                        id: factorWeightRadio
                        text: "因子权重"
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 12
                            color: parent.checked ? "#3B82F6" : "#94A3B8"
                        }
                        
                        indicator: Rectangle {
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            border.width: 2
                            border.color: parent.checked ? "#3B82F6" : "#64748B"
                            
                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: "#3B82F6"
                                visible: parent.parent.checked
                            }
                        }
                        
                        onCheckedChanged: if (checked) updateConfig()
                    }
                    
                    // 风险平价策略
                    RadioButton {
                        id: riskParityRadio
                        text: "风险平价"
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 12
                            color: parent.checked ? "#3B82F6" : "#94A3B8"
                        }
                        
                        indicator: Rectangle {
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            border.width: 2
                            border.color: parent.checked ? "#3B82F6" : "#64748B"
                            
                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: "#3B82F6"
                                visible: parent.parent.checked
                            }
                        }
                        
                        onCheckedChanged: if (checked) updateConfig()
                    }
                }
            }
            
            // 高级配置
            ColumnLayout {
                spacing: 8
                
                Text {
                    text: "高级配置"
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
                
                // 交易成本
                RowLayout {
                    spacing: 12
                    
                    Text {
                        text: "交易成本"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        Layout.preferredWidth: 80
                    }
                    
                    Slider {
                        id: transactionCostSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 0.01
                        value: 0.001
                        stepSize: 0.0001
                        
                        background: Rectangle {
                            x: parent.leftPadding
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            implicitWidth: 200
                            implicitHeight: 4
                            width: parent.availableWidth
                            height: implicitHeight
                            radius: 2
                            color: "#334155"
                            
                            Rectangle {
                                width: parent.width * (parent.parent.value - parent.parent.from) / (parent.parent.to - parent.parent.from)
                                height: parent.height
                                color: "#3B82F6"
                                radius: 2
                            }
                        }
                        
                        handle: Rectangle {
                            x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            color: "#3B82F6"
                            border.color: "#1E40AF"
                        }
                        
                        onValueChanged: updateConfig()
                    }
                    
                    Text {
                        text: (transactionCostSlider.value * 100).toFixed(2) + "%"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        Layout.preferredWidth: 60
                    }
                }
                
                // 滑点
                RowLayout {
                    spacing: 12
                    
                    Text {
                        text: "滑点"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        Layout.preferredWidth: 80
                    }
                    
                    Slider {
                        id: slippageSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 0.01
                        value: 0.001
                        stepSize: 0.0001
                        
                        background: Rectangle {
                            x: parent.leftPadding
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            implicitWidth: 200
                            implicitHeight: 4
                            width: parent.availableWidth
                            height: implicitHeight
                            radius: 2
                            color: "#334155"
                            
                            Rectangle {
                                width: parent.width * (parent.parent.value - parent.parent.from) / (parent.parent.to - parent.parent.from)
                                height: parent.height
                                color: "#3B82F6"
                                radius: 2
                            }
                        }
                        
                        handle: Rectangle {
                            x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            color: "#3B82F6"
                            border.color: "#1E40AF"
                        }
                        
                        onValueChanged: updateConfig()
                    }
                    
                    Text {
                        text: (slippageSlider.value * 100).toFixed(2) + "%"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        Layout.preferredWidth: 60
                    }
                }
                
                // 初始资金
                RowLayout {
                    spacing: 12
                    
                    Text {
                        text: "初始资金"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        Layout.preferredWidth: 80
                    }
                    
                    TextField {
                        id: initialCapitalField
                        Layout.fillWidth: true
                        text: "1000000"
                        placeholderText: "输入初始资金"
                        
                        background: Rectangle {
                            implicitWidth: 200
                            implicitHeight: 32
                            radius: 6
                            color: "#0F172A"
                            border.width: 1
                            border.color: "#334155"
                        }
                        
                        color: "#F1F5F9"
                        font.pixelSize: 12
                        selectByMouse: true
                        
                        validator: DoubleValidator {
                            bottom: 10000
                            top: 1000000000
                            decimals: 0
                        }
                        
                        onTextChanged: updateConfig()
                    }
                    
                    Text {
                        text: "元"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        Layout.preferredWidth: 30
                    }
                }
            }
            
            Item { Layout.fillHeight: true }
            
            // 配置摘要
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                radius: 8
                color: "#0F172A"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4
                    
                    Text {
                        text: "📋 配置摘要"
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }
                    
                    Text {
                        text: getConfigSummary()
                        font.pixelSize: 10
                        color: "#F1F5F9"
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    
    // 获取分组方法
    function getGroupingMethod() {
        if (quantileRadio.checked) return "quantile"
        if (equalValueRadio.checked) return "equal_value"
        if (customRadio.checked) return "custom"
        return "quantile"
    }
    
    // 获取回测策略
    function getStrategy() {
        if (equalWeightRadio.checked) return "equal_weight"
        if (factorWeightRadio.checked) return "factor_weight"
        if (riskParityRadio.checked) return "risk_parity"
        return "equal_weight"
    }
    
    // 更新配置
    function updateConfig() {
        var config = {
            groupingMethod: getGroupingMethod(),
            numGroups: parseInt(groupCountSlider.value),
            strategy: getStrategy(),
            transactionCost: transactionCostSlider.value,
            slippage: slippageSlider.value,
            initialCapital: parseFloat(initialCapitalField.text) || 1000000
        }
        
        currentConfig = config
        configChanged(config)
    }
    
    // 获取配置摘要
    function getConfigSummary() {
        var methodText = ""
        switch(getGroupingMethod()) {
            case "quantile": methodText = "分位数分组"; break
            case "equal_value": methodText = "等值分组"; break
            case "custom": methodText = "自定义分组"; break
        }
        
        var strategyText = ""
        switch(getStrategy()) {
            case "equal_weight": strategyText = "等权重策略"; break
            case "factor_weight": strategyText = "因子权重策略"; break
            case "risk_parity": strategyText = "风险平价策略"; break
        }
        
        return methodText + " · " + groupCountSlider.value.toFixed(0) + "组 · " + strategyText
    }
    
    // 加载配置
    function loadConfig(config) {
        if (!config) return
        
        // 设置分组方法
        switch(config.groupingMethod) {
            case "quantile": quantileRadio.checked = true; break
            case "equal_value": equalValueRadio.checked = true; break
            case "custom": customRadio.checked = true; break
        }
        
        // 设置分组数量
        if (config.numGroups) {
            groupCountSlider.value = config.numGroups
        }
        
        // 设置回测策略
        switch(config.strategy) {
            case "equal_weight": equalWeightRadio.checked = true; break
            case "factor_weight": factorWeightRadio.checked = true; break
            case "risk_parity": riskParityRadio.checked = true; break
        }
        
        // 设置交易成本
        if (config.transactionCost) {
            transactionCostSlider.value = config.transactionCost
        }
        
        // 设置滑点
        if (config.slippage) {
            slippageSlider.value = config.slippage
        }
        
        // 设置初始资金
        if (config.initialCapital) {
            initialCapitalField.text = config.initialCapital.toString()
        }
        
        updateConfig()
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("分组配置面板初始化完成")
        updateConfig()
    }
}
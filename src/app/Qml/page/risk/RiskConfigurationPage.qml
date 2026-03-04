// RiskConfigurationPage.qml - 风险管理配置页面
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../components/Risk" as RiskComponents
import ConsoleUi 1.0 as Theme

Item {
    id: riskConfigPage
    
    // 风险配置属性
    property var riskConfig: ({
        // 止损止盈配置
        stopLoss: -0.05,      // 止损线 -5%
        takeProfit: 0.20,     // 止盈线 +20%
        trailingStop: 0.10,   // 移动止损 10%
        
        // 仓位控制
        maxPositionSize: 0.30,    // 单股最大仓位 30%
        maxPositions: 10,         // 最大持仓数
        maxTotalExposure: 0.95,   // 最大总仓位 95%
        
        // 行业/题材仓位控制
        maxIndustryExposure: 0.40, // 单一行业最大仓位 40%
        maxThemeExposure: 0.25,    // 单一题材最大仓位 25%
        
        // 账户风险
        maxDailyLoss: -0.03,      // 单日最大亏损 -3%
        maxDrawdown: -0.15,       // 最大回撤 -15%
        
        // 其他
        minHoldingPeriod: 1,      // 最小持仓天数
        maxCorrelation: 0.7       // 持仓最大相关性
    })
    
    // 滚动区域
    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        
        ColumnLayout {
            width: Math.min(parent.width, 1200) - 20
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 20
            
            // ============= 1. 页面标题 =============
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                
                Text {
                    text: "风险管理配置"
                    font.pixelSize: 28
                    font.bold: true
                    color: Theme.darkText
                }
                
                Item { Layout.fillWidth: true }
                
                // 保存按钮
                Button {
                    text: "保存配置"
                    font.pixelSize: 14
                    font.bold: true
                    padding: 12
                    background: Rectangle {
                        radius: 8
                        color: Theme.accentColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        saveRiskConfiguration()
                    }
                }
            }
            
            // ============= 2. 风险配置说明 =============
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                radius: 8
                color: Theme.darkCard
                border.color: Theme.darkBorder
                border.width: 1
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    
                    Image {
                        source: "qrc:/icons/shield.svg"
                        width: 32
                        height: 32
                        Layout.alignment: Qt.AlignVCenter
                    }
                    
                    ColumnLayout {
                        spacing: 4
                        
                        Text {
                            text: "风险配置说明"
                            font.pixelSize: 16
                            font.bold: true
                            color: Theme.darkText
                        }
                        
                        Text {
                            text: "配置止损止盈、仓位限制、风险监控等参数，确保交易安全"
                            font.pixelSize: 14
                            color: Theme.darkTextSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            
            // ============= 3. 止损止盈配置 =============
            RiskComponents.RiskCategoryCard {
                Layout.fillWidth: true
                title: "止损止盈配置"
                icon: "qrc:/icons/stop-circle.svg"
                description: "设置止损、止盈和移动止损参数"
                
                GridLayout {
                    width: parent.width
                    columns: 3
                    rowSpacing: 16
                    columnSpacing: 20
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "止损比例"
                        value: riskConfig.stopLoss * 100
                        minValue: -20
                        maxValue: 0
                        unit: "%"
                        description: "当亏损达到此比例时自动平仓"
                        onValueChanged: {
                            riskConfig.stopLoss = value / 100
                        }
                    }
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "止盈比例"
                        value: riskConfig.takeProfit * 100
                        minValue: 0
                        maxValue: 50
                        unit: "%"
                        description: "当盈利达到此比例时自动平仓"
                        onValueChanged: {
                            riskConfig.takeProfit = value / 100
                        }
                    }
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "移动止损"
                        value: riskConfig.trailingStop * 100
                        minValue: 0
                        maxValue: 30
                        unit: "%"
                        description: "从最高点回撤比例触发止损"
                        onValueChanged: {
                            riskConfig.trailingStop = value / 100
                        }
                    }
                }
            }
            
            // ============= 4. 仓位控制配置 =============
            RiskComponents.RiskCategoryCard {
                Layout.fillWidth: true
                title: "仓位控制配置"
                icon: "qrc:/icons/trending-up.svg"
                description: "设置单股、总仓位和持仓数量限制"
                
                GridLayout {
                    width: parent.width
                    columns: 3
                    rowSpacing: 16
                    columnSpacing: 20
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "单股最大仓位"
                        value: riskConfig.maxPositionSize * 100
                        minValue: 5
                        maxValue: 100
                        unit: "%"
                        description: "单只股票最大持仓比例"
                        onValueChanged: {
                            riskConfig.maxPositionSize = value / 100
                        }
                    }
                    
                    RiskComponents.RiskParameterInput {
                        Layout.fillWidth: true
                        parameterName: "最大持仓数"
                        value: riskConfig.maxPositions
                        minValue: 1
                        maxValue: 50
                        unit: "只"
                        description: "同时持有的最大股票数量"
                        onValueChanged: {
                            riskConfig.maxPositions = value
                        }
                    }
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "最大总仓位"
                        value: riskConfig.maxTotalExposure * 100
                        minValue: 10
                        maxValue: 100
                        unit: "%"
                        description: "所有持仓总市值占资金比例"
                        onValueChanged: {
                            riskConfig.maxTotalExposure = value / 100
                        }
                    }
                }
            }
            
            // ============= 5. 行业/题材仓位限制 =============
            RiskComponents.RiskCategoryCard {
                Layout.fillWidth: true
                title: "行业与题材仓位限制"
                icon: "qrc:/icons/layers.svg"
                description: "设置行业和题材集中度限制"
                
                GridLayout {
                    width: parent.width
                    columns: 2
                    rowSpacing: 16
                    columnSpacing: 20
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "单一行业最大仓位"
                        value: riskConfig.maxIndustryExposure * 100
                        minValue: 10
                        maxValue: 100
                        unit: "%"
                        description: "同一行业股票总持仓比例限制"
                        onValueChanged: {
                            riskConfig.maxIndustryExposure = value / 100
                        }
                    }
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "单一题材最大仓位"
                        value: riskConfig.maxThemeExposure * 100
                        minValue: 5
                        maxValue: 50
                        unit: "%"
                        description: "同一题材股票总持仓比例限制"
                        onValueChanged: {
                            riskConfig.maxThemeExposure = value / 100
                        }
                    }
                }
            }
            
            // ============= 6. 账户风险配置 =============
            RiskComponents.RiskCategoryCard {
                Layout.fillWidth: true
                title: "账户风险配置"
                icon: "qrc:/icons/alert-triangle.svg"
                description: "设置账户级别的风险控制参数"
                
                GridLayout {
                    width: parent.width
                    columns: 2
                    rowSpacing: 16
                    columnSpacing: 20
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "单日最大亏损"
                        value: riskConfig.maxDailyLoss * 100
                        minValue: -10
                        maxValue: 0
                        unit: "%"
                        description: "单日账户净值最大回撤限制"
                        onValueChanged: {
                            riskConfig.maxDailyLoss = value / 100
                        }
                    }
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "最大回撤"
                        value: riskConfig.maxDrawdown * 100
                        minValue: -30
                        maxValue: 0
                        unit: "%"
                        description: "账户历史最大回撤限制"
                        onValueChanged: {
                            riskConfig.maxDrawdown = value / 100
                        }
                    }
                }
            }
            
            // ============= 7. 其他配置 =============
            RiskComponents.RiskCategoryCard {
                Layout.fillWidth: true
                title: "其他配置"
                icon: "qrc:/icons/settings.svg"
                description: "设置持仓期和相关性限制"
                
                GridLayout {
                    width: parent.width
                    columns: 2
                    rowSpacing: 16
                    columnSpacing: 20
                    
                    RiskComponents.RiskParameterInput {
                        Layout.fillWidth: true
                        parameterName: "最小持仓天数"
                        value: riskConfig.minHoldingPeriod
                        minValue: 0
                        maxValue: 30
                        unit: "天"
                        description: "最小持仓时间，防止频繁交易"
                        onValueChanged: {
                            riskConfig.minHoldingPeriod = value
                        }
                    }
                    
                    RiskComponents.RiskParameterSlider {
                        Layout.fillWidth: true
                        parameterName: "最大持仓相关性"
                        value: riskConfig.maxCorrelation * 100
                        minValue: 0
                        maxValue: 100
                        unit: "%"
                        description: "持仓股票间最大允许相关性"
                        onValueChanged: {
                            riskConfig.maxCorrelation = value / 100
                        }
                    }
                }
            }
            
            // ============= 8. 配置预览 =============
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                radius: 8
                color: Theme.darkCard
                border.color: Theme.darkBorder
                border.width: 1
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    
                    Text {
                        text: "配置预览"
                        font.pixelSize: 18
                        font.bold: true
                        color: Theme.darkText
                    }
                    
                    // 配置摘要
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 20
                        
                        // 第一列
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "止损: " + (riskConfig.stopLoss * 100).toFixed(1) + "%"
                                font.pixelSize: 14
                                color: riskConfig.stopLoss < -0.03 ? Theme.dangerColor : Theme.successColor
                            }
                            
                            Text {
                                text: "止盈: " + (riskConfig.takeProfit * 100).toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.successColor
                            }
                            
                            Text {
                                text: "单股仓位: " + (riskConfig.maxPositionSize * 100).toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                            
                            Text {
                                text: "最大持仓数: " + riskConfig.maxPositions + " 只"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                        }
                        
                        // 第二列
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "总仓位: " + (riskConfig.maxTotalExposure * 100).toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                            
                            Text {
                                text: "单日亏损: " + (riskConfig.maxDailyLoss * 100).toFixed(1) + "%"
                                font.pixelSize: 14
                                color: riskConfig.maxDailyLoss < -0.05 ? Theme.dangerColor : Theme.warningColor
                            }
                            
                            Text {
                                text: "最大回撤: " + (riskConfig.maxDrawdown * 100).toFixed(1) + "%"
                                font.pixelSize: 14
                                color: riskConfig.maxDrawdown < -0.10 ? Theme.dangerColor : Theme.warningColor
                            }
                            
                            Text {
                                text: "行业限制: " + (riskConfig.maxIndustryExposure * 100).toFixed(1) + "%"
                                font.pixelSize: 14
                                color: Theme.accentColor
                            }
                        }
                    }
                    
                    // 风险等级评估
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        radius: 4
                        color: getRiskLevelColor()
                        
                        Text {
                            anchors.centerIn: parent
                            text: "风险等级: " + getRiskLevelText()
                            font.pixelSize: 14
                            font.bold: true
                            color: "white"
                        }
                    }
                }
            }
            
            // ============= 9. 操作按钮 =============
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                spacing: 16
                
                Button {
                    text: "重置为默认"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    font.pixelSize: 14
                    
                    onClicked: {
                        resetToDefaults()
                    }
                }
                
                Button {
                    text: "应用配置"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    font.pixelSize: 14
                    font.bold: true
                    background: Rectangle {
                        radius: 8
                        color: Theme.accentColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        applyRiskConfiguration()
                    }
                }
            }
        }
    }
    
    // ============= 辅助函数 =============
    
    // 保存风险配置
    function saveRiskConfiguration() {
        console.log("保存风险配置:", JSON.stringify(riskConfig))
        showNotification("风险配置已保存")
        
        // 这里可以添加保存到数据库或配置文件的逻辑
        // riskService.saveConfiguration(riskConfig)
    }
    
    // 应用风险配置
    function applyRiskConfiguration() {
        console.log("应用风险配置:", JSON.stringify(riskConfig))
        showNotification("风险配置已应用")
        
        // 这里可以添加应用到风险管理的逻辑
        // riskManager.applyConfiguration(riskConfig)
        
        // 保存配置
        saveRiskConfiguration()
    }
    
    // 重置为默认值
    function resetToDefaults() {
        console.log("重置风险配置为默认值")
        
        riskConfig = {
            stopLoss: -0.05,
            takeProfit: 0.20,
            trailingStop: 0.10,
            maxPositionSize: 0.30,
            maxPositions: 10,
            maxTotalExposure: 0.95,
            maxIndustryExposure: 0.40,
            maxThemeExposure: 0.25,
            maxDailyLoss: -0.03,
            maxDrawdown: -0.15,
            minHoldingPeriod: 1,
            maxCorrelation: 0.7
        }
        
        showNotification("已重置为默认风险配置")
    }
    
    // 获取风险等级颜色
    function getRiskLevelColor() {
        var riskScore = calculateRiskScore()
        
        if (riskScore < 3) {
            return Theme.successColor  // 低风险 - 绿色
        } else if (riskScore < 7) {
            return Theme.warningColor  // 中风险 - 黄色
        } else {
            return Theme.dangerColor   // 高风险 - 红色
        }
    }
    
    // 获取风险等级文本
    function getRiskLevelText() {
        var riskScore = calculateRiskScore()
        
        if (riskScore < 3) {
            return "低风险"
        } else if (riskScore < 7) {
            return "中风险"
        } else {
            return "高风险"
        }
    }
    
    // 计算风险评分（简化版）
    function calculateRiskScore() {
        var score = 0
        
        // 止损止盈评分
        if (riskConfig.stopLoss < -0.08) score += 2
        else if (riskConfig.stopLoss < -0.05) score += 1
        
        if (riskConfig.takeProfit > 0.30) score += 2
        else if (riskConfig.takeProfit > 0.20) score += 1
        
        // 仓位控制评分
        if (riskConfig.maxPositionSize > 0.50) score += 2
        else if (riskConfig.maxPositionSize > 0.30) score += 1
        
        if (riskConfig.maxTotalExposure > 0.95) score += 2
        else if (riskConfig.maxTotalExposure > 0.90) score += 1
        
        // 账户风险评分
        if (riskConfig.maxDailyLoss < -0.05) score += 2
        else if (riskConfig.maxDailyLoss < -0.03) score += 1
        
        if (riskConfig.maxDrawdown < -0.20) score += 2
        else if (riskConfig.maxDrawdown < -0.15) score += 1
        
        // 行业集中度评分
        if (riskConfig.maxIndustryExposure > 0.50) score += 2
        else if (riskConfig.maxIndustryExposure > 0.40) score += 1
        
        return Math.min(score, 10) // 最高10分
    }
    
    // 显示通知
    function showNotification(message) {
        console.log("通知:", message)
        // 这里可以实现更复杂的通知系统
        // notificationPanel.show(message)
    }
}

// BacktestConfigDialog.qml
// 回测配置对话框组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

/**
 * 回测配置对话框组件
 * 提供因子回测的高级配置选项
 */
Rectangle {
    id: root
    
    // ============ 属性 ============
    
    property Bridge.FactorBacktestController factorBacktestController: null
    property string factorId: ""
    property string factorName: ""
    
    signal configSaved(var config)
    signal cancelled()
    
    // 配置属性
    property var config: ({})
    
    // ============ UI ============
    
    width: 600
    height: 700
    radius: 12
    color: "#1E293B"
    border.width: 1
    border.color: "#334155"
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        
        // 标题
        Text {
            text: "⚙️ 回测配置"
            font.pixelSize: 20
            font.weight: Font.DemiBold
            color: "#F1F5F9"
        }
        
        // 因子信息
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            radius: 8
            color: "#0F172A"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4
                
                Text {
                    text: "当前因子"
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
                
                Text {
                    text: factorName || factorId || "未选择因子"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: factorName ? "#3B82F6" : "#94A3B8"
                }
                
                Text {
                    text: "ID: " + (factorId || "未选择")
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
            }
        }
        
        // 配置表单
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            ColumnLayout {
                width: parent.width
                spacing: 16
                
                // 回测周期
                ColumnLayout {
                    spacing: 8
                    
                    Text {
                        text: "回测周期"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                    }
                    
                    RowLayout {
                        spacing: 12
                        
                        // 开始日期
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "开始日期"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            TextField {
                                id: startDateField
                                Layout.preferredWidth: 120
                                placeholderText: "YYYY-MM-DD"
                                text: "2010-01-01"
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                color: "#F1F5F9"
                                font.pixelSize: 12
                            }
                        }
                        
                        // 结束日期
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "结束日期"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            TextField {
                                id: endDateField
                                Layout.preferredWidth: 120
                                placeholderText: "YYYY-MM-DD"
                                text: getTodayDate()
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                color: "#F1F5F9"
                                font.pixelSize: 12
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                    }
                }
                
                // 分组设置
                ColumnLayout {
                    spacing: 8
                    
                    Text {
                        text: "分组设置"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                    }
                    
                    RowLayout {
                        spacing: 12
                        
                        // 分组方法
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "分组方法"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            ComboBox {
                                id: groupingMethodCombo
                                Layout.preferredWidth: 140
                                model: ["分位数分组", "等值分组", "自定义分组"]
                                currentIndex: 0
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: parent.displayText
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        // 分组数量
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "分组数量"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            SpinBox {
                                id: numGroupsSpin
                                Layout.preferredWidth: 80
                                from: 2
                                to: 20
                                value: 10
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: parent.value
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                    }
                }
                
                // 回测策略
                ColumnLayout {
                    spacing: 8
                    
                    Text {
                        text: "回测策略"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                    }
                    
                    ComboBox {
                        id: strategyCombo
                        Layout.preferredWidth: 200
                        model: ["等权重策略", "因子权重策略", "风险平价策略", "自定义策略"]
                        currentIndex: 0
                        
                        background: Rectangle {
                            radius: 6
                            color: "#0F172A"
                            border.width: 1
                            border.color: "#334155"
                        }
                        
                        contentItem: Text {
                            text: parent.displayText
                            font.pixelSize: 12
                            color: "#F1F5F9"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                
                // 资金设置
                ColumnLayout {
                    spacing: 8
                    
                    Text {
                        text: "资金设置"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                    }
                    
                    RowLayout {
                        spacing: 12
                        
                        // 初始资金
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "初始资金"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            TextField {
                                id: initialCapitalField
                                Layout.preferredWidth: 120
                                text: "1000000"
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                color: "#F1F5F9"
                                font.pixelSize: 12
                                validator: DoubleValidator { bottom: 0 }
                            }
                        }
                        
                        // 交易成本
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "交易成本 (%)"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            TextField {
                                id: transactionCostField
                                Layout.preferredWidth: 80
                                text: "0.1"
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                color: "#F1F5F9"
                                font.pixelSize: 12
                                validator: DoubleValidator { bottom: 0; top: 100 }
                            }
                        }
                        
                        // 滑点
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "滑点 (%)"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            TextField {
                                id: slippageField
                                Layout.preferredWidth: 80
                                text: "0.1"
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                color: "#F1F5F9"
                                font.pixelSize: 12
                                validator: DoubleValidator { bottom: 0; top: 100 }
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                    }
                }
                
                // 性能设置
                ColumnLayout {
                    spacing: 8
                    
                    Text {
                        text: "性能设置"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                    }
                    
                    RowLayout {
                        spacing: 12
                        
                        // 最大线程数
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "最大线程数"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            SpinBox {
                                id: maxThreadsSpin
                                Layout.preferredWidth: 80
                                from: 1
                                to: 16
                                value: 4
                                
                                background: Rectangle {
                                    radius: 6
                                    color: "#0F172A"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                
                                contentItem: Text {
                                    text: parent.value
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        
                        // 缓存设置
                        ColumnLayout {
                            spacing: 4
                            
                            Text {
                                text: "缓存设置"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            Row {
                                spacing: 12
                                
                                CheckBox {
                                    id: enableCacheCheck
                                    checked: true
                                    text: "启用缓存"
                                    
                                    contentItem: Text {
                                        text: parent.text
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: parent.indicator.width + parent.spacing
                                    }
                                }
                                
                                TextField {
                                    id: cacheTTLField
                                    width: 80
                                    text: "3600"
                                    enabled: enableCacheCheck.checked
                                    placeholderText: "秒"
                                    
                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }
                                    
                                    color: "#F1F5F9"
                                    font.pixelSize: 12
                                    validator: IntValidator { bottom: 0 }
                                }
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                    }
                }
                
                Item { Layout.fillHeight: true }
            }
        }
        
        // 按钮区域
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            
            // 验证按钮
            Rectangle {
                Layout.preferredWidth: 100
                Layout.preferredHeight: 36
                radius: 6
                color: "#334155"
                
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    
                    Text {
                        text: "✓"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                    }
                    
                    Text {
                        text: "验证配置"
                        font.pixelSize: 12
                        color: "#F1F5F9"
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: validateConfig()
                }
            }
            
            Item { Layout.fillWidth: true }
            
            // 取消按钮
            Rectangle {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 36
                radius: 6
                color: "#334155"
                
                Text {
                    anchors.centerIn: parent
                    text: "取消"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.cancelled()
                }
            }
            
            // 保存按钮
            Rectangle {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 36
                radius: 6
                color: "#3B82F6"
                
                Text {
                    anchors.centerIn: parent
                    text: "保存"
                    font.pixelSize: 12
                    color: "white"
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: saveConfig()
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    
    // 获取今天日期
    function getTodayDate() {
        var today = new Date()
        var year = today.getFullYear()
        var month = (today.getMonth() + 1).toString().padStart(2, '0')
        var day = today.getDate().toString().padStart(2, '0')
        return year + "-" + month + "-" + day
    }
    
    // 保存配置
    function saveConfig() {
        var config = getConfig()
        
        // 验证配置
        if (!validateConfig()) {
            return
        }
        
        console.log("保存回测配置:", config)
        root.config = config
        root.configSaved(config)
    }
    
    // 获取配置
    function getConfig() {
        var config = {}
        
        // 基本配置
        config.startDate = startDateField.text
        config.endDate = endDateField.text
        
        // 分组配置
        var groupingMethod = ""
        switch(groupingMethodCombo.currentIndex) {
            case 0: groupingMethod = "quantile"; break
            case 1: groupingMethod = "equal_value"; break
            case 2: groupingMethod = "custom"; break
        }
        config.groupingMethod = groupingMethod
        config.numGroups = numGroupsSpin.value
        
        // 策略配置
        var strategy = ""
        switch(strategyCombo.currentIndex) {
            case 0: strategy = "equal_weight"; break
            case 1: strategy = "factor_weight"; break
            case 2: strategy = "risk_parity"; break
            case 3: strategy = "custom"; break
        }
        config.strategy = strategy
        
        // 资金配置
        config.initialCapital = parseFloat(initialCapitalField.text) || 1000000
        config.transactionCost = parseFloat(transactionCostField.text) / 100 || 0.001
        config.slippage = parseFloat(slippageField.text) / 100 || 0.001
        
        // 性能配置
        config.maxThreads = maxThreadsSpin.value
        config.enableCache = enableCacheCheck.checked
        config.cacheTTL = parseInt(cacheTTLField.text) || 3600
        
        return config
    }
    
    // 验证配置
    function validateConfig() {
        if (!factorBacktestController) {
            console.error("回测控制器未设置")
            return false
        }
        
        var config = getConfig()
        var validation = factorBacktestController.validateConfig(config)
        
        if (validation.isValid) {
            console.log("配置验证通过")
            return true
        } else {
            console.error("配置验证失败:", validation.errors)
            // TODO: 显示错误信息
            return false
        }
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("回测配置对话框初始化完成")
        console.log("因子ID:", factorId)
        console.log("因子名称:", factorName)
    }
}
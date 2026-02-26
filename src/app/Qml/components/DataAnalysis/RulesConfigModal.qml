import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Popup {
    id: rulesConfigModal
    width: Math.min(parent ? parent.width * 0.9 : 900, 1000)
    height: Math.min(parent ? parent.height * 0.9 : 700, 800)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 1
    
    property var rules: ({})
    property var selectedRules: []
    
    signal rulesSaved(var rulesData)
    
    background: Rectangle {
        radius: 16
        color: "#ffffff"
        border.width: 1
        border.color: "#e5e7eb"
    }
    
    contentItem: ColumnLayout {
        spacing: 0
        
        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: "#1a2980"
            radius: 16
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                
                Rectangle {
                    width: 32
                    height: 32
                    radius: 8
                    color: "#26d0ce"
                    
                    Text {
                        text: "⚙️"
                        font.pixelSize: 16
                        anchors.centerIn: parent
                    }
                }
                
                Label {
                    text: "股票数据处理规则配置"
                    font.pixelSize: 18
                    font.bold: true
                    color: "white"
                    Layout.leftMargin: 10
                }
                
                Item { Layout.fillWidth: true }
                
                // 已选规则计数
                Rectangle {
                    width: 120
                    height: 28
                    radius: 14
                    color: "#ffffff20"
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6
                        
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: "#00b09b"
                        }
                        
                        Label {
                            text: selectedRules.length + "项规则已启用"
                            font.pixelSize: 11
                            color: "white"
                            Layout.fillWidth: true
                        }
                    }
                }
                
                Rectangle {
                    width: 28
                    height: 28
                    radius: 14
                    color: "transparent"
                    
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: rulesConfigModal.close()
                        
                        Text {
                            text: "×"
                            color: "white"
                            font.pixelSize: 18
                            font.bold: true
                            anchors.centerIn: parent
                        }
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 14
                            color: parent.containsMouse ? "#ffffff20" : "transparent"
                        }
                    }
                }
            }
        }
        
        // 主内容区
        ColumnLayout {
            spacing: 16
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            
            // 配置类别标题
            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true
                
                RowLayout {
                    Layout.fillWidth: true
                    
                    Label {
                        text: "数据处理规则配置"
                        font.pixelSize: 16
                        font.bold: true
                        color: "#1f2937"
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Label {
                        text: "点击卡片启用/禁用规则"
                        font.pixelSize: 11
                        color: "#6b7280"
                    }
                }
                
                Label {
                    text: "配置数据筛选、清洗和处理的规则条件"
                    font.pixelSize: 12
                    color: "#6b7280"
                }
            }
            
            // 规则卡片区域 - 使用GridLayout替代Flow
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"
                
                ScrollView {
                    anchors.fill: parent
                    clip: true
                    
                    // 使用宽度适配的容器
                    // Item {
                    //     width: Math.max(rulesGrid.width, rulesConfigModal.width - 40)
                        
                        GridLayout {
                            id: rulesGrid
                            width: rulesConfigModal.width - 40
                            columns: Math.max(1, Math.floor((rulesConfigModal.width - 40) / 142)) // 计算可容纳的列数
                            columnSpacing: 12
                            rowSpacing: 12
                            
                            // 市场选择规则
                            RuleCard {
                                ruleId: "market_filter"
                                ruleName: "市场选择"
                                icon: "🏢"
                                cardColor: "#3b82f6"
                                defaultValue: true
                            }
                            
                            // 价格筛选规则
                            RuleCard {
                                ruleId: "price_filter"
                                ruleName: "价格筛选"
                                icon: "💰"
                                cardColor: "#10b981"
                                defaultValue: false
                            }
                            
                            // 成交量筛选规则
                            RuleCard {
                                ruleId: "volume_filter"
                                ruleName: "成交量筛选"
                                icon: "📊"
                                cardColor: "#8b5cf6"
                                defaultValue: false
                            }
                            
                            // 财务指标规则
                            RuleCard {
                                ruleId: "financial_filter"
                                ruleName: "财务指标"
                                icon: "📈"
                                cardColor: "#f59e0b"
                                defaultValue: false
                            }
                            
                            // 数据清洗规则
                            RuleCard {
                                ruleId: "data_cleaning"
                                ruleName: "数据清洗"
                                icon: "🧹"
                                cardColor: "#ef4444"
                                defaultValue: true
                            }
                            
                            // 时间区间规则
                            RuleCard {
                                ruleId: "time_range"
                                ruleName: "时间区间"
                                icon: "⏰"
                                cardColor: "#06b6d4"
                                defaultValue: true
                            }
                            
                            // 股票状态规则
                            RuleCard {
                                ruleId: "stock_status"
                                ruleName: "股票状态"
                                icon: "🏷️"
                                cardColor: "#84cc16"
                                defaultValue: false
                            }
                            
                            // 数据标准化规则
                            RuleCard {
                                ruleId: "data_normalization"
                                ruleName: "数据标准化"
                                icon: "📐"
                                cardColor: "#a855f7"
                                defaultValue: false
                            }
                            
                            // 缺失值处理规则
                            RuleCard {
                                ruleId: "missing_value"
                                ruleName: "缺失值处理"
                                icon: "🔍"
                                cardColor: "#ec4899"
                                defaultValue: true
                            }
                            
                            // 异常值处理规则
                            RuleCard {
                                ruleId: "outliers_filter"
                                ruleName: "异常值处理"
                                icon: "⚠️"
                                cardColor: "#f97316"
                                defaultValue: true
                            }
                            
                            // 数据抽样规则
                            RuleCard {
                                ruleId: "data_sampling"
                                ruleName: "数据抽样"
                                icon: "📝"
                                cardColor: "#6366f1"
                                defaultValue: false
                            }
                            
                            // 自定义筛选规则
                            RuleCard {
                                ruleId: "custom_filter"
                                ruleName: "自定义筛选"
                                icon: "🎯"
                                cardColor: "#14b8a6"
                                defaultValue: false
                            }
                        }
                    //}
                }
            }
            
            // 底部操作区域
            ColumnLayout {
                spacing: 12
                Layout.fillWidth: true
                
                // 已选规则标签
                ColumnLayout {
                    spacing: 6
                    visible: selectedRules.length > 0
                    
                    Label {
                        text: "已启用规则"
                        font.pixelSize: 12
                        font.bold: true
                        color: "#374151"
                    }
                    
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        
                        Repeater {
                            model: selectedRules
                            
                            Rectangle {
                                height: 26
                                radius: 13
                                color: {
                                    var rule = getRuleById(modelData)
                                    return Qt.lighter(rule.cardColor, 1.3)
                                }
                                implicitWidth: tagRow.implicitWidth + 12
                                
                                RowLayout {
                                    id: tagRow
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 4
                                    
                                    Text {
                                        text: getRuleById(modelData).icon
                                        font.pixelSize: 10
                                    }
                                    
                                    Text {
                                        text: getRuleById(modelData).ruleName
                                        font.pixelSize: 10
                                        color: "#1f2937"
                                    }
                                    
                                    MouseArea {
                                        width: 14
                                        height: 14
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: toggleRule(modelData)
                                        
                                        Text {
                                            text: "×"
                                            color: "#6b7280"
                                            font.pixelSize: 8
                                            font.bold: true
                                            anchors.centerIn: parent
                                        }
                                        
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 7
                                            color: parent.containsMouse ? "#00000010" : "transparent"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 状态栏
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    radius: 8
                    color: "#f8fafc"
                    border.width: 1
                    border.color: "#e2e8f0"
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 8
                        
                        Rectangle {
                            width: 20
                            height: 20
                            radius: 10
                            color: "#3b82f6"
                            
                            Text {
                                text: "ℹ️"
                                font.pixelSize: 10
                                anchors.centerIn: parent
                            }
                        }
                        
                        Label {
                            id: statusText
                            text: "配置数据处理规则"
                            font.pixelSize: 12
                            color: "#334155"
                            Layout.fillWidth: true
                        }
                        
                        Label {
                            text: selectedRules.length + "项规则已启用"
                            font.pixelSize: 11
                            color: selectedRules.length > 0 ? "#10b981" : "#94a3b8"
                            font.bold: selectedRules.length > 0
                        }
                    }
                }
                
                // 按钮区域
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    
                    Button {
                        id: previewBtn
                        text: "预览规则效果"
                        Layout.fillWidth: true
                        height: 40
                        
                        background: Rectangle {
                            radius: 8
                            color: "#475569"
                            border.width: 1
                            border.color: parent.hovered ? "#334155" : "#475569"
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 13
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        
                        onClicked: previewRules()
                    }
                    
                    Button {
                        id: saveBtn
                        text: "保存规则配置"
                        Layout.fillWidth: true
                        height: 40
                        
                        background: Rectangle {
                            radius: 8
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#00b09b" }
                                GradientStop { position: 1.0; color: "#96c93d" }
                            }
                            border.width: 1
                            border.color: parent.hovered ? "#009688" : "transparent"
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 13
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        
                        onClicked: saveRules()
                    }
                }
            }
        }
    }
    
    // 规则卡片组件 - 使用Layout的preferredWidth/Height
    component RuleCard: Rectangle {
        property string ruleId: ""
        property string ruleName: ""
        property string icon: ""
        property color cardColor: "#3b82f6"
        property bool defaultValue: false
        property bool cardEnabled: false
        
        Layout.preferredWidth: 130
        Layout.preferredHeight: 45
        radius: 8
        color: cardEnabled ? Qt.lighter(cardColor, 1.4) : "#f9fafb"
        border.width: cardEnabled ? 2 : 1
        border.color: cardEnabled ? cardColor : "#e5e7eb"
        
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                cardEnabled = !cardEnabled
                if (cardEnabled) {
                    if (!selectedRules.includes(ruleId)) {
                        selectedRules.push(ruleId)
                        selectedRulesChanged()
                    }
                } else {
                    selectedRules = selectedRules.filter(function(id) {
                        return id !== ruleId
                    })
                    selectedRulesChanged()
                }
            }
            
            onEntered: {
                if (!cardEnabled) {
                    parent.color = Qt.lighter("#f9fafb", 0.95)
                }
            }
            
            onExited: {
                if (!cardEnabled) {
                    parent.color = "#f9fafb"
                }
            }
        }
        
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 6
            
            Text {
                text: parent.parent.icon
                font.pixelSize: 14
            }
            
            Text {
                text: parent.parent.ruleName
                font.pixelSize: 12
                font.bold: true
                color: parent.parent.cardEnabled ? parent.parent.cardColor : "#1f2937"
                Layout.fillWidth: true
            }
            
            // 启用指示器
            Rectangle {
                width: 14
                height: 14
                radius: 7
                color: parent.parent.cardEnabled ? parent.parent.cardColor : "transparent"
                border.width: 1
                border.color: parent.parent.cardEnabled ? parent.parent.cardColor : "#9ca3af"
                
                Text {
                    text: "✓"
                    color: "white"
                    font.pixelSize: 8
                    font.bold: true
                    anchors.centerIn: parent
                    visible: parent.parent.cardEnabled
                }
            }
        }
        
        Component.onCompleted: {
            cardEnabled = defaultValue
            if (defaultValue && !selectedRules.includes(ruleId)) {
                selectedRules.push(ruleId)
                selectedRulesChanged()
            }
        }
    }
    
    // 辅助函数
    function getRuleById(id) {
        var rules = [
            { ruleId: "market_filter", ruleName: "市场选择", icon: "🏢", cardColor: "#3b82f6" },
            { ruleId: "price_filter", ruleName: "价格筛选", icon: "💰", cardColor: "#10b981" },
            { ruleId: "volume_filter", ruleName: "成交量筛选", icon: "📊", cardColor: "#8b5cf6" },
            { ruleId: "financial_filter", ruleName: "财务指标", icon: "📈", cardColor: "#f59e0b" },
            { ruleId: "data_cleaning", ruleName: "数据清洗", icon: "🧹", cardColor: "#ef4444" },
            { ruleId: "time_range", ruleName: "时间区间", icon: "⏰", cardColor: "#06b6d4" },
            { ruleId: "stock_status", ruleName: "股票状态", icon: "🏷️", cardColor: "#84cc16" },
            { ruleId: "data_normalization", ruleName: "数据标准化", icon: "📐", cardColor: "#a855f7" },
            { ruleId: "missing_value", ruleName: "缺失值处理", icon: "🔍", cardColor: "#ec4899" },
            { ruleId: "outliers_filter", ruleName: "异常值处理", icon: "⚠️", cardColor: "#f97316" },
            { ruleId: "data_sampling", ruleName: "数据抽样", icon: "📝", cardColor: "#6366f1" },
            { ruleId: "custom_filter", ruleName: "自定义筛选", icon: "🎯", cardColor: "#14b8a6" }
        ]
        
        for (var i = 0; i < rules.length; i++) {
            if (rules[i].ruleId === id) {
                return rules[i]
            }
        }
        return { ruleName: "未知规则", icon: "❓", cardColor: "#6b7280" }
    }
    
    function toggleRule(id) {
        selectedRules = selectedRules.filter(function(ruleId) {
            return ruleId !== id
        })
        selectedRulesChanged()
    }
    
    function previewRules() {
        var rulesData = collectAllRules()
        statusText.text = "✓ 规则预览生成完成 - " + selectedRules.length + "项规则已配置"
    }
    
    function saveRules() {
        var rulesData = collectAllRules()
        rules = rulesData
        rulesSaved(rulesData)
        statusText.text = "✓ 规则配置已保存 - " + selectedRules.length + "项规则已生效"
        closeTimer.start()
    }
    
    function collectAllRules() {
        var rulesData = {}
        
        // 根据选中的规则构建规则数据
        for (var i = 0; i < selectedRules.length; i++) {
            var ruleId = selectedRules[i]
            var rule = getRuleById(ruleId)
            
            switch(ruleId) {
                case "market_filter":
                    rulesData.market = {
                        aShares: true,
                        hk: false,
                        us: false
                    }
                    break
                case "price_filter":
                    rulesData.priceFilter = {
                        min: 0,
                        max: 10000
                    }
                    break
                case "volume_filter":
                    rulesData.volumeFilter = {
                        minVolume: 10000,
                        minTurnover: 1.0
                    }
                    break
                case "data_cleaning":
                    rulesData.dataCleaning = {
                        missingValue: "向前填充",
                        outliers: "封顶处理",
                        excludeST: true,
                        excludeSuspended: true
                    }
                    break
                case "time_range":
                    rulesData.timeRange = {
                        start: "2024-01-01",
                        end: "2024-12-31"
                    }
                    break
                default:
                    rulesData[ruleId] = {
                        enabled: true,
                        ruleName: rule.ruleName
                    }
            }
        }
        
        return rulesData
    }
    
    Timer {
        id: closeTimer
        interval: 1500
        onTriggered: rulesConfigModal.close()
    }
    
    // 延迟初始化以避免组件未完全加载时崩溃
    Component.onCompleted: {
        console.log("RulesConfigModal组件初始化完成")
        // 延迟执行初始化，确保所有组件都已加载
        rulesTimer.start()
    }
    
    Timer {
        id: rulesTimer
        interval: 100
        onTriggered: {
            console.log("开始延迟初始化规则配置")
            try {
                statusText.text = "配置数据处理规则 - 点击卡片启用规则配置"
                console.log("规则配置初始化完成")
            } catch (error) {
                console.error("规则配置初始化时发生错误:", error)
            }
        }
    }
}
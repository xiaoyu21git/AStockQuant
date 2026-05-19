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
    property var availableRules: [
        { ruleId: "completeness", ruleName: "完整性校验", icon: "✅", cardColor: "#10b981", defaultValue: true, required: true },
        { ruleId: "duplicateRemoval", ruleName: "重复数据删除", icon: "🗑️", cardColor: "#f97316", defaultValue: true, required: true },
        { ruleId: "financialDateValidity", ruleName: "财务日期有效性", icon: "🗓️", cardColor: "#06b6d4", defaultValue: true, required: true },
        { ruleId: "financialMetricSanitize", ruleName: "财务指标净化", icon: "📈", cardColor: "#14b8a6", defaultValue: true },
        { ruleId: "reportDateAlignment", ruleName: "财报日期对齐", icon: "📅", cardColor: "#22c55e", defaultValue: true, required: true },
        { ruleId: "survivorBias", ruleName: "生存者偏差处理", icon: "🧬", cardColor: "#14b8a6", defaultValue: true },
        { ruleId: "adjustedPrice", ruleName: "价格复权", icon: "🔁", cardColor: "#8b5cf6", defaultValue: true },
        { ruleId: "newStockFilter", ruleName: "新股过滤", icon: "🆕", cardColor: "#0ea5e9", defaultValue: false },
        { ruleId: "stFilter", ruleName: "ST过滤", icon: "⚠️", cardColor: "#ef4444", defaultValue: false },
        { ruleId: "priceValidity", ruleName: "价格有效性", icon: "📊", cardColor: "#8b5cf6", defaultValue: true, required: true },
        { ruleId: "suspensionFill", ruleName: "停牌填充", icon: "⏸️", cardColor: "#6366f1", defaultValue: true },
        { ruleId: "missingValueFill", ruleName: "缺失值处理", icon: "🔍", cardColor: "#ec4899", defaultValue: true },
        { ruleId: "limitMoveTag", ruleName: "涨跌停标记", icon: "🏷️", cardColor: "#f59e0b", defaultValue: true },
        { ruleId: "valuationSanitize", ruleName: "估值净化", icon: "🧮", cardColor: "#06b6d4", defaultValue: true }
    ]
    
    signal rulesSaved(var rulesData)
    
    background: Rectangle {
        radius: 16
        color: "#ffffff"
        border.width: 1
        border.color: "#e5e7eb"
    }
    
    contentItem: ColumnLayout {
        spacing: 0
        
        // 标题栏 - 与DataSourceModal对齐
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "#1a2980"
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                
                Rectangle {
                    width: 28
                    height: 28
                    radius: 6
                    color: "#26d0ce"
                    
                    Text {
                        text: "⚙️"
                        font.pixelSize: 16
                        anchors.centerIn: parent
                    }
                }
                
                Label {
                    text: "股票数据处理规则配置"
                    font.pixelSize: 16
                    font.bold: true
                    color: "white"
                    Layout.leftMargin: 8
                }
                
                Item { Layout.fillWidth: true }
                
                // 已选规则计数 - 保持原有功能但调整样式
                Rectangle {
                    width: 110
                    height: 24
                    radius: 12
                    color: "#ffffff20"
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 4
                        
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: "#00b09b"
                        }
                        
                        Label {
                            text: selectedRules.length + "项规则已启用"
                            font.pixelSize: 10
                            color: "white"
                            Layout.fillWidth: true
                        }
                    }
                }
                
                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: "transparent"
                    
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: rulesConfigModal.close()
                        
                        Text {
                            text: "×"
                            color: "white"
                            font.pixelSize: 16
                            font.bold: true
                            anchors.centerIn: parent
                        }
                        
                        Rectangle {
                            anchors.fill: parent
                            radius: 12
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

                Label {
                    text: "财务规则会执行 PIT 对齐、缺失值处理、缩尾、标准化和中性化"
                    font.pixelSize: 11
                    color: "#9ca3af"
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

                            Repeater {
                                model: availableRules

                                RuleCard {
                                    ruleId: modelData.ruleId
                                    ruleName: modelData.ruleName
                                    icon: modelData.icon
                                    cardColor: modelData.cardColor
                                    defaultValue: modelData.defaultValue
                                    required: modelData.required === true
                                }
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
                
                // 状态栏 - 与DataSourceModal对齐
                Rectangle {
                    id: statusBar
                    Layout.fillWidth: true
                    height: 32
                    radius: 6
                    color: {
                        if (statusText.text.includes("成功")) return "#dcfce7"
                        else if (statusText.text.includes("失败")) return "#fee2e2"
                        else if (statusText.text.includes("中")) return "#fef9c3"
                        else return "#f3f4f6"
                    }
                    border.width: 1
                    border.color: {
                        if (statusText.text.includes("成功")) return "#86efac"
                        else if (statusText.text.includes("失败")) return "#fca5a5"
                        else if (statusText.text.includes("中")) return "#fde047"
                        else return "#e5e7eb"
                    }
                    visible: statusText.text
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6
                        
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: {
                                if (statusText.text.includes("成功")) return "#16a34a"
                                else if (statusText.text.includes("失败")) return "#dc2626"
                                else if (statusText.text.includes("中")) return "#ca8a04"
                                else return "#6b7280"
                            }
                        }
                        
                        Label {
                            id: statusText
                            text: "配置数据处理规则"
                            font.pixelSize: 12
                            color: {
                                if (statusText.text.includes("成功")) return "#166534"
                                else if (statusText.text.includes("失败")) return "#991b1b"
                                else if (statusText.text.includes("中")) return "#854d0e"
                                else return "#374151"
                            }
                            Layout.fillWidth: true
                        }
                        
                        Label {
                            text: selectedRules.length + "项规则已启用"
                            font.pixelSize: 11
                            color: selectedRules.length > 0 ? "#10b981" : "#9ca3af"
                        }
                        
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 9
                            color: "transparent"
                            visible: !statusText.text.includes("中")
                            
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: statusText.text = ""
                                
                                Text {
                                    text: "×"
                                    color: "#6b7280"
                                    font.pixelSize: 12
                                    anchors.centerIn: parent
                                }
                                
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 9
                                    color: parent.containsMouse ? "#00000010" : "transparent"
                                }
                            }
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
        property bool required: false
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
            enabled: !parent.required
            cursorShape: parent.required ? Qt.ArrowCursor : Qt.PointingHandCursor
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
        for (var i = 0; i < availableRules.length; i++) {
            if (availableRules[i].ruleId === id) {
                return availableRules[i]
            }
        }
        return { ruleName: "未知规则", icon: "❓", cardColor: "#6b7280" }
    }

    function buildCleaningRuleConfig(ruleId) {
        switch (ruleId) {
            case "completeness":
                return { enabled: true }
            case "duplicateRemoval":
                return {
                    enabled: true,
                    keyFields: ["symbol", "trade_date"]
                }
            case "financialDateValidity":
                return { enabled: true }
            case "financialMetricSanitize":
                return { enabled: true }
            case "reportDateAlignment":
                return { enabled: true }
            case "survivorBias":
                return { enabled: true }
            case "adjustedPrice":
                return {
                    enabled: true,
                    preferAdjustedFields: true,
                    applyFactorFallback: true
                }
            case "newStockFilter":
                return {
                    enabled: true,
                    minTradeDays: 60
                }
            case "stFilter":
                return { enabled: true }
            case "priceValidity":
                return {
                    enabled: true,
                    minPrice: 0.01,
                    maxPrice: 10000.0,
                    enforceChain: true,
                    allowZeroWhenSuspended: true
                }
            case "suspensionFill":
                return {
                    enabled: true,
                    fillFields: ["open", "high", "low", "close"],
                    maxForwardFillDays: 10,
                    dropAfterMaxDays: true
                }
            case "missingValueFill":
                return {
                    enabled: true,
                    fields: ["open", "high", "low", "close", "turnover_rate", "market_cap", "circulating_market_cap"],
                    maxLookbackDays: 5
                }
            case "limitMoveTag":
                return {
                    enabled: true,
                    upThreshold: 9.5,
                    downThreshold: -9.5
                }
            case "valuationSanitize":
                return { enabled: true }
            default:
                return null
        }
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
            var ruleConfig = buildCleaningRuleConfig(ruleId)
            if (ruleConfig) {
                rulesData[ruleId] = ruleConfig
            } else {
                console.log("未知规则ID:", ruleId)
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
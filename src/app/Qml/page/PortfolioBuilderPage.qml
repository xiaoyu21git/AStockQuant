// PortfolioBuilderPage.qml
// 增强版组合构建页面，实现拖拽式量化因子组合构建
// 遵循即时反馈、智能辅助、极简路径三大原则
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs 
import AStock.Bridge 1.0 as Bridge
import "../components/Factor" as FactorComponents
import "../components/Navigation" as Navigation
import "../components/TopNavigation" as TopNavigation
import "../components/Base" as BaseComponents
import "../components/Risk" as RiskComponents

/**
 * 增强版组合构建页面 - 拖拽式量化因子组合构建器
 * 三栏式设计：左侧因子池、中间组合构建器、右侧风险监控
 * 底部通知栏，支持实时预览和智能辅助
 */
Item {
    id: root

    signal requestBacktest(string strategyId, string strategyName, var backtestConfig)
    
    // ============ 页面属性 ============
    
    property string currentPortfolioId: "momentum_portfolio"
    property string portfolioName: "动量组合"
    property real totalWeight: 100.0
    property var currentPortfolio: []
    readonly property var factorService: Bridge.FactorService
    readonly property var strategyService: Bridge.StrategyService
    property var factorViewModel: factorService ? factorService.getViewModel() : null
    property int initialPortfolioSize: 4
    
    // 基于真实因子元数据估算的组合指标
    property real simulatedAnnualReturn: 0.0
    property real simulatedSharpeRatio: 0.0
    property real simulatedMaxDrawdown: 0.0
    
    // 风险暴露
    property var sectorExposure: {
        "银行": 0.4,
        "消费": 0.3,
        "医药": 0.2,
        "科技": 0.4
    }
    
    property var styleExposure: {
        "市值": 1.2,
        "动量": 0.8,
        "价值": 0.5,
        "波动率": 1.1
    }
    
    // 快捷面板配置
    property var quickPanelConfig: {
        "常用因子": { expanded: true, items: 6 },
        "行业配置": { expanded: true, items: 4 },
        "风格暴露": { expanded: true, items: 4 }
    }
    
    // 系统状态
    property var systemStatus: ({
        "因子池": { status: "📚", value: factorPoolModel.count + " 个", color: "#3b82f6" },
        "当前组合": { status: "🧩", value: portfolioModel.count + " 个", color: "#10b981" },
        "数据源": {
            status: factorService ? "🟢" : "🔴",
            value: factorService ? "FactorService" : "未连接",
            color: factorService ? "#10b981" : "#ef4444"
        }
    })
    
    // 通知消息
    property var notifications: [
        { type: "info", text: "等待加载真实因子数据", time: "当前", action: "刷新" }
    ]
    
    // ============ 数据模型 ============
    
    // 可用因子池
    ListModel {
        id: factorPoolModel
    }
    
    // 当前组合
    ListModel {
        id: portfolioModel
    }
    
    // 常用因子
    ListModel {
        id: commonFactorsModel
    }

    Connections {
        target: factorService

        function onFactorsLoaded(factors) {
            root.syncFactorModels(factors || [])
        }

        function onFactorAdded() {
            root.refreshFactorSources()
        }

        function onFactorUpdated() {
            root.refreshFactorSources()
        }

        function onFactorDeleted() {
            root.refreshFactorSources()
        }
    }
    
    // 行业配置
    ListModel {
        id: sectorModel
        ListElement { sector: "银行"; weight: 0.4; color: "#3B82F6" }
        ListElement { sector: "消费"; weight: 0.3; color: "#10B981" }
        ListElement { sector: "医药"; weight: 0.2; color: "#8B5CF6" }
        ListElement { sector: "科技"; weight: 0.4; color: "#F59E0B" }
    }
    
    // 风格暴露
    ListModel {
        id: styleModel
        ListElement { style: "市值"; value: 1.2; target: 1.0; color: "#3B82F6" }
        ListElement { style: "动量"; value: 0.8; target: 0.8; color: "#10B981" }
        ListElement { style: "价值"; value: 0.5; target: 0.6; color: "#8B5CF6" }
        ListElement { style: "波动率"; value: 1.1; target: 1.0; color: "#F59E0B" }
    }
    
    // ============ 三栏式主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 主内容区域（三栏）
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            
            // === 左侧因子池面板 ===
            Rectangle {
                id: leftPanel
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                color: "#0F172A"
                border.width: 1
                border.color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    
                    // 面板标题
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        color: "#1E293B"
                        
                        Text {
                            anchors.centerIn: parent
                            text: "📦 可用因子池"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        // 搜索按钮
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: "🔍"
                            font.pixelSize: 14
                            color: "#94A3B8"
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: searchFactors()
                            }
                        }
                    }
                    
                    // 面板内容
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                           // padding: 16
                            
                            // 常用因子
                            QuickPanelSection {
                                title: "🔥 常用因子"
                                expanded: quickPanelConfig["常用因子"].expanded
                                itemCount: quickPanelConfig["常用因子"].items
                                model: commonFactorsModel
                                delegate: QuickPanelItem {
                                    text: model.displayName
                                    subText: model.frequency + " 次使用"
                                    icon: "🔥"
                                    draggable: true
                                    dragData: {
                                        "factorId": model.factorId,
                                        "displayName": model.displayName,
                                        "type": "factor"
                                    }
                                    onClicked: addFactorToPortfolio(model.factorId)
                                }
                                onToggleExpanded: quickPanelConfig["常用因子"].expanded = expanded
                            }
                            
                            // 行业配置
                            QuickPanelSection {
                                title: "🏢 行业配置"
                                expanded: quickPanelConfig["行业配置"].expanded
                                itemCount: quickPanelConfig["行业配置"].items
                                model: sectorModel
                                delegate: QuickPanelItem {
                                    text: model.sector
                                    subText: (model.weight * 100).toFixed(0) + "%"
                                    icon: "🏢"
                                    onClicked: adjustSectorWeight(model.sector)
                                }
                                onToggleExpanded: quickPanelConfig["行业配置"].expanded = expanded
                            }
                            
                            // 风格暴露
                            QuickPanelSection {
                                title: "🎨 风格暴露"
                                expanded: quickPanelConfig["风格暴露"].expanded
                                itemCount: quickPanelConfig["风格暴露"].items
                                model: styleModel
                                delegate: QuickPanelItem {
                                    text: model.style
                                    subText: model.value.toFixed(1) + " / " + model.target.toFixed(1)
                                    icon: "🎨"
                                    onClicked: adjustStyleExposure(model.style)
                                }
                                onToggleExpanded: quickPanelConfig["风格暴露"].expanded = expanded
                            }
                            
                            // 拖拽提示
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 60
                                radius: 8
                                color: "#1E293B"
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "⬆️ 拖拽添加"
                                        font.pixelSize: 12
                                        color: "#94A3B8"
                                    }
                                    
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "拖拽因子到中间区域"
                                        font.pixelSize: 10
                                        color: "#64748B"
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // === 中间组合构建器 ===
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                
                // 工作区标题栏
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    color: "#1E293B"
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16
                        
                        Text {
                            text: "组合构建: " + portfolioName
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 操作按钮
                        Row {
                            spacing: 8
                            
                            // 回测按钮
                            Rectangle {
                                width: 100
                                height: 36
                                radius: 8
                                color: "#3B82F6"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    
                                    Text {
                                        text: "🧪"
                                        font.pixelSize: 14
                                        color: "white"
                                    }
                                    
                                    Text {
                                        text: "回测"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: runBacktest()
                                }
                            }
                            
                            // 保存按钮
                            Rectangle {
                                width: 80
                                height: 36
                                radius: 8
                                color: "#10B981"
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: "保存"
                                    font.pixelSize: 14
                                    color: "white"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: savePortfolio()
                                }
                            }
                        }
                    }
                }
                
                // 组合构建区域
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0F172A"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 24
                        
                        // 当前组合标题
                        RowLayout {
                            spacing: 12
                            
                            Text {
                                text: "📊 当前组合 (总权重: " + totalWeight.toFixed(1) + "%)"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                color: "#F1F5F9"
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            // 权重重置按钮
                            Text {
                                text: "重置权重"
                                font.pixelSize: 12
                                color: "#3B82F6"
                             
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: resetWeights()
                                }
                            }
                        }
                        
                        // 组合表格
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 200
                            radius: 12
                            color: "#1E293B"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 0
                                
                                // 表头
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    spacing: 16
                                    
                                    Text {
                                        text: "因子"
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: "#94A3B8"
                                        Layout.preferredWidth: 120
                                    }
                                    
                                    Text {
                                        text: "权重"
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: "#94A3B8"
                                        Layout.preferredWidth: 80
                                    }
                                    
                                    Text {
                                        text: "相关性"
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: "#94A3B8"
                                        Layout.preferredWidth: 100
                                    }
                                    
                                    Text {
                                        text: "操作"
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: "#94A3B8"
                                        Layout.fillWidth: true
                                    }
                                }
                                
                                // 表格内容
                                ListView {
                                    id: portfolioListView
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    model: portfolioModel
                                    clip: true
                                    spacing: 4
                                    
                                    delegate: PortfolioItem {
                                        width: portfolioListView.width
                                        height: 48
                                        
                                        factorId: model.factorId
                                        displayName: model.displayName
                                        weight: model.weight
                                        correlation: model.correlation
                                        color: model.color
                                        
                                        onWeightChanged: function(newWeight) {
                                            updateFactorWeight(model.factorId, newWeight)
                                        }
                                       // onRemoveRequested: removeFactorFromPortfolio(model.factorId)
                                    }
                                    
                                    ScrollBar.vertical: ScrollBar {
                                        policy: ScrollBar.AlwaysOn
                                        width: 8
                                    }
                                }
                            }
                        }
                        
                        // 风险监控和模拟绩效
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            spacing: 16
                            
                            // 风险监控
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 12
                                color: "#1E293B"
                                
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    
                                    Text {
                                        text: "⚠️ 风险监控"
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: "#F1F5F9"
                                    }
                                    
                                    // 行业暴露
                                    Column {
                                        spacing: 4
                                        
                                        Text {
                                            text: "行业暴露:"
                                            font.pixelSize: 12
                                            color: "#94A3B8"
                                        }
                                        
                                        Row {
                                            spacing: 4
                                            
                                            Repeater {
                                                model: sectorModel
                                                
                                                delegate: Rectangle {
                                                    width: 20
                                                    height: 20
                                                    radius: 4
                                                    color: model.color
                                                    opacity: model.weight
                                                    
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: "⬤"
                                                        font.pixelSize: 8
                                                        color: "white"
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    
                                    // 风格暴露
                                    Column {
                                        spacing: 4
                                        
                                        Text {
                                            text: "风格暴露:"
                                            font.pixelSize: 12
                                            color: "#94A3B8"
                                        }
                                        
                                        Text {
                                            text: "市值" + styleExposure["市值"].toFixed(1) + " 动量" + styleExposure["动量"].toFixed(1) + " 价值" + styleExposure["价值"].toFixed(1) + " 波动率" + styleExposure["波动率"].toFixed(1)
                                            font.pixelSize: 11
                                            color: "#94A3B8"
                                        }
                                    }
                                }
                            }
                            
                            // 模拟绩效
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 12
                                color: "#1E293B"
                                
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    
                                    Text {
                                        text: "📈 模拟绩效"
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: "#F1F5F9"
                                    }
                                    
                                    GridLayout {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        columns: 2
                                        columnSpacing: 8
                                        rowSpacing: 8
                                        
                                        // 年化收益
                                        PerformanceMetric {
                                            label: "年化收益"
                                            value: simulatedAnnualReturn
                                            format: "%.1f"
                                            unit: "%"
                                            color: simulatedAnnualReturn > 15 ? "#10B981" : "#EF4444"
                                        }
                                        
                                        // 夏普比率
                                        PerformanceMetric {
                                            label: "夏普比率"
                                            value: simulatedSharpeRatio
                                            format: "%.2f"
                                            color: simulatedSharpeRatio > 1.5 ? "#10B981" : "#EF4444"
                                        }
                                        
                                        // 最大回撤
                                        PerformanceMetric {
                                            label: "最大回撤"
                                            value: simulatedMaxDrawdown
                                            format: "%.1f"
                                            unit: "%"
                                            color: simulatedMaxDrawdown < 10 ? "#10B981" : "#EF4444"
                                        }
                                    }
                                }
                            }
                        }
                        
                        // 拖拽区域提示
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 80
                            radius: 12
                            color: Qt.rgba(0.231, 0.510, 0.965, 0.1)  // #3B82F6 with alpha
                            border.color: "#3B82F6"
                            border.width: 2
                            //border.style: Border.DashLine
                            
                            Column {
                                anchors.centerIn: parent
                                spacing: 8
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "⬇️ 拖放区域"
                                    font.pixelSize: 14
                                    color: "#3B82F6"
                                }
                                
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "将左侧因子拖拽到此处添加到组合"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                            }
                            
                            // 拖拽区域
                            DropArea {
                                anchors.fill: parent
                                
                                onEntered: {
                                    parent.color = Qt.rgba(0.231, 0.510, 0.965, 0.2)
                                }
                                
                                onExited: {
                                    parent.color = Qt.rgba(0.231, 0.510, 0.965, 0.1)
                                }
                                
                                onDropped: {
                                    console.log("拖拽数据:", drop.text, drop.getDataAsString())
                                    // 处理拖拽添加
                                    if (drop.hasText && drop.text.includes("factorId")) {
                                        try {
                                            var data = JSON.parse(drop.text)
                                            if (data.type === "factor") {
                                                addFactorToPortfolio(data.factorId)
                                            }
                                        } catch(e) {
                                            console.log("拖拽数据解析错误:", e)
                                        }
                                    }
                                    parent.color = Qt.rgba(0.231, 0.510, 0.965, 0.1)
                                }
                            }
                        }
                    }
                }
            }
            
            // === 右侧系统状态面板 ===
            Rectangle {
                id: rightPanel
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                color: "#0F172A"
                border.width: 1
                border.color: "#1E293B"
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    
                    // 面板标题
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        color: "#1E293B"
                        
                        Text {
                            anchors.centerIn: parent
                            text: "⚙️ 系统状态"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                    }
                    
                    // 状态内容
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 16
                      //  padding: 16
                        
                        // 系统状态项
                        Repeater {
                            model: Object.keys(systemStatus)
                            
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 60
                                radius: 8
                                color: "#1E293B"
                                
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 4
                                    
                                    RowLayout {
                                        spacing: 8
                                        
                                        Text {
                                            text: systemStatus[modelData].status
                                            font.pixelSize: 16
                                            color: systemStatus[modelData].color
                                        }
                                        
                                        Text {
                                            text: modelData
                                            font.pixelSize: 14
                                            color: "#94A3B8"
                                        }
                                        
                                        Item { Layout.fillWidth: true }
                                        
                                        Text {
                                            text: systemStatus[modelData].value
                                            font.pixelSize: 18
                                            font.weight: Font.DemiBold
                                            color: "#F1F5F9"
                                        }
                                    }
                                    
                                    // 进度条（用于模拟耗时）
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 4
                                        radius: 2
                                        color: "#334155"
                                        visible: modelData === "模拟耗时"
                                        
                                        Rectangle {
                                            width: parent.width * 0.3  // 模拟30%进度
                                            height: parent.height
                                            radius: 2
                                            color: systemStatus[modelData].color
                                        }
                                    }
                                }
                            }
                        }
                        
                        Item { Layout.fillHeight: true }
                        
                        // 智能建议
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 100
                            radius: 8
                            color: "#1E293B"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                
                                Text {
                                    text: "💡 智能建议"
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: "建议增加价值因子权重至20%"
                                    font.pixelSize: 12
                                    color: "#10B981"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        
                        // 快捷操作
                        Column {
                            Layout.fillWidth: true
                            spacing: 8
                            
                            // 一键优化按钮
                            Rectangle {
                                width: parent.width
                                height: 40
                                radius: 8
                                color: "#3B82F6"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: "⚡"
                                        font.pixelSize: 14
                                        color: "white"
                                    }
                                    
                                    Text {
                                        text: "一键优化"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: autoOptimize()
                                }
                            }
                            
                            // 风险检查按钮
                            Rectangle {
                                width: parent.width
                                height: 40
                                radius: 8
                                color: "#334155"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: "🛡️"
                                        font.pixelSize: 14
                                        color: "#F1F5F9"
                                    }
                                    
                                    Text {
                                        text: "风险检查"
                                        font.pixelSize: 14
                                        color: "#F1F5F9"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: riskCheck()
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // === 底部通知栏 ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#1E293B"
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 16
                
                Text {
                    text: "📢 组合通知"
                    font.pixelSize: 14
                    color: "#94A3B8"
                }
                
                // 通知消息
                Row {
                    spacing: 16
                    
                    Repeater {
                        model: notifications
                        
                        delegate: Row {
                            spacing: 6
                            
                            Text {
                                text: modelData.type === "warning" ? "⚠️" :
                                      modelData.type === "success" ? "✅" : "ℹ️"
                                font.pixelSize: 14
                                color: modelData.type === "warning" ? "#F59E0B" :
                                       modelData.type === "success" ? "#10B981" : "#3B82F6"
                            }
                            
                            Text {
                                text: modelData.text
                                font.pixelSize: 14
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: modelData.time
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            // 操作按钮
                            Text {
                                text: "[" + modelData.action + "]"
                                font.pixelSize: 12
                                color: "#3B82F6"
                               
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: handleNotificationAction(index)
                                }
                            }
                        }
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // 通知控制
                Row {
                    spacing: 8
                    
                    Text {
                        text: "🔕"
                        font.pixelSize: 14
                        color: "#94A3B8"
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: muteNotifications()
                        }
                    }
                    
                    Text {
                        text: "✖️"
                        font.pixelSize: 14
                        color: "#94A3B8"
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: clearNotifications()
                        }
                    }
                }
            }
        }
    }
    
    // ============ 自定义组件 ============
    
    // 快捷面板部分（复用FactorLibraryPageEnhanced中的组件）
    component QuickPanelSection: Column {
        property string title: ""
        property bool expanded: true
        property int itemCount: 5
        property var model: null
        property Component delegate: null
        
        signal toggleExpanded(bool expanded)
        
        spacing: 8
        width: parent.width
        
        // 标题栏
        Rectangle {
            width: parent.width
            height: 32
            radius: 6
            color: "#1E293B"
            
            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: expanded ? "▼" : "▶"
                    font.pixelSize: 12
                    color: "#94A3B8"
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            expanded = !expanded
                            toggleExpanded(expanded)
                        }
                    }
                }
                
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: title
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                Item { width: parent.width - 100 }
                
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: model ? model.count : 0
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
            }
        }
        
        // 内容区域
        Column {
            width: parent.width
            spacing: 4
            visible: expanded
            
            Repeater {
                model: parent.model ? Math.min(parent.itemCount, parent.model.count) : 0
                
                Loader {
                    width: parent.width
                    height: 32
                    sourceComponent: parent.delegate
                }
            }
        }
    }
    
    // 快捷面板项（支持拖拽）
    component QuickPanelItem: Rectangle {
        property string text: ""
        property string subText: ""
        property string icon: ""
        property bool draggable: false
        property var dragData: null
        signal clicked()
        
        width: parent.width
        height: 32
        radius: 6
        color: "#1E293B"
        
        Row {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8
            
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: icon
                font.pixelSize: 14
                color: "#94A3B8"
            }
            
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                
                Text {
                    text: parent.text
                    font.pixelSize: 14
                    color: "#F1F5F9"
                    elide: Text.ElideRight
                    width: parent.width
                }
                
                Text {
                    text: parent.subText
                    font.pixelSize: 11
                    color: "#94A3B8"
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
            
            drag.target: draggable ? dragItem : null
            drag.smoothed: true
            drag.threshold: 10
            
            onPressed: {
                if (draggable && dragData) {
                    dragItem.text = text
                    dragItem.dragData = JSON.stringify(dragData)
                    dragItem.visible = true
                }
            }
            
            onReleased: {
                if (draggable) {
                    dragItem.visible = false
                }
            }
        }
        
        // 拖拽视觉反馈
        Rectangle {
            id: dragItem
            width: 120
            height: 40
            radius: 8
            color: "#3B82F6"
            visible: false
            z: 1000
            
            property string text: ""
            property string dragData: ""
            
            Text {
                anchors.centerIn: parent
                text: parent.text
                font.pixelSize: 12
                color: "white"
            }
            
            Drag.active: parent.draggable
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2
            
            Drag.dragType: Drag.Automatic
            Drag.mimeData: {
                "text/plain": parent.dragData,
                "text": parent.dragData
            }
            
            Drag.onDragStarted: {
                console.log("开始拖拽:", text)
            }
            
            Drag.onDragFinished: {
                console.log("拖拽完成")
                visible = false
            }
        }
    }
    
    // 组合项组件
    component PortfolioItem: Rectangle {
        property string factorId: ""
        property string displayName: ""
        property real weight: 0.0
        property real correlation: 0.0
        property color factorColor: "#3B82F6"

        signal onWeightChanged(string factorId, real newWeight)
        //signal onRemoveRequested(string factorId)

        radius: 8
        color: "#1E293B"
        
        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 16
            
            // 因子名称
            Row {
                spacing: 8
                Layout.preferredWidth: 120
                
                Rectangle {
                    width: 24
                    height: 24
                    radius: 4
                    color: factorColor
                    
                    Text {
                        anchors.centerIn: parent
                        text: "F"
                        font.pixelSize: 10
                        color: "white"
                    }
                }
                
                Text {
                    text: displayName
                    font.pixelSize: 14
                    color: "#F1F5F9"
                    elide: Text.ElideRight
                }
            }
            
            // 权重调节
            Row {
                spacing: 8
                Layout.preferredWidth: 80
                
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: weight.toFixed(1) + "%"
                    font.pixelSize: 14
                    color: "#F1F5F9"
                }
                
                // 权重滑块
                Slider {
                    width: 60
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0
                    to: 100
                    value: weight
                    stepSize: 0.5
                    
                    onValueChanged: {
                        if (Math.abs(weight - value) > 0.1) {
                            onWeightChanged(factorId, value)
                        }
                    }
                }
            }
            
            // 相关性指示器
            Rectangle {
                Layout.preferredWidth: 100
                height: 20
                radius: 10
                color: "#334155"
                
                Rectangle {
                    width: parent.width * Math.abs(correlation)
                    height: parent.height
                    radius: parent.radius
                    color: correlation > 0.5 ? "#EF4444" : 
                           correlation > 0.3 ? "#F59E0B" : "#10B981"
                    
                    Text {
                        anchors.centerIn: parent
                        text: correlation.toFixed(2)
                        font.pixelSize: 10
                        color: "white"
                    }
                }
            }
            
            Item { Layout.fillWidth: true }
            
            // 操作按钮
            Row {
                spacing: 8
                
                // 删除按钮
                Text {
                    text: "✖️"
                    font.pixelSize: 12
                    color: "#EF4444"
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                       // onClicked: onRemoveRequested(factorId)
                    }
                }
            }
        }
    }
    
    // 性能指标组件
    component PerformanceMetric: Rectangle {
        property string label: ""
        property real value: 0
        property string format: "%.2f"
        property string unit: ""
        property color metricColor: "#3B82F6"
        
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        radius: 8
        color: "#334155"
        
        Column {
            anchors.centerIn: parent
            spacing: 2
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: label
                font.pixelSize: 10
                color: "#94A3B8"
            }
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    var formatted = format.replace("%d", Math.round(value)).replace("%.1f", value.toFixed(1)).replace("%.2f", value.toFixed(2))
                    return formatted + (unit ? " " + unit : "")
                }
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: parent.parent.metricColor
            }
        }
    }
    
    // ============ 核心业务函数 ============
    
    // 添加因子到组合
    function addFactorToPortfolio(factorId) {
        console.log("添加因子到组合:", factorId)
        
        // 检查是否已存在
        for (var i = 0; i < portfolioModel.count; i++) {
            if (portfolioModel.get(i).factorId === factorId) {
                console.log("因子已在组合中")
                return
            }
        }
        
        // 从因子池获取数据
        var factorData = null
        for (var j = 0; j < factorPoolModel.count; j++) {
            if (factorPoolModel.get(j).factorId === factorId) {
                factorData = factorPoolModel.get(j)
                break
            }
        }
        
        if (factorData) {
            // 添加到组合
            portfolioModel.append({
                factorId: factorId,
                displayName: factorData.displayName,
                weight: 20.0,  // 默认权重
                correlation: factorData.correlation,
                icValue: factorData.icValue,
                irValue: factorData.irValue,
                turnoverRate: factorData.turnoverRate,
                color: getFactorColor(factorData.category)
            })
            
            // 重新计算权重
            rebalanceWeights()
            updateSimulation()
        }
    }
    
    // 移除因子
    function removeFactorFromPortfolio(factorId) {
        console.log("移除因子:", factorId)
        
        for (var i = 0; i < portfolioModel.count; i++) {
            if (portfolioModel.get(i).factorId === factorId) {
                portfolioModel.remove(i)
                break
            }
        }
        
        // 重新计算权重
        rebalanceWeights()
        updateSimulation()
    }
    
    // 更新因子权重
    function updateFactorWeight(factorId, newWeight) {
        console.log("更新因子权重:", factorId, newWeight)
        
        for (var i = 0; i < portfolioModel.count; i++) {
            if (portfolioModel.get(i).factorId === factorId) {
                portfolioModel.setProperty(i, "weight", newWeight)
                break
            }
        }
        
        // 更新总权重
        updateTotalWeight()
        updateSimulation()
    }
    
    // 更新总权重
    function updateTotalWeight() {
        var sum = 0
        for (var i = 0; i < portfolioModel.count; i++) {
            sum += portfolioModel.get(i).weight
        }
        totalWeight = sum
    }
    
    // 重新平衡权重
    function rebalanceWeights() {
        if (portfolioModel.count === 0) return
        
        var equalWeight = 100.0 / portfolioModel.count
        for (var i = 0; i < portfolioModel.count; i++) {
            portfolioModel.setProperty(i, "weight", equalWeight)
        }
        
        updateTotalWeight()
        updateSimulation()
    }
    
    // 重置权重
    function resetWeights() {
        console.log("重置权重")
        rebalanceWeights()
    }
    
    // 更新模拟绩效
    function updateSimulation() {
        console.log("更新模拟绩效")
        
        // 模拟计算（简化）
        var factorCount = portfolioModel.count
        if (factorCount === 0) {
            simulatedAnnualReturn = 0
            simulatedSharpeRatio = 0
            simulatedMaxDrawdown = 0
            return
        }
        
        var totalIc = 0
        var totalIr = 0
        var totalTurnover = 0
        var diversificationBonus = 0
        for (var i = 0; i < portfolioModel.count; i++) {
            var factor = portfolioModel.get(i)
            totalIc += Number(factor.icValue || 0)
            totalIr += Number(factor.irValue || 0)
            totalTurnover += Number(factor.turnoverRate || 0)
            diversificationBonus += (1.0 - Math.abs(Number(factor.correlation || 0))) * 1.5
        }

        var averageIc = totalIc / factorCount
        var averageIr = totalIr / factorCount
        var averageTurnover = totalTurnover / factorCount

        simulatedAnnualReturn = 8.0 + averageIc * 120 + averageIr * 2.2 + diversificationBonus
        simulatedSharpeRatio = 0.6 + averageIr * 0.45 + diversificationBonus * 0.08
        simulatedMaxDrawdown = 18.0 - diversificationBonus - Math.min(averageIc * 20, 4) + averageTurnover * 0.03
        
        // 确保在合理范围内
        simulatedAnnualReturn = Math.max(0, Math.min(simulatedAnnualReturn, 50))
        simulatedSharpeRatio = Math.max(0, Math.min(simulatedSharpeRatio, 4))
        simulatedMaxDrawdown = Math.max(0, Math.min(simulatedMaxDrawdown, 50))
    }
    
    // 获取因子颜色
    function getFactorColor(category) {
        switch (category) {
            case "动量类": return "#3B82F6"
            case "价值类": return "#F59E0B"
            case "质量类": return "#10B981"
            case "情绪类": return "#8B5CF6"
            case "流动性类": return "#06B6D4"
            default: return "#94A3B8"
        }
    }
    
    // 搜索因子
    function searchFactors() {
        console.log("刷新真实因子池")
        refreshFactorSources()
        notifications = [
            { type: "info", text: "已刷新真实因子池", time: "刚刚", action: "查看" }
        ]
    }

    function refreshFactorSources() {
        if (!factorService || !factorService.getAllFactors) {
            notifications = [
                { type: "warning", text: "FactorService 未连接，无法加载真实因子", time: "当前", action: "检查" }
            ]
            return
        }

        var factors = factorService.getAllFactors() || []
        syncFactorModels(factors)
    }

    function syncFactorModels(factors) {
        factorPoolModel.clear()
        commonFactorsModel.clear()

        var normalizedFactors = []
        for (var i = 0; i < factors.length; i++) {
            var normalized = normalizeFactorRecord(factors[i], i)
            if (normalized.factorId) {
                normalizedFactors.push(normalized)
                factorPoolModel.append(normalized)
            }
        }

        normalizedFactors.sort(function(left, right) {
            return factorScore(right) - factorScore(left)
        })

        for (var j = 0; j < Math.min(6, normalizedFactors.length); j++) {
            commonFactorsModel.append({
                factorId: normalizedFactors[j].factorId,
                displayName: normalizedFactors[j].displayName,
                frequency: Math.max(1, Math.round(factorScore(normalizedFactors[j]) * 10))
            })
        }

        if (portfolioModel.count === 0) {
            for (var k = 0; k < Math.min(initialPortfolioSize, normalizedFactors.length); k++) {
                portfolioModel.append({
                    factorId: normalizedFactors[k].factorId,
                    displayName: normalizedFactors[k].displayName,
                    weight: 0,
                    correlation: normalizedFactors[k].correlation,
                    icValue: normalizedFactors[k].icValue,
                    irValue: normalizedFactors[k].irValue,
                    turnoverRate: normalizedFactors[k].turnoverRate,
                    color: getFactorColor(normalizedFactors[k].category)
                })
            }
            rebalanceWeights()
        } else {
            refreshPortfolioMeta()
        }

        notifications = [
            {
                type: normalizedFactors.length > 0 ? "success" : "warning",
                text: normalizedFactors.length > 0
                    ? "已加载 " + normalizedFactors.length + " 个真实因子"
                    : "未读取到真实因子数据",
                time: "刚刚",
                action: normalizedFactors.length > 0 ? "查看" : "刷新"
            }
        ]

        updateTotalWeight()
        updateSimulation()
    }

    function refreshPortfolioMeta() {
        for (var i = 0; i < portfolioModel.count; i++) {
            var portfolioFactorId = portfolioModel.get(i).factorId
            for (var j = 0; j < factorPoolModel.count; j++) {
                var candidate = factorPoolModel.get(j)
                if (candidate.factorId === portfolioFactorId) {
                    portfolioModel.setProperty(i, "displayName", candidate.displayName)
                    portfolioModel.setProperty(i, "correlation", candidate.correlation)
                    portfolioModel.setProperty(i, "icValue", candidate.icValue)
                    portfolioModel.setProperty(i, "irValue", candidate.irValue)
                    portfolioModel.setProperty(i, "turnoverRate", candidate.turnoverRate)
                    portfolioModel.setProperty(i, "color", getFactorColor(candidate.category))
                    break
                }
            }
        }
    }

    function normalizeFactorRecord(rawFactor, index) {
        var factor = rawFactor || {}
        var factorId = String(factor.factorId || factor.id || factor.factorName || "")
        var displayName = String(factor.displayName || factor.factorName || factor.name || factorId)
        var category = resolveFactorCategory(factor)
        var icValue = Number(factor.icValue || 0)
        var irValue = Number(factor.irValue || 0)
        var turnoverRate = Number(factor.turnoverRate || 0)

        return {
            factorId: factorId,
            displayName: displayName,
            category: category,
            icValue: icValue,
            irValue: irValue,
            turnoverRate: turnoverRate,
            correlation: estimateFactorCorrelation(factor, index)
        }
    }

    function resolveFactorCategory(factor) {
        var categoryText = String(factor.majorCategory || factor.subCategory || factor.category || "")
        var lowered = categoryText.toLowerCase()
        if (lowered.indexOf("动量") >= 0 || lowered.indexOf("momentum") >= 0) return "动量类"
        if (lowered.indexOf("价值") >= 0 || lowered.indexOf("value") >= 0) return "价值类"
        if (lowered.indexOf("质量") >= 0 || lowered.indexOf("quality") >= 0) return "质量类"
        if (lowered.indexOf("情绪") >= 0 || lowered.indexOf("sentiment") >= 0) return "情绪类"
        if (lowered.indexOf("流动") >= 0 || lowered.indexOf("liquidity") >= 0) return "流动性类"
        return "综合类"
    }

    function estimateFactorCorrelation(factor, index) {
        var groupReturns = factor.groupReturns || []
        if (groupReturns.length >= 2) {
            var spread = Math.abs(Number(groupReturns[0] || 0) - Number(groupReturns[groupReturns.length - 1] || 0))
            return Math.max(0, Math.min(0.95, 0.6 - spread * 0.1))
        }

        var turnoverRate = Number(factor.turnoverRate || 0)
        return Math.max(0.05, Math.min(0.95, 0.15 + (turnoverRate % 40) / 100 + (index % 5) * 0.03))
    }

    function factorScore(factor) {
        return Math.abs(Number(factor.icValue || 0)) * 100
            + Math.abs(Number(factor.irValue || 0)) * 10
            + Math.max(0, 30 - Number(factor.turnoverRate || 0)) * 0.2
    }
    
    // 调整行业权重
    function adjustSectorWeight(sector) {
        console.log("调整行业权重:", sector)
        // 实现行业权重调整
    }
    
    // 调整风格暴露
    function adjustStyleExposure(style) {
        console.log("调整风格暴露:", style)
        // 实现风格暴露调整
    }
    
    // 运行回测
    function runBacktest() {
        console.log("运行组合回测")

        if (portfolioModel.count === 0) {
            notifications = [
                { type: "warning", text: "当前组合为空，无法发起回测", time: "刚刚", action: "添加因子" }
            ]
            return
        }

        if (!strategyService) {
            notifications = [
                { type: "warning", text: "StrategyService 未初始化，无法发起回测", time: "刚刚", action: "检查" }
            ]
            return
        }

        savePortfolio()

        if (!currentPortfolioId) {
            notifications = [
                { type: "warning", text: "组合保存失败，无法继续回测", time: "刚刚", action: "重试" }
            ]
            return
        }

        var backtestConfig = {
            source: "portfolio_builder",
            strategy_type: "PORTFOLIO",
            sub_type: "portfolio_builder",
            portfolio_name: portfolioName,
            factor_allocations: buildPortfolioStrategyData().parameters.allocations,
            estimated_metrics: {
                annual_return: simulatedAnnualReturn,
                sharpe_ratio: simulatedSharpeRatio,
                max_drawdown: simulatedMaxDrawdown
            }
        }

        requestBacktest(currentPortfolioId, portfolioName, backtestConfig)
    }
    
    // 保存组合
    function savePortfolio() {
        console.log("保存组合")

        if (!strategyService) {
            notifications = [
                { type: "warning", text: "StrategyService 未初始化，无法保存组合", time: "刚刚", action: "检查" }
            ]
            return
        }

        if (portfolioModel.count === 0) {
            notifications = [
                { type: "warning", text: "当前组合为空，无法保存", time: "刚刚", action: "添加因子" }
            ]
            return
        }

        var portfolioStrategyData = buildPortfolioStrategyData()
        var existingStrategy = currentPortfolioId && strategyService.getStrategyById
            ? strategyService.getStrategyById(currentPortfolioId)
            : ({})

        var success = false
        var savedStrategyId = currentPortfolioId

        if (existingStrategy && Object.keys(existingStrategy).length > 0 && strategyService.updateStrategy) {
            success = strategyService.updateStrategy(currentPortfolioId, portfolioStrategyData)
        } else if (strategyService.createStrategy) {
            savedStrategyId = strategyService.createStrategy(portfolioStrategyData)
            success = !!savedStrategyId
        }

        if (success) {
            if (savedStrategyId) {
                currentPortfolioId = savedStrategyId
            }

            notifications = [
                {
                    type: "success",
                    text: "组合已保存为策略: " + portfolioName,
                    time: "刚刚",
                    action: "查看"
                }
            ]
        } else {
            notifications = [
                {
                    type: "warning",
                    text: "组合保存失败，请检查数据库与策略服务状态",
                    time: "刚刚",
                    action: "重试"
                }
            ]
        }
    }
    
    // 自动优化
    function autoOptimize() {
        console.log("一键优化组合")
        // 实现优化算法
    }
    
    // 风险检查
    function riskCheck() {
        console.log("风险检查")
        // 实现风险检查
    }

    function buildPortfolioStrategyData() {
        var factorAllocations = []
        var parameters = {
            portfolio_name: portfolioName,
            total_weight: totalWeight,
            factor_count: portfolioModel.count,
            allocations: factorAllocations,
            estimated_metrics: {
                annual_return: simulatedAnnualReturn,
                sharpe_ratio: simulatedSharpeRatio,
                max_drawdown: simulatedMaxDrawdown
            },
            exposures: {
                sector: sectorExposure,
                style: styleExposure
            }
        }

        for (var i = 0; i < portfolioModel.count; i++) {
            var factor = portfolioModel.get(i)
            factorAllocations.push({
                factor_id: factor.factorId,
                display_name: factor.displayName,
                weight: factor.weight,
                correlation: factor.correlation,
                ic_value: Number(factor.icValue || 0),
                ir_value: Number(factor.irValue || 0),
                turnover_rate: Number(factor.turnoverRate || 0)
            })
        }

        return {
            strategy_name: portfolioName,
            strategy_type: "PORTFOLIO",
            description: "组合构建页保存的多因子组合策略",
            asset_type: "stock",
            time_frame: "daily",
            risk_level: simulatedMaxDrawdown > 20 ? "high" : (simulatedMaxDrawdown > 12 ? "medium" : "low"),
            optimization_method: "portfolio_builder",
            advanced_options: {
                source: "PortfolioBuilderPage",
                saved_at: new Date().toISOString()
            },
            parameters: parameters,
            sub_type: "portfolio_builder",
            status: "DRAFT",
            version: "1.0",
            language: "QML",
            author: "PortfolioBuilder",
            tags: ["组合策略", "多因子", "PortfolioBuilder"]
        }
    }
    
    // 处理通知操作
    function handleNotificationAction(index) {
        console.log("处理通知:", index, notifications[index])
        // 执行通知对应的操作
    }
    
    // 静音通知
    function muteNotifications() {
        console.log("静音通知")
    }
    
    // 清理通知
    function clearNotifications() {
        console.log("清理通知")
        notifications = []
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("组合构建页面初始化完成")
        console.log("当前组合:", portfolioName)
        console.log("因子数量:", portfolioModel.count)

        refreshFactorSources()
        updateTotalWeight()
        updateSimulation()
    }
}
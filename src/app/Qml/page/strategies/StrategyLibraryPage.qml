// pages/StrategyLibraryPage.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../../components/Strategy" as Components

Rectangle {
    id: strategyLibraryPage
    color: "#0F172A"  // primaryBg
    
    // 属性
    property int selectedStrategyIndex: 0
    property bool showFilter: false
    property bool showSorter: false
    property int runningStrategyIndex: 0
    
    // 信号
    signal createNewStrategy()
    signal strategySelected(string strategyName)
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color primaryBg: "#0F172A"
    readonly property color secondaryBg: "#1E293B"
    readonly property color tertiaryBg: "#334155"
    readonly property color accentBlue: "#3B82F6"
    readonly property color borderColor: "#475569"
    readonly property color warningAmber: "#F59E0B"
    readonly property color successGreen: "#10B981"
    
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeXLarge: 24
    
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    readonly property real spacingXLarge: 24
    
    readonly property real borderRadiusMedium: 8
    readonly property real borderRadiusXLarge: 16
    
    // 数据模型
    ListModel {
        id: strategyModel
        ListElement {
            name: "双均线策略"
            description: "基于短期和长期均线的趋势跟踪策略"
            status: "running"
            returns: "+12.5%"
            maxDrawdown: "-3.2%"
            sharpeRatio: "1.8"
            winRate: "65.4%"
            //tags: ["趋势", "股票", "日内"]
            runningDays: 45
            tradesCount: 128
            position: 0.75
            dailyPnL: 2450.50
        }
        ListElement {
            name: "RSI超买超卖策略"
            description: "基于RSI指标的均值回归策略"
            status: "paused"
            returns: "+8.7%"
            maxDrawdown: "-4.5%"
            sharpeRatio: "1.2"
            winRate: "58.9%"
           // tags: ["均值回归", "加密货币", "短线"]
            runningDays: 30
            tradesCount: 89
            position: 0.3
            dailyPnL: 1230.75
        }
        ListElement {
            name: "布林带突破策略"
            description: "基于布林带通道的突破交易策略"
            status: "stopped"
            returns: "+15.3%"
            maxDrawdown: "-5.1%"
            sharpeRatio: "2.1"
            winRate: "62.3%"
            //tags: ["突破", "期货", "趋势"]
            runningDays: 60
            tradesCount: 156
            position: 0
            dailyPnL: 0
        }
    }
    
    // 定时器 - 用于自动滚动
    Timer {
        id: autoScrollTimer
        interval: 3000  // 3秒切换一次
        running: true
        repeat: true
        onTriggered: {
            var runningCount = 0;
            for (var i = 0; i < strategyModel.count; i++) {
                if (strategyModel.get(i).status === "running") {
                    runningCount++;
                }
            }
            
            if (runningCount > 1) {
                runningStrategyIndex++;
                if (runningStrategyIndex >= runningCount) {
                    runningStrategyIndex = 0;
                }
            }
        }
    }
    
    // 主布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 头部区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            color: secondaryBg
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: spacingXLarge
                
                // 标题
                ColumnLayout {
                    spacing: spacingMedium
                    
                    Text {
                        text: "量化策略库"
                        font.pixelSize: fontSizeXLarge
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }
                    
                    Text {
                        text: "管理您的量化交易策略，监控实时运行状态"
                        font.pixelSize: fontSizeNormal
                        color: textTertiary
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // 操作按钮组
                Row {
                    spacing: spacingLarge
                    
                    // 筛选按钮
                    Components.StrategyFilterButton {
                        onClicked: showFilter = !showFilter
                    }
                    
                    // 排序按钮
                    Components.StrategySortButton {
                        onClicked: showSorter = !showSorter
                    }
                    
                    // 新建策略按钮
                    Components.CreateStrategyButton {
                        onClicked: createDialog.openDialog()
                    }
                }
            }
        }
        
        // 主要内容区域
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            
            // 自定义滚动条 - 修复语法
            ScrollBar.vertical: ScrollBar {
                id: verticalScrollBar
                policy: ScrollBar.AsNeeded
                width: 8
                padding: 0
                
                background: Rectangle {
                    color: "transparent"
                    implicitWidth: 8
                }
                
                contentItem: Rectangle {
                    implicitWidth: 8
                    radius: 4
                    color: "#475569"
                    opacity: verticalScrollBar.pressed ? 1.0 : 0.5
                }
            }
            
            ColumnLayout {
                width: scrollView.width - 10
                spacing: spacingXLarge
                
                // 列表头部信息
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    
                    Text {
                        text: "显示 " + strategyModel.count + " 个策略"
                        font.pixelSize: fontSizeNormal
                        color: textSecondary
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 视图切换按钮
                    Components.ViewModeToggle {
                        currentMode: "grid"
                        onModeChanged: {
                            // 视图模式切换逻辑
                        }
                    }
                }
                
                // 策略列表 - 卡片形式
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 420
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    radius: borderRadiusXLarge
                    color: secondaryBg
                    border.color: borderColor
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: spacingMedium
                        
                        Text {
                            text: "策略列表"
                            font.pixelSize: fontSizeLarge
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }
                        
                        // 策略卡片网格 - 2列布局
                        Grid {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            columns: 2
                            spacing: 12
                            
                            Repeater {
                                model: strategyModel
                                
                                delegate: Rectangle {
                                    width: parent.width / 2 - 6
                                    height: 140
                                    radius: borderRadiusMedium
                                    color: selectedStrategyIndex === index ? 
                                           Qt.rgba(59/255, 130/255, 246/255, 0.1) : "transparent"
                                    border.color: selectedStrategyIndex === index ? 
                                                 accentBlue : borderColor
                                    border.width: 1
                                    
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 6
                                        
                                        // 策略名称和状态
                                        RowLayout {
                                            Layout.fillWidth: true
                                            
                                            Text {
                                                text: model.name
                                                font.pixelSize: fontSizeNormal
                                                font.weight: Font.DemiBold
                                                color: textPrimary
                                                Layout.fillWidth: true
                                            }
                                            
                                            Rectangle {
                                                width: 8
                                                height: 8
                                                radius: 4
                                                color: model.status === "running" ? "#10B981" : 
                                                       model.status === "paused" ? "#F59E0B" : "#EF4444"
                                            }
                                        }
                                        
                                        // 策略描述
                                        Text {
                                            text: model.description
                                            font.pixelSize: fontSizeNormal - 1
                                            color: textSecondary
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        
                                        // 绩效指标
                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 3
                                            rowSpacing: 4
                                            columnSpacing: 8
                                            
                                            // 收益率
                                            Column {
                                                Layout.fillWidth: true
                                                
                                                Text {
                                                    text: "收益率"
                                                    font.pixelSize: fontSizeNormal - 1
                                                    color: textTertiary
                                                }
                                                
                                                Text {
                                                    text: model.returns
                                                    font.pixelSize: fontSizeNormal
                                                    font.weight: Font.Medium
                                                    color: model.returns.startsWith("+") ? "#10B981" : "#EF4444"
                                                }
                                            }
                                            
                                            // 夏普比率
                                            Column {
                                                Layout.fillWidth: true
                                                
                                                Text {
                                                    text: "夏普比率"
                                                    font.pixelSize: fontSizeNormal - 1
                                                    color: textTertiary
                                                }
                                                
                                                Text {
                                                    text: model.sharpeRatio
                                                    font.pixelSize: fontSizeNormal
                                                    color: textPrimary
                                                }
                                            }
                                            
                                            // 胜率
                                            Column {
                                                Layout.fillWidth: true
                                                
                                                Text {
                                                    text: "胜率"
                                                    font.pixelSize: fontSizeNormal - 1
                                                    color: textTertiary
                                                }
                                                
                                                Text {
                                                    text: model.winRate
                                                    font.pixelSize: fontSizeNormal
                                                    color: textPrimary
                                                }
                                            }
                                        }
                                    }
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            selectedStrategyIndex = index;
                                            strategyLibraryPage.strategySelected(model.name);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 策略控制
                Components.StrategyControls {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    currentStatus: selectedStrategyIndex >= 0 ? 
                                  strategyModel.get(selectedStrategyIndex).status : "stopped"
                    
                    onStartClicked: {
                        console.log("启动策略");
                        if (selectedStrategyIndex >= 0) {
                            strategyModel.setProperty(selectedStrategyIndex, "status", "running");
                        }
                    }
                    
                    onStopClicked: {
                        console.log("停止策略");
                        if (selectedStrategyIndex >= 0) {
                            strategyModel.setProperty(selectedStrategyIndex, "status", "stopped");
                        }
                    }
                    
                    onOptimizeClicked: {
                        console.log("优化策略");
                        optimizeStrategy();
                    }
                }
                
                // 详细信息区域 - 三列布局
                GridLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 420
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    columns: 3
                    columnSpacing: spacingLarge
                    rowSpacing: spacingLarge
                    
                    // 第一列：策略参数
                    Components.StrategyParameters {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        parameters: [
                            {name: "短期均线周期", value: 20, min: 5, max: 200, unit: "", color: accentBlue},
                            {name: "长期均线周期", value: 60, min: 10, max: 500, unit: "", color: accentBlue},
                            {name: "止损比例", value: 5, min: 1, max: 20, unit: "%", color: warningAmber},
                            {name: "止盈比例", value: 10, min: 1, max: 30, unit: "%", color: successGreen}
                        ]
                        
                        onParameterChanged: function(index, value) {
                            console.log("参数改变:", index, value);
                            updateStrategyParameter(index, value);
                        }
                        
                        onResetClicked: {
                            console.log("重置参数");
                            resetStrategyParameters();
                        }
                    }
                    
                    // 第二列：当前运行策略 - 单张卡片轮播
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: borderRadiusXLarge
                        color: secondaryBg
                        border.color: borderColor
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: spacingMedium
                            
                            RowLayout {
                                Layout.fillWidth: true
                                
                                Text {
                                    text: "当前运行策略"
                                    font.pixelSize: fontSizeLarge
                                    font.weight: Font.DemiBold
                                    color: textPrimary
                                }
                                
                                Item { Layout.fillWidth: true }
                                
                                // 轮播指示器
                                Row {
                                    spacing: 4
                                    Layout.alignment: Qt.AlignRight
                                    
                                    Repeater {
                                        model: {
                                            var count = 0;
                                            for (var i = 0; i < strategyModel.count; i++) {
                                                if (strategyModel.get(i).status === "running") {
                                                    count++;
                                                }
                                            }
                                            return count;
                                        }
                                        
                                        delegate: Rectangle {
                                            width: 6
                                            height: 6
                                            radius: 3
                                            color: index === runningStrategyIndex ? accentBlue : textTertiary
                                            opacity: index === runningStrategyIndex ? 1.0 : 0.5
                                        }
                                    }
                                }
                            }
                            
                            // 运行策略卡片 - 单张卡片轮播
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                
                                Repeater {
                                    model: strategyModel
                                    
                                    delegate: Rectangle {
                                        id: runningCard
                                        anchors.fill: parent
                                        radius: borderRadiusMedium
                                        visible: {
                                            if (model.status !== "running") return false;
                                            var runningIndex = 0;
                                            for (var i = 0; i < strategyModel.count; i++) {
                                                if (strategyModel.get(i).status === "running") {
                                                    if (runningIndex === runningStrategyIndex && i === index) {
                                                        return true;
                                                    }
                                                    runningIndex++;
                                                }
                                            }
                                            return false;
                                        }
                                        color: Qt.rgba(16/255, 185/255, 129/255, 0.05)
                                        border.color: Qt.rgba(16/255, 185/255, 129/255, 0.3)
                                        border.width: 1
                                        
                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 16
                                            spacing: 8
                                            
                                            // 卡片头部
                                            RowLayout {
                                                Layout.fillWidth: true
                                                
                                                Text {
                                                    text: model.name
                                                    font.pixelSize: fontSizeLarge
                                                    font.weight: Font.DemiBold
                                                    color: textPrimary
                                                    Layout.fillWidth: true
                                                }
                                                
                                                Rectangle {
                                                    width: 12
                                                    height: 12
                                                    radius: 6
                                                    color: "#10B981"
                                                }
                                            }
                                            
                                            Text {
                                                text: model.description
                                                font.pixelSize: fontSizeNormal
                                                color: textSecondary
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            
                                            // 实时数据
                                            GridLayout {
                                                Layout.fillWidth: true
                                                columns: 3
                                                rowSpacing: 8
                                                columnSpacing: 12
                                                
                                                // 运行天数
                                                Column {
                                                    Layout.fillWidth: true
                                                    
                                                    Text {
                                                        text: "运行天数"
                                                        font.pixelSize: fontSizeNormal - 1
                                                        color: textTertiary
                                                    }
                                                    
                                                    Text {
                                                        text: model.runningDays + " 天"
                                                        font.pixelSize: fontSizeNormal
                                                        font.weight: Font.Medium
                                                        color: textPrimary
                                                    }
                                                }
                                                
                                                // 交易次数
                                                Column {
                                                    Layout.fillWidth: true
                                                    
                                                    Text {
                                                        text: "交易次数"
                                                        font.pixelSize: fontSizeNormal - 1
                                                        color: textTertiary
                                                    }
                                                    
                                                    Text {
                                                        text: model.tradesCount + " 次"
                                                        font.pixelSize: fontSizeNormal
                                                        font.weight: Font.Medium
                                                        color: textPrimary
                                                    }
                                                }
                                                
                                                // 今日盈亏
                                                Column {
                                                    Layout.fillWidth: true
                                                    
                                                    Text {
                                                        text: "今日盈亏"
                                                        font.pixelSize: fontSizeNormal - 1
                                                        color: textTertiary
                                                    }
                                                    
                                                    Text {
                                                        text: (model.dailyPnL >= 0 ? "+$" : "-$") + Math.abs(model.dailyPnL).toFixed(0)
                                                        font.pixelSize: fontSizeNormal
                                                        font.weight: Font.Medium
                                                        color: model.dailyPnL >= 0 ? "#10B981" : "#EF4444"
                                                    }
                                                }
                                                
                                                // 累计收益
                                                Column {
                                                    Layout.fillWidth: true
                                                    Layout.columnSpan: 3
                                                    
                                                    Text {
                                                        text: "累计收益率"
                                                        font.pixelSize: fontSizeNormal - 1
                                                        color: textTertiary
                                                    }
                                                    
                                                    Text {
                                                        text: model.returns
                                                        font.pixelSize: fontSizeXLarge
                                                        font.weight: Font.Bold
                                                        color: model.returns.startsWith("+") ? "#10B981" : "#EF4444"
                                                    }
                                                }
                                            }
                                        }
                                        
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                selectedStrategyIndex = index;
                                                strategyLibraryPage.strategySelected(model.name);
                                            }
                                        }
                                    }
                                }
                                
                                // 空状态
                                Text {
                                    text: "没有正在运行的策略"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    anchors.centerIn: parent
                                    visible: {
                                        var hasRunning = false;
                                        for (var i = 0; i < strategyModel.count; i++) {
                                            if (strategyModel.get(i).status === "running") {
                                                hasRunning = true;
                                                break;
                                            }
                                        }
                                        return !hasRunning;
                                    }
                                }
                            }
                        }
                    }
                    
                    // 第三列：策略详情
                    Components.StrategyDetailCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        strategyName: selectedStrategyIndex >= 0 ? 
                                     strategyModel.get(selectedStrategyIndex).name : "未选择策略"
                        status: selectedStrategyIndex >= 0 ? 
                               strategyModel.get(selectedStrategyIndex).status : "stopped"
                        runningDays: selectedStrategyIndex >= 0 ? 
                                   strategyModel.get(selectedStrategyIndex).runningDays : 0
                        tradesCount: selectedStrategyIndex >= 0 ? 
                                   strategyModel.get(selectedStrategyIndex).tradesCount : 0
                        position: selectedStrategyIndex >= 0 ? 
                                 "$" + (strategyModel.get(selectedStrategyIndex).position * 100000).toFixed(0) : "$0"
                        dailyPnL: selectedStrategyIndex >= 0 ? 
                                 (strategyModel.get(selectedStrategyIndex).dailyPnL >= 0 ? "+$" : "-$") + 
                                 Math.abs(strategyModel.get(selectedStrategyIndex).dailyPnL).toFixed(0) : "$0"
                    }
                }
                
                // 策略图表
                Components.StrategyChart {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250
                    Layout.leftMargin: spacingXLarge
                    Layout.rightMargin: spacingXLarge
                    Layout.bottomMargin: spacingXLarge
                }
            }
        }
    }
    
    // 新建策略对话框
    Components.CreateStrategyDialog {
        id: createDialog
        anchors.centerIn: parent
        visible: isOpen
        
        onStrategyCreated: function(strategyData) {
            console.log("创建策略:", strategyData);
            // 添加到策略模型
            strategyModel.append({
                name: strategyData.name,
                description: strategyData.description,
                status: strategyData.status,
                returns: strategyData.returns,
                maxDrawdown: strategyData.maxDrawdown,
                sharpeRatio: strategyData.sharpeRatio,
                winRate: strategyData.winRate,
                tags: strategyData.tags,
                runningDays: 0,
                tradesCount: 0,
                position: 0,
                dailyPnL: 0
            });
        }
        
        onClosed: {
            // 关闭对话框
        }
    }
    
    // 筛选弹窗
    Components.StrategyFilter {
        id: filterComponent
        anchors.centerIn: parent
        visible: showFilter
        
        onFilterApplied: function(filterData) {
            console.log("应用筛选:", filterData);
            showFilter = false;
        }
        
        onFilterReset: function() {
            console.log("重置筛选");
        }
        
        onFilterClosed: function() {
            showFilter = false;
        }
    }
    
    // 排序弹窗
    Components.StrategySorter {
        id: sorterComponent
        anchors.centerIn: parent
        visible: showSorter
        
        onSortApplied: function(sortType) {
            console.log("应用排序:", sortType);
            showSorter = false;
        }
        
        onSortClosed: function() {
            showSorter = false;
        }
    }
    
    // 遮罩层
    Rectangle {
        anchors.fill: parent
        color: "#00000060"
        visible: showFilter || showSorter || createDialog.isOpen
        
        MouseArea {
            anchors.fill: parent
            onClicked: {
                showFilter = false;
                showSorter = false;
                createDialog.closeDialog();
            }
        }
    }
    
    // 工具函数
    function updateStrategyParameter(index, value) {
        console.log("更新参数:", index, value);
    }
    
    function resetStrategyParameters() {
        console.log("重置参数");
    }
    
    function optimizeStrategy() {
        console.log("优化策略");
    }
    
    // 初始化
    Component.onCompleted: {
        console.log("策略库页面初始化完成");
        if (strategyModel.count > 0) {
            selectedStrategyIndex = 0;
            strategyLibraryPage.strategySelected(strategyModel.get(0).name);
        }
    }
}
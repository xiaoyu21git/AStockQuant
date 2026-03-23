// pages/StrategyLibraryPage.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0  // 导入C++桥接模块
import "../../components/Strategy" as StrategyComponents
import "../../components/Base" as BaseComponents
import "../../components" as Components
import "../../utils/StrategyDataAdapter.js" as StrategyAdapter

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
    
    // C++服务引用
    property var strategyService: StrategyService
    property var strategyViewModel: null
    
    // 初始化策略服务 - 确保数据自动加载
    function initializeStrategyViewModel() {
        console.log("初始化策略服务...")
        
        // 获取StrategyService单例
        strategyService = StrategyService
        if (strategyService) {
            console.log("StrategyService 初始化成功")
            
            // 获取视图模型 - 先获取，以便绑定到UI
            strategyViewModel = strategyService.getViewModel()
            console.log("已获取StrategyViewModel，地址:", strategyViewModel, "count:", strategyViewModel ? strategyViewModel.count : 0)
            
            // 确保服务已经初始化
            if (!strategyService.isInitialized()) {
                console.log("StrategyService 尚未初始化，开始初始化...")
                strategyService.initialize()
                
                // 监听初始化完成信号
                strategyService.initializedChanged.connect(function() {
                    console.log("StrategyService 初始化完成，数据已加载")
                    // 初始化完成后手动触发一次数据同步
                    if (strategyService.isCacheLoaded()) {
                        console.log("缓存已加载，策略数量:", strategyViewModel ? strategyViewModel.count : 0)
                    }
                })
            } else {
                console.log("StrategyService 已经初始化，策略数量:", strategyViewModel ? strategyViewModel.count : 0)
                // 如果已经初始化，手动触发一次数据同步
                if (strategyService.isCacheLoaded()) {
                    console.log("缓存已加载，手动触发syncWithDatabase")
                    strategyService.syncWithDatabase()
                }
            }
            
            // 监听缓存加载完成信号
            strategyService.cacheLoadedChanged.connect(function() {
                if (strategyService.isCacheLoaded()) {
                    console.log("缓存加载完成，策略数量:", strategyViewModel ? strategyViewModel.count : 0)
                }
            })
            
            // 监听策略加载完成信号
            strategyService.strategiesLoaded.connect(function(strategies) {
                console.log("策略加载完成信号，数量:", strategies.length)
                // ViewModel应该已经自动更新了数据
                console.log("ViewModel count:", strategyViewModel ? strategyViewModel.count : 0)
            })
            
            // 监听dataChanged信号
            strategyService.dataChanged.connect(function() {
                console.log("策略数据已变更，ViewModel会自动更新")
                // 这里不需要手动更新，ViewModel应该自动更新
            })
            
            // 监听策略创建成功信号
            strategyService.strategyCreated.connect(function(strategyId, strategyData) {
                console.log("新策略创建成功，ID:", strategyId, "名称:", strategyData.strategy_name)
                // 数据变更信号会自动更新ViewModel
            })
            
            console.log("策略服务初始化完成，视图模型已绑定")
        } else {
            console.error("无法获取StrategyService实例")
        }
    }
    
    // 包装器函数：获取策略数量
    function getStrategyCount() {
        if (strategyViewModel) {
            return strategyViewModel.count
        }
        return 0
    }
    
    // 包装器函数：获取策略数据
    function getStrategyData(index) {
        if (strategyViewModel && index >= 0 && index < strategyViewModel.count) {
            return strategyViewModel.getRow(index)
        }
        return null
    }
    
    // 包装器函数：获取运行策略数量
    function getRunningStrategyCount() {
        var count = 0
        if (strategyViewModel) {
            for (var i = 0; i < strategyViewModel.count; i++) {
                var strategy = strategyViewModel.getRow(i)
                if (strategy && (strategy.status === "running" || strategy.status === "ACTIVE")) {
                    count++
                }
            }
        }
        return count
    }
    
    // 包装器函数：获取指定索引的运行策略
    function getRunningStrategy(runningIndex) {
        var runningCount = 0
        if (strategyViewModel) {
            for (var i = 0; i < strategyViewModel.count; i++) {
                var strategy = strategyViewModel.getRow(i)
                if (strategy && (strategy.status === "running" || strategy.status === "ACTIVE")) {
                    if (runningCount === runningIndex) {
                        return strategy
                    }
                    runningCount++
                }
            }
        }
        return null
    }
    
    // 数据模型（完全使用数据库数据，移除模拟数据）
    ListModel {
        id: strategyModel
        // 不再使用硬编码数据，完全依赖数据库
    }
    
    // 定时器 - 用于自动滚动
    Timer {
        id: autoScrollTimer
        interval: 3000  // 3秒切换一次
        running: true
        repeat: true
        onTriggered: {
            var runningCount = 0;
            
            // 只使用数据库数据
            if (strategyViewModel && strategyViewModel.count > 0) {
                for (var i = 0; i < strategyViewModel.count; i++) {
                    var strategy = strategyViewModel.getRow(i);
                    if (strategy && (strategy.status === "running" || strategy.status === "ACTIVE")) {
                        runningCount++;
                    }
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
                    StrategyComponents.StrategyFilterButton {
                        onClicked: showFilter = !showFilter
                    }
                    
                    // 排序按钮
                    StrategyComponents.StrategySortButton {
                        onClicked: showSorter = !showSorter
                    }
                    
                    // 新建策略按钮
                    StrategyComponents.CreateStrategyButton {
                        onClicked: {
                            strategyCreationLoader.active = true;
                        }
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
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff  // 隐藏垂直滚动条
            
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
                        text: "显示 " + (strategyViewModel ? strategyViewModel.count : strategyModel.count) + " 个策略 (数据库: " + (strategyViewModel ? strategyViewModel.count : 0) + ")"
                        font.pixelSize: fontSizeNormal
                        color: textSecondary
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 视图切换按钮
                    StrategyComponents.ViewModeToggle {
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
                        
                            // 策略卡片网格 - 使用统一的StrategyCard组件
                            GridView {
                                id: strategyGridView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                
                                // 模型绑定到StrategyViewModel
                                model: strategyViewModel
                                
                                // 2列布局，使用更大的卡片高度
                                cellWidth: (width - 30) / 2
                                cellHeight: 280  // 统一卡片高度+间距
                                
                                // 隐藏滚动条
                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AlwaysOff
                                }
                                
                                delegate: Components.StrategyCard {
                                    width: strategyGridView.cellWidth - 12
                                    height: strategyGridView.cellHeight - 20
                                    
                                    // 策略基本属性
                                    strategyId: model.strategyId || model.id || ""
                                    strategyName: model.strategyName || model.name || "未命名策略"
                                    displayName: model.strategyName || model.name || "未命名策略"
                                    strategyType: model.strategyType || "趋势策略"
                                    description: model.description || "暂无描述"
                                    status: model.status || "STOPPED"
                                    
                                    // 性能指标
                                    returns: parseFloat(model.returns) || 0.0
                                    sharpeRatio: parseFloat(model.sharpeRatio) || 0.0
                                    maxDrawdown: parseFloat(model.maxDrawdown) || 0.0
                                    winRate: parseFloat(model.winRate) || 0.0
                                    
                                    // 实时状态
                                    runningDays: model.runningDays || 0
                                    tradesCount: model.tradesCount || 0
                                    dailyPnL: parseFloat(model.dailyPnL) || 0
                                    position: parseFloat(model.position) || 0
                                    
                                    // 布局设置
                                    selected: false  // 禁用选中状态
                                    showMiniChart: true
                                    showParameterPanel: false  // 列表视图不显示参数面板
                                    cardWidth: strategyGridView.cellWidth - 12
                                    cardHeight: 260
                                    
                                    // 确保颜色正确
                                    Component.onCompleted: {
                                        // 如果数据适配器可用，使用统一的颜色映射
                                        if (typeof StrategyAdapter !== 'undefined') {
                                            categoryColor = StrategyAdapter.getStrategyTypeColor(strategyType)
                                        }
                                    }
                                    
                                    // 信号连接 - 移除卡片点击处理，只保留按钮点击
                                    // onClicked和onEntitySelected已被移除，因为卡片整体不可点击
                                    
                                    onStartClicked: {
                                        console.log("启动策略:", model.strategyId || model.id)
                                        if (strategyService && (model.strategyId || model.id)) {
                                            strategyService.activateStrategy(model.strategyId || model.id)
                                        }
                                    }
                                    
                                    onStopClicked: {
                                        console.log("停止策略:", model.strategyId || model.id)
                                        if (strategyService && (model.strategyId || model.id)) {
                                            strategyService.deactivateStrategy(model.strategyId || model.id)
                                        }
                                    }
                                    
                                    onOptimizeClicked: {
                                        console.log("优化策略:", model.strategyId || model.id)
                                        // TODO: 实现策略优化功能
                                    }
                                }
                            }
                    }
                }
                
                // 策略详细区域 - 使用统一的StrategyCard（集成策略控制功能）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
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
                            text: "策略详情与控制"
                            font.pixelSize: fontSizeLarge
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }
                        
                        // 当前选择的策略卡片
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: borderRadiusMedium
                            color: Qt.rgba(59/255, 130/255, 246/255, 0.05)
                            border.color: Qt.rgba(59/255, 130/255, 246/255, 0.3)
                            border.width: 1
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8
                                
                                // 空状态
                                Text {
                                    text: "请从上方策略列表中选择一个策略"
                                    font.pixelSize: fontSizeNormal
                                    color: textTertiary
                                    anchors.centerIn: parent
                                    visible: selectedStrategyIndex < 0
                                }
                                
                        // 已选择策略的详细信息 - 使用统一的StrategyCard
                        Components.StrategyCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: selectedStrategyIndex >= 0 && strategyViewModel && strategyViewModel.count > selectedStrategyIndex
                            
                            property var selectedStrategy: {
                                if (selectedStrategyIndex >= 0 && strategyViewModel && strategyViewModel.count > selectedStrategyIndex) {
                                    return strategyViewModel.getRow(selectedStrategyIndex);
                                }
                                return null;
                            }
                            
                            // 策略基本属性
                            strategyId: selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id || "") : ""
                            strategyName: selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : ""
                            displayName: selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : ""
                            strategyType: selectedStrategy ? (selectedStrategy.strategyType || "趋势策略") : "趋势策略"
                            description: selectedStrategy ? (selectedStrategy.description || "暂无描述") : "暂无描述"
                            status: selectedStrategy ? (selectedStrategy.status || "STOPPED") : "STOPPED"
                            
                            // 性能指标
                            returns: selectedStrategy ? parseFloat(selectedStrategy.returns) || 0.0 : 0.0
                            sharpeRatio: selectedStrategy ? parseFloat(selectedStrategy.sharpeRatio) || 0.0 : 0.0
                            maxDrawdown: selectedStrategy ? parseFloat(selectedStrategy.maxDrawdown) || 0.0 : 0.0
                            winRate: selectedStrategy ? parseFloat(selectedStrategy.winRate) || 0.0 : 0.0
                            
                            // 实时状态
                            runningDays: selectedStrategy ? (selectedStrategy.runningDays || 0) : 0
                            tradesCount: selectedStrategy ? (selectedStrategy.tradesCount || 0) : 0
                            dailyPnL: selectedStrategy ? parseFloat(selectedStrategy.dailyPnL) || 0 : 0
                            position: selectedStrategy ? parseFloat(selectedStrategy.position) || 0 : 0
                            
                            // 布局设置 - 详细视图显示更多信息
                            selected: true
                            showMiniChart: true
                            showParameterPanel: true  // 详细视图显示参数面板
                            cardWidth: parent.width - 32  // 减去边距
                            cardHeight: parent.height - 32
                            
                            // 确保颜色正确
                            Component.onCompleted: {
                                // 如果数据适配器可用，使用统一的颜色映射
                                if (typeof StrategyAdapter !== 'undefined' && selectedStrategy) {
                                    var strategyType = selectedStrategy.strategyType || "趋势策略"
                                    categoryColor = StrategyAdapter.getStrategyTypeColor(strategyType)
                                }
                            }
                            
                            // 信号连接
                            onStartClicked: {
                                console.log("启动策略:", selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id) : "")
                                if (strategyService && selectedStrategy && (selectedStrategy.strategyId || selectedStrategy.id)) {
                                    strategyService.activateStrategy(selectedStrategy.strategyId || selectedStrategy.id)
                                }
                            }
                            
                            onStopClicked: {
                                console.log("停止策略:", selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id) : "")
                                if (strategyService && selectedStrategy && (selectedStrategy.strategyId || selectedStrategy.id)) {
                                    strategyService.deactivateStrategy(selectedStrategy.strategyId || selectedStrategy.id)
                                }
                            }
                            
                            onOptimizeClicked: {
                                console.log("优化策略:", selectedStrategy ? (selectedStrategy.strategyId || selectedStrategy.id) : "")
                                // TODO: 实现策略优化功能
                                optimizeStrategy()
                            }
                        }
                            }
                        }
                        
                        // 提示信息
                        Text {
                            text: "提示：统一量化卡片组件已集成到策略库页面"
                            font.pixelSize: fontSizeNormal - 1
                            color: textTertiary
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                
                // 策略图表
                StrategyComponents.StrategyChart {
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
    StrategyComponents.CreateStrategyDialog {
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
    StrategyComponents.StrategyFilter {
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
    StrategyComponents.StrategySorter {
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
    
    // 新建策略页面加载器 - 使用专业版
    Loader {
        id: strategyCreationLoader
        anchors.fill: parent
        active: false
        source: "StrategyCreationPagePro.qml"
        
        onLoaded: {
            if (item) {
                // 连接返回信号
                if (typeof item.backClicked !== "undefined") {
                    item.backClicked.connect(function() {
                        console.log("收到创建页面返回信号，关闭创建页面")
                        strategyCreationLoader.active = false;
                        // 确保返回到策略库页面
                        strategyLibraryPage.forceActiveFocus();
                        // 注意：不需要手动调用syncWithDatabase，因为StrategyService.createStrategy()
                        // 已经发送了dataChanged信号，这个信号会被我们的监听器处理
                        console.log("创建页面已关闭，数据更新将由dataChanged信号处理")
                    });
                }
                
                // 连接策略创建信号（兼容旧版本）
                if (typeof item.strategyCreated !== "undefined") {
                    item.strategyCreated.connect(function(strategyData) {
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
                        
                        // 关闭创建页面
                        strategyCreationLoader.active = false;
                    });
                }
                
                // 连接回测请求信号
                if (typeof item.requestBacktest !== "undefined") {
                    item.requestBacktest.connect(function(strategyId, strategyName) {
                        console.log("接收到回测请求，策略ID:", strategyId, "策略名称:", strategyName);
                        // 关闭创建页面
                        strategyCreationLoader.active = false;
                        // 通知主窗口切换到回测页面
                        if (typeof window !== "undefined" && window.handleStrategyBacktestRequest) {
                            window.handleStrategyBacktestRequest(strategyId, strategyName);
                        }
                    });
                }
                
                // 专业版使用resetForm完成后的返回
                if (typeof item.resetForm !== "undefined") {
                    // 监听resetForm完成事件（如果有）
                }
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
        console.log("策略库页面初始化完成")
        // 初始化策略视图模型，连接到数据库
        initializeStrategyViewModel()
        
        // 注意：不再使用硬编码数据作为后备，完全依赖数据库数据
        // 策略数据将通过dataChanged信号自动更新
    }
}
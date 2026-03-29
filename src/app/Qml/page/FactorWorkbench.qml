// FactorWorkbench.qml
// 统一因子工作台 - 五模式量化因子工作台设计
// 多页面可见性切换方案，避免组件重新加载

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge
import "../utils/FactorSchemaLoader.js" as SchemaLoader
import ConsoleUi 1.0
import "../components/FactorWorkbench/Creation" as CreationComponents
import "../components/FactorWorkbench/Backtest" as BacktestComponents
import "../components/FactorWorkbench/Debug" as DebugComponents
import "../components/FactorWorkbench/Library" as LibraryComponents

/**
 * 统一因子工作台 - 五模式量化工作台设计
 * 多页面可见性切换方案，避免组件重新加载
 * 包含：因子库、创建、调试、分析、回测五大模式
 */
Item {
    id: root
    
    // ============ 页面属性 ============
    
    property string currentMode: "library"  // library, create, debug, analyze, backtest
    property string selectedFactorId: ""
    property string statusMessage: "📢 就绪"

    property string selectedType: ""  // 当前选择的因子类型
    property var factorMetaMap: null   // 全部metadata
    property var mergedMeta: null      // 合并后的meta
    
    // ============ C++ 数据绑定 ============
    
    // 因子服务（单例模式）- 直接使用，不需要实例化
    readonly property var factorService: Bridge.FactorService
    
    // 因子视图模型 - 直接从FactorService获取
    property var factorViewModel: factorService ? factorService.getViewModel() : null

    // 参数控制器
    Bridge.FactorParamController {
        id: factorParamController
        onParametersLoaded: function(type) {
            console.log("因子参数加载完成，类型:", type)
        }
        onFactorCreated: function(success, message, factorId) {
            console.log("因子创建结果:", success, "消息:", message, "因子ID:", factorId)
        }
    }

    // ============ 因子参数配置加载 ============
    Component.onCompleted: {
        console.log("FactorWorkbench 初始化完成")
        // 因子参数配置现在由 CreationPageDynamic 组件动态加载
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 顶部导航栏 - 使用外部组件
        ModeTitleBar {
            currentMode: root.currentMode
            showBackButton: currentMode !== "library"
            onModeSelected: function(mode) { switchMode(mode) }
            onBackClicked: switchMode("library")
        }
        
        // 主内容区 - 多页面并行加载，通过可见性控制显示
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        
            // 1. 因子库页面
            LibraryComponents.FactorLibraryPage {
                id: libraryPage
                anchors.fill: parent
                visible: root.currentMode === "library"
                factorService: root.factorService
                factorModel: root.factorViewModel
                selectedFactorId: root.selectedFactorId
                onFactorSelected: function(factorId) { handleFactorSelected(factorId) }
                onFactorDoubleClicked: function(factorId) { handleFactorDoubleClicked(factorId) }
                onFavoriteToggled: function(factorId, favorite) { handleFavoriteToggled(factorId, favorite) }
                onPreviewRequested: function(factorId) { handlePreviewRequested(factorId) }
                onAnalyzeRequested: function(factorId) { handleAnalyzeRequested(factorId) }
                onAddToPortfolio: function(factorId) { handleAddToPortfolio(factorId) }
                onEditRequested: function(factorId) { handleEditRequested(factorId) }
                onDeleteRequested: function(factorId) {
                    console.log("FactorLibraryPage 请求删除因子:", factorId)
                    showToast("🗑️ 删除因子: " + factorId)
                    factorService.deleteFactor(factorId)
                }
                onCreateRequested: switchMode("create")
                
                Component.onCompleted: {
                    console.log("FactorLibraryPage 初始化完成，factorModel:", factorModel ? "有效" : "无效")
                }
                
                // 页面激活时不需要刷新数据，因为deleteFactor会自动更新
                onVisibleChanged: {
                    if (visible) {
                        console.log("FactorLibraryPage 变为可见")
                        // 不需要调用refreshFactorLibrary，因为deleteFactor会自动更新视图模型
                    }
                }
            }
        
            // 2. 创建因子页面 - 使用插件化架构集成版
            CreationComponents.CreationPagePluginIntegrated {
                id: creationPage
                anchors.fill: parent
                anchors.topMargin: 10
                visible: root.currentMode === "create"
                selectedType: root.selectedType
                factorService: root.factorService
                onToastRequested: function(message) {
                    root.showToast(message)
                }
                onFactorCreated: function(factorData) {
                    handleFactorCreated(factorData)
                }
                onTypeChanged: function(type) {
                    root.selectedType = type
                }
                onBackClicked: switchMode("library")
            }
            
            // 3. 调试页面
            DebugComponents.DebugPage {
                id: debugPage
                anchors.fill: parent
                anchors.topMargin: 10
                visible: root.currentMode === "debug"
                factorService: root.factorService
                selectedFactorId: root.selectedFactorId
                
                Component.onCompleted: {
                    console.log("DebugPage 初始化完成")
                }
                
                // 页面激活时加载选中的因子
                onVisibleChanged: {
                    if (visible && selectedFactorId) {
                        console.log("DebugPage 变为可见，加载因子:", selectedFactorId)
                        if (typeof debugPage.autoValidateCurrentSelection === "function") {
                            debugPage.autoValidateCurrentSelection()
                        }
                    }
                }
            }
        
            // 4. 分析页面
            AnalysisPage {
                id: analysisPage
                anchors.fill: parent
                anchors.topMargin: 10
                visible: root.currentMode === "analyze"
                factorService: root.factorService
                selectedFactorId: root.selectedFactorId
                
                Component.onCompleted: {
                    console.log("AnalysisPage 初始化完成")
                }
                
                // 页面激活时分析选中的因子
                onVisibleChanged: {
                    if (visible && selectedFactorId) {
                        console.log("AnalysisPage 变为可见，分析因子:", selectedFactorId)
                        if (factorService) {
                            factorService.analyzeFactor(selectedFactorId)
                        }
                    }
                }
            }
        
            // 5. 因子回测页面
            FactorBacktestPage {
                id: backtestPage
                anchors.fill: parent
                anchors.topMargin: 10
                visible: root.currentMode === "backtest"
                factorService: root.factorService
                cleanedDataController: Bridge.CleanedDataController
                selectedFactorId: root.selectedFactorId
                onAnalysisReportRequested: function(result) {
                    console.log("回测完成，切换到分析报告页面")
                    root.showToast("📈 回测完成，已切换到分析报告")
                    switchMode("analyze")
                }
                
                Component.onCompleted: {
                    console.log("BacktestPage 初始化完成")
                    console.log("CleanedDataController:", cleanedDataController ? "有效" : "无效")
                }
                
                // 页面激活时回测选中的因子
                onVisibleChanged: {
                    if (visible && selectedFactorId) {
                        console.log("BacktestPage 变为可见，回测因子:", selectedFactorId)
                        if (factorService) {
                            factorService.backtestFactor(selectedFactorId)
                        }
                    }

                    if (visible && typeof backtestPage.rebuildCacheDatasetOptions === "function") {
                        backtestPage.rebuildCacheDatasetOptions()
                    }

                    if (visible && cleanedDataController && typeof cleanedDataController.refreshDatasets === "function") {
                        cleanedDataController.refreshDatasets()
                    }
                }
            }
        }
        
        // 底部通知栏
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#1E293B"
            
            Text {
                anchors.centerIn: parent
                text: root.statusMessage
                font.pixelSize: 14
                color: "#94A3B8"
            }
        }
    }
    
    // ============ 核心函数 ============
    
    // 切换模式
    function switchMode(mode) {
        console.log("切换到模式:", mode, "当前模式:", currentMode)
        currentMode = mode
        
        // 记录切换时的性能信息
        console.time("模式切换耗时")
        
        // 触发页面激活逻辑
        switch(mode) {
            case "library":
                break
            case "create":
                break
            case "debug":
                break
            case "analyze":
                break
            case "backtest":
                if (backtestPage && typeof backtestPage.rebuildCacheDatasetOptions === "function") {
                    backtestPage.rebuildCacheDatasetOptions()
                }
                if (Bridge.CleanedDataController && typeof Bridge.CleanedDataController.refreshDatasets === "function") {
                    Bridge.CleanedDataController.refreshDatasets()
                }
                break
        }
        
        console.timeEnd("模式切换耗时")
    }
    
    // 获取模式标题
    function getModeTitle(mode) {
        switch(mode) {
            case "library": return "📚 因子库浏览"
            case "create": return "📝 因子创建"
            case "debug": return "🔧 因子调试"
            case "analyze": return "📊 因子分析"
            case "backtest": return "🧪 因子回测"
            default: return "因子分析"
        }
    }
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
        statusMessage = message
    }
    
    // 处理因子选择
    function handleFactorSelected(factorId) {
        console.log("因子选择:", factorId)
        selectedFactorId = factorId
    }

    function handleFactorDoubleClicked(factorId) {
        console.log("因子双击:", factorId)
        selectedFactorId = factorId
        if (factorId && currentMode !== "debug") {
            switchMode("debug")
        }
    }

    function handleFavoriteToggled(factorId, favorite) {
        selectedFactorId = factorId
        showToast((favorite ? "⭐ 已收藏因子: " : "☆ 已取消收藏: ") + factorId)
    }

    function handlePreviewRequested(factorId) {
        selectedFactorId = factorId
        showToast("👁️ 预览因子: " + factorId)
        switchMode("analyze")
    }

    function handleAnalyzeRequested(factorId) {
        selectedFactorId = factorId
        showToast("📊 分析因子: " + factorId)
        switchMode("analyze")
    }

    function handleAddToPortfolio(factorId) {
        selectedFactorId = factorId
        showToast("➕ 添加到组合功能待接入: " + factorId)
    }

    function handleEditRequested(factorId) {
        selectedFactorId = factorId
        showToast("✏️ 编辑模式待接入，已选中因子: " + factorId)
    }
    
    // 处理因子创建
    function handleFactorCreated(factorData) {
        console.log("因子创建完成:", factorData)

        var displayName = factorData && factorData.displayName ? factorData.displayName : "未命名因子"
        if (factorData && factorData.factorId) {
            selectedFactorId = factorData.factorId
        }
        if (factorData && factorData.factorType) {
            selectedType = factorData.factorType
        }
        root.showToast("✅ 因子 '" + displayName + "' 创建成功！")
        
        // 切换到因子库页面
        switchMode("library")
    }
}

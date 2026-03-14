// FactorWorkbench.qml
// 统一因子工作台 - 六模式量化因子工作台设计
// 多页面可见性切换方案，避免组件重新加载
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

// 导入拆分的组件 - 使用QML模块导入
import ConsoleUi 1.0

/**
 * 统一因子工作台 - 六模式量化工作台设计
 * 多页面可见性切换方案，避免组件重新加载
 * 包含：首页、因子库、创建、调试、分析、回测六大模式
 */
Item {
    id: root
    
    // ============ 页面属性 ============
    
    property string currentMode: "home"  // home, library, create, debug, analyze, backtest
    property string selectedFactorId: ""
    property string selectedType: ""  // 当前选择的因子类型
    
    // ============ C++ 数据绑定 ============
    
    // 全局数据服务（单例模式）- 直接使用，不需要实例化
    property var globalDataService: Bridge.GlobalDataService
    
    // 因子服务（单例模式）- 直接使用，不需要实例化
    property var factorService: Bridge.FactorService
    
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
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 顶部导航栏 - 使用外部组件
        ModeTitleBar {
            currentMode: root.currentMode
            showBackButton: currentMode !== "home"
            onModeSelected: function(mode) { switchMode(mode) }
            onBackClicked: switchMode("home")
        }
        
        // 主内容区 - 多页面并行加载，通过可见性控制显示
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            // 1. 首页
            HomePage {
                id: homePage
                anchors.fill: parent
                anchors.topMargin: 40
                visible: root.currentMode === "home"
                globalDataService: root.globalDataService
                onStartCreation: switchMode("create")
                onOpenLibrary: switchMode("library")
                onOpenDebug: switchMode("debug")
                onOpenAnalysis: switchMode("analyze")
                onOpenBacktest: switchMode("backtest")
                
                Component.onCompleted: {
                    console.log("HomePage 初始化完成")
                }
            }
            
            // 2. 因子库页面
            FactorLibraryPage {
                id: libraryPage
                anchors.fill: parent
                anchors.topMargin: 40
                visible: root.currentMode === "library"
                factorService: root.factorService
                factorModel: root.factorViewModel
                selectedFactorId: root.selectedFactorId
                onFactorSelected: handleFactorSelected(factorId)
                onDeleteRequested: function(factorId) {
                    console.log("FactorLibraryPage 请求删除因子:", factorId)
                    factorService.deleteFactor(factorId)
                }
                
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
            
            // 3. 创建因子页面
            CreationPage {
                id: creationPage
                anchors.fill: parent
                anchors.topMargin: 40
                visible: root.currentMode === "create"
                globalDataService: root.globalDataService
                factorParamController: root.factorParamController
                factorService: root.factorService
                factorDataModel: root.factorViewModel
                selectedType: root.selectedType
                onFactorCreated: handleFactorCreated(factorData)
                
                Component.onCompleted: {
                    console.log("CreationPage 初始化完成")
                }
                
                // 页面激活时重置状态
                onVisibleChanged: {
                    if (visible) {
                        console.log("CreationPage 变为可见")
                        // 可以在这里执行一些页面激活时的操作
                    }
                }
            }
            
            // 4. 调试页面
            DebugPage {
                id: debugPage
                anchors.fill: parent
                anchors.topMargin: 40
                visible: root.currentMode === "debug"
                globalDataService: root.globalDataService
                factorService: root.factorService
                selectedFactorId: root.selectedFactorId
                
                Component.onCompleted: {
                    console.log("DebugPage 初始化完成")
                }
                
                // 页面激活时加载选中的因子
                onVisibleChanged: {
                    if (visible && selectedFactorId) {
                        console.log("DebugPage 变为可见，加载因子:", selectedFactorId)
                        if (factorService) {
                            factorService.loadFactorForDebug(selectedFactorId)
                        }
                    }
                }
            }
            
            // 5. 分析页面
            AnalysisPage {
                id: analysisPage
                anchors.fill: parent
                anchors.topMargin: 40
                visible: root.currentMode === "analyze"
                globalDataService: root.globalDataService
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
            
            // 6. 回测页面
            BacktestPage {
                id: backtestPage
                anchors.fill: parent
                anchors.topMargin: 40
                visible: root.currentMode === "backtest"
                globalDataService: root.globalDataService
                factorService: root.factorService
                selectedFactorId: root.selectedFactorId
                
                Component.onCompleted: {
                    console.log("BacktestPage 初始化完成")
                }
                
                // 页面激活时回测选中的因子
                onVisibleChanged: {
                    if (visible && selectedFactorId) {
                        console.log("BacktestPage 变为可见，回测因子:", selectedFactorId)
                        if (factorService) {
                            factorService.backtestFactor(selectedFactorId)
                        }
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
                text: "📢 通知栏"
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
            case "home":
                break
            case "library":
                break
            case "create":
                break
            case "debug":
                break
            case "analyze":
                break
            case "backtest":
                break
        }
        
        console.timeEnd("模式切换耗时")
    }
    
    // 获取模式标题
    function getModeTitle(mode) {
        switch(mode) {
            case "home": return "🏠 因子分析工作台"
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
        // TODO: 实现toast提示组件
    }
    
    // 处理因子选择
    function handleFactorSelected(factorId) {
        console.log("因子选择:", factorId)
        selectedFactorId = factorId
        // 可以根据需要切换到对应页面
        // switchMode("analyze")  // 例如选中后自动切换到分析页面
    }
    
    // 处理因子创建
    function handleFactorCreated(factorData) {
        console.log("因子创建完成:", factorData)
        // 自动选中新创建的因子
        selectedFactorId = factorData.factorId || factorData.id
        switchMode("analyze")
        showToast("✅ 因子创建成功！")
    }
    
    // ============ 初始化 ============
    
    // Component.onCompleted: {
    //     console.log("FactorWorkbench 初始化完成")
    //     console.log("页面初始化状态:")
    //     console.log("  - HomePage 已初始化:", homePage)
    //     console.log("  - LibraryPage 已初始化:", libraryPage)
    //     console.log("  - CreationPage 已初始化:", creationPage)
    //     console.log("  - DebugPage 已初始化:", debugPage)
    //     console.log("  - AnalysisPage 已初始化:", analysisPage)
    //     console.log("  - BacktestPage 已初始化:", backtestPage)
    //     console.log("  - factorViewModel:", factorViewModel ? "有效" : "无效")
    //     console.log("  - factorService:", factorService ? "有效" : "无效")
    // }
}
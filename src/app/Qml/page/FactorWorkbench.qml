// FactorWorkbench.qml
// 统一因子工作台 - 六模式量化因子工作台设计
// 组件化架构，导入并使用外部组件文件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

// 导入拆分的组件 - 使用QML模块导入
import ConsoleUi 1.0

/**
 * 统一因子工作台 - 六模式量化工作台设计
 * 组件化架构，使用外部组件文件
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
    
    // 因子服务（业务逻辑）- 负责数据库操作、缓存等
    Bridge.FactorService {
        id: factorService
        onFactorsLoaded: function(factors) {
            console.log("✅ 因子数据加载完成，数量:", factors.length)
            // 更新FactorViewModel的数据
            if (factorViewModel) {
                factorViewModel.updateData(factors)
            }
        }
        onFactorAdded: function(factorId, factorData) {
            console.log("✅ 因子添加完成:", factorId)
            // 添加单个因子到FactorViewModel
            if (factorViewModel) {
                factorViewModel.appendData(factorData)
            }
        }
        onFactorUpdated: function(factorId, factorData) {
            console.log("✅ 因子更新完成:", factorId)
            // 更新FactorViewModel中的因子
            if (factorViewModel) {
                factorViewModel.updateFactor(factorId, factorData)
            }
        }
        onFactorDeleted: function(factorId) {
            console.log("✅ 因子删除完成:", factorId)
            // 从FactorViewModel中删除因子
            if (factorViewModel) {
                factorViewModel.removeFactor(factorId)
            }
        }
        onErrorOccurred: function(errorMessage) {
            console.error("❌ 因子服务错误:", errorMessage)
        }
        
        // 初始化因子服务
        Component.onCompleted: {
            console.log("FactorService Component.onCompleted: 开始初始化因子服务")
            initialize()
        }
    }
    
    // 因子视图模型 - 只负责视图更新，像PreviewDataModel一样简单
    Bridge.FactorViewModel {
        id: factorViewModel
    }
    
    // 参数控制器
    Bridge.FactorParamController {
        id: factorParamController
        onParametersLoaded: function(type) {
            console.log("因子参数加载完成，类型:", type)
        }
        onFactorCreated: function(success, factorId, errorMessage) {
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
        
        // 主内容区 - 添加顶部内边距以避免与工作流导航栏重叠
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            Loader {
                id: contentLoader
                anchors.fill: parent
                anchors.topMargin: 40  // 增加顶部边距，避免与工作流导航栏重叠
                sourceComponent: getComponentForMode(currentMode)
                onLoaded: {
                    // Loader 动态加载时，手动传递数据模型，解决作用域丢失问题
                    if (item) {
                        // 检查并设置 factorModel 属性（用于 FactorLibraryPage）
                        if (item.hasOwnProperty("factorModel")) {
                            item.factorModel = factorViewModel
                        }
                        // 检查并设置 factorDataModel 属性（用于 CreationPage, DebugPage, AnalysisPage, BacktestPage）
                        if (item.hasOwnProperty("factorDataModel")) {
                            item.factorDataModel = factorViewModel
                        }
                        // 检查并设置 factorService 属性
                        if (item.hasOwnProperty("factorService")) {
                            item.factorService = factorService
                        }
                        // 检查并设置 globalDataService 属性
                        if (item.hasOwnProperty("globalDataService")) {
                            item.globalDataService = root.globalDataService
                        }
                        // 检查并设置 factorParamController 属性
                        if (item.hasOwnProperty("factorParamController")) {
                            item.factorParamController = factorParamController
                        }
                    }
                }
            }
        }
        
        // 底部通知栏 - 暂时使用占位符
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
        console.log("切换到模式:", mode)
        currentMode = mode
        contentLoader.sourceComponent = getComponentForMode(mode)
    }
    
    // 获取对应模式的组件
    function getComponentForMode(mode) {
        switch(mode) {
            case "home": return homeComponent
            case "library": return libraryComponent
            case "create": return creationComponent
            case "debug": return debugComponent
            case "analyze": return analysisComponent
            case "backtest": return backtestComponent
            default: return homeComponent
        }
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
        // 可以在这里触发其他操作
    }
    
    // 处理因子创建
    function handleFactorCreated(factorData) {
        console.log("因子创建完成:", factorData)
        switchMode("analyze")
        showToast("✅ 因子创建成功！")
    }
    
    // ============ 组件定义 ============
    
    // 首页组件 - 使用外部组件
    Component {
        id: homeComponent
        HomePage {
            globalDataService: root.globalDataService
            onStartCreation: switchMode("create")
            onOpenLibrary: switchMode("library")
            onOpenDebug: switchMode("debug")
            onOpenAnalysis: switchMode("analyze")
            onOpenBacktest: switchMode("backtest")
        }
    }
    
    // 因子库组件 - 使用外部组件
    Component {
        id: libraryComponent
        FactorLibraryPage {
            //factorModel: root.factorViewModel  // 使用新的因子视图模型
            selectedFactorId: root.selectedFactorId
            onFactorSelected: handleFactorSelected(factorId)
        }
    }
    
    // 创建因子组件 - 使用外部组件
    Component {
        id: creationComponent
        CreationPage {
            globalDataService: root.globalDataService
            factorParamController: root.factorParamController
            factorService: root.factorService      // 添加因子服务
            factorDataModel: root.factorViewModel  // 添加因子数据模型
            selectedType: root.selectedType
            onFactorCreated: handleFactorCreated(factorData)
            
            // 添加调试信息
            Component.onCompleted: {
                if (factorDataModel) {
                    console.log("✅ CreationPage 成功获取 factorViewModel")
                } else {
                    console.error("❌ CreationPage 未能获取 factorViewModel")
                }
            }
        }
    }
    
    // 调试组件 - 使用外部组件
    Component {
        id: debugComponent
        DebugPage {
            globalDataService: root.globalDataService
            //factorDataModel: root.factorViewModel  // 使用新的因子视图模型
            factorService: root.factorService      // 添加因子服务
            selectedFactorId: root.selectedFactorId
        }
    }
    
    // 分析组件 - 使用外部组件
    Component {
        id: analysisComponent
        AnalysisPage {
            globalDataService: root.globalDataService
            //factorDataModel: root.factorViewModel  // 使用新的因子视图模型
            factorService: root.factorService      // 添加因子服务
            selectedFactorId: root.selectedFactorId
        }
    }
    
    // 回测组件 - 使用外部组件
    Component {
        id: backtestComponent
        BacktestPage {
            globalDataService: root.globalDataService
            //factorDataModel: root.factorViewModel  // 使用新的因子视图模型
            factorService: root.factorService      // 添加因子服务
            selectedFactorId: root.selectedFactorId
        }
    }
}
cmake_minimum_required(VERSION 3.16)
project(AStockQuantApp VERSION 1.0.0 LANGUAGES CXX)

# 基本配置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 自动处理 Qt 的元对象系统
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

# 查找 Qt6 find_package(Qt5 COMPONENTS GraphicalEffects REQUIRED) 
find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Widgets 
    Quick 
    Gui 
    Sql 
    Qml 
    Multimedia 
    QuickWidgets
)

# 设置 Qt 项目
qt_standard_project_setup()

# 添加子目录（假设 src/ 中有 CMakeLists.txt）
#add_subdirectory(src)

# 创建主库目标
add_library(astockquantapp SHARED
    # 如果有源文件可以在这里添加，否则在 src/CMakeLists.txt 中处理
    src/AppBootstrap.cpp
    src/VasAurora.cpp
)

# 设置库的属性
target_compile_definitions(astockquantapp PRIVATE BUILD_DLL)

# 链接依赖
target_link_libraries(astockquantapp
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Quick
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Sql
        Qt6::Qml
        Qt6::QuickWidgets
    PRIVATE
        domain
        bridge                # 确保这些目标已定义
        foundation
)
# 创建QML模块
qt_add_qml_module(astockquantapp
    URI ConsoleUi
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES
        Qml/main.qml
        Qml/page/Sidebar.qml
        Qml/page/backtest/BacktestPage.qml
        Qml/page/dashboard/ChartArea.qml
        Qml/page/dashboard/MainContent.qml
        Qml/page/dashboard/MarketGrid.qml
        Qml/page/dashboard/PositionsPanel.qml
        Qml/page/dashboard/StrategiesPanel.qml
        Qml/page/dashboard/TradingPanel.qml
        Qml/page/dashboard/OrderBook.qml
        Qml/page/leftSidebar/DynamicMenu.qml    
        Qml/page/leftSidebar/FundOperationPanel.qml
        Qml/page/leftSidebar/MenuModel.qml
        Qml/page/leftSidebar/SecondaryMenu.qml
        Qml/page/leftSidebar/MenuTitle.qml
        Qml/page/leftSidebar/PrimaryMenu.qml
        Qml/page/strategies/StrategyLibraryPage.qml
        Qml/page/DataAnalysis/Datamain.qml  
        Qml/page/risk/RiskConfigurationPage.qml
        Qml/components/Risk/RiskCategoryCard.qml
        Qml/components/Risk/RiskParameterSlider.qml
        Qml/components/Risk/RiskParameterInput.qml
        Qml/components/DatePicker.qml
        Qml/components/ThemeSwitcher.qml
        Qml/components/GlobalSnackbar.qml
        Qml/components/ButtonPrimary.qml
        Qml/components/ButtonSecondary.qml
        Qml/components/ButtonSmall.qml
        Qml/components/ButtonBuy.qml
        Qml/components/ButtonIcon.qml
        Qml/components/TopNavigation/TopNavigation.qml
        Qml/components/TopNavigation/BreadcrumbSeparator.qml
        Qml/components/TopNavigation/BreadcrumbItem.qml
        Qml/components/TopNavigation/BreadcrumbNavigation.qml
        Qml/components/TopNavigation/IconButton.qml
        Qml/components/TopNavigation/SearchBox.qml
        Qml/components/Base/Constants.qml
        Qml/components/UserInfoPanel.qml
        Qml/components/Strategy/ViewModeToggle.qml
        Qml/components/Strategy/StrategySortButton.qml
        Qml/components/Strategy/StrategyParameters.qml
        Qml/components/Strategy/StrategyFilterButton.qml
        Qml/components/Strategy/StrategyDetailCard.qml
        Qml/components/Strategy/StrategyControls.qml
        Qml/components/Strategy/StrategyChart.qml
        Qml/components/Strategy/CreateStrategyDialog.qml
        Qml/components/Strategy/StrategyFilter.qml
        Qml/components/Strategy/CreateStrategyButton.qml
        Qml/components/Strategy/StrategySorter.qml
        Qml/components/DataAnalysis/theme.qml
        Qml/components/DataAnalysis/DataTable.qml
        Qml/components/DataAnalysis/ModuleCard.qml
        Qml/components/DataAnalysis/QuickStart.qml
        Qml/components/DataAnalysis/StatsCard.qml
        Qml/components/DataAnalysis/TaskCategory.qml
        Qml/components/DataAnalysis/WorkflowSteps.qml
        Qml/components/DataAnalysis/EmbeddedWorkflow.qml
        Qml/components/DataAnalysis/DataSourceModal.qml
        Qml/components/DataAnalysis/RulesConfigModal.qml
        Qml/components/DataAnalysis/DataPreviewModal.qml
        Qml/components/DataAnalysis/DataCleaningModal.qml
        Qml/components/DataAnalysis/DataOverviewCard.qml
        Qml/components/DataAnalysis/ProcessNode.qml
        Qml/components/DataAnalysis/ProcessFlow.qml
        Qml/components/DataAnalysis/CoreModule.qml
        Qml/components/Navigation/PageNavigator.qml
        Qml/components/GlobalWorkflow.qml
    SOURCES
       # Qml/resources/
    RESOURCES

        # 如果有相关的C++类，可以在这里添加
)

# 包含目录
target_include_directories(astockquantapp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/foundation/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/foundation/public
)

# 创建可执行文件
add_executable(astockquantapp-exe
    main.cpp
    Qml/qml.qrc
)

target_link_libraries(astockquantapp-exe
    PRIVATE
        astockquantapp
)

# 测试
include(CTest)
enable_testing()
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
        Qml/Page/dashboard/ChartArea.qml
        Qml/Page/dashboard/MainContent.qml
        Qml/Page/dashboard/MarketGrid.qml
        Qml/Page/dashboard/PositionsPanel.qml
        Qml/Page/dashboard/StrategiesPanel.qml
        Qml/Page/dashboard/TradingPanel.qml
        Qml/Page/dashboard/OrderBook.qml
        Qml/components/StatCard.qml
        Qml/components/TradeTable.qml
        Qml/components/Calendar.qml
        Qml/components/DayCell.qml
        Qml/components/MobileDatePicker.qml
        Qml/components/DateTimePicker.qml
        Qml/components/EquityChart.qml
        Qml/components/ThemeSwitcher.qml
        Qml/components/GradientButton.qml
        Qml/components/GlobalSnackbar.qml
        Qml/components/ButtonPrimary.qml
        Qml/components/ButtonSecondary.qml
        Qml/components/ButtonSmall.qml
        Qml/components/ButtonBuy.qml
        Qml/components/ButtonIcon.qml
        Qml/components/AlertSuccess.qml
        Qml/components/TopNavigation/TopNavigation.qml
        Qml/components/TopNavigation/BreadcrumbSeparator.qml
        Qml/components/TopNavigation/BreadcrumbItem.qml
        Qml/components/TopNavigation/BreadcrumbNavigation.qml
        Qml/components/TopNavigation/IconButton.qml
        Qml/components/TopNavigation/SearchBox.qml
        Qml/components/Base/Constants.qml
    SOURCES
       # Qml/resources/
    RESOURCES
        Qml/resources/icons/chart-bar.svg
        Qml/resources/icons/chart-line.svg
        Qml/resources/icons/robot.svg
        Qml/resources/icons/plus.svg
        Qml/resources/icons/refresh.svg
        Qml/resources/icons/search.svg
        Qml/resources/icons/arrow_back.svg
        Qml/resources/icons/refresh.svg
        Qml/resources/icons/filter_list.svg
        Qml/resources/icons/close.svg
        Qml/resources/icons/search.svg
        Qml/resources/icons/download.svg
        Qml/resources/icons/100.svg
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
)

target_link_libraries(astockquantapp-exe
    PRIVATE
        astockquantapp
)

# 测试
include(CTest)
enable_testing()
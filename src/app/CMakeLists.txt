cmake_minimum_required(VERSION 3.16)
project(AStockQuantApp VERSION 1.0.0 LANGUAGES CXX)

# 鍩烘湰閰嶇疆
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 鑷姩澶勭悊 Qt 鐨勫厓瀵硅薄绯荤粺
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

# 鏌ユ壘 Qt6 find_package(Qt5 COMPONENTS GraphicalEffects REQUIRED) 
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

# 璁剧疆 Qt 椤圭洰
qt_standard_project_setup()

# 娣诲姞瀛愮洰褰曪紙鍋囪 src/ 涓湁 CMakeLists.txt锛?
#add_subdirectory(src)

# 鍒涘缓涓诲簱鐩爣
add_library(astockquantapp SHARED
    # 濡傛灉鏈夋簮鏂囦欢鍙互鍦ㄨ繖閲屾坊鍔狅紝鍚﹀垯鍦?src/CMakeLists.txt 涓鐞?
    src/AppBootstrap.cpp
    src/VasAurora.cpp
)

if(ENABLE_JUJIN_MARKET)
    target_sources(astockquantapp
        PRIVATE
            src/JujinMarketConnector.cpp
    )

    target_compile_definitions(astockquantapp
        PRIVATE
            ASTOCK_ENABLE_JUJIN_MARKET=1
    )
endif()

# 璁剧疆搴撶殑灞炴€?
target_compile_definitions(astockquantapp PRIVATE BUILD_DLL)

# 閾炬帴渚濊禆
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
        engine
        domain
        bridge                # 纭繚杩欎簺鐩爣宸插畾涔?
        foundation
        domain_model
)

if(ENABLE_JUJIN_MARKET)
    target_link_libraries(astockquantapp
        PRIVATE
            thirdParty
    )

    get_filename_component(GMSDK_RUNTIME_DIR "${GMSDK_LIBRARY}" DIRECTORY)
    foreach(GMSDK_RUNTIME_FILE gmsdk.dll thostmduserapi_se.dll thostmd_wrap.dll)
        if(EXISTS "${GMSDK_RUNTIME_DIR}/${GMSDK_RUNTIME_FILE}")
            add_custom_command(TARGET astockquantapp POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GMSDK_RUNTIME_DIR}/${GMSDK_RUNTIME_FILE}"
                    "$<TARGET_FILE_DIR:astockquantapp>/${GMSDK_RUNTIME_FILE}"
            )
        endif()
    endforeach()
endif()
# 鍒涘缓QML妯″潡
qt_add_qml_module(astockquantapp
    URI ConsoleUi
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES
        Qml/main.qml
        Qml/page/Sidebar.qml
        Qml/page/backtest/StrategyBacktestPage.qml
        Qml/page/dashboard/ChartArea.qml
        Qml/page/dashboard/MainContent.qml
        Qml/page/dashboard/MarketGrid.qml
        Qml/page/dashboard/PositionsPanel.qml
        Qml/page/dashboard/QtCandlestickChart.qml
        Qml/page/dashboard/StatusSummaryCard.qml
        Qml/page/dashboard/StrategiesPanel.qml
        Qml/page/dashboard/TradingPanel.qml
        Qml/page/dashboard/OrderBook.qml
        Qml/page/trading/TradingPage.qml
        Qml/page/settings/SystemSettingsPage.qml
        Qml/page/leftSidebar/DynamicMenu.qml    
        Qml/page/leftSidebar/FundOperationPanel.qml
        Qml/page/leftSidebar/MenuModel.qml
        Qml/page/leftSidebar/SecondaryMenu.qml
        Qml/page/leftSidebar/MenuTitle.qml
        Qml/page/leftSidebar/PrimaryMenu.qml
        Qml/page/strategies/StrategyLibraryPage.qml
        Qml/page/strategies/StrategyCreationPagePro.qml
        Qml/page/DataAnalysis/Datamain.qml
        Qml/page/risk/RiskConfigurationPage.qml
        Qml/components/BaseQuantCard.qml
        Qml/components/FactorCard.qml
        Qml/components/StrategyCard.qml
        Qml/components/Risk/RiskCategoryCard.qml
        Qml/components/Risk/RiskParameterSlider.qml
        Qml/components/Risk/RiskParameterInput.qml
        Qml/components/DatePicker.qml
        Qml/components/ThemeSwitcher.qml
        Qml/components/GlobalSnackbar.qml
        Qml/components/ButtonPrimary.qml
        Qml/components/ButtonSecondary.qml
        Qml/components/ButtonSmall.qml
        Qml/components/Backtest/BacktestLabeledDatePicker.qml
        Qml/components/Backtest/BacktestLabeledComboBox.qml
        Qml/components/Backtest/BacktestMetricCard.qml
        Qml/components/Backtest/BacktestParameterPanel.qml
        Qml/components/Backtest/BacktestResultPanel.qml
        Qml/components/Backtest/BacktestStatItem.qml
        Qml/components/ButtonBuy.qml
        Qml/components/ButtonIcon.qml
        Qml/components/TopNavigation/TopNavigation.qml
        Qml/components/TopNavigation/BreadcrumbSeparator.qml
        Qml/components/TopNavigation/BreadcrumbItem.qml
        Qml/components/TopNavigation/BreadcrumbNavigation.qml
        Qml/components/TopNavigation/IconButton.qml
        Qml/components/TopNavigation/SearchBox.qml
        Qml/components/Trading/TradingFormPanel.qml
        Qml/components/Trading/DepthMarketPanel.qml
        Qml/components/Base/Constants.qml
        Qml/components/Base/MetaTable.qml
        Qml/components/Base/MetaForm.qml
        Qml/components/Base/MetaCard.qml
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
        # 绛栫暐鍒涘缓缁勪欢
        Qml/components/Strategy/Creation/StrategyTypeSelector.qml
        Qml/components/Strategy/Creation/StrategyBasicInfo.qml
        Qml/components/Strategy/Creation/StrategyParamConfig.qml
        Qml/components/DataAnalysis/DataTable.qml
        Qml/components/DataAnalysis/ModuleCard.qml
        Qml/components/DataAnalysis/QuickStart.qml
        Qml/components/DataAnalysis/StatsCard.qml
        Qml/components/DataAnalysis/TaskCategory.qml
        Qml/components/DataAnalysis/WorkflowSteps.qml
        Qml/components/DataAnalysis/EmbeddedWorkflow.qml
        Qml/components/DataAnalysis/RulesConfigModal.qml
        Qml/components/DataAnalysis/DataOverviewCard.qml
        Qml/components/DataAnalysis/DataSelectionPanel.qml
        Qml/components/DataAnalysis/ProcessNode.qml
        Qml/components/DataAnalysis/ProcessFlow.qml
        Qml/components/DataAnalysis/CoreModule.qml
        Qml/components/Navigation/PageNavigator.qml
        Qml/components/GlobalWorkflow.qml
        Qml/components/Factor/FactorParameterInput.qml
        Qml/components/Factor/FactorTypeParameterPage.qml
        Qml/components/Factor/FactorListRow.qml
        Qml/components/Factor/FactorCardEnhanced.qml
        Qml/page/FactorWorkbench.qml
        Qml/page/PortfolioBuilderPage.qml
        # FactorWorkbench缁勪欢鏂囦欢
        Qml/components/FactorWorkbench/Library/FactorLibraryPage.qml
        Qml/components/FactorWorkbench/Navigation/ModeTitleBar.qml
        Qml/components/FactorWorkbench/Creation/FactorTypeCard.qml
        Qml/components/FactorWorkbench/Creation/CreationPagePluginIntegrated.qml
        # 鎻掍欢鍖栫粍浠?
        Qml/components/FactorWorkbench/Creation/components/ParamComponents.qml
        Qml/components/FactorWorkbench/Creation/components/DynamicParamGenerator.qml
        Qml/components/FactorWorkbench/Creation/components/SliderParam.qml
        Qml/components/FactorWorkbench/Creation/components/SelectParam.qml
        Qml/components/FactorWorkbench/Creation/components/MultiSelectParam.qml
        Qml/components/FactorWorkbench/Creation/components/InputParam.qml
        Qml/components/FactorWorkbench/Creation/components/ToggleParam.qml
        Qml/components/FactorWorkbench/Creation/components/ParamRegistry.qml
        Qml/components/Factor/FactorParamForm.qml
        Qml/components/Factor/DynamicParamForm.qml
        Qml/utils/FactorMetaLoader.js
        Qml/utils/FactorSchemaLoader.js
        Qml/utils/FactorDefaultParams.js
        Qml/utils/StrategyMetaLoader.js
        Qml/utils/StrategyCreationUtils.js
        Qml/utils/I18n.js
        Qml/utils/BacktestPerformanceAdapter.js
        Qml/utils/BacktestResultAdapter.js
        Qml/utils/RiskBacktestMetaLoader.js
        Qml/utils/StrategyDataAdapter.js
        Qml/utils/FactorDataAdapter.js
        Qml/utils/TradingPageAdapter.js
        Qml/utils/TradeTestingPageAdapter.js
        Qml/components/FactorWorkbench/Analysis/AnalysisPage.qml
        Qml/components/FactorWorkbench/Backtest/FactorBacktestPage.qml
        Qml/components/FactorWorkbench/Backtest/BacktestConfigDialog.qml
        Qml/components/FactorWorkbench/Backtest/BacktestResultView.qml
        Qml/components/FactorWorkbench/Backtest/GroupConfigPanel.qml
        Qml/components/FactorWorkbench/Backtest/GroupResultChart.qml
        Qml/components/FactorWorkbench/Debug/DebugPage.qml
        Qml/components/FactorWorkbench/Backtest/FactorSelectorDialog.qml
        # 娉ㄦ剰锛欶actorWizardStep.qml宸茶鍒犻櫎锛屾柊寤哄洜瀛愬姛鑳藉凡缁熶竴鍒板洜瀛愬垎鏋愬伐浣滃彴
    SOURCES
       # Qml/resources/
    RESOURCES

        # 濡傛灉鏈夌浉鍏崇殑C++绫伙紝鍙互鍦ㄨ繖閲屾坊鍔?
)

# 鍖呭惈鐩綍
target_include_directories(astockquantapp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/foundation/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/foundation/public
)

# 鍒涘缓鍙墽琛屾枃浠?
add_executable(astockquantapp-exe
    main.cpp
    Qml/qml.qrc
)

target_link_libraries(astockquantapp-exe
    PRIVATE
        astockquantapp
        domain_model
)

# 娴嬭瘯
include(CTest)
enable_testing()


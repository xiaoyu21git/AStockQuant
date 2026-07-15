cmake_minimum_required(VERSION 3.16)
project(AStockQuantApp VERSION 1.0.0 LANGUAGES CXX)

# 閸╃儤婀伴柊宥囩枂
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 閼奉亜濮╂径鍕倞 Qt 閻ㄥ嫬鍘撶€电钖勭化鑽ょ埠
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

# 閺屻儲澹?Qt6 find_package(Qt5 COMPONENTS GraphicalEffects REQUIRED) 
find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Charts
    Concurrent
    Network
    Widgets 
    Quick 
    Gui 
    Sql 
    Qml 
    Multimedia 
    QuickWidgets
)

# 鐠佸墽鐤?Qt 妞ゅ湱娲?
qt_standard_project_setup()

# 璁?Qt 宸ュ叿閾炬妸 ConsoleUi 妯″潡杈撳嚭鍒颁笌 URI 瀵归綈鐨勭洰褰曪紝閬垮厤 qmllint 鏃犳硶瑙ｆ瀽妯″潡銆?
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ConsoleUi")

# 濞ｈ濮炵€涙劗娲拌ぐ鏇礄閸嬪洩顔?src/ 娑擃厽婀?CMakeLists.txt閿?
#add_subdirectory(src)
add_subdirectory(adapters)

# 閸掓稑缂撴稉璇茬氨閻╊喗鐖?
add_library(astockquantapp SHARED
    src/AppBootstrap.cpp
    src/PythonEventBridge.cpp
    src/VasAurora.cpp
    facade/MarketDataFacade.cpp
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

# 鐠佸墽鐤嗘惔鎾舵畱鐏炵偞鈧?
target_compile_definitions(astockquantapp PRIVATE BUILD_DLL)

if(MSVC)
    target_compile_options(astockquantapp PRIVATE /FS)
endif()

# 闁剧偓甯存笟婵婄
target_link_libraries(astockquantapp
    PUBLIC
        Qt6::Core
        Qt6::Charts
        Qt6::Widgets
        Qt6::Quick
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Sql
        Qt6::Qml
        Qt6::QuickWidgets
        Python::Python
    PRIVATE
        engine
        domain
        domain_strategy_runtime
        bridge
        app_adapters
        foundation
        factor
)

if(TARGET Arrow::arrow_shared)
    add_custom_command(TARGET astockquantapp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Arrow::arrow_shared>
            $<TARGET_FILE_DIR:astockquantapp>
    )
elseif(TARGET arrow_shared)
    add_custom_command(TARGET astockquantapp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:arrow_shared>
            $<TARGET_FILE_DIR:astockquantapp>
    )
endif()

if(ENABLE_JUJIN_MARKET)
    target_link_libraries(astockquantapp
        PRIVATE
            thirdParty
    )

    # Python嵌入: 将CMake变量传递给C++
    target_compile_definitions(astockquantapp PRIVATE
        PYTHON_EXECUTABLE="${Python_EXECUTABLE}"
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

# 閸掓稑缂換ML濡€虫健
qt_add_qml_module(astockquantapp
    URI ConsoleUi
    VERSION 1.0
    OUTPUT_DIRECTORY "${QT_QML_OUTPUT_DIRECTORY}"
    DEPENDENCIES
        QtCharts
    IMPORT_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/qmllint"
        "${CMAKE_CURRENT_BINARY_DIR}"
    RESOURCE_PREFIX /
    QML_FILES
        Qml/main.qml
        Qml/dialogs/DepositDialog.qml
        Qml/dialogs/WithdrawDialog.qml
        Qml/dialogs/UserProfileDialog.qml
        Qml/page/Sidebar.qml
        Qml/page/dashboard/ChartArea.qml
        Qml/page/dashboard/MainContent.qml
        Qml/page/dashboard/MarketGrid.qml
        Qml/page/dashboard/PositionsPanel.qml
        Qml/page/dashboard/StatusSummaryCard.qml
        Qml/page/dashboard/StrategiesPanel.qml
        Qml/page/dashboard/OrderBook.qml
        Qml/page/monitoring/MonitoringPage.qml
        Qml/page/trading/TradingPage.qml
        Qml/page/settings/SystemSettingsPage.qml
        Qml/page/SettingsPage.qml
        Qml/page/leftSidebar/DynamicMenu.qml
        Qml/page/leftSidebar/FundOperationPanel.qml
        Qml/page/leftSidebar/MenuModel.qml
        Qml/page/leftSidebar/SecondaryMenu.qml
        Qml/page/leftSidebar/MenuTitle.qml
        Qml/page/leftSidebar/PrimaryMenu.qml
        Qml/page/strategies/StrategyLibraryPage.qml
        Qml/page/strategies/StrategyCreationPagePro.qml
        Qml/page/DataAnalysis/Datamain.qml
        Qml/page/DataAnalysis/CacheManagementPage.qml
            Qml/page/risk/RiskConfigurationPage.qml
        Qml/components/BaseQuantCard.qml
        Qml/components/FactorCard.qml
        Qml/components/StrategyCard.qml
        Qml/components/DatePicker.qml
        Qml/components/ThemeSwitcher.qml
        Qml/components/GlobalSnackbar.qml
        Qml/components/ButtonPrimary.qml
        Qml/components/ButtonSecondary.qml
        Qml/components/ButtonSmall.qml
        Qml/components/Backtest/BacktestLabeledDatePicker.qml
        Qml/components/Backtest/BacktestLabeledComboBox.qml
        Qml/components/Backtest/BacktestMetricCard.qml
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
        Qml/components/Trading/TradingChartWorkspace.qml
        Qml/components/Trading/TradingFormPanel.qml
        Qml/components/Trading/DepthMarketPanel.qml
        Qml/components/Trading/CandlestickChart.qml
        Qml/components/Trading/CrosshairIndicator.qml
        Qml/components/Trading/CandleTooltip.qml
        Qml/components/Trading/OrderFormSection.qml
        Qml/components/Trading/OrderActionBar.qml
        Qml/components/Trading/RuntimeRuleObserverPanel.qml
        Qml/components/Base/ActionButton.qml
        Qml/components/Base/ActionChip.qml
        Qml/components/Base/Constants.qml
        Qml/components/Base/MetaTable.qml
        Qml/components/Base/MetaForm.qml
        Qml/components/Base/MetaCard.qml
        Qml/components/UserInfoPanel.qml
        Qml/components/Strategy/ViewModeToggle.qml
        Qml/components/Strategy/StrategyBacktestParams.qml
        Qml/components/Strategy/StrategySortButton.qml
        Qml/components/Strategy/StrategyParameters.qml
        Qml/components/Strategy/StrategyFilterButton.qml
        Qml/components/Strategy/StrategyChart.qml
        Qml/components/Strategy/CreateStrategyDialog.qml
        Qml/components/Strategy/StrategyFilter.qml
        Qml/components/Strategy/StrategySorter.qml
        Qml/components/Strategy/StrategySummaryHeaderCard.qml
        Qml/components/Strategy/PerformanceHistoryList.qml
        Qml/page/strategies/BacktestResultAnalysisView.qml
        # 缁涙牜鏆愰崚娑樼紦缂佸嫪娆?
        Qml/components/Strategy/Creation/StrategyTypeSelector.qml
        Qml/components/Strategy/Creation/StrategyBasicInfo.qml
        Qml/components/Strategy/Creation/StrategyParamConfig.qml
        Qml/components/Strategy/Creation/RuleComposerSummaryBar.qml
        Qml/components/Strategy/Creation/RuleGroupCard.qml
        Qml/components/Strategy/Creation/RuleStageBoard.qml
        Qml/components/Strategy/Creation/RuleStageCard.qml
        Qml/components/Strategy/Creation/RuleStageNavigator.qml
        Qml/components/Strategy/Creation/RuleTemplateStructureView.qml
        Qml/components/Strategy/Creation/RuleTemplateSuggestionPanel.qml
        Qml/components/Strategy/Creation/StrategyProfilePanel.qml
        Qml/components/DataAnalysis/DataTable.qml
        Qml/components/DataAnalysis/ModuleCard.qml
        Qml/components/DataAnalysis/QuickStart.qml
        Qml/components/DataAnalysis/StatsCard.qml
        Qml/components/DataAnalysis/TaskCategory.qml
        Qml/components/DataAnalysis/WorkflowSteps.qml
        Qml/components/DataAnalysis/EmbeddedWorkflow.qml
        Qml/components/DataAnalysis/RulesConfigModal.qml
        Qml/components/DataAnalysis/RulePaginationBar.qml
        Qml/components/DataAnalysis/DataOverviewCard.qml
        Qml/components/DataAnalysis/DataSelectionPanel.qml
        Qml/components/DataAnalysis/DataPreviewPanel.qml
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
        Qml/page/FactorPerformancePage.qml
        # FactorWorkbench缂佸嫪娆㈤弬鍥︽
        Qml/components/FactorWorkbench/Library/FactorLibraryPage.qml
        Qml/components/FactorWorkbench/Navigation/ModeTitleBar.qml
        Qml/components/FactorWorkbench/Creation/FactorTypeCard.qml
        Qml/components/FactorWorkbench/Creation/CreationPagePluginIntegrated.qml
        # 閹绘帊娆㈤崠鏍矋娴?
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
        Qml/utils/StrategyCreationUtils.js
        Qml/utils/RuleTemplatePreviewUtils.js
        Qml/utils/I18n.js
        Qml/utils/StrategyStructureAdapter.js
        Qml/utils/RiskBacktestMetaLoader.js
        Qml/utils/StrategyDataAdapter.js
        Qml/utils/FactorDataAdapter.js
        Qml/utils/OrderUtils.js
        Qml/utils/TradingConstants.js
        Qml/utils/TradingWidgetBase.js


        Qml/utils/MarketEnvironmentProfile.js
        Qml/utils/StartupGateFormatter.js
        Qml/components/FactorWorkbench/Analysis/AnalysisPage.qml
        Qml/components/FactorWorkbench/Backtest/FactorAnalysisCharts.qml
        Qml/components/FactorWorkbench/Debug/DebugPage.qml
        Qml/components/FactorWorkbench/Backtest/BacktestConfigDialog.qml
        Qml/components/FactorWorkbench/Backtest/BacktestPage.qml
        Qml/components/FactorWorkbench/Backtest/BacktestResultView.qml
        Qml/components/FactorWorkbench/Backtest/FactorBacktestPage.qml
        Qml/components/FactorWorkbench/Backtest/FactorSelectorDialog.qml
        Qml/components/FactorWorkbench/Backtest/GroupConfigPanel.qml
        Qml/components/FactorWorkbench/Backtest/GroupResultChart.qml
        Qml/page/dynamicworkspace/DynamicWorkspacePage.qml
        Qml/page/dynamicworkspace/WidgetRegistry.qml
        Qml/components/DynamicWorkspace/WidgetCanvas.qml
        Qml/components/DynamicWorkspace/WidgetFrame.qml
        Qml/components/DynamicWorkspace/WidgetPalette.qml
        Qml/components/DynamicWorkspace/widgets/AccountCardWidget.qml
        Qml/components/DynamicWorkspace/widgets/CandlestickChartWidget.qml
        Qml/components/DynamicWorkspace/widgets/DepthPanelWidget.qml
        Qml/components/DynamicWorkspace/widgets/ExecutionLogWidget.qml
        Qml/components/DynamicWorkspace/widgets/OrderFormWidget.qml
        Qml/components/DynamicWorkspace/widgets/OrderListWidget.qml
        Qml/components/DynamicWorkspace/widgets/PositionListWidget.qml
        Qml/components/DynamicWorkspace/widgets/StrategyMonitorWidget.qml
    SOURCES
       # Qml/resources/
    RESOURCES
        Qml/components/Backtest/qmldir
        Qml/components/DataAnalysis/qmldir
        Qml/components/FactorWorkbench/Creation/components/qmldir
        Qml/components/DynamicWorkspace/qmldir
        Qml/components/Trading/qmldir
        Qml/page/dynamicworkspace/qmldir
        Qml/page/risk/qmldir
        Qml/page/settings/qmldir
        Qml/page/strategies/qmldir
        Qml/page/trading/qmldir

        # 婵″倹鐏夐張澶屾祲閸忓磭娈慍++缁紮绱濋崣顖欎簰閸︺劏绻栭柌灞惧潑閸?
)

if(MSVC AND TARGET astockquantappplugin)
    target_compile_options(astockquantappplugin PRIVATE /FS)
endif()

# 閸栧懎鎯堥惄顔肩秿
target_include_directories(astockquantapp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/facade/include
        ${CMAKE_CURRENT_SOURCE_DIR}/facade
        ${CMAKE_CURRENT_SOURCE_DIR}/foundation/thread
        ${CMAKE_CURRENT_SOURCE_DIR}/foundation/public
        ${CMAKE_SOURCE_DIR}/src/ui/bridge/include
        ${CMAKE_BINARY_DIR}/src/ui/bridge/bridge_autogen/include_Debug
)

# 閸掓稑缂撻崣顖涘⒔鐞涘本鏋冩禒?
add_executable(astockquantapp-exe
    main.cpp
    app_icon.rc
    Qml/qml.qrc
)

target_link_libraries(astockquantapp-exe
    PRIVATE
    astockquantapp
)

add_custom_command(TARGET astockquantapp-exe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:astockquantapp>
        $<TARGET_FILE_DIR:astockquantapp-exe>
)


# 濞村鐦?
include(CTest)
enable_testing()



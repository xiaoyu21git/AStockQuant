set(BRIDGE_SOURCES
    registerQmlTypes.cpp
    MetaData.cpp
    DataManager.cpp
    CleanedDataController.cpp
    DataCacheAdapter.cpp
    DataCleaningServiceRefactored.cpp
    PreviewDataModel.cpp
    DataFetchController.cpp
    FactorDebugController.cpp
    FactorMetaService.cpp
    FactorViewModel.cpp
    StrategyBridge.cpp
    FactorBacktestBridge.cpp
    StrategyBacktestBridge.cpp
    FactorService.cpp
    FactorDetectionService.cpp
    # factor_check 已迁移到 domain/factor_check/，编入 factor 库
    MarketDataBridge.cpp
    TradingBridges.cpp
    StrategyListModel.cpp
    TradingConnectionConfigService.cpp
    TradingRuntimeStatusService.cpp
    TradingMarketCalendarService.cpp
    RiskConfigService.cpp
    TradingFormPanelHelper.cpp
    UiLifecycleCoordinator.cpp
    StrategyPerformanceModel.cpp
    StockNameResolver.cpp
    SymbolSearchModel.cpp
    CandleDataModel.cpp
    RuleTemplateDetailHelper.cpp
    RuleTemplateSuggestionService.cpp
    CrosshairManager.cpp
)
list(TRANSFORM BRIDGE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
target_sources(bridge PRIVATE ${BRIDGE_SOURCES})
target_include_directories(bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/domain ${CMAKE_SOURCE_DIR}/src/domain/factor/include ${CMAKE_SOURCE_DIR}/src/domain/factor_check/include ${CMAKE_SOURCE_DIR}/src/domain/trading ${CMAKE_SOURCE_DIR}/src/domain/trading/include ${CMAKE_SOURCE_DIR}/src/cache/include ${CMAKE_SOURCE_DIR}/thirdparty/gmsdk ${EIGEN_INCLUDE_DIR})
set_target_properties(bridge PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC ON)
target_link_libraries(bridge PRIVATE Qt6::Core Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::Sql Qt6::Gui infrastructure engine foundation cache_module factor domain ${GMSDK_LIBRARY})
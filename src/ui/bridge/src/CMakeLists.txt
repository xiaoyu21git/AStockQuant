set(BRIDGE_SOURCES
    registerQmlTypes.cpp
    MetaData.cpp
    DataManager.cpp
    DataUIController.cpp
    CleanedDataController.cpp
    DataRuleService.cpp
    DataServiceCache.cpp
    DataCleaningServiceRefactored.cpp
    DataService.cpp
    CacheDetailPreviewModel.cpp
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
    factor_check/FactorSupportCheckCore.cpp
    factor_check/FactorDetectionCoreService.cpp
    factor_check/FactorSupportScopeCacheCore.cpp
    factor_check/FactorSupportScopeKeyCore.cpp
    cleaning/CleaningEngine.cpp
    MarketDataBridge.cpp
    TradingBridges.cpp
    DataCleaningPersistence.cpp
    StrategyListModel.cpp
    TradingConnectionConfigService.cpp
    TradingRuntimeStatusService.cpp
    TradingMarketCalendarService.cpp
    RiskConfigService.cpp
    TradingFormPanelHelper.cpp
    UiLifecycleCoordinator.cpp
    StrategyPerformanceModel.cpp
    SymbolSearchModel.cpp
    JujinApi.cpp
    GmStrategySession.cpp
    TradingCommandQueue.cpp
    TradingRuntimeManager.cpp
)
list(TRANSFORM BRIDGE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
target_sources(bridge PRIVATE ${BRIDGE_SOURCES})
target_include_directories(bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/domain ${CMAKE_SOURCE_DIR}/src/domain/factor/include ${CMAKE_SOURCE_DIR}/src/domain/trading ${CMAKE_SOURCE_DIR}/src/domain/trading/include ${CMAKE_SOURCE_DIR}/src/cache/include ${CMAKE_SOURCE_DIR}/thirdparty/gmsdk ${EIGEN_INCLUDE_DIR})
set_target_properties(bridge PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC ON)
target_link_libraries(bridge PRIVATE Qt6::Core Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::Sql Qt6::Gui infrastructure engine foundation cache_module factor domain ${GMSDK_LIBRARY})
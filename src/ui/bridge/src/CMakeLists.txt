set(BRIDGE_SOURCES
    registerQmlTypes.cpp
    OrderRuntimeUtils.cpp
    MetaData.cpp
    DataManager.cpp
    DataUIController.cpp
    RuleTemplateRuntimeEvaluator.cpp
    ExecutionSchedulingRuleChain.cpp
    CleanedDataController.cpp
    RuleTemplateDetailHelper.cpp
    RuleTemplateSuggestionService.cpp
    DataRuleService.cpp
    DataServiceCache.cpp
    TradingFormPanelHelper.cpp
    RealtimeDataMonitor.cpp
    TradingConnectionConfigService.cpp
    TradingMarketCalendarService.cpp
    TradingRuntimeStatusService.cpp
    RiskConfigService.cpp
    TradeExecutionBridge.cpp
    FactorDetectionService.cpp
    factor_check/FactorSupportCheckCore.cpp
    factor_check/FactorDetectionCoreService.cpp
    factor_check/FactorSupportScopeCacheCore.cpp
    factor_check/FactorSupportScopeKeyCore.cpp
    cleaning/CleaningEngine.cpp
    MarketDataBridge.cpp
)
if(ENABLE_JUJIN_MARKET)
    list(APPEND BRIDGE_SOURCES JujinApi.cpp TradingCommandQueue.cpp GmStrategySession.cpp TradingRuntimeManager.cpp)
endif()
list(TRANSFORM BRIDGE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
target_sources(bridge PRIVATE ${BRIDGE_SOURCES})
target_include_directories(bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/domain ${CMAKE_SOURCE_DIR}/src/domain/factor/include ${CMAKE_SOURCE_DIR}/src/domain/trading ${CMAKE_SOURCE_DIR}/src/domain/trading/include ${CMAKE_SOURCE_DIR}/src/cache/include ${EIGEN_INCLUDE_DIR})
set_target_properties(bridge PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC ON)
target_link_libraries(bridge PRIVATE Qt6::Core Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::Sql Qt6::Gui infrastructure engine foundation cache_module factor domain)
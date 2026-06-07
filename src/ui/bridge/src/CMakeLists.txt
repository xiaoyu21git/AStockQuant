# bridge库的源文件 - 精简版本
# 只包含实际需要的文件，避免链接错误

# 核心模型文件 - 精简版，移除旧数据服务组件
set(BRIDGE_SOURCES
    ../include/RuleTemplateSuggestionService.h
    ../include/RuleTemplateDetailHelper.h
    ../include/FactorService.h
    ../include/FactorBacktestBridge.h
    ../include/StrategyBacktestBridge.h
    ../include/CachedMarketDataView.h
        CleanedDataController.cpp
    cleaning/CleaningEngine.cpp
    DataManager.cpp
    DataUIController.cpp
    DataService.cpp
    DataRuleService.cpp
    DataServiceCache.cpp  
    CacheDetailPreviewModel.cpp
    PreviewDataModel.cpp
    registerQmlTypes.cpp
    DataFetchController.cpp
    MetaData.cpp
    ExecutionSchedulingRuleChain.cpp
    FactorViewModel.cpp
    FactorDebugController.cpp
    FactorDetectionService.cpp
    factor_check/FactorSupportCheckCore.cpp
    factor_check/FactorDetectionCoreService.cpp
    factor_check/FactorSupportScopeCacheCore.cpp
    factor_check/FactorSupportScopeKeyCore.cpp
    FactorMetaService.cpp
    MarketDataService.cpp
    OrderRuntimeUtils.cpp
    RuleTemplateDetailHelper.cpp
    RuleTemplateRuntimeEvaluator.cpp
    RiskConfigService.cpp
    RiskMonitorService.cpp
    PositionAccountService.cpp
    TradeExecutionService.cpp
    TradingConnectionConfigService.cpp
    TradingMarketCalendarService.cpp
    TradingRuntimeStatusService.cpp
    UiLifecycleCoordinator.cpp
    RuleTemplateSuggestionService.cpp
        StrategyBridge.cpp
        StrategyListModel.cpp
        StrategyStructureResolvers.cpp
        FactorService.cpp
        FactorBacktestBridge.cpp
        StrategyBacktestBridge.cpp
        CachedMarketDataView.cpp
        CachedMarketDataViewHistoricalAdapter.cpp
)

if(ENABLE_JUJIN_MARKET)
    list(APPEND BRIDGE_SOURCES
        JujinApi.cpp
        TradingCommandQueue.cpp
        GmStrategySession.cpp
        TradingRuntimeManager.cpp
    )
endif()

list(TRANSFORM BRIDGE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

target_sources(bridge PRIVATE ${BRIDGE_SOURCES})

target_include_directories(bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/src/domain/factor/include
    ${CMAKE_SOURCE_DIR}/src/domain/factor/include/factor_compute
    ${CMAKE_SOURCE_DIR}/src/domain/backtest/include
    ${CMAKE_SOURCE_DIR}/src/domain/strategy/include
    ${CMAKE_SOURCE_DIR}/src/application/backtest/include
    ${CMAKE_SOURCE_DIR}/src/cache/include
    ${EIGEN_INCLUDE_DIR}
)

set_target_properties(bridge PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
)

target_link_libraries(bridge PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    Qt6::Sql
    Qt6::Gui
)

target_link_libraries(bridge PRIVATE
    infrastructure
    engine
    foundation
    cache_module
    factor_system
    factor_compute
    domain
    application_backtest
)

message(STATUS "bridge库源文件: ${BRIDGE_SOURCES}")
message(STATUS "bridge库包含目录: ${CMAKE_CURRENT_SOURCE_DIR}/../include")
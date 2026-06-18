# bridge库的CMake配置 - 精简版本

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS 
    Core Widgets Quick Gui Sql Qml Multimedia QuickWidgets Concurrent
)
find_package(yaml-cpp CONFIG REQUIRED)

# 精简头文件列表 — 只加对 MOC 生成有必要的 Q_OBJECT 类型
# 包含：原始 cleaning 头文件 + 原始 7 个基础头文件
#        + registerQmlTypes.cpp 中注册的 13 个类型
# 注意：排除 DataServiceCache.h, DataCleaningPersistence.h 等不在 QML 注册列表中的，
#       因其信号使用了自定义 struct 可能导致 Qt 运行时断言崩溃
set(BRIDGE_HEADERS
    include/cleaning/CleaningRuleContract.h
    include/cleaning/CleaningEngine.h
    include/cleaning/rules/CompletenessRule.h
    include/cleaning/rules/DuplicateRemovalRule.h
    include/cleaning/rules/FinancialDateValidityRule.h
    include/cleaning/rules/FinancialMetricSanitizeRule.h
    include/cleaning/rules/PriceValidityRule.h
    include/cleaning/rules/VolumeFilterRule.h
    include/cleaning/rules/ValuationSanitizeRule.h
    include/cleaning/rules/AdjustedPriceRule.h
    include/cleaning/rules/SuspensionFillRule.h
    include/cleaning/rules/MissingValueFillRule.h
    include/cleaning/rules/NewStockFilterRule.h
    include/cleaning/rules/STFilterRule.h
    include/cleaning/rules/SurvivorBiasRule.h
    include/cleaning/rules/ReportDateAlignmentRule.h
    include/cleaning/rules/LimitMoveTagRule.h
    include/cleaning/rules/FieldStandardizationRule.h
    include/field_traits.h
    include/MarketDataBridge.h
    include/TradingBridges.h
    include/CleanedDataController.h
    include/DataManager.h
    include/DataRuleService.h
    # ── 仅 registerQmlTypes.cpp 中注册的类型 ──
    include/DataService.h
    include/DataCleaningServiceRefactored.h
    include/CacheDetailPreviewModel.h
    include/PreviewDataModel.h
    include/DataFetchController.h
    include/FactorViewModel.h
    include/FactorDebugController.h
    include/FactorMetaService.h
    include/FactorService.h
    include/FactorBacktestBridge.h
    include/StrategyBridge.h
    include/StrategyBacktestBridge.h
    include/StrategyListModel.h
    include/DataServiceCache.h
    # ── 新增桥接类型 (TradingConnectionConfig 等) ──
    include/TradingConnectionConfigService.h
    include/TradingRuntimeStatusService.h
    include/TradingMarketCalendarService.h
    include/RiskConfigService.h
    include/TradingFormPanelHelper.h
    include/UiLifecycleCoordinator.h
    include/StrategyPerformanceModel.h
    include/SymbolSearchModel.h
    include/RuleTemplateDetailHelper.h
)

add_library(bridge STATIC
    ${BRIDGE_HEADERS}
)

add_subdirectory(src)

target_compile_definitions(bridge PRIVATE
    BUILD_DLL
    BRIDGE_LIBRARY
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(bridge PRIVATE /FS)
endif()

if(ENABLE_JUJIN_MARKET)
    target_compile_definitions(bridge PRIVATE ASTOCK_ENABLE_JUJIN_MARKET=1)
endif()

target_link_libraries(bridge
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Quick
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Sql
        Qt6::Qml
        Qt6::QuickWidgets
        Qt6::Concurrent
    PRIVATE
        engine
        domain
        domain_strategies
        domain_strategy_runtime
        astock_market
        infrastructure
        yaml-cpp
)

if(ENABLE_JUJIN_MARKET)
    target_link_libraries(bridge PRIVATE thirdParty)
endif()

target_include_directories(bridge
    PUBLIC
        include
    PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/src/domain/cleaning/include
)
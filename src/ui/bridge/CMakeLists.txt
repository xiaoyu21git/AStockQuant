# bridge库的CMake配置 - 精简版本

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS 
    Core Widgets Quick Gui Sql Qml Multimedia QuickWidgets Concurrent
)
find_package(yaml-cpp CONFIG REQUIRED)

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
    include/TradingConnectionConfigService.h
    include/TradingMarketCalendarService.h
    include/TradingRuntimeStatusService.h
    include/RuleTemplateDetailHelper.h
    include/RuleTemplateSuggestionService.h
    include/CleanedDataController.h
    include/TradeExecutionBridge.h
    include/DataManager.h
    include/RiskConfigService.h
    include/DataRuleService.h
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
)
file(GLOB DOMAIN_BACKTEST_SOURCES_CXX "*.cxx")

target_sources(domain_backtest PRIVATE
    FactorAnalyticsTypes.cpp
    FactorGrouper.cpp
    BacktestLayerGuard.cpp
    BacktestWindowBuilder.cpp
    DynamicUniverseBuilder.cpp
    RebalanceScheduleBuilder.cpp
    GroupingAllocator.cpp
    ValuationEngine.cpp
    SurvivorshipValidator.cpp
    ${DOMAIN_BACKTEST_SOURCES_CXX}
)

# 璁剧疆C++鏍囧噯
set_target_properties(domain_backtest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# 娣诲姞鍖呭惈鐩綍
target_include_directories(domain_backtest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)




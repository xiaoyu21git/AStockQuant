file(GLOB DOMAIN_BACKTEST_SOURCES_CXX "*.cxx")

target_sources(domain_backtest PRIVATE
    BacktestRuleTemplateEvaluator.cpp
    BacktestEngine.cpp
    FactorGrouper.cpp
    GroupBacktester.cpp
    ICIRCalculator.cpp
    FactorBacktestService.cpp
    StrategyBacktestService.cpp
    FactorBacktestTypes.cpp
    DatabaseFactorDataProvider.cpp
    DatabaseStockDataProvider.cpp
    ${DOMAIN_BACKTEST_SOURCES_CXX}
)

# 设置C++标准
set_target_properties(domain_backtest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# 添加包含目录
target_include_directories(domain_backtest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

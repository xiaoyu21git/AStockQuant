file(GLOB DOMAIN_BACKTEST_SOURCES_CXX "*.cxx")

target_sources(domain_backtest PRIVATE
    FactorGrouper.cpp
    ICIRCalculator.cpp
    FactorBacktestTypes.cpp
    DatabaseFactorDataProvider.cpp
    DatabaseStockDataProvider.cpp
    BacktestLayerGuard.cpp
    BacktestWindowAbstraction.cpp
    DynamicUniverseAbstraction.cpp
    RebalanceScheduleAbstraction.cpp
    GroupingAllocationAbstraction.cpp
    PositionConstraintAllocationAbstraction.cpp
    PositionStateMachineAbstraction.cpp
    TradingCostModelAbstraction.cpp
    SignalOrderTranslatorAbstraction.cpp
    RiskApprovalEngineAbstraction.cpp
    OrderRoutingEngineAbstraction.cpp
    PortfolioValuationEngineAbstraction.cpp
    PerformanceMetricsAggregatorAbstraction.cpp
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

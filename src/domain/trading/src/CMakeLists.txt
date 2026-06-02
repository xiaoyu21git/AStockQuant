target_sources(domain_trading PRIVATE
    DefaultTradingPlanner.cpp
    InMemoryTradingLedger.cpp
    DefaultRiskRuleChain.cpp
    BacktestExecutionVenue.cpp
    DefaultTradingCore.cpp
    execution/OrderRouter.cpp
    execution/SignalOrderTranslator.cpp
    execution/RiskApprovalEngine.cpp
    execution/PositionConstraintAllocator.cpp
    execution/PositionStateMachine.cpp
    execution/TradingCostModel.cpp
)

target_include_directories(domain_trading PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/execution
)
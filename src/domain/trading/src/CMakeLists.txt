target_sources(domain_trading PRIVATE
    DefaultTradingPlanner.cpp
    InMemoryTradingLedger.cpp
    DefaultRiskRuleChain.cpp
    BacktestExecutionVenue.cpp
    DefaultTradingCore.cpp
)

target_include_directories(domain_trading PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
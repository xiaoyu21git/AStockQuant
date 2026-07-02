# domain/trading — 券商网关抽象层 (纯 C++, 零 Qt)
cmake_minimum_required(VERSION 3.15)

add_library(domain_trading STATIC
    TradingTypes.cpp
    TradeExecutionEngine.cpp
    PnlCalculator.cpp
    src/OrderBuilder.cpp
    src/OrderConflictDetector.cpp
    src/ExecutionCheckpointManager.cpp
    src/ExecutionSchedulingEngine.cpp
    src/OrderSubmissionPipeline.cpp
)

target_include_directories(domain_trading
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/engine/include
        ${CMAKE_SOURCE_DIR}/src/domain
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(domain_trading
    PUBLIC
        foundation
        engine
        domain_strategy_runtime
)

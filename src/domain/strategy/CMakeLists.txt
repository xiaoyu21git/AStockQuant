add_subdirectory(rules)

add_library(domain_strategy_runtime STATIC)

target_sources(domain_strategy_runtime PRIVATE
    src/RuntimeFactorSvc.cpp
    src/MultiFactorStrategy.cpp
    src/StrategyServiceContracts.cpp
    src/StrategySnapshotTypes.cpp
    src/StrategyPseudoServices.cpp
    src/StrategyPythonRuleService.cpp
    src/StrategyServer.cpp
    src/StrategyEngineFacade.cpp
    src/DailyEodScheduler.cpp
    src/StrategyManager.cpp
    src/RiskEvaluator.cpp
    src/EventRiskSubscriber.cpp
    src/MarketTimingGate.cpp
    src/TimedCircuitBreaker.cpp
    src/FactorSignalProcessor.cpp
    src/SignalBlendCompositor.cpp
    src/NonFactorStrategy.cpp
    src/OrderGenerator.cpp
    src/RiskManager.cpp
    src/RiskControl.cpp
    src/StrategyFactory.cpp
    src/StrategyLifecycleManager.cpp
    src/RulePipeline.cpp
)

target_include_directories(domain_strategy_runtime
    PUBLIC
        include
        rules
    PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/src/domain/factor/include
        ${CMAKE_SOURCE_DIR}/src/domain/trading
        ${CMAKE_SOURCE_DIR}/src/domain/trading/include
        ${CMAKE_SOURCE_DIR}/src/domain/market/include
)

target_link_libraries(domain_strategy_runtime
    PUBLIC
        foundation
        astock_rule_engine
    PRIVATE
        factor
        domain_strategies
        astock_market
)

if (MSVC)
    # /utf-8 和 /Zc:__cplusplus 已在根 CMakeLists.txt 全局设置
    target_compile_options(domain_strategy_runtime PRIVATE /permissive-)
endif()

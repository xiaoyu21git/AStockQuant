add_library(domain_strategy_runtime STATIC)

target_sources(domain_strategy_runtime PRIVATE
    src/RuntimeFactorSvc.cpp
    src/RuntimeStrategyFactory.cpp
    src/StrategyServiceContracts.cpp
    src/StrategySnapshotTypes.cpp
    src/StrategyPseudoServices.cpp
    src/StrategyRealFactorAdapter.cpp
    src/StrategyPythonRuleService.cpp
    src/StrategyServer.cpp
    src/StrategyEngineFacade.cpp
    src/StrategyManager.cpp
    src/RiskEvaluator.cpp
    src/RiskControl.cpp
    src/StrategyFactory.cpp
    src/StrategyLifecycleManager.cpp
)

target_include_directories(domain_strategy_runtime
    PUBLIC
        include
    PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/src/domain/factor/include
        ${CMAKE_SOURCE_DIR}/src/domain/trading
        ${CMAKE_SOURCE_DIR}/src/domain/trading/include
)

target_link_libraries(domain_strategy_runtime
    PUBLIC
        foundation
    PRIVATE
        factor
        domain_strategies
)

if (MSVC)
    target_compile_options(domain_strategy_runtime PRIVATE /utf-8 /Zc:__cplusplus /permissive-)
endif()

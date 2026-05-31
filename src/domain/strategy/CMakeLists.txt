add_library(domain_strategy_runtime STATIC)

target_sources(domain_strategy_runtime PRIVATE
    src/RuntimeStrategyFactory.cpp
    src/StrategyServiceContracts.cpp
    src/StrategySnapshotTypes.cpp
    src/StrategyPseudoServices.cpp
    src/StrategyRealFactorAdapter.cpp
    src/StrategyPythonRuleService.cpp
    src/StrategyServer.cpp
    src/StrategyEngineFacade.cpp
)

target_include_directories(domain_strategy_runtime
    PUBLIC
        include
    PRIVATE
        src
)

target_link_libraries(domain_strategy_runtime
    PUBLIC
        foundation
    PRIVATE
        domain_strategies
)

if (MSVC)
    target_compile_options(domain_strategy_runtime PRIVATE /utf-8 /Zc:__cplusplus /permissive-)
endif()

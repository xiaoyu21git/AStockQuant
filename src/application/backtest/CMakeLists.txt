add_library(application_backtest STATIC)

target_sources(application_backtest
    PRIVATE
        src/BacktestApplicationService.cpp
        src/BacktestApplicationTypeAnchors.cpp
        src/CanonicalBacktestRequestFactory.cpp
        src/StrategyBacktestEntryService.cpp
        src/StrategyBacktestEngineGateway.cpp
        src/StrategyBacktestRuntimeHost.cpp
        src/ThreadedAsyncBacktestScheduler.cpp
)

target_include_directories(application_backtest
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(application_backtest
    PUBLIC
        domain_backtest
        foundation
)

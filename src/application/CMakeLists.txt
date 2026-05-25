add_subdirectory(strategy)
add_subdirectory(backtest)
add_subdirectory(runtime)
add_subdirectory(trading)

add_library(application_contracts INTERFACE)

target_link_libraries(application_contracts
    INTERFACE
        application_strategy
        application_backtest
        application_runtime
        application_trading
)
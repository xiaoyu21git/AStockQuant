add_subdirectory(backtest)
add_subdirectory(trading)

add_library(application_contracts INTERFACE)

target_link_libraries(application_contracts
    INTERFACE
    application_backtest
        application_trading
)
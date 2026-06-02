add_subdirectory(trading)
add_subdirectory(backtest)

add_library(application_contracts INTERFACE)

target_link_libraries(application_contracts
    INTERFACE
        application_trading
    application_backtest
)
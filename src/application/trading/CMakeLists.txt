add_library(application_trading STATIC)
add_subdirectory(src)

target_include_directories(application_trading
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(application_trading
    PUBLIC
        domain_trading
        domain_backtest
        factor_system
)

if(TARGET Qt6::Core)
    target_link_libraries(application_trading PUBLIC Qt6::Core)
endif()
add_library(application_backtest STATIC)
add_subdirectory(src)

target_include_directories(application_backtest
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(application_backtest
    PUBLIC
        domain_backtest
        factor_system
)

if(TARGET Qt6::Core)
    target_link_libraries(application_backtest PUBLIC Qt6::Core)
endif()

set_target_properties(application_backtest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
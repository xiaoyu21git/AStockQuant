# domain/CMakeLists.txt
add_subdirectory(types)
# add_subdirectory(indicators) -- removed (dead code, depended on deleted Bar.h)
# add_subdirectory(signals)   -- removed (dead code, depended on deleted Bar.h)
add_subdirectory(strategies)
add_subdirectory(data)
add_subdirectory(market)
add_subdirectory(backtest)
add_subdirectory(factor)
add_subdirectory(strategy)
add_subdirectory(scheduler)
add_subdirectory(trading)

    # indicators/signals removed — astock_ta_lib link removed

set(DOMAIN_DUMMY_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/domain_dummy.cpp")

add_library(domain STATIC
    ${DOMAIN_DUMMY_SOURCE}
)

if(NOT EXISTS "${DOMAIN_DUMMY_SOURCE}")
    file(WRITE "${DOMAIN_DUMMY_SOURCE}" "// Dummy source file for domain library\n")
endif()

target_link_libraries(domain
    PUBLIC
        engine
        foundation
        domain_types
        domain_strategies
        domain_data
        domain_backtest
        domain_scheduler
        domain_strategy_runtime
        factor
        domain_trading
)

target_include_directories(domain
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/types/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/indicators/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/signals/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/strategies/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/data/include>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backtest/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/market/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/factor/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/strategy/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/trading>
        $<INSTALL_INTERFACE:include>
)

set(LIBS_ROOT "C:/libs")


# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

message(STATUS "[TESTS] Configuring unit tests in ${CMAKE_CURRENT_SOURCE_DIR}")

# 设置测试输出目录
set(TEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/tests)
file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})

set(GTEST_SOURCE_DIR "G:/C++/lib/googletest")
if(NOT EXISTS "${GTEST_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "[TESTS] Local googletest source not found at ${GTEST_SOURCE_DIR}")
endif()

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(${GTEST_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_deps/googletest-build EXCLUDE_FROM_ALL)

function(copy_gtest_runtime_dlls target_name)
    if(TARGET gtest)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:gtest>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()
    if(TARGET gtest_main)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:gtest_main>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()
    if(TARGET arrow_shared)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:arrow_shared>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()
endfunction()

function(copy_benchmark_runtime_dlls target_name)
    if(TARGET hiredis)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:hiredis>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()
    if(TARGET Qt6::Core)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::Core>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()
    if(TARGET Qt6::Sql)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::Sql>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()
    if(TARGET arrow_shared)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:arrow_shared>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()
endfunction()

function(copy_benchmark_qt_sql_plugins target_name)
    if(NOT TARGET Qt6::Core)
        return()
    endif()

    set(qt_sql_plugin_sources)

    get_target_property(qt_core_debug_location Qt6::Core IMPORTED_LOCATION_DEBUG)
    if(qt_core_debug_location)
        get_filename_component(qt_core_debug_bin_dir "${qt_core_debug_location}" DIRECTORY)
        list(APPEND qt_sql_plugin_sources
            "${qt_core_debug_bin_dir}/../plugins/sqldrivers/qsqlmysqld.dll")
    endif()

    get_target_property(qt_core_release_location Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(qt_core_release_location)
        get_filename_component(qt_core_release_bin_dir "${qt_core_release_location}" DIRECTORY)
        list(APPEND qt_sql_plugin_sources
            "${qt_core_release_bin_dir}/../plugins/sqldrivers/qsqlmysql.dll")
    endif()

    list(REMOVE_DUPLICATES qt_sql_plugin_sources)

    foreach(qt_sql_plugin_source IN LISTS qt_sql_plugin_sources)
        if(EXISTS "${qt_sql_plugin_source}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${target_name}>/plugins/sqldrivers"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${qt_sql_plugin_source}"
                    "$<TARGET_FILE_DIR:${target_name}>/plugins/sqldrivers"
            )
        endif()
    endforeach()
endfunction()

# ==================== 检查测试源文件是否存在 ====================
set(TEST_SOURCES "")
set(LIBS_ROOT "C:/libs")

# 检查 test_eventsystem.cpp
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_eventsystem.cpp")
    list(APPEND TEST_SOURCES "test_eventsystem.cpp")
    message(STATUS "[TESTS] Found: test_eventsystem.cpp")
else()
    message(WARNING "[TESTS] test_eventsystem.cpp not found!")
endif()

# 检查新增测试文件
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_eventbus_cpp_units.cpp")
    list(APPEND TEST_SOURCES "test_eventbus_cpp_units.cpp")
    message(STATUS "[TESTS] Found: test_eventbus_cpp_units.cpp")
else()
    message(WARNING "[TESTS] test_eventbus_cpp_units.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_event_domain_integration.cpp")
    list(APPEND TEST_SOURCES "test_event_domain_integration.cpp")
    message(STATUS "[TESTS] Found: test_event_domain_integration.cpp")
else()
    message(WARNING "[TESTS] test_event_domain_integration.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_event_stress.cpp")
    list(APPEND TEST_SOURCES "test_event_stress.cpp")
    message(STATUS "[TESTS] Found: test_event_stress.cpp")
else()
    message(WARNING "[TESTS] test_event_stress.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_factor_custom_expression.cpp")
    list(APPEND TEST_SOURCES "test_factor_custom_expression.cpp")
    message(STATUS "[TESTS] Found: test_factor_custom_expression.cpp")
else()
    message(WARNING "[TESTS] test_factor_custom_expression.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_factor_backtest_regression.cpp")
    list(APPEND TEST_SOURCES "test_factor_backtest_regression.cpp")
    message(STATUS "[TESTS] Found: test_factor_backtest_regression.cpp")
else()
    message(WARNING "[TESTS] test_factor_backtest_regression.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_factor_crosscheck_export.cpp")
    list(APPEND TEST_SOURCES "test_factor_crosscheck_export.cpp")
    message(STATUS "[TESTS] Found: test_factor_crosscheck_export.cpp")
else()
    message(WARNING "[TESTS] test_factor_crosscheck_export.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data_service_regression.cpp")
    list(APPEND TEST_SOURCES "test_data_service_regression.cpp")
    message(STATUS "[TESTS] Found: test_data_service_regression.cpp")
else()
    message(WARNING "[TESTS] test_data_service_regression.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_strategy_backtest_regression.cpp")
    list(APPEND TEST_SOURCES "test_strategy_backtest_regression.cpp")
    message(STATUS "[TESTS] Found: test_strategy_backtest_regression.cpp")
else()
    message(WARNING "[TESTS] test_strategy_backtest_regression.cpp not found!")
endif()

if(NOT TEST_SOURCES)
    message(FATAL_ERROR "[TESTS] No test source files found!")
endif()

message(STATUS "[TESTS] Test sources: ${TEST_SOURCES}")

# ==================== 创建测试可执行文件 ====================
add_executable(test_eventsystem ${TEST_SOURCES})
set_target_properties(test_eventsystem PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL"
)
if(MSVC)
    target_compile_options(test_eventsystem PRIVATE /FS)
endif()
target_include_directories(test_eventsystem PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine/include     # Event.h 等引擎头文件
    ${CMAKE_SOURCE_DIR}/src/domain/include     # Domain 模块头文件
    ${CMAKE_SOURCE_DIR}/src/foundation/include # Foundation 头文件
    ${CMAKE_SOURCE_DIR}/astock_engine          # EventSystem.hpp 等
    ${CMAKE_SOURCE_DIR}/src                    # 其他头文件
)

# 链接库
target_link_libraries(test_eventsystem
    PUBLIC
        GTest::gtest
        GTest::gtest_main
    PRIVATE
        Qt6::Core
        engine          # Engine 库（包含 Event）
        domain          # Domain 库
        bridge          # StrategyBacktestController 回归测试
        factor_system   # ConfigurableFactor/Factor runtime symbols
        infrastructure  # MarketDataRepository 回归测试
        foundation      # Foundation 库
)

# 设置输出目录
set_target_properties(test_eventsystem PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${TEST_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${TEST_OUTPUT_DIR}
)

if(DEFINED GMSDK_LIBRARY AND EXISTS "${GMSDK_LIBRARY}")
    get_filename_component(GMSDK_RUNTIME_DIR "${GMSDK_LIBRARY}" DIRECTORY)
    foreach(GMSDK_RUNTIME_FILE gmsdk.dll thostmduserapi_se.dll thostmd_wrap.dll)
        if(EXISTS "${GMSDK_RUNTIME_DIR}/${GMSDK_RUNTIME_FILE}")
            add_custom_command(TARGET test_eventsystem POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GMSDK_RUNTIME_DIR}/${GMSDK_RUNTIME_FILE}"
                    "$<TARGET_FILE_DIR:test_eventsystem>/${GMSDK_RUNTIME_FILE}"
            )
        endif()
    endforeach()
endif()

# ==================== 添加运行目标 ====================
add_custom_target(run_event_tests
    COMMAND ${TEST_OUTPUT_DIR}/test_eventsystem
    DEPENDS test_eventsystem
    COMMENT "Running EventSystem tests..."
    VERBATIM
)

# 添加到CTest
enable_testing()
add_test(NAME EventSystemTest
    COMMAND ${TEST_OUTPUT_DIR}/test_eventsystem
    WORKING_DIRECTORY ${TEST_OUTPUT_DIR}
)

message(STATUS "[TESTS] Configured test_eventsystem executable")
message(STATUS "[TESTS] Test output: ${TEST_OUTPUT_DIR}/test_eventsystem")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_trading_strategy_binding_regression.cpp")
    message(STATUS "[TESTS] Found: test_trading_strategy_binding_regression.cpp")

    add_executable(test_trading_strategy_binding_regression
        test_trading_strategy_binding_regression.cpp
    )

copy_gtest_runtime_dlls(test_eventsystem)
copy_benchmark_runtime_dlls(test_eventsystem)
copy_benchmark_qt_sql_plugins(test_eventsystem)

    set_target_properties(test_trading_strategy_binding_regression PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL"
    )

    target_include_directories(test_trading_strategy_binding_regression
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/engine/include
            ${CMAKE_SOURCE_DIR}/src/domain/include
            ${CMAKE_SOURCE_DIR}/src/foundation/include
            ${CMAKE_SOURCE_DIR}/src/ui/bridge/include
            ${CMAKE_SOURCE_DIR}/astock_engine
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(test_trading_strategy_binding_regression
        PRIVATE
            GTest::gtest
            engine
            domain
            bridge
            foundation
    )

    copy_gtest_runtime_dlls(test_trading_strategy_binding_regression)

    set_target_properties(test_trading_strategy_binding_regression PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${TEST_OUTPUT_DIR}
    )

    add_custom_target(run_trading_binding_tests
        COMMAND ${TEST_OUTPUT_DIR}/test_trading_strategy_binding_regression
        DEPENDS test_trading_strategy_binding_regression
        COMMENT "Running trading strategy binding regression tests..."
        VERBATIM
    )

    add_test(NAME TradingStrategyBindingRegressionTest
        COMMAND ${TEST_OUTPUT_DIR}/test_trading_strategy_binding_regression
        WORKING_DIRECTORY ${TEST_OUTPUT_DIR}
    )

    message(STATUS "[TESTS] Configured test_trading_strategy_binding_regression executable")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/run_strategy_backtest_once.cpp")
    message(STATUS "[TESTS] Found: run_strategy_backtest_once.cpp")

    add_executable(run_strategy_backtest_once
        run_strategy_backtest_once.cpp
    )

    set_target_properties(run_strategy_backtest_once PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL"
    )

    target_include_directories(run_strategy_backtest_once
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/engine/include
            ${CMAKE_SOURCE_DIR}/src/domain/include
            ${CMAKE_SOURCE_DIR}/src/foundation/include
            ${CMAKE_SOURCE_DIR}/src/infrastructure/include
            ${CMAKE_SOURCE_DIR}/src/ui/bridge/include
            ${CMAKE_SOURCE_DIR}/astock_engine
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(run_strategy_backtest_once
        PRIVATE
            engine
            domain
            bridge
            infrastructure
            foundation
    )

    set_target_properties(run_strategy_backtest_once PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${TEST_OUTPUT_DIR}
    )

    message(STATUS "[TESTS] Configured run_strategy_backtest_once executable")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/run_factor_full_market_benchmark.cpp")
    message(STATUS "[TESTS] Found: run_factor_full_market_benchmark.cpp")

    add_executable(run_factor_full_market_benchmark
        run_factor_full_market_benchmark.cpp
    )

    target_include_directories(run_factor_full_market_benchmark
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/engine/include
            ${CMAKE_SOURCE_DIR}/src/domain/include
            ${CMAKE_SOURCE_DIR}/src/foundation/include
            ${CMAKE_SOURCE_DIR}/src/infrastructure/include
            ${CMAKE_SOURCE_DIR}/src/ui/bridge/include
            ${CMAKE_SOURCE_DIR}/astock_engine
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(run_factor_full_market_benchmark
        PRIVATE
            Qt6::Core
            Qt6::Sql
            cache_module
            foundation
            infrastructure
            factor_system
    )

    set_target_properties(run_factor_full_market_benchmark PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${TEST_OUTPUT_DIR}
    )

    copy_benchmark_runtime_dlls(run_factor_full_market_benchmark)
    copy_benchmark_qt_sql_plugins(run_factor_full_market_benchmark)

    message(STATUS "[TESTS] Configured run_factor_full_market_benchmark executable")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/run_cleaning_cache_probe.cpp")
    message(STATUS "[TESTS] Found: run_cleaning_cache_probe.cpp")

    add_executable(run_cleaning_cache_probe
        run_cleaning_cache_probe.cpp
    )

    set_target_properties(run_cleaning_cache_probe PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL"
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${TEST_OUTPUT_DIR}
    )

    target_include_directories(run_cleaning_cache_probe
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/engine/include
            ${CMAKE_SOURCE_DIR}/src/domain/include
            ${CMAKE_SOURCE_DIR}/src/foundation/include
            ${CMAKE_SOURCE_DIR}/src/infrastructure/include
            ${CMAKE_SOURCE_DIR}/src/ui/bridge/include
            ${CMAKE_SOURCE_DIR}/astock_engine
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(run_cleaning_cache_probe
        PRIVATE
            bridge
            foundation
            infrastructure
    )

    message(STATUS "[TESTS] Configured run_cleaning_cache_probe executable")
endif()
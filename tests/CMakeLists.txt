# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

message(STATUS "[TESTS] Configuring unit tests in ${CMAKE_CURRENT_SOURCE_DIR}")

# 设置测试输出目录
set(TEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/tests)
set(TEST_OUTPUT_DIR_BY_CONFIG ${CMAKE_BINARY_DIR}/tests/$<CONFIG>)
file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})

set(TEST_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

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

function(copy_gmsdk_runtime_dlls target_name)
    if(DEFINED GMSDK_LIBRARY AND EXISTS "${GMSDK_LIBRARY}")
        get_filename_component(GMSDK_RUNTIME_DIR "${GMSDK_LIBRARY}" DIRECTORY)
        foreach(GMSDK_RUNTIME_FILE gmsdk.dll thostmduserapi_se.dll thostmd_wrap.dll)
            if(EXISTS "${GMSDK_RUNTIME_DIR}/${GMSDK_RUNTIME_FILE}")
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${GMSDK_RUNTIME_DIR}/${GMSDK_RUNTIME_FILE}"
                        "$<TARGET_FILE_DIR:${target_name}>/${GMSDK_RUNTIME_FILE}"
                )
            endif()
        endforeach()
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
            "${qt_core_debug_bin_dir}/../plugins/sqldrivers/qsqlited.dll")
    endif()

    get_target_property(qt_core_release_location Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(qt_core_release_location)
        get_filename_component(qt_core_release_bin_dir "${qt_core_release_location}" DIRECTORY)
        list(APPEND qt_sql_plugin_sources
            "${qt_core_release_bin_dir}/../plugins/sqldrivers/qsqlite.dll")
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

# 检�?test_eventsystem.cpp
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_eventsystem.cpp")
    list(APPEND TEST_SOURCES "test_eventsystem.cpp")
    message(STATUS "[TESTS] Found: test_eventsystem.cpp")
else()
    message(WARNING "[TESTS] test_eventsystem.cpp not found!")
endif()

# 检查新增测试文�?
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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_factor_registry_signal_engine.cpp")
    list(APPEND TEST_SOURCES "test_factor_registry_signal_engine.cpp")
    message(STATUS "[TESTS] Found: test_factor_registry_signal_engine.cpp")
else()
    message(WARNING "[TESTS] test_factor_registry_signal_engine.cpp not found!")
endif()


# test_factor_crosscheck_export.cpp 引用已删除的 ConfigurableFactor.h，暂时排除

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data_service_regression.cpp")
    list(APPEND TEST_SOURCES "test_data_service_regression.cpp")
    message(STATUS "[TESTS] Found: test_data_service_regression.cpp")
else()
    message(WARNING "[TESTS] test_data_service_regression.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_strict_cleaning_regression.cpp")
    list(APPEND TEST_SOURCES "test_strict_cleaning_regression.cpp")
    message(STATUS "[TESTS] Found: test_strict_cleaning_regression.cpp")
else()
    message(WARNING "[TESTS] test_strict_cleaning_regression.cpp not found!")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_cpp_cleaning_rules.cpp")
    list(APPEND TEST_SOURCES "test_cpp_cleaning_rules.cpp")
    message(STATUS "[TESTS] Found: test_cpp_cleaning_rules.cpp")
else()
    message(WARNING "[TESTS] test_cpp_cleaning_rules.cpp not found!")
endif()


if(NOT TEST_SOURCES)
    message(FATAL_ERROR "[TESTS] No test source files found!")
endif()

message(STATUS "[TESTS] Test sources: ${TEST_SOURCES}")

# ==================== 创建测试可执行文�?====================
add_executable(test_eventsystem ${TEST_SOURCES})
set_target_properties(test_eventsystem PROPERTIES
    MSVC_RUNTIME_LIBRARY "${TEST_MSVC_RUNTIME_LIBRARY}"
)
if(MSVC)
    target_compile_options(test_eventsystem PRIVATE /FS /bigobj)
endif()
target_include_directories(test_eventsystem PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine/include     # Event.h 等引擎头文件
    ${CMAKE_SOURCE_DIR}/src/domain/include     # Domain 模块头文�?
    ${CMAKE_SOURCE_DIR}/src/foundation/include # Foundation 头文�?
    ${CMAKE_SOURCE_DIR}/astock_engine          # EventSystem.hpp �?
    ${CMAKE_SOURCE_DIR}/src                    # 其他头文�?
)

# 链接�?
target_link_libraries(test_eventsystem
    PUBLIC
        GTest::gtest
        GTest::gtest_main
    PRIVATE
        Qt6::Core
        engine
        domain
        domain_backtest
        domain_strategies
        domain_strategy_runtime
        bridge
        app_adapters
        astockquantapp   # TradingSystem symbols
        factor
        infrastructure
        foundation
)

# 设置输出目录
set_target_properties(test_eventsystem PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR_BY_CONFIG}
)

copy_gmsdk_runtime_dlls(test_eventsystem)

# ==================== 添加运行目标 ====================
add_custom_target(run_event_tests
    COMMAND $<TARGET_FILE:test_eventsystem>
    DEPENDS test_eventsystem
    COMMENT "Running EventSystem tests..."
    VERBATIM
)

# 添加到CTest
enable_testing()
add_test(NAME EventSystemTest
    COMMAND $<TARGET_FILE:test_eventsystem>
    WORKING_DIRECTORY $<TARGET_FILE_DIR:test_eventsystem>
)

message(STATUS "[TESTS] Configured test_eventsystem executable")
message(STATUS "[TESTS] Test output base: ${TEST_OUTPUT_DIR}")

copy_gtest_runtime_dlls(test_eventsystem)
copy_benchmark_runtime_dlls(test_eventsystem)
copy_benchmark_qt_sql_plugins(test_eventsystem)


if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/run_cleaning_cache_probe.cpp")
    message(STATUS "[TESTS] Found: run_cleaning_cache_probe.cpp")

    add_executable(run_cleaning_cache_probe
        run_cleaning_cache_probe.cpp
    )

    set_target_properties(run_cleaning_cache_probe PROPERTIES
        MSVC_RUNTIME_LIBRARY "${TEST_MSVC_RUNTIME_LIBRARY}"
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR_BY_CONFIG}
    )

    target_include_directories(run_cleaning_cache_probe
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/engine/include
            ${CMAKE_SOURCE_DIR}/src/domain/include
            ${CMAKE_SOURCE_DIR}/src/domain/cleaning/include
            ${CMAKE_SOURCE_DIR}/src/foundation/include
            ${CMAKE_SOURCE_DIR}/src/infrastructure/include
            ${CMAKE_SOURCE_DIR}/src/ui/bridge/include
            ${CMAKE_SOURCE_DIR}/astock_engine
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(run_cleaning_cache_probe
        PRIVATE
            bridge
            app_adapters
            foundation
            infrastructure
            astockquantapp
            astock_unordered_dense
    )

    message(STATUS "[TESTS] Configured run_cleaning_cache_probe executable")
endif()

option(ENABLE_RULE_SERVICE_HF_BENCHMARK "Enable rule service HF benchmark target" OFF)
option(ENABLE_RULE_SERVICE_PY_HF_BENCHMARK "Enable python rule service HF benchmark target" OFF)
option(ENABLE_RULE_SERVICE_PY_WORKER_HF_BENCHMARK "Enable python worker rule service HF benchmark target" OFF)

if(ENABLE_RULE_SERVICE_HF_BENCHMARK AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/run_strategy_rule_service_hf_benchmark.cpp")
    message(STATUS "[TESTS] Found: run_strategy_rule_service_hf_benchmark.cpp")

    add_executable(run_strategy_rule_service_hf_benchmark
        run_strategy_rule_service_hf_benchmark.cpp
    )

    set_target_properties(run_strategy_rule_service_hf_benchmark PROPERTIES
        MSVC_RUNTIME_LIBRARY "${TEST_MSVC_RUNTIME_LIBRARY}"
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR_BY_CONFIG}
    )

    target_include_directories(run_strategy_rule_service_hf_benchmark
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/domain/strategy/include
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(run_strategy_rule_service_hf_benchmark
        PRIVATE
            domain_strategy_runtime
            foundation
    )

    message(STATUS "[TESTS] Configured run_strategy_rule_service_hf_benchmark executable")
endif()

if(ENABLE_RULE_SERVICE_PY_HF_BENCHMARK AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/run_strategy_rule_service_py_hf_benchmark.cpp")
    message(STATUS "[TESTS] Found: run_strategy_rule_service_py_hf_benchmark.cpp")

    add_executable(run_strategy_rule_service_py_hf_benchmark
        run_strategy_rule_service_py_hf_benchmark.cpp
    )

    set_target_properties(run_strategy_rule_service_py_hf_benchmark PROPERTIES
        MSVC_RUNTIME_LIBRARY "${TEST_MSVC_RUNTIME_LIBRARY}"
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR_BY_CONFIG}
    )

    target_include_directories(run_strategy_rule_service_py_hf_benchmark
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/domain/strategy/include
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(run_strategy_rule_service_py_hf_benchmark
        PRIVATE
            Qt6::Core
            domain_strategy_runtime
            foundation
    )

    message(STATUS "[TESTS] Configured run_strategy_rule_service_py_hf_benchmark executable")
endif()

if(ENABLE_RULE_SERVICE_PY_WORKER_HF_BENCHMARK AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/run_strategy_rule_service_py_worker_hf_benchmark.cpp")
    message(STATUS "[TESTS] Found: run_strategy_rule_service_py_worker_hf_benchmark.cpp")

    add_executable(run_strategy_rule_service_py_worker_hf_benchmark
        run_strategy_rule_service_py_worker_hf_benchmark.cpp
    )

    set_target_properties(run_strategy_rule_service_py_worker_hf_benchmark PROPERTIES
        MSVC_RUNTIME_LIBRARY "${TEST_MSVC_RUNTIME_LIBRARY}"
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR_BY_CONFIG}
    )

    target_include_directories(run_strategy_rule_service_py_worker_hf_benchmark
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/domain/strategy/include
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(run_strategy_rule_service_py_worker_hf_benchmark
        PRIVATE
            Qt6::Core
            domain_strategy_runtime
            foundation
    )

    message(STATUS "[TESTS] Configured run_strategy_rule_service_py_worker_hf_benchmark executable")
endif()

# ─── 权重方案单元测试 (市值加权/风险平价, 无 DB/UI 依赖) ───
add_executable(test_weight_schemes
    test_weight_schemes.cpp
)

set_target_properties(test_weight_schemes PROPERTIES
    MSVC_RUNTIME_LIBRARY "${TEST_MSVC_RUNTIME_LIBRARY}"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

target_include_directories(test_weight_schemes
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/domain/strategy/include
        ${CMAKE_SOURCE_DIR}/src/domain/strategies/include
        ${CMAKE_SOURCE_DIR}/src/domain/factor/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_weight_schemes
    PRIVATE
        astock_core
        thirdParty
)

message(STATUS "[TESTS] Configured test_weight_schemes executable")

# ─── 规则引擎单元测试 (条件求值三态语义 + compiled.json 全量加载) ───
add_executable(test_rule_engine
    test_rule_engine.cpp
)

set_target_properties(test_rule_engine PROPERTIES
    MSVC_RUNTIME_LIBRARY "${TEST_MSVC_RUNTIME_LIBRARY}"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

target_include_directories(test_rule_engine
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/domain/strategy/rules
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_rule_engine
    PRIVATE
        astock_core
        thirdParty
)

message(STATUS "[TESTS] Configured test_rule_engine executable")

# ─── EOD 下单流程测试 (日终评估, 需 PG) ───
add_executable(test_eod_order_flow
    test_eod_order_flow.cpp
)

set_target_properties(test_eod_order_flow PROPERTIES
    MSVC_RUNTIME_LIBRARY "${TEST_MSVC_RUNTIME_LIBRARY}"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

target_include_directories(test_eod_order_flow
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/domain/strategy/include
        ${CMAKE_SOURCE_DIR}/src/domain/strategies/include
        ${CMAKE_SOURCE_DIR}/src/domain/factor/include
        ${CMAKE_SOURCE_DIR}/src/domain/trading/include
        ${CMAKE_SOURCE_DIR}/src/domain/trading
        ${CMAKE_SOURCE_DIR}/src/domain/market/include
        ${CMAKE_SOURCE_DIR}/src/engine/include
        ${CMAKE_SOURCE_DIR}/src/foundation/include
        ${CMAKE_SOURCE_DIR}/src/infrastructure/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_eod_order_flow
    PRIVATE
        domain_strategy_runtime
        domain_strategies
        engine
        astock_market
        infrastructure
        foundation
)
if(TARGET thirdParty)
    target_link_libraries(test_eod_order_flow PRIVATE thirdParty)
endif()

message(STATUS "[TESTS] Configured test_eod_order_flow executable")

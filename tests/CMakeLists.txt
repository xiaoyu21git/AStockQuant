# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

message(STATUS "[TESTS] Configuring unit tests in ${CMAKE_CURRENT_SOURCE_DIR}")

# 设置测试输出目录
set(TEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/tests)
file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})

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
# 包含目录
target_include_directories(test_eventsystem
    PUBLIC
        ${LIBS_ROOT}/GTest/include
)
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
        ${LIBS_ROOT}/GTest/lib/gtest.lib
        ${LIBS_ROOT}/GTest/lib/gtest_main.lib
    PRIVATE
        engine          # Engine 库（包含 Event）
        domain          # Domain 库
        bridge          # StrategyBacktestController 回归测试
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
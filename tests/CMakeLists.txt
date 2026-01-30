# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

message(STATUS "[TESTS] Configuring unit tests in ${CMAKE_CURRENT_SOURCE_DIR}")

# 设置测试输出目录
set(TEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/tests)
file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})

# ==================== 检查测试源文件是否存在 ====================
set(TEST_SOURCES "")
#set(TEST_SOURCES "test_eventsystem.cpp")
#set(GTEST_ROOT "C:/libs/GTest")  # 指向 GTest 根目录
set(LIBS_ROOT "C:/libs")
# 检查 test_eventsystem.cpp
#find_package(GTEST CONFIG REQUIRED)

# if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_eventsystem.cpp")
#     list(APPEND TEST_SOURCES "test_eventsystem.cpp")
#     message(STATUS "[TESTS] Found: test_eventsystem.cpp")
# else()
#     message(WARNING "[TESTS] test_eventsystem.cpp not found!")
# endif()

# 可以添加更多测试文件检查
# if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/other_test.cpp")
#     list(APPEND TEST_SOURCES "other_test.cpp")
# endif()

if(NOT TEST_SOURCES)
    message(WARNING "[TESTS] No test source files found!")
    return()
endif()

# ==================== 创建测试可执行文件 ====================
add_executable(test_eventsystem ${TEST_SOURCES})
# 包含目录
target_include_directories(test_eventsystem
    PUBLIC
        ${LIBS_ROOT}/GTest/include
)
target_include_directories(test_eventsystem PRIVATE
    ${CMAKE_SOURCE_DIR}/astock_engine          # EventSystem.hpp 在这里
    ${CMAKE_SOURCE_DIR}/foundation/include     # Foundation 头文件
    ${CMAKE_SOURCE_DIR}/src                    # 其他头文件
    C:/libs/GTest/lib/gtest.lib 
    C:/libs/GTest/lib/gtest_main.lib
    C:/libs/GTest/lib/gmock.lib
)

# 链接库
target_link_libraries(test_eventsystem
    astock_core      # 你的核心库
    foundation       # Foundation库
)

# 设置输出目录
set_target_properties(test_eventsystem PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${TEST_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${TEST_OUTPUT_DIR}
)

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
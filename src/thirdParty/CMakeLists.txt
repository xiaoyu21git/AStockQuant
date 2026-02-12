# 第三方API统一接口库配置
set(LIBS_ROOT "C:/libs")

# 添加第三方API库
add_library(thirdParty STATIC
    src/ThirdPartyApi.cpp
    src/JujinApi.cpp
    src/JujinApi_EventBus.cpp
)

# 包含头文件路径
target_include_directories(thirdParty
    PUBLIC
        include
        "${CMAKE_SOURCE_DIR}/thirdparty/gmsdk"
        "${LIBS_ROOT}/foundation/include"
        "${LIBS_ROOT}/foundation"
)

target_link_libraries(thirdParty
    PUBLIC
        foundation
)

# 添加示例程序
add_executable(thirdParty_example
    examples/ThirdPartyApiDemo.cpp
)

target_include_directories(thirdParty_example
    PRIVATE
        include
        "${CMAKE_SOURCE_DIR}/thirdparty/gmsdk"
        "${LIBS_ROOT}/foundation/include"
        "${LIBS_ROOT}/foundation"
)

target_link_libraries(thirdParty_example
    thirdParty
    foundation
)

# 原有的测试程序（保留兼容性）
add_executable(thirdParty_test 
    test/demo_gm_strategy.cpp
)

target_include_directories(thirdParty_test
    PRIVATE
        include
        src
        "${LIBS_ROOT}/foundation/include"
        "${LIBS_ROOT}/foundation"
        "${LIBS_ROOT}/GTest/include"
        "${LIBS_ROOT}/gmsdk/include"
)

target_link_libraries(thirdParty_test
    thirdParty
    foundation
    gmsdk
    "${LIBS_ROOT}/GTest/lib/gtest.lib"
    "${LIBS_ROOT}/GTest/lib/gtest_main.lib"
    "${LIBS_ROOT}/GTest/lib/gmock.lib"
)

enable_testing()
add_test(NAME thirdParty_test COMMAND thirdParty_test)

# 添加EventBus集成测试
add_executable(jujin_eventbus_test
    ${CMAKE_SOURCE_DIR}/test_jujin_eventbus_simple.cpp
)

target_include_directories(jujin_eventbus_test
    PRIVATE
        include
        src
        "${CMAKE_SOURCE_DIR}/src/engine/Event"
        "${LIBS_ROOT}/foundation/include"
        "${LIBS_ROOT}/foundation"
)

target_link_libraries(jujin_eventbus_test
    thirdParty
    foundation
)

add_test(NAME jujin_eventbus_test COMMAND jujin_eventbus_test)

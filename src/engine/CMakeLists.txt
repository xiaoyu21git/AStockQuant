
# 最简单直接的配置
set(LIBS_ROOT "C:/libs")

# engine 库
add_library(engine STATIC)
add_subdirectory(src)

# 关键：直接指定 foundation 路径
target_include_directories(engine
    PUBLIC
        include
        "${LIBS_ROOT}/foundation/include"    # 假设 foundation 头文件在这里
        "${LIBS_ROOT}/foundation"            # 或者这里
)

target_link_libraries(engine
    PUBLIC
        foundation
)

# 测试
add_executable(engine_test 
    test/BacktestEngineTest.cpp
    #test/EventBusTest.cpp
    )

# 测试需要所有包含路径
target_include_directories(engine_test
    PRIVATE
        include
        src
        "${LIBS_ROOT}/foundation/include"
        "${LIBS_ROOT}/foundation"
        "${LIBS_ROOT}/GTest/include"
)

target_link_libraries(engine_test
    engine
    foundation
    "${LIBS_ROOT}/GTest/lib/gtest.lib"
    "${LIBS_ROOT}/GTest/lib/gtest_main.lib"
    "${LIBS_ROOT}/GTest/lib/gmock.lib"
)

enable_testing()
add_test(NAME engine_test COMMAND engine_test)
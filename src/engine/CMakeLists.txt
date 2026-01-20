add_library(engine STATIC)
add_subdirectory(src)

target_link_libraries(engine
    PUBLIC
        foundation
        domain
        # domain_model
        # domain_signals
)
target_include_directories(engine
    PUBLIC
        include
    PRIVATE
        src
)


# ---------- 单元测试 -----------
add_executable(BacktestEngineTest
    test/BacktestEngineTest.cpp
)

target_link_libraries(BacktestEngineTest
    PUBLIC
        domain
        foundation
    PRIVATE 
)

# 可选：启用 CTest 测试
enable_testing()
add_test(NAME BacktestEngineTest COMMAND BacktestEngineTest)
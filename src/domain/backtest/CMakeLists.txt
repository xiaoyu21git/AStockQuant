add_library(domain_backtest STATIC)
add_subdirectory(src)

# 依赖：基础模型 + 策略 + engine + foundation
target_link_libraries(domain_backtest
    PUBLIC
        domain_model
        domain_strategies
        engine
        foundation
)

target_include_directories(domain_backtest
    PUBLIC
        include
    PRIVATE
        src
)

# 添加测试可执行文件
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_executable(test_factor_backtest
        test/test_factor_backtest.cpp
    )
    
    target_link_libraries(test_factor_backtest
        domain_backtest
        domain_model
        domain_strategies
        engine
        foundation
    )
    
    target_include_directories(test_factor_backtest
        PRIVATE
            include
            src
    )
    
    # 设置C++标准
    set_target_properties(test_factor_backtest PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
endif()

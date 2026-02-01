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

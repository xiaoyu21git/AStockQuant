add_library(domain_backtest STATIC)
add_subdirectory(src)

if(TARGET Qt6::Core AND TARGET Qt6::Sql)
    target_link_libraries(domain_backtest
        PRIVATE
            Qt6::Core
            Qt6::Sql
    )
endif()

# 设置C++标准
set_target_properties(domain_backtest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# 对于MSVC，添加/Zc:__cplusplus标志和/permissive-选项
if(MSVC)
    target_compile_options(domain_backtest PRIVATE "/Zc:__cplusplus" "/permissive-")
endif()

# 依赖：基础模型 + 策略 + engine + foundation
target_link_libraries(domain_backtest
    PUBLIC
    domain_types
    domain_strategy
        domain_strategies
    domain_trading
        astock_market
        engine
        foundation
)

target_include_directories(domain_backtest
    PUBLIC
        include
        strategy_engine/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../market/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../engine/include
    PRIVATE
        src
        strategy_engine/src
)

target_sources(domain_backtest
    PRIVATE
    src/HistoricalMarketDataCache.cpp
    strategy_engine/src/CanonicalRuntimeStrategies.cpp
    strategy_engine/src/BacktestRequestValidator.cpp
    strategy_engine/src/BacktestRuntimeSession.cpp
    strategy_engine/src/DiagnosticsRecorder.cpp
    strategy_engine/src/LayerExecutionPipeline.cpp
    strategy_engine/src/ResultAssembler.cpp
    strategy_engine/src/StrategyBacktestEngine.cpp
        strategy_engine/src/StrategyBacktestEngineTypeAnchors.cpp
)

# 添加测试可执行文件
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_executable(test_factor_backtest
        test/test_factor_backtest.cpp
    )
    
    target_link_libraries(test_factor_backtest
        domain_backtest
        domain_types
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

# domain/CMakeLists.txt
add_subdirectory(model)
add_subdirectory(indicators)
add_subdirectory(signals)
add_subdirectory(strategies)
add_subdirectory(data)
add_subdirectory(market)
add_subdirectory(backtest)
add_subdirectory(factor)
# 创建一个聚合库，添加一个虚拟源文件
add_library(domain STATIC
    # 添加一个虚拟源文件，避免空库错误
    ${CMAKE_CURRENT_SOURCE_DIR}/model/src/dummy.cpp
)

# 如果dummy.cpp不存在，创建一个
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/model/src/dummy.cpp")
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/model/src/dummy.cpp" "// Dummy source file for domain library\n")
endif()

# link 子模块库
target_link_libraries(domain
    PUBLIC
        engine
        foundation
        domain_model
        domain_indicators
        domain_signals
        domain_strategies
        domain_data
        domain_backtest
        factor_system
)

# 暴露 include 目录
target_include_directories(domain
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/model/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/indicators/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/signals/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/strategies/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/data/include>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backtest/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/market/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/factor/include>
        $<INSTALL_INTERFACE:include>   # 安装时路径
)



# 最简单直接的配置
set(LIBS_ROOT "C:/libs")



# 测试
add_executable(domain_test 
    test/test_backtest.cpp
    )

# 测试需要所有包含路径
target_include_directories(domain_test
    PUBLIC
        include
    PRIVATE
        src
        "${LIBS_ROOT}/foundation/include"
        "${LIBS_ROOT}/foundation"
        "${LIBS_ROOT}/GTest/include"
)

target_link_libraries(domain_test
    PRIVATE
        domain
        engine
        foundation
        "${LIBS_ROOT}/GTest/lib/gtest.lib"
        "${LIBS_ROOT}/GTest/lib/gtest_main.lib"
        "${LIBS_ROOT}/GTest/lib/gmock.lib"
)

enable_testing()
#add_test(NAME domain_test COMMAND domain_test)
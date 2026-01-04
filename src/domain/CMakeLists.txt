# domain/CMakeLists.txt
add_subdirectory(model)
add_subdirectory(indicators)
add_subdirectory(signals)
add_subdirectory(strategies)
add_subdirectory(data)

# 用 INTERFACE 库代替空库
add_library(domain INTERFACE)

# link 子模块库
target_link_libraries(domain
    INTERFACE
        domain_model
        domain_indicators
        domain_signals
        domain_strategies
        domain_data
)

# 暴露 include 目录
target_include_directories(domain
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/model/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/indicators/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/signals/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/strategies/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/data/include>
        $<INSTALL_INTERFACE:include>   # 安装时路径
)

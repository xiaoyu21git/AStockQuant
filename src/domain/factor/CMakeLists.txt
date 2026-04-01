# 因子系统CMake配置

# 设置项目
cmake_minimum_required(VERSION 3.10)
project(FactorSystem)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/foundation/include
    ${CMAKE_SOURCE_DIR}/src/infrastructure/include
)

# 源文件
set(FACTOR_SOURCES
    src/DataAvailabilityChecker.cpp
    src/BaseFactor.cpp
    src/MomentumFactor.cpp
    src/ValueFactor.cpp
    src/SizeFactor.cpp
    src/LowVolFactor.cpp
    src/QualityFactor.cpp
    src/ConfigurableFactor.cpp
    src/FactorCacheManager.cpp
    src/FactorInstanceManager.cpp
    src/FactorBacktestExecutor.cpp
)

# 头文件
set(FACTOR_HEADERS
    include/DataAvailabilityChecker.h
    include/BaseFactor.h
    include/FactorDataProvider.h
    include/MomentumFactor.h
    include/ValueFactor.h
    include/SizeFactor.h
    include/LowVolFactor.h
    include/QualityFactor.h
    include/ConfigurableFactor.h
    include/FactorCacheManager.h
    include/FactorInstanceManager.h
    include/FactorBacktestExecutor.h
)

# 创建库
add_library(factor_system STATIC ${FACTOR_SOURCES} ${FACTOR_HEADERS})

# 链接库
target_link_libraries(factor_system
    PUBLIC
    Qt6::Sql
        foundation
        infrastructure
)

# 安装
install(TARGETS factor_system
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/domain/factor
    FILES_MATCHING PATTERN "*.h"
)

# 测试程序
set(FACTOR_TEST_SOURCE "${CMAKE_SOURCE_DIR}/test_factor_system.cpp")
if(BUILD_TESTS AND EXISTS "${FACTOR_TEST_SOURCE}")
    add_executable(test_factor_system
        ${FACTOR_TEST_SOURCE}
    )
    
    target_link_libraries(test_factor_system
        factor_system
        foundation
        infrastructure
        pymysql
        pthread
    )
    
    # 添加测试
    add_test(NAME test_factor_system
        COMMAND test_factor_system
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()
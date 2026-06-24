# src/cache/CMakeLists.txt
# 缓存系统构建配置

cmake_minimum_required(VERSION 3.16)
project(AStockQuantEngine_Cache)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/thirdparty
)

# 查找依赖库
find_package(Threads REQUIRED)

set(_cache_module_build_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(DISABLE_TESTS ON CACHE BOOL "Disable hiredis tests" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "Disable hiredis examples" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "Disable hiredis SSL support" FORCE)
set(ENABLE_SSL_TESTS OFF CACHE BOOL "Disable hiredis SSL tests" FORCE)
set(ENABLE_ASYNC_TESTS OFF CACHE BOOL "Disable hiredis async tests" FORCE)
set(ENABLE_NUGET OFF CACHE BOOL "Disable hiredis NuGet packaging" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/hiredis ${CMAKE_BINARY_DIR}/thirdparty/hiredis EXCLUDE_FROM_ALL)

set(BUILD_SHARED_LIBS ${_cache_module_build_shared_libs})

# 查找Caffeine（本地缓存）
find_path(CAFFEINE_INCLUDE_DIR caffeine/caffeine.hpp
    PATHS
    ${CMAKE_SOURCE_DIR}/thirdparty/caffeine
    /usr/local/include
    /usr/include
)

find_library(CAFFEINE_LIBRARY caffeine
    PATHS
    ${CMAKE_SOURCE_DIR}/thirdparty/caffeine/lib
    /usr/local/lib
    /usr/lib
)

# 使用Foundation JSON接口，不需要外部JSON库
# find_package(nlohmann_json 3.9.1 REQUIRED) 已移除

# 源文件
set(CACHE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cache_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/local_cache_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/redis_cache_manager.cpp
)

# 头文件
set(CACHE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cache_manager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cache_facade.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/serialization.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/local_cache_manager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/redis_cache_manager.h
)

# 创建缓存库
add_library(cache_module STATIC)
target_sources(cache_module PRIVATE ${CACHE_SOURCES} ${CACHE_HEADERS})

# /utf-8 已在根 CMakeLists.txt 全局设置

# 链接依赖库（使用Foundation JSON，不需要外部JSON库）
target_link_libraries(cache_module
    Threads::Threads
    hiredis
    foundation  # 使用Foundation的JSON接口
)

# 添加Caffeine依赖（如果找到）
if(CAFFEINE_INCLUDE_DIR AND CAFFEINE_LIBRARY)
    target_include_directories(cache_module PRIVATE ${CAFFEINE_INCLUDE_DIR})
    target_link_libraries(cache_module ${CAFFEINE_LIBRARY})
    add_definitions(-DHAVE_CAFFEINE)
    message(STATUS "Found Caffeine: ${CAFFEINE_LIBRARY}")
else()
    message(WARNING "Caffeine not found, using built-in local cache fallback")
    add_definitions(-DNO_CAFFEINE)
endif()

# 安装目标
install(TARGETS cache_module
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/astock/cache
    FILES_MATCHING PATTERN "*.h"
)

# 测试目标 (已禁用 - 测试文件不存在)
#if(BUILD_TESTS)
#    enable_testing()
#    
#    # 测试源文件
#    set(TEST_SOURCES
#        tests/test_local_cache.cpp
#        tests/test_redis_cache.cpp
#        tests/test_cache_integration.cpp
#        tests/test_serialization.cpp
#    )
#    
#    # 创建测试可执行文件
#    add_executable(cache_tests ${TEST_SOURCES})
#    
#    # 链接测试依赖
#    target_link_libraries(cache_tests
#        cache_module
#        gtest
#        gtest_main
#        Threads::Threads
#    )
#    
#    # 添加测试
#    add_test(NAME test_local_cache COMMAND cache_tests --gtest_filter=LocalCacheTest*)
#    add_test(NAME test_redis_cache COMMAND cache_tests --gtest_filter=RedisCacheTest*)
#    add_test(NAME test_serialization COMMAND cache_tests --gtest_filter=SerializationTest*)
#    add_test(NAME test_integration COMMAND cache_tests --gtest_filter=IntegrationTest*)
#    
#    # 安装测试
#    install(TARGETS cache_tests DESTINATION tests)
#endif()

# 配置头文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config/cache_config.yaml
    ${CMAKE_BINARY_DIR}/cache_config.yaml
    COPYONLY
)

# 添加版本信息
set_target_properties(cache_module PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

# 输出信息
message(STATUS "Cache module configured successfully")
message(STATUS "  - Caffeine: ${CAFFEINE_FOUND}")
message(STATUS "  - Redis: vendored hiredis")
message(STATUS "  - JSON: Foundation JSON")

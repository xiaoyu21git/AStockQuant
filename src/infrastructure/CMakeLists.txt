# CMakeLists.txt for infrastructure module
cmake_minimum_required(VERSION 3.15)

project(infrastructure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET Qt6::Core)
    find_package(Qt6 COMPONENTS Core Sql REQUIRED)
endif()

if (WIN32)
    set(OPENSSL_ROOT_DIR "C:/libs/OpenSSL")
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()

find_package(OpenSSL REQUIRED)

# 基础设施静态库源码
add_library(infrastructure STATIC
    src/database/NativePgDatabase.cpp
    src/database/NativePgConnectionPool.cpp
    src/database/OrderRecorder.cpp
    src/database/PostMarketSyncService.cpp
    src/database/EventBridgePoller.cpp
    src/database/StrategyRepository.cpp
    src/database/MarketDataRepository.cpp
    src/database/MarketDataService.cpp
    src/database/SymbolMapper.cpp
    src/database/BacktestResultRepository.cpp
)

set_target_properties(infrastructure PROPERTIES
    AUTOMOC ON
)

# 添加 domain include 路径
target_include_directories(infrastructure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PostgreSQL_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/domain/factor/include
        ${CMAKE_SOURCE_DIR}/src/domain/market/include
        ${CMAKE_SOURCE_DIR}/src/domain/cleaning/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(infrastructure
    PUBLIC
        foundation
        ${PostgreSQL_LIBRARY_RELEASE}
    PRIVATE
        Qt6::Core
        Qt6::Sql
        OpenSSL::SSL
        OpenSSL::Crypto
)

find_package(Threads REQUIRED)
target_link_libraries(infrastructure PRIVATE Threads::Threads)
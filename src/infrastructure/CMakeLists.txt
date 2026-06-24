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

# 查找 MySQL 库
find_library(MYSQL_LIB
    NAMES libmysql mysqlclient
    PATHS
        "D:/Program Files/MySQL/lib"
        "D:/Program Files/MySQL/MySQL Server 8.0/lib"
        "C:/Program Files/MySQL/MySQL Server 8.0/lib"
        "C:/Program Files/MySQL/MySQL Connector C++ 8.0/lib64"
        "/usr/lib/x86_64-linux-gnu"
        "/usr/local/lib"
)

find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h mysql/mysql.h
    PATHS
        "D:/Program Files/MySQL/include"
        "D:/Program Files/MySQL/MySQL Server 8.0/include"
        "C:/Program Files/MySQL/MySQL Server 8.0/include"
        "C:/Program Files/MySQL/MySQL Connector C++ 8.0/include"
        "/usr/include/mysql"
        "/usr/local/include/mysql"
)

if(NOT MYSQL_INCLUDE_DIR AND MARKET_MYSQL_INCLUDE_DIR)
    set(MYSQL_INCLUDE_DIR ${MARKET_MYSQL_INCLUDE_DIR})
endif()

if(MYSQL_LIB AND MYSQL_INCLUDE_DIR)
    message(STATUS "Found MySQL: ${MYSQL_LIB}")
    message(STATUS "MySQL include: ${MYSQL_INCLUDE_DIR}")
else()
    message(WARNING "MySQL not found. Database features will be disabled.")
endif()

# 基础设施静态库源码
add_library(infrastructure STATIC
    src/database/NativePgDatabase.cpp
    src/database/NativeMySQLConnectionPool.cpp
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
        ${PostgreSQL_INCLUDE_DIRS}
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

if(MYSQL_LIB)
    target_link_libraries(infrastructure PRIVATE ${MYSQL_LIB})
endif()

find_package(Threads REQUIRED)
target_link_libraries(infrastructure PRIVATE Threads::Threads)
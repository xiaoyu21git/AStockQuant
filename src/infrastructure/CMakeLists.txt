# CMakeLists.txt for infrastructure module
cmake_minimum_required(VERSION 3.15)

project(infrastructure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找Qt6（如果项目使用Qt）
if(NOT TARGET Qt6::Core)
    find_package(Qt6 COMPONENTS Core Sql QUIET)
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

# 如果本模块未单独找到 MYSQL_INCLUDE_DIR，则复用 domain/market 中已经找到的 MARKET_MYSQL_INCLUDE_DIR
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
    src/database/ConnectionPool.cpp
    src/database/MarketDataRepository.cpp
    src/database/QtMySQLDatabase.cpp
    src/database/QueryBuilder.cpp
    src/database/FactorRepository.cpp
    # 其他数据源/缓存实现如需启用可在此追加
)

# 启用Qt元对象编译器（MOC）
set_target_properties(infrastructure PROPERTIES
    AUTOMOC ON
    INTERPROCEDURAL_OPTIMIZATION TRUE
)

target_link_libraries(infrastructure
    PUBLIC
        foundation
        domain_model
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

# 如果找到Qt6，链接Qt库
if(TARGET Qt6::Core AND TARGET Qt6::Sql)
    target_link_libraries(infrastructure PRIVATE Qt6::Core Qt6::Sql)
endif()

if(MYSQL_LIB)
    target_link_libraries(infrastructure PRIVATE ${MYSQL_LIB})
endif()

# 线程库支持
find_package(Threads REQUIRED)
target_link_libraries(infrastructure PRIVATE Threads::Threads)

# 头文件搜索路径
target_include_directories(infrastructure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${MYSQL_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

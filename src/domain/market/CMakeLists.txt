# PostgreSQL via infrastructure
find_package(PostgreSQL REQUIRED)

# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(astock_market LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (WIN32)
    set(OPENSSL_ROOT_DIR "C:/libs/OpenSSL")
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()
find_package(OpenSSL REQUIRED)

# 查找 MySQL 头文件（用于 DatabaseProvider）
find_path(MARKET_MYSQL_INCLUDE_DIR
    NAMES mysql.h mysql/mysql.h
    PATHS
        "D:/Program Files/MySQL/include"
        "D:/Program Files/MySQL/MySQL Server 8.0/include"
        "C:/Program Files/MySQL/MySQL Server 8.0/include"
        "C:/Program Files/MySQL/MySQL Connector C++ 8.0/include"
        "/usr/include/mysql"
        "/usr/local/include/mysql"
)

if(NOT MARKET_MYSQL_INCLUDE_DIR)
    message(WARNING "MySQL headers not found for astock_market; DatabaseProvider may fail to build.")
endif()
# 添加源码
add_library(astock_market STATIC
    src/AdjustableMarketDataView.cpp
    src/LiveData.cpp
    src/MarketDataService.cpp
)

target_link_libraries(astock_market
    PUBLIC
        foundation
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)
target_include_directories(astock_market
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/domain/factor/include
        ${CMAKE_SOURCE_DIR}/src/domain/market/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# 链接选项
set_target_properties(astock_market PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
)

# market_db_demo 引用了已删除的 market/core/MarketData.h，暂时禁用
#add_executable(market_db_demo
#    test/market_db_demo.cpp
#)

# # 测试可执行文件
# if(BUILD_TESTS)
#     add_executable(market_tests
#         tests/test_datatypes.cpp
#         tests/test_market.cpp
#         tests/test_providers.cpp
#         tests/TestRepositoryWithFoundation.cpp
#     )
    
#     target_link_libraries(market_tests  ${openssl}  foundation domain_types astock_market gtest gtest_main )
    
#     add_test(NAME market_tests COMMAND market_tests)
# endif()
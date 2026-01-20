# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(infrastructure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (WIN32)
    set(OPENSSL_ROOT_DIR "C:/libs/OpenSSL")
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()
find_package(OpenSSL REQUIRED)
#find_package(MySql REQUIRED)
#find_package(SQLite3 REQUIRED)
# 添加源码
add_library(infrastructure STATIC
#    Cache/CacheManager.cpp
#    Cache/MemoryCache.cpp
#    Cache/RedisCache.cpp
#    database/DatabaseManager.cpp
#    database/IDatabaseConnection.cpp
#    database/MySQLRepository.cpp
#    database/QueryBuilder.cpp
#    database/SQLiteRepository.cpp
#    database/Transaction.cpp
#    datasource/ApiDataSource.cpp
#    datasource/DatabaseSource.cpp
#    datasource/DataSourceChain.cpp
#    datasource/DataSourceContext.cpp
#    datasource/DataSourceFactory.cpp
#    datasource/FileDataSource.cpp
)
target_link_libraries(infrastructure
    PUBLIC
        foundation
        domain_model
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        #MySql::MySql
)
target_include_directories(infrastructure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# 链接选项
set_target_properties(infrastructure PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
)

# 测试可执行文件
if(BUILD_TESTS)
    add_executable(infrastructure
        
    )
    
    target_link_libraries(infrastructure_test  ${openssl}  foundation domain_model astock_market gtest gtest_main )
    
    add_test(NAME infrastructure_test COMMAND infrastructure_test)
endif()
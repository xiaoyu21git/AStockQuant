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
#find_package(MySql REQUIRED)
#find_package(SQLite3 REQUIRED)
# 添加源码
add_library(astock_market STATIC
    # src/core/DataTypes.cpp
    # src/core/MarketData.cpp
    # src/core/DataProviderFactory.cpp
    # src/providers/SimProvider.cpp
    # src/providers/FileProvider.cpp
    # src/providers/ApiProvider.cpp
    # src/providers/DatabaseProvider.cpp
    # src/observer/data_observer.cpp
    # src/observer/data_observer_base.cpp
    # src/observer/kline_observer.cpp
    # src/observer/tick_observer.cpp
    # src/observer/storage_observer.cpp
    # src/observer/AlertObserver.cpp
    # src/observer/ObserverFactory.cpp
    # src/repository/BaseMarketDataRepository.cpp
    # src/repository/MemoryMarketDataRepository.cpp
)
target_link_libraries(astock_market
    PUBLIC
        foundation
        domain_model
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        #MySql::MySql
)
target_include_directories(astock_market
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# 链接选项
set_target_properties(astock_market PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
)

# 测试可执行文件
if(BUILD_TESTS)
    add_executable(market_tests
        tests/test_datatypes.cpp
        tests/test_market.cpp
        tests/test_providers.cpp
        tests/TestRepositoryWithFoundation.cpp
    )
    
    target_link_libraries(market_tests  ${openssl}  foundation domain_model astock_market gtest gtest_main )
    
    add_test(NAME market_tests COMMAND market_tests)
endif()
# app/adapters — 券商网关适配器 (纯 C++, 零 Qt)
cmake_minimum_required(VERSION 3.15)

add_library(app_adapters STATIC
    SimulatedBrokerGateway.cpp
)

target_include_directories(app_adapters
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../ui/bridge/include
        ${CMAKE_SOURCE_DIR}/thirdparty/gmsdk
)

target_link_libraries(app_adapters
    PUBLIC
        domain_trading
        foundation
    PRIVATE
        Threads::Threads
)
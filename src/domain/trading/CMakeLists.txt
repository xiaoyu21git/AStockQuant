# domain/trading — 券商网关抽象层 (纯 C++, 零 Qt)
cmake_minimum_required(VERSION 3.15)

add_library(domain_trading INTERFACE)

target_include_directories(domain_trading
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(domain_trading
    INTERFACE
        foundation
)
# 标准设置 - 根目录已经设置了AUTOMOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Quick Gui Sql Qml Multimedia QuickWidgets Concurrent)

# 收集头文件用于MOC处理
file(GLOB BRIDGE_HEADERS include/*.h)

add_library(bridge STATIC
    ${BRIDGE_HEADERS}
)

add_subdirectory(src)

target_compile_definitions(bridge PRIVATE BUILD_DLL BRIDGE_LIBRARY)

target_link_libraries(bridge
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Quick
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Sql
        Qt6::Qml
        Qt6::QuickWidgets
        Qt6::Concurrent
    PRIVATE
        engine
        domain
        astock_market
        infrastructure
)

target_include_directories(bridge
    PUBLIC
        include
    PRIVATE
        src
)

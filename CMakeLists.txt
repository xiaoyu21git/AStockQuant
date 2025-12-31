cmake_minimum_required(VERSION 3.20)
project(AStockQuant LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickWidgets Svg WebEngineQuick DBus
)

qt_standard_project_setup()

add_subdirectory(src)
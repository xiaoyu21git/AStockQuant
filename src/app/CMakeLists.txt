cmake_minimum_required(VERSION 3.15)
project(AStockQuantApp
VERSION 0.0.1
LANGUAGES CXX)
# message(STATUS "$ENV{CMAKE_PREFIX_PATH}")

# Qt build specific options, need to be defined before an Qt targets
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
add_library(astockquantapp SHARED
)
add_subdirectory(src)
find_package(Qt6 REQUIRED COMPONENTS Core  Widgets Quick Gui Sql Qml Multimedia QuickWidgets  WebEngineQuick WebEngineWidgets)
target_compile_definitions(astockquantapp PRIVATE BUILD_DLL)
target_link_libraries(astockquantapp
    Qt6::Core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Sql
    Qt6::Qml
    Qt6::QuickWidgets
    Qt6::WebEngineQuick
    Qt6::WebEngineWidgets
 )
target_include_directories(astockquantapp
    PUBLIC
        include
    PRIVATE
        src
)

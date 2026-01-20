# 主CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

project(AstockQuantEngine VERSION 0.0.1)

# ==================== 基础设置 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== Qt设置 ====================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON) 
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS 
    Core Widgets Quick Gui Sql Qml Multimedia QuickWidgets
)

# ==================== Python设置 ====================
set(Python_FIND_STRATEGY LOCATION)
set(Python_FIND_REGISTRY LAST)
find_package(Python 3.12 REQUIRED COMPONENTS Interpreter Development)

message(STATUS "Python found: ${Python_EXECUTABLE}")
message(STATUS "Python include dirs: ${Python_INCLUDE_DIRS}")
message(STATUS "Python library: ${Python_LIBRARIES}")

# ==================== 构建选项 ====================
option(BUILD_PYTHON_EXTENSIONS "构建Python C++扩展" ON)
option(BUILD_TESTS "构建测试" OFF)

# ==================== 输出目录设置 ====================
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) #先设置到build的目录下 ${CMAKE_CURRENT_SOURCE_DIR}这个项目根目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ==================== 添加子目录 ====================
# 先构建C++核心
add_subdirectory(src)

# 然后构建Python绑定（依赖C++库）
if(BUILD_PYTHON_EXTENSIONS)
    add_subdirectory(astock_engine)
endif()
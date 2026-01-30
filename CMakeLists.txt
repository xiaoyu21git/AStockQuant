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

# 可选 UI 支持（默认禁用以便在无 Qt 的 CI 运行测试）
option(BUILD_UI "Build UI (Qt6) components" OFF)

if(BUILD_UI)
    find_package(Qt6 REQUIRED COMPONENTS 
        Core Widgets Quick Gui Sql Qml Multimedia QuickWidgets
    )
endif()

# 可选：网络支持（libcurl），在 CI runner 上通常禁用
option(BUILD_WITH_CURL "Enable libcurl-based network support" ON)

# ==================== Python设置 ====================
set(Python_FIND_STRATEGY LOCATION)
set(Python_FIND_REGISTRY LAST)
find_package(Python 3.12 REQUIRED COMPONENTS Interpreter Development)

message(STATUS "Python found: ${Python_EXECUTABLE}")
message(STATUS "Python include dirs: ${Python_INCLUDE_DIRS}")
message(STATUS "Python library: ${Python_LIBRARIES}")

# ==================== 构建选项 ====================
option(BUILD_PYTHON_EXTENSIONS "构建Python C++扩展" ON)
option(BUILD_TESTS "构建测试" ON)

# ==================== 输出目录设置 ====================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/lib)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ==================== 添加子目录 ====================
# 先构建C++核心
add_subdirectory(src)
# 然后构建Python绑定（依赖C++库）
if(BUILD_PYTHON_EXTENSIONS)
    add_subdirectory(astock_engine)
endif()
# ==================== 添加C++测试 ====================
# 主 CMakeLists.txt - 确保这部分正确
if(BUILD_TESTS)
    message(STATUS "Building C++ tests")
    
    # 直接添加 tests 目录（不是 tests/cpp）
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    else()
        message(WARNING "tests/CMakeLists.txt not found at ${CMAKE_CURRENT_SOURCE_DIR}/tests/")
        
        # 列出 tests 目录内容以便调试
        file(GLOB TEST_CONTENTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*")
        message(STATUS "Tests directory contents: ${TEST_CONTENTS}")
    endif()
endif()
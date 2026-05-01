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

include(FetchContent)

set(ARROW_SIMD_LEVEL NONE CACHE STRING "" FORCE)
set(ARROW_RUNTIME_SIMD_LEVEL NONE CACHE STRING "" FORCE)
set(ARROW_DEFINE_OPTIONS ON CACHE BOOL "" FORCE)
set(ARROW_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(ARROW_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ARROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_INTEGRATION OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_UTILITIES OFF CACHE BOOL "" FORCE)
set(ARROW_COMPUTE ON CACHE BOOL "" FORCE)
set(ARROW_IPC ON CACHE BOOL "" FORCE)
set(ARROW_USE_BOOST ON CACHE BOOL "" FORCE)

set(BOOST_LOCAL_SOURCE_DIR "G:/C++/lib/boost")
if(EXISTS "${BOOST_LOCAL_SOURCE_DIR}/boost/multiprecision/cpp_int.hpp")
    set(FETCHCONTENT_SOURCE_DIR_BOOST "${BOOST_LOCAL_SOURCE_DIR}" CACHE PATH "Local Boost source directory" FORCE)
    set(BOOST_ROOT "${BOOST_LOCAL_SOURCE_DIR}" CACHE PATH "Boost source root" FORCE)
    set(BOOST_INCLUDEDIR "${BOOST_LOCAL_SOURCE_DIR}" CACHE PATH "Boost include root" FORCE)
    set(Boost_INCLUDE_DIR "${BOOST_LOCAL_SOURCE_DIR}" CACHE PATH "Boost include root" FORCE)
    set(Boost_INCLUDE_DIRS "${BOOST_LOCAL_SOURCE_DIR}" CACHE PATH "Boost include root" FORCE)
    message(STATUS "Using local Boost source directory: ${BOOST_LOCAL_SOURCE_DIR}")

    if(NOT TARGET boost_headers)
        add_library(boost_headers INTERFACE)
        target_include_directories(boost_headers INTERFACE ${BOOST_LOCAL_SOURCE_DIR})
    endif()
else()
    message(STATUS "Local Boost source tree is incomplete or missing required headers; falling back to FetchContent tarball")
    if(DEFINED FETCHCONTENT_SOURCE_DIR_BOOST)
        unset(FETCHCONTENT_SOURCE_DIR_BOOST CACHE)
    endif()
endif()

FetchContent_Declare(
    boost
    URL https://archives.boost.io/release/1.91.0/source/boost_1_91_0.tar.gz
    URL_HASH SHA256=5734305f40a76c30f951c9abd409a45a2a19fb546efe4162119250bbe4d3a463
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(boost)
if(NOT boost_POPULATED)
    FetchContent_Populate(boost)
endif()

set(_astock_arrow_saved_build_type "${CMAKE_BUILD_TYPE}")
if(MSVC)
    # Arrow's third-party ExternalProject logic keys off CMAKE_BUILD_TYPE.
    # Keep the FetchContent configure pass on Debug so re2 and friends match the
    # current Debug build, then restore the parent value immediately after.
    set(CMAKE_BUILD_TYPE Debug)
endif()

FetchContent_Declare(
    arrow
    URL https://archive.apache.org/dist/arrow/arrow-15.0.0/apache-arrow-15.0.0.tar.gz
    URL_HASH SHA512=d5dccaa0907b0e6f2a460e32ae75091942dcb70b51db4aefe2767ee8d99882694607b723a9c06898dda3938d8eb498258d7f9aad11054665b6ea9c2fbaeafa74
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(arrow)
if(NOT arrow_POPULATED)
    FetchContent_Populate(arrow)
    add_subdirectory(${arrow_SOURCE_DIR}/cpp ${arrow_BINARY_DIR})
endif()

if(MSVC)
    set(CMAKE_BUILD_TYPE "${_astock_arrow_saved_build_type}")
endif()
unset(_astock_arrow_saved_build_type)

FetchContent_Declare(
    eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
)
FetchContent_GetProperties(eigen)
if(NOT eigen_POPULATED)
    FetchContent_Populate(eigen)
    include_directories(${eigen_SOURCE_DIR})
endif()

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
option(BUILD_TESTS "构建测试" ON)
option(ENABLE_JUJIN_MARKET "启用掘金实时行情接入" OFF)

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
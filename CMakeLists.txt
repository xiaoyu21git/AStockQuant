# 主CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

project(AstockQuantEngine VERSION 0.0.1)

# ==================== 基础设置 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:/FS>"
        "$<$<COMPILE_LANGUAGE:CXX>:/FS>"
    )
endif()

# ==================== Qt设置 ====================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON) 
set(CMAKE_AUTOUIC ON)

include(FetchContent)

set(ARROW_SIMD_LEVEL NONE CACHE STRING "" FORCE)
set(ARROW_RUNTIME_SIMD_LEVEL NONE CACHE STRING "" FORCE)
set(ARROW_DEFINE_OPTIONS ON CACHE BOOL "" FORCE)
set(ARROW_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(ARROW_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_INTEGRATION OFF CACHE BOOL "" FORCE)
set(ARROW_BUILD_UTILITIES OFF CACHE BOOL "" FORCE)
set(ARROW_COMPUTE ON CACHE BOOL "" FORCE)
set(ARROW_IPC ON CACHE BOOL "" FORCE)
set(ARROW_USE_BOOST ON CACHE BOOL "" FORCE)

set(ARROW_LOCAL_SOURCE_DIR_HINTS
    "G:/C++/lib/apache-arrow-15.0.0"
    "G:/C++/lib/apache-arrow"
    "${CMAKE_SOURCE_DIR}/build/_deps/arrow-src"
)

unset(_astock_arrow_local_source_dir)
foreach(_astock_arrow_candidate IN LISTS ARROW_LOCAL_SOURCE_DIR_HINTS)
    if(EXISTS "${_astock_arrow_candidate}/cpp/CMakeLists.txt"
       AND EXISTS "${_astock_arrow_candidate}/cpp/src/arrow/api.h")
        set(_astock_arrow_local_source_dir "${_astock_arrow_candidate}")
        break()
    endif()
endforeach()

if(_astock_arrow_local_source_dir)
    set(FETCHCONTENT_SOURCE_DIR_ARROW "${_astock_arrow_local_source_dir}" CACHE PATH "Local Arrow source directory" FORCE)
    message(STATUS "Using local Arrow source directory: ${_astock_arrow_local_source_dir}")
else()
    message(STATUS "Local Arrow source tree is incomplete or missing required sources; falling back to FetchContent tarball")
    if(DEFINED FETCHCONTENT_SOURCE_DIR_ARROW)
        unset(FETCHCONTENT_SOURCE_DIR_ARROW CACHE)
    endif()
endif()
unset(_astock_arrow_candidate)
unset(_astock_arrow_local_source_dir)

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
    # Arrow's VS project generation hard-codes vendored RE2/utf8proc to the
    # *_ep-install/lib path. Sync that install location from the matching
    # config-specific build outputs before arrow_shared links so Debug never
    # picks up a previously installed Release library.
    if(TARGET arrow_shared AND TARGET re2_ep AND TARGET utf8proc_ep)
        add_custom_target(astock_arrow_sync_vendored_libs
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${arrow_BINARY_DIR}/re2_ep-prefix/src/re2_ep-build/$<CONFIG>/re2.lib"
                "${arrow_BINARY_DIR}/re2_ep-install/lib/re2.lib"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${arrow_BINARY_DIR}/utf8proc_ep-prefix/src/utf8proc_ep-build/$<CONFIG>/utf8proc_static.lib"
                "${arrow_BINARY_DIR}/utf8proc_ep-install/lib/utf8proc_static.lib"
            DEPENDS re2_ep utf8proc_ep
            BYPRODUCTS
                "${arrow_BINARY_DIR}/re2_ep-install/lib/re2.lib"
                "${arrow_BINARY_DIR}/utf8proc_ep-install/lib/utf8proc_static.lib"
            VERBATIM
        )
        add_dependencies(arrow_shared astock_arrow_sync_vendored_libs)
        if(TARGET arrow_bundled_dependencies_merge)
            add_dependencies(arrow_bundled_dependencies_merge astock_arrow_sync_vendored_libs)
        endif()
    endif()
endif()

if(MSVC)
    set(CMAKE_BUILD_TYPE "${_astock_arrow_saved_build_type}")
endif()
unset(_astock_arrow_saved_build_type)

set(EIGEN_LOCAL_SOURCE_DIR_HINTS
    "G:/C++/lib/eigen-3.4.0"
    "G:/C++/lib/eigen"
    "${CMAKE_SOURCE_DIR}/build/_deps/eigen-src"
)

unset(_astock_eigen_local_source_dir)
foreach(_astock_eigen_candidate IN LISTS EIGEN_LOCAL_SOURCE_DIR_HINTS)
    if(EXISTS "${_astock_eigen_candidate}/Eigen/Dense"
       AND EXISTS "${_astock_eigen_candidate}/unsupported")
        set(_astock_eigen_local_source_dir "${_astock_eigen_candidate}")
        break()
    endif()
endforeach()

if(_astock_eigen_local_source_dir)
    message(STATUS "Using local Eigen source directory: ${_astock_eigen_local_source_dir}")
    include_directories(${_astock_eigen_local_source_dir})
else()
    FetchContent_Declare(
        eigen
        URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
    )
    FetchContent_GetProperties(eigen)
    if(NOT eigen_POPULATED)
        FetchContent_Populate(eigen)
    endif()
    include_directories(${eigen_SOURCE_DIR})
endif()
unset(_astock_eigen_candidate)
unset(_astock_eigen_local_source_dir)

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
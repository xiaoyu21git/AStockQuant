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
        "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:C>:/wd4828>"
        "$<$<COMPILE_LANGUAGE:CXX>:/wd4828>"
        "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>"
    )
endif()

# ==================== Qt设置 ====================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON) 
set(CMAKE_AUTOUIC ON)

include(FetchContent)

# ==================== 性能开源库集成（先集成，后替换） ====================
option(ASTOCK_ENABLE_PERF_THIRDPARTY "Enable performance-oriented third-party libraries" ON)

if(ASTOCK_ENABLE_PERF_THIRDPARTY)
    # 强制只使用本地源码，不允许动态下载
    set(ASTOCK_LIB_ROOT "G:/C++/lib")

    set(ASTOCK_FMT_CANDIDATES
        "${ASTOCK_LIB_ROOT}/fmt-11.0.2"
        "${ASTOCK_LIB_ROOT}/fmt"
    )
    unset(ASTOCK_FMT_LOCAL_SOURCE_DIR)
    foreach(_astock_fmt_candidate IN LISTS ASTOCK_FMT_CANDIDATES)
        if(EXISTS "${_astock_fmt_candidate}/CMakeLists.txt"
           AND EXISTS "${_astock_fmt_candidate}/include/fmt/format.h")
            set(ASTOCK_FMT_LOCAL_SOURCE_DIR "${_astock_fmt_candidate}")
            break()
        endif()
    endforeach()
    if(NOT ASTOCK_FMT_LOCAL_SOURCE_DIR)
        message(FATAL_ERROR "fmt local source not found under G:/C++/lib. Expected one of: ${ASTOCK_FMT_CANDIDATES}")
    endif()

    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
    set(FMT_HEADER_ONLY ON CACHE BOOL "" FORCE)
    add_subdirectory("${ASTOCK_FMT_LOCAL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/thirdparty/fmt")

    set(ASTOCK_UNORDERED_DENSE_CANDIDATES
        "${ASTOCK_LIB_ROOT}/unordered_dense-4.4.0"
        "${ASTOCK_LIB_ROOT}/unordered_dense"
    )
    unset(ASTOCK_UNORDERED_DENSE_LOCAL_SOURCE_DIR)
    foreach(_astock_unordered_dense_candidate IN LISTS ASTOCK_UNORDERED_DENSE_CANDIDATES)
        if(EXISTS "${_astock_unordered_dense_candidate}/include/ankerl/unordered_dense.h")
            set(ASTOCK_UNORDERED_DENSE_LOCAL_SOURCE_DIR "${_astock_unordered_dense_candidate}")
            break()
        endif()
    endforeach()
    if(NOT ASTOCK_UNORDERED_DENSE_LOCAL_SOURCE_DIR)
        message(FATAL_ERROR "unordered_dense local source not found under G:/C++/lib. Expected one of: ${ASTOCK_UNORDERED_DENSE_CANDIDATES}")
    endif()

    if(NOT TARGET astock_unordered_dense)
        add_library(astock_unordered_dense INTERFACE)
        target_include_directories(astock_unordered_dense INTERFACE "${ASTOCK_UNORDERED_DENSE_LOCAL_SOURCE_DIR}/include")
    endif()

    set(ASTOCK_XXHASH_CANDIDATES
        "${ASTOCK_LIB_ROOT}/xxHash-0.8.2"
        "${ASTOCK_LIB_ROOT}/xxhash"
        "${ASTOCK_LIB_ROOT}/xxHash"
    )
    unset(ASTOCK_XXHASH_LOCAL_SOURCE_DIR)
    foreach(_astock_xxhash_candidate IN LISTS ASTOCK_XXHASH_CANDIDATES)
        if(EXISTS "${_astock_xxhash_candidate}/xxhash.c"
           AND EXISTS "${_astock_xxhash_candidate}/xxhash.h")
            set(ASTOCK_XXHASH_LOCAL_SOURCE_DIR "${_astock_xxhash_candidate}")
            break()
        endif()
    endforeach()
    if(NOT ASTOCK_XXHASH_LOCAL_SOURCE_DIR)
        message(FATAL_ERROR "xxhash local source not found under G:/C++/lib. Expected one of: ${ASTOCK_XXHASH_CANDIDATES}")
    endif()

    if(NOT TARGET astock_xxhash)
        add_library(astock_xxhash STATIC "${ASTOCK_XXHASH_LOCAL_SOURCE_DIR}/xxhash.c")
        target_include_directories(astock_xxhash PUBLIC "${ASTOCK_XXHASH_LOCAL_SOURCE_DIR}")
    endif()

    if(NOT TARGET astock_perf_thirdparty)
        add_library(astock_perf_thirdparty INTERFACE)

        if(TARGET fmt::fmt-header-only)
            target_link_libraries(astock_perf_thirdparty INTERFACE fmt::fmt-header-only)
        elseif(TARGET fmt::fmt)
            target_link_libraries(astock_perf_thirdparty INTERFACE fmt::fmt)
        endif()

        target_link_libraries(astock_perf_thirdparty INTERFACE astock_unordered_dense)

        target_link_libraries(astock_perf_thirdparty INTERFACE astock_xxhash)
    endif()

    message(STATUS "Astock perf third-party integrated from local sources only: fmt, unordered_dense, xxhash")
endif()

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
set(ARROW_UTF8PROC_USE_SHARED OFF CACHE BOOL "" FORCE)

# ==================== 本地预编译第三方库（禁止网络下载） ====================
# 使用相对路径 ${CMAKE_SOURCE_DIR}/../lib/ 指向 G:/C++/lib/
# 在 Arrow find_package 之前预建 IMPORTED 目标，Arrow 的 Find 模块看到已有目标直接跳过

# re2（Google 正则引擎）
set(re2Alt_FOUND TRUE CACHE BOOL "re2 found (local prebuilt)" FORCE)
if(NOT TARGET re2::re2)
    add_library(re2::re2 STATIC IMPORTED)
    set_target_properties(re2::re2 PROPERTIES
        IMPORTED_LOCATION_DEBUG      "${CMAKE_SOURCE_DIR}/../lib/re2-2022-06-01/lib/Debug/re2.lib"
        IMPORTED_LOCATION_RELEASE    "${CMAKE_SOURCE_DIR}/../lib/re2-2022-06-01/lib/Release/re2.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/../lib/re2-2022-06-01"
    )
endif()
set(re2_SOURCE "SYSTEM" CACHE STRING "Use local re2" FORCE)

# utf8proc（UTF-8 规范化）
set(utf8proc_FOUND TRUE CACHE BOOL "utf8proc found (local prebuilt)" FORCE)
if(NOT TARGET utf8proc::utf8proc)
    add_library(utf8proc::utf8proc STATIC IMPORTED)
    set_target_properties(utf8proc::utf8proc PROPERTIES
        IMPORTED_LOCATION_DEBUG      "${CMAKE_SOURCE_DIR}/../lib/utf8proc-2.7.0/lib/Debug/utf8proc_static.lib"
        IMPORTED_LOCATION_RELEASE    "${CMAKE_SOURCE_DIR}/../lib/utf8proc-2.7.0/lib/Release/utf8proc_static.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/../lib/utf8proc-2.7.0"
        INTERFACE_COMPILE_DEFINITIONS "UTF8PROC_STATIC"
    )
endif()
set(utf8proc_SOURCE "SYSTEM" CACHE STRING "Use local utf8proc" FORCE)

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

if(NOT _astock_arrow_local_source_dir)
    message(FATAL_ERROR "Arrow local source not found under G:/C++/lib. Expected one of: ${ARROW_LOCAL_SOURCE_DIR_HINTS}")
endif()

set(FETCHCONTENT_SOURCE_DIR_ARROW "${_astock_arrow_local_source_dir}" CACHE PATH "Local Arrow source directory" FORCE)
message(STATUS "Using local Arrow source directory: ${_astock_arrow_local_source_dir}")
unset(_astock_arrow_candidate)
unset(_astock_arrow_local_source_dir)

set(BOOST_LOCAL_SOURCE_DIR "G:/C++/lib/boost")
if(NOT EXISTS "${BOOST_LOCAL_SOURCE_DIR}/boost/multiprecision/cpp_int.hpp")
    message(FATAL_ERROR "Boost local source not found at ${BOOST_LOCAL_SOURCE_DIR}. Expected boost headers under G:/C++/lib/boost")
endif()

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

# Boost 完全使用本地源码，不进行网络下载
FetchContent_Declare(boost SOURCE_DIR "${BOOST_LOCAL_SOURCE_DIR}")
FetchContent_GetProperties(boost)
if(NOT boost_POPULATED)
    FetchContent_Populate(boost)
endif()

# Arrow 完全使用本地源码，不进行网络下载。
# re2/utf8proc 已在上面通过 IMPORTED 目标提供，Arrow 的 find_package 直接跳过。
FetchContent_Declare(arrow SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_ARROW}")
FetchContent_GetProperties(arrow)
if(NOT arrow_POPULATED)
    FetchContent_Populate(arrow)
    add_subdirectory(${arrow_SOURCE_DIR}/cpp ${arrow_BINARY_DIR})
endif()

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

if(NOT _astock_eigen_local_source_dir)
    message(FATAL_ERROR "Eigen local source not found under G:/C++/lib. Expected one of: ${EIGEN_LOCAL_SOURCE_DIR_HINTS}")
endif()

message(STATUS "Using local Eigen source directory: ${_astock_eigen_local_source_dir}")
include_directories(${_astock_eigen_local_source_dir})
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
set(ENABLE_JUJIN_MARKET ON)

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
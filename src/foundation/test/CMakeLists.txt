cmake_minimum_required(VERSION 3.15)
project(FoundationTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# main.cpp
add_executable(test_main main.cpp)

# 添加 Foundation 库
add_subdirectory(foundation foundation_build)

target_link_libraries(test_main PRIVATE foundation)

# include 目录
target_include_directories(test_main PRIVATE ../foundation/include)

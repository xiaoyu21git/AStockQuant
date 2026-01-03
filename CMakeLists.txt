
cmake_minimum_required(VERSION 3.15)

project(AstockQuantEngine
VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "$ENV{CMAKE_PREFIX_PATH}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# 动态库文件目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 可执行文件目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)


#windows系统动态库生成lib文件命令
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_subdirectory(src)
#add_subdirectory(Qml)
# add_subdirectory(data)
# add_subdirectory(astock_engine)
# if(WIN32)
#     #add_subdirectory(Astock_console-ui)

# else()
#     add_subdirectory(Astock_console-ui)
# endif()

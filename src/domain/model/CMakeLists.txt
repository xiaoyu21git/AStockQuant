
add_library(domain_model STATIC)
add_subdirectory(src)

# 添加MSVC的/Zc:__cplusplus编译器标志（Qt需要）
if(MSVC)
    target_compile_options(domain_model PRIVATE "/Zc:__cplusplus")
endif()

# 链接Qt Core（因为使用了QString）
find_package(Qt6 REQUIRED COMPONENTS Core)
target_link_libraries(domain_model
    PRIVATE
        foundation
)
target_include_directories(domain_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

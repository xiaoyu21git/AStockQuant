# optimization/CMakeLists.txt
add_library(domain_optimization STATIC)
add_subdirectory(src)

# 设置C++标准
set_target_properties(domain_optimization PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# 对于MSVC，添加/utf-8标志
if(MSVC)
    target_compile_options(domain_optimization PRIVATE "/Zc:__cplusplus" "/permissive-")
endif()

# 依赖: 仅依赖领域层 (零Qt)
target_link_libraries(domain_optimization
    PUBLIC
        domain_strategies
        domain_strategy_runtime
    PRIVATE
        foundation
)

target_include_directories(domain_optimization
    PUBLIC
        include
    PRIVATE
        src
)

# optimization/src/CMakeLists.txt
target_sources(domain_optimization PRIVATE
    ParamRange.cpp
    ParameterSpace.cpp
    ParameterSpaceFactory.cpp
    ObjectiveSpec.cpp
    GridSearchOptimizer.cpp
    BayesianOptimizer.cpp
)

# 设置C++标准
set_target_properties(domain_optimization PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# 添加包含目录
target_include_directories(domain_optimization PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# attribution/src/CMakeLists.txt
target_sources(domain_attribution PRIVATE
    AttributionAnalyzer.cpp
)

# 设置C++标准
set_target_properties(domain_attribution PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# 添加包含目录
target_include_directories(domain_attribution PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(foundation_yaml STATIC)
add_subdirectory(src)
# target_sources(foundation_yaml
#     PRIVATE
#         src/YamlManager.cpp
# )

find_package(yaml-cpp CONFIG REQUIRED)
# 如果用到了第三方
target_link_libraries(foundation_yaml
    PUBLIC
        yaml-cpp
)
target_include_directories(foundation_yaml
    PUBLIC
        include
    PRIVATE
        src
)